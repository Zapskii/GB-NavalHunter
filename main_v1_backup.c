/* BATTLESHIP for the Game Boy -- human vs computer.
 *
 * Classic 10x10 rules (papg.com reference):
 *   fleet 5,4,3,2,2,1,1 (18 squares); ships never touch; turn-based;
 *   hit / miss; a ship is announced and revealed when its last square is hit;
 *   you win when every enemy ship is sunk.
 *
 * SCREEN LAYOUT -- read this before changing any coordinates.
 *   The 160x144 screen is exactly 20x18 background tiles, so NOTHING scrolls.
 *   Cells are 8x8 (one tile each), boards side by side:
 *       enemy grid  = tile cols 0..9    (cells hidden until hit)
 *       your  grid  = tile cols 10..19  (your ships always visible)
 *     tile row 0        : status line 1
 *     tile rows 1..10   : the ten board rows (cell y maps to row 1+y)
 *     tile row 11       : "ENEMY" / "YOU" labels
 *     tile row 12       : status line 2 (messages, coordinate, turn no.)
 *     tile rows 14..16  : legend
 *
 *   SEPARATION: the screen is exactly 20 tiles wide and the two 10-wide boards
 *   consume all 20, so there is no spare column for a gap. Instead the first
 *   column of each board uses TILE_CELLG, whose leftmost 3px are carved blank.
 *   See add_grid_lines(gutter=N) in mkgfx.py.
 *
 *   The cursor is an 8x8 SPRITE so it overlays a whole cell (a background
 *   tile could not do that without a second tileset).
 *
 * IMPORTANT: we do NOT call console_init(). It would stomp tiles 0..? with
 * GBDK's own font. We load our glyphs at FONT_BASE.. and draw our own text
 * straight into the tilemap.
 */
#include <gb/gb.h>
#include <stdint.h>
#include <rand.h>

#include "gfx.h"

#define ROW0      1                 /* first board tile row */
#define CBOARD    10                /* tile-col offset of your grid */
#define STATUS1   0
#define STATUS2   12

/* cell states */
#define S_EMPTY 0
#define S_MISS  1
#define S_HIT   2
#define S_SUNK  3

#define NSHIP      7
#define CELL_EMPTY 255  /* also doubles as "no ship" / NOSHIP */
static const uint8_t ship_len[NSHIP] = {5, 4, 3, 2, 2, 1, 1};
static const char *const ship_name[NSHIP] = {
    "CARRIER", "BATTLESHIP", "CRUISER", "DESTROYER", "DESTROYER", "SUB", "SUB"
};

typedef struct {
    uint8_t own[100];
    uint8_t st[100];
    uint8_t hits[NSHIP];
    uint8_t alive;
} Board;

static Board pb;   /* your waters      -- the computer shoots here */
static Board ob;   /* enemy waters     -- you shoot here          */

static uint8_t vram[360];

/* ---------- map painting ---------- */
static void paint(uint8_t r, uint8_t c, uint8_t t) {
    if (r < 18 && c < 20) vram[r * 20 + c] = t;
}
static void clear_row(uint8_t r) { uint8_t c; for (c = 0; c < 20; c++) paint(r, c, TILE_BLANK); }
static void put_text(uint8_t r, uint8_t c, const char *s) {
    while (*s) paint(r, c++, font_map[(uint8_t)*s++]);
}
static void status1(const char *s) { clear_row(STATUS1); put_text(STATUS1, 0, s); }
static void status2(const char *s) { clear_row(STATUS2); put_text(STATUS2, 0, s); }
static void flush(void) { set_bkg_tiles(0, 0, 20, 18, vram); }

static uint8_t cell_tile(const Board *b, uint8_t i, uint8_t reveal) {
    if (b->st[i] == S_MISS) return TILE_MISS;
    if (b->own[i] == CELL_EMPTY) return TILE_CELL;
    if (b->st[i] == S_HIT || b->st[i] == S_SUNK) return TILE_HIT;
    return reveal ? TILE_SHIP : TILE_CELL;
}

