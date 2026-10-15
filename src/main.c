/**
 * @file main.c
 * @brief Password expiration notification daemon for Samba AD
 *
 * Monitors domain password expiration and notifies user N days before expiry.
 * Runs in user session with existing Kerberos ticket.
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <glib-unix.h>
#include <syslog.h>

#include "i18n.h"
#include "config.h"
#include "scheduler.h"
#include "ldap_query.h"
#include "password_change.h"
#include "ui.h"

#define PROGRAM_NAME "password-notify"
#define VERSION "1.0.0"

static ui_context_t *g_ui_ctx = NULL;

/**
 * Signal handler for graceful shutdown
 */
static gboolean signal_handler(gpointer data) {
    (void)data;
    gtk_main_quit();
    return G_SOURCE_CONTINUE;
}

static void setup_signals(void) {
    g_unix_signal_add(SIGTERM, signal_handler, NULL);
    g_unix_signal_add(SIGINT, signal_handler, NULL);
}

/**
 * Print usage information
 */
static void print_usage(const char *progname) {
    printf(_("Usage: %s [OPTIONS]\n"), progname);
    printf("\n");
    printf(_("Domain password expiration notification daemon\n"));
    printf("\n");
    printf(_("Options:\n"));
    printf(_("  -c FILE     Configuration file path (default: /etc/password-notify.ini)\n"));
    printf(_("  -d          Debug mode (log to stderr)\n"));
    printf(_("  -t          Check password expiration once without GUI\n"));
    printf(_("  -a          Start session timer and check immediately\n"));
    printf(_("  -s          Scheduled check: exit unless a warning is needed\n"));
    printf(_("  -h          Show this help\n"));
    printf(_("  -v          Show version\n"));
    printf("\n");
}

/**
 * Print version information
 */
static void print_version(void) {
    printf(_("%s version %s\n"), PROGRAM_NAME, VERSION);
    printf(_("Samba AD password expiration notifier\n"));
}

/**
 * Main entry point
 */
int main(int argc, char *argv[]) {
    i18n_init();
    config_t config;
    const char *config_path = NULL;
    int opt;
    int ret = 0;
    int debug_mode = 0;
    int check_once = 0;
    int scheduled = 0, bootstrap = 0, rearm = 0;
    int lock_fd = -1;

    /* Parse command line options */
    while ((opt = getopt(argc, argv, "c:adhstuv")) != -1) {
        switch (opt) {
            case 'c':
                config_path = optarg;
                break;
            case 'd':
                debug_mode = 1;
                break;
            case 'a': bootstrap = 1; break;
            case 's': scheduled = 1; break;
            case 'u': rearm = 1; break;
            case 't':
                check_once = 1;
                break;
            case 'h':
                print_usage(argv[0]);
                return 0;
            case 'v':
                print_version();
                return 0;
            default:
                print_usage(argv[0]);
                return 1;
        }
    }

    /* Plain GUI invocation uses the managed session/timer workflow too. */
    if (!check_once && !scheduled && !rearm) bootstrap = 1;

    /* Initialize syslog */
    openlog(PROGRAM_NAME, LOG_PID | LOG_CONS | (debug_mode ? LOG_PERROR : 0), LOG_USER);
    syslog(LOG_INFO, "Starting %s version %s", PROGRAM_NAME, VERSION);

    /* Load configuration */
    if (config_parse(&config, config_path) != 0) {
        fprintf(stderr, _("Error: Failed to parse configuration\n"));
        syslog(LOG_ERR, "Failed to parse configuration");
        return 1;
    }

    syslog(LOG_INFO, "Configuration loaded: warn_days=%d, check_interval=%dh",
           config.warn_days, config.check_interval_hours);

    if (check_once) {
        void *ld = ldap_init_gssapi(config.server_uri, config.use_ldaps, config.skip_issuer);
        ldap_result_t result = {0};
        ret = ld ? ldap_query_password_expiry(ld, config.base_dn, &result) : -1;
        if (ret == 0) {
            if (result.never_expires) printf(_("Password never expires\n"));
            else printf(_("Days until password expiration: %d\n"),
                        ldap_days_until_expiry(result.expiry_time));
        } else fprintf(stderr, _("Password expiration check failed; see diagnostic log\n"));
        if (ld) ldap_close(ld);
        ldap_result_free(&result); config_free(&config); closelog();
        return ret == 0 ? 0 : 1;
    }

    GError *scheduler_error = NULL;
    if (rearm) {
        bool ok = scheduler_prepare(config.check_interval_hours, config_path, &scheduler_error) && scheduler_arm(&scheduler_error);
        if (!ok) syslog(LOG_ERR, "%s", scheduler_error ? scheduler_error->message : "Cannot rearm timer");
        g_clear_error(&scheduler_error); config_free(&config); closelog(); return ok ? 0 : 1;
    }
    if (bootstrap) {
        bool ok = scheduler_bootstrap(config.check_interval_hours, config_path, &scheduler_error);
        if (!ok) { syslog(LOG_ERR, "%s", scheduler_error ? scheduler_error->message : "Cannot start timer");
            fprintf(stderr, "%s\n", scheduler_error ? scheduler_error->message : "Cannot start timer"); }
        g_clear_error(&scheduler_error); config_free(&config); closelog();
        return ok ? 0 : 1;
    }
    if (scheduled && !scheduler_prepare(config.check_interval_hours, config_path, &scheduler_error)) {
        syslog(LOG_ERR, "%s", scheduler_error->message); g_clear_error(&scheduler_error);
        config_free(&config); closelog(); return 1;
    }
    /* One GUI/checking instance per UID, also for manual launches. */
    if (scheduled || g_getenv("XDG_RUNTIME_DIR")) {
        lock_fd = scheduler_lock(&scheduler_error);
        if (lock_fd < 0) {
            if (scheduler_error) syslog(LOG_ERR, "%s", scheduler_error->message);
            g_clear_error(&scheduler_error); config_free(&config); closelog();
            return lock_fd == -2 ? 0 : 1;
        }
    }

    /* Setup signal handlers */
    setup_signals();

    /* Initialize UI */
    g_ui_ctx = ui_init(&argc, &argv, &config);
    if (!g_ui_ctx) {
        fprintf(stderr, _("Error: Failed to initialize UI\n"));
        syslog(LOG_ERR, "Failed to initialize UI");
        config_free(&config);
        return 1;
    }

    g_ui_ctx->scheduled_mode = scheduled;
    if (scheduled && g_ui_ctx->check_timer_id) {
        g_source_remove(g_ui_ctx->check_timer_id);
        g_ui_ctx->check_timer_id = 0;
    }

    syslog(LOG_INFO, "UI initialized, starting main loop");

    /* Run UI main loop */
    ret = ui_run(g_ui_ctx);

    /* Cleanup */
    syslog(LOG_INFO, "Shutting down");
    if (scheduled && !scheduler_arm(&scheduler_error)) {
        syslog(LOG_ERR, "%s", scheduler_error->message); ret = 1; g_clear_error(&scheduler_error);
    }
    ui_cleanup(g_ui_ctx);
    if (lock_fd >= 0) close(lock_fd);
    config_free(&config);
    closelog();

    return ret;
}