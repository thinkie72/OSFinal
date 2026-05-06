//
// Created by Tyler Hinkie on 4/30/26.
//
// Worker-pool simulation engine.
// Job queue is a fixed-size array; workers pop indices under a CRITICAL_SECTION,
// run a possession-by-possession sim, and write into g_sim_results[idx].
// g_completed_sims is incremented atomically (InterlockedIncrement) so the UI
// thread can render progress without taking the queue lock.
//

#include "utils.h"
#include "thread_compat.h"

#include <math.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

// ---- Per-job descriptor ----
typedef struct {
    PNBA_TEAM team_a;
    PNBA_TEAM team_b;
    int       start_a;
    int       start_b;
    float     time_sec;
    int       a_star_in;   // 1 = star plays, 0 = star sits
    int       b_star_in;
    int       scenario;    // 0..3 (low bit A, high bit B)
} SimJob;

// ---- Module-level shared state ----
SimResult     g_sim_results[TOTAL_SIMS];
volatile long g_completed_sims = 0;

static SimJob           g_jobs[TOTAL_SIMS];
static int              g_queue_head = 0;
static CRITICAL_SECTION g_queue_lock;
static int              g_lock_initialized = 0;

// ---- Per-thread RNG (xorshift32) — rand() isn't thread-safe so each worker keeps its own state. ----
static inline unsigned int rng_next(unsigned int *s) {
    unsigned int x = *s;
    x ^= x << 13;
    x ^= x >> 17;
    x ^= x << 5;
    *s = x ? x : 0xDEADBEEFu;
    return *s;
}
static inline float rng_unit(unsigned int *s) {
    return (rng_next(s) & 0xFFFFFFu) / (float)0x1000000;
}

// Compute one offensive possession's points.
// off_star_in / def_star_in toggle whether the respective star plays.
static int sim_possession(PNBA_TEAM off, PNBA_TEAM def,
                          int off_star_in, int def_star_in,
                          unsigned int *seed) {
    // EBPM differential — slight nudge to make % when offense is the better team.
    float off_eff = off->ebpm - (off_star_in ? 0.0f : off->player->impact);
    float def_eff = def->ebpm - (def_star_in ? 0.0f : def->player->impact);
    float ebpm_bonus = (off_eff - def_eff) * 0.005f; // ~0.5% FG per point of margin

    // Pick shooter: star (with prob = usage/100) or non-star aggregate.
    int shooter_is_star = 0;
    if (off_star_in) {
        shooter_is_star = (rng_unit(seed) * 100.0f) < off->player->usage;
    }

    float s2pa, s2pm, s3pa, s3pm, sfta, sftm;
    if (shooter_is_star) {
        PSHOT_DATA pd = off->player->player_data;
        s2pa = pd->attempted_twos;   s2pm = pd->made_twos;
        s3pa = pd->attempted_threes; s3pm = pd->made_threes;
        sfta = pd->attempted_fts;    sftm = pd->made_fts;
    } else {
        // non-star = team total minus star
        PSHOT_DATA td = off->team_data;
        PSHOT_DATA pd = off->player->player_data;
        // If star sits, only subtract their share when they were originally counted
        // in the team totals (which they always are in the CSV); when star sits we
        // also boost rest-of-team a bit by adding star's usage back to non-star
        // attempts. Simpler model: just use team-minus-star for non-star.
        s2pa = td->attempted_twos   - pd->attempted_twos;
        s2pm = td->made_twos        - pd->made_twos;
        s3pa = td->attempted_threes - pd->attempted_threes;
        s3pm = td->made_threes      - pd->made_threes;
        sfta = td->attempted_fts    - pd->attempted_fts;
        sftm = td->made_fts         - pd->made_fts;
        if (s2pa < 0) s2pa = 0; if (s2pm < 0) s2pm = 0;
        if (s3pa < 0) s3pa = 0; if (s3pm < 0) s3pm = 0;
        if (sfta < 0) sfta = 0; if (sftm < 0) sftm = 0;
    }

    // Shot type weights: 2PA, 3PA, FTA*0.44 (the standard "trips to the line" factor).
    float w2  = s2pa;
    float w3  = s3pa;
    float wft = sfta * 0.44f;
    float total = w2 + w3 + wft;
    if (total <= 0.0f) return 0;

    float roll = rng_unit(seed) * total;

    if (roll < w2) {
        float pct = (s2pa > 0) ? (s2pm / s2pa) : 0.0f;
        pct += ebpm_bonus;
        return (rng_unit(seed) < pct) ? 2 : 0;
    } else if (roll < w2 + w3) {
        float pct = (s3pa > 0) ? (s3pm / s3pa) : 0.0f;
        pct += ebpm_bonus;
        return (rng_unit(seed) < pct) ? 3 : 0;
    } else {
        // Trip to the line: 2 free throws.
        float pct = (sfta > 0) ? (sftm / sfta) : 0.7f;
        int pts = 0;
        if (rng_unit(seed) < pct) pts++;
        if (rng_unit(seed) < pct) pts++;
        return pts;
    }
}

