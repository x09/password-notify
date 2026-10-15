#include "i18n.h"
/**
 * @file password_dialog.c
 * @brief GTK password change dialog
 */

#include "ui.h"
#include "ldap_query.h"
#include "password_change.h"
#include <string.h>
#include <syslog.h>

/* Dialog widgets structure */
typedef struct {
    GtkWidget *dialog;
    GtkWidget *old_password_entry;
    GtkWidget *new_password_entry;
    GtkWidget *confirm_password_entry;
    GtkWidget *info_label;
    GtkWidget *spinner;
    ui_context_t *ctx;
} password_dialog_t;

/**
 * Clear sensitive data from entry
 */
static void clear_entry_secure(GtkEntry *entry) {
    const gchar *text = gtk_entry_get_text(entry);
    if (text) {
        /* Overwrite memory */
        gsize len = strlen(text);
        gchar *mutable = g_strdup(text);
        if (mutable) {
            memset(mutable, 0, len);
            g_free(mutable);
        }
    }
    gtk_entry_set_text(entry, "");
}

/**
 * Validate password inputs
 */
static gboolean validate_passwords(password_dialog_t *dlg, char **error_msg) {
    const char *old_pwd = gtk_entry_get_text(GTK_ENTRY(dlg->old_password_entry));
    const char *new_pwd = gtk_entry_get_text(GTK_ENTRY(dlg->new_password_entry));
    const char *confirm_pwd = gtk_entry_get_text(GTK_ENTRY(dlg->confirm_password_entry));

    /* Check if old password is provided */
    if (!old_pwd || strlen(old_pwd) == 0) {
        *error_msg = _("Please enter your current password");
        return FALSE;
    }

    /* Check if new password is provided */
    if (!new_pwd || strlen(new_pwd) == 0) {
        *error_msg = _("Please enter a new password");
        return FALSE;
    }

    /* Check if passwords match */
    if (strcmp(new_pwd, confirm_pwd) != 0) {
        *error_msg = _("New passwords do not match");
        return FALSE;
    }

    /* Check minimum length */
    if (strlen(new_pwd) < 8) {
        *error_msg = _("New password must be at least 8 characters");
        return FALSE;
    }

    /* Check if new password is different from old */
    if (strcmp(old_pwd, new_pwd) == 0) {
        *error_msg = _("New password must be different from current password");
        return FALSE;
    }

    return TRUE;
}

/**
 * Show error message in dialog
 */
static void show_error(password_dialog_t *dlg, const char *message) {
    char *markup = g_markup_printf_escaped(
        _("<span foreground='red'>Error: %s</span>"), message
    );
    gtk_label_set_markup(GTK_LABEL(dlg->info_label), markup);
    g_free(markup);
}

/**
 * Show info message in dialog
 */
static void show_info(password_dialog_t *dlg, const char *message) {
    char *markup = g_markup_printf_escaped(
        "<span foreground='blue'>%s</span>", message
    );
    gtk_label_set_markup(GTK_LABEL(dlg->info_label), markup);
    g_free(markup);
}

/**
 * Show success message in dialog
 */
static void show_success(password_dialog_t *dlg, const char *message) {
    char *markup = g_markup_printf_escaped(
        "<span foreground='green'>%s</span>", message
    );
    gtk_label_set_markup(GTK_LABEL(dlg->info_label), markup);
    g_free(markup);
}

/**
 * Password change worker (runs in idle handler to avoid blocking UI)
 */
typedef struct {
    password_dialog_t *dlg;
    char *username;
    char *old_password;
    char *new_password;
    pwd_change_result_t result;
    char *error_msg;
} pwd_change_data_t;

