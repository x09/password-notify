#include "../src/i18n.h"
#include <glib.h>
#include <assert.h>
#include <string.h>
#include <stdio.h>
int main(int argc, char **argv) {
    assert(argc == 3);
    i18n_init();
    bindtextdomain(GETTEXT_PACKAGE, argv[1]);
    gboolean ru = !strcmp(argv[2], "ru");
    assert(!strcmp(_("Change Password"), ru ? "Сменить пароль" : "Change Password"));
    const char *singular = "Password expires in %d day", *plural = "Password expires in %d days";
    const int counts[] = {1, 2, 5, 11, 21, 22, 25, 101, 111};
    const int forms[] = {0, 1, 2, 2, 0, 1, 2, 0, 2};
    const char *expected[] = {"Пароль истекает через %d день", "Пароль истекает через %d дня", "Пароль истекает через %d дней"};
    for (size_t i = 0; i < G_N_ELEMENTS(counts); i++)
        assert(!strcmp(P_(singular, plural, counts[i]), ru ? expected[forms[i]] : (counts[i] == 1 ? singular : plural)));
    const char *raw = "Your password must be at least 7 characters; cannot repeat any of your previous 24 passwords; must contain capitals, numerals or punctuation; and cannot contain your account or full name; Please type a different password. Type a password which meets these requirements in both text boxes.\npasswd: Authentication token manipulation error.";
    char *error = i18n_translate_error(raw);
    if (ru) {
        assert(strstr(error, "не менее 7 символов") && strstr(error, "последних 24 паролей"));
        assert(strstr(error, "Ошибка изменения данных аутентификации"));
        assert(!strstr(error, "Your password") && !strstr(error, "Authentication token"));
    } else assert(!strcmp(error, raw));
    g_free(error);
    raw = "Password change rejected, password changes may not be permitted on this account, or the minimum password age may not have elapsed.";
    error = i18n_translate_error(raw);
    assert(ru ? strstr(error, "минимальный срок") != NULL : !strcmp(error, raw));
    g_free(error);
    error = i18n_translate_error("Password does not meet complexity requirements");
    assert(!strcmp(error, ru ? "Пароль не соответствует требованиям сложности" : "Password does not meet complexity requirements"));
    g_free(error);
    char *success = g_strdup_printf(_("Password changed successfully via kpasswd for %s"), "domainuser2@TEST.ALT");
    assert(!strcmp(success, ru ? "Пароль пользователя domainuser2@TEST.ALT успешно изменён через kpasswd" : "Password changed successfully via kpasswd for domainuser2@TEST.ALT"));
    g_free(success);
    /* Client-facing replies from Samba 4.21 kpasswd-helper.c. */
    error = i18n_translate_error("No such user when changing password");
    assert(!strcmp(error, ru ? "Пользователь для смены пароля не найден" : "No such user when changing password"));
    g_free(error);
    error = i18n_translate_error("Not permitted to change password");
    assert(!strcmp(error, ru ? "Смена пароля не разрешена" : "Not permitted to change password"));
    g_free(error);
    error = i18n_translate_error("Password too short");
    assert(!strcmp(error, ru ? "Пароль слишком короткий" : "Password too short"));
    g_free(error);
    error = i18n_translate_error("Password is already in password history");
    assert(!strcmp(error, ru ? "Этот пароль уже использовался" : "Password is already in password history"));
    g_free(error);
    error = i18n_translate_error("Failed to set password");
    assert(!strcmp(error, ru ? "Не удалось установить пароль" : "Failed to set password"));
    g_free(error);
    error = i18n_translate_error("Password changed");
    assert(!strcmp(error, ru ? "Пароль изменён" : "Password changed"));
    g_free(error);
    error = i18n_translate_error("A TGT may not be used as a ticket to kpasswd");
    assert(!strcmp(error, ru ? "Билет TGT нельзя использовать для обращения к kpasswd" : "A TGT may not be used as a ticket to kpasswd"));
    g_free(error);
    error = i18n_translate_error("Failed to ascertain that ticket to kpasswd is not a TGT");
    assert(!strcmp(error, ru ? "Не удалось убедиться, что билет для обращения к kpasswd не является TGT" : "Failed to ascertain that ticket to kpasswd is not a TGT"));
    g_free(error);
    const char *limits[] = {"0", "1", "7", "24", "100", "65535"};
    for (size_t i = 0; i < G_N_ELEMENTS(limits); i++) {
        {
        char *input = g_strdup_printf("Password is already in password history. New password must not match any of your %s previous passwords.", limits[i]);
        char *expected = g_strdup_printf(ru ? "Этот пароль уже использовался. Новый пароль не должен совпадать с предыдущими паролями (число паролей в истории: %s)." : "Password is already in password history. New password must not match any of your %s previous passwords.", limits[i]);
        char *actual = i18n_translate_error(input);
        assert(!strcmp(actual, expected));
        char *again = i18n_translate_error(actual);
        assert(!strcmp(again, actual));
        g_free(input); g_free(expected); g_free(actual); g_free(again);
        }
        {
        char *input = g_strdup_printf("Password too short, password must be at least %s characters long.", limits[i]);
        char *expected = g_strdup_printf(ru ? "Пароль слишком короткий. Минимальное число символов в пароле: %s." : "Password too short, password must be at least %s characters long.", limits[i]);
        char *actual = i18n_translate_error(input);
        assert(!strcmp(actual, expected));
        char *again = i18n_translate_error(actual);
        assert(!strcmp(again, actual));
        g_free(input); g_free(expected); g_free(actual); g_free(again);
        }
        {
        char *input = g_strdup_printf("Failed to set password: %s", limits[i]);
        char *expected = g_strdup_printf(ru ? "Не удалось установить пароль: %s" : "Failed to set password: %s", limits[i]);
        char *actual = i18n_translate_error(input);
        assert(!strcmp(actual, expected));
        char *again = i18n_translate_error(actual);
        assert(!strcmp(again, actual));
        g_free(input); g_free(expected); g_free(actual); g_free(again);
        }
    }
    error = i18n_translate_error("Failed to set password: NT_STATUS_ACCESS_DENIED");
    assert(!strcmp(error, ru ? "Не удалось установить пароль: NT_STATUS_ACCESS_DENIED" : "Failed to set password: NT_STATUS_ACCESS_DENIED"));
    g_free(error);
    error = i18n_translate_error("Unknown server diagnostic 123");
    assert(!strcmp(error, "Unknown server diagnostic 123")); g_free(error);
    printf("PASS: %s gettext, plural forms, policy limits, passwd/Kerberos diagnostics, unknown-detail preservation\n", argv[2]);
}
