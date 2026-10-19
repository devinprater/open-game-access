"""Extract the four cup rental tables from the Stadium 1 ROM.

Record layout (84 bytes):
  +0x00  four move ids (up, right, down, left)
  +0x1B  level
  +0x1D  five u16 big-endian stats: HP, Attack, Defense, Speed, Special
  +0x27  ten-byte ASCII name
Move names are packed null-terminated strings at 0x796DD8.
"""
import json
import struct

ROM = r"C:\Users\Devin Prater\Dropbox\games\N64\Pokemon Stadium (USA) (Rev 2).z64"
data = open(ROM, "rb").read()

# Move names: 165 null-terminated strings.
names = []
p = 0x796DD8
while len(names) < 165:
    e = data.find(b"\x00", p)
    names.append(data[p:e].decode("ascii"))
    p = e + 1
assert names[0] == "POUND" and names[72] == "LEECH SEED" and names[91] == "TOXIC"

CUPS = {
    "pika": 0x8A735D,
    "petit": 0x8A648D,
    "poke": 0x8AC22D,
    "prime": 0x8A913D,
}

out = {}
for cup, base in CUPS.items():
    entries = []
    for n in range(151):
        off = base + n * 0x54
        raw_name = data[off + 0x27:off + 0x31].split(b"\x00")[0]
        level_probe = data[off + 0x1B]
        ok = (3 <= len(raw_name) <= 10 and level_probe in (15, 20, 25, 30, 50, 100)
              and sum(1 for c in raw_name if 65 <= c <= 90 or 97 <= c <= 122) >= 3)
        if not ok:
            break
        level = data[off + 0x1B]
        hp, atk, df, spe, spc = struct.unpack(">5H", data[off + 0x1D:off + 0x27])
        moves = list(data[off:off + 4])
        entries.append({
            "dex": len(entries) + 1,
            "name": raw_name.decode("latin-1").title(),
            "level": level,
            "hp": hp, "attack": atk, "defense": df, "speed": spe, "special": spc,
            "moves": [names[m - 1].title() if 1 <= m <= len(names) else None for m in moves],
            "move_ids": moves,
        })
    out[cup] = entries
    print(cup, len(entries), entries[0]["name"], entries[0]["level"],
          entries[-1]["name"], entries[-1]["level"])

json.dump(out, open("rentals.json", "w", encoding="utf-8"), indent=1)
print("wrote rentals.json")