static gboolean password_change_complete(gpointer user_data) {
    pwd_change_data_t *data = (pwd_change_data_t *)user_data;
    password_dialog_t *dlg = data->dlg;

    /* Stop spinner */
    gtk_spinner_stop(GTK_SPINNER(dlg->spinner));
    gtk_widget_set_sensitive(dlg->dialog, TRUE);

    if (data->result == PWD_CHANGE_SUCCESS) {
        show_success(dlg, _("Password changed successfully!"));
        syslog(LOG_INFO, _("Password changed successfully"));

        /* Clear entries */
        clear_entry_secure(GTK_ENTRY(dlg->old_password_entry));
        clear_entry_secure(GTK_ENTRY(dlg->new_password_entry));
        clear_entry_secure(GTK_ENTRY(dlg->confirm_password_entry));

        /* Close dialog after short delay */
        g_timeout_add(1500, (GSourceFunc)gtk_widget_destroy, dlg->dialog);

        /* Reset notification flag so it can show again after next check */
        dlg->ctx->notification_shown = FALSE;

        /* Schedule immediate recheck */
        g_idle_add((GSourceFunc)ui_check_password_expiry, dlg->ctx);
    } else {
        /* Show error */
        char *msg = i18n_translate_error(data->error_msg ? data->error_msg :
                                         password_change_strerror(data->result));
        show_error(dlg, msg);
        syslog(LOG_WARNING, _("Password change failed: %s"), msg);
        g_free(msg);
    }

    /* Cleanup */
    if (data->username) {
        memset(data->username, 0, strlen(data->username));
        g_free(data->username);
    }
    if (data->old_password) {
        memset(data->old_password, 0, strlen(data->old_password));
        g_free(data->old_password);
    }
    if (data->new_password) {
        memset(data->new_password, 0, strlen(data->new_password));
        g_free(data->new_password);
    }
    if (data->error_msg) {
        g_free(data->error_msg);
    }
    g_free(data);

    return G_SOURCE_REMOVE;
}

static gpointer password_change_worker(gpointer user_data) {
    pwd_change_data_t *data = (pwd_change_data_t *)user_data;

    /* Perform password change */
    data->result = password_change(
        data->dlg->ctx->config,
        data->username,
        data->old_password,
        data->new_password,
        &data->error_msg
    );

    /* Schedule completion callback in main thread */
    g_idle_add(password_change_complete, data);

    return NULL;
}

/**
 * Handle OK button click
 */
static void on_ok_clicked(GtkButton *button, gpointer user_data) {
    password_dialog_t *dlg = (password_dialog_t *)user_data;
    char *error_msg = NULL;
    pwd_change_data_t *data;

    /* Clear previous messages */
    gtk_label_set_text(GTK_LABEL(dlg->info_label), "");

    /* Validate inputs */
    if (!validate_passwords(dlg, &error_msg)) {
        show_error(dlg, error_msg);
        return;
    }

    /* Get username */
    char *principal = ldap_get_current_principal();
    if (!principal) {
        show_error(dlg, _("Failed to determine current user"));
        return;
    }

    /* Prepare data for worker thread */
    data = g_new0(pwd_change_data_t, 1);
    data->dlg = dlg;
    data->username = principal;
    data->old_password = g_strdup(gtk_entry_get_text(GTK_ENTRY(dlg->old_password_entry)));
    data->new_password = g_strdup(gtk_entry_get_text(GTK_ENTRY(dlg->new_password_entry)));

    /* Show progress */
    show_info(dlg, _("Changing password..."));
    gtk_spinner_start(GTK_SPINNER(dlg->spinner));
    gtk_widget_set_sensitive(dlg->dialog, FALSE);

    /* Start password change in background thread */
    g_thread_new("password-change", password_change_worker, data);
}

/**
 * Handle Cancel button click
 */
static void on_cancel_clicked(GtkButton *button, gpointer user_data) {
    password_dialog_t *dlg = (password_dialog_t *)user_data;

    /* Clear entries securely */
    clear_entry_secure(GTK_ENTRY(dlg->old_password_entry));
    clear_entry_secure(GTK_ENTRY(dlg->new_password_entry));
    clear_entry_secure(GTK_ENTRY(dlg->confirm_password_entry));

    gtk_widget_destroy(dlg->dialog);
}

/**
 * Show password change dialog
 */
