/* NAVAL HUNTER for the Game Boy -- human vs computer.
 *
 * Classic 10x10 rules (papg.com reference):
 *   fleet 5,4,3,2,2,1,1 (18 squares); ships never touch; turn-based;
 *   hit / miss; a ship is announced and revealed when its last square is hit;
 *   you win when every enemy ship is sunk.
 *
 * SCREEN LAYOUT -- read this before changing any coordinates.
 *   The 160x144 screen is exactly 20x18 background tiles, so NOTHING scrolls.
 *
 *   ONE board, centred. A single 10-wide board leaves 5 tiles of margin each
 *   side, so instead of the old crammed side-by-side pair we get real margins
 *   AND a fleet-status panel in the freed space.
 *
 *     tile cols 5..14   : the board (10 cells, 8x8 px each)
 *     tile rows 1..10   : the ten board rows (cell y maps to row 1+y)
 *     tile row  0       : status line 1 (whose turn / win / lose)
 *     tile row  11      : message line (MISS / HIT / <SHIP> SUNK)
 *     tile row  12      : view label, centred (ENEMY WATERS / YOUR WATERS)
 *     tile rows 13..16  : fleet panel, two columns (left 1..9, right 11..19)
 *     tile row  17      : control hint
 *
 *   THREE SCREENS run in sequence:
 *     1. BOOT MENU (choose_mode) -- "PLACE YOUR FLEET?"  A=RANDOM  B=MANUAL
 *     2. MANUAL PLACEMENT (place_fleet), only if B was pressed: steers a live
 *        preview of each ship and commits it. Board shows YOUR waters; the
 *        fleet panel doubles as PLACEMENT PROGRESS, so it must stay blank until
 *        a ship is committed (see status_code / ST_PLACING in draw_fleet).
 *     3. THE GAME (below).
 *   The boot menu draws into row 0 / 11 / 12 / 17, the same rows the game uses.
 *
 *   SEPARATE VIEWS: you must see two sets of ships but there is only room for
 *   one board, so the board shows EITHER the enemy waters you are shelling OR
 *   your own waters. SELECT (or the automatic flip after the enemy's turn)
 *   switches between them.
 *
 *   CONTROLS (the hint row is context-sensitive, so it always shows the keys
 *   that actually apply to the current screen):
 *     game      : D-pad aim, A fire, SELECT flip view
 *     placement : D-pad move preview, B rotate, A commit, START randomise rest
 *
 *   The cursor is an 8x8 SPRITE so it overlays a whole cell (a background
 *   tile could not do that without a second tileset), and it is hidden while
 *   you are inspecting your own waters -- it only marks a firing target.
 *
 * IMPORTANT: we do NOT call console_init(). It would stomp tiles 0..? with
 * GBDK's own font. We load our glyphs at FONT_BASE.. and draw our own text
 * straight into the tilemap.
 */
#include <gb/gb.h>
#include <stdint.h>
#include <rand.h>

#include "gfx.h"
#include "place.h"

#define ROW0      1                 /* first board tile row */
#define BOARD_C0  5                 /* first board tile col (board = 5..14) */
#define STATUS1   0
#define MSG       11                /* message line */
#define INFO      12                /* coord + view label + turn counter */
#define FLEET     13                /* fleet panel, rows 13..16 */
#define HINT      17
#define PANEL_IND 1                 /* fleet-panel indent (icon art starts at the
                                       tile's left edge; at column 0 the bow would
                                       clip against the screen edge) */

/* cell states */
#define S_EMPTY 0
#define S_MISS  1
#define S_HIT   2
#define S_SUNK  3

#define NSHIP      7
static const char *const ship_name[NSHIP] = {
    "CARRIER", "BATTLESHIP", "CRUISER", "DESTROYER", "DESTROYER", "SUB", "SUB"
};
/* short codes for the fleet panel are gone: the panel now draws ship icons
   (see mkgfx.py ship_icon()), so the three-letter acronyms are not needed. */

static Board pb;   /* your waters      -- the computer shoots here */
static Board ob;   /* enemy waters     -- you shoot here          */

static uint8_t vram[360];

/* 0 = looking at ENEMY waters (you fire), 1 = looking at YOUR waters.
   The enemy's turn forces 1 so you always watch your own fleet take the hit. */
/* What the machine is currently doing. The fleet panel needs this: during
   manual placement only ships already committed should be drawn, or the panel
   advertises a full fleet the player has not placed yet. */
#define ST_PLAYING 0
#define ST_PLACING 1
static uint8_t status_code = ST_PLAYING;

