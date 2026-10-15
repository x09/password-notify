/* Domain/site DNS discovery. All calls run in the expiry worker, never GTK. */
#include "dc_discovery.h"
#include <arpa/inet.h>
#include <sys/socket.h>
#include <syslog.h>
#include <string.h>
#include <stdlib.h>

#define MAX_CANDIDATES 32
typedef struct {
    char *host;
    guint16 port, priority;
    gint64 latency;
} candidate_t;

int dc_subnet_prefix(const char *address, const char *subnet) {
    char *copy = g_strdup(subnet);
    char *slash = strrchr(copy, '/'), *end = NULL;
    int result = -1;
    if (!slash) { g_free(copy); return -1; }
    *slash++ = '\0';
    long prefix = strtol(slash, &end, 10);
    GInetAddress *ip = g_inet_address_new_from_string(address);
    GInetAddress *net = g_inet_address_new_from_string(copy);
    if (ip && net && *slash && !*end && prefix >= 0 &&
        g_inet_address_get_family(ip) == g_inet_address_get_family(net)) {
        gsize size = g_inet_address_get_native_size(ip);
        if (prefix <= (long)size * 8) {
            const guint8 *a = g_inet_address_to_bytes(ip);
            const guint8 *b = g_inet_address_to_bytes(net);
            result = (int)prefix;
            for (long bit = 0; bit < prefix; bit++)
                if ((a[bit / 8] ^ b[bit / 8]) & (0x80 >> (bit % 8))) {
                    result = -1; break;
                }
        }
    }
    g_clear_object(&ip); g_clear_object(&net); g_free(copy);
    return result;
}

static char *entry_string(LDAP *ld, LDAPMessage *entry, const char *attr) {
    struct berval **values = ldap_get_values_len(ld, entry, attr);
    char *s = values && values[0] ?
        g_strndup(values[0]->bv_val, values[0]->bv_len) : NULL;
    if (values) ldap_value_free_len(values);
    return s;
}

/* Query only subnet names covering the connection's actual source address.
 * No enumeration limits or administrator privileges are needed. */
static char *client_site(LDAP *ld) {
    int fd = -1;
    struct sockaddr_storage local;
    socklen_t len = sizeof(local);
    char address[INET6_ADDRSTRLEN];
    if (ldap_get_option(ld, LDAP_OPT_DESC, &fd) != LDAP_OPT_SUCCESS ||
        fd < 0 || getsockname(fd, (struct sockaddr *)&local, &len) < 0)
        return NULL;
    const void *bytes;
    int size;
    if (local.ss_family == AF_INET) {
        bytes = &((struct sockaddr_in *)&local)->sin_addr; size = 4;
    } else if (local.ss_family == AF_INET6) {
        bytes = &((struct sockaddr_in6 *)&local)->sin6_addr; size = 16;
    } else return NULL;
    if (!inet_ntop(local.ss_family, bytes, address, sizeof(address))) return NULL;
    char *attrs[] = {"configurationNamingContext", NULL};
    LDAPMessage *res = NULL;
    struct timeval timeout = {3, 0};
    int rc = ldap_search_ext_s(ld, "", LDAP_SCOPE_BASE, "(objectClass=*)",
                              attrs, 0, NULL, NULL, &timeout, 1, &res);
    char *config = NULL;
    if (rc == LDAP_SUCCESS && ldap_first_entry(ld, res))
        config = entry_string(ld, ldap_first_entry(ld, res), attrs[0]);
    if (res) ldap_msgfree(res);
    if (!config) return NULL;
    char *base = g_strdup_printf("CN=Subnets,CN=Sites,%s", config);
    g_free(config);
    GString *filter = g_string_new("(&(objectClass=subnet)(|");
    for (int prefix = size * 8; prefix >= 0; prefix--) {
        guint8 network[16] = {0};
        memcpy(network, bytes, (gsize)size);
        for (int bit = prefix; bit < size * 8; bit++)
            network[bit / 8] &= (guint8)~(0x80 >> (bit % 8));
        char text[INET6_ADDRSTRLEN];
        inet_ntop(local.ss_family, network, text, sizeof(text));
        g_string_append_printf(filter, "(cn=%s/%d)", text, prefix);
    }
    g_string_append(filter, "))");
    char *subattrs[] = {"cn", "siteObject", NULL};
    res = NULL;
    rc = ldap_search_ext_s(ld, base, LDAP_SCOPE_ONELEVEL, filter->str,
                          subattrs, 0, NULL, NULL, &timeout, 0, &res);
    char *site_dn = NULL;
    int best = -1;
    if (rc == LDAP_SUCCESS) {
        for (LDAPMessage *entry = ldap_first_entry(ld, res); entry;
             entry = ldap_next_entry(ld, entry)) {
            char *subnet = entry_string(ld, entry, "cn");
            char *dn = entry_string(ld, entry, "siteObject");
            int prefix = subnet ? dc_subnet_prefix(address, subnet) : -1;
            if (dn && prefix > best) {
                best = prefix; g_free(site_dn); site_dn = g_strdup(dn);
            }
            g_free(subnet); g_free(dn);
        }
    } else syslog(LOG_WARNING, "Cannot read AD subnet mapping: %s", ldap_err2string(rc));
    if (res) ldap_msgfree(res);
    g_free(base); g_string_free(filter, TRUE);
    if (!site_dn) {
        syslog(LOG_INFO, "No AD site mapping for source address %s; using reachable DCs by priority/latency", address);
        return NULL;
    }
    /* Read cn rather than splitting an escaped distinguished name. */
    char *siteattrs[] = {"cn", NULL};
    res = NULL;
    rc = ldap_search_ext_s(ld, site_dn, LDAP_SCOPE_BASE, "(objectClass=site)",
                          siteattrs, 0, NULL, NULL, &timeout, 1, &res);
    char *site = NULL;
    if (rc == LDAP_SUCCESS && ldap_first_entry(ld, res))
        site = entry_string(ld, ldap_first_entry(ld, res), "cn");
    if (res) ldap_msgfree(res);
    g_free(site_dn);
    if (site) syslog(LOG_INFO, "Client AD site: %s (source address %s)", site, address);
    return site;
}