gboolean ui_show_password_dialog(ui_context_t *ctx) {
    password_dialog_t *dlg = g_new0(password_dialog_t, 1);
    dlg->ctx = ctx;

    /* Create dialog */
    dlg->dialog = gtk_dialog_new_with_buttons(
        _("Change Domain Password"),
        NULL,
        GTK_DIALOG_MODAL | GTK_DIALOG_DESTROY_WITH_PARENT,
        NULL
    );

    gtk_window_set_default_size(GTK_WINDOW(dlg->dialog), 400, 300);
    gtk_window_set_resizable(GTK_WINDOW(dlg->dialog), FALSE);

    /* Get content area */
    GtkWidget *content = gtk_dialog_get_content_area(GTK_DIALOG(dlg->dialog));
    gtk_container_set_border_width(GTK_CONTAINER(content), 12);

    /* Create grid for layout */
    GtkWidget *grid = gtk_grid_new();
    gtk_grid_set_row_spacing(GTK_GRID(grid), 12);
    gtk_grid_set_column_spacing(GTK_GRID(grid), 12);
    gtk_container_add(GTK_CONTAINER(content), grid);

    /* Title label */
    GtkWidget *title_label = gtk_label_new(NULL);
    gtk_label_set_markup(GTK_LABEL(title_label),
                         _("<b>Change your domain password</b>"));
    gtk_widget_set_halign(title_label, GTK_ALIGN_START);
    gtk_grid_attach(GTK_GRID(grid), title_label, 0, 0, 2, 1);

    /* Current password */
    GtkWidget *old_label = gtk_label_new(_("Current password:"));
    gtk_widget_set_halign(old_label, GTK_ALIGN_END);
    gtk_grid_attach(GTK_GRID(grid), old_label, 0, 1, 1, 1);

    dlg->old_password_entry = gtk_entry_new();
    gtk_entry_set_visibility(GTK_ENTRY(dlg->old_password_entry), FALSE);
    gtk_entry_set_input_purpose(GTK_ENTRY(dlg->old_password_entry),
                                GTK_INPUT_PURPOSE_PASSWORD);
    gtk_widget_set_hexpand(dlg->old_password_entry, TRUE);
    gtk_grid_attach(GTK_GRID(grid), dlg->old_password_entry, 1, 1, 1, 1);

    /* New password */
    GtkWidget *new_label = gtk_label_new(_("New password:"));
    gtk_widget_set_halign(new_label, GTK_ALIGN_END);
    gtk_grid_attach(GTK_GRID(grid), new_label, 0, 2, 1, 1);

    dlg->new_password_entry = gtk_entry_new();
    gtk_entry_set_visibility(GTK_ENTRY(dlg->new_password_entry), FALSE);
    gtk_entry_set_input_purpose(GTK_ENTRY(dlg->new_password_entry),
                                GTK_INPUT_PURPOSE_PASSWORD);
    gtk_grid_attach(GTK_GRID(grid), dlg->new_password_entry, 1, 2, 1, 1);

    /* Confirm password */
    GtkWidget *confirm_label = gtk_label_new(_("Confirm password:"));
    gtk_widget_set_halign(confirm_label, GTK_ALIGN_END);
    gtk_grid_attach(GTK_GRID(grid), confirm_label, 0, 3, 1, 1);

    dlg->confirm_password_entry = gtk_entry_new();
    gtk_entry_set_visibility(GTK_ENTRY(dlg->confirm_password_entry), FALSE);
    gtk_entry_set_input_purpose(GTK_ENTRY(dlg->confirm_password_entry),
                                GTK_INPUT_PURPOSE_PASSWORD);
    gtk_grid_attach(GTK_GRID(grid), dlg->confirm_password_entry, 1, 3, 1, 1);

    /* Info label for messages */
    dlg->info_label = gtk_label_new("");
    gtk_label_set_line_wrap(GTK_LABEL(dlg->info_label), TRUE);
    gtk_widget_set_halign(dlg->info_label, GTK_ALIGN_START);
    gtk_grid_attach(GTK_GRID(grid), dlg->info_label, 0, 4, 2, 1);

    /* Spinner */
    dlg->spinner = gtk_spinner_new();
    gtk_grid_attach(GTK_GRID(grid), dlg->spinner, 0, 5, 2, 1);

    /* Buttons */
    GtkWidget *button_box = gtk_button_box_new(GTK_ORIENTATION_HORIZONTAL);
    gtk_button_box_set_layout(GTK_BUTTON_BOX(button_box), GTK_BUTTONBOX_END);
    gtk_box_set_spacing(GTK_BOX(button_box), 6);

    GtkWidget *cancel_button = gtk_button_new_with_label(_("Cancel"));
    g_signal_connect(cancel_button, "clicked", G_CALLBACK(on_cancel_clicked), dlg);
    gtk_container_add(GTK_CONTAINER(button_box), cancel_button);

    GtkWidget *ok_button = gtk_button_new_with_label(_("Change Password"));
    gtk_style_context_add_class(gtk_widget_get_style_context(ok_button),
                                 "suggested-action");
    g_signal_connect(ok_button, "clicked", G_CALLBACK(on_ok_clicked), dlg);
    gtk_container_add(GTK_CONTAINER(button_box), ok_button);

    gtk_grid_attach(GTK_GRID(grid), button_box, 0, 6, 2, 1);

    /* Show dialog */
    gtk_widget_show_all(dlg->dialog);
    gtk_widget_hide(dlg->spinner);

    /* Focus on old password entry */
    gtk_widget_grab_focus(dlg->old_password_entry);

    /* Run dialog */
    gtk_dialog_run(GTK_DIALOG(dlg->dialog));

    /* Cleanup on destroy */
    g_signal_connect_swapped(dlg->dialog, "destroy",
                              G_CALLBACK(g_free), dlg);

    return TRUE;
}
