# Dissidia Battle Setup value lists (host-mapped, ULUS10437)

Rows: Enter Battle, STAGE, STRENGTH(CPU), LEVEL(CPU), BEHAVIOR(CPU).
Down path: Enter Battle -> STAGE -> STRENGTH -> LEVEL -> BEHAVIOR ->
STAGE (the four value rows loop; Enter Battle is NOT in the down-loop).
Up reverses, including STAGE -> Enter Battle (verified). Up from Enter
Battle untested (presumed BEHAVIOR; reader announces the row anyway).
Harness buttons: Left 5, Right 4, Up 6, Down 7 (all verified 1:1).

Every value list wraps both directions; cursor stays on its row.
Help line (bottom center) names the cursor row and doubles as the
future reader's cursor signal:
  Enter Battle "Enter battle.", STAGE "Select a stage.",
  STRENGTH "Set your opponent's strength.",
  LEVEL "Set your opponent's level.",
  BEHAVIOR "Select your opponent's behavior."

STAGE (12, all unlocked on this save): Random, Old Chaos Shrine,
Pandaemonium, World of Darkness, Lunar Subterrane, The Rift,
Kefka's Tower, Planet's Core, Ultimecia's Castle, Crystal World,
Dream's End, Order's Sanctuary. Right from 12:12 wraps to Random 1:12;
Left from Random wraps to Order's Sanctuary 12:12.

STRENGTH(CPU) (9): Minimal, Very Low, Low (Actions), Low (Equipment),
Average (default 5:9), High (Equipment), High (Actions), Very High,
Maximum. Wraps both ends (Left from Minimal -> Maximum verified).

LEVEL(CPU) (2): Auto (matches player level), 1 (fixed CPU Lv1).
Switching Auto->1 visibly drops the CPU banner (Lv4->Lv1 at Very High).
Under Auto the CPU level scales with STRENGTH: ~Lv1 at 7:9 and below,
Lv4 at Very High 8:9, Lv6 at Maximum 9:9.

BEHAVIOR(CPU) (9): Random, Tactician, Valiant, Survivor, Cautious,
Calm, Extreme, Conservative, Vicious. Right from Vicious 9:9 wraps to
Random 1:9, cursor stays.

METHODS NOTE: long multi-tap runs (12-13 inputs, 600-frame period)
gave unreliable mid-run states (setup1/setup4: cursor/value
combinations no single-step chain reproduces), so every value above
was verified by single-step chains instead: setup5->setup19 resume a
proven savestate, apply 1-4 inputs, read one screenshot with its
fraction. Suspect multi-tap input loss after resume, not game logic;
do not trust long scripted walks for exact states without a
per-step screenshot.
