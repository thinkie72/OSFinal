//
// main.c — raylib UI for the NBA mid-game start/sit simulator.
//
// State machine:
//   ST_SELECTING -> user picks two teams + score + time, hits Simulate
//   ST_RUNNING   -> background workers chew through TOTAL_SIMS jobs; we draw a progress bar
//   ST_RESULTS   -> 2x2 matrix of win % across the four start/sit scenarios
//

#include "raylib.h"
#include "thread_compat.h"
#include "utils.h"

#include <stdio.h>
#include <string.h>
#include <stdlib.h>

#define WIN_W 1280
#define WIN_H 800

#define LOGO_COLS 6
#define LOGO_ROWS 5
#define LOGO_W   140
#define LOGO_H   90
#define LOGO_PAD 10
#define GRID_X   20
#define GRID_Y   80

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
static int agg_ties[NUM_SCENARIOS];
static int agg_margin_sum[NUM_SCENARIOS]; // sum of (final_a - final_b)

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
    // deterministic-ish color from index so each team gets a distinct fallback tile
    unsigned char r = (unsigned char)(40  + (idx * 73)  % 180);
    unsigned char g = (unsigned char)(60  + (idx * 131) % 160);
    unsigned char b = (unsigned char)(90  + (idx * 191) % 140);
    return (Color){ r, g, b, 255 };
}

static int point_in_rect(int x, int y, Rectangle r) {
    return x >= r.x && x <= r.x + r.width && y >= r.y && y <= r.y + r.height;
}

