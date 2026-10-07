local M = {}

-- ============================================================
-- WORLDMAP.LUA
-- Pure data module for Zelda 1 overworld.
-- Owns: baked room entries (special exits + cave/event placements),
-- unfindable rooms list, and lazy-loaded ROM-derived data
-- (overworld enemy placements + per-screen manifest).
-- Exit scanning is live via Navigation.lua tile grid -- no cache needed.
-- ============================================================

-- ============================================================
-- BAKED ROOM ENTRIES
-- Single source of truth for overworld cave/event placements and
-- special exit layouts. Keyed by "00:RR" room key.
--
-- Each entry has a name (debug-friendly) and an entities list. Each
-- entity has type_name + label + x/y position + approach_x/approach_y
-- (the tile BFS routes to). Entity types:
--   "cave"     -- generic cave/event placeholder. Suppressed by the
--                 live tile scan when a 0x24 cave-mouth or 0x70
--                 stair tile appears on the screen.
--   "stairs"   -- specifically a stair-revealed entity (used by
--                 dual-entry screens like L1). Suppressed when
--                 the live scan finds 0x70.
--   "landmark" -- non-cave marker (false walls, lake drains,
--                 docks). NEVER suppressed; coexists with live
--                 tile-scan entities.
--   "exit"     -- forced exit position, used for split exits
--                 (river crossings, divided room layouts).
--
-- Label convention:
--   "Cave"     -- baked fallback for visible/open caves AND live
--                 label emitted by the 0x24 cave-mouth scan. Both
--                 paths converge on the same word so the player
--                 hears consistent terminology regardless of
--                 whether the live scan fires (it can miss screens
--                 with non-standard tile signatures, e.g. K2's
--                 mountain-style 89/8B/88 cave entrance).
--   "Stairs"   -- live label for 0x70 stair tile detection. Baked
--                 "Event" entries get replaced by this once the
--                 reveal mechanic fires.
--   "Event"    -- baked placeholder for hidden caves before reveal.
--   custom     -- specific labels like "Health Recharge", "Dock",
--                 "Lake Drain" for screens with non-cave events.
-- ============================================================
local BAKED_ROOM_ENTRIES = {
    -- =========================================================
    -- ROW 1 (formerly old row A) -- baked from user-walked Q1 chart
    -- =========================================================

    -- B1 (0x01) -- Door Repair Cave, bomb reveal at (18, 3).
    ["00:01"] = {
        name = "Overworld B1",
        entities = {
            { type_name="cave", label="Event", x=18, y=3, approach_x=18, approach_y=3 },
        },
    },
    -- D1 (0x03) -- Door Repair Cave, bomb reveal at (13, 9).
    ["00:03"] = {
        name = "Overworld D1",
        entities = {
            { type_name="cave", label="Event", x=13, y=9, approach_x=13, approach_y=9 },
        },
    },
    -- E1 (0x04) -- Potion Shop, open entrance at (24, 3).
    ["00:04"] = {
        name = "Overworld E1",
        entities = {
            { type_name="cave", label="Cave", x=24, y=3, approach_x=24, approach_y=3 },
        },
    },
    -- F1 (0x05) -- Dungeon 9 entrance via bomb reveal at (10, 13).
    -- Per user, D9 has no physical entrance; it's a bombable event.
    -- Labeled "Event" per the visible/hidden convention.
    ["00:05"] = {
        name = "Overworld F1",
        entities = {
            { type_name="cave", label="Event", x=10, y=13, approach_x=10, approach_y=13 },
        },
    },
    -- H1 (0x07) -- Door Repair Cave, bomb reveal at (20, 9).
    ["00:07"] = {
        name = "Overworld H1",
        entities = {
            { type_name="cave", label="Event", x=20, y=9, approach_x=20, approach_y=9 },
        },
    },
    -- K1 (0x0A) -- White Sword Cave, open entrance at (4, 3).
    ["00:0A"] = {
        name = "Overworld K1",
        entities = {
            { type_name="cave", label="Cave", x=4, y=3, approach_x=4, approach_y=3 },
        },
    },
    -- M1 (0x0C) -- Bomb Shop, open entrance at (16, 3).
    ["00:0C"] = {
        name = "Overworld M1",
        entities = {
            { type_name="cave", label="Cave", x=16, y=3, approach_x=16, approach_y=3 },
        },
    },
    -- O1 (0x0E) -- Letter Cave, open entrance at (10, 9).
    ["00:0E"] = {
        name = "Overworld O1",
        entities = {
            { type_name="cave", label="Cave", x=10, y=9, approach_x=10, approach_y=9 },
        },
    },
    -- P1 (0x0F) -- 100 Secret Rupee Cave, open entrance at (16, 9).
    ["00:0F"] = {
        name = "Overworld P1",
        entities = {
            { type_name="cave", label="Cave", x=16, y=9, approach_x=16, approach_y=9 },
        },
    },

    -- =========================================================
    -- ROW 2 (formerly old row B) -- baked from user-walked Q1 chart
    -- =========================================================

    -- A2 (0x10) -- Gambling Cave, bomb reveal at (18, 3).
    ["00:10"] = {
        name = "Overworld A2",
        entities = {
            { type_name="cave", label="Event", x=18, y=3, approach_x=18, approach_y=3 },
        },
    },
    -- C2 (0x12) -- Bait Shop, bomb reveal at (16, 3).
    ["00:12"] = {
        name = "Overworld C2",
        entities = {
            { type_name="cave", label="Event", x=16, y=3, approach_x=16, approach_y=3 },
        },
    },
    -- D2 (0x13) -- Secret Rupee Cave, bomb reveal at (4, 3).
    ["00:13"] = {
        name = "Overworld D2",
        entities = {
            { type_name="cave", label="Event", x=4, y=3, approach_x=4, approach_y=3 },
        },
    },
    -- E2 (0x14) -- Door Repair Cave, bomb reveal at (24, 3).
    ["00:14"] = {
        name = "Overworld E2",
        entities = {
            { type_name="cave", label="Event", x=24, y=3, approach_x=24, approach_y=3 },
        },
    },
    -- G2 (0x16) -- Gambling Cave, bomb reveal at (12, 3).
    ["00:16"] = {
        name = "Overworld G2",
        entities = {
            { type_name="cave", label="Event", x=12, y=3, approach_x=12, approach_y=3 },
        },
    },
    -- K2 (0x1A) -- Information Cave, open entrance at (12, 9).
    -- The K2 cave entrance uses tile bytes 0x89/0x8B/0x88 (mountain-
    -- style entrance), not the standard 0x24 mouth or 0x70 stair --
    -- so the live tile scan misses it and this baked entry is the
    -- only emitter. Label "Cave" matches what the live scan would
    -- have said if it could see this signature.
    ["00:1A"] = {
        name = "Overworld K2",
        entities = {
            { type_name="cave", label="Cave", x=12, y=9, approach_x=12, approach_y=9 },
        },
    },
    -- N2 (0x1D) -- Any Road Cave, push rock reveal at (6, 11). One
    -- of the four Take Any Road caves; entrance is hidden behind a
    -- pushable rock so labeled "Event". Same mechanic as J5 and D3.
    -- After the rock is pushed, the live tile scan picks up the 0x70
    -- stair cluster and emits a live Stairs entity that supersedes
    -- this baked Event.
    ["00:1D"] = {
        name = "Overworld N2",
        entities = {
            { type_name="cave", label="Event", x=6, y=11, approach_x=6, approach_y=11 },
        },
    },
    -- O2 (0x1E) -- Door Repair Cave, bomb reveal at (24, 3).
    ["00:1E"] = {
        name = "Overworld O2",
        entities = {
            { type_name="cave", label="Event", x=24, y=3, approach_x=24, approach_y=3 },
        },
    },

    -- =========================================================
    -- EXISTING ENTRIES (rows 3-8)
    -- =========================================================

    -- D5 (0x34) -- armos touch, Blue Ring Shop (loc_idx 32). Was
    -- previously labeled burn; corrected to armos per live verification.
    ["00:34"] = {
        name = "Overworld D5",
        entities = {
            { type_name="cave", label="Event", x=8, y=9, approach_x=8, approach_y=9 },
        },
    },
    -- D4 (0x33) -- bomb wall, Potion Shop. Hidden cave at (20, 3),
    -- labeled "Event" per the visible/hidden convention.
    ["00:33"] = {
        name = "Overworld D4",
        entities = {
            { type_name="cave", label="Event", x=20, y=3, approach_x=20, approach_y=3 },
        },
    },
    -- D8 (0x37) -- open Dungeon 1 entrance at (14, 9). Labeled just
    -- "Cave" per the visible/open convention -- the dungeon's
    -- identity is part of what the player discovers by walking in.
    ["00:37"] = {
        name = "Overworld D8",
        entities = {
            { type_name="cave", label="Cave", x=14, y=9, approach_x=14, approach_y=9 },
        },
    },
    -- D10 (0x39) -- Health Recharge Event. Special non-cave event
    -- like E4: a fairy heals Link to full hearts. Same custom label
    -- as E4 since the player benefits from knowing what the screen
    -- does.
    ["00:39"] = {
        name = "Overworld D10",
        entities = {
            { type_name="cave", label="Health Recharge", x=15, y=15, approach_x=15, approach_y=15 },
        },
    },
    -- D13 (0x3C) -- open Dungeon 2 entrance at (14, 9). Labeled just
    -- "Cave" per the visible/open convention.
    ["00:3C"] = {
        name = "Overworld D13",
        entities = {
            { type_name="cave", label="Cave", x=14, y=9, approach_x=14, approach_y=9 },
        },
    },
    -- D16 (0x3F) -- Dock. Raft launch point at (12, 9). Custom
    -- label "Dock" so the player knows what the screen does.
    -- Same special-screen pattern as E3 (Lake Drain) and E4
    -- (Health Recharge) -- counted as verified_extra so it
    -- doesn't bump the secret/entrance totals.
    ["00:3F"] = {
        name = "Overworld D16",
        entities = {
            { type_name="cave", label="Dock", x=12, y=9, approach_x=12, approach_y=9 },
        },
    },
    -- F16 (0x5F) -- Heart Container reachable via stepladder dock-hop
    -- across single-tile water gaps between floating dock pieces. The
    -- screen is in stepladder_screens ROM table (0x1F20D). Hardcoded
    -- in the disassembly (MakeHeartContainerOW) to drop a heart
    -- container at pixel (0xC0, 0x90) which is tile (24, 7) but the
    -- player approaches it from (16, 11) -- that's where the dock
    -- chain begins. Labeled "Event" since it's a hidden-style reward
    -- screen, not a cave entrance.
    ["00:5F"] = {
        name = "Overworld F16",
        entities = {
            { type_name="cave", label="Event", x=16, y=11, approach_x=16, approach_y=11 },
        },
    },
    -- F2 (0x51) -- burn, 10 Secret Rupee Cave
    ["00:51"] = {
        name = "Overworld F2",
        entities = {
            { type_name="cave", label="Event", x=18, y=13, approach_x=18, approach_y=13 },
        },
    },
    -- D6 (0x53) entry removed -- the 100 Secret Rupee Cave burn event
    -- at (4, 6) was a leftover/incorrect entry, not part of actual Q1
    -- logic. Removed from CAVE_ROOMS, CAVE_ENTRANCE_TILES,
    -- BAKED_ROOM_ENTRIES, and the OVERWORLD_REVEAL_ROOMS list in
    -- Navigation.lua.
    -- F12 (0x5B) -- burn, 10 Secret Rupee Cave + split east exits
    -- (room east is divided into NE and SE areas with separate openings).
    -- Exit data preserved per the no-touch-exits rule.
    ["00:5B"] = {
        name = "Overworld F12",
        entities = {
            { type_name="cave", label="Event", x=4, y=13, approach_x=4, approach_y=13 },
            { type_name="exit", label="Northeast exit", x=31, y=5,  approach_x=30, approach_y=5,  exit_direction="East", fixed_approach=true },
            { type_name="exit", label="Southeast exit", x=31, y=17, approach_x=30, approach_y=17, exit_direction="East", fixed_approach=true },
        },
    },
    -- G3 (0x62) -- burn, 100 Secret Rupee Cave
    ["00:62"] = {
        name = "Overworld G3",
        entities = {
            { type_name="cave", label="Event", x=16, y=5, approach_x=16, approach_y=5 },
        },
    },
    -- G12 (0x6B) -- burn, 100 Secret Rupee Cave
    ["00:6B"] = {
        name = "Overworld G12",
        entities = {
            { type_name="cave", label="Event", x=16, y=13, approach_x=16, approach_y=13 },
        },
    },
    -- F7 (0x56) -- burn, 10 Secret Rupee Cave
    ["00:56"] = {
        name = "Overworld F7",
        entities = {
            { type_name="cave", label="Event", x=20, y=13, approach_x=20, approach_y=13 },
        },
    },
    -- F15 (0x5E) -- open Blue Candle Shop
    ["00:5E"] = {
        name = "Overworld F15",
        entities = {
            { type_name="cave", label="Cave", x=14, y=3, approach_x=14, approach_y=3 },
        },
    },
    -- E7 (0x46) -- burn, Cheap Shield Shop
    ["00:46"] = {
        name = "Overworld E7",
        entities = {
            { type_name="cave", label="Event", x=18, y=15, approach_x=18, approach_y=15 },
        },
    },
    -- E8 (0x47) -- burn, Heart or Potion Cave
    ["00:47"] = {
        name = "Overworld E8",
        entities = {
            { type_name="cave", label="Event", x=22, y=15, approach_x=22, approach_y=15 },
        },
    },
    -- E9 (0x48) -- burn, 30 Secret Rupee Cave
    ["00:48"] = {
        name = "Overworld E9",
        entities = {
            { type_name="cave", label="Event", x=26, y=5, approach_x=26, approach_y=5 },
        },
    },
    -- C9 (0x28) -- burn secret cave
    ["00:28"] = {
        name = "Overworld C9",
        entities = {
            { type_name="cave", label="Event", x=26, y=13, approach_x=26, approach_y=13 },
        },
    },
    -- E14 (0x4D) -- burn, Cheap Shield Shop
    ["00:4D"] = {
        name = "Overworld E14",
        entities = {
            { type_name="cave", label="Event", x=26, y=13, approach_x=26, approach_y=13 },
        },
    },
    -- G14 (0x6D) -- burn tree, Dungeon 8 entrance -- confirmed.
    -- Labeled "Event" rather than "Dungeon 8" so the player has to walk
    -- in to discover what's there -- preserves the same mystery a sighted
    -- player would experience.
    ["00:6D"] = {
        name = "Overworld G14",
        entities = {
            { type_name="cave", label="Event", x=20, y=5, approach_x=20, approach_y=5 },
        },
    },
    -- G13 (0x6C) entry removed -- the alt-route burn tree was a leftover
    -- game mechanic, not part of actual Q1 logic. Removed from
    -- CAVE_ROOMS, CAVE_ENTRANCE_TILES, BAKED_ROOM_ENTRIES, and the
    -- OVERWORLD_REVEAL_ROOMS list in Navigation.lua.
    -- H13 (0x7C) -- bomb wall, Gambling Cave (loc_idx 22) -- confirmed.
    -- Hidden cave, labeled "Event" per the visible/hidden convention.
    ["00:7C"] = {
        name = "Overworld H13",
        entities = {
            { type_name="cave", label="Event", x=12, y=3, approach_x=12, approach_y=3 },
        },
    },
    -- H14 (0x7D) -- bomb wall, Door Repair Cave -- confirmed.
    -- Hidden cave, labeled "Event" per the visible/hidden convention.
    ["00:7D"] = {
        name = "Overworld H14",
        entities = {
            { type_name="cave", label="Event", x=12, y=3, approach_x=12, approach_y=3 },
        },
    },
    -- G11 (0x6A) -- Door Repair Cave (loc 23) -- confirmed.
    -- Hidden cave, labeled "Event" per the visible/hidden convention.
    ["00:6A"] = {
        name = "Overworld G11",
        entities = {
            { type_name="cave", label="Event", x=24, y=13, approach_x=24, approach_y=13 },
        },
    },
    -- C13 (0x2C) -- bomb secret rupee cave, two north exits
    ["00:2C"] = {
        name = "Overworld C13",
        entities = {
            { type_name="cave",  label="Event",              x=18, y=13, approach_x=18, approach_y=13 },
            { type_name="exit",  label="Northwest exit",     x=6,  y=0,  approach_x=6,  approach_y=1,  exit_direction="North", fixed_approach=true },
            { type_name="exit",  label="Northeast exit",     x=22, y=0,  approach_x=22, approach_y=1,  exit_direction="North", fixed_approach=true },
        },
    },
    -- A14 (0x0D) -- bomb wall potion shop. Bombable reveal at (18, 3).
    ["00:0D"] = {
        name = "Overworld N1",
        entities = {
            { type_name="cave", label="Event", x=18, y=3, approach_x=18, approach_y=3 },
        },
    },
    -- C14 (0x2D) -- bomb secret rupee cave, two north exits (room above is split)
    ["00:2D"] = {
        name = "Overworld C14",
        entities = {
            { type_name="cave", label="Event",          x=10, y=3,  approach_x=10, approach_y=3  },
            { type_name="exit", label="Western North exit", x=5,  y=0,  approach_x=5,  approach_y=1,  exit_direction="North", fixed_approach=true },
            { type_name="exit", label="Eastern North exit", x=16, y=0,  approach_x=16, approach_y=1,  exit_direction="North", fixed_approach=true },
        },
    },
    -- D14 (0x3D) -- open secret rupee cave
    ["00:3D"] = {
        name = "Overworld D14",
        entities = {
            { type_name="cave", label="Event", x=18, y=9, approach_x=18, approach_y=9 },
        },
    },
    -- E15 (0x4E) -- armos push, 10 Secret Rupee Cave. The mechanic
    -- here is distinct from the standard touch-armos reveal: the
    -- player has to PUSH the armos statue to reveal the stairs.
    -- Functionally still labeled "Event" per the visible/hidden
    -- convention.
    ["00:4E"] = {
        name = "Overworld E15",
        entities = {
            { type_name="cave", label="Event", x=20, y=9, approach_x=20, approach_y=9 },
        },
    },
    -- E3 (0x24) -- Power Bracelet item screen. Touching the armos here
    -- drops the Power Bracelet on the ground. The drop is picked up
    -- by the standard live dropped-item slot scan, so we don't need
    -- to bake the item itself. Before touch, baked Event marks the
    -- pickup tile so the player knows there's something to find.
    -- After Link owns the bracelet (InvBracelet at 0x0665 != 0), the
    -- Event is suppressed by the Power-Bracelet special case in
    -- build_entity_list -- otherwise the Event would persist forever
    -- since this screen has no cave/stair tiles for the standard
    -- live-scan suppression to act on.
    ["00:24"] = {
        name = "Overworld E3",
        entities = {
            { type_name="cave", label="Event", x=26, y=9, approach_x=26, approach_y=9 },
        },
    },
    -- C2
    ["00:21"] = {
        name = "Overworld C2",
        entities = {
            { type_name="cave", label="Event", x=18, y=11, approach_x=18, approach_y=11 },
        },
    },
    -- D3 (0x23) -- push rock, Take Any Road cave entrance at (6, 11).
    -- One of the four Take Any Road caves; entrance is hidden behind
    -- a pushable rock so labeled "Event". After the rock is pushed,
    -- the live tile scan picks up the 0x70 stair cluster and emits a
    -- live Stairs entity that supersedes this baked Event.
    ["00:23"] = {
        name = "Overworld D3",
        entities = {
            { type_name="cave", label="Event", x=6, y=11, approach_x=6, approach_y=11 },
        },
    },
    -- C6
    ["00:25"] = {
        name = "Overworld C6",
        entities = {
            { type_name="cave", label="Cave", x=20, y=3, approach_x=20, approach_y=3 },
        },
    },
    -- C7
    ["00:26"] = {
        name = "Overworld C7",
        entities = {
            { type_name="cave", label="Event", x=6, y=3, approach_x=6, approach_y=3 },
        },
    },
    -- C8
    ["00:27"] = {
        name = "Overworld C8",
        entities = {
            { type_name="cave", label="Event", x=28, y=3, approach_x=28, approach_y=3 },
        },
    },
    -- B16 / P2 (0x1F) -- false wall, Shop. The screen has a visible
    -- Gambling Cave entrance which the live tile scan picks up
    -- automatically. Separately, there's a false wall on the east
    -- side at tile (16, 3) leading to a Shop -- the player walks
    -- through what looks like solid rock. Per user spec: even
    -- though false-wall mechanics are normally left to the player
    -- to discover, this one is so well-hidden it's worth telling
    -- the player it exists.
    --
    -- Uses type_name="landmark" not "cave" so the live tile-scan
    -- suppression doesn't drop this entry when the visible Gambling
    -- Cave 0x24 tile is detected. Both entities coexist: live
    -- "Entrance" for the gambling cave + baked "Event" for the
    -- false-wall shop.
    ["00:1F"] = {
        name = "Overworld P2",
        entities = {
            { type_name="landmark", label="Event", x=16, y=3, approach_x=16, approach_y=3 },
        },
    },
    -- C13 (split north exits)
    ["00:2C"] = {
        name = "Overworld C13",
        entities = {
            { type_name="cave",  label="Event",              x=18, y=13, approach_x=18, approach_y=13 },
            { type_name="exit",  label="Northwest exit",     x=6,  y=0,  approach_x=6,  approach_y=1,  exit_direction="North", fixed_approach=true },
            { type_name="exit",  label="Northeast exit",     x=22, y=0,  approach_x=22, approach_y=1,  exit_direction="North", fixed_approach=true },
        },
    },
    -- C16
    ["00:2F"] = {
        name = "Overworld C16",
        entities = {
            { type_name="cave", label="Cave", x=12, y=9, approach_x=12, approach_y=9 },
        },
    },
    -- C3 (0x22) -- Dungeon 6 entrance. Has both a visible open entrance
    -- at (13, 9) and an armos-revealed alt route at (6, 9). Same
    -- dual-entry pattern as L1:
    --   * The visible entrance is auto-detected by the live tile scan
    --     (0x24 cluster). The baked "Entrance" entry exists only as
    --     a fallback in case the live scan ever misses; in practice
    --     it gets suppressed by the cave-type suppression when the
    --     0x24 tile is found and the live "Cave" entity replaces it.
    --   * The armos alt route is type_name="stairs" so it's NOT
    --     suppressed by the 0x24 mouth scan (that's the visible
    --     entrance, not this one). Only suppressed by 0x70 stair-tile
    --     suppression once the player touches the armos and reveals
    --     the alt route stairs. Until then, the baked Stairs Event
    --     coexists with the live Entrance.
    ["00:22"] = {
        name = "Overworld C3",
        entities = {
            { type_name="cave",   label="Cave", x=13, y=9, approach_x=13, approach_y=9 },
            { type_name="stairs", label="Event",    x=6,  y=9, approach_x=6,  approach_y=9 },
        },
    },
    -- B13 (0x1C) -- armos reveal, Hint Cave (loc 21). Armos at tile
    -- (22, 8) reveals stairs to a cave whose old man tells you "Secret
    -- is in the tree at the dead-end". Event marker placed at (22, 9)
    -- per the accessibility-pathfinding convention (odd Y, even X) so
    -- it's one tile south of the actual armos.
    ["00:1C"] = {
        name = "Overworld B13",
        entities = {
            { type_name="cave", label="Event", x=22, y=9, approach_x=22, approach_y=9 },
        },
    },
    -- H10 (0x79) -- Take Any Road cave entrance, triggered event at
    -- tile (16, 11). One of the four Take Any Road caves; entrance is
    -- not visible until triggered, so labeled "Event" per the
    -- visible/hidden convention.
    ["00:79"] = {
        name = "Overworld H10",
        entities = {
            { type_name="cave", label="Event", x=16, y=11, approach_x=16, approach_y=11 },
        },
    },
    -- L1 (0x0B) -- Dungeon 5. Has both a visible open entrance at
    -- (14, 9) and an armos-revealed alt route at (22, 9). The visible
    -- entrance is auto-detected by the live tile scan (0x24 cluster).
    -- We bake the alt route as type_name="stairs" Event so:
    --   * Before armos push: baked Stairs Event sits at (22, 9) and
    --     the live scan emits a separate Entrance from the visible
    --     0x24 cluster. Player sees Entrance + Event.
    --   * After armos push: live scan finds the new 0x70 stair tile
    --     and emits a live "Stairs" entity. The baked Stairs Event
    --     is suppressed (stair_x is set, type_name == "stairs"
    --     triggers the type-match suppression). Player sees
    --     Entrance + Stairs.
    -- The armos route is verified_extra in confirmed_screens because
    -- it's a duplicate path to the same dungeon.
    ["00:0B"] = {
        name = "Overworld L1",
        entities = {
            { type_name="stairs", label="Event", x=22, y=9, approach_x=22, approach_y=9 },
        },
    },
    -- F6 (0x55) -- river split south exit, plus a Dock at (16, 9)
    -- where the raft launches/lands. Same custom "Dock" label as
    -- D16 since both are raft endpoints; counted as verified_extra
    -- in confirmed_screens so it doesn't bump the totals.
    ["00:55"] = {
        name = "Overworld F6 (river split)",
        entities = {
            { type_name="cave", label="Dock", x=16, y=9, approach_x=16, approach_y=9 },
            { type_name="exit", label="South exit", x=6, y=22, approach_x=6, approach_y=20, exit_direction="South" },
        },
    },
    -- E3 (0x42) -- Lake Drain Event. Special non-cave event tied to
    -- the screen itself; firing the recorder here drains the lake.
    -- Labeled "Event" per the visible/hidden convention; the M-key
    -- counter does not count it as a cave.
    ["00:42"] = {
        name = "Overworld E3",
        entities = {
            { type_name="cave", label="Event", x=15, y=15, approach_x=15, approach_y=15 },
        },
    },
    -- E4 (0x43) -- Health Recharge Event. Special non-cave event;
    -- the fairy here heals Link to full hearts. Labeled with custom
    -- text "Health Recharge" instead of generic "Entrance/Event"
    -- per user spec, since the player benefits from knowing what
    -- the screen does.
    ["00:43"] = {
        name = "Overworld E4",
        entities = {
            { type_name="cave", label="Health Recharge", x=15, y=15, approach_x=15, approach_y=15 },
        },
    },
    -- E5 (0x44) -- open Bomb Shop
    ["00:44"] = {
        name = "Overworld E5",
        entities = {
            { type_name="cave", label="Cave", x=8, y=3, approach_x=8, approach_y=3 },
        },
    },
    -- E6 (0x45) -- Dungeon 4 entrance (open). Labeled just "Cave"
    -- to match the convention: open caves are "Cave".
    ["00:45"] = {
        name = "Overworld E6",
        entities = {
            { type_name="cave", label="Cave", x=16, y=9, approach_x=16, approach_y=9 },
        },
    },
    -- J5 (0x49) -- push rock, Take Any Road cave entrance at (6, 11).
    -- One of the four Take Any Road caves; entrance is hidden behind
    -- a pushable rock so labeled "Event". After the rock is pushed,
    -- the live tile scan picks up the 0x70 stair cluster and emits a
    -- live Stairs entity that supersedes this baked Event.
    ["00:49"] = {
        name = "Overworld J5",
        entities = {
            { type_name="cave", label="Event", x=6, y=11, approach_x=6, approach_y=11 },
        },
    },
    -- E12 (0x4B) -- burn, Potion Shop
    ["00:4B"] = {
        name = "Overworld E12",
        entities = {
            { type_name="cave", label="Event", x=22, y=5, approach_x=22, approach_y=5 },
        },
    },
    -- H4 (0x73) -- empty for cave purposes (no entrance), but the
    -- screen has split west exits. Per user: cave is removed but
    -- exit data preserved.
    ["00:73"] = {
        name = "Overworld H4",
        entities = {
            { type_name="exit", label="Northwest exit", x=-1, y=4,  approach_x=0, approach_y=4,  exit_direction="West", fixed_approach=true },
            { type_name="exit", label="Southwest exit", x=-1, y=16, approach_x=0, approach_y=16, exit_direction="West", fixed_approach=true },
        },
    },
    -- G5 (0x64) -- open Potion Shop
    ["00:64"] = {
        name = "Overworld G5",
        entities = {
            { type_name="cave", label="Cave", x=14, y=3, approach_x=14, approach_y=3 },
        },
    },
    -- G6 (0x65) -- river with bridge. Per user: no cave on this
    -- screen. Exit data preserved (river splits the room above into
    -- two openings; south and east are normal). No cave entity.
    ["00:65"] = {
        name = "Overworld G6 (river with bridge)",
        entities = {
            { type_name="exit", label="North exit west of river", x=6,  y=0,  approach_x=6,  approach_y=1,  exit_direction="North", fixed_approach=true },
            { type_name="exit", label="North exit east of river", x=17, y=0,  approach_x=17, approach_y=1,  exit_direction="North", fixed_approach=true },
            { type_name="exit", label="South exit",               x=8,  y=22, approach_x=8,  approach_y=20, exit_direction="South", fixed_approach=true },
            { type_name="exit", label="East exit",                x=31, y=10, approach_x=29, approach_y=10, exit_direction="East" },
        },
    },
    -- G7 (0x66) -- open Blue Candle Shop
    ["00:66"] = {
        name = "Overworld G7",
        entities = {
            { type_name="cave", label="Cave", x=14, y=3, approach_x=14, approach_y=3 },
        },
    },
    -- G8 (0x67) -- bomb, 30 Secret Rupee Cave
    ["00:67"] = {
        name = "Overworld G8",
        entities = {
            { type_name="cave", label="Event", x=14, y=3, approach_x=14, approach_y=3 },
        },
    },
    -- G9 (0x68) -- burn tree, Door Repair Cave -- confirmed.
    -- Hidden cave, labeled "Event" per the visible/hidden convention.
    ["00:68"] = {
        name = "Overworld G9",
        entities = {
            { type_name="cave", label="Event", x=4, y=13, approach_x=4, approach_y=13 },
        },
    },
    -- G4 (0x63) -- burn, Door Repair Cave
    ["00:63"] = {
        name = "Overworld G4",
        entities = {
            { type_name="cave", label="Event", x=12, y=13, approach_x=12, approach_y=13 },
        },
    },
    -- G16 (0x6F) -- open Bomb Shop
    ["00:6F"] = {
        name = "Overworld G16",
        entities = {
            { type_name="cave", label="Cave", x=6, y=3, approach_x=6, approach_y=3 },
        },
    },
    -- O7 (0x6E) baked entry removed -- 0x6E is G15 which is unfindable.
    -- P7 (0x6F) baked entry replaced -- 0x6F is G16 which is the Bomb Shop.
    -- H1 (0x70) -- open gambling cave
    ["00:70"] = {
        name = "Overworld H1",
        entities = {
            { type_name="cave", label="Cave", x=22, y=3, approach_x=22, approach_y=3 },
        },
    },
    -- H2 (0x71) -- bomb cave
    ["00:71"] = {
        name = "Overworld H2",
        entities = {
            { type_name="cave", label="Event", x=10, y=3, approach_x=10, approach_y=3 },
        },
    },
    -- H12 (0x7B) -- bomb cave (heart/potion shop) -- labeled "Event" to preserve mystery.
    ["00:7B"] = {
        name = "Overworld H12",
        entities = {
            { type_name="cave", label="Event", x=18, y=3, approach_x=18, approach_y=3 },
        },
    },
    -- H5 (0x74) -- Dungeon 3 entrance (open). Labeled just "Cave"
    -- to match the convention: open caves are "Cave", hidden ones
    -- are "Event". The dungeon's identity is part of what the player
    -- discovers by walking in.
    ["00:74"] = {
        name = "Overworld H5",
        entities = {
            { type_name="cave", label="Cave", x=16, y=9, approach_x=16, approach_y=9 },
        },
    },
    -- H6 (0x75) -- open Hint Cave (loc 25). Visible entrance at (4, 3).
    ["00:75"] = {
        name = "Overworld H6",
        entities = {
            { type_name="cave", label="Cave", x=4, y=3, approach_x=4, approach_y=3 },
        },
    },
    -- H7 (0x76) -- bomb wall, Money Making Cave. Triggered event at
    -- (12, 3); per the visible/hidden convention this is an "Event"
    -- (counts toward entrances, not secrets, since it's a bombable
    -- reveal that opens to a paid-money-rewards cave). Note: room
    -- ID is 0x76 not 0x67 -- 0x67 is G8.
    ["00:76"] = {
        name = "Overworld H7",
        entities = {
            { type_name="cave", label="Event", x=12, y=3, approach_x=12, approach_y=3 },
        },
    },
    -- H8 (0x77) -- open wood sword cave
    ["00:77"] = {
        name = "Overworld H8",
        entities = {
            { type_name="cave", label="Cave", x=8, y=3, approach_x=8, approach_y=3 },
        },
    },
    -- H9 (0x78) -- triggered event, Potion Cave. Like H10's Take Any
    -- Road, the entrance is not visible until triggered. Event marker
    -- at (8, 13).
    ["00:78"] = {
        name = "Overworld H9",
        entities = {
            { type_name="cave", label="Event", x=8, y=13, approach_x=8, approach_y=13 },
        },
    },
}

