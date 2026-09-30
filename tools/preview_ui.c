/* Uses the production UI and TTF renderer; all service values below are fixtures. */
#include "preview_ui.h"
#include "../main/ttf_font.h"
#include "../main/net_service.h"
#include "../main/instrument.h"
#include "../main/pi_link.h"
static int preview_text(uint16_t *, int, int, int, int, int, uint16_t, const char *);
static time_t preview_time(time_t *out) {
    time_t t = 1789389000;
    if (out)
        *out = t;
    return t;
}
#define time preview_time
#define ttf_draw_text preview_text
#include "../main/ui.c"
#undef ttf_draw_text
#undef time

int64_t preview_now_us = 120000000;
static net_snapshot_t fixture_net;
static instrument_snapshot_t fixture_instrument = {.ready = true, .volume = 65, .last_note = 60};
static pi_link_snapshot_t fixture_pi = {.online = true,
                                        .can_shutdown = true,
                                        .cpu_millicelsius = 48300,
                                        .cpu_khz = 1200000,
                                        .uptime_s = 7200,
                                        .age_ms = 500, .transport=1, .ip="192.0.2.11", .mem_mib=1258, .disk_mib=18942};
static int note_on_count, note_off_count, panic_count, connect_count, shutdown_count;
static int fixture_sample_count = 120;
static bool fixture_sensor_error;
static bool held[128];
static bool checking_password, masked_text_seen;
static const char *forbidden_password = "PreviewSecret42";
static unsigned char *font_bytes;
static esp_partition_t font_partition;
const uint8_t _binary_special_elite_subset_ttf_start[1] = {0};
const uint8_t _binary_special_elite_subset_ttf_end[1] = {0};

const esp_partition_t *esp_partition_find_first(int type, int subtype, const char *label) {
    (void)type;
    (void)subtype;
    return !strcmp(label, "font") ? &font_partition : NULL;
}
esp_err_t esp_partition_mmap(const esp_partition_t *part, size_t offset, size_t size, int flags,
                             const void **out, esp_partition_mmap_handle_t *handle) {
    (void)part;
    (void)size;
    (void)flags;
    *out = font_bytes + offset;
    *handle = 1;
    return ESP_OK;
}
static int preview_text(uint16_t *fb, int w, int h, int x, int y, int size, uint16_t color,
                        const char *text) {
    assert(ttf_text_supported(text));
    if (checking_password) {
        assert(!strstr(text, forbidden_password));
        if (text[0] == '*')
            masked_text_seen = true;
    }
    return ttf_draw_text(fb, w, h, x, y, size, color, text);
}
esp_err_t ina219_read(i2c_master_dev_handle_t dev, float *v, float *a) {
    if (fixture_sensor_error)
        return ESP_FAIL;
    *v = dev == (void *)2 ? 5.10f : 3.91f;
    *a = dev == (void *)2 ? 1.18f : 1.21f;
    return ESP_OK;
}
esp_err_t cw2015_read(i2c_master_dev_handle_t dev, float *v, int *soc) {
    (void)dev;
    if (fixture_sensor_error)
        return ESP_FAIL;
    *v = 3.91f;
    *soc = 79;
    return ESP_OK;
}
esp_err_t stc3117_read(i2c_master_dev_handle_t dev, float *v, float *soc) {
    (void)dev;
    if (fixture_sensor_error)
        return ESP_FAIL;
    *v = 3.91f;
    *soc = 78;
    return ESP_OK;
}
esp_err_t stc3117_read_current(i2c_master_dev_handle_t dev, float *a) {
    (void)dev;
    if (fixture_sensor_error)
        return ESP_FAIL;
    *a = .32f;
    return ESP_OK;
}

