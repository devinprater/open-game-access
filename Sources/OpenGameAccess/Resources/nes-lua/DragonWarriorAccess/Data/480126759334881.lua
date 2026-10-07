local CONFIG = { use_baked_world_grid = true }
local WORLD_MAP_GRID = {
    name = "World Map",
    map_id = 0x01,
    width = 120,
    height = 120,
    terrain = {
        walkable = {
            grass = true,
            desert = true,
            forest = true,
            hill = true,
            swamp = true,
            bridge = true,
            town = true,
            castle = true,
            cave = true,
            shrine = true,
            stairs = true,
        },
        blocked = {
            water = true,
            mountain = true,
            rock_wall = true,
        },
    },
    landmarks = {
        { id = "tantegel", name = "Tantegel Castle", kind = "castle", x = 43, y = 43 },
        { id = "brecconary", name = "Brecconary", kind = "town", x = 48, y = 41 },
        { id = "erdricks_cave", name = "Erdrick's Cave", kind = "cave", x = 28, y = 12 },
        { id = "garinham", name = "Garinham", kind = "town", x = 2, y = 2 },
        { id = "kol", name = "Kol", kind = "town", x = 104, y = 10 },
        { id = "staff_of_rain_cave", name = "Rain Shrine", kind = "shrine", x = 81, y = 1 },
        { id = "rock_mountain_cave", name = "Rock Mountain Cave", kind = "cave", x = 29, y = 57 },
        { id = "swamp_cave_north", name = "Swamp Cave North Entrance", kind = "cave", x = 104, y = 44 },
        { id = "swamp_cave_south", name = "Swamp Cave South Entrance", kind = "cave", x = 104, y = 49 },
        { id = "rimuldar", name = "Rimuldar", kind = "town", x = 102, y = 72 },
        { id = "rainbow_drop_cave", name = "Sanctum", kind = "shrine", x = 108, y = 109 },
        { id = "cantlin", name = "Cantlin", kind = "town", x = 73, y = 102 },
        { id = "hauksness", name = "Hauksness", kind = "town", x = 25, y = 89 },
        { id = "charlock", name = "Charlock Castle", kind = "castle", x = 48, y = 48 },
    },
    hidden_item_labels_by_position = {
        ["65,49"] = "Event",
        ["73,100"] = "Event",
        ["83,113"] = "Event",
    },
    hidden_item_requirements_by_position = {
        ["65,49"] = {
            require_inventory_item = 0x0E,
            ram_mask_set = {
                address = 0x00CF,
                mask = 0x08,
            },
        },
        ["73,100"] = {
            ram_mask_set = {
                address = 0x00E4,
                mask = 0x02,
            },
        },
        ["83,113"] = {
            ram_nibble_contains = {
                addresses = { 0x00C1, 0x00C2, 0x00C3, 0x00C4 },
                value = 0x07,
            },
        },
    },
    hidden_item_category = "other",
    -- Each keyed row will hold terrain labels by absolute world Y coordinate.
    -- Example: rows[43] = { [43] = "castle", [44] = "grass" }
    rows = {},
    defaults = {
        unknown = "unknown",
    },
}

local WORLD_TERRAIN_CODES = {
    ["."] = "grass",
    ["d"] = "desert",
    ["f"] = "forest",
    ["h"] = "hill",
    ["s"] = "swamp",
    ["b"] = "bridge",
    ["w"] = "water",
    ["m"] = "mountain",
    ["t"] = "town",
    ["c"] = "castle",
    ["v"] = "cave",
    ["r"] = "shrine",
    ["?"] = "unknown",
}

