#ifndef PASSWORD_NOTIFY_I18N_H
#define PASSWORD_NOTIFY_I18N_H
#include <libintl.h>
#define GETTEXT_PACKAGE "password-notify"
#define _(s) dgettext(GETTEXT_PACKAGE, s)
#define N_(s) (s)
#define P_(one, many, count) dngettext(GETTEXT_PACKAGE, one, many, count)
void i18n_init(void);
/* Caller owns the returned UTF-8 diagnostic. Unknown details are preserved. */
char *i18n_translate_error(const char *message);
#endif
