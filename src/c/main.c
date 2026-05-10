#include <pebble.h>
#include <string.h>

extern uint32_t MESSAGE_KEY_AnimationEnabled;
extern uint32_t MESSAGE_KEY_TickingEnabled;

// ── Layout (derived from clock_skin.xml offsets scaled to 200x228) ────────────
#define CLOCK_CENTER_X  100
#define CLOCK_CENTER_Y  136

#define GEAR_TOP_X      100
#define GEAR_TOP_Y       58
#define GEAR_TOP_SIZE    76

#define COMP_L_X         16
#define COMP_L_Y         42
#define COMP_R_X        184
#define COMP_R_Y         42

#define DIGIT_W          14
#define DIGIT_H          30

#define CGEAR_SPOKE_INNER   7
#define CGEAR_SPOKE_OUTER  26
#define CGEAR_FILL_R       29
#define CGEAR_HUB_R         8
#define CGEAR_NUM_SPOKES    8

#define ANIM_INTERVAL_MS   100
#define GEAR_FRAME_COUNT     8
#define GEAR_FRAME_MAX       7
#define GEAR_CENTER_FRAME    3

// ── Settings persistence ─────────────────────────────────────────────────────
#define PERSIST_KEY_ANIMATION 0
#define PERSIST_KEY_TICKING   1

// ── Tick sound parameters ────────────────────────────────────────────────────
#define TICK_FREQ_HZ    65000
#define TICK_DURATION_MS    1
#define TICK_VOLUME         0

// ── Resource ID arrays ────────────────────────────────────────────────────────
static const uint32_t s_gear_rids[GEAR_FRAME_COUNT] = {
    RESOURCE_ID_GEAR_TOP_0, RESOURCE_ID_GEAR_TOP_1, RESOURCE_ID_GEAR_TOP_2,
    RESOURCE_ID_GEAR_TOP_3, RESOURCE_ID_GEAR_TOP_4, RESOURCE_ID_GEAR_TOP_5,
    RESOURCE_ID_GEAR_TOP_6, RESOURCE_ID_GEAR_TOP_7
};

static const uint32_t s_digit1_rids[10] = {
    RESOURCE_ID_DIGIT1_0, RESOURCE_ID_DIGIT1_1, RESOURCE_ID_DIGIT1_2,
    RESOURCE_ID_DIGIT1_3, RESOURCE_ID_DIGIT1_4, RESOURCE_ID_DIGIT1_5,
    RESOURCE_ID_DIGIT1_6, RESOURCE_ID_DIGIT1_7, RESOURCE_ID_DIGIT1_8,
    RESOURCE_ID_DIGIT1_9
};

static const uint32_t s_digit2_rids[10] = {
    RESOURCE_ID_DIGIT2_0, RESOURCE_ID_DIGIT2_1, RESOURCE_ID_DIGIT2_2,
    RESOURCE_ID_DIGIT2_3, RESOURCE_ID_DIGIT2_4, RESOURCE_ID_DIGIT2_5,
    RESOURCE_ID_DIGIT2_6, RESOURCE_ID_DIGIT2_7, RESOURCE_ID_DIGIT2_8,
    RESOURCE_ID_DIGIT2_9
};

// ── Hand GPath definitions (tip = -Y = 12 o'clock) ───────────────────────────
static const GPathInfo HOUR_HAND_INFO = {
    .num_points = 7,
    .points = (GPoint[]) {
        {-6, 20}, {-7, 0}, {-4, -28}, {0, -72}, {4, -28}, {7, 0}, {6, 20}
    }
};

static const GPathInfo MINUTE_HAND_INFO = {
    .num_points = 7,
    .points = (GPoint[]) {
        {-5, 22}, {-6, 0}, {-3, -44}, {0, -76}, {3, -44}, {6, 0}, {5, 22}
    }
};

// ── State ─────────────────────────────────────────────────────────────────────
static Window    *s_window;
static Layer     *s_canvas;