local BAKED_WORLD_ROWS = [[
000:ww......wwwwwwwwwwww.......wwwwwwwwwwwwwww........wwwwwwwwwwwwwwwwwwwwwwwwwfffff...wwwwwwwwwwwwww..........wwwwwwwwwwwww
001:w...ffff..wwwwwwww...........wwwwwwwwwww......hhhhhhhhwwwwwwwwwwwwwwwwwwffffffff.S.wwwwwwwwwmmmmmmmhhhhh.......wwwwwwwww
002:..tffffff..wwwww......fffff...wwwwwwwww......fffffhhhhhhwwwwwwwwwwwwwwfffffffff...wwwwwwwwwmmmmhhhhhhhhhh.......wwwwwwww
003:.fffffffff...........ffffffff..wwwwwwww.....fffffffffhhhhwwwwwwwwwwwwffffffffffmmwwwwwwwwwmmmhhhhhhhhhhhhh.........wwwww
004:ffffffhhhhhmmm......ffffffffff..wwwwww.....ffffffffffhhhhhwwwwwwwwwwwfffffffffmmmwwwwwwwmmmhhhhhhhhhffffffff........wwww
005:fffffhhhhhmmmmm.....ffffffffff....ww.....fffffwwwfffffhhhhwwwwwwwwwwfffffffffmmmwwwwwwwmmmhhhhhhhhffffffffffff.......www
006:fffhhhhhmmmmmmmm...fffffffffffff.........ffffwwwwwfffhhhhwwwwwwwwwmmmfffffffmmmwwwwwwwhhhhhhhhhhhffffffffffffff......www
007:hhhhhhmmmmmffffmmmmffffffffffff.........ffffffwwwhhhhhhhwwwwwwwwwmmmfffffffffmmmwwwwwwwhhhhhhhhhffffffmmmmmffffff.....ww
008:hhhhmmmmfffffffffmmmfffffffffffff......ffffffwwwhhhhhhhhwwwwwwwwwwmmmfffffffffmmmwwwwwhhhhhhhhffffffmmmfffmmmffffff...ww
009:hhhhmmffffffffffffmmffffffddddfffff.....fffffffhhhhhhhhwwwwwwwwwfffmmmffffffffffmmwwwwhhhhhhhhfffffmmffffffmmfffffff...w
010:hh...ffffffffffffmmmfffffddddddffffff.....ffffffhhhhhhwwwwwwwwwfffmmmmfffffffffffmmwwwwhhhhhhhfffffffffftfffmmffffffffww
011:ww....ffffffffffffmmmfffddddddddffffff.....fffffffhhhwwwwwwwwwwffffmmmmffffffffffmmwwwwwhhhhhhhffffffffffffffffffffffffw
012:www.....ffffffffffmmmmffddddvdddfffff......fffffffhhhhhwwwwwwwfffffmmmmfffffffffffmwwwwwhhhhhhhhffffffffffffffffffffffff
013:wwww........fffffffmmmmffdddddddffffff......fffffffhhhhhwwwwwfffffffmmmmmmffffffffmmwwwwhhhhhhhhhhffffffffffffffffffffff
014:wwwwwffff.....ffffffmmmmfffddddffffffff....ffffffffhhhh..wwwfffffffffmmmmmmfffffffmmmwwwwhhhhhhh....ffffffffffffffffffff
015:wwwwfffff......fffffffmmmfffffffffffff......ffffffffhh...wwfffffff.......mmmfffffffmmmwwwwhhhhh......ffffffffffffffffwww
016:wwwwwwff.......ffffff....fffffffffffff.....ffffffffff....wwffff............mmmmfffffmmmwwwwwff.......ffffffffffffwwwwwww
017:wwwffffff.......ffff.....ffffffffffff.....ffffffffff......ww.................mmmfffffmmmwwfff.......ffffffffmmmmwwwwwwww
018:wwfffffffff.............fffffffffffff......ffffffff........ww.................mmmmffffwwwffff.......ffffmmmmmmffffwwwwww
019:wwfffffffff.............ffffffffffffff......ffffff..........b......ffffff......mmmmfffffffff.......ffmmmmmmmfffddddwwwww
020:wfffffffffff..............ffffffffffff.......fff............ww....ffffffff......mmmmmmmmmmffw.....ffmmmmmfffffdddddddwww
021:ffffffffffff...............ffffffffff........................w...ffffffffff.....ffmmmmmmmmmww.....fmmmmmmdddddddddddddww
022:fffffffffffff............hhhhfffffff.........................w..fffffhhhhfff...fffffffhhhwwwww.....mmmmmdddddddddddddddd
023:ffffffffffffff.......wwwhhhhhhhffff...............ffff.......wwffffhhhhhhffff...fffffhhhhhwwww......mmmddddddddddffddddd
024:fffffffffffffff....wwwwwwwhhhhhhffff..........fffffffff.......wwfffhhhhhhhffff....ffhhhhhhhwwww........dddddddddffffdddd
025:ffffffffffffffff..wwwwwwwwwhhhhhffff........ffffffffffhhh......wwfffhhhhhhhffff......hhhhhhhwwww.......dddddddddffffdddd
026:fffffffffffffffffwwwwwwwwwwhhhhffff........ffffffffffhhhhhh.....wwfffhhhhhfffff.......fffffffwww.......ddddddddddffddddd
027:wwfffffffffffffffwwwwwwwwwmmmffffff.......ffffffffffhhhhhhhh.....wwwwwffffffffff.....fffffffwwwww.......dddddddddddddddw
028:wwwfffffffffffffwwwwwwwwwmmmmmmmff......ffffffffffffhhhhhhhhh.......wwwwwffffffff.....fffffwwwww.........dddddddddddddww
029:wwwwwwwwfffffffwwwwwwmmmmmmmmmmmm.......fffffffffffffhhhhhhhh....mmmmwwwwwfffffff......fffwwwww...........dddddddddddwww
030:wwwwhhhwwwffffwwwwwmmmmmmmmmmmmmmm.....fffffffffmmmmmmhhhhhh...mmmmmwwwwwwwfffffff......wwwwww..............dddddddwwwww
031:wwwhhhhhhbffwwwmmmmmmmmmmmmmmmmm......fffffffmmmmmmmmmmhhhhhhmmmmmmwwwwwwwwwffffff.......wwwwww.............hhhhhhwwwwww
032:wwhhhhhhhwwwwwhhhhmmmmmmmmmmmm.........fffmmmmmmmmmmmmmmhhhhhmmmmmmmwwwwwwwffffff........wwwwwww..........hhhhhhhhhhwwww
033:wwhhhhhhhhhhhhhhhhhhhmmmmmmm............mmmmmmmhhhhmmmmmmfffmmmmmmmmmwwwwwffffffff........wwwww..........hhhhhhhhffffwww
034:wwhhhhhhhhhhhhhhhhhhhhhmmmm............mmmmhhhhhhhhhhhmmffffffffffmmmmwwwffffffffff........www..........hhhhhhhfffffffww
035:wwwhhhhhhhhhh....hhhhhhhmm..............hhhhhhhhhhhhhhhmmfffffdddddmmmmmmmffffffff.........ww..........hhhhhhhfffffffffw
036:wwwwhhhffff........hhhhhww.............fffffffhhhhhhhhmmmmffdddddddddmmmmmmffffff.........ww.....mmmmhhhhhhhhfffffffffff
037:wwffffffff..........hhhwwww...........fffffffffffhhhhhhmmmmdddddddddsssfffffffff..........b...ffmmmmmmhhhhhhfffffffffffw
038:wfffffff...........hhhhwwwww........ffffffffffffffhhhhhhhhwwwwdddddsssssfffffffff......wwwwfffffmmmmmmmhhhhffffffffffffw
039:fffffff..........fffffwwwww........ffffffffffffff....hhhhwwwwwwwwssssssssfffffff....wwwwwwwffffffffmmmmmhhffffffffffffww
040:fffff.........fffffffwwwwww.......fffffffffff..........wwwwwwwwwssssssssfffffffwwwwwwwwwwwwwffffssssmmmmfffffffffffffwww
041:ffff.......ffffffffwwwwwwwww.......fffffff......t.....wwwwwwwwwwwwsssssffffffwwwwwwwwwwwwwwwwwssssssssmmmmmffffffwwwwwww
042:fffff.....fffffwwwwwwwwww...........ffff.........wwwwwwwwwwwwwwwwwwwwfffffwwwwwwwwwwwwwwwwwwwwwwwssssssmmmmmmmwwwwwwwwww
043:wfffff...ffffwwwwwwwwwww...................c...wwwwwwwwwwwwwwwwwwwwwwwwwwwwwwwwwwwwwwwwwwwwwwwwwwwwsssssssmmmwwwwwwwwwww
044:wwffffffffffwwwwwwwww........................wwwwwwwwwwwwwwwwwwwwwwwwwwwwwwwwwwwwwwwwwwwwwwwwwwwwwwwwwwsvsmmwwwwwwwwwwww
045:wwfffffffffwwwwwwwwwffffff.................wwwwwwwwmmmwwwwwwwwwwwwwwwwwwwwwwwwwwwwwwwffffffwwwwwwwwwwwwwwwwwwwwwwwwwwwww
046:wwwwfffffffffwwwwwwffffffff..............wwwwwwwwmmmmmmwwwwwwwwwwwwwwwwwwwwwwwwfffffffffff.....wwwwwwwwwwwwwwwwwwwwhhhhw
047:wwwfffffffffffwwwwwwwwfffffff...........wwwwwwwsssmmmmmmmwwwwwwwwwwwwwwwwwfffffffffffffff.........wwwwwwwwwwwwwwwwhhhhhh
048:wwfffffffffffffwwwwwwwwffffffff........wwwwwwwmscsmmmmmmmmwwwwwwwwwwwwwddddffffffffffffff............wwwwwwwwwwwwhhhhhhh
049:wwwfffffffffffwwwwwwwwwwffffffff........wwwwwmmsssddmmmmmmmwwdddwdddwwdddddddfffffffffffff.............svswwwwwwhhhhhhhh
050:wwwffffffffffmmmmwwwwwfffffffffff.......wwwwmmmmmmmddmmmmddddddwwwwdddddddddddffffffffffffff...........ssswwwwwffffhhhhh
051:wwwwwffffffmmmmmmmmmmmmmffffffff........wwwwwmmmmddddmmmdddddmmwwwwmmddddddddddfffffffffffffffff......hhhhhhwwffffffffff
052:wwww..ffmmmmmmmmmmmmmmmmmffffffff......wwwwwmmmmddddmmmmmdddmmwwwwwwmmmddddddddddfffffffffffffffmmmhhhhhhhhhhfffffffffff
053:ww....mmmmmmmmmmmmmmmmmmmmffffffff....wwwwwwwmmddddddmmmmmdmmwwwwwwwwmmmmdddddddddfffffffwwwwmmmmmmhhhhhhhhhffffffffffff
054:w....mmmmfffffffmmmmmmmmmmmmmfffffhhhwwwwwwwwmmmddddddmmmmhhmmwwwwwwwwwmmmddddddddfffffwwwwwwwmmmmmmmhhhhhhhhfffffffffff
055:....mmmffffffffffmmmmmmmmmmmmmmffffhhhwwwwwwwwmmddddddmmmhhmmmwwwwwwwwwwmmmmdddddffffwwwwwwwwmmmmmmmmmmmmmhhhfffffffffff
056:....fffffffssssffffmmmmmmmmmmmmmffffhhhwwwwwwwmmdddddmmmhhhmmwwwwwwwwwwwwwmmmhhhhhffffwwwwmmmmfffffmmmmmmmmffffffffffffw
057:...ffffffsssssssfffffmmmm....vmmfffhhhhwwwwwwwmmmdddmmmhhhhmmwwwwwwwwwwwwwmmhhhhhhhhffffffffffffffffmmmmmmmmmmfffffffwww
058:....ffffssssssssffffff......mmmhhhhhhhhwwwwwwwwmmddmmmhhhhhhmmwwwwwwwwwwwmmmhhhhhhhhffffffffffffffffmmmmmmmmmdddffffwwww
059:...ffffffssssssfffffff.....mmmhhhhhhhhwwwwwwwwwmmsmmmmmmhmmmmmwwwwwwwwwwwwmmhhhhhh.........ffffffffmmmmmmmmddddddffwwwww
060:...fffffffssssfffffff......mmmmhhhhhhwwwwwwwwwmmssmmmmm...mmmwwwwwwwwwwwwwwmmhhhh..............fffffffffffddddddddffwwww
061:...fffffffffffffffffmmmmmmmmmmmmmmmwwwwwwwwwwwwmmssmmm...mmmwwwwwwwwwwwwwwmmmhhh................fffffffffddddddddffffwww
062:....ffffffffffffffmmmmmmmmmmmffffwwwwwwwwwwwwwwwmmssmmm...mmmwwwwwwwwwwwwwwmmmhhh...............ffffffffffddddddfffffwww
063:w....fffffffffhhhmmmmmmfffffffff..wwwwwwwwwwwwwwmmssssmm...mmwwwwwwwwwwwwwwwmmmhhhhhhhhfff.....ffffffffffffddddfffffffww
064:ww....fffffffhhhhhhmmmfffffffff.....wwwwwwwwwwwwwmmssssssmmmwwwwwwwwwwwwwwwwwmmhhhhhhhfffff...ffffmmmmmmmmfffffffffffffw
065:wwww....ffffffffhhhhhfffffffff.......wwwwwwwwwwwwwmmmssmmmwwwwwwwwwwwwwwwwwwwwwwhhhhhfffffffffffmmm.....mmmmmmffffffffff
066:wwwwww....fffffffhhhhhhfffff........wwwwwwwwwwwwwwwwmmmmwwwwwwwwwwwwwwwwwwwwwwwwwwfffffffffffffmmm.........mmmmfffffffff
067:wwwwwww.....ffffffhhhffffff........wwwwwwwwwwwwwwwwwwwwwwwwwwwwwwwwwwwwwwwwwwwwwwwwwfffffffffffmm...fffff...mmmmmfffffff
068:wwwwwwww......ffffffffffff.......wwwwwfffffffwwwwwwwwwwwwwwwwwwwwwwwwwwwwwwwwwwwwwwwwwffffffff.....fffffff....mmmfffffff
069:wwwwwwwww......ffffffffff.....wwwwwwfffffffwwwwwwwwwwwwwwwwwwwwwwwwwwwwwwwwwwwwwwwwwfffffffff.....ffwwwwwff....mmmffffff
070:wwwwwwwwwww......ffffffff......wwww..fffffwwwwwwwwwwwwwwwwwwwwwwwwwwwwwwwwwwwwwwwwffffffffff.....ffwwwwwwwff...mmfffffff
071:wwwwwwwwwwww.......ffffffff...........fffwwwwwwwwwwwwwwwwwwwwwwwwwwwwwwwwwwwwwwwwwwffffffffff...ffwww...wwwff..mmmffffff
072:wwwwwwwwwwwwww......fffffffff..........wwwwwwwwwwwwwwwwwwwwwwwwwwwwwwwwwwwwwwwwwwwwwwwffffffff..ffwww.t.dddff..mmfffffff
073:wwwwwwmmmmmmwwwwwwwwwffffffffffff....wwwwwwwwwwwwwwwwwwwwwwwwwwwwwwwwwwwwwwwwwwwwwwwwfffffffffffffwww...wwwff..mmfffffff
074:wwwwwmmmffffffwwwwwwwwwfffffffffff....wwwwwwwwwwwwwwwwwwwwwwwwwwwwwwwwwwwwwwwwwwwwwwwwfffffffffffffwwwwwwwff..mmfffffffw
075:wwwwmmmffffffffffwwwwwfffffffffff......wwwwwwwwwwwwwwwwwwwwwwwwwwwwwwwmmmmmmmmwwwwwwwwwfffffffffffffwwwwwfffmmmmffffffww
076:wwwmmmffffffff....wwwwwwffffffff..........wwwwwwwwwwwwwwwwwwwwwwwwwwmmmmmdddddddwwwwwwwwwfffffffffmmmmmfffffffffffffffww
077:wwmmmffffff..........wwwwfffffff...........wwwwwwwwwwwwwwwwwwwwwwwwddddddddmmmmddwwwwwwwfffffffffffmmmmmfffffffffffffwww
078:wmmmffffff.............wfffffff......ff.....wwwwwwwwwwwwwwwwwwwwwwmmmmmmmmmmmmmmddwwwwwwwffffffffffffmmmmhhhffffffff..ww
079:mmmffff.........ffff...bfffffff.....ffff.....wwwwwwwwwwwwwwmmmmmmmmmmddddddmmmmmmdwwwwwwwwwfffffffffffmmmhhhhffffff.....
080:mmmfff........fffffffffwwfffff.....ffffff....wwwwwwwwwwwmmmmmmmmddddddddddddddmmmdmmwwwwwwffffffffffffffmmhhhfffff......
081:mmfffff.....ffffffffffffwwffff....ffffffff....wwwwwwwwmmmmmmmmmddmmmmmmmmmmmmddmmdmmwwwwwwwffffffffffffffmmhhhfffff.....
082:fffff......hhfffffffffffmmmmfff....ffffff.....wwwwwwwmmmmmmmmmddmmmmddddddddmmdmmdmmmwwwwwwwwfffffffffffffmmhhfffff.....
083:ffff......hhhhfffffffffmmmmmmmm.....ffff......wwwwwwwwmmmmmmmmdmmmmmmmmmdddmmmddmdmmmmwwwwwwwfffffffffffffmmmhhfffff....
084:fffff....hhhhfffffffffmmmmmmmmmmm............wwwwwwwwwwwhhdddddmmmmmmmmmmdmmmmmddddmmmwwwwwwwwfffffffffffffmmhhhfffff..w
085:fffff..hhhhhfffffffffmmmmmmmmmmmmmm........wwwwwwwwwwwhhhhhmmmddmmmmmmmmmdmmmmmmmmfmmmmwwwwwwwwfffffffffffffmmffffffffww
086:wwfffffhhhhhhfffffmmmmmdddddddmmmmmmm....wwwwwwwwwfffhhhhhhhmmmdddddmmmmmdmmmmmmmfffmmmwwwwwwwwwwwffffffffhhhhffffffffww
087:wwwfffffhhhhhhffmmmddddddddddddmmmmmwwwwwwwwwwwwwfffffhhhhhmmmmmmmmmmmmmmdddddddfffffmmmwwwwwwwwwwwwfffffhhhhhffffffffww
088:wwffffffhhhhhhhhhdddddddddddddddmmmwwwwwwwwwwwwffffffhhhhhhmmmmmmm...mmmmmmmmmmfffffffmmwwwwwwwwwwwwwffffffhhhhffffffwww
089:fffffffffhhhhhhhdddddddddtdddddmmwwwwwwwwwwwwffffffhhhhhhhhmmfff....wwwwmmmmmmmmfffffmmwwwwwwwwwwwwwwwffffffhhhhfffwwwww
090:fffffffffffhhhhhddddddddddddddwwwwwwfffffffffffffhhhhhhhhhmmfffff.....wwwsssssmmmmffmmwwwwwwwwwwwwwwfffffffhhhhhhfffwwww
091:wffffffffffffhhddddddddddddwwwwwwwfffffffwwwwwwwhhhhhhhmmmmffffff.....bsssss....mmmfwwwwwwwwwwwwwwwwwfffffffhhhhfffffwww
092:wwwwwfffffffffdddddddddddwwwwwwwffffwwwwffffffhhhhhhhmmmffffffffff...wwssss......mm..wwwwwwwwwwwwwwwwfffffffhhhfffffwwww
093:wwwwwwwwwwwffdddddddddddddwwwwwffffffffwwwwwwwwhhhhhmmfffffffffffwwwwsssss......mmmm..wwwwwwwwwwwwwwwwffffhhhhfffffwwwww
094:wwwwwwwwdddddddddddddddwwwwwwwwwwfffffffffffffffhhhmmffffffffwwwwwwwsssss......mmmmm.wwwwwwwwwwwwwwffwwffffhhhhfffffwwww
095:wwwwdddddddddddddddddddddwwwwwffffffffwwwwfffffffmmmfffffwwwwwwwwwwwwsss.......ffmm...wwwwwwwwwwwwffffwfffffhhfffffwwwww
096:wwddddddhhhhhhdddddddddddfffffwwwwwwwwwwwwwffffmmmfffffwwwwwwwwwwwww...........fffmm...wwwwwwwwwwfffffwwffffffffffwwwwww
097:wwwdddhhhhfffffdddddddddfffffffffffffffwwwfffmmfffffffwwwwwwwwwwwffff.........fffffmmm..wwwwwwwwwwwffffwwfffffffffwwwwww
098:wwhhhhhhffffffffffdddddfffffffffffffffffbffmmmfffffffwwwwwwwffffffffff.......ffffffffmm..wwwwwwwwwffffffwwfffffffwwwwwww
099:whhhhhffffffffffffffddddffffffffffffffwwwwmmffff....wwwwwwwwwwfffffffff......fffffffmm...wwwwwwwwwffsffffbffffffwwwwwwww
100:hhhhhhhffffffffffffffddddfffffffffffwwwwwwwww......wwwwwwwwwwffffffffffrr.rrfffffffmmmm...wwwwwwwffsssfffwwffffwwwwwwwww
101:hhhhhmmmmffffffffffffdddddfffffffwwwwwwwwwww......wwwwwwwwwwwwfffffffffr...rfffffffmmmmm...wwwwwwfffsssffwwfffwwwwwwwwww
102:hhhhhhmmmmmfffffffffdddddddffffffmmwwwwmmmmm......wwwwwwwwwwwwwwfffffffr.t.rffffffffmmmmmhhhwwwwwwfffssfffwwwwwwwwwwwwww
103:hhhhhhhhmmmmmfffffffddddddddfffffmmmwwmmmmff.....ww....wwwwwwwwwwwfffffr...rffffffffffmmhhhhhwwwwwff....fffwwwwwwwwwwwww
104:hhhhhhhmmmmmmmmmffffddddddddhhffffmmmmmmmffff....b.....fffffwwwwwwwffffrrrrrfffffffffmmmhhhhhhwwwwwff...hhhh...wwwwwwwww
105:hhhhhhhmmfffffmmmmmmmdddddhhhhhhfffmmmmmfffff...ww....ffffffffffmmmmffffffffffffffffmmmhhhhhhwwwwwwww..hhhhhhh......wwww
106:whhhhhmmffffffffmmmmmmmddhhhhhhhhmmmmmmfffffffwwwfff.....ffffffffmmmmmmffffffffffmmmmmmhhhhhwwwwwwwwwwhhhmmmmmhhh.....ww
107:wwwhhhmmfffff.....mmmmhhhhhhhhhhhhmmmmmmfffffwwwfffff......ffffffffmmmmmmmmffffmmmmmmmhhhhhhhwwwwwwwwhhhmmmffmmmhh....ww
108:wwwwwhhmmfff.......hhhhhhhhhhhhhmmmmmmmfffffwwwfffffff......ffffffffmmmmmmmmmmmmmmmmmhhhhhhhhhwwwwwwhhhmmmffffmmhhh.wwww
109:wwwwhhhhmmm.......hhhhhhhhhhhhhmmmmmmmfffffwwwwffffff.........fffffsssmmmmmmmmmmmssssshhhhhhwwwwwwwhhhmmmfffSffmm..wwwww
110:wwwhhhhhmm.........hhhhhhhhhhmmmmmmffffffffwwwwwffff..........fffssssssssmmmmmmmssssssshhhhhhwwwwwwwhhhmmfffffmmmm....ww
111:wwwwhhhhmm..........hhhhhhhhmmmmmmfffffffffwwwwfffff..........fffsssssssssmmmmmssssssssshhhhwwwwwwwwwhhhmmffffmmhhh...ww
112:wwwwwhhhhh..........hhhhhhhhhmmmmfffffffffwwwwwwfffff........fffsssssssssssmmmssssssssssshhwwwwwwwwwwhhhmmmfmmmhhh...www
113:wwwwwwwhhhh........hhhhhhhhmmmmmfffffffffwwwwwwwfffffff......ffffssssssssssssssssssssssssswwwwwwwwwwwwhhhmmfmmhhh...wwww
114:wwwwwwhhhhhh......hhhhwwwmmmmmmmmfffffffwwwwwwwwwffffffff...fffffssssssssssswwssssssssssswwwwwwwwwwwwwhhhhhhhhh....wwwww
115:wwwwwwwhhhhhh....hhhhwwwwwfffffffffffffwwwwwwwwwwwwfffffffffffffssssssssssswwwwssssssssswwwwwwwwwwwwwwhhhhhhssswwwwwwwww
116:wwwwwwwwhhhhhhhhhhhhhhwwwffffffffffffwwwwwwwwwwwwwwwwffffffffffssssssssssswwwwwwwwsssswwwwwwwwwwwwwwwwwhhhhsswwwwwwwwwww
117:wwwwwwwwwhhhhhhhhhhhhhbhhhhhhfffffwwwwwwwwwwwwwwwwwwwwwfffffssssssssssssswwwwwwwwwwwwwwwwwwwwwwwwwwwwwwwhhsswwwwwwwwwwww
118:wwwwwwwwwwwhhhhhhhhhhwwwhhhhhhhwwwwwwwwwwwwwwwwwwwwwwwwwwwwssssssssssssswwwwwwwwwwwwwwwwwwwwwwwwwwwwwwwwsssswwwwwwwwwwww
119:wwwwwwwwwwwwhhhhhhhwwwwwwwwwwwwwwwwwwwwwwwwwwwwwwwwwwwwwwwwwsssssssssswwwwwwwwwwwwwwwwwwwwwwwwwwwwwwwwwwssswwwwwwwwwwwww
]]

