/**
 * @file config.h
 * @brief Configuration file parser for password-notify
 */

#ifndef CONFIG_H
#define CONFIG_H

#include <stdbool.h>

#define DEFAULT_CONFIG_PATH "/etc/password-notify.ini"
#define USER_CONFIG_PATH ".config/password-notify.ini"
#define DEFAULT_WARN_DAYS 7
#define DEFAULT_CHECK_INTERVAL_HOURS 24

/**
 * Password change method
 */
typedef enum {
    PWD_METHOD_SAMBA,    /**< Use Kerberos password-change protocol (legacy name samba) */
    PWD_METHOD_PASSWD    /**< Use system passwd command directly */
} password_method_t;

/**
 * Notification urgency level
 */
typedef enum {
    NOTIFY_LOW,
    NOTIFY_NORMAL,
    NOTIFY_CRITICAL
} notify_urgency_t;

/**
 * Configuration structure
 */
typedef struct {
    /* General settings */
    int warn_days;                    /**< Days before expiration to warn */
    int check_interval_hours;         /**< Check interval in hours */

    /* LDAP settings */
    char *server_uri;                 /**< LDAP server URI (NULL = auto-discover) */
    char *base_dn;                    /**< Base DN (NULL = auto-detect) */
    bool skip_issuer;                 /**< Disable LDAPS certificate validation (default false) */
    bool use_ldaps;                   /**< Use LDAPS for secure connection */

    /* Password change settings */
    password_method_t pwd_method;     /**< Password change method */
    char *dc_hostname;                /**< Domain controller hostname (NULL = auto) */

    /* UI settings */
    bool always_notify;               /**< Show notifications even with tray */
    notify_urgency_t notify_urgency;  /**< Notification urgency */
} config_t;

/**
 * Parse configuration file
 * @param config Pointer to config structure to fill
 * @param config_path Path to config file (NULL = use default paths)
 * @return 0 on success, -1 on error
 */
int config_parse(config_t *config, const char *config_path);

/**
 * Free configuration resources
 * @param config Configuration structure to free
 */
void config_free(config_t *config);

/**
 * Get default configuration
 * @param config Pointer to config structure to fill with defaults
 */
void config_defaults(config_t *config);

#endif /* CONFIG_H */