static uint8_t view = 0;

/* ---------- map painting ---------- */
static void paint(uint8_t r, uint8_t c, uint8_t t) {
    if (r < 18 && c < 20) vram[r * 20 + c] = t;
}
static void clear_row(uint8_t r) { uint8_t c; for (c = 0; c < 20; c++) paint(r, c, TILE_BLANK); }
static void put_text(uint8_t r, uint8_t c, const char *s) {
    while (*s) paint(r, c++, font_map[(uint8_t)*s++]);
}
static void status1(const char *s) { clear_row(STATUS1); put_text(STATUS1, 0, s); }
/* Draw a centred string on any row. status1() left-aligns at column 0, which is
   right for a short state message but reads lopsided for a screen title. */
static void text_centred(uint8_t row, const char *s) {
    uint8_t len = 0;
    while (s[len]) len++;
    clear_row(row);
    put_text(row, (uint8_t)((20 - len) / 2), s);
}
static void msg(const char *s)     { clear_row(MSG);     put_text(MSG, 0, s); }
static void flush(void) { set_bkg_tiles(0, 0, 20, 18, vram); }

/* Boards sit on a grid whose hairlines live in the CELL ART: every cell carries
   a right and a bottom hairline, so an interior line is drawn once, by the cell
   on its upper/left side. The board's OUTER left edge (column 0) and OUTER top
   edge (row 0) are the only lines no neighbour can supply, so those cells use an
   edge variant that adds exactly the missing edge.
   Do NOT draw a full box on them: a box adds the opposite line too, duplicating
   the line the neighbour already drew -- which reads as every grid line doubled. */
static uint8_t cell_tile(const Board *b, uint8_t i, uint8_t reveal) {
    if (b->st[i] == S_MISS) return TILE_MISS;
    if (b->own[i] == CELL_EMPTY) return TILE_CELL;
    if (b->st[i] == S_HIT || b->st[i] == S_SUNK) return TILE_HIT;
    return reveal ? TILE_SHIP : TILE_CELL;
}

/* Pick the edge variant for a cell on column 0 and/or row 0. */
static uint8_t edge_tile(uint8_t t, uint8_t left, uint8_t top) {
    if (left && top) {
        switch (t) {
            case TILE_CELL: return TILE_CELLTL;
            case TILE_MISS: return TILE_MISSTL;
            case TILE_HIT:  return TILE_HITTL;
            case TILE_SHIP: return TILE_SHIPTL;
        }
    } else if (left) {
        switch (t) {
            case TILE_CELL: return TILE_CELLL;
            case TILE_MISS: return TILE_MISSL;
            case TILE_HIT:  return TILE_HITL;
            case TILE_SHIP: return TILE_SHIPL;
        }
    } else if (top) {
        switch (t) {
            case TILE_CELL: return TILE_CELLT;
            case TILE_MISS: return TILE_MISST;
            case TILE_HIT:  return TILE_HITT;
            case TILE_SHIP: return TILE_SHIPT;
        }
    }
    return t;
}

static const Board *viewed_board(void) { return view ? &pb : &ob; }

/* Draw whichever board `view` currently refers to. */
static void draw_board(void) {
    const Board *b = viewed_board();
    uint8_t reveal = view;          /* your own ships are always visible */
    uint8_t x, y;
    for (y = 0; y < 10; y++)
        for (x = 0; x < 10; x++) {
            uint8_t t = cell_tile(b, y * 10 + x, reveal);
            if (x == 0 || y == 0) t = edge_tile(t, x == 0, y == 0);
            paint(ROW0 + y, BOARD_C0 + x, t);
        }
}

/* Fleet panel: one entry per ship, drawn as a SHIP ICON whose hull width encodes
   the ship's length, followed by the length digit. The old 3-letter code (CAR,
   DS1) is gone, and so is the '.'/'X' status column: the icon itself is grey
   while afloat and solid black with a white X once sunk.
   Indented by PANEL_IND tiles: the icon art fills its tile from the left edge,
   so drawing at column 0 would clip the bow against the screen edge.
   4 down the left column (rows 13..16), 3 down the right (rows 13..15). */
