#include "scheduler.h"
#include <gio/gio.h>
#include <unistd.h>
#include <sys/file.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <errno.h>
#include <string.h>
#include <syslog.h>
#ifndef SYSTEMCTL_PATH
#define SYSTEMCTL_PATH "/usr/bin/systemctl"
#endif
static bool prepared;
static bool command(const char *action, const char *unit, GError **error) {
    char *argv[] = {SYSTEMCTL_PATH, "--user", "--no-block", (char *)action, (char *)unit, NULL};
    int status = 0;
    char *diagnostic = NULL;
    bool ok = g_spawn_sync(NULL, argv, NULL, G_SPAWN_STDOUT_TO_DEV_NULL, NULL, NULL,
                           NULL, &diagnostic, &status, error);
    if (ok && !g_spawn_check_wait_status(status, NULL)) {
        g_set_error(error, G_IO_ERROR, G_IO_ERROR_FAILED, "systemctl --user %s: %s", action,
                    diagnostic && *diagnostic ? diagnostic : "command failed");
        ok = false;
    }
    g_free(diagnostic);
    return ok;
}
static char *runtime_directory(GError **error) {
    const char *runtime = g_getenv("XDG_RUNTIME_DIR");
    struct stat st;
    if (!runtime || !g_path_is_absolute(runtime) || lstat(runtime, &st) ||
        !S_ISDIR(st.st_mode) || st.st_uid != getuid() || (st.st_mode & 0077)) {
        g_set_error_literal(error, G_IO_ERROR, G_IO_ERROR_FAILED, "A private XDG_RUNTIME_DIR owned by the current user is required");
        return NULL;
    }
    return g_strdup(runtime);
}
/* systemd ExecStart uses its own quoting and expands percent/dollar characters. */
static char *unit_argument(const char *value) {
    GString *out = g_string_new("\"");
    for (const char *p = value; *p; p++) {
        if (*p == '%' || *p == '$') g_string_append_c(out, *p);
        if (*p == '\\' || *p == '"') g_string_append_c(out, '\\');
        if (*p == '\n') g_string_append(out, "\\n");
        else if (*p == '\r') g_string_append(out, "\\r");
        else g_string_append_c(out, *p);
    }
    g_string_append_c(out, '"');
    return g_string_free(out, FALSE);
}
static bool write_if_changed(const char *path, const char *contents, bool *changed, GError **error) {
    char *old = NULL;
    if (g_file_get_contents(path, &old, NULL, NULL) && !strcmp(old, contents)) {
        g_free(old); return true;
    }
    g_free(old);
    if (!g_file_set_contents(path, contents, -1, error)) return false;
    *changed = true;
    return true;
}
bool scheduler_prepare(int hours, const char *config_path, GError **error) {
    if (hours < 1 || hours > 720) {
        g_set_error_literal(error, G_IO_ERROR, G_IO_ERROR_INVALID_ARGUMENT, "Invalid timer interval"); return false;
    }
    char *runtime = runtime_directory(error);
    if (!runtime) return false;
    char *dir = g_build_filename(runtime, "systemd", "user", NULL);
    g_free(runtime);
    if (g_mkdir_with_parents(dir, 0700)) {
        g_set_error(error, G_IO_ERROR, G_IO_ERROR_FAILED, "Cannot create timer directory: %s", g_strerror(errno));
        g_free(dir); return false;
    }
    char *exe = g_file_read_link("/proc/self/exe", error);
    if (!exe) { g_free(dir); return false; }
    char *quoted_exe = unit_argument(exe);
    char *absolute_config = config_path ? g_canonicalize_filename(config_path, NULL) : NULL;
    char *quoted_config = absolute_config ? unit_argument(absolute_config) : NULL;
    char *args = quoted_config ? g_strdup_printf(" -c %s", quoted_config) : g_strdup("");
    char *service = g_strdup_printf("[Unit]\nDescription=Password expiration check and tray warning\n\n[Service]\nType=exec\nExecStart=%s -s%s\nExecStopPost=%s -u%s\nRestart=no\n", quoted_exe, args, quoted_exe, args);
    /* OnActiveSec covers the first scheduled run; the inactive interval covers
     * subsequent runs and never accumulates concurrent processes. */
    char *timer = g_strdup_printf("[Unit]\nDescription=Periodic password expiration check\n\n[Timer]\nOnActiveSec=%dh\nOnUnitInactiveSec=%dh\nAccuracySec=1s\nUnit=" SCHEDULER_SERVICE "\n", hours, hours);
    char *service_path = g_build_filename(dir, SCHEDULER_SERVICE, NULL);
    char *timer_path = g_build_filename(dir, SCHEDULER_TIMER, NULL);
    bool changed = false;
    bool ok = write_if_changed(service_path, service, &changed, error) &&
              write_if_changed(timer_path, timer, &changed, error);
    if (ok && changed) ok = command("daemon-reload", NULL, error);
    prepared = ok;
    g_free(exe); g_free(quoted_exe); g_free(absolute_config); g_free(quoted_config);
    g_free(args); g_free(service); g_free(timer); g_free(service_path); g_free(timer_path); g_free(dir);
    return ok;
}
bool scheduler_arm(GError **error) {
    if (!prepared) { g_set_error_literal(error, G_IO_ERROR, G_IO_ERROR_FAILED, "Timer is not configured"); return false; }
    return command("restart", SCHEDULER_TIMER, error);
}
bool scheduler_pause(GError **error) { return command("stop", SCHEDULER_TIMER, error); }
bool scheduler_bootstrap(int hours, const char *config_path, GError **error) {
    const char *names[] = {"DISPLAY", "WAYLAND_DISPLAY", "XAUTHORITY", "DBUS_SESSION_BUS_ADDRESS", "XDG_RUNTIME_DIR", "KRB5CCNAME", "LANG", "LANGUAGE", "LC_ALL", "LC_MESSAGES", "LC_TIME"};
    const char *display = g_getenv("DISPLAY"), *wayland = g_getenv("WAYLAND_DISPLAY");
    if ((!display || !*display) && (!wayland || !*wayland)) {
        g_set_error_literal(error, G_IO_ERROR, G_IO_ERROR_FAILED, "No graphical session environment"); return false;
    }
    GPtrArray *present = g_ptr_array_new(), *missing = g_ptr_array_new();
    g_ptr_array_add(present, SYSTEMCTL_PATH); g_ptr_array_add(present, "--user"); g_ptr_array_add(present, "import-environment");
    g_ptr_array_add(missing, SYSTEMCTL_PATH); g_ptr_array_add(missing, "--user"); g_ptr_array_add(missing, "unset-environment");
    for (unsigned i = 0; i < G_N_ELEMENTS(names); i++)
        g_ptr_array_add(g_getenv(names[i]) ? present : missing, (gpointer)names[i]);
    int status; bool ok = true;
    GPtrArray *lists[] = {missing, present};
    for (unsigned i = 0; i < 2 && ok; i++) {
        if (lists[i]->len == 3) continue;
        g_ptr_array_add(lists[i], NULL);
        ok = g_spawn_sync(NULL, (char **)lists[i]->pdata, NULL, G_SPAWN_DEFAULT, NULL, NULL, NULL, NULL, &status, error);
        if (ok) ok = g_spawn_check_wait_status(status, error);
    }
    g_ptr_array_free(present, TRUE); g_ptr_array_free(missing, TRUE);
    if (!ok || !scheduler_prepare(hours, config_path, error)) return false;
    char *query[] = {SYSTEMCTL_PATH, "--user", "--quiet", "is-active", SCHEDULER_SERVICE, NULL};
    if (!g_spawn_sync(NULL, query, NULL, G_SPAWN_STDOUT_TO_DEV_NULL | G_SPAWN_STDERR_TO_DEV_NULL,
                      NULL, NULL, NULL, NULL, &status, error)) return false;
    /* Repeated session/menu launches never interrupt a running password dialog. */
    if (g_spawn_check_wait_status(status, NULL)) return scheduler_pause(error);
    return scheduler_arm(error) && command("start", SCHEDULER_SERVICE, error);
}
int scheduler_lock(GError **error) {
    char *runtime = runtime_directory(error);
    if (!runtime) return -1;
    char *path = g_build_filename(runtime, "password-notify.lock", NULL);
    g_free(runtime);
    int fd = open(path, O_CREAT | O_RDWR | O_CLOEXEC | O_NOFOLLOW, 0600);
    g_free(path);
    if (fd >= 0 && flock(fd, LOCK_EX | LOCK_NB) == 0) return fd;
    int saved = errno;
    if (fd >= 0) close(fd);
    if (saved == EWOULDBLOCK) return -2;
    g_set_error(error, G_IO_ERROR, G_IO_ERROR_FAILED, "Cannot lock password-notify: %s", g_strerror(saved));
    return -1;
}
