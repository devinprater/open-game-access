/* snes_core.cpp — the Super Nintendo console, MesenCE underneath.
 *
 * Task 1 of docs/plans/alttp-snes-console.md: create, load, identify, and say
 * why a load failed. Frames, video, input and reads arrive in Tasks 2 and 3.
 *
 * ⛔ EVERY FAILURE PATH SETS A SENTENCE. Mesen catches its own exceptions and
 * turns them into a bare `false`, so the caller must be told something real;
 * "Mesen refused the ROM" with no reason cost the NES core a session.
 */
#include "snes_core.h"

#include "pch.h"

#include "Shared/Emulator.h"
#include "Shared/Interfaces/IConsole.h"
#include "Utilities/VirtualFile.h"
#include "Utilities/FolderUtilities.h"

#include <array>
#include <cstdint>
#include <cstdio>
#include <memory>
#include <new>
#include <string>
#include <vector>

struct SnesCore {
    std::unique_ptr<Emulator> emu;
    std::shared_ptr<IConsole> console;
    std::string error;
    uint32_t crc = 0;          /* CRC32 of the ROM PAYLOAD, copier header skipped */
    long header_bytes = 0;     /* 512 for a headered .smc, 0 otherwise */
    bool loaded = false;
};

namespace {

/* ---- identity is the PAYLOAD, not the file ---------------------------------
 * Mesen's Emulator::GetCrc32 hashes the FILE. A .smc carrying the 512-byte
 * copier header that old dumpers wrote is the same cartridge data and hashes
 * differently — measured here: the same ROM with a 512-byte prefix gave
 * f59f60d1 instead of 777aac2f. Mesen strips the header for the cartridge
 * itself (Core/SNES/BaseCartridge.cpp), so the game runs either way; only the
 * id would be wrong, and the id is what selects a reader.
 *
 * A headered SNES dump is the only ambiguous case on this console and its
 * signature is size % 1024 == 512. The payload is what gets hashed.
 */
const std::array<uint32_t, 256> &CrcTable()
{
    static const std::array<uint32_t, 256> table = [] {
        std::array<uint32_t, 256> t{};
        for (uint32_t i = 0; i < 256; i++) {
            uint32_t c = i;
            for (int k = 0; k < 8; k++)
                c = (c & 1u) ? (0xEDB88320u ^ (c >> 1)) : (c >> 1);
            t[i] = c;
        }
        return t;
    }();
    return table;
}

uint32_t Crc32Payload(const uint8_t *p, size_t n)
{
    const std::array<uint32_t, 256> &t = CrcTable();
    uint32_t c = 0xFFFFFFFFu;
    for (size_t i = 0; i < n; i++)
        c = t[(c ^ p[i]) & 0xFFu] ^ (c >> 8);
    return c ^ 0xFFFFFFFFu;
}

bool PayloadCrc(const char *path, uint32_t *out_crc, long *out_skip)
{
    FILE *f = fopen(path, "rb");
    if (!f) return false;
    if (fseek(f, 0, SEEK_END) != 0) { fclose(f); return false; }
    long size = ftell(f);
    if (size <= 0) { fclose(f); return false; }
    long skip = (size > 512 && (size % 1024) == 512) ? 512 : 0;
    if (fseek(f, skip, SEEK_SET) != 0) { fclose(f); return false; }

    std::vector<uint8_t> buf((size_t) (size - skip));
    size_t got = fread(buf.data(), 1, buf.size(), f);
    fclose(f);
    if (got != buf.size()) return false;

    if (out_crc) *out_crc = Crc32Payload(buf.data(), buf.size());
    if (out_skip) *out_skip = skip;
    return true;
}

} // namespace

extern "C" {

SnesCore *snes_create(void)
{
    return new (std::nothrow) SnesCore();
}

void snes_destroy(SnesCore *c)
{
    if (!c) return;
    c->console.reset();
    c->emu.reset();
    delete c;
}

bool snes_load_rom(SnesCore *c, const char *rom_path, const char *save_path,
                   char code_out[16])
{
    if (!c || !rom_path) return false;
    c->error.clear();
    c->crc = 0;
    c->header_bytes = 0;
    c->loaded = false;

    /* Identity first, so a failure here names the file rather than Mesen. */
    if (!PayloadCrc(rom_path, &c->crc, &c->header_bytes)) {
        c->error = "The ROM could not be read.";
        return false;
    }

    /* Never leave the home folder unset: Mesen throws from inside LoadRom and
     * the exception is swallowed by its own catch. */
    {
        std::string home = (save_path && *save_path) ? std::string(save_path) : std::string(".");
        FolderUtilities::SetHomeFolder(home);
    }

    c->emu.reset(new (std::nothrow) Emulator());
    if (!c->emu) { c->error = "Could not create the emulator."; return false; }

    /* Settings, memory registrations and the console factory are wired here.
     * Loading on an uninitialized Emulator fails in ways that look like a bad ROM. */
    c->emu->Initialize();

    VirtualFile rom((std::string(rom_path)));
    if (!rom.IsValid()) { c->error = "The ROM could not be opened."; return false; }

    bool loaded = false;
    try {
        loaded = c->emu->LoadRom(rom, VirtualFile(), false);
    } catch (const std::exception &ex) {
        c->error = std::string("Mesen threw while loading: ") + ex.what();
        return false;
    } catch (...) {
        c->error = "Mesen threw a non-std exception while loading.";
        return false;
    }
    if (!loaded) {
        c->error = "Mesen refused the ROM (LoadRom returned false).";
        return false;
    }

    c->console = c->emu->GetConsole();
    if (!c->console) { c->error = "The ROM loaded but no console was created."; return false; }

    /* ⛔ DO NOT TAKE Mesen's GetCrc32 HERE. It hashes the file, header included;
     * the payload CRC above is the id. See the note beside CrcTable(). */
    if (code_out) std::snprintf(code_out, 16, "crc:%08x", c->crc);
    c->loaded = true;
    return true;
}

uint32_t snes_rom_crc32(SnesCore *c)
{
    return c ? c->crc : 0;
}

const char *snes_last_error(SnesCore *c)
{
    return (c && !c->error.empty()) ? c->error.c_str() : "";
}

} // extern "C"
