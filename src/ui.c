#include "i18n.h"
#include "scheduler.h"
/**
 * @file ui.c
 * @brief User interface implementation with AppIndicator or GtkStatusIcon
 */

#include "ui.h"
#include "ldap_query.h"
#include "password_change.h"
#include <libnotify/notify.h>
#include <syslog.h>
#include <string.h>
#include <unistd.h>
#ifndef ICONDIR
#define ICONDIR "/usr/local/share/password-notify/icons"
#endif

static char *tray_key_icon(gboolean red) {
    const char *name = red ? "password-notify-key-red.svg" : "password-notify-key-black.svg";
    char executable[4096];
    ssize_t size = readlink("/proc/self/exe", executable, sizeof(executable) - 1);
    if (size > 0) {
        executable[size] = 0;
        char *dir = g_path_get_dirname(executable);
        char *path = g_build_filename(dir, "icons", name, NULL);
        g_free(dir);
        if (g_file_test(path, G_FILE_TEST_IS_REGULAR)) return path;
        g_free(path);
    }
    return g_build_filename(ICONDIR, name, NULL);
}


/* Tray backend detection */
#ifdef USE_STATUS_ICON
#define TRAY_BACKEND "GtkStatusIcon"
#else
#define TRAY_BACKEND "AppIndicator"
#endif

/* Menu action IDs */
#define ACTION_CHANGE_PASSWORD "change-password"
#define ACTION_CHECK_NOW "check-now"
#define ACTION_ABOUT "about"
#define ACTION_QUIT "quit"

/* Seconds avoid millisecond overflow for long intervals. */
#define HOURS_TO_SECONDS(h) ((guint)(h) * 3600U)

/* Forward declarations */
static void on_change_password(GtkMenuItem *item, gpointer user_data);
static void on_check_now(GtkMenuItem *item, gpointer user_data);
static void on_about(GtkMenuItem *item, gpointer user_data);
static void on_quit(GtkMenuItem *item, gpointer user_data);
static gboolean check_timer_callback(gpointer user_data);
static gboolean initial_check_callback(gpointer user_data);

#ifdef USE_STATUS_ICON
static void status_icon_popup_menu(GtkStatusIcon *status_icon, guint button,
                                    guint activate_time, gpointer user_data);
#endif

/**
 * Convert config urgency to libnotify urgency
 */
int ui_urgency_to_notify(notify_urgency_t urgency) {
    switch (urgency) {
        case NOTIFY_LOW:
            return NOTIFY_URGENCY_LOW;
        case NOTIFY_CRITICAL:
            return NOTIFY_URGENCY_CRITICAL;
        case NOTIFY_NORMAL:
        default:
            return NOTIFY_URGENCY_NORMAL;
    }
}

#ifdef USE_STATUS_ICON
/**
 * Popup menu callback for GtkStatusIcon
 */
static void status_icon_popup_menu(GtkStatusIcon *status_icon, guint button,
                                    guint activate_time, gpointer user_data) {
    ui_context_t *ctx = (ui_context_t *)user_data;
    (void)status_icon;  /* Unused */

    gtk_menu_popup(GTK_MENU(ctx->menu), NULL, NULL,
                   gtk_status_icon_position_menu, ctx->status_icon,
                   button, activate_time);
}
#endif

/**
 * Create tray menu
 */