local function build_overworld_landmarks()
    local landmarks = {}
    for _, landmark in ipairs(WORLD_MAP_GRID.landmarks or {}) do
        landmarks[#landmarks + 1] = {
            key = "landmark:" .. tostring(landmark.id),
            kind = "landmark",
            name = landmark.name,
            terrain_kind = landmark.kind,
            x = landmark.x,
            y = landmark.y,
        }
    end
    return landmarks
end

local OVERWORLD_LANDMARKS = build_overworld_landmarks()

local function world_row(y)
    return WORLD_MAP_GRID.rows and WORLD_MAP_GRID.rows[y] or nil
end

local function world_terrain_at(x, y)
    if x == nil or y == nil then
        return WORLD_MAP_GRID.defaults.unknown
    end
    if x < 0 or y < 0 or x >= WORLD_MAP_GRID.width or y >= WORLD_MAP_GRID.height then
        return "blocked_edge"
    end
    local row = world_row(y)
    if row and row[x] then
        return row[x]
    end
    return (WORLD_MAP_GRID.defaults and WORLD_MAP_GRID.defaults.unknown) or "unknown"
end

local function world_tile_walkable(terrain_name)
    if not terrain_name then
        return false
    end
    return WORLD_MAP_GRID.terrain
        and WORLD_MAP_GRID.terrain.walkable
        and WORLD_MAP_GRID.terrain.walkable[terrain_name] == true
