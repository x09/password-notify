/**
 * @file config.c
 * @brief Configuration parser implementation
 */

#include "config.h"
#include <stdio.h>
#include <ctype.h>
#include <stdlib.h>
#include <string.h>
#include <sys/types.h>
#include <pwd.h>
#include <unistd.h>

/* Simple INI parser macros */
#define MAX_LINE 512
#define IS_WHITESPACE(c) ((c) == ' ' || (c) == '\t')
#define SKIP_WHITESPACE(p) while (IS_WHITESPACE(*(p))) (p)++

/**
 * Get full path to user config file
 */
static char *get_user_config_path(void) {
    const char *home = getenv("HOME");
    if (!home) {
        struct passwd *pw = getpwuid(getuid());
        if (pw) home = pw->pw_dir;
    }

    if (!home) return NULL;

    size_t len = strlen(home) + strlen(USER_CONFIG_PATH) + 2;
    char *path = malloc(len);
    if (path) {
        snprintf(path, len, "%s/%s", home, USER_CONFIG_PATH);
    }
    return path;
}

/**
 * Trim whitespace from string
 */
static void trim(char *str) {
    char *start = str;
    while (*start && isspace((unsigned char)*start)) start++;
    if (start != str) memmove(str, start, strlen(start) + 1);
    size_t len = strlen(str);
    while (len && isspace((unsigned char)str[len - 1])) str[--len] = '\0';
}

/**
 * Parse boolean value
 */
static bool parse_bool(const char *value) {
    return (strcasecmp(value, "yes") == 0 ||
            strcasecmp(value, "true") == 0 ||
            strcasecmp(value, "1") == 0);
}

/**
 * Parse notification urgency
 */
static notify_urgency_t parse_urgency(const char *value) {
    if (strcasecmp(value, "low") == 0) return NOTIFY_LOW;
    if (strcasecmp(value, "critical") == 0) return NOTIFY_CRITICAL;
    return NOTIFY_NORMAL;
}

/**
 * Parse password method
 */
static password_method_t parse_method(const char *value) {
    if (strcasecmp(value, "passwd") == 0) return PWD_METHOD_PASSWD;
    return PWD_METHOD_SAMBA;
}

/**
 * Set default configuration values
 */
void config_defaults(config_t *config) {
    memset(config, 0, sizeof(config_t));
    config->warn_days = DEFAULT_WARN_DAYS;
    config->check_interval_hours = DEFAULT_CHECK_INTERVAL_HOURS;
    config->use_ldaps = true;
    config->skip_issuer = false;
    config->pwd_method = PWD_METHOD_SAMBA;
    config->always_notify = true;
    config->notify_urgency = NOTIFY_NORMAL;
}

/**
 * Parse configuration file
 */
int config_parse(config_t *config, const char *config_path) {
    FILE *fp = NULL;
    char line[MAX_LINE];
    char section[64] = "";
    char *user_config = NULL;

    /* Set defaults */
    config_defaults(config);

    /* Determine config file path */
    if (config_path) {
        fp = fopen(config_path, "r");
    } else {
        /* Try user config first */
        user_config = get_user_config_path();
        if (user_config) {
            fp = fopen(user_config, "r");
        }

        /* Fallback to system config */
        if (!fp) {
            fp = fopen(DEFAULT_CONFIG_PATH, "r");
        }
    }

    if (!fp) {
        /* No config file - use defaults */
        free(user_config);
        return 0;
    }

    /* Parse INI file */
    while (fgets(line, sizeof(line), fp)) {
        char *p = line;

        /* Skip whitespace */
        SKIP_WHITESPACE(p);

        /* Skip comments and empty lines */
        if (*p == '#' || *p == ';' || *p == '\0' || *p == '\n') {
            continue;
        }

        /* Section header */
        if (*p == '[') {
            char *end = strchr(p, ']');
            if (end) {
                *end = '\0';
                strncpy(section, p + 1, sizeof(section) - 1);
                section[sizeof(section) - 1] = '\0';
                trim(section);
            }
            continue;
        }

        /* Key = Value */
        char *eq = strchr(p, '=');
        if (eq) {
            *eq = '\0';
            char *key = p;
            char *value = eq + 1;

            trim(key);
            trim(value);

            /* Remove trailing newline */
            char *nl = strchr(value, '\n');
            if (nl) *nl = '\0';

            /* Parse based on section */
            if (strcmp(section, "general") == 0) {
                if (strcmp(key, "warn_days") == 0) {
                    config->warn_days = atoi(value);
                } else if (strcmp(key, "check_interval_hours") == 0) {
                    config->check_interval_hours = atoi(value);
                }
            } else if (strcmp(section, "ldap") == 0) {
                if (strcmp(key, "server_uri") == 0 && strlen(value) > 0) {
                    config->server_uri = strdup(value);
                } else if (strcmp(key, "base_dn") == 0 && strlen(value) > 0) {
                    config->base_dn = strdup(value);
                } else if (strcmp(key, "use_ldaps") == 0) {
                    config->use_ldaps = parse_bool(value);
                } else if (strcmp(key, "skip-issuer") == 0) {
                    config->skip_issuer = parse_bool(value);
                }
            } else if (strcmp(section, "password_change") == 0) {
                if (strcmp(key, "method") == 0) {
                    config->pwd_method = parse_method(value);
                } else if (strcmp(key, "dc_hostname") == 0 && strlen(value) > 0) {
                    config->dc_hostname = strdup(value);
                }
            } else if (strcmp(section, "ui") == 0) {
                if (strcmp(key, "always_notify") == 0) {
                    config->always_notify = parse_bool(value);
                } else if (strcmp(key, "notification_urgency") == 0) {
                    config->notify_urgency = parse_urgency(value);
                }
            }
        }
    }

    if (config->check_interval_hours < 1 || config->check_interval_hours > 720)
        config->check_interval_hours = DEFAULT_CHECK_INTERVAL_HOURS;
    if (config->warn_days < 0) config->warn_days = DEFAULT_WARN_DAYS;

    fclose(fp);
    free(user_config);

    return 0;
}

/**
 * Free configuration resources
 */
void config_free(config_t *config) {
    if (config) {
        free(config->server_uri);
        free(config->base_dn);
        free(config->dc_hostname);
        memset(config, 0, sizeof(config_t));
    }
}