static GtkWidget *create_menu(ui_context_t *ctx) {
    GtkWidget *menu = gtk_menu_new();
    GtkWidget *item;

    /* Change Password */
    item = gtk_menu_item_new_with_label(_("Change Password..."));
    g_signal_connect(item, "activate", G_CALLBACK(on_change_password), ctx);
    gtk_menu_shell_append(GTK_MENU_SHELL(menu), item);

    /* Check Now */
    item = gtk_menu_item_new_with_label(_("Check Now"));
    g_signal_connect(item, "activate", G_CALLBACK(on_check_now), ctx);
    gtk_menu_shell_append(GTK_MENU_SHELL(menu), item);

    /* Separator */
    item = gtk_separator_menu_item_new();
    gtk_menu_shell_append(GTK_MENU_SHELL(menu), item);

    /* About */
    item = gtk_menu_item_new_with_label(_("About"));
    g_signal_connect(item, "activate", G_CALLBACK(on_about), ctx);
    gtk_menu_shell_append(GTK_MENU_SHELL(menu), item);

    /* Separator */
    item = gtk_separator_menu_item_new();
    gtk_menu_shell_append(GTK_MENU_SHELL(menu), item);

    /* Quit */
    item = gtk_menu_item_new_with_label(_("Quit"));
    g_signal_connect(item, "activate", G_CALLBACK(on_quit), ctx);
    gtk_menu_shell_append(GTK_MENU_SHELL(menu), item);

    gtk_widget_show_all(menu);
    return menu;
}

/**
 * Initialize UI
 */
ui_context_t *ui_init(int *argc, char ***argv, config_t *config) {
    ui_context_t *ctx = g_new0(ui_context_t, 1);
    ctx->config = config;
    ctx->days_until_expiry = -1;
    ctx->notification_shown = false;

    /* Initialize GTK */
    gtk_init(argc, argv);

    /* Initialize libnotify */
    notify_init("password-notify");

    /* Create GTK application */
    ctx->app = gtk_application_new("org.samba.password-notify",
                                    G_APPLICATION_DEFAULT_FLAGS);

    /* Create and set menu */
    ctx->menu = create_menu(ctx);

#ifdef USE_STATUS_ICON
    /* Create GtkStatusIcon (legacy, but works everywhere) */
    char *key_icon = tray_key_icon(FALSE);
    ctx->status_icon = gtk_status_icon_new_from_file(key_icon);
    g_free(key_icon);
    gtk_status_icon_set_title(ctx->status_icon, _("Password Expiration"));
    gtk_status_icon_set_tooltip_text(ctx->status_icon, _("Password expiration notifier"));

    /* Connect popup menu */
    g_signal_connect(ctx->status_icon, "popup-menu",
                     G_CALLBACK(status_icon_popup_menu), ctx);

    /* Initially hidden */
    gtk_status_icon_set_visible(ctx->status_icon, FALSE);

    syslog(LOG_INFO, "Using GtkStatusIcon tray backend (legacy)");
#else
    /* Create AppIndicator (modern) */
    char *key_icon = tray_key_icon(FALSE);
    ctx->indicator = app_indicator_new(
        "password-notify",
        key_icon,
        APP_INDICATOR_CATEGORY_SYSTEM_SERVICES
    );
    g_free(key_icon);
    app_indicator_set_label(ctx->indicator, "", "");

    /* Set indicator status - initially passive (hidden) */
    app_indicator_set_status(ctx->indicator, APP_INDICATOR_STATUS_PASSIVE);
    app_indicator_set_title(ctx->indicator, _("Password Expiration"));

    /* Set menu */
    app_indicator_set_menu(ctx->indicator, GTK_MENU(ctx->menu));

    syslog(LOG_INFO, "Using AppIndicator tray backend (modern)");
#endif

    /* Setup timer for periodic checks */
    guint interval_seconds = HOURS_TO_SECONDS(config->check_interval_hours);
    ctx->check_timer_id = g_timeout_add_seconds(interval_seconds, check_timer_callback, ctx);

    /* Perform initial check */
    ctx->initial_check_id = g_idle_add(initial_check_callback, ctx);

    syslog(LOG_INFO, "UI initialized with check interval: %d hours",
           config->check_interval_hours);

    return ctx;
}

/* GTask owns copies of configuration so shutdown does not wait for network I/O. */
typedef struct {
    char *server_uri;
    char *base_dn;
    bool use_ldaps;
    bool skip_issuer;
} expiry_request_t;

