#ifndef PASSWORD_NOTIFY_SCHEDULER_H
#define PASSWORD_NOTIFY_SCHEDULER_H
#include <stdbool.h>
#include <glib.h>
#define SCHEDULER_SERVICE "password-notify-check.service"
#define SCHEDULER_TIMER "password-notify-check.timer"
bool scheduler_prepare(int hours, const char *config_path, GError **error);
bool scheduler_bootstrap(int hours, const char *config_path, GError **error);
bool scheduler_arm(GError **error);
bool scheduler_pause(GError **error);
/* Returns an open lock fd, -1 on error, -2 if another instance owns it. */
int scheduler_lock(GError **error);
#endif