static GBitmap   *s_bg_bitmap;
static GBitmap   *s_front_bitmap;
static GBitmap   *s_gear_bitmap;
static int        s_gear_idx = GEAR_CENTER_FRAME;
static int        s_gear_dir = 1;

static GBitmap   *s_digits1[10];
static GBitmap   *s_digits2[10];

static AppTimer  *s_anim_timer;

static int  s_hours   = 10;
static int  s_minutes = 10;
static int  s_seconds = 30;
static int  s_month   = 1;
static int  s_mday    = 1;

static GPath *s_hour_path;
static GPath *s_minute_path;

static bool s_animation_enabled = true;
static bool s_ticking_enabled   = true;

// ── Helpers ───────────────────────────────────────────────────────────────────

static GPoint polar_pt(GPoint c, int32_t angle, int r) {
    return GPoint(
        c.x + (int16_t)(sin_lookup(angle) * r / TRIG_MAX_RATIO),
        c.y - (int16_t)(cos_lookup(angle) * r / TRIG_MAX_RATIO)
    );
}

// Forward declarations
static void tick_handler(struct tm *tick_time, TimeUnits units_changed);
static void animation_timer_callback(void *data);
static void start_animation_timer(void);

static void update_tick_subscription(void) {
    tick_timer_service_unsubscribe();
    if (s_animation_enabled || s_ticking_enabled) {
        tick_timer_service_subscribe(SECOND_UNIT, tick_handler);
    } else {
        tick_timer_service_subscribe(MINUTE_UNIT, tick_handler);
    }
}

// ── Drawing ───────────────────────────────────────────────────────────────────

static void draw_center_gear(GContext *ctx, int32_t angle) {
    GPoint c = GPoint(CLOCK_CENTER_X, CLOCK_CENTER_Y);

    graphics_context_set_fill_color(ctx, GColorFromRGB(12, 6, 0));
    graphics_fill_circle(ctx, c, CGEAR_FILL_R);

    graphics_context_set_stroke_color(ctx, GColorFromRGB(170, 85, 0));
    graphics_context_set_stroke_width(ctx, 3);
    graphics_draw_circle(ctx, c, CGEAR_FILL_R - 1);

    for (int i = 0; i < CGEAR_NUM_SPOKES; i++) {
        int32_t a = angle + DEG_TO_TRIGANGLE(i * (360 / CGEAR_NUM_SPOKES));
        GPoint inner = polar_pt(c, a, CGEAR_SPOKE_INNER);
        GPoint outer = polar_pt(c, a, CGEAR_SPOKE_OUTER);
        GPoint si = polar_pt(GPoint(c.x+1,c.y+1), a, CGEAR_SPOKE_INNER);
        GPoint so = polar_pt(GPoint(c.x+1,c.y+1), a, CGEAR_SPOKE_OUTER);
        graphics_context_set_stroke_color(ctx, GColorFromRGB(0, 0, 85));
        graphics_context_set_stroke_width(ctx, 5);
        graphics_draw_line(ctx, si, so);
        graphics_context_set_stroke_color(ctx, GColorFromRGB(255, 170, 0));
        graphics_context_set_stroke_width(ctx, 3);
        graphics_draw_line(ctx, inner, outer);
    }

    graphics_context_set_stroke_color(ctx, GColorFromRGB(255, 170, 0));
    graphics_context_set_stroke_width(ctx, 2);
    graphics_draw_circle(ctx, c, CGEAR_SPOKE_OUTER);

    graphics_context_set_fill_color(ctx, GColorFromRGB(30, 15, 0));
    graphics_fill_circle(ctx, c, CGEAR_HUB_R + 2);
    graphics_context_set_stroke_color(ctx, GColorFromRGB(170, 85, 0));
    graphics_context_set_stroke_width(ctx, 2);
    graphics_draw_circle(ctx, c, CGEAR_HUB_R + 2);

    graphics_context_set_fill_color(ctx, GColorFromRGB(255, 170, 0));
    graphics_fill_circle(ctx, c, CGEAR_HUB_R);
    graphics_context_set_fill_color(ctx, GColorFromRGB(255, 255, 85));
    graphics_fill_circle(ctx, c, CGEAR_HUB_R - 3);
}