static void expiry_request_free(gpointer ptr) {
    expiry_request_t *request = ptr;
    g_free(request->server_uri); g_free(request->base_dn); g_free(request);
}
static void expiry_result_free(gpointer ptr) {
    ldap_result_t *result = ptr;
    ldap_result_free(result); g_free(result);
}
static void expiry_worker(GTask *task, gpointer source, gpointer data, GCancellable *cancel) {
    (void)source; (void)cancel;
    expiry_request_t *request = data;
    void *ld = ldap_init_gssapi(request->server_uri, request->use_ldaps, request->skip_issuer);
    if (!ld) {
        g_task_return_new_error(task, G_IO_ERROR, G_IO_ERROR_FAILED,
                               _("Cannot connect to a domain controller. Check network, Kerberos and LDAP/TLS settings."));
        return;
    }
    ldap_result_t *result = g_new0(ldap_result_t, 1);
    int rc = ldap_query_password_expiry(ld, request->base_dn, result);
    ldap_close(ld);
    if (rc != 0) {
        expiry_result_free(result);
        g_task_return_new_error(task, G_IO_ERROR, G_IO_ERROR_FAILED,
                               _("Cannot read password expiration from the domain controller."));
        return;
    }
    g_task_return_pointer(task, result, expiry_result_free);
}

static void show_check_error(ui_context_t *ctx, const char *message) {
#ifdef USE_STATUS_ICON
    gtk_status_icon_set_visible(ctx->status_icon, TRUE);
    gtk_status_icon_set_from_icon_name(ctx->status_icon, "dialog-warning-symbolic");
    gtk_status_icon_set_tooltip_text(ctx->status_icon, message);
#else
    app_indicator_set_status(ctx->indicator, APP_INDICATOR_STATUS_ACTIVE);
    app_indicator_set_icon(ctx->indicator, "dialog-warning-symbolic");
    app_indicator_set_label(ctx->indicator, "", "");
    app_indicator_set_title(ctx->indicator, _("Password status unavailable"));
#endif
    if (!ctx->check_failed) {
        NotifyNotification *n = notify_notification_new(_("Password status unavailable"), message, "dialog-warning");
        GError *error = NULL;
        notify_notification_show(n, &error);
        g_clear_error(&error); g_object_unref(n);
    }
    ctx->check_failed = TRUE;
}

/* Applies worker result on the GTK thread. */
static void apply_expiry_result(ui_context_t *ctx, ldap_result_t result) {
    int days;
    if (ctx->scheduled_mode) {
        days = result.never_expires ? ctx->config->warn_days + 1 : ldap_days_until_expiry(result.expiry_time);
        if (days > ctx->config->warn_days) {
            syslog(LOG_INFO, "No password warning required; returning to systemd timer");
            gtk_main_quit();
            return;
        }
        /* While a warning is visible, the existing tray process can refresh
         * the remaining days; systemd does not launch another instance. */
        if (!ctx->check_timer_id)
            ctx->check_timer_id = g_timeout_add_seconds(HOURS_TO_SECONDS(ctx->config->check_interval_hours), check_timer_callback, ctx);
        GError *timer_error = NULL;
        if (!scheduler_pause(&timer_error)) {
            syslog(LOG_WARNING, "%s", timer_error->message); g_clear_error(&timer_error);
        }
    }
    /* Check if password never expires */
    if (result.never_expires) {
        syslog(LOG_INFO, _("Password never expires"));
        ctx->days_until_expiry = -1;
        ctx->expiry_time = 0;

        /* Hide indicator */
#ifdef USE_STATUS_ICON
        gtk_status_icon_set_visible(ctx->status_icon, FALSE);
#else
        app_indicator_set_status(ctx->indicator, APP_INDICATOR_STATUS_PASSIVE);
#endif

        return;
    }

    /* Calculate days until expiry */
    days = ldap_days_until_expiry(result.expiry_time);
    ctx->days_until_expiry = days;
    ctx->expiry_time = result.expiry_time;

    /* Handle different expiry states */
    if (days < 0) {
        /* Password already expired */
        syslog(LOG_CRIT, "Password has already expired %d days ago!", -days);

        /* Show critical notification */
        ui_show_notification(ctx, days);

        /* Show indicator with critical status */
        ui_update_indicator(ctx, days);

        ctx->notification_shown = true;
    } else if (days == 0) {
        /* Password expires today */
        syslog(LOG_CRIT, _("Password expires TODAY!"));

        /* Always show notification for expiring today */
        ui_show_notification(ctx, days);

        /* Show indicator */
        ui_update_indicator(ctx, days);

        ctx->notification_shown = true;
    } else if (days <= ctx->config->warn_days) {
        /* Password expires soon (within warning period) */
        syslog(LOG_INFO, "Password expires in %d days", days);

        /* Show notification if not already shown or always_notify is set */
        if (ctx->config->always_notify || !ctx->notification_shown) {
            ui_show_notification(ctx, days);
            ctx->notification_shown = true;
        }

        /* Show indicator */
        ui_update_indicator(ctx, days);
    } else {
        /* Password expiry not imminent */
        syslog(LOG_DEBUG, "Password expires in %d days (> %d warning threshold)",
               days, ctx->config->warn_days);

        /* Hide indicator */
#ifdef USE_STATUS_ICON
        gtk_status_icon_set_visible(ctx->status_icon, FALSE);
#else
        app_indicator_set_status(ctx->indicator, APP_INDICATOR_STATUS_PASSIVE);
#endif
        ctx->notification_shown = false;
    }

}

