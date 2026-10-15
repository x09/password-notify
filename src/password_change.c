#include "i18n.h"
/**
 * @file password_change.c
 * @brief Password change implementation using Kerberos and LDAP
 *
 * Primary method: kpasswd (Kerberos Change Password Protocol)
 * Fallback: LDAP modify (unicodePwd attribute)
 * Last resort: system passwd command
 */

#include "password_change.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <sys/wait.h>
#include <sys/types.h>
#include <fcntl.h>
#include <syslog.h>
#include <errno.h>
#include <ctype.h>
#include <glib.h>
#include <poll.h>
#include <termios.h>
#include <sys/ioctl.h>
#include <signal.h>

/* Kerberos */
#include <krb5.h>

/* LDAP for fallback */
#include <ldap.h>

/**
 * Get error message for result code
 */
const char *password_change_strerror(pwd_change_result_t result) {
    switch (result) {
        case PWD_CHANGE_SUCCESS:
            return _("Password changed successfully");
        case PWD_CHANGE_ERROR_AUTH:
            return _("Authentication failed - incorrect current password");
        case PWD_CHANGE_ERROR_POLICY:
            return _("New password does not meet domain policy requirements");
        case PWD_CHANGE_ERROR_CONNECTION:
            return _("Cannot connect to domain controller");
        case PWD_CHANGE_ERROR_OTHER:
        default:
            return _("Password change failed");
    }
}

/**
 * Parse username to extract realm and principal
 * Supports: user@REALM, DOMAIN\user, user
 */
static void parse_username(const char *username, char **realm, char **user) {
    *realm = NULL;
    *user = NULL;

    if (!username) return;

    /* Check for user@REALM format (preferred) */
    const char *at = strchr(username, '@');
    if (at) {
        size_t user_len = at - username;
        *user = strndup(username, user_len);
        *realm = strdup(at + 1);
        return;
    }

    /* Check for DOMAIN\user format */
    const char *backslash = strchr(username, '\\');
    if (backslash) {
        size_t domain_len = backslash - username;
        *realm = strndup(username, domain_len);
        *user = strdup(backslash + 1);
        return;
    }

    /* Just username */
    *user = strdup(username);
}

/**
 * Change password using Kerberos kpasswd protocol
 * This is the standard method for AD/Kerberos environments
 */