static void draw_fleet(void) {
    const Board *b = viewed_board();
    uint8_t s;
    /* During manual placement, only ships actually committed so far exist in
       `own`, so show just those as afloat. Without this the panel reads as a
       full fleet that the player has not placed yet -- the panel is meant to be
       placement PROGRESS, and it lied. */
    uint8_t placing = (status_code == ST_PLACING);
    for (s = 0; s < NSHIP; s++) {
        uint8_t r = (uint8_t)(FLEET + (s < 4 ? s : s - 4));
        uint8_t c = (uint8_t)((s < 4 ? 0 : 10) + PANEL_IND);
        /* NOTE: test the ship's OWN cells only. An earlier version also checked
           `b->own[s*10] != CELL_EMPTY` -- cell (s,0), which is meaningless and
           just happens to be occupied by whatever ship sits there, so ships
           showed as "placed" before they were. ship_is_placed() is the check. */
        uint8_t placed = (uint8_t)(placing ? ship_is_placed(b, s) : 1);
        if (!placed) {                 /* not placed yet: blank the whole entry */
            uint8_t k;
            for (k = 0; k < 8; k++) paint(r, (uint8_t)(c + k), TILE_BLANK);
            continue;
        }
        uint8_t sunk = (b->hits[s] >= ship_len[s]);
        uint8_t k;
        for (k = 0; k < 8; k++) paint(r, (uint8_t)(c + k), TILE_BLANK);
        /* Icons are 16x8, i.e. TWO tiles (L then R). Tile indices form a
           contiguous block: SHIP_ICON_BASE + 4*s [+2 when sunk], then +1 for
           the right-hand tile. */
        uint8_t base = (uint8_t)(SHIP_ICON_BASE + 4 * s + (sunk ? SHIP_ICON_SUNK : 0));
        uint8_t lenbuf[2] = { (char)('0' + ship_len[s]), 0 };
        paint(r, c, base);
        paint(r, (uint8_t)(c + 1), (uint8_t)(base + 1));
        put_text(r, (uint8_t)(c + 2), lenbuf);
    }
}

static void draw_view_label(void) {
    /* The coord readout and turn counter are gone, so this label owns the whole
       INFO row and can be centred with the full wording ("ENEMY WATERS" was
       shortened to "ENEMY SEA" only to avoid running into the turn counter). */
    text_centred(INFO, view ? "YOUR WATERS" : "ENEMY WATERS");
}

static void draw_hint(void) {
    clear_row(HINT);
    put_text(HINT, 0, "A=FIRE SEL=VIEW");
}

/* GBDK's Game Boy target has NO show_sprite/hide_sprite -- those exist only
   for NES/SMS. On DMG you hide a sprite by parking it off-screen: the visible
   Y range starts at 16, so Y = 0 draws nothing. */
static void cursor_off(void) { move_sprite(0, 0, 0); }

/* ---- game-over splash screens (supplied art) --------------------------------
 * Two full-screen 160x144 images, 125 and 139 unique tiles. They share only 9
 * tiles (the wave rows, which land on the same 8x8 boundary in both) and the
 * most-used tile differs between them, so they are effectively independent
 * tilesets: 255 tiles together against ~158 free VRAM slots. They therefore
 * cannot both be resident -- but only one is ever shown, so we load the needed
 * set just above the game tiles, blit the map, and put the game tiles back
 * before returning. That is why the fleet reveal after it still renders.
 */
#define SPLASH_VRAM_BASE GFX_TILE_COUNT   /* 98 -- first slot above the game tiles */

#ifndef ANIM_TOTAL
#define ANIM_TOTAL 300      /* 300 frames @60 Hz = 5.00 s */
#endif

static void splash_show(const uint8_t *tiles, uint8_t ntiles, const uint8_t *map) {
    uint16_t i;
    uint16_t f;             /* uint8_t would never reach ANIM_TOTAL=300 */

    cursor_off();           /* else the firing cursor's hollow box sits on the art */

    set_bkg_data(SPLASH_VRAM_BASE, ntiles, tiles);
    for (i = 0; i < 360; i++)
        vram[i] = (uint8_t)(SPLASH_VRAM_BASE + map[i]);
    flush();

    for (f = 0; f < ANIM_TOTAL; f++) wait_vbl_done();

    /* Hand the tile area back to the game, AND clear the map.
       Clearing vram matters as much as restoring the tiles: the splash map is
       full of indices >= GFX_TILE_COUNT, and game_over() only repaints the rows
       it owns (status/info/hint + board + panel). Anything it does not repaint
       -- the rows the big art occupied, the side columns -- would still point at
       splash tiles, so the splash bled through behind the reveal. */
    set_bkg_data(0, GFX_TILE_COUNT, gfx_tiles);
    for (i = 0; i < 360; i++) vram[i] = TILE_BLANK;
    flush();
}

