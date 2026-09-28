#!/usr/bin/env python3
import os
from PIL import Image

"""Generates gfx.h for the battleship ROM: 5x7 font (47 glyphs), board tiles, cursor.
Tile encoding: 2bpp Game Boy format, low byte then high byte per row.
'#'->colour 3 (black), '+'->1, '*'->2, '.'->0 (transparent/white).
"""
FONT = {}   # filled by build_font() below

def build_font():
    g = {}
    g[" "] = ["....."] * 7
    g["!"] = ["..#..","..#..","..#..","..#..","..#..","....." ,"..#.."]
    g["'"] = ["..#..","..#..","....."  ,"....."  ,"....."  ,"....."  ,"....."]
    g[","] = [".....",".....",".....",".....","..#..","..#..",".#..."]
    g["-"] = [".....",".....",".....",".###.",".....",".....","....."]
    g["."] = [".....",".....",".....",".....",".....",".....","..#.."]
    g["/"] = ["....#","....#","...#.","..#..",".#...","#....","#...."]
    g[":"] = [".....","..#..","..#..",".....","..#..","..#..","....."]
    g["<"] = ["...#.","..#..",".#...","#....",".#...","..#..","...#."]
    g[">"] = [".#...","..#..","...#.","....#","...#.","..#..",".#..."]
    g["="] = [".....",".....","#####",".....","#####",".....","....."]
    g["["] = ["..###","..#..","..#..","..#..","..#..","..#..","..###"]
    g["]"] = ["###..","..#..","..#..","..#..","..#..","..#..","###.."]
    g["0"] = [".###.","#...#","#..##","#.#.#","##..#","#...#",".###."]
    g["1"] = ["..#..",".##..","..#..","..#..","..#..","..#..",".###."]
    g["2"] = [".###.","#...#","....#","...#.","..#..",".#...","#####"]
    g["3"] = ["####.","....#","....#",".###.","....#","....#","####."]
    g["4"] = ["...#.","..##.",".#.#.","#..#.","#####","...#.","...#."]
    g["5"] = ["#####","#....","####.","....#","....#","#...#",".###."]
    g["6"] = ["..##.",".#...","#....","####.","#...#","#...#",".###."]
    g["7"] = ["#####","....#","...#.","..#..",".#...",".#...",".#..."]
    g["8"] = [".###.","#...#","#...#",".###.","#...#","#...#",".###."]
    g["9"] = [".###.","#...#","#...#",".####","....#","...#.",".##.."]
    g["A"] = ["..#..",".#.#.","#...#","#...#","#####","#...#","#...#"]
    g["B"] = ["####.","#...#","#...#","####.","#...#","#...#","####."]
    g["C"] = [".###.","#...#","#....","#....","#....","#...#",".###."]
    g["D"] = ["###..","#..#.","#...#","#...#","#...#","#..#.","###.."]
    g["E"] = ["#####","#....","#....","####.","#....","#....","#####"]
    g["F"] = ["#####","#....","#....","####.","#....","#....","#...."]
    g["G"] = [".###.","#...#","#....","#.###","#...#","#...#",".###."]
    g["H"] = ["#...#","#...#","#...#","#####","#...#","#...#","#...#"]
    g["I"] = [".###.","..#..","..#..","..#..","..#..","..#..",".###."]
    g["J"] = ["....#","....#","....#","....#","#...#","#...#",".###."]
    g["K"] = ["#...#","#..#.","#.#..","##...","#.#..","#..#.","#...#"]
    g["L"] = ["#....","#....","#....","#....","#....","#....","#####"]
    g["M"] = ["#...#","##.##","#.#.#","#.#.#","#...#","#...#","#...#"]
    g["N"] = ["#...#","##..#","#.#.#","#..##","#...#","#...#","#...#"]
    g["O"] = [".###.","#...#","#...#","#...#","#...#","#...#",".###."]
    g["P"] = ["####.","#...#","#...#","####.","#....","#....","#...."]
    g["Q"] = [".###.","#...#","#...#","#...#","#.#.#","#..#.",".##.#"]
    g["R"] = ["####.","#...#","#...#","####.","#.#..","#..#.","#...#"]
    g["S"] = [".####","#....","#....",".###.","....#","....#","####."]
    g["T"] = ["#####","..#..","..#..","..#..","..#..","..#..","..#.."]
    g["U"] = ["#...#","#...#","#...#","#...#","#...#","#...#",".###."]
    g["V"] = ["#...#","#...#","#...#","#...#","#...#",".#.#.","..#.."]
    g["W"] = ["#...#","#...#","#...#","#.#.#","#.#.#","##.##","#...#"]
    g["X"] = ["#...#","#...#",".#.#.","..#..",".#.#.","#...#","#...#"]
    g["Y"] = ["#...#","#...#",".#.#.","..#..","..#..","..#..","..#.."]
    g["Z"] = ["#####","....#","...#.","..#..",".#...","#....","#####"]
    return g

