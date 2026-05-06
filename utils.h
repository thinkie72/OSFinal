//
// Created by Tyler Hinkie on 4/29/26.
//

#ifndef OSFINAL_UTILS_H
#define OSFINAL_UTILS_H

#define NUM_TEAMS         30
#define SIMS_PER_SCENARIO 100
#define NUM_SCENARIOS     4
#define TOTAL_SIMS        (SIMS_PER_SCENARIO * NUM_SCENARIOS)

typedef struct {
    // Per-game made / attempted
    float made_twos;
    float attempted_twos;
    float made_threes;
    float attempted_threes;
    float made_fts;
    float attempted_fts;
    // Percentages (filled in by setup, not from CSV)
    float two_point_percentage;
    float three_point_percentage;
    float free_throw_percentage;
    // Shared rates (unused right now; kept for future)
    float two_point_rate;
    float three_point_rate;
    float free_throw_rate;
} SHOT_DATA, *PSHOT_DATA;

typedef struct {
    char       *player_name;
    PSHOT_DATA  player_data;
    float       impact;   // IMPACT column — star's contribution to team plus-minus
    float       usage;    // USAGE % — fraction of possessions ending with the star
} STAR_PLAYER, *PSTAR_PLAYER;

typedef struct {
    char        *team_name;
    PSTAR_PLAYER player;
    PSHOT_DATA   team_data;
    float        ebpm;    // estimated BPM (team)
    float        abpm;    // actual BPM (team)
} NBA_TEAM, *PNBA_TEAM;

// ---- Globals owned by setup.c ----
extern PNBA_TEAM g_teams[NUM_TEAMS];
extern int       g_team_count;

void setup(void);

// ---- Simulation API (simulation_thread.c) ----
typedef struct {
    int final_a;
    int final_b;
    int scenario;   // 0..3, low bit = A star plays, high bit = B star plays
} SimResult;

extern SimResult     g_sim_results[TOTAL_SIMS];
extern volatile long g_completed_sims;

// Kicks off TOTAL_SIMS sims across a worker pool. Returns immediately;
// poll g_completed_sims for progress.
void run_simulations(PNBA_TEAM A, PNBA_TEAM B,
                     int start_a, int start_b,
                     float time_remaining_sec);

#endif // OSFINAL_UTILS_H