/* ---- title screen ----------------------------------------------------------
 * Shown ONCE, on boot only -- replays go straight to the mode menu.
 *
 * The art is two frames of the same 160x144 image that differ ONLY in the
 * PRESS START band (tile rows 14-15), so they share ONE 190-tile tileset and
 * the flash is just a map swap: no VRAM reload, so the band switches instantly
 * and cannot tear mid-blit.
 *
 * Loaded at VRAM tile base 0, NOT the game-over splash base (98): 98 + 190 =
 * 288 would run past the 256-tile VRAM and corrupt the sprite tiles. Base 0
 * costs 190 of 256, and the game tiles are restored on exit.
 *
 * Only START leaves this screen. A/B/SELECT/d-pad are deliberately ignored, so
 * the game cannot be started by mashing -- the player must read the prompt.
 */
#define TITLE_BLINK_FRAMES 60          /* 1 s on, 1 s off @ 60 Hz */

static void title_screen(void) {
    uint16_t i, f;

    cursor_off();
    HIDE_SPRITES;                      /* the firing cursor must not show here */

    set_bkg_data(0, TITLE_TILES, title_tiles);
    for (i = 0; i < 360; i++) vram[i] = title_map_on[i];
    flush();

    for (;;) {
        /* ---- on for one second ---- */
        for (f = 0; f < TITLE_BLINK_FRAMES; f++) {
            if (joypad() & J_START) goto pressed;
            wait_vbl_done();
        }
        /* ---- off for one second ---- */
        for (i = 0; i < 360; i++) vram[i] = title_map_off[i];
        flush();
        for (f = 0; f < TITLE_BLINK_FRAMES; f++) {
            if (joypad() & J_START) goto pressed;
            wait_vbl_done();
        }
        /* ---- back on (same tiles, so just re-blit the 'on' map) ---- */
        for (i = 0; i < 360; i++) vram[i] = title_map_on[i];
        flush();
    }

pressed:
    while (joypad() & J_START) wait_vbl_done();    /* wait for release */

    /* hand the tile area back to the game */
    set_bkg_data(0, GFX_TILE_COUNT, gfx_tiles);
    for (i = 0; i < 360; i++) vram[i] = TILE_BLANK;
    flush();
}

/* ---- game over: reveal BOTH fleets ------------------------------------------
 * Previously each end state froze with `for (;;) wait_vbl_done()`, so the enemy
 * fleet was never shown -- the player could win without ever learning where the
 * ships had been. This screen replaces that freeze with something browsable.
 *
 * It does NOT touch `view`: draw_board()'s `reveal` is derived from `view`, but
 * we override it by repainting from the enemy board with reveal forced on. The
 * player can flip between the two fleets, so they see both what they were
 * hunting and what was hunting them.
 */
static void game_over(const char *title) {
    uint8_t showing_own = 0;

    while (joypad() & (J_A | J_B | J_START | J_SELECT)) wait_vbl_done();
    cursor_off();

    for (;;) {
        const Board *b = showing_own ? &pb : &ob;
        uint8_t x, y;

        /* draw_fleet() reads viewed_board(), i.e. `view` -- so keep view in step
           with what we are showing, or the panel describes one fleet while the
           grid shows the other. */
        view = showing_own;

        status1(title);
        msg("");
        /* label the board underneath: whose fleet are we looking at? */
        text_centred(INFO, showing_own ? "YOUR FLEET" : "ENEMY FLEET");
        clear_row(HINT);
        /* Two actions, one row each, so neither is cramped: SELECT browses the
           other fleet, START starts a new game (returns to the mode menu). */
        clear_row(MSG);
        put_text(MSG, 0, "SELECT=SWAP FLEET");
        put_text(HINT, 0, "START=NEW GAME");

        /* repaint the board with ALL ships revealed, regardless of hits */
        for (y = 0; y < 10; y++)
            for (x = 0; x < 10; x++) {
                uint8_t i = (uint8_t)(y * 10 + x);
                uint8_t t;
                if (b->st[i] == S_HIT || b->st[i] == S_SUNK) t = TILE_HIT;
                else if (b->own[i] != CELL_EMPTY)           t = TILE_SHIP;
                else                                        t = TILE_CELL;
                if (x == 0 || y == 0) t = edge_tile(t, x == 0, y == 0);
                paint((uint8_t)(ROW0 + y), (uint8_t)(BOARD_C0 + x), t);
            }
        /* fleet panel for the board on screen */
        draw_fleet();
        flush();

        /* ---- input ----
           START = play again (the requested behaviour): return to the caller,
           which breaks out of the game loop and re-runs setup.
           SELECT/B/A browse the other fleet. */
        for (;;) {
            uint8_t j = joypad();
            if (j & J_START) return;
            if (j & (J_SELECT | J_A | J_B)) {
                while (joypad() & j) wait_vbl_done();
                showing_own ^= 1;
                break;
            }
            wait_vbl_done();
        }
    }
}