// Draw a button. Returns 1 if clicked this frame.
static int button(Rectangle r, const char *label, int enabled) {
    Vector2 m = GetMousePosition();
    int hovered = enabled && point_in_rect((int)m.x, (int)m.y, r);
    Color bg = enabled
                  ? (hovered ? (Color){80,140,220,255} : (Color){60,100,180,255})
                  : (Color){70,70,80,255};
    DrawRectangleRec(r, bg);
    DrawRectangleLinesEx(r, 2, (Color){200,200,210,255});
    int tw = MeasureText(label, 18);
    DrawText(label, (int)(r.x + (r.width - tw) / 2),
                    (int)(r.y + (r.height - 18) / 2), 18, RAYWHITE);
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

        Color border = (Color){80,80,90,255};
        if (i == team_a_idx)      border = (Color){80, 180, 255, 255};
        else if (i == team_b_idx) border = (Color){255, 120, 90, 255};

        DrawRectangleRec(r, (Color){25,25,32,255});
        if (logos_loaded[i]) {
            float scale_x = (float)LOGO_W / logos[i].width;
            float scale_y = (float)LOGO_H / logos[i].height;
            float scale   = (scale_x < scale_y) ? scale_x : scale_y;
            float dw = logos[i].width * scale;
            float dh = logos[i].height * scale;
            DrawTextureEx(logos[i],
                          (Vector2){ r.x + (LOGO_W - dw) / 2,
                                     r.y + (LOGO_H - dh) / 2 },
                          0.0f, scale, WHITE);
        } else {
            DrawRectangle((int)r.x + 4, (int)r.y + 4, LOGO_W - 8, LOGO_H - 8,
                          team_fallback_color(i));
            int tw = MeasureText(g_teams[i]->team_name, 12);
            int x  = (int)(r.x + (LOGO_W - tw) / 2);
            int y  = (int)(r.y + (LOGO_H - 12) / 2);
            DrawText(g_teams[i]->team_name, x + 1, y + 1, 12, BLACK);
            DrawText(g_teams[i]->team_name, x,     y,     12, RAYWHITE);
        }
        DrawRectangleLinesEx(r, 3, border);

        if (mouse_clicked && point_in_rect((int)m.x, (int)m.y, r)) {
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
    DrawText(label, (int)r.x, (int)r.y - 22, 16, (Color){200,200,210,255});
    Color bg = (focus_field == field_id) ? (Color){50,80,130,255}
                                         : (Color){35,35,45,255};
    DrawRectangleRec(r, bg);
    DrawRectangleLinesEx(r, 2,
        focus_field == field_id ? (Color){80,180,255,255}
                                : (Color){80,80,90,255});

    char buf[16];
    snprintf(buf, sizeof(buf), "%d", value);
    int tw = MeasureText(buf, 22);
    DrawText(buf, (int)(r.x + (r.width - tw) / 2),
                  (int)(r.y + (r.height - 22) / 2), 22, RAYWHITE);

    if (IsMouseButtonPressed(MOUSE_BUTTON_LEFT)) {
        Vector2 m = GetMousePosition();
        if (point_in_rect((int)m.x, (int)m.y, r)) focus_field = field_id;
    }
}

static int draw_inputs_panel(void) {
    int px = GRID_X + LOGO_COLS * (LOGO_W + LOGO_PAD) + 20;
    int py = GRID_Y;
    int pw = WIN_W - px - 20;

    DrawRectangle(px, py, pw, WIN_H - py - 20, (Color){20,20,28,255});
    DrawRectangleLines(px, py, pw, WIN_H - py - 20, (Color){80,80,90,255});

    int x = px + 16;
    int y = py + 16;

    DrawText("Selection", x, y, 22, RAYWHITE);
    y += 32;

    const char *a_name = (team_a_idx >= 0) ? g_teams[team_a_idx]->team_name : "(click to pick)";
    const char *b_name = (team_b_idx >= 0) ? g_teams[team_b_idx]->team_name : "(click to pick)";
    DrawText(TextFormat("Team A: %s", a_name), x, y, 16, (Color){80,180,255,255}); y += 22;
    DrawText(TextFormat("Team B: %s", b_name), x, y, 16, (Color){255,120,90,255}); y += 32;

    Rectangle ra = { x,       y, 100, 38 };
    Rectangle rb = { x + 120, y, 100, 38 };
    draw_field(ra, "Score A", score_a, 0);
    draw_field(rb, "Score B", score_b, 1);
    y += 60;

    Rectangle rm = { x,       y, 60, 38 };
    Rectangle rs = { x + 80,  y, 60, 38 };
    draw_field(rm, "Min left", time_min, 2);
    draw_field(rs, "Sec left", time_sec, 3);
    y += 60;

    if (focus_field == 0)      capture_digit_input(&score_a,  299);
    else if (focus_field == 1) capture_digit_input(&score_b,  299);
    else if (focus_field == 2) capture_digit_input(&time_min, 48);
    else if (focus_field == 3) capture_digit_input(&time_sec, 59);

    int ready = (team_a_idx >= 0) && (team_b_idx >= 0)
              && (time_min > 0 || time_sec > 0);
    Rectangle rsim = { x, WIN_H - 80, 220, 44 };
    if (button(rsim, "Simulate", ready)) {
        return 1;
    }
    return 0;
}

// ---------- running state: progress bar ----------

static void draw_running(void) {
    int total = TOTAL_SIMS;
    long done = g_completed_sims;
    if (done > total) done = total;
    float frac = (float)done / (float)total;

    int bw = 600, bh = 40;
    int bx = (WIN_W - bw) / 2;
    int by = WIN_H / 2 - bh / 2;

    DrawText("Simulating...", bx, by - 50, 28, RAYWHITE);
    DrawRectangle(bx, by, bw, bh, (Color){35,35,45,255});
    DrawRectangle(bx, by, (int)(bw * frac), bh, (Color){80,180,120,255});
    DrawRectangleLines(bx, by, bw, bh, (Color){200,200,210,255});

    DrawText(TextFormat("%ld / %d", done, total),
             bx, by + bh + 10, 18, (Color){200,200,210,255});
}

// ---------- results ----------

static void aggregate_results(void) {
    for (int s = 0; s < NUM_SCENARIOS; s++) {
        agg_a_wins[s] = agg_b_wins[s] = agg_ties[s] = 0;
        agg_margin_sum[s] = 0;
    }
    for (int i = 0; i < TOTAL_SIMS; i++) {
        SimResult r = g_sim_results[i];
        int s = r.scenario;
        if (r.final_a > r.final_b)      agg_a_wins[s]++;
        else if (r.final_b > r.final_a) agg_b_wins[s]++;
        else                             agg_ties[s]++;
        agg_margin_sum[s] += (r.final_a - r.final_b);
    }
}

// scenario index helper: low bit = A in, high bit = B in
static int scen_idx(int a_in, int b_in) { return (a_in ? 1 : 0) | (b_in ? 2 : 0); }

static void draw_cell(Rectangle r, const char *title, int s) {
    DrawRectangleRec(r, (Color){25,25,32,255});
    DrawRectangleLinesEx(r, 2, (Color){80,80,90,255});
    DrawText(title, (int)r.x + 12, (int)r.y + 10, 18, (Color){180,200,255,255});

    int a_pct = (agg_a_wins[s] * 100) / SIMS_PER_SCENARIO;
    int b_pct = (agg_b_wins[s] * 100) / SIMS_PER_SCENARIO;
    int t_pct = 100 - a_pct - b_pct;
    float avg_margin = agg_margin_sum[s] / (float)SIMS_PER_SCENARIO;

    const char *a_name = g_teams[team_a_idx]->team_name;
    const char *b_name = g_teams[team_b_idx]->team_name;
    int y = (int)r.y + 40;
    DrawText(TextFormat("%s win: %d%%", a_name, a_pct), (int)r.x + 12, y, 18, (Color){80,180,255,255}); y += 24;
    DrawText(TextFormat("%s win: %d%%", b_name, b_pct), (int)r.x + 12, y, 18, (Color){255,120,90,255}); y += 24;
    DrawText(TextFormat("Ties: %d%%",   t_pct),         (int)r.x + 12, y, 18, (Color){200,200,210,255}); y += 24;
    DrawText(TextFormat("Avg margin (A-B): %+.1f", avg_margin),
             (int)r.x + 12, y, 16, (Color){200,200,210,255});
}

static void draw_results(void) {
    const char *a_name = g_teams[team_a_idx]->team_name;
    const char *b_name = g_teams[team_b_idx]->team_name;
    const char *a_star = g_teams[team_a_idx]->player->player_name;
    const char *b_star = g_teams[team_b_idx]->player->player_name;

    DrawText(TextFormat("%s %d  -  %d %s    (%d:%02d remaining)",
                        a_name, score_a, score_b, b_name, time_min, time_sec),
             20, 20, 24, RAYWHITE);
    DrawText(TextFormat("Star players: %s (A)  vs  %s (B)", a_star, b_star),
             20, 50, 16, (Color){200,200,210,255});

    int top_y = 110;
    DrawText("",                     0, top_y - 30, 16, RAYWHITE);
    DrawText(TextFormat("%s star IN",  b_name), 480, top_y - 28, 18, (Color){255,120,90,255});
    DrawText(TextFormat("%s star OUT", b_name), 870, top_y - 28, 18, (Color){255,120,90,255});

    int cell_w = 380, cell_h = 200;
    int col1_x = 440, col2_x = col1_x + cell_w + 20;
    DrawText(TextFormat("%s\nstar IN",  a_name), 30, top_y +   30, 18, (Color){80,180,255,255});
    DrawText(TextFormat("%s\nstar OUT", a_name), 30, top_y + 270, 18, (Color){80,180,255,255});

    Rectangle c00 = { col1_x, top_y,           cell_w, cell_h }; // A in,  B in
    Rectangle c01 = { col2_x, top_y,           cell_w, cell_h }; // A in,  B out
    Rectangle c10 = { col1_x, top_y + 240,     cell_w, cell_h }; // A out, B in
    Rectangle c11 = { col2_x, top_y + 240,     cell_w, cell_h }; // A out, B out
    draw_cell(c00, "A in / B in",   scen_idx(1, 1));
    draw_cell(c01, "A in / B out",  scen_idx(1, 0));
    draw_cell(c10, "A out / B in",  scen_idx(0, 1));
    draw_cell(c11, "A out / B out", scen_idx(0, 0));

    // Summary: holding the OPPONENT's star in (the realistic question), what does A's star add?
    int s_AinBin   = scen_idx(1, 1);
    int s_AoutBin  = scen_idx(0, 1);
    int s_BinAin   = scen_idx(1, 1);
    int s_BoutAin  = scen_idx(1, 0);
    int a_with    = (agg_a_wins[s_AinBin]  * 100) / SIMS_PER_SCENARIO;
    int a_without = (agg_a_wins[s_AoutBin] * 100) / SIMS_PER_SCENARIO;
    int b_with    = (agg_b_wins[s_BinAin]  * 100) / SIMS_PER_SCENARIO;
    int b_without = (agg_b_wins[s_BoutAin] * 100) / SIMS_PER_SCENARIO;

    int sy = top_y + 480;
    DrawText("Start/Sit value (other team's star plays):", 30, sy, 20, RAYWHITE); sy += 30;
    DrawText(TextFormat("%s wins %d%% with %s, %d%% without  (delta %+d)",
                        a_name, a_with, a_star, a_without, a_with - a_without),
             30, sy, 18, (Color){80,180,255,255}); sy += 24;
    DrawText(TextFormat("%s wins %d%% with %s, %d%% without  (delta %+d)",
                        b_name, b_with, b_star, b_without, b_with - b_without),
             30, sy, 18, (Color){255,120,90,255});

    Rectangle r_again = { 30, WIN_H - 60, 220, 40 };
    Rectangle r_back  = { 270, WIN_H - 60, 220, 40 };
    if (button(r_again, "Simulate again", 1)) {
        ui_state = ST_RUNNING;
        run_simulations(g_teams[team_a_idx], g_teams[team_b_idx],
                        score_a, score_b,
                        (float)(time_min * 60 + time_sec));
    }
    if (button(r_back, "Pick new teams", 1)) {
        ui_state = ST_SELECTING;
    }
}

// ---------- main ----------

int main(void) {
    setup();

    InitWindow(WIN_W, WIN_H, "NBA Star Sit Simulator");
    SetTargetFPS(60);
    load_logos();

    while (!WindowShouldClose()) {
        if (ui_state == ST_SELECTING) {
            // grid + inputs handled inside draw functions
        } else if (ui_state == ST_RUNNING) {
            if (g_completed_sims >= TOTAL_SIMS) {
                aggregate_results();
                ui_state = ST_RESULTS;
            }
        }

        BeginDrawing();
        ClearBackground((Color){15, 15, 22, 255});

        if (ui_state == ST_SELECTING) {
            DrawText("Pick two teams (blue = A, orange = B), then enter game state.",
                     20, 20, 22, RAYWHITE);
            DrawText("Click a team again to deselect. Click a number field, type to edit, backspace to clear.",
                     20, 48, 14, (Color){180,180,200,255});
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
