/* Scripted simulator runs (HOMEPANE_SCRIPT): screenshots and touches
 * without a person at the window, for checking UI work.
 *
 *   HOMEPANE_SCRIPT="wait 2000; shot a.bmp; click 120 300; wait 500; shot b.bmp; quit"
 *   HOMEPANE_SCRIPT=@steps.txt     (one step per line or ';')
 *
 * Steps: wait MS · shot PATH (BMP of the screen incl. top layer) ·
 * click X Y · hold X Y MS · drag X1 Y1 X2 Y2 MS · quit
 */
#include "sim_script.h"

#include "lvgl/lvgl.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

typedef struct {
    char cmd[8];
    int a[5];
    char path[256];
} step_t;

static step_t s_steps[256];
static int s_count, s_pos;
static uint32_t s_step_start;
static bool s_step_begun;

/* scripted pointer */
static int32_t s_px, s_py;
static bool s_pressed;

static void pointer_read(lv_indev_t *indev, lv_indev_data_t *data)
{
    (void)indev;
    data->point.x = s_px;
    data->point.y = s_py;
    data->state = s_pressed ? LV_INDEV_STATE_PRESSED : LV_INDEV_STATE_RELEASED;
}

static void write_bmp(const char *path)
{
#if LV_USE_SNAPSHOT
    lv_draw_buf_t *scr = lv_snapshot_take(lv_screen_active(), LV_COLOR_FORMAT_XRGB8888);
    lv_draw_buf_t *top = lv_snapshot_take(lv_layer_top(), LV_COLOR_FORMAT_ARGB8888);
    if (!scr) {
        fprintf(stderr, "sim_script: snapshot failed\n");
        return;
    }
    const int w = scr->header.w, h = scr->header.h;
    FILE *f = fopen(path, "wb");
    if (!f) {
        fprintf(stderr, "sim_script: cannot write %s\n", path);
    } else {
        const int row = (w * 3 + 3) & ~3;
        const uint32_t size = 54 + (uint32_t)row * h;
        uint8_t hdr[54] = {'B', 'M'};
        memcpy(hdr + 2, &size, 4);
        hdr[10] = 54;
        hdr[14] = 40;
        memcpy(hdr + 18, &w, 4);
        memcpy(hdr + 22, &h, 4);
        hdr[26] = 1;
        hdr[28] = 24;
        fwrite(hdr, 1, 54, f);
        uint8_t *line = calloc(1, row);
        for (int y = h - 1; y >= 0; --y) {
            const uint8_t *s = scr->data + y * scr->header.stride;
            const uint8_t *t = top ? top->data + y * top->header.stride : NULL;
            for (int x = 0; x < w; ++x) {
                int b = s[x * 4], g = s[x * 4 + 1], r = s[x * 4 + 2];
                if (t && x < (int)top->header.w && y < (int)top->header.h) {
                    const int a = t[x * 4 + 3];
                    b = (t[x * 4] * a + b * (255 - a)) / 255;
                    g = (t[x * 4 + 1] * a + g * (255 - a)) / 255;
                    r = (t[x * 4 + 2] * a + r * (255 - a)) / 255;
                }
                line[x * 3] = (uint8_t)b;
                line[x * 3 + 1] = (uint8_t)g;
                line[x * 3 + 2] = (uint8_t)r;
            }
            fwrite(line, 1, row, f);
        }
        free(line);
        fclose(f);
        printf("sim_script: wrote %s\n", path);
    }
    lv_draw_buf_destroy(scr);
    if (top) lv_draw_buf_destroy(top);
#else
    (void)path;
    fprintf(stderr, "sim_script: LV_USE_SNAPSHOT is off\n");
#endif
}

static void parse(char *text)
{
    for (char *tok = strtok(text, ";\n"); tok && s_count < (int)(sizeof(s_steps) / sizeof(s_steps[0]));
         tok = strtok(NULL, ";\n")) {
        while (*tok == ' ') ++tok;
        if (!*tok || *tok == '#') continue;
        step_t *st = &s_steps[s_count];
        memset(st, 0, sizeof(*st));
        if (sscanf(tok, "%7s", st->cmd) != 1) continue;
        if (strcmp(st->cmd, "shot") == 0) sscanf(tok, "%*s %255s", st->path);
        else sscanf(tok, "%*s %d %d %d %d %d", &st->a[0], &st->a[1], &st->a[2], &st->a[3], &st->a[4]);
        ++s_count;
    }
}