static void draw_hand(GContext *ctx, GPath *path, int32_t angle,
                      GPoint center, GColor shadow, GColor fill,
                      GColor highlight, int hlen) {
    gpath_rotate_to(path, angle);
    gpath_move_to(path, GPoint(center.x + 2, center.y + 2));
    graphics_context_set_fill_color(ctx, shadow);
    gpath_draw_filled(ctx, path);

    gpath_move_to(path, center);
    graphics_context_set_fill_color(ctx, fill);
    gpath_draw_filled(ctx, path);

    graphics_context_set_stroke_color(ctx, GColorFromRGB(0, 0, 0));
    graphics_context_set_stroke_width(ctx, 1);
    gpath_draw_outline(ctx, path);

    GPoint tip = polar_pt(center, angle, hlen);
    graphics_context_set_stroke_color(ctx, highlight);
    graphics_context_set_stroke_width(ctx, 1);
    graphics_draw_line(ctx, center, tip);
}

static void draw_second_hand(GContext *ctx, GPoint center, int32_t angle) {
    GPoint tip  = polar_pt(center, angle,  76);
    GPoint tail = polar_pt(center, angle, -20);

    graphics_context_set_stroke_color(ctx, GColorFromRGB(0, 0, 0));
    graphics_context_set_stroke_width(ctx, 4);
    graphics_draw_line(ctx, GPoint(tail.x+2,tail.y+2), GPoint(tip.x+2,tip.y+2));

    graphics_context_set_stroke_color(ctx, GColorFromRGB(170, 85, 0));
    graphics_context_set_stroke_width(ctx, 3);
    graphics_draw_line(ctx, tail, tip);

    graphics_context_set_stroke_color(ctx, GColorFromRGB(255, 85, 85));
    graphics_context_set_stroke_width(ctx, 1);
    graphics_draw_line(ctx, tail, tip);

    graphics_context_set_fill_color(ctx, GColorFromRGB(170, 85, 0));
    graphics_fill_circle(ctx, tail, 5);
    graphics_context_set_fill_color(ctx, GColorFromRGB(255, 85, 85));
    graphics_fill_circle(ctx, tail, 2);
}

static void draw_digit_number(GContext *ctx, GBitmap **digits,
                              int value, int cx, int cy) {
    char buf[3];
    snprintf(buf, sizeof(buf), "%02d", value);
    int n = 2;
    int total_w = n * DIGIT_W;
    int start_x = cx - total_w / 2;
    int start_y = cy - DIGIT_H / 2;

    graphics_context_set_compositing_mode(ctx, GCompOpSet);
    for (int i = 0; i < n; i++) {
        int d = buf[i] - '0';
        if (d >= 0 && d < 10 && digits[d]) {
            GRect r = GRect(start_x + i * DIGIT_W, start_y, DIGIT_W, DIGIT_H);
            graphics_draw_bitmap_in_rect(ctx, digits[d], r);
        }
    }
    graphics_context_set_compositing_mode(ctx, GCompOpAssign);
}