-- ============================================================
-- EXIT CACHE REMOVED
-- ============================================================
-- Exit scanning is now always live via Navigation.lua tile grid.
-- Baked room entries in BAKED_ROOM_ENTRIES handle special cases:
--   - Rooms with split exits (multiple exits on one side)
--   - Secret cave positions (bomb/burn/armos entrances)
-- ============================================================

-- ============================================================
-- PUBLIC API
-- ============================================================

-- Load disk cache and merge new rooms into hardcoded table (never overwrite hardcoded).
function M.load(log_fn)
    if log_fn then log_fn("WorldMap: loaded (live exit scanning, no cache)") end
end

function M.get_room_entry(room_key)
    return BAKED_ROOM_ENTRIES[room_key]
end

-- ============================================================
-- UNFINDABLE ROOMS
-- Rooms where the ROM screen_manifest reports has_underground=true
-- but the cave entrance is unreachable in normal play (e.g. H3's
-- unfindable Door Repair Cave). Listed here so Navigation suppresses
-- the auto-fallback Entrance entity it would otherwise emit.
-- Keyed by quest:room (e.g. "00:72").
-- ============================================================
local UNFINDABLE_ROOMS = {
    ["00:2B"] = true,  -- C12: recorder secret -- Q2 ONLY (per strategy guide)
    ["00:3A"] = true,  -- D11: unfindable
    ["00:4A"] = true,  -- E11: Shop -- unfindable
    ["00:58"] = true,  -- F9: 30 Secret Rupee Cave -- unfindable
    ["00:60"] = true,  -- G1: Gambling Cave -- unfindable
    ["00:6E"] = true,  -- G15: 10 Secret Rupee Cave -- unfindable
    ["00:72"] = true,  -- H3: unfindable Door Repair Cave
}

