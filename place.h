/* place.h -- fleet-placement RULES, shared by main.c and tests/test_place.c.
 *
 * WHY THIS IS A SEPARATE HEADER: these two functions are pure board logic -- no
 * GB hardware, no GBDK calls. Keeping them out of main.c means the host test can
 * include the REAL implementation instead of a copy, so the test can actually
 * fail, and the two copies can never drift.
 *
 * Verified GB-header-free: this header includes only <stdint.h>, and
 * tests/test_place.c compiles and runs with plain gcc.
 */
#ifndef PLACE_H
#define PLACE_H

#include <stdint.h>

#define CELL_EMPTY 255          /* also doubles as "no ship" / NOSHIP */
#define NSHIP      7

/* Ship lengths in placement order: carrier, battleship, cruiser, 2 destroyers,
   2 subs. Declared here (not in main.c) so the rules can use it. */
static const uint8_t ship_len[NSHIP] = {5, 4, 3, 2, 2, 1, 1};

/* Board is a plain 10x10 grid. own[] holds the ship id of each cell, or
   CELL_EMPTY. st[] holds firing state (S_EMPTY/S_MISS/S_HIT/S_SUNK). */
typedef struct {
    uint8_t own[100];
    uint8_t st[100];
    uint8_t hits[NSHIP];
    uint8_t alive;
} Board;

/* 1 if a ship of `len` fits at (x,y) horizontally (horiz!=0) or vertically,
   WITHOUT touching any existing ship -- classic Battleship needs a 1-cell moat,
   implemented by scanning the 3x3 neighbourhood of every hull cell.
   Returns 0 if the hull would run off the board. */
static uint8_t can_place(const Board *b, uint8_t x, uint8_t y, uint8_t len, uint8_t horiz) {
    uint8_t k, dx, dy;
    for (k = 0; k < len; k++) {
        uint8_t nx = horiz ? x + k : x;
        uint8_t ny = horiz ? y : y + k;
        if (nx > 9 || ny > 9) return 0;
        for (dy = 0; dy < 3; dy++)
            for (dx = 0; dx < 3; dx++) {
                int8_t sx = (int8_t)nx + (int8_t)dx - 1;
                int8_t sy = (int8_t)ny + (int8_t)dy - 1;
                if (sx < 0 || sy < 0 || sx > 9 || sy > 9) continue;
                if (b->own[sy * 10 + sx] != CELL_EMPTY) return 0;
            }
    }
    return 1;
}

/* Write one ship into the board. The caller MUST have verified can_place()
   first -- this function does no checking and will happily overwrite. */
static void place_ship(Board *b, uint8_t x, uint8_t y, uint8_t len,
                       uint8_t horiz, uint8_t id) {
    uint8_t k;
    for (k = 0; k < len; k++)
        b->own[(horiz ? y : y + k) * 10 + (horiz ? x + k : x)] = id;
}

/* 1 if ship `s` has any cell on the board (i.e. it has been placed).
   Used by the fleet panel during manual placement to show progress: only ships
   already committed should appear, otherwise the panel claims a full fleet. */
static uint8_t ship_is_placed(const Board *b, uint8_t s) {
    uint8_t i;
    for (i = 0; i < 100; i++)
        if (b->own[i] == s) return 1;
    return 0;
}

/* Empty `own` and reset `st`/`hits`/`alive` so the board is ready to place. */
static void clear_board(Board *b) {
    uint8_t i;
    for (i = 0; i < 100; i++) { b->own[i] = CELL_EMPTY; b->st[i] = 0; }
    for (i = 0; i < NSHIP; i++) b->hits[i] = 0;
    b->alive = NSHIP;
}

/* Randomly place ONE ship (index s). Returns 1 on success, 0 if it gave up.
   Used by init_board and by the manual-placement "skip the rest" action. */
static uint8_t place_random_one(Board *b, uint8_t s) {
    uint8_t tries = 0;
    for (;;) {
        uint8_t horiz = (uint8_t)(rand() & 1);
        uint8_t x = (uint8_t)(rand() % 10);
        uint8_t y = (uint8_t)(rand() % 10);
        if (can_place(b, x, y, ship_len[s], horiz)) {
            place_ship(b, x, y, ship_len[s], horiz, s);
            return 1;
        }
        if (++tries > 200) return 0;
    }
}

#endif /* PLACE_H */