static void canvas_update_proc(Layer *layer, GContext *ctx) {
    GPoint center = GPoint(CLOCK_CENTER_X, CLOCK_CENTER_Y);

    if (s_bg_bitmap) {
        graphics_draw_bitmap_in_rect(ctx, s_bg_bitmap, layer_get_bounds(layer));
    }

    if (s_gear_bitmap) {
        graphics_context_set_compositing_mode(ctx, GCompOpSet);
        int half = GEAR_TOP_SIZE / 2;
        GRect gr = GRect(GEAR_TOP_X - half, GEAR_TOP_Y - half,
                         GEAR_TOP_SIZE, GEAR_TOP_SIZE);
        graphics_draw_bitmap_in_rect(ctx, s_gear_bitmap, gr);
        graphics_context_set_compositing_mode(ctx, GCompOpAssign);
    }

    if (s_front_bitmap) {
        graphics_context_set_compositing_mode(ctx, GCompOpSet);
        graphics_draw_bitmap_in_rect(ctx, s_front_bitmap, layer_get_bounds(layer));
        graphics_context_set_compositing_mode(ctx, GCompOpAssign);
    }

    int32_t hour_angle = (TRIG_MAX_ANGLE * ((s_hours % 12) * 60 + s_minutes))
                         / (12 * 60);
    draw_center_gear(ctx, hour_angle);

    int32_t min_angle = (TRIG_MAX_ANGLE * s_minutes) / 60;

    draw_hand(ctx, s_hour_path, hour_angle, center,
              GColorFromRGB(0, 0, 85),
              GColorFromRGB(255, 170, 0),
              GColorFromRGB(255, 255, 85),
              72);

    draw_hand(ctx, s_minute_path, min_angle, center,
              GColorFromRGB(0, 0, 85),
              GColorFromRGB(170, 85, 0),
              GColorFromRGB(255, 170, 85),
              76);

    if (s_animation_enabled) {
        int32_t sec_angle = (TRIG_MAX_ANGLE * s_seconds) / 60;
        draw_second_hand(ctx, center, sec_angle);
    }

    graphics_context_set_fill_color(ctx, GColorFromRGB(255, 170, 0));
    graphics_fill_circle(ctx, center, 7);
    graphics_context_set_fill_color(ctx, GColorFromRGB(255, 255, 85));
    graphics_fill_circle(ctx, center, 3);

    draw_digit_number(ctx, s_digits2, s_month, COMP_L_X, COMP_L_Y);
    draw_digit_number(ctx, s_digits1, s_mday,  COMP_R_X, COMP_R_Y);
}

// ── Timer & Tick ──────────────────────────────────────────────────────────────

static void load_gear_frame(int idx) {
    if (s_gear_bitmap) { gbitmap_destroy(s_gear_bitmap); s_gear_bitmap = NULL; }
    s_gear_bitmap = gbitmap_create_with_resource(s_gear_rids[idx]);
}

static void animation_timer_callback(void *data) {
    s_gear_idx += s_gear_dir;
    if (s_gear_idx >= GEAR_FRAME_MAX) { s_gear_idx = GEAR_FRAME_MAX; s_gear_dir = -1; }
    if (s_gear_idx <= 0)              { s_gear_idx = 0;               s_gear_dir =  1; }

    load_gear_frame(s_gear_idx);
    layer_mark_dirty(s_canvas);
    s_anim_timer = app_timer_register(ANIM_INTERVAL_MS, animation_timer_callback, NULL);
}

static void start_animation_timer(void) {
    if (s_anim_timer) { app_timer_cancel(s_anim_timer); s_anim_timer = NULL; }
    if (s_animation_enabled) {
        s_anim_timer = app_timer_register(ANIM_INTERVAL_MS, animation_timer_callback, NULL);
    } else {
        s_gear_idx = GEAR_CENTER_FRAME;
        s_gear_dir = 1;
        load_gear_frame(s_gear_idx);
        layer_mark_dirty(s_canvas);
    }
}

static void tick_handler(struct tm *tick_time, TimeUnits units_changed) {
    s_hours   = tick_time->tm_hour;
    s_minutes = tick_time->tm_min;
    s_seconds = tick_time->tm_sec;
    s_month   = tick_time->tm_mon + 1;
    s_mday    = tick_time->tm_mday;

    if (s_ticking_enabled) {
        speaker_play_tone(TICK_FREQ_HZ, TICK_DURATION_MS, TICK_VOLUME,
                          SpeakerWaveformTriangle);
    }

    layer_mark_dirty(s_canvas);
}

// ── Settings ─────────────────────────────────────────────────────────────────