pwd_change_result_t password_change_kpasswd(
    const char *username,
    const char *old_password,
    const char *new_password,
    char **error_msg
) {
    krb5_context context = NULL;
    krb5_error_code ret;
    krb5_principal principal = NULL;
    krb5_get_init_creds_opt *options = NULL;
    krb5_creds creds;
    int result_code;
    krb5_data result_code_string, result_string;
    pwd_change_result_t result = PWD_CHANGE_ERROR_OTHER;

    memset(&creds, 0, sizeof(creds));
    memset(&result_code_string, 0, sizeof(result_code_string));
    memset(&result_string, 0, sizeof(result_string));

    /* Initialize Kerberos context */
    ret = krb5_init_context(&context);
    if (ret) {
        if (error_msg) {
            *error_msg = strdup(_("Failed to initialize Kerberos context"));
        }
        return PWD_CHANGE_ERROR_OTHER;
    }

    /* Parse principal name */
    ret = krb5_parse_name(context, username, &principal);
    if (ret) {
        if (error_msg) {
            const char *err = krb5_get_error_message(context, ret);
            asprintf(error_msg, _("Invalid username format: %s"), err);
            krb5_free_error_message(context, err);
        }
        krb5_free_context(context);
        return PWD_CHANGE_ERROR_OTHER;
    }

    /* Get initial credentials with old password */
    ret = krb5_get_init_creds_opt_alloc(context, &options);
    if (ret) {
        if (error_msg) {
            *error_msg = strdup(_("Failed to allocate credentials options"));
        }
        krb5_free_principal(context, principal);
        krb5_free_context(context);
        return PWD_CHANGE_ERROR_OTHER;
    }

    ret = krb5_get_init_creds_password(
        context,
        &creds,
        principal,
        (char *)old_password,
        NULL,  /* prompter */
        NULL,  /* data */
        0,     /* start_time */
        "kadmin/changepw", /* Initial ticket for password change, not a TGT. */
        options
    );

    if (ret) {
        if (error_msg) {
            const char *err = krb5_get_error_message(context, ret);
            if (ret == KRB5KDC_ERR_PREAUTH_FAILED ||
                ret == KRB5KRB_AP_ERR_BAD_INTEGRITY) {
                *error_msg = strdup(_("Current password is incorrect"));
                result = PWD_CHANGE_ERROR_AUTH;
            } else if (ret == KRB5_KDC_UNREACH) {
                asprintf(error_msg, _("Cannot reach KDC: %s"), err);
                result = PWD_CHANGE_ERROR_CONNECTION;
            } else {
                asprintf(error_msg, _("Authentication failed: %s"), err);
            }
            krb5_free_error_message(context, err);
        }
        krb5_get_init_creds_opt_free(context, options);
        krb5_free_principal(context, principal);
        krb5_free_context(context);
        return result;
    }

    /* Change password using kpasswd protocol */
    ret = krb5_change_password(
        context,
        &creds,
        (char *)new_password,
        &result_code,
        &result_code_string,
        &result_string
    );

    krb5_free_cred_contents(context, &creds);

    if (ret) {
        if (error_msg) {
            const char *err = krb5_get_error_message(context, ret);
            if (ret == KRB5_KDC_UNREACH) {
                asprintf(error_msg, _("Cannot reach password service: %s"), err);
                result = PWD_CHANGE_ERROR_CONNECTION;
            } else {
                asprintf(error_msg, _("Password change failed: %s"), err);
            }
            krb5_free_error_message(context, err);
        }
    } else if (result_code) {
        /* Password change protocol returned an error */
        if (error_msg) {
            if (result_string.length > 0) {
                char *raw = strndup(result_string.data, result_string.length);
                *error_msg = i18n_translate_error(raw);
                free(raw);
            } else {
                asprintf(error_msg, _("Password change failed with code %d"), result_code);
            }
        }

        /* Map result code to our enum */
        switch (result_code) {
            case KRB5_KPASSWD_SOFTERROR: /* Policy rejection */
                result = PWD_CHANGE_ERROR_POLICY;
                break;
            case KRB5_KPASSWD_AUTHERROR:
                result = PWD_CHANGE_ERROR_AUTH;
                break;
            default:
                result = PWD_CHANGE_ERROR_OTHER;
        }
    } else {
        /* Success */
        result = PWD_CHANGE_SUCCESS;
        if (error_msg) {
            *error_msg = NULL;
        }
        syslog(LOG_INFO, _("Password changed successfully via kpasswd for %s"), username);
    }

    /* Cleanup */
    if (result_code_string.data) {
        krb5_free_data_contents(context, &result_code_string);
    }
    if (result_string.data) {
        krb5_free_data_contents(context, &result_string);
    }
    krb5_get_init_creds_opt_free(context, options);
    krb5_free_principal(context, principal);
    krb5_free_context(context);

    return result;
}

/**
 * Change password using LDAP modify operation (fallback)
 * Modifies unicodePwd attribute - requires proper ACLs
 */