static void expiry_complete(GObject *source, GAsyncResult *async, gpointer user_data) {
    (void)source;
    ui_context_t *ctx = user_data;
    GError *error = NULL;
    ldap_result_t *result = g_task_propagate_pointer(G_TASK(async), &error);
    ctx->check_in_progress = FALSE;
    if (error) {
        syslog(LOG_ERR, "%s", error->message);
        show_check_error(ctx, error->message);
        g_error_free(error);
        if (ctx->scheduled_mode) { ctx->exit_status = 1; gtk_main_quit(); }
    } else {
        ctx->check_failed = FALSE;
        apply_expiry_result(ctx, *result);
        expiry_result_free(result);
    }
}

/* Also used as a one-shot idle callback after a password change. */
gboolean ui_check_password_expiry(ui_context_t *ctx) {
    if (ctx->check_in_progress) {
        syslog(LOG_DEBUG, "Password check already running; ignoring duplicate request");
        return G_SOURCE_REMOVE;
    }
    ctx->check_in_progress = TRUE;
    syslog(LOG_DEBUG, "Checking password expiration");
    expiry_request_t *request = g_new0(expiry_request_t, 1);
    request->server_uri = g_strdup(ctx->config->server_uri);
    request->base_dn = g_strdup(ctx->config->base_dn);
    request->use_ldaps = ctx->config->use_ldaps;
    request->skip_issuer = ctx->config->skip_issuer;
    GTask *task = g_task_new(NULL, NULL, expiry_complete, ctx);
    g_task_set_task_data(task, request, expiry_request_free);
    g_task_run_in_thread(task, expiry_worker);
    g_object_unref(task);
    return G_SOURCE_REMOVE;
}

static gboolean initial_check_callback(gpointer data) {
    ui_context_t *ctx = data;
    ctx->initial_check_id = 0;
    ui_check_password_expiry(ctx);
    return G_SOURCE_REMOVE;
}

/**
 * Show notification
 */
