/**
 * @file ldap_query.c
 * @brief LDAP query implementation with GSSAPI authentication
 */

#include "ldap_query.h"
#include "dc_discovery.h"
#include <ldap.h>
#include <sasl/sasl.h>
#include <krb5.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <ctype.h>
#include <syslog.h>

/* Active Directory attributes */
#define ATTR_PWD_EXPIRY "msDS-UserPasswordExpiryTimeComputed"
#define ATTR_PWD_LAST_SET "pwdLastSet"
#define ATTR_USER_ACCOUNT_CONTROL "userAccountControl"

/* AD UserAccountControl flags */
#define UF_DONT_EXPIRE_PASSWD 0x10000

/**
 * Convert Windows FILETIME to Unix timestamp
 * FILETIME is 100-nanosecond intervals since January 1, 1601
 */
static time_t filetime_to_unix(const char *filetime_str) {
    unsigned long long filetime = strtoull(filetime_str, NULL, 10);

    /* Special values in AD */
    if (filetime == 0 || filetime == 0x7FFFFFFFFFFFFFFF) {
        return 0;  /* Never expires or not set */
    }

    /* Convert from Windows FILETIME epoch to Unix epoch */
    const unsigned long long EPOCH_DIFF = 116444736000000000ULL;
    if (filetime < EPOCH_DIFF) return 0;

    unsigned long long unix_time = (filetime - EPOCH_DIFF) / 10000000ULL;
    return (time_t)unix_time;
}

/**
 * Get current Kerberos principal name
 */
char *ldap_get_current_principal(void) {
    krb5_context context = NULL;
    krb5_ccache ccache = NULL;
    krb5_principal principal = NULL;
    char *principal_name = NULL;
    char *result = NULL;

    if (krb5_init_context(&context) != 0) {
        return NULL;
    }

    /* Get default credential cache */
    if (krb5_cc_default(context, &ccache) != 0) {
        krb5_free_context(context);
        return NULL;
    }

    /* Get principal from cache */
    if (krb5_cc_get_principal(context, ccache, &principal) == 0) {
        if (krb5_unparse_name(context, principal, &principal_name) == 0) {
            result = strdup(principal_name);
            krb5_free_unparsed_name(context, principal_name);
        }
        krb5_free_principal(context, principal);
    }

    krb5_cc_close(context, ccache);
    krb5_free_context(context);

    return result;
}

/**
 * Convert Kerberos realm to LDAP base DN
 * Example: EXAMPLE.COM -> DC=example,DC=com
 */
char *ldap_realm_to_basedn(const char *realm) {
    if (!realm) return NULL;

    size_t len = strlen(realm);
    /* Worst case: DC=x,DC=y,... for each char + null */
    char *basedn = malloc(len * 4 + 10);
    if (!basedn) return NULL;

    char *out = basedn;
    const char *p = realm;
    bool first = true;

    while (*p) {
        if (*p == '.') {
            p++;
            first = true;
            continue;
        }

        if (first) {
            if (out != basedn) {
                *out++ = ',';
            }
            strcpy(out, "DC=");
            out += 3;
            first = false;
        }

        *out++ = tolower(*p);
        p++;
    }
    *out = '\0';

    return basedn;
}

/**
 * Extract realm from principal name
 */
static char *extract_realm(const char *principal) {
    const char *at = strchr(principal, '@');
    if (!at) return NULL;
    return strdup(at + 1);
}

/**
 * Extract username from principal
 */
static char *extract_username(const char *principal) {
    const char *at = strchr(principal, '@');
    if (!at) return strdup(principal);

    size_t len = at - principal;
    char *username = malloc(len + 1);
    if (username) {
        memcpy(username, principal, len);
        username[len] = '\0';
    }
    return username;
}

/**
 * Get default realm from Kerberos credential cache
 */
