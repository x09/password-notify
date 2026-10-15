/* Exercise the actual completion logic without changing a password. */
#include "../src/ui.c"
#include <assert.h>
#include <stdio.h>
static unsigned pauses;
bool scheduler_pause(GError **error) { (void)error; pauses++; return true; }
static gboolean transition(gpointer data) {
    ui_context_t *ctx = data;
    ldap_result_t result = {0};
    result.expiry_time = time(NULL) + 2 * 86400 + 10;
    apply_expiry_result(ctx, result);
    assert(pauses == 1 && ctx->days_until_expiry == 2 && ctx->check_timer_id != 0);
    /* Same result path used by the recheck after a successful password change. */
    result.expiry_time = time(NULL) + 40 * 86400;
    apply_expiry_result(ctx, result);
    assert(pauses == 1);
    return G_SOURCE_REMOVE;
}
int main(int argc, char **argv) {
    config_t config; config_defaults(&config);
    ui_context_t *ctx = ui_init(&argc, &argv, &config);
    g_source_remove(ctx->initial_check_id); ctx->initial_check_id = 0;
    g_source_remove(ctx->check_timer_id); ctx->check_timer_id = 0;
    ctx->scheduled_mode = true;
    g_idle_add(transition, ctx);
    assert(ui_run(ctx) == 0);
    ui_cleanup(ctx); config_free(&config);
    puts("PASS: active password warning pauses timer; renewed password result exits GUI loop");
}