end

local function world_tile_blocked(terrain_name)
    if terrain_name == "blocked_edge" then
        return true
    end
    return WORLD_MAP_GRID.terrain
        and WORLD_MAP_GRID.terrain.blocked
        and WORLD_MAP_GRID.terrain.blocked[terrain_name] == true
end

local function set_world_row(y, values)
    WORLD_MAP_GRID.rows[y] = values
end

local function set_world_cell(x, y, terrain_name)
    if x == nil or y == nil then
        return
    end
    if x < 0 or y < 0 or x >= WORLD_MAP_GRID.width or y >= WORLD_MAP_GRID.height then
        return
    end
    if not WORLD_MAP_GRID.rows[y] then
        WORLD_MAP_GRID.rows[y] = {}
    end
    WORLD_MAP_GRID.rows[y][x] = terrain_name
end

local function set_world_span(y, x_start, x_end, terrain_name)
    if y == nil or x_start == nil or x_end == nil then
        return
    end
    local left = math.min(x_start, x_end)
    local right = math.max(x_start, x_end)
    for x = left, right do
        set_world_cell(x, y, terrain_name)
    end
end

local function set_world_row_codes(y, x_start, codes)
    if not y or not x_start or not codes then
        return
    end
    local index = 0
    for code in string.gmatch(codes, ".") do
        local terrain_name = WORLD_TERRAIN_CODES[code]
        if terrain_name then
            set_world_cell(x_start + index, y, terrain_name)
        end
        index = index + 1
    end
