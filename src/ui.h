/**
 * @file ui.h
 * @brief User interface: AppIndicator/StatusIcon tray and GTK dialogs
 */

#ifndef UI_H
#define UI_H

#include <gtk/gtk.h>

/* Conditional includes based on tray backend */
#ifdef USE_STATUS_ICON
/* Use GtkStatusIcon (deprecated but works everywhere) */
#else
/* Use AppIndicator (modern, requires libayatana-appindicator3) */
#include <libayatana-appindicator/app-indicator.h>
#endif

#include "config.h"

/**
 * UI context structure
 */
typedef struct {
    GtkApplication *app;              /**< GTK application */

#ifdef USE_STATUS_ICON
    GtkStatusIcon *status_icon;       /**< GtkStatusIcon tray icon (legacy) */
#else
    AppIndicator *indicator;          /**< AppIndicator tray icon (modern) */
#endif

    GtkWidget *menu;                  /**< Tray menu */

    /* State */
    int days_until_expiry;            /**< Days until password expiration */
    time_t expiry_time;               /**< Password expiration timestamp */
    bool notification_shown;          /**< Whether notification was shown */

    /* Configuration */
    config_t *config;                 /**< Configuration reference */

    /* Timer and asynchronous check state */
    guint initial_check_id;
    bool check_in_progress;
    bool check_failed;
    bool scheduled_mode;
    int exit_status;
    guint check_timer_id;             /**< Timer ID for periodic checks */
} ui_context_t;

/**
 * Initialize UI (GTK application and AppIndicator)
 * @param argc Pointer to argc
 * @param argv Pointer to argv
 * @param config Configuration
 * @return UI context on success, NULL on error
 */
ui_context_t *ui_init(int *argc, char ***argv, config_t *config);

/**
 * Start UI main loop
 * @param ctx UI context
 * @return Exit status
 */
int ui_run(ui_context_t *ctx);

/**
 * Show password expiration notification
 * @param ctx UI context
 * @param days_until_expiry Days until password expires
 */
void ui_show_notification(ui_context_t *ctx, int days_until_expiry);

/**
 * Update tray icon state
 * @param ctx UI context
 * @param days_until_expiry Days until password expires (-1 = no expiry)
 */
void ui_update_indicator(ui_context_t *ctx, int days_until_expiry);

/**
 * Show password change dialog
 * @param ctx UI context
 * @return TRUE if password was changed successfully
 */
gboolean ui_show_password_dialog(ui_context_t *ctx);

/**
 * Perform password expiration check
 * Called by timer and on startup
 * @param ctx UI context
 * @return TRUE if check was successful
 */
gboolean ui_check_password_expiry(ui_context_t *ctx);

/**
 * Cleanup and free UI context
 * @param ctx UI context
 */
void ui_cleanup(ui_context_t *ctx);

/**
 * Convert notification urgency to libnotify urgency
 * @param urgency Configuration urgency level
 * @return NotifyUrgency value
 */
int ui_urgency_to_notify(notify_urgency_t urgency);

#endif /* UI_H */
