#include "../src/ui.h"
#include <assert.h>
#include <string.h>
#include <stdio.h>
int main(int argc, char **argv) {
    config_t config; config_defaults(&config); config.warn_days = 7;
    ui_context_t *ctx = ui_init(&argc, &argv, &config);
    const int days[] = {30, 7, 1, 0, -2};
    for (unsigned i = 0; i < sizeof(days)/sizeof(days[0]); i++) {
        ui_update_indicator(ctx, days[i]);
#ifdef USE_STATUS_ICON
        char *tooltip = gtk_status_icon_get_tooltip_text(ctx->status_icon);
        assert(tooltip && *tooltip); g_free(tooltip);
        GdkPixbuf *pixbuf = gtk_status_icon_get_pixbuf(ctx->status_icon);
        assert(pixbuf);
        const guchar *pixels = gdk_pixbuf_get_pixels(pixbuf);
        int stride = gdk_pixbuf_get_rowstride(pixbuf);
        int channels = gdk_pixbuf_get_n_channels(pixbuf);
        int red = 0, dark = 0;
        for (int y = 0; y < gdk_pixbuf_get_height(pixbuf); y++)
            for (int x = 0; x < gdk_pixbuf_get_width(pixbuf); x++) {
                const guchar *p = pixels + y * stride + x * channels;
                if (channels == 4 && p[3] < 128) continue;
                if (p[0] > 160 && p[1] < 80 && p[2] < 80) red++;
                if (p[0] < 80 && p[1] < 80 && p[2] < 80) dark++;
            }
        assert(days[i] <= 7 ? red > 0 : dark > 0);
#else
        const char *label = app_indicator_get_label(ctx->indicator);
        assert(!label || !*label);
        const char *icon = app_indicator_get_icon(ctx->indicator);
        assert(icon && strstr(icon, days[i] <= 7 ? "key-red.svg" : "key-black.svg"));
#endif
    }
    ui_cleanup(ctx); config_free(&config);
    puts("PASS: tray has no adjacent label; black/red key selection and MATE tooltip");
    return 0;
}
