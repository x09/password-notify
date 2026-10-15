#include "i18n.h"
#include <locale.h>
#include <glib.h>
#include <unistd.h>
#ifndef LOCALEDIR
#define LOCALEDIR "/usr/local/share/locale"
#endif
void i18n_init(void) {
    setlocale(LC_ALL, "");
    /* Support running a freshly built binary without make install. */
    char exe[4096];
    ssize_t n = readlink("/proc/self/exe", exe, sizeof(exe) - 1);
    char *local = NULL;
    if (n > 0) {
        exe[n] = '\0';
        char *dir = g_path_get_dirname(exe);
        local = g_build_filename(dir, "locale", NULL);
        g_free(dir);
    }
    bindtextdomain(GETTEXT_PACKAGE, local && g_file_test(local, G_FILE_TEST_IS_DIR) ? local : LOCALEDIR);
    bind_textdomain_codeset(GETTEXT_PACKAGE, "UTF-8");
    textdomain(GETTEXT_PACKAGE);
    g_free(local);
}
static char *replace_text(char *text, const char *from, const char *to) {
    char **parts = g_strsplit(text, from, -1);
    char *result = g_strjoinv(to, parts);
    g_strfreev(parts); g_free(text);
    return result;
}
/* Samba 4.21 source4/kdc/kpasswd-helper.c: retain server policy values. */
static gboolean translate_parameter(const GMatchInfo *match, GString *out, gpointer data) {
    const char *format = data;
    char *value = g_match_info_fetch(match, 1);
    char *translated = g_strdup_printf(_(format), value);
    g_string_append(out, translated);
    g_free(translated);
    g_free(value);
    return FALSE;
}
static char *translate_pattern(char *text, const char *pattern, const char *format) {
    GRegex *regex = g_regex_new(pattern, 0, 0, NULL);
    char *result = g_regex_replace_eval(regex, text, -1, 0, 0,
                                       translate_parameter, (gpointer)format, NULL);
    g_regex_unref(regex);
    g_free(text);
    return result;
}
char *i18n_translate_error(const char *message) {
    if (!message) return g_strdup(_("Password change failed"));
    char *result = g_strdup(message);
    /* Samba/Windows policy diagnostics contain domain-specific numeric limits. */
    GRegex *policy = g_regex_new(
        "Your password must be at least ([0-9]+) characters; cannot repeat any of your previous ([0-9]+) passwords; must contain capitals, numerals or punctuation; and cannot contain your account or full name; Please type a different password\\. Type a password which meets these requirements in both text boxes\\.",
        0, 0, NULL);
    GMatchInfo *match = NULL;
    if (g_regex_match(policy, result, 0, &match)) {
        char *whole = g_match_info_fetch(match, 0);
        char *minimum = g_match_info_fetch(match, 1), *history = g_match_info_fetch(match, 2);
        char *translated = g_strdup_printf(_("Your password must be at least %s characters; cannot repeat any of your previous %s passwords; must contain capitals, numerals or punctuation; and cannot contain your account or full name; Please type a different password. Type a password which meets these requirements in both text boxes."), minimum, history);
        result = replace_text(result, whole, translated);
        g_free(whole); g_free(minimum); g_free(history); g_free(translated);
    }
    if (match) g_match_info_free(match);
    g_regex_unref(policy);
    result = translate_pattern(result,
        "Password is already in password history\\. New password must not match any of your ([0-9]+) previous passwords\\.",
        N_("Password is already in password history. New password must not match any of your %s previous passwords."));
    result = translate_pattern(result,
        "Password too short, password must be at least ([0-9]+) characters long\\.",
        N_("Password too short, password must be at least %s characters long."));
    result = translate_pattern(result, "Failed to set password: ([^\r\n]+)",
        N_("Failed to set password: %s"));
    static const char *known[] = {
        N_("No such user when changing password"),
        N_("Not permitted to change password"),
        N_("Password too short"),
        N_("Password is already in password history"),
        N_("Failed to set password"),
        N_("Password changed"),
        N_("A TGT may not be used as a ticket to kpasswd"),
        N_("Failed to ascertain that ticket to kpasswd is not a TGT"),
        N_("Password does not meet complexity requirements"),
        N_("Password change rejected, password changes may not be permitted on this account, or the minimum password age may not have elapsed."),
        N_("Authentication token manipulation error"),
        N_("Authentication failure"),
        N_("Current password is incorrect"),
        N_("Password change failed"),
        N_("BAD PASSWORD"),
        N_("The password is shorter than"),
        N_("The password is too similar to the old one"),
        N_("The password is the same as the old one"),
        N_("Password has been already used"),
        N_("Password unchanged"),
        N_("Permission denied"),
        N_("Cannot contact any KDC for requested realm"),
        N_("Preauthentication failed"),
        N_("Password incorrect"),
        N_("Insufficient access"),
        N_("Constraint violation"),
        N_("Can't contact LDAP server"),
        N_("Invalid credentials"),
        N_("Server is unwilling to perform"),
        N_("(current) UNIX password:"), N_("(current) NT password:"),
        N_("Enter new NT password:"), N_("Retype new NT password:"),
        N_("New password:"), N_("Retype new password:"),
        N_("too short"), N_("too simple"), N_("incorrect password")
    };
    for (size_t i = 0; i < G_N_ELEMENTS(known); i++)
        result = replace_text(result, known[i], _(known[i]));
    return result;
}