static char *get_default_realm_from_ccache(void) {
    krb5_context context = NULL;
    krb5_ccache ccache = NULL;
    krb5_principal principal = NULL;
    char *principal_str = NULL;
    char *realm = NULL;

    if (krb5_init_context(&context) != 0) {
        return NULL;
    }

    if (krb5_cc_default(context, &ccache) != 0) {
        krb5_free_context(context);
        return NULL;
    }

    if (krb5_cc_get_principal(context, ccache, &principal) == 0) {
        if (krb5_unparse_name(context, principal, &principal_str) == 0) {
            /* Extract realm from principal (everything after @) */
            char *at = strchr(principal_str, '@');
            if (at) {
                realm = strdup(at + 1);
            }
            krb5_free_unparsed_name(context, principal_str);
        }
        krb5_free_principal(context, principal);
    }

    krb5_cc_close(context, ccache);
    krb5_free_context(context);

    return realm;
}

/* GSSAPI may request an empty authorization identity even in quiet mode. */
static int sasl_defaults(LDAP *ld, unsigned flags, void *defaults, void *interact) {
    (void)ld; (void)flags; (void)defaults;
    for (sasl_interact_t *i = interact; i->id != SASL_CB_LIST_END; i++) {
        i->result = i->defresult ? i->defresult : "";
        i->len = strlen(i->result);
    }
    return LDAP_SUCCESS;
}

static LDAP *bind_uri(const char *uri, gpointer bind_data) {
    bool skip_issuer = bind_data && *(const bool *)bind_data;
    LDAP *ld = NULL;
    LDAPURLDesc *url = NULL;
    if (ldap_url_parse(uri, &url) != LDAP_URL_SUCCESS ||
        !url || !url->lud_host || !*url->lud_host ||
        (strcmp(url->lud_scheme, "ldap") && strcmp(url->lud_scheme, "ldaps"))) {
        syslog(LOG_ERR, "Invalid LDAP URI (a server hostname is required): %s", uri);
        if (url) ldap_free_urldesc(url);
        return NULL;
    }
    gboolean tls = !strcmp(url->lud_scheme, "ldaps");
    ldap_free_urldesc(url);
    int rc = ldap_initialize(&ld, uri);
    if (rc != LDAP_SUCCESS) {
        syslog(LOG_ERR, "ldap_initialize(%s): %s", uri, ldap_err2string(rc));
        return NULL;
    }
    int version = LDAP_VERSION3;
    struct timeval timeout = {3, 0};
    const char *secprops = tls ? "noanonymous,minssf=0,maxssf=0" : "noanonymous,minssf=128";
    if (ldap_set_option(ld, LDAP_OPT_PROTOCOL_VERSION, &version) != LDAP_OPT_SUCCESS ||
        ldap_set_option(ld, LDAP_OPT_NETWORK_TIMEOUT, &timeout) != LDAP_OPT_SUCCESS ||
        ldap_set_option(ld, LDAP_OPT_TIMEOUT, &timeout) != LDAP_OPT_SUCCESS ||
        ldap_set_option(ld, LDAP_OPT_REFERRALS, LDAP_OPT_OFF) != LDAP_OPT_SUCCESS ||
        ldap_set_option(ld, LDAP_OPT_X_SASL_NOCANON, LDAP_OPT_ON) != LDAP_OPT_SUCCESS ||
        ldap_set_option(ld, LDAP_OPT_X_SASL_SECPROPS, secprops) != LDAP_OPT_SUCCESS) {
        syslog(LOG_ERR, "Cannot configure LDAP connection: %s", uri);
        ldap_unbind_ext_s(ld, NULL, NULL); return NULL;
    }
    if (tls) {
        int verify = skip_issuer ? LDAP_OPT_X_TLS_NEVER : LDAP_OPT_X_TLS_DEMAND;
        int san = skip_issuer ? LDAP_OPT_X_TLS_NEVER : LDAP_OPT_X_TLS_ALLOW;
        int new_context = 0; /* Client context, scoped to this handle. */
        if (ldap_set_option(ld, LDAP_OPT_X_TLS_REQUIRE_CERT, &verify) != LDAP_OPT_SUCCESS ||
            ldap_set_option(ld, LDAP_OPT_X_TLS_REQUIRE_SAN, &san) != LDAP_OPT_SUCCESS ||
            ldap_set_option(ld, LDAP_OPT_X_TLS_NEWCTX, &new_context) != LDAP_OPT_SUCCESS) {
            syslog(LOG_ERR, "Cannot configure LDAPS certificate validation: %s", uri);
            ldap_unbind_ext_s(ld, NULL, NULL); return NULL;
        }
        if (skip_issuer)
            syslog(LOG_WARNING, "skip-issuer=true: LDAPS certificate validation disabled for %s; GSSAPI authentication remains enabled", uri);
    }
    syslog(LOG_DEBUG, "%s security: %s", tls ? "LDAPS" : "LDAP",
           tls ? "TLS encryption; GSSAPI authentication without SASL sign/seal" : "GSSAPI authentication with SASL encryption");
    syslog(LOG_INFO, "Attempting LDAP GSSAPI bind: %s", uri);
    rc = ldap_sasl_interactive_bind_s(ld, NULL, "GSSAPI", NULL, NULL,
                                     LDAP_SASL_QUIET, sasl_defaults, NULL);
    if (rc != LDAP_SUCCESS) {
        char *diagnostic = NULL;
        ldap_get_option(ld, LDAP_OPT_DIAGNOSTIC_MESSAGE, &diagnostic);
        syslog(LOG_WARNING, "GSSAPI bind %s failed: %s (code %d)%s%s",
               uri, ldap_err2string(rc), rc, diagnostic ? ": " : "",
               diagnostic ? diagnostic : "");
        if (diagnostic) ldap_memfree(diagnostic);
        ldap_unbind_ext_s(ld, NULL, NULL); return NULL;
    }
    syslog(LOG_INFO, "LDAP connection established with GSSAPI: %s", uri);
    return ld;
}