static void draw_board(uint8_t board, const Board *b, uint8_t reveal) {
    uint8_t x, y;
    for (y = 0; y < 10; y++)
        for (x = 0; x < 10; x++) {
            uint8_t t = cell_tile(b, y * 10 + x, reveal);
            /* Column 0 of each board gets a guttered tile so the two boards
               read as SEPARATE grids. Only empty cells need it: ship/hit art
               is solid and already separates itself. */
            if (x == 0 && t == TILE_CELL) t = TILE_CELLG;
            paint(ROW0 + y, board * CBOARD + x, t);
        }
}

static void draw_legend(void) {
    /* left grid = ENEMY waters (what you're shooting at), right = YOU.
       Must match main(): draw_board(0,&ob) is LEFT, draw_board(1,&pb) is RIGHT. */
    /* Labels go on row 11, UNDER the boards. Row 10 is the boards' bottom row --
       writing labels there chops the art of any ship in that row. */
    clear_row(11);
    put_text(11, 0, "ENEMY");
    put_text(11, 10, "YOU");
    /* key: legend swatches deliberately have no grid hairline */
    put_text(14, 0,  "SHIP");
    paint(14, 6, TILE_LEG_SHIP);
    put_text(14, 10, "PAD=MOVE");
    put_text(15, 0,  "HIT");
    paint(15, 6, TILE_LEG_HIT);
    put_text(15, 10, "A=FIRE");
    put_text(16, 0,  "MISS");
    paint(16, 6, TILE_LEG_MISS);
    put_text(16, 10, "A1-J10");
}

/* ---------- fleet placement ---------- */
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

static void init_board(Board *b) {
    uint8_t i, s;
    for (i = 0; i < 100; i++) { b->own[i] = CELL_EMPTY; b->st[i] = S_EMPTY; }
    for (i = 0; i < NSHIP; i++) b->hits[i] = 0;
    b->alive = NSHIP;

    for (s = 0; s < NSHIP; s++) {
        uint8_t tries = 0;
        for (;;) {
            uint8_t horiz = rand() & 1;
            uint8_t x = rand() % 10;
            uint8_t y = rand() % 10;
            if (can_place(b, x, y, ship_len[s], horiz)) {
                uint8_t k;
                for (k = 0; k < ship_len[s]; k++)
                    b->own[(horiz ? y : y + k) * 10 + (horiz ? x + k : x)] = s;
                break;
            }
            if (++tries > 200) {           /* pathological; start the fleet over */
                for (i = 0; i < 100; i++) b->own[i] = CELL_EMPTY;
                s = 0;
                break;
            }
        }
    }
}

/* ---------- firing ---------- */
/* Returns:
 *   0 = miss
 *   1 = hit
 *   2 = hit + ship sunk
 *   3 = ILLEGAL: that square was already fired at (caller must not consume a turn)
 */
static uint8_t fire(Board *b, uint8_t x, uint8_t y, uint8_t *sunk) {
    uint8_t i = y * 10 + x, o;
    if (b->st[i] != S_EMPTY) return 3;      /* already fired here */
    o = b->own[i];
    if (o == CELL_EMPTY) { b->st[i] = S_MISS; return 0; }

    b->st[i] = S_HIT;
    b->hits[o]++;
    if (b->hits[o] >= ship_len[o]) {
        uint8_t k;
        for (k = 0; k < 100; k++) if (b->own[k] == o) b->st[k] = S_SUNK;
        b->alive--;
        *sunk = o;
        return 2;
    }
    return 1;
}

/* ---------- computer AI: queue neighbours on hit, else parity hunt ---------- */
static uint8_t target[100];

static void ai_queue(uint8_t x, uint8_t y) {
    if (x > 9 || y > 9) return;
    if (pb.st[y * 10 + x] != S_EMPTY) return;
    target[y * 10 + x] = 1;
}

