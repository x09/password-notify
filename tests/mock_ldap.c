#include "../src/ldap_query.h"
#include <glib.h>
#include <syslog.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
void *ldap_init_gssapi(const char *uri, bool tls, bool skip_issuer) {
    (void)uri; (void)tls; (void)skip_issuer;
    if (getenv("TEST_EXPIRY_STATE")) return (void *)1;
    syslog(LOG_INFO, "TEST: blocked network worker started");
    g_usleep(30 * G_USEC_PER_SEC);
    return NULL;
}
int ldap_query_password_expiry(void *ld, const char *base, ldap_result_t *result) {
    (void)ld; (void)base;
    const char *state = getenv("TEST_EXPIRY_STATE");
    if (!state || !strcmp(state, "error")) return -1;
    memset(result, 0, sizeof(*result));
    result->never_expires = !strcmp(state, "never");
    result->expiry_time = time(NULL) + atoi(state) * 86400 + 10;
    return 0;
}
void ldap_close(void *ld) { (void)ld; }
void ldap_result_free(ldap_result_t *result) { free(result->user_dn); }
int ldap_days_until_expiry(time_t when) { return (int)(difftime(when, time(NULL)) / 86400); }
char *ldap_get_current_principal(void) { return strdup("test@EXAMPLE"); }