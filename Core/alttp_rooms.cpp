/*
 * alttp_rooms.cpp — what each dungeon room holds, generated; do not edit.
 *
 * Source: the zelda3 decompilation's per-room database (assets/dungeon/*.yaml),
 * which is itself extracted from the USA 1.0 cartridge. Only derived facts are
 * here: how many chests, whether one is a big chest, and the editor's own
 * one-line note about the room's puzzle. No room layout, sprite list or ROM data
 * is copied.
 *
 * Regenerate with scripts/gen-alttp-rooms.py; the room index is the game's own
 * dungeon_room_index (0..319).
 */
#include "alttp_rooms.h"

namespace oga {
namespace {

struct RoomRow { uint8_t chests; uint8_t big; const char* note; };

const RoomRow kRooms[320] = {
    { 0, 0, "Kill to open Ganon's door" },  // 0
    { 0, 0, nullptr },  // 1
    { 0, 0, "Pull lever to open" },  // 2
    { 0, 0, nullptr },  // 3
    { 1, 0, "Kill enemy to move block" },  // 4
    { 1, 0, "Kill enemy to clear level" },  // 5
    { 0, 0, "Kill enemy to clear level" },  // 6
    { 0, 0, "Kill enemy to clear level" },  // 7
    { 0, 0, nullptr },  // 8
    { 1, 0, "Trigger activated chest" },  // 9
    { 1, 0, "Trigger activated chest" },  // 10
    { 0, 0, "Kill enemy to open" },  // 11
    { 0, 0, nullptr },  // 12
    { 0, 0, "Agahnim's room" },  // 13
    { 0, 0, "Kill enemy to open" },  // 14
    { 0, 0, nullptr },  // 15
    { 0, 0, nullptr },  // 16
    { 3, 0, nullptr },  // 17
    { 1, 0, nullptr },  // 18
    { 0, 0, nullptr },  // 19
    { 1, 0, nullptr },  // 20
    { 0, 0, nullptr },  // 21
    { 2, 0, "Moving water" },  // 22
    { 0, 0, "Holes(4)" },  // 23
    { 0, 0, nullptr },  // 24
    { 2, 0, nullptr },  // 25
    { 3, 1, "Switch opens door(Hold)" },  // 26
    { 0, 0, "Kill enemy to open" },  // 27
    { 3, 0, "Kill enemy to open" },  // 28
    { 0, 0, nullptr },  // 29
    { 0, 0, "Switch opens door(Toggle)" },  // 30
    { 1, 0, "Switch opens door(Toggle)" },  // 31
    { 0, 0, "Clear level to open door" },  // 32
    { 0, 0, nullptr },  // 33
    { 0, 0, nullptr },  // 34
    { 0, 0, nullptr },  // 35
    { 1, 1, "Kill enemy to open" },  // 36
    { 0, 0, "Switch opens door(Hold)" },  // 37
    { 0, 0, "Switch opens door(Hold)" },  // 38
    { 2, 1, "Holes(5)" },  // 39
    { 1, 0, "Clear room for chest" },  // 40
    { 0, 0, "Kill enemy to clear level" },  // 41
    { 2, 0, nullptr },  // 42
    { 1, 0, "Switch opens door(Hold)" },  // 43
    { 0, 0, nullptr },  // 44
    { 0, 0, "Kill enemy for chest" },  // 45
    { 1, 0, "Kill enemy for chest" },  // 46
    { 5, 0, nullptr },  // 47
    { 0, 0, nullptr },  // 48
    { 0, 0, "Holes(6)" },  // 49
    { 1, 0, nullptr },  // 50
    { 0, 0, "Clear level to open door" },  // 51
    { 1, 0, nullptr },  // 52
    { 1, 0, nullptr },  // 53
    { 1, 1, nullptr },  // 54
    { 1, 0, nullptr },  // 55
    { 0, 0, nullptr },  // 56
    { 0, 0, nullptr },  // 57
    { 1, 0, nullptr },  // 58
    { 0, 0, nullptr },  // 59
    { 4, 0, nullptr },  // 60
    { 3, 0, "Light torches to open" },  // 61
    { 0, 0, "Kill enemy to open" },  // 62
    { 1, 0, "Trigger activated chest" },  // 63
    { 0, 0, nullptr },  // 64
    { 0, 0, "Move block to open" },  // 65
    { 0, 0, nullptr },  // 66
    { 0, 0, nullptr },  // 67
    { 1, 1, "Kill enemy to open" },  // 68
    { 1, 0, "Kill enemy to open" },  // 69
    { 1, 0, "Move block to get chest" },  // 70
    { 0, 0, "Light torches to open" },  // 71
    { 0, 0, "Light torches to open" },  // 72
    { 0, 0, "Light torches to open" },  // 73
    { 0, 0, "Switch opens door(Toggle)" },  // 74
    { 0, 0, "Kill enemy to open" },  // 75
    { 0, 0, nullptr },  // 76
    { 1, 0, "Clear room for chest" },  // 77
    { 0, 0, "Switch opens door(Toggle)" },  // 78
    { 0, 0, nullptr },  // 79
    { 0, 0, nullptr },  // 80
    { 0, 0, nullptr },  // 81
    { 0, 0, nullptr },  // 82
    { 0, 0, "Kill enemy to open" },  // 83
    { 0, 0, nullptr },  // 84
    { 1, 0, nullptr },  // 85
    { 0, 0, "Holes(1)" },  // 86
    { 2, 0, "Switch opens door(Hold)" },  // 87
    { 2, 1, "Holes(0)" },  // 88
    { 1, 0, nullptr },  // 89
    { 0, 0, "Kill enemy to clear level" },  // 90
    { 1, 0, "Switch opens door(Toggle)" },  // 91
    { 0, 0, nullptr },  // 92
    { 0, 0, "Clear quadrant to open" },  // 93
    { 0, 0, nullptr },  // 94
    { 1, 0, "Trigger activated chest" },  // 95
    { 0, 0, nullptr },  // 96
    { 0, 0, nullptr },  // 97
    { 0, 0, nullptr },  // 98
    { 0, 0, "Move block to open" },  // 99
    { 0, 0, "Switch opens door(Toggle)" },  // 100
    { 1, 0, nullptr },  // 101
    { 1, 0, "Move block to get chest" },  // 102
    { 1, 0, "Open chest for holes(0)" },  // 103
    { 1, 0, nullptr },  // 104
    { 0, 0, nullptr },  // 105
    { 2, 0, nullptr },  // 106
    { 0, 0, "Kill enemy to open" },  // 107
    { 0, 0, "Kill enemy to open" },  // 108
    { 0, 0, "Kill enemy to open" },  // 109
    { 0, 0, "Kill enemy to open" },  // 110
    { 0, 0, nullptr },  // 111
    { 0, 0, nullptr },  // 112
    { 1, 0, "Kill enemy to open" },  // 113
    { 1, 0, nullptr },  // 114
    { 1, 1, "Switch opens door(Toggle)" },  // 115
    { 1, 0, "Trigger activated chest" },  // 116
    { 1, 0, "Kill enemy to open" },  // 117
    { 2, 0, nullptr },  // 118
    { 1, 0, nullptr },  // 119
    { 0, 0, "Kill enemy to open" },  // 120
    { 0, 0, "Kill enemy to open" },  // 121
    { 0, 0, "Kill enemy to open" },  // 122
    { 4, 0, "Kill enemy to open" },  // 123
    { 4, 0, "Use switch to bomb wall" },  // 124
    { 1, 0, "Kill enemy to open" },  // 125
    { 2, 0, "Kill enemies for chest" },  // 126
    { 0, 0, nullptr },  // 127
    { 1, 0, nullptr },  // 128
    { 0, 0, nullptr },  // 129
    { 0, 0, nullptr },  // 130
    { 0, 0, "Move block to open" },  // 131
    { 0, 0, nullptr },  // 132
    { 1, 0, "Kill enemy to open" },  // 133
    { 0, 0, "Light torches to get chest" },  // 134
    { 1, 0, "Light torches to get chest" },  // 135
    { 0, 0, nullptr },  // 136
    { 0, 0, nullptr },  // 137
    { 0, 0, "Holes(8)" },  // 138
    { 1, 0, "Holes(8)" },  // 139
    { 4, 1, "Switch opens door(Hold)" },  // 140
    { 1, 0, "Light torches to open" },  // 141
    { 0, 0, nullptr },  // 142
    { 0, 0, "Kill enemy to clear level" },  // 143
    { 0, 0, "Kill enemy to clear level" },  // 144
    { 0, 0, nullptr },  // 145
    { 0, 0, nullptr },  // 146
    { 0, 0, "Switch opens door(Hold)" },  // 147
    { 0, 0, nullptr },  // 148
    { 0, 0, nullptr },  // 149
    { 0, 0, "Light torches to open" },  // 150
    { 0, 0, "Secret wall (Right)" },  // 151
    { 0, 0, nullptr },  // 152
    { 0, 0, nullptr },  // 153
    { 0, 0, nullptr },  // 154
    { 0, 0, nullptr },  // 155
    { 0, 0, "Light torch to see floor" },  // 156
    { 4, 0, "Light torch to see floor" },  // 157
    { 1, 1, nullptr },  // 158
    { 0, 0, "Switch opens door(Toggle)" },  // 159
    { 0, 0, nullptr },  // 160
    { 0, 0, nullptr },  // 161
    { 1, 0, nullptr },  // 162
    { 0, 0, nullptr },  // 163
    { 0, 0, "Kill enemy to clear level" },  // 164
    { 0, 0, "Kill enemy to open" },  // 165
    { 0, 0, nullptr },  // 166
    { 0, 0, nullptr },  // 167
    { 1, 0, "Kill enemy to open" },  // 168
    { 1, 1, "Switch opens door(Toggle)" },  // 169
    { 1, 0, "Switch opens door(Toggle)" },  // 170
    { 0, 0, nullptr },  // 171
    { 0, 0, "Kill enemy to clear level" },  // 172
    { 0, 0, "Trigger activated chest" },  // 173
    { 1, 0, "Trigger activated chest" },  // 174
    { 0, 0, nullptr },  // 175
    { 0, 0, "Kill enemy to open" },  // 176
    { 0, 0, nullptr },  // 177
    { 0, 0, "Kill enemy to open" },  // 178
    { 2, 0, "Trigger activated chest" },  // 179
    { 0, 0, nullptr },  // 180
    { 0, 0, "Switch opens door(Toggle)" },  // 181
    { 1, 0, "Kill enemy to open" },  // 182
    { 2, 0, nullptr },  // 183
    { 1, 0, "Trigger activated chest" },  // 184
    { 1, 0, nullptr },  // 185
    { 0, 0, "Switch opens door(Toggle)" },  // 186
    { 0, 0, nullptr },  // 187
    { 0, 0, "Switch opens door(Toggle)" },  // 188
    { 0, 0, "Switch opens door(Hold)" },  // 189
    { 0, 0, "Switch opens door(Hold)" },  // 190
    { 0, 0, nullptr },  // 191
    { 0, 0, "Kill enemy to open" },  // 192
    { 1, 0, "Light torches to open" },  // 193
    { 1, 0, "Trigger activated chest" },  // 194
    { 2, 1, nullptr },  // 195
    { 0, 0, nullptr },  // 196
    { 0, 0, nullptr },  // 197
    { 0, 0, nullptr },  // 198
    { 0, 0, "Light torches to open" },  // 199
    { 0, 0, "Clear level to open door" },  // 200
    { 0, 0, "Switch opens door(Toggle)" },  // 201
    { 0, 0, nullptr },  // 202
    { 1, 0, nullptr },  // 203
    { 0, 0, nullptr },  // 204
    { 0, 0, "Pull lever to open" },  // 205
    { 0, 0, "Pull lever to open" },  // 206
    { 0, 0, "Move block to open" },  // 207
    { 1, 0, "Move block to open" },  // 208
    { 1, 0, nullptr },  // 209
    { 0, 0, "Clear room to open" },  // 210
    { 0, 0, "Kill enemy to open" },  // 211
    { 0, 0, "Kill enemy to open" },  // 212
    { 4, 0, nullptr },  // 213
    { 1, 0, nullptr },  // 214
    { 0, 0, "Kill enemy to open" },  // 215
    { 0, 0, "Kill enemy to open" },  // 216
    { 0, 0, "Switch opens door(Toggle)" },  // 217
    { 0, 0, "Switch opens door(Toggle)" },  // 218
    { 2, 0, nullptr },  // 219
    { 1, 0, nullptr },  // 220
    { 0, 0, "Kill enemy to clear level" },  // 221
    { 0, 0, "Kill enemy to clear level" },  // 222
    { 0, 0, nullptr },  // 223
    { 1, 0, "Kill enemy to open" },  // 224
    { 0, 0, nullptr },  // 225
    { 0, 0, nullptr },  // 226
    { 0, 0, nullptr },  // 227
    { 0, 0, nullptr },  // 228
    { 0, 0, nullptr },  // 229
    { 0, 0, nullptr },  // 230
    { 0, 0, nullptr },  // 231
    { 0, 0, nullptr },  // 232
    { 0, 0, nullptr },  // 233
    { 0, 0, nullptr },  // 234
    { 0, 0, nullptr },  // 235
    { 0, 0, nullptr },  // 236
    { 0, 0, nullptr },  // 237
    { 0, 0, nullptr },  // 238
    { 5, 0, "Kill enemy to open" },  // 239
    { 0, 0, nullptr },  // 240
    { 0, 0, nullptr },  // 241
    { 0, 0, nullptr },  // 242
    { 0, 0, nullptr },  // 243
    { 0, 0, nullptr },  // 244
    { 0, 0, nullptr },  // 245
    { 0, 0, nullptr },  // 246
    { 0, 0, nullptr },  // 247
    { 2, 0, nullptr },  // 248
    { 0, 0, nullptr },  // 249
    { 0, 0, nullptr },  // 250
    { 0, 0, nullptr },  // 251
    { 0, 0, nullptr },  // 252
    { 0, 0, nullptr },  // 253
    { 1, 0, nullptr },  // 254
    { 2, 0, nullptr },  // 255
    { 0, 0, nullptr },  // 256
    { 0, 0, nullptr },  // 257
    { 0, 0, nullptr },  // 258
    { 1, 0, nullptr },  // 259
    { 1, 0, nullptr },  // 260
    { 3, 0, nullptr },  // 261
    { 1, 0, nullptr },  // 262
    { 0, 0, nullptr },  // 263
    { 1, 0, nullptr },  // 264
    { 0, 0, nullptr },  // 265
    { 1, 0, nullptr },  // 266
    { 1, 0, nullptr },  // 267
    { 1, 0, "Kill enemy to open" },  // 268
    { 2, 0, nullptr },  // 269
    { 0, 0, nullptr },  // 270
    { 0, 0, nullptr },  // 271
    { 0, 0, nullptr },  // 272
    { 0, 0, nullptr },  // 273
    { 0, 0, nullptr },  // 274
    { 1, 0, nullptr },  // 275
    { 0, 0, nullptr },  // 276
    { 0, 0, nullptr },  // 277
    { 0, 0, nullptr },  // 278
    { 1, 0, nullptr },  // 279
    { 0, 0, nullptr },  // 280
    { 0, 0, nullptr },  // 281
    { 0, 0, nullptr },  // 282
    { 0, 0, nullptr },  // 283
    { 1, 0, nullptr },  // 284
    { 5, 0, nullptr },  // 285
    { 4, 0, nullptr },  // 286
    { 0, 0, nullptr },  // 287
    { 1, 0, nullptr },  // 288
    { 0, 0, nullptr },  // 289
    { 0, 0, nullptr },  // 290
    { 4, 0, "Kill enemy to open" },  // 291
    { 1, 0, nullptr },  // 292
    { 0, 0, nullptr },  // 293
    { 0, 0, nullptr },  // 294
    { 0, 0, nullptr },  // 295
    { 0, 0, nullptr },  // 296
    { 0, 0, nullptr },  // 297
    { 0, 0, nullptr },  // 298
    { 0, 0, nullptr },  // 299
    { 0, 0, nullptr },  // 300
    { 1, 0, nullptr },  // 301
    { 0, 0, nullptr },  // 302
    { 0, 0, nullptr },  // 303
    { 0, 0, nullptr },  // 304
    { 0, 0, nullptr },  // 305
    { 0, 0, nullptr },  // 306
    { 0, 0, nullptr },  // 307
    { 0, 0, nullptr },  // 308
    { 0, 0, nullptr },  // 309
    { 0, 0, nullptr },  // 310
    { 0, 0, nullptr },  // 311
    { 0, 0, nullptr },  // 312
    { 0, 0, nullptr },  // 313
    { 0, 0, nullptr },  // 314
    { 0, 0, nullptr },  // 315
    { 0, 0, nullptr },  // 316
    { 1, 0, nullptr },  // 317
    { 0, 0, nullptr },  // 318
    { 0, 0, nullptr },  // 319
};

} // namespace

const RoomNote* AlttpRoomNote(uint16_t room)
{
    static RoomNote out;
    if (room >= 320) return nullptr;
    const RoomRow& row = kRooms[room];
    if (row.chests == 0 && !row.note) return nullptr;
    out.chests = row.chests;
    out.big = row.big != 0;
    out.note = row.note;
    return &out;
}

} // namespace oga