function M.is_unfindable(room_key)
    return UNFINDABLE_ROOMS[room_key] == true
end

-- ============================================================
-- OVERWORLD ENEMIES + SCREEN MANIFEST
-- Loaded on demand when first entering overworld, not at startup.
--
-- Enemy name lookups are owned by GameData.lua. Navigation.lua passes
-- the enemy name table into load_overworld_data so this module does not
-- need its own copy of the type-id -> string dictionary.
-- ============================================================

-- LOCATION_TYPES table removed -- it was a generic Z1 default table
-- (e.g. labeling loc 34 as "cave_white_sword", loc 35 as
-- "cave_magical_sword") that did NOT match this ROM's actual cave
-- contents. Multiple loc indexes were verified empirically to be
-- different cave types in the wild than the table claimed.
-- cave_data.lua is the only authoritative source for cave type/label/
-- dialog. Nothing in the codebase ever read screen_manifest's
-- location_type field anyway -- consumers only use location_idx and
-- has_underground. So the field is gone too.

local overworld_enemies = nil  -- nil until first loaded
local screen_manifest   = nil

local ROM_DOMAIN = "PRG ROM"
local function rom_read(addr)
    local ok, v = pcall(function() return memory.read_u8(addr, ROM_DOMAIN) end)
    return ok and v or 0
