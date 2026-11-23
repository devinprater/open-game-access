/*
 * alttp_graph.h — the overworld walking graph, generated data.
 *
 * Per screen (0..127; 8x8 cells per world, light first), the area that owns
 * it and the border gaps where Link can walk — or drop — into the next
 * screen. Derived offline from the game's own tile assembly by
 * scripts/gen-alttp-graph.py; no tile data ships, only the gaps.
 */
#ifndef OGA_ALTTP_GRAPH_H
#define OGA_ALTTP_GRAPH_H

#include <stdint.h>

namespace oga {

struct AlttpGap {
    uint8_t side;      /* 0 north, 1 south, 2 west, 3 east */
    uint8_t third;     /* which third of the border the gap sits in: 0, 1, 2 */
    uint8_t region;    /* this screen's walkable region at the gap */
    uint8_t toRegion;  /* the neighbour screen's region at the gap */
    int8_t  dScreen;   /* neighbour screen delta: -8, +8, -1, +1 */
    uint8_t ledge;     /* 1: a one-way ledge drop in the direction of travel */
};

/* The area id that owns a screen (a big area owns four screens). 0..127. */
uint8_t AlttpAreaOfScreen(uint8_t screen);

/* The screen's gaps; returns the count and sets *out (nullptr when none). */
int AlttpScreenGaps(uint8_t screen, const AlttpGap** out);

/* How many walkable regions the screen's tiles fall into. Region ids in
 * the gap table (and in a live flood fill done the same way: row-major
 * scan, four-connectivity) run 0..count-1. */
uint8_t AlttpScreenRegionCount(uint8_t screen);

/* Whether a map16 tile id is walkable ground for Link: any of its four
 * map8 subtiles carries a walk-on behaviour. The adapter flood-fills the
 * live screen from the game's own tile-attribute RAM with this table, so
 * the region it stands in is measured, not tracked. */
bool AlttpMap16Walkable(uint16_t map16);

/* Whether a map16 tile id is STEERABLE ground for auto walk
 * (docs/design/auto-walk.md SS5): the collision-shrunk form of the
 * table above. ALL four subtiles must carry a walk-on behaviour (a
 * tile walkable on one subtile only is a wall the walker would grind
 * on), and no subtile may be a spike floor, a warp tile or an entrance
 * threshold — the walker stops AT doors and warps, never on them. */
bool AlttpMap16Steerable(uint16_t map16);

/* Whether a map16 tile id is PASSABLE ground for auto walk's
 * fallback plans: at least three of its four subtiles walk-on, and
 * still no spike, warp or entrance subtile. Used only when no
 * steerable path exists on a screen (stairs and cliff paths). */
bool AlttpMap16Passable(uint16_t map16);

} // namespace oga

#endif
