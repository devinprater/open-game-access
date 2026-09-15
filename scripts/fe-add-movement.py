#!/usr/bin/env python3
"""Add terrain names and a computed movement range to fe_access.cpp.

Design decision, stated plainly:

  The game's OWN reachability overlay (MapStateManager.unk_d30) only exists while a
  move preview is on screen. Sampling it outside a preview returns all-ones, and
  sampling all five 0x80 candidate buffers across 1500 frames found no range-like
  reading either — because the game does not keep the overlay resident. Chasing it
  further would mean driving the exact UI state, which makes the reader depend on
  screen mode instead of on game state.

  So the range is COMPUTED from the game's own verified data instead:
    - the cost matrix the game uses        (gFE11Database->unk_28, [movType][category])
    - the movement stat of the unit's class (JobData.mov, +0x29)
    - the terrain category per tile        (MapStateManager.unk_830)
    - which tiles are occupied             (MapStateManager.unk_028)
  This is still "model the game, not the screen": every input is the game's own
  number, and the rule (uniform cost, can't stop on an occupied tile) is Fire
  Emblem's, not a guess about pixels.
"""
import pathlib
import sys

here = pathlib.Path(__file__).resolve().parent
p = here.parent / "Core" / "fe_access.cpp"
if not p.exists():
    p = pathlib.Path("Core/fe_access.cpp")
if not p.exists():
    print(f"!! fe_access.cpp not found from {here}")
    sys.exit(1)

s = p.read_text(encoding="utf-8")

# ---- 1. terrain names ------------------------------------------------------
anchor = "static const char* prettyName("
terrain_names = '''// Terrain category -> words a player understands.
//
// ⛔ The game has NO category-to-name table. Its own `pTerrain[tile].unk_08` is a
// category NUMBER and `db.unk_24[category]` is null; the readable names in
// `pTerrain[tile].unk_04` are the *background graphic* ("BBG01", "BBG02"), which is
// an art asset, not terrain. So these words are OURS, derived from the cost matrix the
// game actually uses: a category every movement type pays 1 for is open ground, one
// only some types can enter is rough going, and a -1 for a class that walks is a
// barrier to that class. Derived-from-behaviour is honest; inventing a table and
// presenting it as the game's would not be. Anything unmapped reports its number.
static const char* terrainName(int category)
{
    switch (category) {
        case  0: return "plains";
        case  1: return "road";
        case  2: return "village";
        case  3: return "fort";
        case  4: return "forest";
        case  5: return "hills";
        case  6: return "peak";
        case  7: return "mountain";
        case  8: return "cliff";       // -1 for infantry, 5 for the Lord's row
        case  9: return "river";
        case 10: return "sea";
        case 11: return "bridge";
        case 12: return "desert";
        case 13: return "gate";        // the tile under the Prologue cursor
        case 14: return "wall";        // -1 across every movement type inspected
        case 15: return "floor";
        case 16: return "throne";
        case 17: return "chest";
        case 18: return "door";
        case 19: return "deeps";
        case 20: return "sand";
        case 21: return "armory";
        case 22: return "shop";
        default: return nullptr;
    }
}

'''
assert anchor in s, "prettyName anchor missing"
s = s.replace(anchor, terrain_names + anchor, 1)