static SimResult simulate_one(const SimJob *job, unsigned int *seed) {
    // 100 possessions per full 48-minute game, scaled by remaining time, split evenly.
    float minutes = job->time_sec / 60.0f;
    int total_poss = (int)(100.0f * (minutes / 48.0f) + 0.5f);
    if (total_poss < 0) total_poss = 0;
    int poss_a = total_poss / 2;
    int poss_b = total_poss - poss_a;

    int score_a = job->start_a;
    int score_b = job->start_b;

    for (int p = 0; p < poss_a; p++) {
        score_a += sim_possession(job->team_a, job->team_b,
                                  job->a_star_in, job->b_star_in, seed);
    }
    for (int p = 0; p < poss_b; p++) {
        score_b += sim_possession(job->team_b, job->team_a,
                                  job->b_star_in, job->a_star_in, seed);
    }

    SimResult r;
    r.final_a  = score_a;
    r.final_b  = score_b;
    r.scenario = job->scenario;
    return r;
}

// ---- Worker thread entrypoint (Win32 signature) ----
static DWORD WINAPI worker_main(LPVOID arg) {
    unsigned int seed = (unsigned int)((uintptr_t)arg) * 2654435769u
                      ^ (unsigned int)time(NULL);
    if (seed == 0) seed = 0x12345678u;

    for (;;) {
        EnterCriticalSection(&g_queue_lock);
        if (g_queue_head >= TOTAL_SIMS) {
            LeaveCriticalSection(&g_queue_lock);
            break;
        }
        int idx = g_queue_head++;
        LeaveCriticalSection(&g_queue_lock);

        g_sim_results[idx] = simulate_one(&g_jobs[idx], &seed);
        InterlockedIncrement(&g_completed_sims);
    }
    return 0;
}

static int choose_worker_count(void) {
#ifdef _WIN32
    SYSTEM_INFO si;
    GetSystemInfo(&si);
    int n = (int)si.dwNumberOfProcessors;
#else
    int n = (int)sysconf(_SC_NPROCESSORS_ONLN);
#endif
    if (n < 2) n = 2;
    if (n > 16) n = 16;
    return n;
}

void run_simulations(PNBA_TEAM A, PNBA_TEAM B,
                     int start_a, int start_b,
                     float time_remaining_sec) {
    if (!g_lock_initialized) {
        InitializeCriticalSection(&g_queue_lock);
        g_lock_initialized = 1;
    }

    g_queue_head     = 0;
    g_completed_sims = 0;
    memset(g_sim_results, 0, sizeof(g_sim_results));

    // Populate the queue: 4 scenarios × SIMS_PER_SCENARIO each.
    int idx = 0;
    for (int s = 0; s < NUM_SCENARIOS; s++) {
        int a_in = (s & 1) ? 1 : 0;
        int b_in = (s & 2) ? 1 : 0;
        for (int i = 0; i < SIMS_PER_SCENARIO; i++) {
            SimJob *job = &g_jobs[idx];
            job->team_a    = A;
            job->team_b    = B;
            job->start_a   = start_a;
            job->start_b   = start_b;
            job->time_sec  = time_remaining_sec;
            job->a_star_in = a_in;
            job->b_star_in = b_in;
            job->scenario  = s;
            idx++;
        }
    }

    int N = choose_worker_count();
    for (int i = 0; i < N; i++) {
        HANDLE h = CreateThread(NULL, 0, worker_main,
                                (LPVOID)(uintptr_t)(i + 1), 0, NULL);
        // Detach: workers exit on their own once the queue drains.
        if (h) CloseHandle(h);
    }
}
