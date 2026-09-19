# TASK10B — Stage-service address + bias resolution (follows TASK10/ANSWER10)

Live reads during an ACTIVE battle (all real bytes, retried):

- World root + aux (DAT bias +0x08804000, proven): 0x08B963C0+0x180 = 0x08B7587C,
  +0x790 = 0x08BFB318 (non-null); aux 0x08B95230+0 = 0x09EDF4B0 (heap ptr).
  These validate.
- Your E=0x08805E48 / F=0x08805AAC (data bias): GARBAGE (counts in the billions,
  pointers outside RAM).
- Same addresses with code bias (+0x08800000: E=0x08801E48, F=0x08801AAC): clean
  ZEROS (env count 0/items null; field pairs 0/null).

Suspected cause: two live biases (code +0x08800000, data +0x08804000 — the board
holder file 0x00394940 == RAM 0x08B98940 proves data bias). "Constructs the object
at vaddr 0x1AAC" may mean the CONSTRUCTOR CODE lives there while the object is
heap/static elsewhere.

## Ask

1. For FUN_00030A18/FUN_00066314: is 0x1AAC/0x1E48 code or the object? If the object,
in which segment, and what is its LIVE ram address under each bias?
2. Re-issue the validator reads with explicit per-address bias so the numbers above
are explained (garbage vs zeros).
3. Keep the honest-negative core: only claim what two references support.

Append the correction to ANSWER10.md (new section, keep the old).
