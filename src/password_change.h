/**
 * @file password_change.h
 * @brief Password change module using Samba API or passwd fallback
 */

#ifndef PASSWORD_CHANGE_H
#define PASSWORD_CHANGE_H

#include "config.h"

/**
 * Password change result codes
 */
typedef enum {
    PWD_CHANGE_SUCCESS = 0,           /**< Password changed successfully */
    PWD_CHANGE_ERROR_AUTH = -1,       /**< Authentication failed (wrong old password) */
    PWD_CHANGE_ERROR_POLICY = -2,     /**< Password doesn't meet policy requirements */
    PWD_CHANGE_ERROR_CONNECTION = -3, /**< Cannot connect to DC */
    PWD_CHANGE_ERROR_OTHER = -4       /**< Other error */
} pwd_change_result_t;

/**
 * Change password using Samba API
 * @param username Domain username (DOMAIN\\user or user@REALM)
 * @param old_password Current password
 * @param new_password New password
 * @param dc_hostname Domain controller hostname (NULL for auto-discovery)
 * @param error_msg Pointer to store error message (caller must free), can be NULL
 * @return pwd_change_result_t code
 */
pwd_change_result_t password_change_samba(
    const char *username,
    const char *old_password,
    const char *new_password,
    const char *dc_hostname,
    char **error_msg
);

/**
 * Change password using system passwd command (fallback)
 * @param old_password Current password
 * @param new_password New password
 * @param error_msg Pointer to store error message (caller must free), can be NULL
 * @return pwd_change_result_t code
 */
pwd_change_result_t password_change_passwd(
    const char *old_password,
    const char *new_password,
    char **error_msg
);

/**
 * Change password using configured method
 * @param config Configuration
 * @param username Domain username
 * @param old_password Current password
 * @param new_password New password
 * @param error_msg Pointer to store error message (caller must free), can be NULL
 * @return pwd_change_result_t code
 */
pwd_change_result_t password_change(
    const config_t *config,
    const char *username,
    const char *old_password,
    const char *new_password,
    char **error_msg
);

/**
 * Get error message for result code
 * @param result Result code
 * @return Human-readable error message (static string)
 */
const char *password_change_strerror(pwd_change_result_t result);

#endif /* PASSWORD_CHANGE_H */