pwd_change_result_t password_change_ldap(
    const char *username,
    const char *old_password,
    const char *new_password,
    const char *ldap_uri,
    const char *base_dn,
    char **error_msg
) {
    LDAP *ld = NULL;
    int rc;
    char *user_dn = NULL;
    LDAPMod mod_old, mod_new, *mods[3];
    struct berval old_pwd_bv, new_pwd_bv;
    struct berval *old_pwd_bvs[2] = { &old_pwd_bv, NULL };
    struct berval *new_pwd_bvs[2] = { &new_pwd_bv, NULL };
    pwd_change_result_t result = PWD_CHANGE_ERROR_OTHER;
    char *realm = NULL, *user = NULL;

    parse_username(username, &realm, &user);
    if (!user) {
        if (error_msg) {
            *error_msg = strdup(_("Invalid username"));
        }
        return PWD_CHANGE_ERROR_OTHER;
    }

    /* Connect to LDAP */
    rc = ldap_initialize(&ld, ldap_uri ? ldap_uri : "ldap://localhost");
    if (rc != LDAP_SUCCESS) {
        if (error_msg) {
            asprintf(error_msg, _("LDAP connection failed: %s"), ldap_err2string(rc));
        }
        result = PWD_CHANGE_ERROR_CONNECTION;
        goto cleanup;
    }

    /* Set LDAP version 3 */
    int version = LDAP_VERSION3;
    ldap_set_option(ld, LDAP_OPT_PROTOCOL_VERSION, &version);

    /* Simple bind with user credentials */
    struct berval cred;
    cred.bv_val = (char *)old_password;
    cred.bv_len = strlen(old_password);

    /* Build user DN */
    if (base_dn) {
        asprintf(&user_dn, "cn=%s,%s", user, base_dn);
    } else {
        if (error_msg) {
            *error_msg = strdup(_("LDAP base DN not configured"));
        }
        result = PWD_CHANGE_ERROR_OTHER;
        goto cleanup;
    }

    rc = ldap_sasl_bind_s(ld, user_dn, LDAP_SASL_SIMPLE, &cred, NULL, NULL, NULL);
    if (rc != LDAP_SUCCESS) {
        if (error_msg) {
            if (rc == LDAP_INVALID_CREDENTIALS) {
                *error_msg = strdup(_("Current password is incorrect"));
                result = PWD_CHANGE_ERROR_AUTH;
            } else {
                asprintf(error_msg, _("LDAP bind failed: %s"), ldap_err2string(rc));
                result = PWD_CHANGE_ERROR_CONNECTION;
            }
        }
        goto cleanup;
    }

    /* Prepare password modification
     * AD requires unicodePwd in UTF-16LE format surrounded by quotes
     * For simplicity, we use the userPassword attribute which works with
     * many LDAP implementations
     */
    old_pwd_bv.bv_val = (char *)old_password;
    old_pwd_bv.bv_len = strlen(old_password);
    new_pwd_bv.bv_val = (char *)new_password;
    new_pwd_bv.bv_len = strlen(new_password);

    /* Delete old password */
    mod_old.mod_op = LDAP_MOD_DELETE | LDAP_MOD_BVALUES;
    mod_old.mod_type = "userPassword";
    mod_old.mod_bvalues = old_pwd_bvs;

    /* Add new password */
    mod_new.mod_op = LDAP_MOD_ADD | LDAP_MOD_BVALUES;
    mod_new.mod_type = "userPassword";
    mod_new.mod_bvalues = new_pwd_bvs;

    mods[0] = &mod_old;
    mods[1] = &mod_new;
    mods[2] = NULL;

    /* Perform modification */
    rc = ldap_modify_ext_s(ld, user_dn, mods, NULL, NULL);

    if (rc == LDAP_SUCCESS) {
        result = PWD_CHANGE_SUCCESS;
        if (error_msg) {
            *error_msg = NULL;
        }
        syslog(LOG_INFO, "Password changed successfully via LDAP for %s", username);
    } else {
        if (error_msg) {
            if (rc == LDAP_CONSTRAINT_VIOLATION || rc == LDAP_INVALID_SYNTAX) {
                asprintf(error_msg, _("Password policy violation: %s"), ldap_err2string(rc));
                result = PWD_CHANGE_ERROR_POLICY;
            } else if (rc == LDAP_INSUFFICIENT_ACCESS) {
                *error_msg = strdup(_("Insufficient permissions to change password via LDAP"));
                result = PWD_CHANGE_ERROR_OTHER;
            } else {
                asprintf(error_msg, _("LDAP modify failed: %s"), ldap_err2string(rc));
                result = PWD_CHANGE_ERROR_OTHER;
            }
        }
    }

cleanup:
    if (ld) {
        ldap_unbind_ext_s(ld, NULL, NULL);
    }
    free(user_dn);
    free(realm);
    free(user);

    return result;
}

/* passwd/PAM may read /dev/tty, so use a private controlling terminal.
 * Never put passwords in argv, environment, or logs. */
static gboolean passwd_write_line(int fd, const char *password) {
    size_t len = strlen(password), off = 0;
    while (off < len) {
        ssize_t n = write(fd, password + off, len - off);
        if (n < 0 && errno == EINTR) continue;
        if (n <= 0) return FALSE;
        off += n;
    }
    return write(fd, "\n", 1) == 1;
}

static char *passwd_redact(const char *text, const char *old, const char *newpw) {
    char *result = g_strdup(text);
    const char *secrets[] = {old, newpw};
    for (size_t i = 0; i < 2; i++) {
        if (!*secrets[i]) continue;
        char **parts = g_strsplit(result, secrets[i], -1);
        char *clean = g_strjoinv("[redacted]", parts);
        g_strfreev(parts); g_free(result); result = clean;
    }
    return result;
}

