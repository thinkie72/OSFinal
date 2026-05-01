//
// Created by Tyler Hinkie on 4/30/26.
//

#include <stdio.h>
#include <stdbool.h>
#include <string.h>
#include "utils.h"

void setup () {
    // make data.csv structure
    PNBA_TEAM teams[32];

    // read CSV
    FILE *data = fopen("data.csv", "r");

    while (feof(data) == 0) {
        fscanf(data, "%s", &teams[0]);
    }
    // spawn team threads and GUI
    // wait for user input for team buttons and game state variables
}