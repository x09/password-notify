#ifndef DC_DISCOVERY_H
#define DC_DISCOVERY_H
#include <ldap.h>
#include <gio/gio.h>
typedef LDAP *(*dc_bind_fn)(const char *uri, gpointer bind_data);
LDAP *dc_discover_connect(const char *realm, gboolean use_ldaps,
                          dc_bind_fn bind, gpointer bind_data, GCancellable *cancel);
/* Returns prefix length, or -1 when the address is outside the subnet. */
int dc_subnet_prefix(const char *address, const char *subnet);
#endif