static void ai_pick(uint8_t *ox, uint8_t *oy) {
    uint8_t i;
    for (i = 0; i < 100; i++)                 /* 1. follow up a hit */
        if (target[i]) { target[i] = 0; *ox = i % 10; *oy = i / 10; return; }

    for (i = 0; i < 100; i++) {               /* 2. parity hunt */
        uint8_t x = i % 10, y = i / 10;
        if (((x + y) & 1) == 0 && pb.st[i] == S_EMPTY) { *ox = x; *oy = y; return; }
    }
    for (i = 0; i < 100; i++)                 /* 3. anything left */
        if (pb.st[i] == S_EMPTY) { *ox = i % 10; *oy = i / 10; return; }

    *ox = 0; *oy = 0;
}

/* ---------- misc ---------- */
static void move_cursor(uint8_t board, uint8_t x, uint8_t y) {
    /* sprite coords are (pixel + 8, pixel + 16) */
    move_sprite(0, (uint8_t)((board * CBOARD + x) * 8 + 8),
                   (uint8_t)((ROW0 + y) * 8 + 16));
}

/* Row 13 holds BOTH the "A12" cursor readout (cols 0..2) and the turn
   counter (cols 14..17). Anything written here must respect those fields;
   row 13 is otherwise blank, so a status2() call does NOT clobber it. */
static uint8_t turn_no = 0;

static void show_coord(uint8_t x, uint8_t y) {
    char b[4];
    b[0] = 'A' + x;
    b[1] = '0' + (y + 1) / 10;
    b[2] = '0' + (y + 1) % 10;
    b[3] = 0;
    paint(STATUS2 + 1, 0, TILE_BLANK);
    paint(STATUS2 + 1, 1, TILE_BLANK);
    paint(STATUS2 + 1, 2, TILE_BLANK);
    put_text(STATUS2 + 1, 0, b);
}

static void show_turn(void) {
    char b[4];
    uint8_t n = turn_no % 100, i = 0;
    /* '#' is not in the glyph set (no room in VRAM), so use 'N' as the label */
    b[i++] = 'N';
    if (n >= 10) b[i++] = (char)('0' + n / 10);
    b[i++] = (char)('0' + n % 10);
    b[i] = 0;
    paint(STATUS2 + 1, 14, TILE_BLANK);
    paint(STATUS2 + 1, 15, TILE_BLANK);
    paint(STATUS2 + 1, 16, TILE_BLANK);
    paint(STATUS2 + 1, 17, TILE_BLANK);
    put_text(STATUS2 + 1, 15, b);
}

static void pause_frames(uint16_t n) { while (n--) wait_vbl_done(); }

/* ---------- direction input with key-repeat ----------
 * Without this, holding a direction moves one square per frame (~60/sec), so a
 * normal tap overshoots by several squares and you cannot land on a neighbour.
 * Behaviour: move immediately on press, then wait REPEAT_DELAY frames before
 * auto-repeating every REPEAT_RATE frames (like a keyboard cursor). */
#define REPEAT_DELAY 18   /* ~0.30 s before auto-repeat kicks in */
#define REPEAT_RATE   9   /* then ~6.7 moves/sec while held */

static uint8_t held_btn;      /* which direction is currently repeating (0 = none) */
static uint8_t held_timer;    /* frames remaining before the next repeat */

