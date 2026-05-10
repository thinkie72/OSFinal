//
// main.c — raylib UI for the NBA mid-game start/sit simulator.
//
// State machine:
//   ST_SELECTING -> user picks two teams + score + time, hits Simulate
//   ST_RUNNING   -> background workers chew through TOTAL_SIMS jobs; we draw a progress bar
//   ST_RESULTS   -> 2x2 matrix of win % across the four start/sit scenarios
//
// Visual style is loosely modeled on Apple Keynote: SF Pro from /System/Library/Fonts,
// soft dark slate background, generous whitespace, rounded surfaces.
//

#include "raylib.h"
#include "thread_compat.h"
#include "utils.h"

#include <stdio.h>
#include <string.h>
#include <stdlib.h>

#define WIN_W 1440
#define WIN_H 800

#define LOGO_COLS 6
#define LOGO_W   150
#define LOGO_H   100
#define LOGO_PAD 14
#define GRID_X   40
#define GRID_Y   130

// ---- Keynote-ish palette ----
static const Color C_BG          = (Color){ 22,  24,  30, 255 };
static const Color C_SURFACE     = (Color){ 32,  34,  42, 255 };
static const Color C_SURFACE_HI  = (Color){ 42,  46,  56, 255 };
static const Color C_BORDER      = (Color){ 64,  68,  80, 255 };
static const Color C_TEXT        = (Color){235, 236, 240, 255 };
static const Color C_TEXT_DIM    = (Color){170, 174, 188, 255 };
static const Color C_ACCENT_A    = (Color){ 92, 168, 255, 255 }; // soft blue   = Team A
static const Color C_ACCENT_B    = (Color){255, 138,  92, 255 }; // soft orange = Team B
static const Color C_GREEN       = (Color){ 96, 196, 140, 255 };
static const Color C_BTN         = (Color){ 92, 168, 255, 255 };
static const Color C_BTN_HOVER   = (Color){130, 188, 255, 255 };
static const Color C_BTN_DISABLED= (Color){ 60,  64,  74, 255 };

// ---- Type system (Keynote uses SF Pro at varying sizes/weights) ----
typedef enum { F_TITLE, F_HEAD, F_BODY, F_SMALL, F_NUM, F_COUNT } FontSlot;
static Font fonts[F_COUNT];
static int  font_size[F_COUNT] = { 36, 24, 18, 14, 32 };

typedef enum { ST_SELECTING, ST_RUNNING, ST_RESULTS } UIState;

static UIState   ui_state    = ST_SELECTING;
static int       team_a_idx  = -1;
static int       team_b_idx  = -1;
static int       score_a     = 0;
static int       score_b     = 0;
static int       time_min    = 12;
static int       time_sec    = 0;
static int       focus_field = -1;   // 0=score_a 1=score_b 2=time_min 3=time_sec

static Texture2D logos[NUM_TEAMS];
static int       logos_loaded[NUM_TEAMS];

// Aggregated results filled in once g_completed_sims hits TOTAL_SIMS.
static int agg_a_wins[NUM_SCENARIOS];
static int agg_b_wins[NUM_SCENARIOS];
static int agg_margin_sum[NUM_SCENARIOS]; // sum of (final_a - final_b)

// ---------- font + text helpers ----------

