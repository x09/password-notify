/* Offline regression tests; never contact AD or change passwords. */
#include <assert.h>
#include <unistd.h>
#include <stdio.h>
#include "../src/config.h"
#include "../src/dc_discovery.c"

static int attempts;
static LDAP *fake_bind(const char *uri, gpointer bind_data) {
    (void)bind_data;
    attempts++;
    return strstr(uri, "good.example") ? (LDAP *)0x1 : NULL;
}
static void test_failover(void) {
    GPtrArray *list = g_ptr_array_new_with_free_func(candidate_free);
    for (int i=0; i<3; i++) {
        candidate_t *c = g_new0(candidate_t, 1);
        c->host = g_strdup(i == 0 ? "slow.example" : i == 1 ? "bad.example" : "good.example");
        c->port = 389; c->priority = i == 0 ? 1 : 0; c->latency = i == 1 ? 10 : 20;
        g_ptr_array_add(list, c);
    }
    g_ptr_array_sort(list, candidate_compare);
    GCancellable *cancel = g_cancellable_new();
    char *selected = NULL;
    attempts = 0;
    assert(try_list(list, FALSE, fake_bind, NULL, cancel, NULL, NULL, &selected) == (LDAP *)0x1);
    assert(attempts == 2);
    assert(!strcmp(selected, "ldap://good.example:389"));
    g_free(selected);
    g_cancellable_cancel(cancel);
    attempts = 0; selected = NULL;
    assert(try_list(list, FALSE, fake_bind, NULL, cancel, NULL, NULL, &selected) == NULL);
    assert(attempts == 0);
    g_object_unref(cancel); g_ptr_array_unref(list);
}
int main(void) {
    assert(dc_subnet_prefix("192.168.150.10", "192.168.150.0/24") == 24);
    assert(dc_subnet_prefix("192.168.171.9", "192.168.150.0/24") == -1);
    assert(dc_subnet_prefix("192.168.150.10", "192.168.0.0/16") == 16);
    assert(dc_subnet_prefix("192.168.150.10", "0.0.0.0/0") == 0);
    assert(dc_subnet_prefix("192.168.150.10", "192.168.150.10/32") == 32);
    assert(dc_subnet_prefix("2001:db8:1::1", "2001:db8:1::/48") == 48);
    assert(dc_subnet_prefix("2001:db8:2::1", "2001:db8:1::/48") == -1);
    assert(dc_subnet_prefix("2001:db8::1", "192.168.0.0/16") == -1);
    assert(dc_subnet_prefix("127.0.0.1", "127.0.0.1/33") == -1);
    assert(dc_subnet_prefix("127.0.0.1", "127.0.0.1/not-a-number") == -1);
    assert(dc_subnet_prefix("127.0.0.1", "invalid") == -1);
    test_failover();
    char path[] = "/tmp/password-notify-test-XXXXXX";
    int fd = mkstemp(path); assert(fd >= 0);
    FILE *fp = fdopen(fd, "w"); assert(fp);
    fputs("[ ldap ]\r\n server_uri =  ldap://dc.test.alt  \r\n use_ldaps = no \r\n skip-issuer = TrUe \r\n"
          " base_dn = DC=test,DC=alt \r\n[ general ]\r\n check_interval_hours = 0\r\n", fp);
    fclose(fp);
    config_t defaults; config_defaults(&defaults); assert(!defaults.skip_issuer);
    config_t c; assert(config_parse(&c, path) == 0);
    assert(!strcmp(c.server_uri, "ldap://dc.test.alt"));
    assert(!strcmp(c.base_dn, "DC=test,DC=alt"));
    assert(!c.use_ldaps);
    assert(c.skip_issuer);
    assert(c.check_interval_hours == 24);
    config_free(&c);
    const char *cases[] = {"[ldap]\nuse_ldaps = no\n", "[ldap]\nuse_ldaps = yes\n"};
    const int expected[] = {0, 1};
    for (size_t i = 0; i < sizeof(cases)/sizeof(cases[0]); i++) {
        fp = fopen(path, "w"); assert(fp);
        fputs(cases[i], fp); fclose(fp);
        int rc = config_parse(&c, path);
        assert(rc == 0); assert(c.use_ldaps == (expected[i] == 1)); config_free(&c);
    }
    unlink(path);
    puts("PASS: IPv4/IPv6 subnet matching, candidate priority/failover/cancellation, INI whitespace/CRLF/interval");
    return 0;
}