end

function M.load_overworld_data(log_fn, enemy_names)
    if overworld_enemies then return end  -- already loaded
    enemy_names = enemy_names or {}

    -- Build OVERWORLD_ENEMIES from ROM
    local count_table = {}
    for i = 0, 3 do count_table[i] = rom_read(0x19324 + i) end
    overworld_enemies = {}
    for room_id = 0x00, 0x7F do
        local placement = rom_read(0x18500 + room_id)
        local cc = bit.rshift(bit.band(placement, 0xC0), 6)
        local mm = bit.band(placement, 0x3F)
        overworld_enemies[room_id] = {
            type_id = mm,
            count   = count_table[cc] or 1,
            label   = enemy_names[mm],
        }
    end

    -- Build screen_manifest from ROM
    local ROM = {
        table2             = 0x18480, table3 = 0x18680,
        armos_item_screen  = 0x10CB2, armos_stair_screens = 0x10CB3,
        false_wall_screen  = 0x1EE9A, recorder_screens    = 0x1EF66,
        stepladder_screens = 0x1F20D, warp_screens        = 0x6010,
        raft_screen1       = 0x14944, raft_screen2        = 0x14948,
    }
    local secrets = {}
    local function add_s(addr, kind)
        local s = rom_read(addr)
        if s ~= 0xFF then secrets[s] = secrets[s] or {}; table.insert(secrets[s], kind) end
    end
    add_s(ROM.armos_item_screen, "armos_item_secret")
    for i = 0, 5  do add_s(ROM.armos_stair_screens + i, "armos_stair_secret") end
    add_s(ROM.false_wall_screen, "false_wall")
    for i = 0, 10 do add_s(ROM.recorder_screens + i, "recorder_secret") end
    for i = 0, 5  do add_s(ROM.stepladder_screens + i, "stepladder_required") end
    for i = 0, 7  do add_s(ROM.warp_screens + i, "warp_destination") end
    add_s(ROM.raft_screen1, "raft_dock"); add_s(ROM.raft_screen2, "raft_dock")
    screen_manifest = {}
    for room_id = 0x00, 0x7F do
        local t2 = rom_read(ROM.table2 + room_id)
        local t3 = rom_read(ROM.table3 + room_id)
        local loc_idx = bit.rshift(bit.band(t2, 0xFC), 2)
        screen_manifest[room_id] = {
            location_idx    = loc_idx,
            has_underground = loc_idx > 0,
            is_dungeon      = loc_idx >= 1 and loc_idx <= 9,
            ignore_q1       = bit.band(t3, 0x80) ~= 0,
            ignore_q2       = bit.band(t3, 0x40) ~= 0,
            stair_pos       = bit.rshift(bit.band(t3, 0x30), 4),
            secrets         = secrets[room_id] or {},
        }
    end

    if log_fn then log_fn("WorldMap: overworld_enemies + screen_manifest loaded") end
end

function M.get_overworld_enemy(room_id)
    return overworld_enemies and overworld_enemies[room_id] or nil
end

function M.get_screen_manifest(room_id)
    return screen_manifest and screen_manifest[room_id] or nil
end

function M.is_overworld_data_loaded()
    return overworld_enemies ~= nil
end

return M
