/* Offline tests use plain GObjects and never call a notification daemon. */
#include <libnotify/notify.h>
NotifyNotification *notify_notification_new(const char *summary, const char *body, const char *icon) {
    (void)summary; (void)body; (void)icon;
    return (NotifyNotification *)g_object_new(G_TYPE_OBJECT, NULL);
}
gboolean notify_notification_show(NotifyNotification *notification, GError **error) {
    (void)notification; (void)error; return TRUE;
}
void notify_notification_set_image_from_pixbuf(NotifyNotification *n, GdkPixbuf *p) { (void)n; (void)p; }
void notify_notification_set_urgency(NotifyNotification *n, NotifyUrgency u) { (void)n; (void)u; }
void notify_notification_set_timeout(NotifyNotification *n, gint t) { (void)n; (void)t; }