static void load_fonts(void) {
    // raylib's stb_truetype loader chokes on (a) variable fonts like SFNS.ttf
    // and (b) TrueType collections like HelveticaNeue.ttc — both render as
    // tofu/question marks or fall through to the bitmap default. We need a
    // plain single-face .ttf. Tahoma/Verdana/Arial all live in macOS
    // Supplemental fonts and load cleanly. Drop your own TTF at
    // assets/font.ttf to override.
    const char *candidates[] = {
        "assets/font.ttf",
        "/System/Library/Fonts/Supplemental/Tahoma.ttf",
        "/System/Library/Fonts/Supplemental/Verdana.ttf",
        "/System/Library/Fonts/Supplemental/Trebuchet MS.ttf",
        "/System/Library/Fonts/Supplemental/Arial.ttf",
        "C:\\Windows\\Fonts\\segoeui.ttf",
        "C:\\Windows\\Fonts\\tahoma.ttf",
        NULL
    };

    // Find the first candidate that LoadFontEx can actually decode. Some
    // .ttf files (or .ttc files mistakenly named .ttf) come back with a
    // degenerate font; in that case we move on instead of getting stuck.
    const char *path = NULL;
    for (int i = 0; candidates[i]; i++) {
        if (!FileExists(candidates[i])) continue;
        Font probe = LoadFontEx(candidates[i], 18, NULL, 0);
        int ok = probe.glyphCount > 1;
        UnloadFont(probe);
        if (ok) { path = candidates[i]; break; }
    }

    for (int i = 0; i < F_COUNT; i++) {
        if (path) {
            fonts[i] = LoadFontEx(path, font_size[i], NULL, 0);
            if (fonts[i].glyphCount <= 1) {
                fonts[i] = GetFontDefault();
            } else {
                SetTextureFilter(fonts[i].texture, TEXTURE_FILTER_BILINEAR);
            }
        } else {
            fonts[i] = GetFontDefault();
        }
    }
}

static void text(FontSlot f, const char *s, int x, int y, Color c) {
    DrawTextEx(fonts[f], s, (Vector2){(float)x, (float)y}, (float)font_size[f], 0.5f, c);
}
static void text_centered(FontSlot f, const char *s, Rectangle r, Color c) {
    Vector2 sz = MeasureTextEx(fonts[f], s, (float)font_size[f], 0.5f);
    DrawTextEx(fonts[f], s,
               (Vector2){ r.x + (r.width  - sz.x) / 2.0f,
                          r.y + (r.height - sz.y) / 2.0f },
               (float)font_size[f], 0.5f, c);
}
static int text_width(FontSlot f, const char *s) {
    return (int)MeasureTextEx(fonts[f], s, (float)font_size[f], 0.5f).x;
}

// ---------- helpers ----------

static void filename_from_team_name(const char *name, char *out, int outlen) {
    int j = 0;
    for (int i = 0; name[i] && j < outlen - 5; i++) {
        out[j++] = (name[i] == ' ') ? '_' : name[i];
    }
    out[j++] = '.';
    out[j++] = 'p';
    out[j++] = 'n';
    out[j++] = 'g';
    out[j]   = '\0';
}

static void load_logos(void) {
    for (int i = 0; i < g_team_count; i++) {
        char fname[128];
        char path[160];
        filename_from_team_name(g_teams[i]->team_name, fname, sizeof(fname));
        snprintf(path, sizeof(path), "logos/%s", fname);
        if (FileExists(path)) {
            logos[i] = LoadTexture(path);
            logos_loaded[i] = 1;
        } else {
            logos_loaded[i] = 0;
        }
    }
}

static Color team_fallback_color(int idx) {
    unsigned char r = (unsigned char)(60  + (idx * 73)  % 140);
    unsigned char g = (unsigned char)(70  + (idx * 131) % 130);
    unsigned char b = (unsigned char)(95  + (idx * 191) % 110);
    return (Color){ r, g, b, 255 };
}

static int point_in_rect(int x, int y, Rectangle r) {
    return x >= r.x && x <= r.x + r.width && y >= r.y && y <= r.y + r.height;
}

static void surface(Rectangle r, Color c) {
    DrawRectangleRounded(r, 0.18f, 8, c);
}
static void surface_outline(Rectangle r, float thick, Color c) {
    DrawRectangleRoundedLinesEx(r, 0.18f, 8, thick, c);
}

