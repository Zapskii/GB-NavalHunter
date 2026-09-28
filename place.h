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

/* cell firing states (st[]) */
#define S_EMPTY 0
#define S_MISS  1
#define S_HIT   2
#define S_SUNK  3

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

/* ---------- computer opponent (pure logic, host-testable like the rules) ------
 * HUNT: fire at a random cell. A cell next to a KNOWN ship (hit or sunk) can
   hold no ship -- ships never touch -- so those dead shots are skipped while
   any other cell remains. The random start kills the old every-other-slot
   parity rhythm; the moat rule replaces the accuracy it bought.
 * TARGET: after a hit, try the four neighbours; once a second hit of the same
   ship is adjacent, the orientation is known and we extend the ship's line in
   both directions, the way a human finishes off a damaged ship.
 * target[] queues the cells to fire before hunting resumes; sinking the ship
   clears it, because every neighbour of a sunk ship is provably water.
 * ai_hunt() is only reached with an EMPTY queue, which means no ship is
   partially hit -- so the moat check (st >= S_HIT, sunk only) is exact: it
   can never mark a live ship's continuation as dead.
 */
static uint8_t target[100];

static void ai_clear_queue(void) {
    uint8_t i;
    for (i = 0; i < 100; i++) target[i] = 0;
}

static void ai_queue(const Board *b, uint8_t x, uint8_t y) {
    if (x > 9 || y > 9) return;
    if (b->st[y * 10 + x] != S_EMPTY) return;
    target[y * 10 + x] = 1;
}

/* Queue the continuation of ship `o` along one axis from a hit at (x,y).
   The walk steps over already-hit hull cells and stops at water, the edge,
   or the first unfired hull cell (which it queues). */
static void ai_extend(const Board *b, uint8_t o, uint8_t x, uint8_t y, uint8_t horiz) {
    int8_t sgn;
    for (sgn = -1; sgn <= 1; sgn += 2) {
        int8_t px = (int8_t)x, py = (int8_t)y;
        for (;;) {
            int8_t nx = horiz ? (int8_t)(px + sgn) : px;
            int8_t ny = horiz ? py : (int8_t)(py + sgn);
            if (nx < 0 || nx > 9 || ny < 0 || ny > 9) break;
            if (b->own[ny * 10 + nx] != o) break;    /* water: the hull ended */
            if (b->st[ny * 10 + nx] == S_EMPTY) {
                ai_queue(b, (uint8_t)nx, (uint8_t)ny);
                break;
            }
            px = nx; py = ny;                        /* another hit: walk past it */
        }
    }
}

/* Follow-up after a hit on ship `o` at (x,y). Ships never touch, so any
   adjacent hit is part of the same ship and reveals the orientation. */
static void ai_hit(const Board *b, uint8_t o, uint8_t x, uint8_t y) {
    uint8_t horiz = 0xFF;                            /* unknown */
    if ((x > 0 && b->st[y * 10 + x - 1] == S_HIT) ||
        (x < 9 && b->st[y * 10 + x + 1] == S_HIT)) horiz = 1;
    else if ((y > 0 && b->st[(y - 1) * 10 + x] == S_HIT) ||
             (y < 9 && b->st[(y + 1) * 10 + x] == S_HIT)) horiz = 0;
    if (horiz != 0xFF) ai_extend(b, o, x, y, horiz);
    else {
        ai_queue(b, (uint8_t)(x + 1), y); ai_queue(b, (uint8_t)(x - 1), y);
        ai_queue(b, x, (uint8_t)(y + 1)); ai_queue(b, x, (uint8_t)(y - 1));
    }
}

/* 1 if any of the 8 neighbours of (x,y) is a known ship cell. */
static uint8_t ai_moated(const Board *b, uint8_t x, uint8_t y) {
    int8_t dx, dy;
    for (dy = -1; dy <= 1; dy++)
        for (dx = -1; dx <= 1; dx++) {
            int8_t nx = (int8_t)(x + dx), ny = (int8_t)(y + dy);
            if (nx < 0 || nx > 9 || ny < 0 || ny > 9) continue;
            if (b->st[ny * 10 + nx] >= S_HIT) return 1;
        }
    return 0;
}

/* HUNT: random unfired cell, moat-aware. Two passes so the loop always
   terminates: preferred (not moated), then any unfired cell. */
static uint8_t ai_hunt(const Board *b) {
    uint8_t start = (uint8_t)(rand() % 100);
    uint8_t pass, i;
    for (pass = 0; pass < 2; pass++)
        for (i = 0; i < 100; i++) {
            uint8_t idx = (uint8_t)((start + i) % 100);
            uint8_t x = (uint8_t)(idx % 10), y = (uint8_t)(idx / 10);
            if (b->st[idx] != S_EMPTY) continue;
            if (pass == 0 && ai_moated(b, x, y)) continue;
            return idx;
        }
    return 0;   /* only reachable once every cell has been fired */
}

#endif /* PLACE_H */