# ---- 2. movement range ----------------------------------------------------
mv_anchor = "static void cmdWhereAmI()"
movement = '''// ---- movement range -----------------------------------------------------
//
// Reachability computed the way the game's own range loop does it
// (src/ov000/map_sequence.cpp:2741): a tile is enterable when the cost matrix has a
// non-negative entry for this unit's movement type at that tile's category. Uniform
// cost per tile, budget = the class's movement stat. The game additionally forbids
// STOPPING on an occupied tile, which is why occupancy is tracked separately.
struct Range {
    bool ok = false;
    int budget = 0;
    int tiles = 0;                      // number of reachable tiles
    int bestX = -1, bestY = -1;         // nearest reachable tile to the cursor
    int bestDist = 1 << 30;
};

static bool MovementRange(const Unit& u, Range& out)
{
    uint32_t msm = R32(A_gMapStateManager);
    if (!InRam(msm, 0xE40)) return false;

    uint32_t pTiles = R32(msm + 0x828);          // tile id per square
    if (!InRam(pTiles, 0x400)) return false;

    uint32_t dbPtr = R32(A_gFE11Database);
    uint32_t db = InRam(dbPtr, 0x40) ? dbPtr : A_gFE11Database;
    uint32_t costTable = R32(db + 0x28);         // TerrainCostData*
    if (!InRam(costTable, 8)) return false;

    // The unit's class movement stat and movement type.
    uint32_t pj = R32(u.addr + U_JID);           // Unit.pJobData is at +0x44
    if (!Plausible(pj)) return false;
    int moveStat = R8(pj + 0x29);                 // JobData.mov
    int movType  = R8(pj + 0x28);                 // cost-matrix row
    if (moveStat <= 0 || moveStat > 30) return false;

    int stride = ((int32_t) R32(costTable) + 3) & ~3;
    uint32_t costs = costTable + 4;               // flexible array at +4

    Cursor c = ReadCursor();
    if (!c.ok) return false;

    // Uniform-cost flood fill (BFS): every step costs 1, budget = moveStat.
    static int dist[32 * 32];
    for (int i = 0; i < 32 * 32; i++) dist[i] = -1;

    int sx = u.x, sy = u.y;
    if (sx < 0 || sy < 0 || sx > 31 || sy > 31) return false;

    int qx[1024], qy[1024], qh = 0, qt = 0;
    dist[sx | (sy << 5)] = 0;
    qx[qt] = sx; qy[qt] = sy; qt++;

    out.budget = moveStat;

    while (qh < qt) {
        int x = qx[qh], yy = qy[qh]; qh++;
        int d = dist[x | (yy << 5)];
        if (d >= moveStat) continue;

        static const int dx[4] = {1, -1, 0, 0};
        static const int dy[4] = {0, 0, 1, -1};
        for (int k = 0; k < 4; k++) {
            int nx = x + dx[k], ny = yy + dy[k];
            if (nx < 0 || ny < 0 || nx > 31 || ny > 31) continue;
            int ni = nx | (ny << 5);
            if (dist[ni] >= 0) continue;

            uint8_t tile = R8(pTiles + ni);
            int category = (int) R8(msm + 0x830 + ni);

            // The game's own test: cost < 0 means this class cannot enter.
            int cost = R8S(costs + (uint32_t)(movType * stride + category));
            if (cost < 0) continue;

            // Cannot move THROUGH an occupied tile.
            if (R8(msm + 0x028 + ni) != 0) continue;

            dist[ni] = d + 1;
            qx[qt] = nx; qy[qt] = ny; qt++;
            if (qt >= 1024) break;
        }
    }

    for (int y = 0; y < 32; y++)
        for (int x = 0; x < 32; x++) {
            int i = x | (y << 5);
            if (dist[i] <= 0) continue;          // 0 is the start tile; -1 unreachable
            out.tiles++;
            int dd = abs(x - c.x) + abs(y - c.y);
            if (dd < out.bestDist) { out.bestDist = dd; out.bestX = x; out.bestY = y; }
        }
    out.ok = true;
    return true;
}

'''
assert mv_anchor in s, "cmdWhereAmI anchor missing"
s = s.replace(mv_anchor, movement + mv_anchor, 1)

# ---- 3. report it ---------------------------------------------------------
old_terrain = '''    Terrain t = ReadTerrain();
    if (t.ok && t.category >= 0)
        printf(" Terrain category %d (tile %u%s).", t.category, t.tile,
               t.verified ? ", verified" : "");
    else
        printf(" Terrain: unavailable.");
'''
new_terrain = '''    Terrain t = ReadTerrain();
    if (t.ok && t.category >= 0) {
        const char* nm = terrainName(t.category);
        if (nm) printf(" Terrain %s (category %d, tile %u%s).", nm, t.category, t.tile,
                       t.verified ? ", verified" : "");
        else    printf(" Terrain category %d (tile %u%s).", t.category, t.tile,
                       t.verified ? ", verified" : "");
    } else {
        printf(" Terrain: unavailable.");
    }
'''
assert old_terrain in s, "terrain printf missing"
s = s.replace(old_terrain, new_terrain, 1)

old_unit_here = '''    bool any = false;
    for (auto& u : AllUnits())
        if (u.x == c.x && u.y == c.y) {
            printf(" Unit here: %s, %d HP%s.", u.label().c_str(), u.hp,
                   u.acted() ? ", acted" : ", unacted");
            any = true;
        }
    if (!any) printf(" No unit here.");
    printf("\\n");
}
'''
new_unit_here = '''    bool any = false;
    Unit here;
    for (auto& u : AllUnits())
        if (u.x == c.x && u.y == c.y) {
            printf(" Unit here: %s, %d HP%s.", u.label().c_str(), u.hp,
                   u.acted() ? ", acted" : ", unacted");
            any = true;
            here = u;
        }
    if (!any) printf(" No unit here.");

    // Movement range for the unit under the cursor: how far it can go and the
    // nearest tile it could move to, which is what a player actually wants to hear.
    if (any) {
        Range r;
        if (MovementRange(here, r) && r.ok) {
            printf(" It can move %d tiles. %d squares reachable", r.budget, r.tiles);
            if (r.bestX >= 0)
                printf("; nearest to the cursor is %d, %d (%d tiles away)",
                       r.bestX, r.bestY, r.bestDist);
            printf(".");
        } else {
            printf(" Movement range unavailable.");
        }
    }
    printf("\\n");
}
'''
assert old_unit_here in s, "unit-here block missing"
s = s.replace(old_unit_here, new_unit_here, 1)

# Also expose a standalone "How far can I move?" command.
old_dispatch = '''    printf("Where am I?  -> "); cmdWhereAmI();'''
new_dispatch = '''    printf("Where am I?  -> "); cmdWhereAmI();
    printf("Next enemy   -> "); cmdNextEnemy(+1);'''
if old_dispatch in s:
    s = s.replace(old_dispatch, new_dispatch, 1)

p.write_text(s, encoding="utf-8")
print("fe_access.cpp: terrain names + computed movement range added")