void ui_show_notification(ui_context_t *ctx, int days_until_expiry) {
    NotifyNotification *notification;
    char *title;
    char *body;
    char time_str[64];
    struct tm *tm_info;

    /* Format expiry date */
    tm_info = localtime(&ctx->expiry_time);
    strftime(time_str, sizeof(time_str), "%B %d, %Y", tm_info);

    /* Create notification text based on days until expiry */
    if (days_until_expiry < 0) {
        /* Password already expired */
        title = _("⚠️ Password Has Expired!");
        body = g_strdup_printf(
            P_("Your password expired %d day ago (%s).\nYou must change your password NOW.\nClick 'Change Password' in the system tray.",
               "Your password expired %d days ago (%s).\nYou must change your password NOW.\nClick 'Change Password' in the system tray.", -days_until_expiry),
            -days_until_expiry, time_str
        );
    } else if (days_until_expiry == 0) {
        title = _("⚠️ Password Expires Today!");
        body = g_strdup_printf(
            _("Your password expires today (%s).\n"
            "Please change your password immediately.\n"
            "Click 'Change Password' in the system tray."),
            time_str
        );
    } else if (days_until_expiry == 1) {
        title = _("Password Expires Tomorrow!");
        body = g_strdup_printf(
            _("Your password expires tomorrow (%s).\n"
            "Please change your password soon.\n"
            "Click 'Change Password' in the system tray."),
            time_str
        );
    } else {
        title = _("Password Expiration Warning");
        body = g_strdup_printf(
            P_("Your password expires in %d day (%s).\nPlease change your password before it expires.",
               "Your password expires in %d days (%s).\nPlease change your password before it expires.", days_until_expiry),
            days_until_expiry, time_str
        );
    }

    /* Create notification */
    notification = notify_notification_new(title, body, "dialog-password-symbolic");
    /* Supply pixels as well: notification daemons may resolve theme names
     * differently from GTK (especially symbolic icons). */
    GtkIconTheme *theme = gtk_icon_theme_get_default();
    const char *icon_names[] = {"dialog-password", "dialog-password-symbolic", "dialog-warning"};
    for (size_t i = 0; i < G_N_ELEMENTS(icon_names); i++) {
        GError *icon_error = NULL;
        GdkPixbuf *pixbuf = gtk_icon_theme_load_icon(theme, icon_names[i], 48,
                                                    GTK_ICON_LOOKUP_FORCE_SIZE, &icon_error);
        g_clear_error(&icon_error);
        if (pixbuf) {
            notify_notification_set_image_from_pixbuf(notification, pixbuf);
            g_object_unref(pixbuf);
            break;
        }
    }

    /* Set urgency based on days remaining */
    NotifyUrgency urgency;
    if (days_until_expiry <= 1) {
        urgency = NOTIFY_URGENCY_CRITICAL;
    } else if (days_until_expiry <= 3) {
        urgency = NOTIFY_URGENCY_NORMAL;
    } else {
        urgency = ui_urgency_to_notify(ctx->config->notify_urgency);
    }
    notify_notification_set_urgency(notification, urgency);

    /* Set timeout - critical notifications stay longer */
    if (urgency == NOTIFY_URGENCY_CRITICAL) {
        notify_notification_set_timeout(notification, 0);  /* No timeout */
    } else {
        notify_notification_set_timeout(notification, 10000);  /* 10 seconds */
    }

    /* Show notification */
    GError *error = NULL;
    if (!notify_notification_show(notification, &error)) {
        syslog(LOG_WARNING, "Failed to show notification: %s",
               error ? error->message : _("unknown error"));
        if (error) g_error_free(error);
    } else {
        syslog(LOG_INFO, "Notification shown: %d days until expiry",
               days_until_expiry);
    }

    g_object_unref(G_OBJECT(notification));
    g_free(body);
}

/**
 * Update indicator
 */