// Draw a rounded button. Returns 1 if clicked this frame.
static int button(Rectangle r, const char *label, int enabled, Color accent) {
    Vector2 m = GetMousePosition();
    int hovered = enabled && point_in_rect((int)m.x, (int)m.y, r);
    Color bg;
    if (!enabled)   bg = C_BTN_DISABLED;
    else if (hovered) {
        // hovered = lighter version of accent
        bg = (Color){
            (unsigned char)(accent.r + (255 - accent.r) / 4),
            (unsigned char)(accent.g + (255 - accent.g) / 4),
            (unsigned char)(accent.b + (255 - accent.b) / 4),
            255
        };
    } else {
        bg = accent;
    }
    DrawRectangleRounded(r, 0.35f, 8, bg);
    text_centered(F_BODY, label, r, enabled ? C_TEXT : C_TEXT_DIM);
    return enabled && hovered && IsMouseButtonPressed(MOUSE_BUTTON_LEFT);
}

// ---------- team grid ----------

static void draw_team_grid(void) {
    Vector2 m = GetMousePosition();
    int mouse_clicked = IsMouseButtonPressed(MOUSE_BUTTON_LEFT);

    for (int i = 0; i < g_team_count; i++) {
        int col = i % LOGO_COLS;
        int row = i / LOGO_COLS;
        Rectangle r = {
            GRID_X + col * (LOGO_W + LOGO_PAD),
            GRID_Y + row * (LOGO_H + LOGO_PAD),
            LOGO_W, LOGO_H
        };
        int hovered = point_in_rect((int)m.x, (int)m.y, r);

        Color border    = C_BORDER;
        float border_th = 1.5f;
        if (i == team_a_idx)      { border = C_ACCENT_A; border_th = 3.0f; }
        else if (i == team_b_idx) { border = C_ACCENT_B; border_th = 3.0f; }
        else if (hovered)         { border = C_TEXT_DIM; border_th = 2.0f; }

        surface(r, hovered ? C_SURFACE_HI : C_SURFACE);

        // logo or fallback inside an inset rect
        Rectangle inner = { r.x + 8, r.y + 8, r.width - 16, r.height - 16 };
        if (logos_loaded[i]) {
            float scale_x = inner.width  / (float)logos[i].width;
            float scale_y = inner.height / (float)logos[i].height;
            float scale   = (scale_x < scale_y) ? scale_x : scale_y;
            float dw = logos[i].width  * scale;
            float dh = logos[i].height * scale;
            DrawTextureEx(logos[i],
                          (Vector2){ inner.x + (inner.width  - dw) / 2,
                                     inner.y + (inner.height - dh) / 2 },
                          0.0f, scale, WHITE);
        } else {
            DrawRectangleRounded(inner, 0.22f, 8, team_fallback_color(i));
            text_centered(F_SMALL, g_teams[i]->team_name, inner, C_TEXT);
        }
        surface_outline(r, border_th, border);

        if (mouse_clicked && hovered) {
            if (i == team_a_idx)        team_a_idx = -1;
            else if (i == team_b_idx)   team_b_idx = -1;
            else if (team_a_idx == -1)  team_a_idx = i;
            else if (team_b_idx == -1)  team_b_idx = i;
        }
    }
}

// ---------- right-hand panel: inputs + simulate ----------

static void capture_digit_input(int *target, int max_val) {
    int key = GetCharPressed();
    while (key > 0) {
        if (key >= '0' && key <= '9') {
            int next = (*target) * 10 + (key - '0');
            if (next <= max_val) *target = next;
        }
        key = GetCharPressed();
    }
    if (IsKeyPressed(KEY_BACKSPACE)) *target /= 10;
}

static void draw_field(Rectangle r, const char *label, int value, int field_id) {
    text(F_SMALL, label, (int)r.x, (int)r.y - 22, C_TEXT_DIM);
    int focused = (focus_field == field_id);
    surface(r, focused ? C_SURFACE_HI : C_SURFACE);
    surface_outline(r, focused ? 2.0f : 1.5f, focused ? C_ACCENT_A : C_BORDER);

    char buf[16];
    snprintf(buf, sizeof(buf), "%d", value);
    text_centered(F_NUM, buf, r, C_TEXT);

    if (IsMouseButtonPressed(MOUSE_BUTTON_LEFT)) {
        Vector2 m = GetMousePosition();
        if (point_in_rect((int)m.x, (int)m.y, r)) focus_field = field_id;
    }
}