float batt_log_capacity_mah(void) {
    return 2980;
}
int batt_log_get(batt_sample_t *out, int cap) {
    int n = cap < fixture_sample_count ? cap : fixture_sample_count;
    for (int i = 0; i < n; i++)
        out[i] = (batt_sample_t){
            .soc = 62 + i * 16 / (n ? n : 1), .mv = 3800 + i, .ma = 1200, .plugged = 1, .bus_mv = 5100, .bus_ma = 1180};
    return n;
}
float vsync_mon_fps(void) {
    return 59.9f;
}
bool vsync_mon_locked(void) {
    return true;
}
void net_service_get_snapshot(net_snapshot_t *out) {
    *out = fixture_net;
}
bool net_service_set_enabled(bool value) {
    fixture_net.enabled = value;
    return true;
}
bool net_service_scan(void) {
    return true;
}
bool net_service_cancel(void) {
    return true;
}
bool net_service_forget(void) {
    return true;
}
bool net_service_connect(const char *ssid, const char *pass) {
    (void)ssid;
    (void)pass;
    connect_count++;
    return true;
}
bool net_service_request_ntp(void) {
    return true;
}
bool net_service_set_time_config(const char *server, int offset) {
    strlcpy(fixture_net.ntp_server, server, sizeof(fixture_net.ntp_server));
    fixture_net.timezone_offset_minutes = offset;
    return true;
}
void instrument_get_snapshot(instrument_snapshot_t *out) {
    *out = fixture_instrument;
}
void instrument_set_active(bool active) {
    fixture_instrument.active = active;
}
bool instrument_active(void) {
    return fixture_instrument.active;
}
void instrument_note_on(uint8_t note, uint8_t velocity) {
    (void)velocity;
    assert(note < 128);
    held[note] = true;
    note_on_count++;
}
void instrument_note_off(uint8_t note) {
    assert(note < 128);
    held[note] = false;
    note_off_count++;
}
void instrument_panic(void) {
    memset(held, 0, sizeof(held));
    panic_count++;
}
void instrument_set_timbre(uint8_t value) {
    fixture_instrument.timbre = value;
}
void instrument_set_volume(uint8_t value) {
    fixture_instrument.volume = value;
}
void instrument_set_octave(int value) {
    fixture_instrument.octave = value < -2 ? -2 : value > 2 ? 2 : value;
}
void pi_link_get_snapshot(pi_link_snapshot_t *out) {
    *out = fixture_pi;
}
bool pi_link_request_shutdown(void) {
    shutdown_count++;
    fixture_pi.shutdown_pending = true;
    return true;
}

static pi_share_snapshot_t fixture_share={.configured=true};
static const char *fixture_file="Preview file / synthetic data only.\nWi-Fi first; CDC telemetry fallback.\n";
void pi_share_get_snapshot(pi_share_snapshot_t *out) { *out=fixture_share; }
bool pi_share_get_status(pi_link_snapshot_t *out) { (void)out; return false; }
bool pi_share_request(pi_share_kind_t kind,const char *name) { (void)name;fixture_share.kind=kind;return true; }
void pi_share_cancel(void) {}
void pi_share_forget(void) { fixture_share.configured=false; }
size_t pi_share_copy_data(size_t offset,void *out,size_t cap) {
    size_t n=strlen(fixture_file);
    if (offset>=n) return 0;
    n-=offset; if(n>cap)n=cap;memcpy(out,fixture_file+offset,n);return n;
}

