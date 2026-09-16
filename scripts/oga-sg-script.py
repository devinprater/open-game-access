#!/usr/bin/env python3
"""
oga-sg-script.py — parse the Steins;Gate: My Darling's Embrace script format.

⭐ THE FORMAT, verified against the English-patched file:

    records are separated by a NUL byte
    within a record, SHIFT-JIS control pairs delimit the fields:

        81 6B  <name>     speaker name ("Rintaro", "???", ...)
        81 6C             delimiter after the speaker name
        81 67  <text>     the spoken line (narration, or the character's words)
        81 68  "%K%P"     end-of-box marker; %%K%%P is a PAGE BREAK in the text
        NUL               end of record

    A record may have NO 81 6B pair — that is narration / inner monologue, and the
    game shows it without a name plate. That is exactly why the raw RAM view looked
    like lines starting with odd letters ('g', 'C', 'A'): those were unmapped byte
    values, not speaker codes.

⛔ DO NOT PARSE THIS AS PLAIN ASCII. 81 6B / 81 67 / 81 68 are multi-byte Shift-JIS
control pairs that happen to sit between ASCII runs, so a naive ASCII scan yields
lines split at the wrong places and invents leading letters that are not in the game.

Usage:
  oga-sg-script.py DATA0.CPK --find "paper cups"        show the record containing text
  oga-sg-script.py DATA0.CPK --dump --from 0x442100 --count 20
  oga-sg-script.py DATA0.CPK --stats                    how many controls, sanity check
"""
import argparse
import re
import sys

# Shift-JIS control pairs used as delimiters in this game's script
C_NAME = b"\x81\x6b"      # speaker name follows
C_SEP = b"\x81\x6c"       # delimiter after the speaker name
C_TEXT = b"\x81\x67"      # the line itself follows
C_END = b"\x81\x68"       # end of box; "%K%P" inside the text is a page break

REC_SEP = b"\x00"


def split_records(buf):
    """Yield (offset, record_bytes) for each NUL-separated record."""
    start = 0
    for m in re.finditer(re.escape(REC_SEP), buf):
        yield start, buf[start:m.start()]
        start = m.end()


def decode_record(rec):
    """
    Return (name, text) or (None, text) for narration.

    ⛔ SPLIT ON THE CONTROL BYTES, NOT ON ASCII. Using the controls is what makes the
    'g'/'C' prefix artefacts disappear.
    """
    name = None
    text_parts = []

    # a record may hold several boxes (multiple 81 67 runs); join them for a reader,
    # but keep the page-break markers so a caller can split on them.
    pos = 0
    while pos < len(rec):
        if rec.startswith(C_NAME, pos):
            pos += len(C_NAME)
            end = pos
            while end < len(rec) and not (rec.startswith(C_SEP, end) or
                                          rec.startswith(C_TEXT, end) or
                                          rec.startswith(C_END, end)):
                end += 1
            chunk = rec[pos:end]
            if name is None:
                name = _text(chunk)
            pos = end
        elif rec.startswith(C_SEP, pos):
            pos += len(C_SEP)
        elif rec.startswith(C_TEXT, pos):
            pos += len(C_TEXT)
            end = pos
            while end < len(rec) and not (rec.startswith(C_END, end) or
                                          rec.startswith(C_NAME, end)):
                end += 1
            text_parts.append(_text(rec[pos:end]))
            pos = end
        elif rec.startswith(C_END, pos):
            pos += len(C_END)
        else:
            pos += 1

    text = " ".join(p for p in text_parts if p)
    return name, text


def _text(raw):
    raw = raw.strip(b" ")
    try:
        s = raw.decode("utf-8")
    except UnicodeDecodeError:
        s = raw.decode("shift_jis", "replace")
    # ⛔ %K%P IS A PAGE BREAK, not literal text. Left in, a reader would speak the
    # percent signs aloud in the middle of a sentence.
    s = s.replace("%K%P", " ").replace("%P", " ").replace("%K", " ")
    return re.sub(r"\s+", " ", s).strip()


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("cpk")
    ap.add_argument("--find", help="show the record containing this text")
    ap.add_argument("--dump", action="store_true")
    ap.add_argument("--from", dest="start", default=None, help="byte offset to start")
    ap.add_argument("--count", type=int, default=20)
    ap.add_argument("--stats", action="store_true")
    args = ap.parse_args()

    # read a window, not the whole 778 MB, unless finding
    if args.find:
        needle = args.find.encode()
        with open(args.cpk, "rb") as f:
            chunk = 16 * 1024 * 1024
            off = 0
            while True:
                b = f.read(chunk)
                if not b:
                    print("not found"); return 1
                i = b.find(needle)
                if i >= 0:
                    # widen to the record boundaries around the hit
                    lo = max(0, i - 0x400)
                    hi = min(len(b), i + 0x400)
                    win = b[lo:hi]
                    base = off + lo
                    for roff, rec in split_records(win):
                        if needle in rec:
                            name, text = decode_record(rec)
                            print(f"@0x{base + roff:X}")
                            print(f"  speaker: {name!r}" if name else "  speaker: (narration)")
                            print(f"  text   : {text!r}")
                            return 0
                    print("hit found but no record contained it; widen the window")
                    return 1
                off += len(b)

    start = int(args.start, 0) if args.start else 0
    span = args.count * 0x400 if args.dump else 0x20000
    with open(args.cpk, "rb") as f:
        f.seek(start)
        buf = f.read(span)

    if args.stats:
        print(f"window @0x{start:X} ({len(buf)} bytes)")
        for label, c in (("81 6B name", C_NAME), ("81 6C sep", C_SEP),
                         ("81 67 text", C_TEXT), ("81 68 end", C_END)):
            print(f"  {label:<12} {buf.count(c)}")
        return 0

    # dump records
    shown = 0
    for roff, rec in split_records(buf):
        if not rec:
            continue
        name, text = decode_record(rec)
        if not text:
            continue
        who = name if name else "(narration)"
        print(f"@0x{start + roff:08X}  {who:<12} {text[:96]}")
        shown += 1
        if shown >= args.count:
            break
    return 0


if __name__ == "__main__":
    sys.exit(main())