/* command and timeout are explicit to allow offline fake-process tests. */
static pwd_change_result_t passwd_run(const char *command, const char *old_password,
                                     const char *new_password, char **error_msg,
                                     gint64 timeout_us) {
    if (error_msg) *error_msg = NULL;
    if (!old_password || !new_password || strpbrk(old_password, "\r\n") ||
        strpbrk(new_password, "\r\n")) {
        if (error_msg) *error_msg = strdup(_("Passwords containing line breaks cannot be passed to passwd"));
        return PWD_CHANGE_ERROR_OTHER;
    }
    int master = posix_openpt(O_RDWR | O_NOCTTY | O_CLOEXEC | O_NONBLOCK);
    char slave_name[256];
    if (master < 0 || grantpt(master) || unlockpt(master) ||
        ptsname_r(master, slave_name, sizeof(slave_name))) {
        if (master >= 0) close(master);
        if (error_msg) *error_msg = strdup(_("Cannot create private terminal for passwd"));
        return PWD_CHANGE_ERROR_OTHER;
    }
    /* Disable echo before the child starts, not after the first prompt. */
    int slave = open(slave_name, O_RDWR | O_NOCTTY | O_CLOEXEC);
    struct termios term;
    if (slave < 0 || tcgetattr(slave, &term)) {
        if (slave >= 0) close(slave);
        close(master);
        if (error_msg) *error_msg = strdup(_("Cannot configure passwd terminal"));
        return PWD_CHANGE_ERROR_OTHER;
    }
    term.c_lflag &= ~(ECHO | ECHONL);
    if (tcsetattr(slave, TCSANOW, &term)) {
        close(slave); close(master);
        if (error_msg) *error_msg = strdup(_("Cannot disable password echo"));
        return PWD_CHANGE_ERROR_OTHER;
    }
    close(slave);
    /* Prepare environment before fork: caller runs in a GTK worker thread. */
    char **env = g_get_environ();
    env = g_environ_setenv(env, "LC_ALL", "C", TRUE);
    pid_t pid = fork();
    if (!pid) {
        close(master);
        if (setsid() < 0) _exit(126);
        int tty = open(slave_name, O_RDWR);
        if (tty < 0 || ioctl(tty, TIOCSCTTY, 0) < 0) _exit(126);
        if (dup2(tty, 0) < 0 || dup2(tty, 1) < 0 || dup2(tty, 2) < 0) _exit(126);
        if (tty > 2) close(tty);
        char *args[] = {(char *)command, NULL};
        execve(command, args, env);
        _exit(127);
    }
    g_strfreev(env);
    if (pid < 0) {
        close(master);
        if (error_msg) *error_msg = strdup(_("Cannot start passwd"));
        return PWD_CHANGE_ERROR_OTHER;
    }
    GString *output = g_string_new(NULL), *pending = g_string_new(NULL);
    gint64 deadline = g_get_monotonic_time() + timeout_us;
    const char *failure = NULL;
    int status = 0, responses = 0;
    gboolean reaped = FALSE;
    while (!failure) {
        if (g_get_monotonic_time() >= deadline) { failure = _("passwd dialog timed out; password change result is unknown"); break; }
        struct pollfd pfd = {master, POLLIN, 0};
        int rc = poll(&pfd, 1, 100);
        if (rc < 0 && errno == EINTR) continue;
        if (rc < 0) { failure = _("Cannot read passwd terminal"); break; }
        if (rc && (pfd.revents & (POLLIN | POLLHUP))) {
            char buf[512];
            ssize_t n = read(master, buf, sizeof(buf));
            if (n > 0) {
                g_string_append_len(output, buf, n);
                g_string_append_len(pending, buf, n);
                if (output->len > 65536) { failure = _("Unexpected excessive output from passwd"); break; }
                char *lower = g_ascii_strdown(pending->str, -1);
                g_strchomp(lower);
                /* A prompt can be split across several reads and need not end in a newline. */
                if (g_str_has_suffix(lower, ":")) {
                    char *line = strrchr(lower, '\n');
                    line = line ? line + 1 : lower;
                    if (strstr(line, "password")) {
                        const char *answer = NULL;
                        if (strstr(line, "current") || strstr(line, "old")) answer = old_password;
                        else if (strstr(line, "new") || strstr(line, "retype") ||
                                 strstr(line, "repeat") || strstr(line, "again")) answer = new_password;
                        if (!answer) failure = _("Unrecognized passwd password prompt");
                        else if (++responses > 8) failure = _("passwd repeatedly requested passwords; stopping");
                        else if (tcgetattr(master, &term) || (term.c_lflag & (ECHO | ECHONL)))
                            failure = _("passwd terminal unexpectedly enabled password echo");
                        else if (!passwd_write_line(master, answer)) failure = _("Cannot answer passwd prompt");
                        g_string_truncate(pending, 0);
                    }
                }
                g_free(lower);
                continue; /* Drain all output before interpreting the exit status. */
            }
            if (n < 0 && errno != EIO && errno != EINTR && errno != EAGAIN) { failure = _("Cannot read passwd output"); break; }
        }
        pid_t done = waitpid(pid, &status, WNOHANG);
        if (done == pid) { reaped = TRUE; break; }
        if (done < 0 && errno != EINTR) { failure = _("Cannot obtain passwd exit status"); break; }
    }
    if (!reaped) {
        kill(pid, SIGKILL);
        while (waitpid(pid, &status, 0) < 0 && errno == EINTR) {}
    }
    close(master);
    pwd_change_result_t result = PWD_CHANGE_ERROR_OTHER;
    if (!failure && WIFEXITED(status) && WEXITSTATUS(status) == 0) {
        result = PWD_CHANGE_SUCCESS;
        syslog(LOG_INFO, "Password changed successfully via passwd command");
    } else {
        char *clean = passwd_redact(output->str, old_password, new_password);
        g_strstrip(clean);
        if (error_msg) *error_msg = failure ? strdup(failure) : i18n_translate_error(*clean ? clean : _("passwd failed without a diagnostic"));
        char *lower = g_ascii_strdown(clean, -1);
        if (!failure && (strstr(lower, "bad password") || strstr(lower, "too short") ||
            strstr(lower, "too simple") || strstr(lower, "password history"))) result = PWD_CHANGE_ERROR_POLICY;
        else if (!failure && (strstr(lower, "incorrect password") || strstr(lower, "authentication failure"))) result = PWD_CHANGE_ERROR_AUTH;
        g_free(lower); g_free(clean);
    }
    g_string_free(output, TRUE); g_string_free(pending, TRUE);
    return result;
}

