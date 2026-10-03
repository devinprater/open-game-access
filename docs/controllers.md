# Playing with a game controller

Connect any controller iOS supports as a gamepad: MFi controllers, PlayStation
(DualSense, DualShock 4), Xbox and similar. The app says "connected" and hides the
on-screen game buttons. The reader, speech and Quit controls stay on screen.
Disconnecting brings the on-screen buttons back.

## Game buttons

| Controller | Game |
|---|---|
| D-pad or left stick | Directions |
| L1 / R1 | L / R |
| Menu (Options on PlayStation) | Start |
| View / Create button | Select |

Face buttons follow the **position** of the console's own buttons, not the letter
printed on the controller:

| Position | DS | Game Boy / GBA | PSP |
|---|---|---|---|
| Right | A | A | Circle |
| Bottom | B | B | Cross |
| Top | X | — | Triangle |
| Left | Y | — | Square |

L2 and R2 never reach the game. They are held for reader commands.

## Reader commands

Hold a trigger, then press a button. The game does not receive that press.

**Hold L2 — ask** (right thumb on the face buttons)

| Press | Command |
|---|---|
| Bottom (Cross) | Where am I |
| Right (Circle) | Stop speech |
| Top (Triangle) | Repeat the last thing said |
| Left (Square) | Find path |
| D-pad down | Tiles around you |

**Hold R2 — browse** (left thumb on the D-pad)

| Press | Command |
|---|---|
| D-pad left / right | Previous / next item (party members, list entries) |
| D-pad up / down | Previous / next group (enemies, groups of items) |
| Bottom (Cross) | Read the item again (Fire Emblem: next ally who has not acted) |
| Top (Triangle) | Game toggle 1 — Dissidia: Customize menu |
| Left (Square) | Game toggle 2 — Dissidia: character select |
| Right (Circle) | Game toggle 3 — Dissidia: name entry |

Each command uses the game's own reader when it has one, otherwise the Pokémon
script's key. A combination with nothing behind it for the current game says
"No reader command there for this game." Holding a trigger and pressing a D-pad
direction with no command still moves normally.