void ui_update_indicator(ui_context_t *ctx, int days_until_expiry) {
    char *tooltip;
    if (days_until_expiry < 0) {
        tooltip = days_until_expiry == -1 ? g_strdup(_("Password EXPIRED yesterday!")) :
            g_strdup_printf(P_("Password EXPIRED %d day ago!", "Password EXPIRED %d days ago!", -days_until_expiry), -days_until_expiry);
    } else if (days_until_expiry == 0) {
        tooltip = g_strdup(_("Password expires TODAY!"));
    } else if (days_until_expiry == 1) {
        tooltip = g_strdup(_("Password expires TOMORROW"));
    } else {
        tooltip = g_strdup_printf(P_("Password expires in %d day", "Password expires in %d days", days_until_expiry), days_until_expiry);
    }
    /* Red throughout the configured warning period, including expired passwords. */
    char *icon = tray_key_icon(days_until_expiry <= ctx->config->warn_days);
#ifdef USE_STATUS_ICON
    gtk_status_icon_set_from_file(ctx->status_icon, icon);
    gtk_status_icon_set_tooltip_text(ctx->status_icon, tooltip);
    gtk_status_icon_set_visible(ctx->status_icon, TRUE);
#else
    /* Ayatana exposes title/icon description, but has no tooltip setter.
     * Never display a label beside the icon; hover presentation is host-specific. */
    app_indicator_set_label(ctx->indicator, "", "");
    app_indicator_set_title(ctx->indicator, tooltip);
    app_indicator_set_icon_full(ctx->indicator, icon, tooltip);
    app_indicator_set_status(ctx->indicator, APP_INDICATOR_STATUS_ACTIVE);
#endif
    g_free(icon);
    g_free(tooltip);
}

/**
 * Timer callback
 */
static gboolean check_timer_callback(gpointer user_data) {
    ui_context_t *ctx = (ui_context_t *)user_data;
    ui_check_password_expiry(ctx);
    return G_SOURCE_CONTINUE;  /* Keep timer active */
}

/**
 * Menu action: Change Password
 */
static void on_change_password(GtkMenuItem *item, gpointer user_data) {
    ui_context_t *ctx = (ui_context_t *)user_data;
    ui_show_password_dialog(ctx);
}

/**
 * Menu action: Check Now
 */
static void on_check_now(GtkMenuItem *item, gpointer user_data) {
    ui_context_t *ctx = (ui_context_t *)user_data;
    syslog(LOG_INFO, "Manual password check requested");
    ctx->notification_shown = false;  /* Allow notification to show again */
    ui_check_password_expiry(ctx);
}

/**
 * Menu action: About
 */
static void on_about(GtkMenuItem *item, gpointer user_data) {
    GtkWidget *dialog;

    dialog = gtk_about_dialog_new();
    gtk_about_dialog_set_program_name(GTK_ABOUT_DIALOG(dialog),
                                       _("Password Notification"));
    gtk_about_dialog_set_version(GTK_ABOUT_DIALOG(dialog), "1.0.0");
    gtk_about_dialog_set_comments(GTK_ABOUT_DIALOG(dialog),
                                   _("Password-notify monitors the expiration of your domain password, checks its status periodically using LDAP and Kerberos authentication, and shows desktop reminders when it is time to change it. The tray menu lets you check the status immediately and change your password using Kerberos or the system passwd/PAM mechanism. The application runs in your graphical session and supports English and Russian."));
    gtk_about_dialog_set_website(GTK_ABOUT_DIALOG(dialog),
                                  "https://github.com/x09/password-notify");

    gtk_dialog_run(GTK_DIALOG(dialog));
    gtk_widget_destroy(dialog);
}

/**
 * Menu action: Quit
 */
static void on_quit(GtkMenuItem *item, gpointer user_data) {
    ui_context_t *ctx = (ui_context_t *)user_data;
    (void)ctx;
    gtk_main_quit();
}

/**
 * Run UI main loop
 */
int ui_run(ui_context_t *ctx) {
    gtk_main();
    return ctx->exit_status;
}

/**
 * Cleanup UI
 */
void ui_cleanup(ui_context_t *ctx) {
    if (ctx) {
        if (ctx->initial_check_id > 0) g_source_remove(ctx->initial_check_id);
        if (ctx->check_timer_id > 0) {
            g_source_remove(ctx->check_timer_id);
        }

#ifdef USE_STATUS_ICON
        if (ctx->status_icon) {
            g_object_unref(ctx->status_icon);
        }
#else
        if (ctx->indicator) {
            g_object_unref(ctx->indicator);
        }
#endif

        if (ctx->app) {
            g_object_unref(ctx->app);
        }

        notify_uninit();
        g_free(ctx);
    }
}