void main(void) {
    uint8_t cx = 0, cy = 0;

    initrand((uint16_t)sys_time ^ 0xA55Au);

    set_bkg_data(0, GFX_TILE_COUNT, gfx_tiles);
    set_sprite_data(0, 1, sprite_tiles);
    SPRITES_8x8;

    { uint16_t i; for (i = 0; i < 360; i++) vram[i] = TILE_BLANK; }

    init_board(&pb);
    init_board(&ob);
    { uint8_t i; for (i = 0; i < 100; i++) target[i] = 0; }

    draw_board(0, &ob, 0);
    draw_board(1, &pb, 1);
    draw_legend();
    status1("YOUR TURN");
    status2("");
    show_coord(cx, cy);
    show_turn();
    flush();

    set_sprite_tile(0, 0);
    move_cursor(0, cx, cy);
    SHOW_SPRITES;
    SHOW_BKG;
    DISPLAY_ON;

    for (;;) {
        uint8_t j, r, sunk = 0, px = cx, py = cy;
        uint8_t ex, ey;                 /* the enemy's shot lands here */

        wait_vbl_done();
        j = joypad();

        /* ---- direction with key-repeat (see REPEAT_* above) ---- */
        {
            uint8_t d = j & (J_LEFT | J_RIGHT | J_UP | J_DOWN);
            uint8_t step = 0;
            if (d == 0) {
                held_btn = 0;                       /* released: reset the repeat */
            } else if (d != held_btn) {
                held_btn = d; held_timer = REPEAT_DELAY; step = 1;   /* fresh press */
            } else if (held_timer) {
                held_timer--;                       /* still inside the delay */
            } else {
                held_timer = REPEAT_RATE; step = 1; /* auto-repeat */
            }
            if (step) {
                if (d & J_LEFT)  cx = (cx == 0) ? 9 : cx - 1;
                if (d & J_RIGHT) cx = (cx == 9) ? 0 : cx + 1;
                if (d & J_UP)    cy = (cy == 0) ? 9 : cy - 1;
                if (d & J_DOWN)  cy = (cy == 9) ? 0 : cy + 1;
                if (cx != px || cy != py) {
                    move_cursor(0, cx, cy);
                    show_coord(cx, cy);
                    flush();
                }
            }
        }

        if (!(j & J_A)) continue;
        while (joypad() & J_A) wait_vbl_done();

        /* ---- your shot ---- */
        /* Re-shooting a spent square is refused and does NOT cost a turn. */
        r = fire(&ob, cx, cy, &sunk);
        if (r == 3) {
            status2("ALREADY SHOT");
            flush();
            pause_frames(30);
            status2("");
            show_coord(cx, cy);
            flush();
            continue;
        }
        turn_no++;
        show_turn();
        if (r == 0)      { status2("MISS"); }
        else if (r == 1) { status2("HIT!"); }
        else             { status2(""); put_text(STATUS2, 0, ship_name[sunk]); put_text(STATUS2, 10, "SUNK"); }
        draw_board(0, &ob, 0);
        flush();
        if (ob.alive == 0) { status1("YOU WIN!"); status2(""); flush(); for (;;) wait_vbl_done(); }
        pause_frames(45);

        /* ---- enemy shot ---- */
        status1("ENEMY TURN");
        flush();
        pause_frames(30);
        ai_pick(&ex, &ey);
        move_cursor(1, ex, ey);
        r = fire(&pb, ex, ey, &sunk);
        if (r == 3) {
            /* ai_pick should never choose a spent square, but if it somehow
               does, fall through to the hunt so it cannot stall the game. */
            uint8_t i;
            for (i = 0; i < 100; i++) {
                if (pb.st[i] == S_EMPTY) { ex = i % 10; ey = i / 10; break; }
            }
            move_cursor(1, ex, ey);
            r = fire(&pb, ex, ey, &sunk);
        }
        if (r >= 1) {               /* queue the four orthogonal neighbours */
            ai_queue(ex + 1, ey); ai_queue(ex - 1, ey);
            ai_queue(ex, ey + 1); ai_queue(ex, ey - 1);
        }
        if (r == 2) {               /* ship gone: its queued cells are stale */
            uint8_t i; for (i = 0; i < 100; i++) target[i] = 0;
        }
        if (r == 0)      { status2("MISS"); }
        else if (r == 1) { status2("HIT!"); }
        else             { status2(""); put_text(STATUS2, 0, ship_name[sunk]); put_text(STATUS2, 10, "SUNK"); }
        draw_board(1, &pb, 1);
        flush();
        if (pb.alive == 0) { status1("YOU LOSE"); status2(""); flush(); for (;;) wait_vbl_done(); }
        pause_frames(45);

        /* ---- back to you: cursor stays where you left it ---- */
        status1("YOUR TURN");
        status2("");
        move_cursor(0, cx, cy);
        show_coord(cx, cy);
        flush();
    }
}