static int draw_inputs_panel(void) {
    int px = GRID_X + LOGO_COLS * (LOGO_W + LOGO_PAD) + 30;
    int py = GRID_Y;
    int pw = WIN_W - px - 40;
    int ph = WIN_H - py - 40;

    Rectangle panel = { (float)px, (float)py, (float)pw, (float)ph };
    surface(panel, C_SURFACE);
    surface_outline(panel, 1.0f, C_BORDER);

    int x = px + 24;
    int y = py + 24;

    text(F_HEAD, "Matchup", x, y, C_TEXT);
    y += 40;

    const char *a_name = (team_a_idx >= 0) ? g_teams[team_a_idx]->team_name : "Pick a team";
    const char *b_name = (team_b_idx >= 0) ? g_teams[team_b_idx]->team_name : "Pick a team";
    text(F_SMALL, "Team A", x, y, C_TEXT_DIM); y += 18;
    text(F_BODY,  a_name,   x, y, C_ACCENT_A); y += 28;
    text(F_SMALL, "Team B", x, y, C_TEXT_DIM); y += 18;
    text(F_BODY,  b_name,   x, y, C_ACCENT_B); y += 40;

    text(F_HEAD, "Game state", x, y, C_TEXT);
    y += 50;

    Rectangle ra = { (float)x,         (float)y, 110, 44 };
    Rectangle rb = { (float)(x + 130), (float)y, 110, 44 };
    draw_field(ra, "Team A score", score_a, 0);
    draw_field(rb, "Team B score", score_b, 1);
    y += 76;

    Rectangle rm = { (float)x,         (float)y, 75, 44 };
    Rectangle rs = { (float)(x + 95),  (float)y, 75, 44 };
    draw_field(rm, "Minutes left", time_min, 2);
    draw_field(rs, "Seconds left", time_sec, 3);
    y += 80;

    if (focus_field == 0)      capture_digit_input(&score_a,  299);
    else if (focus_field == 1) capture_digit_input(&score_b,  299);
    else if (focus_field == 2) capture_digit_input(&time_min, 48);
    else if (focus_field == 3) capture_digit_input(&time_sec, 59);

    text(F_SMALL,
         "Click a field, then type to set its value.",
         x, y, C_TEXT_DIM);
    y += 24;
    text(F_SMALL,
         "Backspace clears the last digit.",
         x, y, C_TEXT_DIM);

    int ready = (team_a_idx >= 0) && (team_b_idx >= 0)
              && (time_min > 0 || time_sec > 0);
    Rectangle rsim = { (float)x, (float)(py + ph - 70), (float)(pw - 48), 50 };
    if (button(rsim, "Simulate", ready, C_BTN)) return 1;
    return 0;
}

// ---------- running state: progress bar ----------

static void draw_running(void) {
    int total = TOTAL_SIMS;
    long done = g_completed_sims;
    if (done > total) done = total;
    float frac = (float)done / (float)total;

    int bw = 640, bh = 16;
    int bx = (WIN_W - bw) / 2;
    int by = WIN_H / 2;

    text_centered(F_TITLE, "Simulating",
                  (Rectangle){0, (float)(by - 80), WIN_W, 40}, C_TEXT);
    text_centered(F_BODY,
                  TextFormat("%ld of %d games completed", done, total),
                  (Rectangle){0, (float)(by - 30), WIN_W, 20}, C_TEXT_DIM);

    Rectangle track = { (float)bx, (float)by, (float)bw, (float)bh };
    Rectangle fill  = { (float)bx, (float)by, bw * frac, (float)bh };
    DrawRectangleRounded(track, 1.0f, 8, C_SURFACE);
    DrawRectangleRounded(fill,  1.0f, 8, C_GREEN);
}

// ---------- results ----------

static void aggregate_results(void) {
    for (int s = 0; s < NUM_SCENARIOS; s++) {
        agg_a_wins[s] = agg_b_wins[s] = 0;
        agg_margin_sum[s] = 0;
    }
    for (int i = 0; i < TOTAL_SIMS; i++) {
        SimResult r = g_sim_results[i];
        int s = r.scenario;
        if (r.final_a > r.final_b)      agg_a_wins[s]++;
        else if (r.final_b > r.final_a) agg_b_wins[s]++;
        // sim runs OT until someone wins, so no tie branch
        agg_margin_sum[s] += (r.final_a - r.final_b);
    }
}

