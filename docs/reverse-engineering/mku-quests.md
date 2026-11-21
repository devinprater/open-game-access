# MK: Unchained — the Konquest quest scripts in the decomp

Read from `kon_earthrealm_1.ssf` (the Earthrealm Konquest resource) section 5,
the 9,377-string script bank. Every village quest is a family of named script
entries:

```
LS_<QUEST>_<STATE>_<line>
```

The states repeat across quests and say what the game is doing:

| State | Meaning |
|---|---|
| `GREETING_n` | what the NPC says when you first talk to them |
| `BOTHER_n` | what they say when you talk again before the task is done |
| `PUNCHED_n` | their reaction to being hit — this is how the toothache quest completes |
| `AFTER_n` | what they say once the quest is done |
| `WAITING_n` | idle lines |
| `GOTRING`, `GIVE`, `NOTE_QUEST` | quest-specific steps |

## Quest families in the Earthrealm village

| Prefix | Quest / NPC | Lines |
|---|---|---|
| `LS_LOOSETOOTH` | the old man's toothache; punch him to cure it | 11 |
| `LS_RING` | the pawned wedding ring | 35 |
| `LS_BLACKSMITH` | deliver the old woman's note | 10 |
| `LS_SICK` | the sick boy and his mother's medicine | 9 |
| `LS_OLDWOMAN` | the old woman and her key | 9 |
| `LS_OLDLADY` | the old lady by the fountain | 4 |
| `LS_PICKPOCKET` | the pickpocket | 9 |
| `LS_BAKER` | the baker's flour | 11 |
| `LS_BUTCHER` | the butcher's meat | 15 |
| `LS_DOG` | the dead dog | 21 |
| `LS_VASE` | the vase and the vase thief | 11 |
| `LS_BRACELET` | the lost bracelet | 4 |
| `LS_SMOKE` | the smoke-bomb boy | 3 |
| `LS_MEDITATE` / `LS_MEDITATION` | the meditating boy teaches Circle | 9 + 5 |
| `LS_AUCTION` | the auction | 13 |
| `LS_PEEPINGTOM` | the peeping tom at 2 AM | 15 |
| `LS_RACE` / `LS_GORO` | the race man and the Goro kid | 15 + 14 |
| `LS_GUARD` / `LS_BRIDGE` | the bridge guard and the toll | 29 + 15 |
| `LS_LIN`, `LS_MEKKO`, `LS_ONI`, `LS_NINJA` | later-village fights and items | 12, 8, 8, 4 |
| `LS_TEMPLE` | the temple | 3 |
| `LS_KONQ` | main-story beats | 50 |
| `LS_NIS` | cutscenes | 29 |
| `LS_WALKER`, `LS_NPC`, `LS_FIGHTING`, `LS_TEACHER` | generic villager chatter | 98, 65, 17, 6 |

## Quest object and reward names

The same bank names the objects the quests hand out or spawn:

`toothman_reward`, `ringman_ring`, `ringman_reward`, `old_lady_key`,
`blacksmiths_note`, `sickkid_reward`, `lost_key`, `ninja_star`,
`butchers_meat`, `dog_meat`, `bucket`, `empty_bucket`, `flour`,
`got_flour_reward`, `got_bucket_reward`, `got_oni_meat_reward`,
`pui_oni_meat`, `pui_vase`, `pui_vase_thief`, `vase_thief_reward`,
`lost_braclet_reward`, `dead_dog_reward`, `killed_ninja_reward`,
`park_tease_koin`, `pickpocket_koin`, `peep_koin`, `well_money`,
`locked_hut_koin_gold`, `old_lady_key`, `smoke_key`, `bully_kid_key`,
`key_chest_bgnd_portal`, `key_charalt_scorpion`, and the koin caches
`behind_house_*`, `behind_hut_*`, `park_koin_*`, `laundry_line_koin_1`.

## What this settles

**The toothache quest is the entry point.** It needs no item, no fetch and no
travel: the old man in the park at A-6 has `LS_LOOSETOOTH_GREETING_*`,
`LS_LOOSETOOTH_PUNCHED_*` and `LS_LOOSETOOTH_AFTER_*` entries, and the reward is
the named object `toothman_reward`. Landing one punch is the whole quest.

The trigger table (see `mortal-kombat-unchained.md`) gives the world positions
for the story beats; this bank gives the behaviour behind each NPC.