/* Wait n vertical blanks (~n/60 s). Declared here, above its first use in
   place_fleet/choose_mode -- C needs it before it is called. */
static void pause_frames(uint16_t n) { while (n--) wait_vbl_done(); }

/* Forward decls: choose_mode/place_fleet are defined above init_board, but call
   it. C requires a declaration before use. */
static void init_board(Board *b);

/* Boot menu. A = random fleet (as before), B = place your own.
   Returns 1 for manual placement, 0 for random. Blocks until a choice is made
   and the button released, so a held button cannot skip straight into the game. */
static uint8_t choose_mode(void) {
    uint8_t choice;
    /* The game name sits one row below the status line. Row 1 is the board's
       FIRST tile row, so the title deliberately overlaps the playfield here --
       a title drawn on a row the board later owns would otherwise never be
       repainted. choose_mode() therefore clears the row again before returning.
       (Row 0 above it is left blank.) */
    text_centred(STATUS1 + 1, "NAVAL HUNTER");
    msg("PLACE YOUR FLEET?");
    clear_row(INFO);
    put_text(INFO, 0, "A=RANDOM");
    put_text(INFO, 10, "B=MANUAL");
    clear_row(HINT);
    put_text(HINT, 0, "PICK A OR B");
    flush();

    for (;;) {
        uint8_t j = joypad();
        if (j & J_A) { while (joypad() & J_A) wait_vbl_done(); choice = 0; break; }
        if (j & J_B) { while (joypad() & J_B) wait_vbl_done(); choice = 1; break; }
        wait_vbl_done();
    }

    /* Undo the title before the playfield appears. Only the board's own columns
       (5..14) get repainted from here on, so the outermost letters of the title
       -- which sit on columns 4 and 15, OUTSIDE the board -- would otherwise
       survive into the game. Clearing here rather than at the call site keeps
       the screen self-contained, so neither the manual nor the random path can
       forget it. */
    clear_row(STATUS1 + 1);
    flush();
    return choice;
}

/* ---------- direction input with key-repeat ----------
 * Without this, holding a direction moves one square per frame (~60/sec), so a
 * normal tap overshoots by several squares and you cannot land on a neighbour.
 * Behaviour: move immediately on press, then wait REPEAT_DELAY frames before
 * auto-repeating every REPEAT_RATE frames (like a keyboard cursor). */
#define REPEAT_DELAY 18   /* ~0.30 s before auto-repeat kicks in */
#define REPEAT_RATE   9   /* then ~6.7 moves/sec while held */

static uint8_t held_btn;      /* which direction is currently repeating (0 = none) */
static uint8_t held_timer;    /* frames remaining before the next repeat */

/* Repaint board + panel + view label for the current view. */
static void redraw(void) {
    draw_board();
    draw_fleet();
    draw_view_label();
    flush();
}

/* Manual fleet placement.
 *
 * Walks the 7 ships in order. The player steers a preview of the ship around
 * the board and commits it:
 *   D-pad   move the preview (key-repeat, same feel as the firing cursor)
 *   B       rotate horizontal <-> vertical
 *   A       commit, if the position is legal
 *   START   accept randomly-placed ships for everything still unplaced
 *
 * Legal positions show the ship in grey (TILE_SHIP); illegal ones show a solid
 * block (TILE_BAD), so the no-touching rule is visible rather than a silent
 * refusal. Everything already committed stays drawn on the board.
 *
 * Writes ONLY to pb (the player's board); the enemy's ob is placed randomly
 * before this runs. */
