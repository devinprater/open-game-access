/*
 * dbz_adapter.cpp — Dragon Ball Z: Attack of the Saiyans, as an Open Game Access
 * adapter.
 *
 * WHY THIS EXISTS: the map below was established by live measurement over a long
 * investigation (see docs/reverse-engineering/dbz-attack-of-the-saiyans.md), and
 * every address in it was confirmed against the game's OWN screens — the Status
 * page listing the party, and the per-member HP/KI readout. Nothing here is
 * inferred from a plausible-looking value.
 *
 * ⛔ THE VERIFIED MAP (do not adjust without re-confirming on screen):
 *
 *   character record array  (8 entries: Goku, Gohan, Piccolo, Krillin, Tien,
 *                            Yamcha, Bubbles, Gregory)
 *     base   0x020CD660        <- NOT 0x020CD754. An earlier reading used a base
 *     stride 0x24C                one whole stride too high, which made every
 *     name   +0x114               record decode to a plausible NEIGHBOUR.
 *
 *   party pointer array
 *     at     0x020CC774        <- NUL-terminated array of pointers to record BASES
 *     count  0x020CC794        <- low u16 == number of entries
 *
 *   per-record stats (u16, each stored three times: cur / max / display copy)
 *     +0x0A0  HP
 *     +0x0B0  KI
 *     +0x240  NEXT (exp to next level)
 *
 * ⛔ WHY THE PUBLISHED ACTION REPLAY CODES READ ZERO: the published base is
 * 0x020CD300, which is 0x360 below the true base. The list is right about the
 * SHAPE of this data (party stats at a 0x24C stride) and wrong about WHERE.
 *
 * ⛔ READ-ONLY. Like every other adapter here, this only inspects memory and
 * drives the real console's buttons when the player asks. It never writes RAM.
 */
#include "adapter.h"

#include <stdio.h>
#include <string.h>