def add_grid_lines(rows, gutter=0):
    """Add 1px colour-1 hairlines on the right and bottom edge of a cell,
    so adjacent cells tile into a visible grid. Never overwrites existing art.
    gutter>0 leaves the leftmost `gutter` pixels blank -> a board divider."""
    rows = pad(rows)
    out = []
    for y in range(8):
        r = list(rows[y])
        if r[7] == '.':
            r[7] = '1'                  # right edge
        if y == 7:
            for i in range(8):
                if r[i] == '.':
                    r[i] = '1'          # bottom edge
        for i in range(gutter):         # carved gutter: force blank
            r[i] = '.'
        out.append("".join(r))
    return out

def add_edges(rows, left=False, top=False):
    """Add a 1px grid hairline on the requested OUTER edge(s) ONLY, on top of the
    right/bottom hairlines add_grid_lines already gives every cell.

    Why only the requested edges: a full box (set the top row AND the left column)
    is WRONG except at the corner cell. Every cell already inherits its top line
    from the neighbour above and its left line from the neighbour to the left, so
    the only genuinely missing lines are the board's OUTER left edge (column 0) and
    OUTER top edge (row 0). Drawing a full box on a column-0 tile paints a spurious
    horizontal line down the left column, and on a row-0 tile a spurious vertical
    line across the top row -- which reads as every grid line being DOUBLED."""
    rows = list(add_grid_lines(rows))
    out = []
    for y in range(8):
        r = list(rows[y])
        if top and y == 0:
            for i in range(8):
                r[i] = '1'              # outer top edge
        if left:
            r[0] = '1'                  # outer left edge
        out.append("".join(r))
    return out