void *ldap_init_gssapi(const char *server_uri, bool use_ldaps, bool skip_issuer) {
    char *principal = ldap_get_current_principal();
    if (!principal) {
        syslog(LOG_ERR, "No Kerberos principal in default credential cache - run kinit first");
        return NULL;
    }
    free(principal);
    if (server_uri && *server_uri) {
        /* URI scheme overrides use_ldaps; do not silently switch servers. */
        char *uri = g_strdup(server_uri);
        g_strstrip(uri);
        syslog(LOG_INFO, "Using explicit LDAP server: %s", uri);
        LDAP *ld = bind_uri(uri, &skip_issuer);
        g_free(uri);
        return ld;
    }
    char *realm = get_default_realm_from_ccache();
    if (!realm || !*realm) { free(realm); return NULL; }
    syslog(LOG_INFO, "Automatic DC discovery protocol: %s", use_ldaps ? "ldaps" : "ldap");
    GCancellable *cancel = g_cancellable_new();
    LDAP *ld = dc_discover_connect(realm, use_ldaps, bind_uri, &skip_issuer, cancel);
    g_object_unref(cancel);
    free(realm);
    return ld;
}

/**
 * Query password expiration time
 */
int ldap_query_password_expiry(void *ld_ptr, const char *base_dn,
                                ldap_result_t *result) {
    LDAP *ld = (LDAP *)ld_ptr;
    LDAPMessage *search_result = NULL;
    LDAPMessage *entry = NULL;
    char *attrs[] = {
        ATTR_PWD_EXPIRY,
        ATTR_PWD_LAST_SET,
        ATTR_USER_ACCOUNT_CONTROL,
        NULL
    };
    char *filter = NULL;
    char *base = NULL;
    int rc;

    memset(result, 0, sizeof(ldap_result_t));

    /* Get current principal */
    char *principal = ldap_get_current_principal();
    if (!principal) {
        syslog(LOG_ERR, "Failed to get current principal");
        return -1;
    }

    /* Extract username and realm */
    char *username = extract_username(principal);
    char *realm = extract_realm(principal);

    /* Determine base DN */
    if (base_dn && strlen(base_dn) > 0) {
        base = strdup(base_dn);
    } else if (realm) {
        base = ldap_realm_to_basedn(realm);
    }

    if (!base) {
        syslog(LOG_ERR, "Failed to determine base DN");
        free(principal);
        free(username);
        free(realm);
        return -1;
    }

    /* Construct search filter */
    size_t filter_len = strlen(username) + 64;
    filter = malloc(filter_len);
    snprintf(filter, filter_len,
             "(&(objectClass=user)(sAMAccountName=%s))", username);

    syslog(LOG_DEBUG, "LDAP search: base=%s, filter=%s", base, filter);

    /* Perform search */
    rc = ldap_search_ext_s(ld, base, LDAP_SCOPE_SUBTREE, filter, attrs,
                           0, NULL, NULL, NULL, 1, &search_result);

    if (rc != LDAP_SUCCESS) {
        syslog(LOG_ERR, "LDAP search failed: %s", ldap_err2string(rc));
        free(principal);
        free(username);
        free(realm);
        free(filter);
        free(base);
        return -1;
    }

    /* Get first entry */
    entry = ldap_first_entry(ld, search_result);
    if (!entry) {
        syslog(LOG_ERR, "User not found in LDAP");
        ldap_msgfree(search_result);
        free(principal);
        free(username);
        free(realm);
        free(filter);
        free(base);
        return -1;
    }

    /* Save user DN */
    char *dn = ldap_get_dn(ld, entry);
    if (dn) {
        result->user_dn = strdup(dn);
        ldap_memfree(dn);
    }

    /* Check if password never expires (UserAccountControl flag) */
    struct berval **uac_vals = ldap_get_values_len(ld, entry,
                                                     ATTR_USER_ACCOUNT_CONTROL);
    if (uac_vals && uac_vals[0]) {
        unsigned long uac = strtoul(uac_vals[0]->bv_val, NULL, 10);
        if (uac & UF_DONT_EXPIRE_PASSWD) {
            result->never_expires = true;
            syslog(LOG_INFO, "Password is set to never expire");
        }
        ldap_value_free_len(uac_vals);
    }

    /* Get password expiration time (preferred attribute) */
    struct berval **exp_vals = ldap_get_values_len(ld, entry, ATTR_PWD_EXPIRY);
    if (exp_vals && exp_vals[0]) {
        result->expiry_time = filetime_to_unix(exp_vals[0]->bv_val);
        ldap_value_free_len(exp_vals);

        if (result->expiry_time == 0) {
            result->never_expires = true;
        }
    } else {
        /* Fallback: use pwdLastSet (requires calculating with domain policy) */
        syslog(LOG_WARNING, "msDS-UserPasswordExpiryTimeComputed not available");
        result->never_expires = true;
    }

    ldap_msgfree(search_result);
    free(principal);
    free(username);
    free(realm);
    free(filter);
    free(base);

    return 0;
}

/**
 * Calculate days until expiration
 */
int ldap_days_until_expiry(time_t expiry_time) {
    if (expiry_time == 0) return -1;  /* Never expires */

    time_t now = time(NULL);
    double diff_seconds = difftime(expiry_time, now);

    return (int)(diff_seconds / 86400);  /* Convert seconds to days */
}

/**
 * Close LDAP connection
 */
void ldap_close(void *ld_ptr) {
    if (ld_ptr) {
        LDAP *ld = (LDAP *)ld_ptr;
        ldap_unbind_ext_s(ld, NULL, NULL);
    }
}

/**
 * Free LDAP result
 */
void ldap_result_free(ldap_result_t *result) {
    if (result) {
        free(result->user_dn);
        memset(result, 0, sizeof(ldap_result_t));
    }
}