namespace oga {
namespace {

const Host* g_host = nullptr;

// ---- the verified map ------------------------------------------------------
constexpr uint32_t REC_BASE   = 0x020CD660;
constexpr uint32_t REC_STRIDE = 0x24C;
constexpr uint32_t REC_COUNT  = 8;
constexpr uint32_t NAME_OFF   = 0x114;

constexpr uint32_t PARTY_PTR  = 0x020CC774;
constexpr uint32_t PARTY_CNT  = 0x020CC794;
constexpr uint32_t PARTY_MAX  = 4;      // the array is 4 slots + terminator

constexpr uint32_t OFF_HP     = 0x0A0;
constexpr uint32_t OFF_KI     = 0x0B0;
constexpr uint32_t OFF_NEXT   = 0x240;

uint16_t u16(uint32_t a) { return g_host ? g_host->read16(g_host->ctx, a) : 0; }
uint32_t u32(uint32_t a) { return g_host ? g_host->read32(g_host->ctx, a) : 0; }

void Say(const char* s, bool interrupt = true)
{
    if (g_host && g_host->speak) g_host->speak(g_host->ctx, s, interrupt);
}
void Log(const char* s)
{
    if (g_host && g_host->log) g_host->log(g_host->ctx, s);
}

/// Read a NUL-terminated ASCII name at an absolute address. Returns false when the
/// bytes there are not printable text, which is the signal that we are NOT looking
/// at a character record — the same "prove it before you speak it" rule the other
/// adapters follow.
bool ReadName(uint32_t addr, char* out, size_t cap)
{
    size_t n = 0;
    for (; n + 1 < cap; n++) {
        uint8_t c = g_host ? g_host->read8(g_host->ctx, addr + (uint32_t) n) : 0;
        if (c == 0) break;
        if (c < 0x20 || c > 0x7E) return false;   // not text
        out[n] = (char) c;
    }
    out[n] = 0;
    return n >= 2;
}

/// Resolve party slot `i` (0-based) to a record index, or -1 when empty/invalid.
///
/// ⛔ VALIDATED, NOT ASSUMED. A slot is only accepted when its pointer is 4-aligned
/// and lands EXACTLY on a record boundary ((ptr - base) % stride == 0) inside the
/// array, AND the name at that record is readable text. That triple check is what
/// would have caught the one-stride base error immediately, so it is built in.
int PartySlot(int i)
{
    if (i < 0 || i >= (int) PARTY_MAX) return -1;
    uint32_t ptr = u32(PARTY_PTR + (uint32_t) i * 4);
    if (ptr == 0) return -1;                       // terminator
    if (ptr & 3) return -1;                        // not a valid pointer
    if (ptr < REC_BASE) return -1;
    uint32_t delta = ptr - REC_BASE;
    if (delta % REC_STRIDE) return -1;             // NOT a record base
    uint32_t rec = delta / REC_STRIDE;
    if (rec >= REC_COUNT) return -1;               // outside the array
    char nm[24];
    if (!ReadName(REC_BASE + rec * REC_STRIDE + NAME_OFF, nm, sizeof(nm))) return -1;
    return (int) rec;
}

// ⛔ START AT -1 ("nothing chosen yet"), NOT 0. CmdNextAlly advances before it
// speaks, so a cursor initialised to 0 makes the very first "next ally" skip the
// party's FIRST member and announce the second — the player never hears slot 0.
// -1 makes the first press land on slot 0, which is what "next" means from a
// standing start. (Found by the host test; it is invisible in a manual play-through
// because the skip is only ever one member, once.)
int g_cursor = -1;   // which party member the speech commands walk

// ---- commands --------------------------------------------------------------

void SpeakMember(int rec)
{
    uint32_t base = REC_BASE + (uint32_t) rec * REC_STRIDE;
    char nm[24];
    if (!ReadName(base + NAME_OFF, nm, sizeof(nm))) { Say("Unknown member."); return; }

    uint16_t hp  = u16(base + OFF_HP);
    uint16_t hp2 = u16(base + OFF_HP + 2);
    uint16_t ki  = u16(base + OFF_KI);
    uint16_t ki2 = u16(base + OFF_KI + 2);
    uint16_t nxt = u16(base + OFF_NEXT);

    char line[160];
    // HP: the record stores current and max in adjacent u16s. Print both when they
    // differ, because "current" alone is ambiguous mid-battle.
    if (hp2 && hp2 != hp)
        snprintf(line, sizeof(line), "%s. HP %u of %u. Ki %u of %u.",
                 nm, hp, hp2, ki, ki2 ? ki2 : ki);
    else
        snprintf(line, sizeof(line), "%s. HP %u. Ki %u.", nm, hp, ki);
    Say(line);

    if (nxt) {
        snprintf(line, sizeof(line), "%u experience to the next level.", nxt);
        Say(line, false);   // queue behind the stats rather than interrupting
    }
}

void CmdWhereAmI(void)
{
    // ⛔ HONEST SCOPE. This adapter reads the PARTY, not the map. There is no
    // verified map/position structure for this game yet, so rather than narrate a
    // guess it says what it actually knows: which party screen is current.
    int slot = (g_cursor >= 0) ? g_cursor : 0;
    int rec = PartySlot(slot);
    if (rec < 0) { Say("Party not available yet."); return; }
    char nm[24];
    ReadName(REC_BASE + (uint32_t) rec * REC_STRIDE + NAME_OFF, nm, sizeof(nm));
    char line[96];
    snprintf(line, sizeof(line), "Showing %s, party slot %d.", nm, slot + 1);
    Say(line);
}

void CmdNextAlly(int dir)
{
    // From a standing start (-1, nothing spoken yet) "next" is the FIRST member and
    // "previous" is the LAST one — not a wrap through the middle of the party.
    int start = (g_cursor < 0) ? (dir > 0 ? -1 : (int) PARTY_MAX) : g_cursor;
    for (int step = 0; step < (int) PARTY_MAX; step++) {
        int i = start + dir * (step + 1);
        i = ((i % (int) PARTY_MAX) + (int) PARTY_MAX) % (int) PARTY_MAX;
        int rec = PartySlot(i);
        if (rec >= 0) {
            g_cursor = i;
            SpeakMember(rec);
            return;
        }
    }
    Say("No party members found.");
}

void CmdNextEnemy(int /*dir*/)
{
    // ⛔ NOT APPLICABLE, AND SAYING SO IS THE CORRECT BEHAVIOUR. This game's enemy
    // data was not located (only the enemy NAME table in the ROM was). Inventing an
    // answer here would be worse than refusing — the same rule the GBA adapter
    // follows for its own unsupported commands.
    Say("Not applicable in this game.");
}

void CmdDump(void)
{
    char line[200];
    snprintf(line, sizeof(line),
             "[dbz] base=0x%08X stride=0x%X party=0x%08X count=0x%08X",
             REC_BASE, REC_STRIDE, PARTY_PTR, u32(PARTY_CNT));
    Log(line);

    for (int i = 0; i < (int) PARTY_MAX; i++) {
        uint32_t ptr = u32(PARTY_PTR + (uint32_t) i * 4);
        int rec = PartySlot(i);
        if (rec < 0) {
            snprintf(line, sizeof(line), "[dbz] slot %d ptr=0x%08X (none)", i, ptr);
            Log(line);
            continue;
        }
        uint32_t base = REC_BASE + (uint32_t) rec * REC_STRIDE;
        char nm[24];
        ReadName(base + NAME_OFF, nm, sizeof(nm));
        snprintf(line, sizeof(line),
                 "[dbz] slot %d ptr=0x%08X rec=%d name=%s hp=%u/%u ki=%u/%u next=%u",
                 i, ptr, rec, nm,
                 u16(base + OFF_HP), u16(base + OFF_HP + 2),
                 u16(base + OFF_KI), u16(base + OFF_KI + 2),
                 u16(base + OFF_NEXT));
        Log(line);
    }
}

bool DbzReady(void)
{
    // ⛔ READY ONLY WHEN THE DATA IS ACTUALLY THERE. Before the game allocates its
    // character array, these reads return zero/garbage; speaking then would read
    // nonsense to the player. Require a valid record name behind at least one party
    // slot — the same gate the other adapters use, for the same reason.
    return PartySlot(0) >= 0;
}

} // namespace

static bool dbz_attach(const Host* host)
{
    g_host = host;
    g_cursor = -1;
    return true;   // the game code check already happened in the registry
}

static void dbz_on_frame(void) { /* nothing per-frame: this adapter polls on demand */ }

static void dbz_command(Command cmd)
{
    switch (cmd) {
        case Command::WhereAmI:          CmdWhereAmI();            break;
        case Command::NextAlly:          CmdNextAlly(+1);          break;
        case Command::PrevAlly:          CmdNextAlly(-1);          break;
        case Command::NextEnemy:         CmdNextEnemy(+1);         break;
        case Command::PrevEnemy:         CmdNextEnemy(-1);         break;
        case Command::NextUnactedAlly:   CmdNextAlly(+1);          break;
        case Command::DumpState:         CmdDump();                break;
    }
}

static bool dbz_ready(void) { return DbzReady(); }

static void dbz_detach(void) { g_host = nullptr; g_cursor = -1; }

extern const Adapter kDragonBallZSaiyans;
const Adapter kDragonBallZSaiyans = {
    "dbz-saiyans",
    "Dragon Ball Z: Attack of the Saiyans",
    "BRPE",                 // ROM header game code (0x0C..0x0F)
    dbz_attach,
    dbz_on_frame,
    dbz_command,
    dbz_ready,
    dbz_detach,
};

} // namespace oga