pwd_change_result_t password_change_passwd(const char *old_password,
                                          const char *new_password, char **error_msg) {
    return passwd_run("/usr/bin/passwd", old_password, new_password, error_msg,
                      90 * G_USEC_PER_SEC);
}

/**
 * Main password change function - tries methods in order
 */
pwd_change_result_t password_change_samba(
    const char *username,
    const char *old_password,
    const char *new_password,
    const char *dc_hostname,
    char **error_msg
) {
    pwd_change_result_t result;
    char *kpasswd_error = NULL;

    (void)dc_hostname;  /* Unused - kpasswd discovers KDC automatically */

    /* Try kpasswd first (standard method) */
    result = password_change_kpasswd(username, old_password, new_password, &kpasswd_error);

    if (result == PWD_CHANGE_SUCCESS) {
        if (error_msg) *error_msg = NULL;
        free(kpasswd_error);
        return PWD_CHANGE_SUCCESS;
    }

    /* If kpasswd failed with connection error, don't try other methods */
    if (result == PWD_CHANGE_ERROR_CONNECTION) {
        if (error_msg) {
            *error_msg = kpasswd_error;
        } else {
            free(kpasswd_error);
        }
        return result;
    }

    syslog(LOG_WARNING, _("kpasswd failed: %s"),
           kpasswd_error ? kpasswd_error : _("unknown error"));
    if (error_msg) {
        *error_msg = kpasswd_error ? kpasswd_error : strdup(password_change_strerror(result));
    } else {
        free(kpasswd_error);
    }
    return result;
}

/**
 * Change password using configured method
 */
pwd_change_result_t password_change(
    const config_t *config,
    const char *username,
    const char *old_password,
    const char *new_password,
    char **error_msg
) {
    if (config && config->pwd_method == PWD_METHOD_PASSWD) {
        syslog(LOG_INFO, "Password change method: /usr/bin/passwd (system PAM)");
        return password_change_passwd(old_password, new_password, error_msg);
    }

    /* Try domain method first */
    pwd_change_result_t result = password_change_samba(
        username,
        old_password,
        new_password,
        config ? config->dc_hostname : NULL,
        error_msg
    );

    /* If domain method fails with "other" error, try passwd as last resort */
    if (result == PWD_CHANGE_ERROR_OTHER) {
        syslog(LOG_WARNING, "Domain password change failed, trying passwd fallback");
        char *passwd_error = NULL;
        result = password_change_passwd(old_password, new_password, &passwd_error);

        if (result == PWD_CHANGE_SUCCESS) {
            if (error_msg) {
                free(*error_msg);
                *error_msg = NULL;
            }
        } else {
            if (error_msg) {
                char *combined = NULL;
                asprintf(&combined, _("Kerberos: %s; passwd: %s"),
                         *error_msg ? *error_msg : _("unknown error"),
                         passwd_error ? passwd_error : password_change_strerror(result));
                free(*error_msg);
                *error_msg = combined;
            }
            free(passwd_error);
        }
    }

    return result;
}