// scenario index helper: low bit = A in, high bit = B in
static int scen_idx(int a_in, int b_in) { return (a_in ? 1 : 0) | (b_in ? 2 : 0); }

static void draw_cell(Rectangle r, const char *title, int s) {
    surface(r, C_SURFACE);
    surface_outline(r, 1.0f, C_BORDER);

    text(F_BODY, title, (int)r.x + 18, (int)r.y + 16, C_TEXT_DIM);

    int a_pct = (agg_a_wins[s] * 100) / SIMS_PER_SCENARIO;
    int b_pct = (agg_b_wins[s] * 100) / SIMS_PER_SCENARIO;
    float avg_margin = agg_margin_sum[s] / (float)SIMS_PER_SCENARIO;

    const char *a_name = g_teams[team_a_idx]->team_name;
    const char *b_name = g_teams[team_b_idx]->team_name;
    int x = (int)r.x + 18;
    int y = (int)r.y + 52;

    text(F_BODY,  a_name, x, y, C_ACCENT_A);
    char pct_a[16]; snprintf(pct_a, sizeof(pct_a), "%d%%", a_pct);
    int pa_w = text_width(F_NUM, pct_a);
    text(F_NUM, pct_a, (int)(r.x + r.width - 18 - pa_w), y - 8, C_ACCENT_A);
    y += 36;

    text(F_BODY,  b_name, x, y, C_ACCENT_B);
    char pct_b[16]; snprintf(pct_b, sizeof(pct_b), "%d%%", b_pct);
    int pb_w = text_width(F_NUM, pct_b);
    text(F_NUM, pct_b, (int)(r.x + r.width - 18 - pb_w), y - 8, C_ACCENT_B);
    y += 44;

    char margin_s[64];
    snprintf(margin_s, sizeof(margin_s), "Avg margin (A - B): %+.1f", avg_margin);
    text(F_SMALL, margin_s, x, y, C_TEXT_DIM);
}