end

local function apply_world_patch(patch)
    if not patch or not patch.rows then
        return
    end
    for _, row in ipairs(patch.rows) do
        set_world_row_codes(row.y, row.x, row.codes)
    end
end

local function load_baked_world_rows()
    local raw_rows = {}
    for line in string.gmatch(BAKED_WORLD_ROWS, "[^\r\n]+") do
        local y_text, codes = string.match(line, "^(%d+):(.+)$")
        if y_text and codes then
            local y = tonumber(y_text)
            raw_rows[y] = {}
            set_world_row_codes(y, 0, codes)
            local x = 0
            for code in string.gmatch(codes, ".") do
                local block_id = nil
                if code == "." then block_id = 0 end
                if code == "d" then block_id = 1 end
                if code == "h" then block_id = 2 end
                if code == "m" then block_id = 3 end
                if code == "w" then block_id = 4 end
                if code == "r" then block_id = 5 end
                if code == "f" then block_id = 6 end
                if code == "s" then block_id = 7 end
                if code == "t" then block_id = 8 end
                if code == "v" then block_id = 9 end
                if code == "c" then block_id = 10 end
                if code == "b" then block_id = 11 end
                if code == "S" then block_id = 12 end
                raw_rows[y][x] = block_id
                x = x + 1
            end
        end
    end
    return raw_rows
