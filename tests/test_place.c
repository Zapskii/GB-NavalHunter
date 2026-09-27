/* tests/test_place.c -- host test for the fleet-placement rules.
 *
 * Compiles and runs with plain gcc, NO Game Boy toolchain and no emulator:
 *
 *   gcc -O1 -I.. -o /tmp/test_place tests/test_place.c && /tmp/test_place
 *
 * This includes the REAL place.h that main.c also includes, so these tests can
 * actually fail and the two copies can never drift. Verify the RULES here;
 * verify only the RENDERING in the emulator.
 */
#include <stdio.h>
#include <stdlib.h>
#include "place.h"

static int fails = 0;
#define CHECK(cond, msg) do { if (!(cond)) { printf("FAIL: %s\n", msg); fails++; } } while (0)

int main(void) {
    Board b;

    /* ---- place_ship geometry ---- */
    clear_board(&b);
    place_ship(&b, 2, 3, 5, 1, 0);            /* horizontal, len 5, at (2,3) */
    CHECK(b.own[3*10+2] == 0, "horiz head");
    CHECK(b.own[3*10+6] == 0, "horiz tail");
    CHECK(b.own[3*10+7] == CELL_EMPTY, "horiz past tail empty");
    CHECK(b.own[2*10+2] == CELL_EMPTY, "row above untouched");
    CHECK(b.own[4*10+2] == CELL_EMPTY, "row below untouched");

    clear_board(&b);
    place_ship(&b, 4, 1, 4, 0, 3);            /* vertical, len 4, id 3 */
    CHECK(b.own[1*10+4] == 3, "vert head");
    CHECK(b.own[4*10+4] == 3, "vert tail");
    CHECK(b.own[5*10+4] == CELL_EMPTY, "vert past tail empty");
    CHECK(b.own[1*10+5] == CELL_EMPTY, "col right untouched");

    /* ---- no-touch rule ---- */
    clear_board(&b);
    place_ship(&b, 0, 0, 5, 1, 0);            /* horizontal along row 0 */
    CHECK(can_place(&b, 0, 1, 3, 1) == 0, "row 1 touches row 0 ship");
    CHECK(can_place(&b, 0, 2, 3, 1) == 1, "row 2 is one clear row away");

    clear_board(&b);
    place_ship(&b, 0, 0, 5, 0, 0);            /* vertical along col 0 */
    CHECK(can_place(&b, 1, 0, 5, 0) == 0, "col 1 touches col 0 ship");
    CHECK(can_place(&b, 2, 0, 5, 0) == 1, "col 2 is clear");

    /* ---- board edges ---- */
    clear_board(&b);
    CHECK(can_place(&b, 0, 0, 5, 1) == 1, "len 5 fits from col 0");
    CHECK(can_place(&b, 5, 0, 5, 1) == 1, "len 5 fits ending exactly at col 9");
    CHECK(can_place(&b, 6, 0, 5, 1) == 0, "len 5 from col 6 runs off");
    CHECK(can_place(&b, 0, 6, 5, 0) == 0, "len 5 from row 6 runs off");
    CHECK(can_place(&b, 0, 9, 1, 1) == 1, "len 1 fits in the last row");

    /* ---- a lone corner ship blocks its whole 3x3 ---- */
    clear_board(&b);
    place_ship(&b, 0, 0, 1, 1, 5);
    CHECK(can_place(&b, 1, 0, 1, 1) == 0, "right of corner ship blocked");
    CHECK(can_place(&b, 0, 1, 1, 1) == 0, "below corner ship blocked");
    CHECK(can_place(&b, 1, 1, 1, 1) == 0, "diagonal of corner ship blocked");
    CHECK(can_place(&b, 2, 2, 1, 1) == 1, "cell (2,2) is 2 away, legal");

    /* ---- clear_board resets everything ---- */
    clear_board(&b);
    b.st[0] = 2; b.hits[3] = 4; b.alive = 1;
    clear_board(&b);
    CHECK(b.st[0] == 0 && b.hits[3] == 0 && b.alive == NSHIP,
          "clear_board resets st/hits/alive");

    /* ---- random placement: a full fleet always fits, uses all 7 ids, never touches ---- */
    {
        int trial, ok = 1;
        for (trial = 0; trial < 200 && ok; trial++) {
            uint8_t s, seen[NSHIP];
            int own = 0;
            clear_board(&b);
            for (s = 0; s < NSHIP; s++)
                if (!place_random_one(&b, s)) { ok = 0; printf("FAIL: place_random_one gave up (trial %d ship %d)\n", trial, s); fails++; break; }
            if (!ok) break;
            /* every id present, with the right cell count */
            for (s = 0; s < NSHIP; s++) seen[s] = 0;
            { int i; for (i = 0; i < 100; i++) if (b.own[i] != CELL_EMPTY) { seen[b.own[i]]++; own++; } }
            for (s = 0; s < NSHIP; s++)
                if (seen[s] != ship_len[s]) { ok = 0; printf("FAIL: ship %d has %d cells, expected %d (trial %d)\n", s, seen[s], ship_len[s], trial); fails++; break; }
            if (own != 18) { ok = 0; printf("FAIL: %d occupied cells, expected 18\n", own); fails++; }
        }
    }

    printf("test_place: %s (%d failures)\n", fails ? "FAIL" : "PASS", fails);
    return fails ? 1 : 0;
}
