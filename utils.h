//
// Created by Tyler Hinkie on 4/29/26.
//

#ifndef OSFINAL_UTILS_H
#define OSFINAL_UTILS_H

// I'm still not sure what I need for the simulations
typedef struct {
    // Actual per game values
    float made_twos;
    float attempted_twos;
    float made_threes;
    float attempted_threes;
    float made_fts;
    float attempted_fts;
    // Percentages (or conversion rates)
    float two_point_percentage;
    float three_point_percentage;
    float free_throw_percentage;
    // Shared rates
    float two_point_rate;
    float three_point_rate;
    float free_throw_rate;
} SHOT_DATA, *PSHOT_DATA;

typedef struct {
    char *player_name;
    PSHOT_DATA player_data;
    float impact;
    float usage;
    // image headshot;

} STAR_PLAYER, *PSTAR_PLAYER;

typedef struct {
    char *team_name;
    PSTAR_PLAYER player;
    PSHOT_DATA team_data;
    // image logo;
    // button button;
} NBA_TEAM, *PNBA_TEAM;

#endif //OSFINAL_UTILS_H