def ship_icon(length, pips, sunk):
    """16x8 fleet-panel icon (TWO tiles wide): a side-profile boat whose hull
    width encodes the ship's length, so the panel shows fleet composition at a
    glance instead of a three-letter code.

    Why 16 px wide rather than 8: at 8 px the widest hull was 8 px and the bridge
    and masts collapsed into a featureless blob -- the ships did not read as
    boats. Two tiles give w = 4 + 2*length (6..14 px) and room for a bridge, so
    each ship is recognisable and its length is obvious.

    Silhouette, bottom-up: deck line, two hull rows (the lower one inset at the
    bow so the hull tapers), a bridge block sitting on the deck, and `pips`
    masts. Masts are what tell apart the two ships sharing a length -- the
    destroyers 1/2 and subs 1/2 are otherwise identical.

    `sunk` draws the hull solid black with a white X punched through it, so
    afloat/sunk is graphical and the old '.'/'X' column is redundant."""
    w = 4 + 2 * length
    if w > 16:
        w = 16
    rows = [['.'] * 16 for _ in range(8)]
    body = '3' if sunk else '2'
    for x in range(w):
        rows[4][x] = '3'                        # deck line
    for x in range(w):
        rows[5][x] = body                       # hull
    for x in range(w - 2):
        rows[6][x] = body                       # hull, inset at the bow
    bw = max(2, w // 3)                         # bridge block on the deck
    for x in range(2, min(16, 2 + bw)):
        rows[2][x] = '3'
        rows[3][x] = '3'
    for i in range(pips):                       # masts disambiguate same-length ships
        x = 3 + i * 4
        if x < 15:
            rows[0][x] = '3'
            rows[1][x] = '3'
    if sunk:                                    # white X, only over hull pixels so
        for i in range(16):                     # it can never paint white on the bg
            for (r, c) in ((4 + i, i), (4 + i, w - 1 - i)):
                if 0 <= r < 8 and 0 <= c < 16 and rows[r][c] != '.':
                    rows[r][c] = '0'
    return ["".join(r) for r in rows]


def split_wide(rows):
    """Split a 16x8 icon into its two 8x8 VRAM tiles: (left, right)."""
    return ([r[0:8] for r in rows], [r[8:16] for r in rows])


# (length, masts) per ship, in the same order as ship_len[] in main.c
FLEET_SPEC = [(5, 0), (4, 0), (3, 0), (2, 1), (2, 2), (1, 1), (1, 2)]


# --- game-over splash art (supplied as PNGs) --------------------------------
# The two splashes are full-screen 160x144 four-colour images. They are LARGE
# tilesets: 125 (win) and 139 (lose) unique tiles, sharing only 9 -- the wave
# rows, which happen to land on the same 8x8 boundary in both. 125+139-9 = 255,
# but only ~158 VRAM slots are free, so the two CANNOT be resident at once.
# They do not need to be: only one is ever shown, so main.c loads the needed
# set over the game's tile area and restores the game tiles immediately after.
#
# Shade mapping is by EXACT RGB, not by how often a colour appears. The images
# are mostly different greens (win is mostly the lightest, lose is mostly
# colour 2), so a frequency-based map would render their skies as different
# shades and the two screens would not match.
DMG_RGB = {                      # standard DMG palette, lightest -> darkest
    (155, 188, 15): 0,
    (139, 172, 15): 1,
    (48, 98, 48):   2,
    (15, 56, 15):   3,
}
SPLASH_SRC = (("win", "art/win.png"), ("lose", "art/lose.png"))


def load_splash(path):
    """Decode a 160x144 4-colour PNG -> (tiles, map).

    `tiles` is a list of 8-row strings using '0'..'3' (encode() understands
    those directly); `map` is 360 local tile indices in row-major order, so
    main.c can blit the whole screen with one set_bkg_tiles and only the
    distinct tiles ever reach VRAM.
    """
    im = Image.open(path).convert("RGB")
    px = im.load()
    seen, order, mp = {}, [], []
    for ty in range(im.size[1] // 8):
        for tx in range(im.size[0] // 8):
            key = tuple(px[tx * 8 + x, ty * 8 + y] for y in range(8) for x in range(8))
            if key not in seen:
                seen[key] = len(order)
                order.append(["".join("0123"[DMG_RGB[key[y * 8 + x]]] for x in range(8))
                              for y in range(8)])
            mp.append(seen[key])
    return order, mp


SHAPES = {
  # empty board cell, grid lines added below
  'CELL': ["........"] * 8,
  # same cell, but with a 3px GUTTER hard against its left edge. Used only for
  # column 0 of each board so the two boards are visibly separated.
  # (160px is exactly 20 tiles wide, so there is NO spare tile column for a gap
  #  -- the gutter has to be carved out of the cell art itself.)
  'CELLG': ["........"] * 8,
  # light filled box with a dark outline -> "your ship"
  'SHIP': ["########",
           "#222222#",
           "#222222#",
           "#222222#",
           "#222222#",
           "#222222#",
           "#222222#",
           "########"],
  # bold X -> confirmed hit
  'HIT':  ["##....##",
           ".##..##.",
           "..####..",
           "...##...",
           "..####..",
           ".##..##.",
           "##....##",
           "........"],
  # small centred dot -> miss
  'MISS': ["........",
           "........",
           "...##...",
           "...##...",
           "........",
           "........",
           "........",
           "........"],
  # legend swatches: same art WITHOUT grid hairlines
  'LEG_SHIP': ["########",
               "#222222#",
               "#222222#",
               "#222222#",
               "#222222#",
               "#222222#",
               "#222222#",
               "########"],
  'LEG_HIT':  ["##....##",
               ".##..##.",
               "..####..",
               "...##...",
               "..####..",
               ".##..##.",
               "##....##",
               "........"],
  'LEG_MISS': ["........",
               "........",
               "...##...",
               "...##...",
               "........",
               "........",
               "........",
               "........"],
  # solid block, no grid lines -> "you cannot place here" during manual placement.
  # Deliberately a flat dark fill so it reads as a void/blocked area, clearly
  # different from the grey ship preview drawn for a legal position.
  'BAD': ["33333333"] * 8,
  # solid block, used as one "pixel" of the big game-over lettering. Separate
  # from BAD on purpose: same art, but the name says what it is for, so the
  # big-font code does not read as if it were drawing "bad" cells.
  'SOLID': ["33333333"] * 8,
  # hollow box, transparent interior -> cursor (drawn as a sprite)
  'CURSOR': ["########",
             "#......#",
             "#......#",
             "#......#",
             "#......#",
             "#......#",
             "#......#",
             "########"],
}

def pad(rows):
    rows = list(rows)[:8]
    while len(rows) < 8:
        rows.append(".....")
    return [r.ljust(8, ".") for r in rows]

def encode(rows):
    out = []
    for r in pad(rows):
        lo = hi = 0
        for i, ch in enumerate(r):
            v = {'#': 3, '+': 1, '*': 2, '.': 0, '0': 0, '1': 1, '2': 2, '3': 3}[ch]
            bit = 7 - i
            if v & 1: lo |= (1 << bit)
            if v & 2: hi |= (1 << bit)
        out += [lo, hi]
    return out

def carr(name, data, per=12):
    s = "static const uint8_t %s[] = {\n" % name
    for i in range(0, len(data), per):
        s += "    " + ",".join("0x%02X" % b for b in data[i:i+per]) + ",\n"
    return s[:-2] + "\n};\n"

def main():
    g = build_font()
    order = sorted(k for k in g if k != " ")
    tiles = [encode(g[" "])]
    font_map = [0] * 128
    for ch in order:
        font_map[ord(ch)] = len(tiles)
        tiles.append(encode(g[ch]))
    shape = {}
    for name in ['CELL', 'MISS', 'HIT', 'SHIP', 'LEG_SHIP', 'LEG_HIT', 'LEG_MISS', 'BAD', 'SOLID']:
        shape[name] = len(tiles)
        rows = SHAPES[name]
        if name in ('CELL', 'MISS', 'HIT'):
            rows = add_grid_lines(rows)
        tiles.append(encode(rows))
        # Board cells also get three EDGE variants so the board's outer left/top
        # edge can be drawn without doubling any interior line:
        #   <name>L  = outer left edge only   (board column 0)
        #   <name>T  = outer top edge only    (board row 0)
        #   <name>TL = both                   (corner cell 0,0)
        # A single "full box" variant is NOT enough: it duplicates the line the
        # neighbouring tile already draws. LEG_* swatches carry no grid lines, so
        # they get no variants.
        if name in ('CELL', 'MISS', 'HIT', 'SHIP'):
            for suf, lft, tp in (('L', True, False), ('T', False, True), ('TL', True, True)):
                shape[name + suf] = len(tiles)
                tiles.append(encode(add_edges(SHAPES[name], left=lft, top=tp)))
    # Fleet-panel ship icons: two tiles per ship (a 16x8 icon split into an L and
    # an R tile), afloat + sunk -> 4 tiles per ship, 28 total.
    # SHIP_ICON_BASE is a runtime-indexable block: main.c picks ship s with
    # SHIP_ICON_BASE + 4*s (+2 when sunk), drawing the L tile then the R tile.
    icon_base = len(tiles)
    for (ln, pips) in FLEET_SPEC:
        for sunk in (False, True):
            l, r = split_wide(ship_icon(ln, pips, sunk))
            tiles.append(encode(l))
            tiles.append(encode(r))
    # --- game-over splash screens: encode each images's own tileset + map ----
    splash = {}
    for tag, path in SPLASH_SRC:
        if not os.path.exists(path):
            raise SystemExit("missing splash art: %s" % path)
        order_s, mp = load_splash(path)
        splash[tag] = (order_s, mp)

    flat = [b for t in tiles for b in t]
    h  = "/* AUTO-GENERATED by mkgfx.py -- do not hand-edit */\n"
    h += "#ifndef GFX_H\n#define GFX_H\n#include <stdint.h>\n\n"
    h += "#define FONT_BASE  1\n#define FONT_COUNT %d\n" % len(order)
    h += "#define TILE_CELL  %d\n#define TILE_MISS  %d\n" % (shape['CELL'], shape['MISS'])
    h += "#define TILE_HIT   %d\n#define TILE_SHIP  %d\n" % (shape['HIT'], shape['SHIP'])
    h += "/* edge variants: L=outer left edge (col 0), T=outer top edge (row 0),\n"
    h += "   TL=both (corner cell 0,0). Applied on top of the right/bottom hairlines. */\n"
    for base in ('CELL', 'MISS', 'HIT', 'SHIP'):
        for suf in ('L', 'T', 'TL'):
            h += "#define TILE_%s%s %d\n" % (base, suf, shape[base + suf])
    h += "#define TILE_BLANK 0\n"
    h += "/* fleet-panel ship icons: 2 tiles per ship (L then R).\n"
    h += "   afloat L tile = SHIP_ICON_BASE + 4*s, sunk L tile = +2. */\n"
    h += "#define SHIP_ICON_BASE %d\n" % icon_base
    h += "#define SHIP_ICON_SUNK 2\n"
    h += "#define TILE_LEG_SHIP %d\n#define TILE_LEG_HIT %d\n#define TILE_LEG_MISS %d\n" % (
        shape['LEG_SHIP'], shape['LEG_HIT'], shape['LEG_MISS'])
    h += "#define TILE_BAD %d\n" % shape['BAD']
    h += "#define TILE_SOLID %d\n" % shape['SOLID']
    h += "#define GFX_TILE_COUNT %d\n\n" % len(tiles)
    for tag, (order_s, mp) in splash.items():
        st = [b for t in order_s for b in encode(t)]
        h += "/* splash '%s': %d unique tiles (loads over the game tiles) */\n" % (tag, len(order_s))
        h += "#define SPLASH_%s_TILES %d\n" % (tag.upper(), len(order_s))
        h += carr("splash_%s_tiles" % tag, st) + "\n"
        h += carr("splash_%s_map" % tag, mp, per=20) + "\n"
    h += carr("gfx_tiles", flat) + "\n"
    h += carr("sprite_tiles", encode(SHAPES['CURSOR'])) + "\n"
    h += carr("font_map", font_map, per=16) + "\n#endif\n"
    open("gfx.h", "w").write(h)
    for tag, (order_s, mp) in splash.items():
        print("splash %-4s %3d tiles, %d bytes" % (tag, len(order_s), len(order_s) * 16))
    print("glyphs %d, tiles %d, CELL %d MISS %d HIT %d SHIP %d | edges CELL L%d T%d TL%d"
          % (len(order), len(tiles), shape['CELL'], shape['MISS'], shape['HIT'], shape['SHIP'],
             shape['CELLL'], shape['CELLT'], shape['CELLTL']))

if __name__ == "__main__":
    main()