static void place_fleet(void) {
    uint8_t s = 0;                 /* ship being placed */
    uint8_t horiz = 1;             /* orientation: 0 = vertical */
    uint8_t x = 0, y = 0;          /* preview origin (top-left cell) */

    clear_board(&pb);
    view = 1;                      /* placement happens on YOUR OWN waters */
    status_code = ST_PLACING;      /* tells draw_fleet to show progress only */
    cursor_off();
    status1("PLACE SHIPS");

    while (s < NSHIP) {
        uint8_t len = ship_len[s];
        uint8_t maxx = horiz ? (uint8_t)(10 - len) : 9;
        uint8_t maxy = horiz ? 9 : (uint8_t)(10 - len);
        uint8_t legal;

        /* keep the whole hull on the board after a rotate or at the edges */
        if (x > maxx) x = maxx;
        if (y > maxy) y = maxy;
        legal = can_place(&pb, x, y, len, horiz);

        /* repaint: committed ships, then a preview of the ship in hand */
        draw_board();
        draw_fleet();
        draw_view_label();
        {
            uint8_t k;
            uint8_t t = legal ? TILE_SHIP : TILE_BAD;
            for (k = 0; k < len; k++) {
                uint8_t px = (uint8_t)(BOARD_C0 + (horiz ? x + k : x));
                uint8_t py = horiz ? y : (uint8_t)(y + k);
                paint(ROW0 + py, px, t);
            }
        }
        {
            /* status: which ship, how long, and whether it fits here */
            char b[20];
            uint8_t i = 0;
            b[i++] = 'S';
            b[i++] = (char)('0' + (s + 1));
            b[i++] = '/';
            b[i++] = '7';
            b[i++] = ' ';
            b[i++] = 'L';
            b[i++] = (char)('0' + len);
            b[i++] = ' ';
            b[i++] = horiz ? 'H' : 'V';
            b[i++] = ' ';
            b[i] = 0;
            clear_row(MSG);
            put_text(MSG, 0, b);
            put_text(MSG, 11, legal ? "OK" : "NO FIT");
        }
        /* context hint, since the controls differ on this screen */
        clear_row(HINT);
        put_text(HINT, 0, "A=OK B=TURN ST=SKIP");
        flush();

        /* ---- input ---- */
        {
            uint8_t j = joypad();

            if (j & J_START) {                 /* randomise the remainder */
                uint8_t ok = 1;
                while (joypad() & J_START) wait_vbl_done();
                /* Fill the rest at random. If one cannot be placed (very
                   unlikely) stop rather than spin -- the fleet is then short a
                   ship, which the panel shows honestly. */
                for (; s < NSHIP && ok; s++)
                    ok = place_random_one(&pb, s);
                break;
            }
            if (j & J_B) {                     /* rotate */
                while (joypad() & J_B) wait_vbl_done();
                horiz ^= 1;
                continue;
            }
            if (j & J_A) {                     /* commit */
                while (joypad() & J_A) wait_vbl_done();
                if (legal) {
                    place_ship(&pb, x, y, len, horiz, s);
                    s++;
                    x = 0; y = 0;
                } else {
                    msg("TOO CLOSE");
                    flush();
                    pause_frames(30);
                }
                continue;
            }

            /* direction, with the same key-repeat feel as the firing cursor */
            {
                uint8_t d = j & (J_LEFT | J_RIGHT | J_UP | J_DOWN);
                uint8_t step = 0;
                if (d == 0) {
                    held_btn = 0;
                } else if (d != held_btn) {
                    held_btn = d; held_timer = REPEAT_DELAY; step = 1;
                } else if (held_timer) {
                    held_timer--;
                } else {
                    held_timer = REPEAT_RATE; step = 1;
                }
                if (step) {
                    if ((d & J_LEFT)  && x > 0)    x--;
                    if ((d & J_RIGHT) && x < maxx) x++;
                    if ((d & J_UP)    && y > 0)    y--;
                    if ((d & J_DOWN)  && y < maxy) y++;
                }
            }
        }
        wait_vbl_done();
    }

    /* done: put the game's own chrome back */
    status_code = ST_PLAYING;
    view = 0;
    status1("YOUR TURN");
    msg("");
    draw_view_label();
    draw_hint();
    redraw();
}

/* ---------- fleet placement (rules live in place.h) ---------- */
/* Random fleet. clear_board/place_random_one/place_ship/can_place live in
   place.h so the placement rules are unit-testable on the host (tests/). */