static void candidate_free(gpointer data) {
    candidate_t *c = data;
    g_free(c->host); g_free(c);
}
static gint candidate_compare(gconstpointer a, gconstpointer b) {
    const candidate_t *ca = *(candidate_t *const *)a;
    const candidate_t *cb = *(candidate_t *const *)b;
    if (ca->priority != cb->priority) return ca->priority < cb->priority ? -1 : 1;
    return ca->latency < cb->latency ? -1 : ca->latency > cb->latency;
}
static GPtrArray *candidates(const char *domain, gboolean tls, GCancellable *cancel) {
    GPtrArray *list = g_ptr_array_new_with_free_func(candidate_free);
    GResolver *resolver = g_resolver_get_default();
    GError *error = NULL;
    GList *targets = g_resolver_lookup_service(resolver, "ldap", "tcp", domain, cancel, &error);
    g_object_unref(resolver);
    if (error) {
        syslog(LOG_WARNING, "SRV lookup _ldap._tcp.%s failed: %s", domain, error->message);
        g_error_free(error);
        return list;
    }
    gint64 deadline = g_get_monotonic_time() + 10 * G_USEC_PER_SEC;
    for (GList *p = targets; p && list->len < MAX_CANDIDATES; p = p->next) {
        if (g_cancellable_is_cancelled(cancel) || g_get_monotonic_time() >= deadline) break;
        GSrvTarget *target = p->data;
        const char *name = g_srv_target_get_hostname(target);
        if (!name || !*name || !strcmp(name, ".")) continue;
        char *host = g_strdup(name);
        gsize n = strlen(host);
        if (n && host[n-1] == '.') host[n-1] = '\0';
        guint16 port = tls ? 636 : g_srv_target_get_port(target);
        GSocketClient *client = g_socket_client_new();
        g_socket_client_set_timeout(client, 1);
        gint64 start = g_get_monotonic_time();
        GSocketConnection *conn = g_socket_client_connect_to_host(client, host, port, cancel, &error);
        if (conn) {
            candidate_t *c = g_new0(candidate_t, 1);
            c->host = host; c->port = port;
            c->priority = g_srv_target_get_priority(target);
            c->latency = g_get_monotonic_time() - start;
            g_ptr_array_add(list, c);
            syslog(LOG_DEBUG, "DC candidate %s:%u priority=%u TCP=%.2f ms",
                   host, port, c->priority, c->latency / 1000.0);
            g_io_stream_close(G_IO_STREAM(conn), NULL, NULL);
            g_object_unref(conn);
        } else {
            syslog(LOG_INFO, "Skipping unreachable DC %s:%u: %s", host, port,
                   error ? error->message : "connection failed");
            g_clear_error(&error); g_free(host);
        }
        g_object_unref(client);
    }
    g_resolver_free_targets(targets);
    g_ptr_array_sort(list, candidate_compare);
    return list;
}
static LDAP *try_list(GPtrArray *list, gboolean tls, dc_bind_fn bind, gpointer bind_data,
                      GCancellable *cancel, const char *reuse_uri, LDAP *reuse,
                      char **selected) {
    for (guint i = 0; i < list->len && !g_cancellable_is_cancelled(cancel); i++) {
        candidate_t *c = g_ptr_array_index(list, i);
        char *uri = g_strdup_printf("%s://%s:%u", tls ? "ldaps" : "ldap", c->host, c->port);
        LDAP *ld = reuse_uri && !strcmp(uri, reuse_uri) ? reuse : bind(uri, bind_data);
        if (ld) {
            *selected = uri;
            syslog(LOG_INFO, "Selected working DC: %s", uri);
            return ld;
        }
        g_free(uri);
    }
    return NULL;
}
LDAP *dc_discover_connect(const char *realm, gboolean tls, dc_bind_fn bind, gpointer bind_data, GCancellable *cancel) {
    GPtrArray *domain_list = candidates(realm, tls, cancel);
    char *selected = NULL;
    LDAP *bootstrap = try_list(domain_list, tls, bind, bind_data, cancel, NULL, NULL, &selected);
    g_ptr_array_unref(domain_list);
    if (!bootstrap) {
        syslog(LOG_ERR, "No working %s DC with GSSAPI found for %s", tls ? "LDAPS" : "LDAP", realm);
        return NULL;
    }
    char *site = client_site(bootstrap);
    if (site && !g_cancellable_is_cancelled(cancel)) {
        char *domain = g_strdup_printf("%s._sites.%s", site, realm);
        GPtrArray *site_list = candidates(domain, tls, cancel);
        char *local_uri = NULL;
        LDAP *local = try_list(site_list, tls, bind, bind_data, cancel, selected, bootstrap, &local_uri);
        if (local && local != bootstrap) {
            ldap_unbind_ext_s(bootstrap, NULL, NULL); bootstrap = local;
        }
        if (!local) syslog(LOG_WARNING, "No working DC in site %s; retaining domain fallback %s", site, selected);
        g_free(local_uri); g_ptr_array_unref(site_list); g_free(domain);
    }
    g_free(site); g_free(selected);
    return bootstrap;
}