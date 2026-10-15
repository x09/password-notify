/* Test real OpenLDAP option setup, with the network bind replaced by a mock. */
#include <ldap.h>
#include <sasl/sasl.h>
#include <assert.h>
#include <string.h>
#include <stdio.h>
static ber_len_t expected_ssf;
static int binds;
static int expected_verify = LDAP_OPT_X_TLS_DEMAND;
static int fake_sasl_bind(LDAP *ld, const char *dn, const char *mechs,
                         LDAPControl **server, LDAPControl **client, unsigned flags,
                         int (*interact)(LDAP *, unsigned, void *, void *), void *defaults) {
    (void)dn; (void)server; (void)client;
    assert(!strcmp(mechs, "GSSAPI"));
    assert(flags == LDAP_SASL_QUIET);
    int verify = -1;
    assert(ldap_get_option(ld, LDAP_OPT_X_TLS_REQUIRE_CERT, &verify) == LDAP_OPT_SUCCESS);
    char *uri = NULL;
    ldap_get_option(ld, LDAP_OPT_URI, &uri);
    if (strstr(uri, "ldaps://")) assert(verify == expected_verify);
    ldap_memfree(uri);
    ber_len_t ssf = 999;
    assert(ldap_get_option(ld, LDAP_OPT_X_SASL_SSF_MIN, &ssf) == LDAP_OPT_SUCCESS);
    assert(ssf == expected_ssf);
    ber_len_t maxssf = 999;
    assert(ldap_get_option(ld, LDAP_OPT_X_SASL_SSF_MAX, &maxssf) == LDAP_OPT_SUCCESS);
    assert(expected_ssf == 0 ? maxssf == 0 : maxssf >= 128);
    sasl_interact_t prompts[2] = {{0}};
    prompts[0].id = SASL_CB_USER;
    assert(interact(ld, flags, defaults, prompts) == LDAP_SUCCESS);
    assert(prompts[0].result && prompts[0].len == 0);
    binds++;
    return LDAP_SUCCESS;
}
#define ldap_sasl_interactive_bind_s fake_sasl_bind
#include "../src/ldap_client.c"
#undef ldap_sasl_interactive_bind_s
int main(void) {
    bool skip_issuer = false;
    expected_ssf = 128;
    LDAP *ld = bind_uri("ldap://dc.test.alt", &skip_issuer);
    assert(ld);
    char *uri = NULL;
    assert(ldap_get_option(ld, LDAP_OPT_URI, &uri) == LDAP_OPT_SUCCESS);
    assert(strstr(uri, "dc.test.alt"));
    ldap_memfree(uri); ldap_close(ld);
    expected_ssf = 0;
    ld = bind_uri("ldaps://dc.test.alt", &skip_issuer);
    assert(ld); ldap_close(ld);
    skip_issuer = true;
    expected_verify = LDAP_OPT_X_TLS_NEVER;
    ld = bind_uri("ldaps://dc.test.alt", &skip_issuer);
    assert(ld); ldap_close(ld);
    skip_issuer = false;
    expected_verify = LDAP_OPT_X_TLS_DEMAND;
    ld = bind_uri("ldaps://dc.test.alt", &skip_issuer);
    assert(ld); ldap_close(ld);
    assert(bind_uri("ldap:///test.alt", &skip_issuer) == NULL);
    assert(binds == 4);
    puts("PASS: LDAP/LDAPS URI validation, SASL security properties and interaction callback (no network)");
    return 0;
}