end

local WORLD_PATCHES = {
    opening_region = {
        name = "Opening Region",
        notes = {
            "Tantegel, Brecconary, Garinham, and Erdrick's Cave region.",
            "Fill only with verified terrain rows.",
            "Use terrain codes from WORLD_TERRAIN_CODES.",
        },
        rows = {
            -- Garinham northwest corner anchor patch.
            -- Player screenshot anchor: standing two north of Garinham at roughly (2, 0).
            { y = 0, x = 0, codes = "ww.w........" },
            { y = 1, x = 0, codes = "wwww........" },
            { y = 2, x = 0, codes = "wwt........." },
            { y = 3, x = 0, codes = "ww.........."},
            { y = 4, x = 0, codes = "wwfffffff..." },
            { y = 5, x = 0, codes = "ww....t....." },
            { y = 6, x = 0, codes = "wwfffffff..." },
            { y = 7, x = 0, codes = "wwfffffff..." },
            { y = 8, x = 0, codes = "wwfffffff.mm" },
            { y = 9, x = 0, codes = "wwfffffff..." },
            { y = 10, x = 0, codes = "wwfffffffmmm" },
        },
    },
}

local function seed_world_landmark_tiles()
    for _, landmark in ipairs(WORLD_MAP_GRID.landmarks or {}) do
        set_world_cell(landmark.x, landmark.y, landmark.kind)
    end
end

if CONFIG.use_baked_world_grid then
    load_baked_world_rows()
else
    apply_world_patch(WORLD_PATCHES.opening_region)
end
seed_world_landmark_tiles()

return WORLD_MAP_GRID
