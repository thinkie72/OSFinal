//
// Created by Tyler Hinkie on 4/30/26.
//

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "utils.h"

PNBA_TEAM g_teams[NUM_TEAMS];
int       g_team_count = 0;

static float safe_div(float num, float den) {
    return (den > 0.0f) ? (num / den) : 0.0f;
}

static void fill_percentages(PSHOT_DATA d) {
    d->two_point_percentage   = safe_div(d->made_twos,   d->attempted_twos);
    d->three_point_percentage = safe_div(d->made_threes, d->attempted_threes);
    d->free_throw_percentage  = safe_div(d->made_fts,    d->attempted_fts);
}

void setup(void) {
    for (int i = 0; i < NUM_TEAMS; i++) {
        g_teams[i] = malloc(sizeof(NBA_TEAM));
        g_teams[i]->player              = malloc(sizeof(STAR_PLAYER));
        g_teams[i]->team_data           = malloc(sizeof(SHOT_DATA));
        g_teams[i]->player->player_data = malloc(sizeof(SHOT_DATA));
        g_teams[i]->player->player_name = malloc(64 * sizeof(char));
        g_teams[i]->team_name           = malloc(64 * sizeof(char));
    }

    FILE *data = fopen("data.csv", "r");
    if (!data) {
        fprintf(stderr, "setup: could not open data.csv\n");
        return;
    }

    char line[512];
    fgets(line, sizeof(line), data); // skip header

    int i = 0;
    while (fgets(line, sizeof(line), data) && i < NUM_TEAMS) {
        char         star_name[64];
        PNBA_TEAM    t  = g_teams[i];
        PSTAR_PLAYER p  = t->player;
        PSHOT_DATA   td = t->team_data;
        PSHOT_DATA   pd = p->player_data;

        // Columns: Team,EBPM,ABPM,GAMES,STAR,IMPACT,
        //          S2PM,S2PA,S3PM,S3PA,SFTM,SFTA,
        //          T2PM,T2PA,T3PM,T3PA,TFTM,TFTA,
        //          USAGE
        int parsed = sscanf(line,
            "%63[^,],%f,%f,%*f,%63[^,],%f,"
            "%f,%f,%f,%f,%f,%f,"
            "%f,%f,%f,%f,%f,%f,"
            "%f",
            t->team_name, &t->ebpm, &t->abpm, star_name, &p->impact,
            &pd->made_twos,   &pd->attempted_twos,
            &pd->made_threes, &pd->attempted_threes,
            &pd->made_fts,    &pd->attempted_fts,
            &td->made_twos,   &td->attempted_twos,
            &td->made_threes, &td->attempted_threes,
            &td->made_fts,    &td->attempted_fts,
            &p->usage);

        if (parsed < 18) {
            fprintf(stderr, "setup: row %d parse only %d fields\n", i, parsed);
            continue;
        }

        strncpy(p->player_name, star_name, 63);
        p->player_name[63] = '\0';

        fill_percentages(pd);
        fill_percentages(td);
        i++;
    }
    fclose(data);
    g_team_count = i;
}
