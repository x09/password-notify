/**
 * @file ldap_query.h
 * @brief LDAP query module for password expiration information
 */

#ifndef LDAP_QUERY_H
#define LDAP_QUERY_H

#include <time.h>
#include <stdbool.h>

/**
 * LDAP query result
 */
typedef struct {
    time_t expiry_time;        /**< Password expiration time (Unix timestamp) */
    bool never_expires;        /**< True if password never expires */
    char *user_dn;             /**< User Distinguished Name */
} ldap_result_t;

/**
 * Initialize LDAP connection with Kerberos GSSAPI authentication
 * @param server_uri LDAP server URI (NULL for auto-discovery)
 * @param use_ldaps Use LDAPS (SSL/TLS)
 * @return LDAP handle on success, NULL on error
 */
void *ldap_init_gssapi(const char *server_uri, bool use_ldaps, bool skip_issuer);

/**
 * Query password expiration time for current user
 * @param ld LDAP handle
 * @param base_dn Base DN for search (NULL for auto-detection)
 * @param result Pointer to result structure to fill
 * @return 0 on success, -1 on error
 */
int ldap_query_password_expiry(void *ld, const char *base_dn, ldap_result_t *result);

/**
 * Calculate days until password expiration
 * @param expiry_time Expiration time from LDAP
 * @return Number of days (negative if already expired)
 */
int ldap_days_until_expiry(time_t expiry_time);

/**
 * Close LDAP connection
 * @param ld LDAP handle
 */
void ldap_close(void *ld);

/**
 * Free LDAP result structure
 * @param result Result structure to free
 */
void ldap_result_free(ldap_result_t *result);

/**
 * Get current Kerberos principal name
 * @return Principal name (caller must free), NULL on error
 */
char *ldap_get_current_principal(void);

/**
 * Convert Kerberos realm to LDAP base DN
 * Example: EXAMPLE.COM -> DC=example,DC=com
 * @param realm Kerberos realm
 * @return Base DN (caller must free), NULL on error
 */
char *ldap_realm_to_basedn(const char *realm);

#endif /* LDAP_QUERY_H */