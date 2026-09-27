#!/usr/bin/env python3
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
    for name in ['CELL', 'MISS', 'HIT', 'SHIP', 'LEG_SHIP', 'LEG_HIT', 'LEG_MISS']:
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
    h += "#define GFX_TILE_COUNT %d\n\n" % len(tiles)
    h += carr("gfx_tiles", flat) + "\n"
    h += carr("sprite_tiles", encode(SHAPES['CURSOR'])) + "\n"
    h += carr("font_map", font_map, per=16) + "\n#endif\n"
    open("gfx.h", "w").write(h)
    print("glyphs %d, tiles %d, CELL %d MISS %d HIT %d SHIP %d | edges CELL L%d T%d TL%d"
          % (len(order), len(tiles), shape['CELL'], shape['MISS'], shape['HIT'], shape['SHIP'],
             shape['CELLL'], shape['CELLT'], shape['CELLTL']))

if __name__ == "__main__":
    main()