static void load_settings(void) {
    if (persist_exists(PERSIST_KEY_ANIMATION)) {
        s_animation_enabled = persist_read_bool(PERSIST_KEY_ANIMATION);
    }
    if (persist_exists(PERSIST_KEY_TICKING)) {
        s_ticking_enabled = persist_read_bool(PERSIST_KEY_TICKING);
    }
}

static void save_settings(void) {
    persist_write_bool(PERSIST_KEY_ANIMATION, s_animation_enabled);
    persist_write_bool(PERSIST_KEY_TICKING, s_ticking_enabled);
}

static void inbox_received_handler(DictionaryIterator *iter, void *context) {
    Tuple *anim_t = dict_find(iter, MESSAGE_KEY_AnimationEnabled);
    if (anim_t) {
        s_animation_enabled = (anim_t->value->int32 != 0);
    }

    Tuple *tick_t = dict_find(iter, MESSAGE_KEY_TickingEnabled);
    if (tick_t) {
        s_ticking_enabled = (tick_t->value->int32 != 0);
    }

    save_settings();
    start_animation_timer();
    update_tick_subscription();
    layer_mark_dirty(s_canvas);
}

// ── Window lifecycle ──────────────────────────────────────────────────────────

static void window_load(Window *window) {
    Layer *root   = window_get_root_layer(window);
    GRect  bounds = layer_get_bounds(root);

    s_canvas = layer_create(bounds);
    layer_set_update_proc(s_canvas, canvas_update_proc);
    layer_add_child(root, s_canvas);

    s_bg_bitmap    = gbitmap_create_with_resource(RESOURCE_ID_BACKGROUND);
    s_front_bitmap = gbitmap_create_with_resource(RESOURCE_ID_FRONT_DIAL);

    load_gear_frame(s_gear_idx);

    for (int d = 0; d < 10; d++) {
        s_digits1[d] = gbitmap_create_with_resource(s_digit1_rids[d]);
        s_digits2[d] = gbitmap_create_with_resource(s_digit2_rids[d]);
    }

    s_hour_path   = gpath_create(&HOUR_HAND_INFO);
    s_minute_path = gpath_create(&MINUTE_HAND_INFO);

    time_t now = time(NULL);
    struct tm *t = localtime(&now);
    tick_handler(t, SECOND_UNIT);

    update_tick_subscription();
    start_animation_timer();
}

static void window_unload(Window *window) {
    if (s_anim_timer) { app_timer_cancel(s_anim_timer); s_anim_timer = NULL; }

    layer_destroy(s_canvas);

    if (s_bg_bitmap)    { gbitmap_destroy(s_bg_bitmap);    s_bg_bitmap    = NULL; }
    if (s_front_bitmap) { gbitmap_destroy(s_front_bitmap); s_front_bitmap = NULL; }
    if (s_gear_bitmap)  { gbitmap_destroy(s_gear_bitmap);  s_gear_bitmap  = NULL; }

    for (int d = 0; d < 10; d++) {
        if (s_digits1[d]) { gbitmap_destroy(s_digits1[d]); s_digits1[d] = NULL; }
        if (s_digits2[d]) { gbitmap_destroy(s_digits2[d]); s_digits2[d] = NULL; }
    }

    if (s_hour_path)   { gpath_destroy(s_hour_path);   s_hour_path   = NULL; }
    if (s_minute_path) { gpath_destroy(s_minute_path); s_minute_path = NULL; }

    tick_timer_service_unsubscribe();
}

// ── App entry ─────────────────────────────────────────────────────────────────

static void init(void) {
    load_settings();

    s_window = window_create();
    window_set_background_color(s_window, GColorBlack);
    window_set_window_handlers(s_window, (WindowHandlers) {
        .load   = window_load,
        .unload = window_unload,
    });

    app_message_register_inbox_received(inbox_received_handler);
    app_message_open(128, 128);

    window_stack_push(s_window, true);
}

static void deinit(void) {
    window_destroy(s_window);
}

int main(void) {
    init();
    app_event_loop();
    deinit();
}
