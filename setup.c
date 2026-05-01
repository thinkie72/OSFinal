//
// Created by Tyler Hinkie on 4/30/26.
//

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "utils.h"

void setup () {
    // make data.csv structure
    PNBA_TEAM teams[30];
    for (int i = 0; i < 30; i++) {
        teams[i] = malloc(sizeof(NBA_TEAM));
        teams[i]->player = malloc(sizeof(STAR_PLAYER));
        teams[i]->team_data = malloc(sizeof(SHOT_DATA));
        teams[i]->player->player_name = malloc(64 * sizeof(char));
        teams[i]->team_name = malloc(64 * sizeof(char));
    }

    // read CSV
    FILE *data = fopen("data.csv", "r");

    char line[512];
    fgets(line, sizeof(line), data);

    int i = 0;
    while (fgets(line, sizeof(line), data) && i < 30) {
        char star_name[64];
        PNBA_TEAM t = teams[i];
        PSTAR_PLAYER p = t->player;
        PSHOT_DATA td = t->team_data;
        PSHOT_DATA pd = malloc(sizeof(SHOT_DATA));
        p->player_data = pd;

        sscanf(line,
            "%63[^,],%*f,%*f,%*f,%63[^,],%f,%f,%f,%f,%f,%f,%f,%f,%f,%f,%f,%f,%f,%f,%f,%f,%f,%f,%f",
            t->team_name, star_name, &p->impact,
            // Player Totals
            &pd->made_twos, &pd->attempted_twos, &pd->made_threes,
            &pd->attempted_threes, &pd->made_fts, &pd->attempted_fts,
            // Team totals
            &td->made_twos, &td->attempted_twos,
            &td->made_threes, &td->attempted_threes,
            &td->made_fts, &td->attempted_fts,
            // Other
            &p->usage
        );
        strncpy(p->player_name, star_name, 63);
        i++;
    }
    fclose(data);

    // spawn team threads and GUI
    // wait for user input for team buttons and game state variables
}