static void bounds(void) {
    assert(UI_TAB_COUNT == 4);
    assert(s_button_count <= 80);
    for (int i = 0; i < s_button_count; i++) {
        button_t *b = &s_buttons[i];
        assert(b->x >= 0 && b->y >= 0 && b->w > 0 && b->h > 0 && b->x + b->w <= 1024 &&
               b->y + b->h <= 768);
    }
}
static void draw(void) {
    ui_page_draw(7200);
    bounds();
}
static void click_id(int id) {
    draw();
    for (int i = 0; i < s_button_count; i++)
        if (s_buttons[i].id == id) {
            assert(s_buttons[i].enabled);
            int x = s_buttons[i].x + 8, y = s_buttons[i].y + 8;
            ui_handle_touch(x, y, true);
            ui_handle_touch(x, y, false);
            return;
        }
    assert(!"Missing button");
}
static void emit(const char *dir, const char *name) {
    draw();
    char path[1024];
    snprintf(path, sizeof(path), "%s/%s.ppm", dir, name);
    FILE *f = fopen(path, "wb");
    assert(f);
    fprintf(f, "P6\n1024 768\n255\n");
    for (int i = 0; i < 1024 * 768; i++) {
        uint16_t v = s_fb[i];
        unsigned char rgb[] = {((v >> 11) & 31) * 255 / 31, ((v >> 5) & 63) * 255 / 63,
                               (v & 31) * 255 / 31};
        assert(fwrite(rgb, 1, 3, f) == 3);
    }
    fclose(f);
}
static bool no_notes(void) {
    for (int i = 0; i < 128; i++)
        if (held[i])
            return false;
    return true;
}
static void focus_id(int id) {
    draw();
    for (int i = 0; i < s_button_count; i++)
        if (s_buttons[i].id == id) {
            assert(s_buttons[i].enabled);
            s_focus = i;
            return;
        }
    assert(!"Missing focus target");
}
static void key(int row, int col, bool down) {
    ui_key_event(row, col, down);
    ui_process_events();
}
static uint16_t pixel(int x, int y) {
    return s_fb[y * 1024 + x];
}
static void reference_ui_tests(void) {
    ui_lang_t old_lang = s_lang;
    for (int lang = LANG_ZH; lang < LANG_COUNT; lang++) {
        s_lang = lang;
        for (size_t i = 0; i < K_I18N_COUNT; i++) {
            const i18n_entry_t *entry = &k_i18n[i];
            assert(ttf_text_supported(tr(entry->en, entry->zh ? entry->zh : "测试")));
        }
    }
    s_lang = LANG_ZH;
    assert(!strcmp(tr("Unsupported glyph", "🦄"), "Unsupported glyph"));
    s_lang = old_lang;
    change_tab(UI_TAB_SENSORS);
    draw();
    assert(s_present_ok == 9 && N_SENSORS == 10);
    assert(history_sample_x(0, 120) == HISTORY_X + HISTORY_W - 595 * HISTORY_W / 3600);
    assert(history_sample_x(119, 120) == HISTORY_X + HISTORY_W);
    assert(history_sample_x(0, 720) >= HISTORY_X && history_sample_x(0, 720) <= HISTORY_X + (HISTORY_W + 719) / 720);
    click_id(402);
    assert(s_topo_detail == 2 && s_history_metric == 2);
    click_id(423);
    assert(s_history_metric == 3);
    click_id(410);
    assert(!s_topo_detail);
    click_id(404);
    assert(s_topo_detail == 4);
    key(3, 0, true); key(2, 0, true); key(2, 0, false); key(3, 0, false);
    assert(!s_topo_detail);
    batt_sample_t missing = {.soc = -1};
    for (int i = 0; i < 4; i++) assert(history_value(&missing, i) == -1);
    fixture_sample_count = 0;
    fixture_sensor_error = true;
    draw();
    assert(s_dash_last.soc == -1 && !s_dash_last.vbat_ok && !s_dash_last.vbus_ok &&
           !s_dash_last.stc_i_ok);
    fixture_sensor_error = false;
    fixture_sample_count = 120;
    change_tab(UI_TAB_APPS);
    click_id(ACT_INSTRUMENT);
    for (int i = 0; i < 7; i++) {
        int x = PIANO_X + i * PIANO_W / 7 + 40;
        assert(piano_hit(x, PIANO_Y + PIANO_H - 20) == k_white_notes[i]);
    }
    for (int i = 0; i < 5; i++) {
        int x = piano_black_x(i);
        assert(piano_hit(x, PIANO_Y) == k_black_notes[i]);
        assert(piano_hit(x + PIANO_BLACK_W - 1, PIANO_Y + PIANO_BLACK_H - 1) == k_black_notes[i]);
        assert(piano_hit(x + PIANO_BLACK_W / 2, PIANO_Y + PIANO_BLACK_H) != k_black_notes[i]);
    }
    assert(piano_hit(PIANO_X, PIANO_Y - 1) == -1);
    assert(piano_hit(PIANO_X + PIANO_W, PIANO_Y) == -1);
    assert(piano_hit(PIANO_X, PIANO_Y + PIANO_H) == -1);
    int bx = piano_black_x(0) + PIANO_BLACK_W / 2;
    ui_handle_touch(bx, PIANO_Y + 30, true);
    ui_maybe_flush();
    assert(held[61] && !held[60]);
    assert(pixel(bx, PIANO_Y + 30) == pal()->accent);
    ui_handle_touch(bx - 15, PIANO_Y + PIANO_BLACK_H + 30, true);
    ui_maybe_flush();
    assert(!held[61] && held[60]);
    ui_handle_touch(PIANO_X - 10, PIANO_Y + 30, true);
    ui_maybe_flush();
    assert(no_notes());
    ui_handle_touch(0, 0, false);
    unsigned full_before=s_full_draws, piano_before=s_piano_draws;
    key(3, 1, true);
    assert(s_full_draws==full_before && s_piano_draws>piano_before);
    ui_periodic_draw(s_last_uptime_s+1);
    assert(s_full_draws==full_before);
    assert(held[60] && pixel(PIANO_X + 30, PIANO_Y + 30) == pal()->accent);
    key(2, 1, true);
    assert(held[61] && pixel(bx, PIANO_Y + 30) == pal()->accent);
    key(3, 1, false);
    assert(!held[60] && held[61]);
    key(2, 1, false);
    assert(no_notes() && pixel(bx, PIANO_Y + 30) != pal()->accent);
    ui_handle_touch(bx, PIANO_Y + 30, true);
    ui_handle_touch(0, 0, false);
    click_id(ACT_OCT_UP);
    ui_handle_touch(bx, PIANO_Y + 30, true);
    assert(held[73]);
    ui_handle_touch(bx, PIANO_Y + 30, false);
    click_id(ACT_OCT_DOWN);
    change_tab(UI_TAB_SETUP);
    s_settings = 0;
    int commits = preview_nvs_commits;
    uint8_t original[3];
    memcpy(original, s_custom_rgb, 3);
    click_id(ACT_CUSTOM);
    assert(s_color_editor);
    int x = COLOR_TRACK_X + COLOR_TRACK_W - 1, y = COLOR_ROW_Y + 40;
    ui_handle_touch(x, y, true);
    ui_maybe_flush();
    assert(s_color_draft[0] == 255);
    ui_handle_touch(COLOR_TRACK_X, y, true);
    ui_maybe_flush();
    assert(s_color_draft[0] == 0);
    ui_handle_touch(COLOR_TRACK_X, y, false);
    focus_id(ACT_RGB_UP);
    key(3, 10, true);
    key(3, 10, false);
    assert(s_color_draft[0] == 1);
    click_id(ACT_CUSTOM_CANCEL);
    assert(!s_color_editor && preview_nvs_commits == commits && !memcmp(original, s_custom_rgb, 3));
    click_id(ACT_CUSTOM);
    memcpy(s_color_draft, (uint8_t[]){242, 183, 66}, 3);
    click_id(ACT_CUSTOM_APPLY);
    assert(s_accent == CUSTOM_ACCENT && !s_color_editor && preview_nvs_commits == commits + 1);
    assert(pal()->tab_sel_fg == C_WHITE && color_ink(pal()->accent)==C_WHITE);
    memset(s_custom_rgb, 0, 3);
    s_accent = 0;
    s_light = true;
    prefs_load();
    assert(s_accent == CUSTOM_ACCENT && !s_light &&
           !memcmp(s_custom_rgb, (uint8_t[]){242, 183, 66}, 3));
    assert(pal()->tab_sel_fg==C_WHITE && color_ink(pal()->accent)==C_WHITE);
    click_id(ACT_CUSTOM);
    memset(s_color_draft, 0, 3);
    click_id(ACT_CUSTOM_APPLY);
    assert(pal()->tab_sel_fg == C_WHITE);
    focus_id(ACT_CUSTOM);
    draw();
    assert(pixel(41, 495) == pal()->accent2);
    click_id(ACT_CUSTOM);
    memset(s_color_draft, 255, 3);
    click_id(ACT_CUSTOM_APPLY);
    assert(pal()->tab_sel_fg == C_WHITE);
    focus_id(ACT_CUSTOM);
    draw();
    assert(pixel(41, 495) == pal()->accent2);
    click_id(ACT_LIGHT);
    assert(s_light);
    s_light = false;
    prefs_load();
    assert(s_light);
    click_id(ACT_DARK);
    assert(!s_light);
    s_light = true;
    prefs_load();
    assert(!s_light);
    preview_nvs_write_fail = true;
    click_id(ACT_COLOR + 2);
    assert(s_prefs_failed);
    preview_nvs_write_fail = false;
    click_id(ACT_COLOR);
    assert(!s_prefs_failed && pal()->tab_sel_fg == C_WHITE);
    memcpy(s_custom_rgb, original, 3);
    prefs_save();
    click_id(ACT_CUSTOM);
    ui_cancel_input();
    assert(!s_color_editor && s_color_drag == -1);
    click_id(ACT_CUSTOM);
    key(3, 0, true);
    key(2, 0, true);
    key(2, 0, false);
    key(3, 0, false);
    assert(!s_color_editor);
    change_tab(UI_TAB_SENSORS);
    s_notice = NULL;
    puts(
        "Reference UI assertions passed: 9/10 actual presence, fixed-hour scale, unknown readings, "
        "7 white/5 black hit priority and held pixels, custom RGB drag/keyboard/Apply/Cancel/NVS "
        "reload/failure, contrasting text and truthful light/dark persistence.");
}
static void behavior_tests(void) {
    change_tab(UI_TAB_APPS);
    click_id(ACT_INSTRUMENT);
    assert(fixture_instrument.active);
    int volume = fixture_instrument.volume;
    ui_handle_touch(910, 295, true);
    assert(fixture_instrument.volume == volume + 5);
    ui_handle_touch(910, 295, true);
    assert(fixture_instrument.volume == volume + 5);
    ui_handle_touch(910, 295, false);
    fixture_instrument.volume = volume;
    ui_handle_touch(40, 400, true);
    int before = note_on_count;
    ui_handle_touch(40, 400, true);
    assert(note_on_count == before);
    assert(held[60]);
    ui_handle_touch(160, 400, true);
    assert(!held[60] && held[61]);
    ui_handle_touch(160, 400, false);
    assert(no_notes());
    ui_handle_touch(40, 400, true);
    ui_request_tab(UI_TAB_SENSORS);
    draw();
    assert(no_notes() && !fixture_instrument.active && s_tab == UI_TAB_SENSORS);
    change_tab(UI_TAB_APPS);
    click_id(ACT_INSTRUMENT);
    ui_handle_touch(40, 400, true);
    ui_notify_mux(false);
    assert(no_notes() && !fixture_instrument.active);
    ui_notify_mux(true);
    assert(!fixture_instrument.active);
    click_id(ACT_INSTRUMENT);
    assert(fixture_instrument.active);
    change_tab(UI_TAB_APPS);
    click_id(ACT_INSTRUMENT);
    ui_process_events();
    ui_key_event(3, 1, true);
    ui_process_events();
    assert(held[60]);
    before = note_on_count;
    ui_key_event(3, 1, true);
    ui_process_events();
    assert(note_on_count == before);
    ui_handle_touch(40, 400, true);
    ui_key_event(3, 1, false);
    ui_process_events();
    assert(held[60]);
    ui_handle_touch(40, 400, false);
    assert(no_notes());
    ui_key_event(3, 1, true);
    ui_process_events();
    change_tab(UI_TAB_SETUP);
    assert(no_notes());
    change_tab(UI_TAB_APPS);
    click_id(ACT_INSTRUMENT);
    ui_key_event(3, 1, true);
    ui_process_events();
    assert(held[60]);
    ui_input_lost();
    ui_process_events();
    assert(no_notes() && !fixture_instrument.active);
    ui_notify_mux(false);
    ui_key_event(3, 1, true);
    assert(s_keys->count == 0);
    ui_notify_mux(true);
    change_tab(UI_TAB_PI);
    click_id(202);
    assert(shutdown_count == 0);
    click_id(203);
    assert(shutdown_count == 0);
    click_id(202);
    click_id(204);
    assert(shutdown_count == 1);
    fixture_pi.shutdown_pending = false;
    change_tab(UI_TAB_SETUP);
    s_settings = 0;
    draw();
    before = s_light;
    int px = 540, py = 690;
    ui_handle_touch(px, py, true);
    bool after = s_light;
    ui_handle_touch(px, py, true);
    assert(s_light == after && s_light);
    ui_handle_touch(px, py, false);
    editor_open(2, forbidden_password);
    checking_password = true;
    masked_text_seen = false;
    draw();
    checking_password = false;
    assert(masked_text_seen);
    for (int i = 0; i < s_button_count; i++)
        assert(s_buttons[i].id < ACT_EDIT_CHAR || s_buttons[i].id >= ACT_EDIT_CHAR + 128);
    editor_open(2, "");
    key(3, 1, true); key(3, 1, false);
    key(4, 0, true); key(2, 2, true); key(2, 2, false); key(4, 0, false);
    assert(!strcmp(s_edit, "aW"));
    key(1, 10, true); key(1, 10, false);
    assert(!strcmp(s_edit, "a"));
    before = connect_count;
    click_id(ACT_EDIT_CANCEL);
    assert(!s_editor && connect_count == before);
    for (size_t i = 0; i < sizeof(s_edit); i++)
        assert(s_edit[i] == 0);
    editor_open(2, forbidden_password);
    ui_cancel_input();
    assert(!s_editor && !s_edit[0]);
    s_light = false;
    palette_update();
    s_settings = 0;
    change_tab(UI_TAB_SENSORS);
    s_notice = NULL;
    puts("UI assertions passed: four tabs, button bounds, touch debounce, note release/glissando, "
         "keyboard repeat, overflow/MUX cleanup and gating, shutdown confirmation, password "
         "masking/cancellation.");
}
int main(int argc, char **argv) {
    assert(argc == 3);
    FILE *f = fopen(argv[1], "rb");
    assert(f);
    fseek(f, 0, SEEK_END);
    font_partition.size = ftell(f);
    rewind(f);
    font_bytes = malloc(font_partition.size);
    assert(fread(font_bytes, 1, font_partition.size, f) == font_partition.size);
    fclose(f);
    assert(ttf_font_init() == ESP_OK);
    s_fb = calloc(1024 * 768, sizeof(uint16_t));
    assert(s_fb);
    s_ctx = (ui_ctx_t){.bus = (void *)1,
                       .ina_vbat = (void *)1,
                       .ina_vbus = (void *)2,
                       .cw2015 = (void *)3,
                       .stc3117 = (void *)4};
    palette_update();
    fixture_net = (net_snapshot_t){.state = NET_CONNECTED,
                                   .enabled = true,
                                   .connected = true,
                                   .saved_network = true,
                                   .time_valid = true,
                                   .timezone_offset_minutes = 480,
                                   .ap_count = 3};
    strlcpy(fixture_net.ssid, "Preview Wi-Fi", 33);
    strlcpy(fixture_net.ip, "192.0.2.10", 16);
    strlcpy(fixture_net.ntp_server, "pool.ntp.org", 64);
    fixture_net.aps[0] = (net_ap_t){.ssid = "Preview Wi-Fi", .rssi = -42, .secured = true};
    fixture_net.aps[1] = (net_ap_t){.ssid = "Guest fixture", .rssi = -58, .secured = false};
    fixture_net.aps[2] = (net_ap_t){.ssid = "Studio fixture", .rssi = -71, .secured = true};
    ui_set_pi_info("model=CM4 cpu=48.3 freq=1200 up=7200");
    behavior_tests();
    reference_ui_tests();
    emit(argv[2], "firmware-dark");
    uint16_t blue_bg=pal()->bg,blue_card=pal()->card,blue_frame=pal()->frame;
    change_tab(UI_TAB_SETUP);click_id(ACT_COLOR+2);change_tab(UI_TAB_SENSORS);
    assert(pal()->bg!=blue_bg&&pal()->card!=blue_card&&pal()->frame!=blue_frame);
    assert(pal()->text==C_WHITE&&pal()->tab_sel_fg==C_WHITE);
    emit(argv[2],"firmware-purple-dark");
    change_tab(UI_TAB_SETUP);click_id(ACT_COLOR+3);change_tab(UI_TAB_SENSORS);
    emit(argv[2],"firmware-pink-dark");
    change_tab(UI_TAB_SETUP);click_id(ACT_LIGHT);change_tab(UI_TAB_SENSORS);
    emit(argv[2],"firmware-pink-light");
    change_tab(UI_TAB_SETUP);click_id(ACT_DARK);click_id(ACT_COLOR);change_tab(UI_TAB_SENSORS);

    fixture_sample_count = 720;
    click_id(402);
    emit(argv[2], "power-history-dark");
    click_id(423);
    emit(argv[2], "input-history-dark");
    click_id(410);
    click_id(404);
    emit(argv[2], "interfaces-dark");
    click_id(410);
    fixture_sample_count = 0;
    fixture_sensor_error = true;
    emit(argv[2], "sensors-unavailable-dark");
    fixture_sample_count = 120;
    fixture_sensor_error = false;
    s_lang = LANG_EN;
    emit(argv[2], "sensors-english-dark");
    s_lang = LANG_ZH;
    change_tab(UI_TAB_PI);
    emit(argv[2], "pi-dark");
    fixture_share=(pi_share_snapshot_t){.configured=true,.count=2,.kind=SHARE_LIST,
        .files={{.name="readme.txt",.size=1024},{.name="power.csv",.size=8192}}};
    click_id(433); assert(s_pi_page==1);
    emit(argv[2], "pi-files-dark");
    click_id(440);strcpy(fixture_share.name,"readme.txt");
    fixture_share.bytes=strlen(fixture_file);
    emit(argv[2], "pi-file-dark");
    fixture_share.error=3;
    emit(argv[2], "pi-share-error-dark");
    click_id(430);assert(!s_pi_page);
    fixture_share.error=0;

    change_tab(UI_TAB_APPS);
    emit(argv[2], "apps-dark");
    click_id(ACT_INSTRUMENT);
    emit(argv[2], "instrument-dark");
    key(3, 1, true);
    ui_handle_touch(piano_black_x(0) + 30, PIANO_Y + 40, true);
    emit(argv[2], "instrument-held-dark");
    key(3, 1, false);
    ui_handle_touch(0, 0, false);
    click_id(ACT_BACK);
    click_id(ACT_CLOCK);
    emit(argv[2], "clock-dark");
    change_tab(UI_TAB_SETUP);
    emit(argv[2], "settings-dark");
    click_id(ACT_CUSTOM);
    memcpy(s_color_draft, (uint8_t[]){93, 174, 224}, 3);
    emit(argv[2], "settings-custom-dark");
    click_id(ACT_CUSTOM_CANCEL);
    s_lang = LANG_JA;
    emit(argv[2], "settings-japanese-dark");
    s_lang = LANG_ZH;
    s_settings = 1;
    emit(argv[2], "wifi-dark");
    s_settings = 2;
    emit(argv[2], "time-dark");
    s_settings = 0;
    s_light = true;
    palette_update();
    emit(argv[2], "settings-light");
    change_tab(UI_TAB_SENSORS);
    emit(argv[2], "firmware-light");
    change_tab(UI_TAB_SETUP);
    s_settings = 1;
    emit(argv[2], "wifi-light");
    click_id(ACT_AP);
    editor_add('x');
    editor_add('x');
    editor_add('x');
    emit(argv[2], "password-light");
    click_id(ACT_EDIT_CANCEL);
    s_settings = 2;
    emit(argv[2], "time-light");
    puts("All images use synthetic service fixtures; production ui.c and ttf_font.c rendered each "
         "pixel.");
    return 0;
}
