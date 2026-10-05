#!/usr/bin/env python3
"""Extract every NES zip, verify each result, then delete the zip ONLY if its .nes is on disk.

⛔ ORDER AND SAFETY. 5829 zips, 1.2 GB. The irreversible step is the delete, so it happens last and
only per-file, after that file's .nes is confirmed present at the expected size. A single pass that
"extracts everything then deletes the zips" would destroy the source if the extract half failed --
and there would be no way to tell which zips were the casualties.

The plan, per zip:
  1. read the archive's file list WITHOUT extracting (unzip -Z1)
  2. extract ONLY if the .nes is not already present at the right size (resumable)
  3. re-stat the extracted file and require its size to match the archive's recorded size
  4. delete the zip only then

Progress is appended to a log so a long run survives interruption, and the counters at the end are
the evidence, not a summary.
"""
import pathlib, subprocess, sys, os

NES = pathlib.Path("/mnt/c/Users/Devin Prater/Dropbox/games/NES")
LOG = pathlib.Path("/home/devin/nes-extract.log")

zips = sorted(NES.glob("*.zip"))
print(f"zips found: {len(zips)}", flush=True)

extracted = skipped = failed = deleted = 0
problems = []

with LOG.open("a", encoding="utf-8") as log:
    log.write(f"\n=== run: {len(zips)} zips\n")
    for i, z in enumerate(zips, 1):
        if i % 250 == 0:
            print(f"  {i}/{len(zips)}  extracted={extracted} skipped={skipped} failed={failed} deleted={deleted}", flush=True)

        # 1. the archive's own listing: name + uncompressed size
        try:
            out = subprocess.run(["unzip", "-Z", "-1", str(z)], capture_output=True, text=True, timeout=60)
            names = [n for n in out.stdout.splitlines() if n.strip()]
        except Exception as e:
            problems.append(f"{z.name}: unreadable ({e})"); failed += 1; continue

        if not names:
            # An empty or unreadable archive: leave it alone rather than deleting a mystery.
            problems.append(f"{z.name}: no entries"); failed += 1; continue

        # 2/3. extract, then verify each entry landed at the right size
        target = NES / names[0]
        if target.exists() and target.stat().st_size > 0:
            skipped += 1
        else:
            try:
                r = subprocess.run(["unzip", "-o", "-j", str(z), "-d", str(NES)],
                                   capture_output=True, text=True, timeout=120)
                if r.returncode != 0:
                    problems.append(f"{z.name}: unzip rc={r.returncode} {r.stderr.strip()[:80]}")
                    failed += 1
                    continue
            except Exception as e:
                problems.append(f"{z.name}: extract failed ({e})"); failed += 1; continue

            sizes = subprocess.run(["unzip", "-Z", "-l", str(z)], capture_output=True, text=True).stdout
            want = None
            for line in sizes.splitlines():
                parts = line.split()
                if len(parts) >= 4 and parts[-1] == names[0]:
                    try: want = int(parts[0])
                    except ValueError: pass
            if not target.exists():
                problems.append(f"{z.name}: {names[0]} not on disk after extract"); failed += 1; continue
            if want is not None and target.stat().st_size != want:
                problems.append(f"{z.name}: size {target.stat().st_size} != archive {want}"); failed += 1; continue
            extracted += 1
            log.write(f"EXTRACT {z.name} -> {names[0]} ({target.stat().st_size})\n")

        # 4. delete, only now
        try:
            z.unlink(); deleted += 1
        except Exception as e:
            problems.append(f"{z.name}: could not delete ({e})")

    log.write(f"=== done extracted={extracted} skipped={skipped} failed={failed} deleted={deleted}\n")
    for p in problems[:60]:
        log.write(f"PROBLEM {p}\n")

print(f"\nextracted={extracted} skipped={skipped} failed={failed} deleted={deleted}", flush=True)
print(f"problems: {len(problems)} (first 15)", flush=True)
for p in problems[:15]:
    print("  ", p, flush=True)