/* Returns true when the step is finished. */
static bool run_step(step_t *st, uint32_t elapsed)
{
    const char *c = st->cmd;
    if (strcmp(c, "wait") == 0) return elapsed >= (uint32_t)st->a[0];
    if (strcmp(c, "shot") == 0) {
        lv_refr_now(NULL);
        write_bmp(st->path);
        return true;
    }
    if (strcmp(c, "quit") == 0) {
        printf("sim_script: done\n");
        fflush(stdout);
        exit(0);
    }
    if (strcmp(c, "click") == 0 || strcmp(c, "hold") == 0) {
        const uint32_t dur = strcmp(c, "click") == 0 ? 80 : (uint32_t)st->a[2];
        s_px = st->a[0];
        s_py = st->a[1];
        s_pressed = elapsed < dur;
        return elapsed >= dur + 100; /* released for a moment before the next step */
    }
    if (strcmp(c, "drag") == 0) {
        const uint32_t dur = st->a[4] > 0 ? (uint32_t)st->a[4] : 300;
        const float f = elapsed >= dur ? 1.0f : (float)elapsed / dur;
        s_px = st->a[0] + (int32_t)((st->a[2] - st->a[0]) * f);
        s_py = st->a[1] + (int32_t)((st->a[3] - st->a[1]) * f);
        s_pressed = elapsed < dur;
        return elapsed >= dur + 100;
    }
    if (strcmp(c, "scroll") == 0) { /* scroll X Y DY: the scrollable object under X,Y by DY (up: positive) */
        lv_point_t p = {st->a[0], st->a[1]};
        lv_obj_t *o = lv_indev_search_obj(lv_layer_top(), &p);
        if (!o) o = lv_indev_search_obj(lv_screen_active(), &p);
        while (o && !(lv_obj_has_flag(o, LV_OBJ_FLAG_SCROLLABLE) && lv_obj_get_scroll_bottom(o) + lv_obj_get_scroll_top(o) > 0))
            o = lv_obj_get_parent(o);
        if (o) lv_obj_scroll_by(o, 0, -st->a[2], LV_ANIM_OFF);
        return true;
    }
    if (strcmp(c, "mem") == 0) { /* LVGL's heap: used / free / fragmentation */
        lv_mem_monitor_t m;
        lv_mem_monitor(&m);
        printf("sim_script: mem used %lu free %lu frag %u%% max_used %lu\n", (unsigned long)(m.total_size - m.free_size),
               (unsigned long)m.free_size, (unsigned)m.frag_pct, (unsigned long)m.max_used);
        fflush(stdout);
        return true;
    }
    if (strcmp(c, "bench") == 0) { /* bench N: N full redraws, timed (for profiling) */
        const int n = st->a[0] > 0 ? st->a[0] : 50;
        const uint32_t t0 = lv_tick_get();
        for (int i = 0; i < n; i++) {
            lv_obj_invalidate(lv_screen_active());
            lv_refr_now(NULL);
        }
        printf("sim_script: bench %d full redraws, %.2f ms each\n", n, (double)lv_tick_elaps(t0) / n);
        fflush(stdout);
        return true;
    }
    fprintf(stderr, "sim_script: unknown step '%s'\n", c);
    return true;
}

static void tick_cb(lv_timer_t *t)
{
    (void)t;
    while (s_pos < s_count) {
        if (!s_step_begun) {
            s_step_begun = true;
            s_step_start = lv_tick_get();
        }
        if (!run_step(&s_steps[s_pos], lv_tick_elaps(s_step_start))) return;
        ++s_pos;
        s_step_begun = false;
    }
}

void sim_script_start(lv_display_t *disp)
{
    const char *spec = getenv("HOMEPANE_SCRIPT");
    if (!spec || !*spec) return;
    char *text = NULL;
    if (spec[0] == '@') {
        FILE *f = fopen(spec + 1, "rb");
        if (!f) {
            fprintf(stderr, "sim_script: cannot read %s\n", spec + 1);
            return;
        }
        fseek(f, 0, SEEK_END);
        long n = ftell(f);
        fseek(f, 0, SEEK_SET);
        text = calloc(1, n + 1);
        fread(text, 1, n, f);
        fclose(f);
    } else {
        text = strdup(spec);
    }
    parse(text);
    free(text);

    lv_indev_t *indev = lv_indev_create();
    lv_indev_set_type(indev, LV_INDEV_TYPE_POINTER);
    lv_indev_set_read_cb(indev, pointer_read);
    lv_indev_set_display(indev, disp);
    lv_timer_create(tick_cb, 10, NULL);
}