static void init_board(Board *b) {
    uint8_t s;

    for (;;) {                      /* retry loop: only re-entered on failure */
        clear_board(b);
        for (s = 0; s < NSHIP; s++)
            if (!place_random_one(b, s)) break;      /* s < NSHIP -> failed */
        if (s == NSHIP) return;                      /* whole fleet placed */
        /* Pathological: a ship could not be placed after 200 tries (needs a
           very unlucky sequence). Start the whole fleet over rather than spin. */
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
/* Single centred board, so the cursor only needs cell coords now. */
static void move_cursor(uint8_t x, uint8_t y) {
    /* sprite coords are (pixel + 8, pixel + 16) */
    move_sprite(0, (uint8_t)((BOARD_C0 + x) * 8 + 8),
                   (uint8_t)((ROW0 + y) * 8 + 16));
}

/* The INFO row now holds only the view label (see draw_view_label). The "A01"
   target-coordinate readout and the turn counter were removed on request:
   both were unlabelled numbers that the player had to decode, and the turn
   count told them nothing actionable. */

/* ---------- sound: a hit is an explosion, a sinking is a bigger one ----------
 * Channel 4 is the noise generator, which is the standard Game Boy explosion
 * voice -- a low, rumbling burst with no tone. There is no sound library in
 * this GBDK build, so the four channel-4 registers are driven directly; that is
 * the same approach GBDK's own examples/gb/sound/sound.c takes.
 *
 * The sound must NOT block. The hardware envelope decays the burst by itself,
 * so a shot resolves and the turn loop carries straight on. Waiting here for
 * the explosion to finish would stall the game for its whole duration.
 *
 * One mechanism ends the sound, not two: NR41 (length) is deliberately left at
 * its reset value and only the envelope (NR42) retires the note. The length
 * counter cannot be relied on for this without also setting NR44 bit 6, and a
 * decay that is switched on by the same write that starts the note is simpler
 * to reason about than two independent stop conditions.
 *
 * Envelope (NR42): volume 15, direction = decay.
 * Poly (NR43): 15-bit noise, higher `shift` clocks the LFSR slower = lower pitch.
 */
#define NOISE_TO_BOTH_SPEAKERS (AUDTERM_4_LEFT | AUDTERM_4_RIGHT)

static void sound_init(void) {
    rAUDENA = AUDENA_ON;                     /* power the APU up FIRST         */
    rAUDVOL = (uint8_t)(AUDVOL_VOL_LEFT(7) | AUDVOL_VOL_RIGHT(7));
    rAUDTERM = NOISE_TO_BOTH_SPEAKERS;       /* channel 4 out of both speakers */
}

/* big != 0 = a ship going down: slower decay, deeper, so it is clearly distinct
   from the ordinary hit sound rather than just louder. */
static void sound_explosion(uint8_t big) {
    rAUD4ENV  = (uint8_t)(big ? 0xF7 : 0xF3); /* vol 15, decay; big = slower    */
    rAUD4POLY = (uint8_t)(big ? 0x76 : 0x65); /* big = lower/slower noise clock */
    rAUD4GO   = 0x80;                         /* retrigger the channel          */
}

void main(void) {
    uint8_t cx, cy;

    initrand((uint16_t)sys_time ^ 0xA55Au);

    /* Power up audio once, at boot. Register writes to a powered-down APU are
       ignored on hardware, so this has to happen before any sound request. */
    sound_init();

    set_bkg_data(0, GFX_TILE_COUNT, gfx_tiles);
    set_sprite_data(0, 1, sprite_tiles);
    SPRITES_8x8;

    /* Turn the display on BEFORE the boot menu: DISPLAY_ON must not be waiting
       at the end of main(), or the menu renders invisible and the player stares
       at a blank screen while the ROM blocks in choose_mode(). */
    SHOW_BKG;
    DISPLAY_ON;

#ifndef NO_TITLE
    /* Boot-only title screen, deliberately OUTSIDE the outer game loop: the
       loop runs once per game, so anything inside it would flash the title on
       every replay as well. Replays go straight to the mode menu.
       Test ROMs pass -DNO_TITLE so a scripted test is not blocked on a
       keypress before the game begins. */
    title_screen();
#endif

    /* ---- outer loop: one iteration == one whole game ----
       Everything from here is re-run each time the player presses START on the
       game-over screen, which is how "play again" works: game_over() returns,
       we fall out of the inner game loop, and come back round to the boot menu
       with freshly generated boards. Nothing persists between games. */
    for (;;) {
    cx = 0; cy = 0;
    /* per-game state, reset so a replay cannot inherit the last game's view
       label, placement mode or key-repeat state */
    view = 0;
    status_code = ST_PLAYING;
    held_btn = 0; held_timer = 0;
    cursor_off();

    { uint16_t i; for (i = 0; i < 360; i++) vram[i] = TILE_BLANK; }

    /* ---- boot menu: random fleet or place your own? ---- */
    {
        uint8_t manual = choose_mode();
        init_board(&ob);                       /* enemy fleet is always random */
        if (manual) place_fleet();             /* fills pb interactively */
        else        init_board(&pb);           /* or randomly, as before */
    }
    { uint8_t i; for (i = 0; i < 100; i++) target[i] = 0; }

#ifdef TEST_WIN
    /* TEST-ONLY (never in the shipped ROM): pre-sink the whole enemy fleet
       except one cell, so a single shot triggers the YOU WIN! branch without
       playing 60+ turns. */
    {
        uint8_t i;
        for (i = 0; i < 100; i++) if (ob.own[i] != CELL_EMPTY) ob.st[i] = S_SUNK;
        ob.own[0] = 0;                 /* pretend the carrier sits on A1 */
        ob.st[0] = S_EMPTY;
        ob.hits[0] = (uint8_t)(ship_len[0] - 1);
        ob.alive = 1;
    }
#endif
#ifdef TEST_LOSE
    {
        uint8_t i;
        for (i = 0; i < 100; i++) if (pb.own[i] != CELL_EMPTY) pb.st[i] = S_SUNK;
        pb.own[0] = 0;
        pb.st[0] = S_EMPTY;
        pb.hits[0] = (uint8_t)(ship_len[0] - 1);
        pb.alive = 1;
    }
#endif

    view = 0;
    draw_board();
    draw_fleet();
    draw_view_label();
    draw_hint();
    status1("YOUR TURN");
    msg("");
    flush();

    set_sprite_tile(0, 0);
    move_cursor(cx, cy);
    SHOW_SPRITES;
    SHOW_BKG;
    DISPLAY_ON;

    for (;;) {
        uint8_t j, r, sunk = 0, px = cx, py = cy;
        uint8_t ex, ey;                 /* the enemy's shot lands here */

        wait_vbl_done();
        j = joypad();

        /* ---- SELECT: flip between the enemy grid and your own waters ---- */
        if (j & J_SELECT) {
            while (joypad() & J_SELECT) wait_vbl_done();
            view ^= 1;
            if (view) cursor_off(); else move_cursor(cx, cy);
            redraw();
            continue;
        }

        /* Inspecting your own waters is read-only: no moving, no firing. You
           cannot shoot your own fleet, and the cursor is hidden anyway. */
        if (view) continue;

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
                    move_cursor(cx, cy);
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
            msg("ALREADY SHOT");
            flush();
            pause_frames(30);
            msg("");
            flush();
            continue;
        }
        if (r == 0)      { msg("MISS"); }
        else if (r == 1) { msg("HIT!"); sound_explosion(0); }
        else             { msg(""); put_text(MSG, 0, ship_name[sunk]); put_text(MSG, 11, "SUNK"); sound_explosion(1); }
        draw_board();
        draw_fleet();
        flush();
        if (ob.alive == 0) {
            /* Big splash for 5 s, then the fleet reveal. game_over() returns
               only when the player presses START, which means "play again". */
            splash_show(splash_win_tiles, SPLASH_WIN_TILES, splash_win_map);
            game_over("YOU WIN!");
            break;
        }
        pause_frames(45);

        /* ---- enemy shot: flip to YOUR waters so the hit is visible ---- */
        view = 1;
        status1("ENEMY TURN");
        cursor_off();
        redraw();
        pause_frames(30);

        ai_pick(&ex, &ey);
        move_cursor(ex, ey);
        r = fire(&pb, ex, ey, &sunk);
        if (r == 3) {
            /* ai_pick should never choose a spent square, but if it somehow
               does, fall through to the hunt so it cannot stall the game. */
            uint8_t i;
            for (i = 0; i < 100; i++) {
                if (pb.st[i] == S_EMPTY) { ex = i % 10; ey = i / 10; break; }
            }
            move_cursor(ex, ey);
            r = fire(&pb, ex, ey, &sunk);
        }
        if (r >= 1) {               /* queue the four orthogonal neighbours */
            ai_queue(ex + 1, ey); ai_queue(ex - 1, ey);
            ai_queue(ex, ey + 1); ai_queue(ex, ey - 1);
        }
        if (r == 2) {               /* ship gone: its queued cells are stale */
            uint8_t i; for (i = 0; i < 100; i++) target[i] = 0;
        }
        if (r == 0)      { msg("ENEMY MISS"); }
        else if (r == 1) { msg("ENEMY HIT!"); sound_explosion(0); }
        else             { msg(""); put_text(MSG, 0, ship_name[sunk]); put_text(MSG, 11, "SUNK"); sound_explosion(1); }
        draw_board();
        draw_fleet();
        flush();
        if (pb.alive == 0) {
            splash_show(splash_lose_tiles, SPLASH_LOSE_TILES, splash_lose_map);
            game_over("YOU LOSE");
            break;
        }
        pause_frames(45);

        /* ---- back to you: enemy grid, cursor where you left it ---- */
        view = 0;
        status1("YOUR TURN");
        msg("");
        move_cursor(cx, cy);
        redraw();
        flush();
    }
    }   /* end outer replay loop */
}