static void draw_results(void) {
    const char *a_name = g_teams[team_a_idx]->team_name;
    const char *b_name = g_teams[team_b_idx]->team_name;
    const char *a_star = g_teams[team_a_idx]->player->player_name;
    const char *b_star = g_teams[team_b_idx]->player->player_name;

    // Title
    char header[256];
    snprintf(header, sizeof(header),
             "%s  %d  -  %d  %s     %d:%02d remaining",
             a_name, score_a, score_b, b_name, time_min, time_sec);
    text(F_TITLE, header, 40, 32, C_TEXT);

    char sub[256];
    snprintf(sub, sizeof(sub), "Stars: %s vs %s", a_star, b_star);
    text(F_BODY, sub, 40, 84, C_TEXT_DIM);

    // 2x2 matrix layout — column / row labels above & to the left of cells.
    int top_y = 140;
    int cell_w = 380, cell_h = 170, gap = 18;
    int col1_x = 320, col2_x = col1_x + cell_w + gap;

    text(F_BODY, TextFormat("%s star IN",  b_name), col1_x + 18, top_y - 32, C_ACCENT_B);
    text(F_BODY, TextFormat("%s star OUT", b_name), col2_x + 18, top_y - 32, C_ACCENT_B);

    text(F_BODY, TextFormat("%s star IN",  a_name), 60, top_y +   76, C_ACCENT_A);
    text(F_BODY, TextFormat("%s star OUT", a_name), 60, top_y + 264, C_ACCENT_A);

    Rectangle c00 = { (float)col1_x, (float)top_y,                cell_w, cell_h };
    Rectangle c01 = { (float)col2_x, (float)top_y,                cell_w, cell_h };
    Rectangle c10 = { (float)col1_x, (float)(top_y + cell_h + gap), cell_w, cell_h };
    Rectangle c11 = { (float)col2_x, (float)(top_y + cell_h + gap), cell_w, cell_h };
    draw_cell(c00, "Both stars play",        scen_idx(1, 1));
    draw_cell(c01, "A plays, B sits",        scen_idx(1, 0));
    draw_cell(c10, "A sits, B plays",        scen_idx(0, 1));
    draw_cell(c11, "Neither star plays",     scen_idx(0, 0));

    // Summary panel below the matrix
    int panel_y = top_y + 2 * cell_h + gap + 22;
    Rectangle summary = { 60, (float)panel_y, WIN_W - 120, 110 };
    surface(summary, C_SURFACE);
    surface_outline(summary, 1.0f, C_BORDER);

    int sx = (int)summary.x + 24;
    int sy = (int)summary.y + 18;
    text(F_HEAD, "Start / sit value (other team's star plays)", sx, sy, C_TEXT);
    sy += 38;

    int s_AinBin   = scen_idx(1, 1);
    int s_AoutBin  = scen_idx(0, 1);
    int s_BoutAin  = scen_idx(1, 0);
    int a_with    = (agg_a_wins[s_AinBin]  * 100) / SIMS_PER_SCENARIO;
    int a_without = (agg_a_wins[s_AoutBin] * 100) / SIMS_PER_SCENARIO;
    int b_with    = (agg_b_wins[s_AinBin]  * 100) / SIMS_PER_SCENARIO;
    int b_without = (agg_b_wins[s_BoutAin] * 100) / SIMS_PER_SCENARIO;

    char line[256];
    snprintf(line, sizeof(line),
             "%s wins %d%% with %s, %d%% without  (delta %+d)",
             a_name, a_with, a_star, a_without, a_with - a_without);
    text(F_BODY, line, sx, sy, C_ACCENT_A);
    sy += 28;
    snprintf(line, sizeof(line),
             "%s wins %d%% with %s, %d%% without  (delta %+d)",
             b_name, b_with, b_star, b_without, b_with - b_without);
    text(F_BODY, line, sx, sy, C_ACCENT_B);

    int btn_y = panel_y + 110 + 24;
    Rectangle r_again = { 60,  (float)btn_y, 220, 46 };
    Rectangle r_back  = { 300, (float)btn_y, 220, 46 };
    if (button(r_again, "Simulate again", 1, C_BTN)) {
        ui_state = ST_RUNNING;
        run_simulations(g_teams[team_a_idx], g_teams[team_b_idx],
                        score_a, score_b,
                        (float)(time_min * 60 + time_sec));
    }
    if (button(r_back, "Pick new teams", 1, C_SURFACE_HI)) {
        ui_state = ST_SELECTING;
    }
}

// ---------- main ----------

int main(void) {
    setup();

    SetConfigFlags(FLAG_MSAA_4X_HINT | FLAG_WINDOW_HIGHDPI);
    InitWindow(WIN_W, WIN_H, "NBA Star Sit Simulator");
    SetTargetFPS(60);
    load_fonts();
    load_logos();

    while (!WindowShouldClose()) {
        if (ui_state == ST_RUNNING && g_completed_sims >= TOTAL_SIMS) {
            aggregate_results();
            ui_state = ST_RESULTS;
        }

        BeginDrawing();
        ClearBackground(C_BG);

        if (ui_state == ST_SELECTING) {
            text(F_TITLE, "NBA Star Sit Simulator", 40, 32, C_TEXT);
            text(F_BODY,
                 "Pick two teams, set the score and clock, then run 100 simulations per scenario.",
                 40, 84, C_TEXT_DIM);
            draw_team_grid();
            if (draw_inputs_panel()) {
                ui_state = ST_RUNNING;
                run_simulations(g_teams[team_a_idx], g_teams[team_b_idx],
                                score_a, score_b,
                                (float)(time_min * 60 + time_sec));
            }
        } else if (ui_state == ST_RUNNING) {
            draw_running();
        } else {
            draw_results();
        }

        EndDrawing();
    }

    CloseWindow();
    return 0;
}
