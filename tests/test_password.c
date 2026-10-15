/* Offline regression: never contact a KDC or invoke passwd. */
#include <krb5.h>
#include <assert.h>
#include <stdlib.h>
#include <string.h>
#include <stdio.h>
static int reply;
static const char *detail;
static int requests, changes;
static krb5_error_code fake_initial(krb5_context ctx, krb5_creds *creds,
    krb5_principal principal, const char *password, krb5_prompter_fct prompter,
    void *data, krb5_deltat start, const char *service, krb5_get_init_creds_opt *opts) {
    (void)ctx; (void)creds; (void)principal; (void)password;
    (void)prompter; (void)data; (void)start; (void)opts;
    assert(service && !strcmp(service, "kadmin/changepw")); requests++;
    return 0;
}
static krb5_error_code fake_change(krb5_context ctx, krb5_creds *creds,
    const char *password, int *code, krb5_data *code_text, krb5_data *text) {
    (void)ctx; (void)creds; (void)password; (void)code_text;
    *code = reply;
    if (detail) { text->data = strdup(detail); text->length = strlen(detail); }
    changes++; return 0;
}
#define krb5_get_init_creds_password fake_initial
#define krb5_change_password fake_change
#include "../src/password_change.c"
#undef krb5_get_init_creds_password
#undef krb5_change_password
int main(int argc, char **argv) {
    const int codes[] = {0, KRB5_KPASSWD_SOFTERROR, KRB5_KPASSWD_HARDERROR, KRB5_KPASSWD_AUTHERROR};
    const pwd_change_result_t expected[] = {PWD_CHANGE_SUCCESS, PWD_CHANGE_ERROR_POLICY,
        PWD_CHANGE_ERROR_OTHER, PWD_CHANGE_ERROR_AUTH};
    for (size_t i = 0; i < sizeof(codes)/sizeof(codes[0]); i++) {
        reply = codes[i]; detail = reply ? "specific server rejection" : NULL;
        char *error = NULL;
        assert(password_change_samba("test@TEST.ALT", "dummy-old", "dummy-new", NULL, &error) == expected[i]);
        if (reply) assert(error && !strcmp(error, detail)); else assert(!error);
        free(error);
    }
    assert(requests == 4 && changes == 4);
    puts("PASS: changepw initial ticket, own-password API, result classification and server diagnostics (offline)");
    assert(argc == 2);
    const char *modes[] = {"success", "policy", "auth", "long", "redact", "unknown", "timeout"};
    const pwd_change_result_t outcomes[] = {PWD_CHANGE_SUCCESS, PWD_CHANGE_ERROR_POLICY,
        PWD_CHANGE_ERROR_AUTH, PWD_CHANGE_ERROR_OTHER, PWD_CHANGE_ERROR_OTHER,
        PWD_CHANGE_ERROR_OTHER, PWD_CHANGE_ERROR_OTHER};
    for (size_t i = 0; i < sizeof(modes)/sizeof(modes[0]); i++) {
        setenv("PASSWORD_NOTIFY_TEST_MODE", modes[i], 1);
        char *error = NULL;
        pwd_change_result_t actual = passwd_run(argv[1], "dummy-old", "dummy-new", &error, 2 * G_USEC_PER_SEC);
        if (actual != outcomes[i]) fprintf(stderr, "mode=%s result=%d error=%s\n", modes[i], actual, error ? error : "none");
        assert(actual == outcomes[i]);
        if (!strcmp(modes[i], "long")) assert(error && strstr(error, "FINAL SERVER REJECTION"));
        if (!strcmp(modes[i], "redact")) assert(error && !strstr(error, "dummy-old") && !strstr(error, "dummy-new"));
        if (!strcmp(modes[i], "timeout")) assert(error && strstr(error, "timed out"));
        if (!strcmp(modes[i], "success")) assert(!error);
        free(error);
    }
    puts("PASS: passwd private TTY, split prompts, UNIX/NT prompts, success, policy/auth errors, full diagnostics, redaction and timeout (fake process)");

    return 0;
}
