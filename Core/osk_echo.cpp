/*
 * osk_echo.cpp — universal PPSSPP OSK input-echo engine (see osk_echo.h).
 *
 * Key tables are verbatim PPSSPP oskKeys[0..1] (src/ppsspp
 * Core/Dialog/PSPOskConstants.cpp, Latin Lowercase / Latin Uppercase).
 * Cursor arithmetic mirrors PSPOskDialog::Update: left/right wrap within the
 * row, up/down wrap modulo the 60-cell total.
 */
#include "osk_echo.h"

#include <stdio.h>

namespace oga {

// PPSSPP oskKeys[0]: Latin Lowercase (5 rows x 12 cols used; table is [6][14]).
static const char* const kLower[OskEcho::kRows] = {
    u8"1234567890-+",
    u8"qwertyuiop[]",
    u8"asdfghjkl;@~",
    u8"zxcvbnm,./?\\",
    u8"=<>'€¥&§£*()",
};

// PPSSPP oskKeys[1]: Latin Uppercase. NOT a case transform of Lower: symbols
// shift too (R1 digits -> !@#$..., R2 [] -> {}, R3 ;@~ -> :"`, R4 ,./?\ -> <>/?|,
// R5 £*() -> §-[]). Copied cell-for-cell from the PPSSPP source.
static const char* const kUpper[OskEcho::kRows] = {
    u8"!@#$%^&*()_+",
    u8"QWERTYUIOP{}",
    u8"ASDFGHJKL:\"`",
    u8"ZXCVBNM<>/?|",
    u8"=<>'€¥&§-[]",
};

const char* OskEcho::keyAt(OskLayout layout, int i)
{
    if (i < 0 || i >= kCount) return "";
    const char* const* table = (layout == OskLayout::LatinLower) ? kLower : kUpper;
    // Tables are stored row-major as UTF-8; index to the i-th code point.
    const char* p = table[i / kCols];
    int want = i % kCols;
    for (int c = 0; c < want; c++) {
        unsigned char ch = (unsigned char) *p;
        if (ch < 0x80) p += 1;
        else if ((ch & 0xE0) == 0xC0) p += 2;
        else if ((ch & 0xF0) == 0xE0) p += 3;
        else p += 4;
    }
    // Return a pointer to a per-call rotating buffer (adapter copies it into
    // its speech line before the next call; tests copy too).
    static char bufs[4][8];
    static int slot = 0;
    char* out = bufs[slot++ % 4];
    unsigned char ch = (unsigned char) *p;
    int len = (ch < 0x80) ? 1 : ((ch & 0xE0) == 0xC0) ? 2
        : ((ch & 0xF0) == 0xE0)                       ? 3
                                                      : 4;
    for (int k = 0; k < len; k++) out[k] = p[k];
    out[len] = '\0';
    return out;
}

std::string OskEcho::spell(const std::string& utf8)
{
    std::string out;
    for (size_t i = 0; i < utf8.size();) {
        unsigned char ch = (unsigned char) utf8[i];
        int len = (ch < 0x80) ? 1 : ((ch & 0xE0) == 0xC0) ? 2
            : ((ch & 0xF0) == 0xE0)                       ? 3
                                                          : 4;
        if (!out.empty()) out += ' ';
        out.append(utf8, i, (size_t) len);
        i += (size_t) len;
    }
    return out;
}

int OskEcho::charCount(const std::string& utf8)
{
    int n = 0;
    for (size_t i = 0; i < utf8.size();) {
        unsigned char ch = (unsigned char) utf8[i];
        i += (size_t) ((ch < 0x80) ? 1 : ((ch & 0xE0) == 0xC0) ? 2
            : ((ch & 0xF0) == 0xE0)                             ? 3
                                                                : 4);
        n++;
    }
    return n;
}

void OskEcho::reset()
{
    live = false;
    idx = 0;
    layout = OskLayout::LatinLower;
}

std::string OskEcho::enter()
{
    live = true;
    idx = 0;
    layout = OskLayout::LatinLower;
    text.clear();
    return "Type your name.";
}

void OskEcho::seed(const std::string& t) { text = t; }
void OskEcho::resync(const std::string& t) { text = t; }

std::string OskEcho::keySpeech() const
{
    char line[64];
    snprintf(line, sizeof(line), "%s. Row %d of %d. Column %d of %d.", keyAt(layout, idx),
             idx / kCols + 1, kRows, idx % kCols + 1, kCols);
    return line;
}

std::string OskEcho::nameSpeech() const
{
    if (text.empty()) return "Name empty.";
    return "Name " + spell(text) + ".";
}

std::string OskEcho::moveUp()
{
    idx = (idx - kCols + kCount) % kCount;
    return keySpeech();
}

std::string OskEcho::moveDown()
{
    idx = (idx + kCols) % kCount;
    return keySpeech();
}

std::string OskEcho::moveLeft()
{
    int col = idx % kCols;
    idx = (col == 0) ? idx + kCols - 1 : idx - 1;
    return keySpeech();
}

std::string OskEcho::moveRight()
{
    int col = idx % kCols;
    idx = (col == kCols - 1) ? idx - (kCols - 1) : idx + 1;
    return keySpeech();
}

std::string OskEcho::type()
{
    if (charCount(text) >= maxChars) return "Full. " + nameSpeech();
    const char* k = keyAt(layout, idx);
    text += k;
    return std::string(k) + ". " + nameSpeech();
}

std::string OskEcho::erase()
{
    if (text.empty()) return "Name empty.";
    // Drop the last UTF-8 code point and report it.
    size_t end = text.size();
    size_t start = end - 1;
    while (start > 0 && ((unsigned char) text[start] & 0xC0) == 0x80) start--;
    std::string dropped = text.substr(start, end - start);
    text.erase(start);
    return "Deleted " + dropped + ". " + nameSpeech();
}

std::string OskEcho::space()
{
    if (charCount(text) >= maxChars) return "Full. " + nameSpeech();
    text += ' ';
    return "Space. " + nameSpeech();
}

std::string OskEcho::shift()
{
    layout = (layout == OskLayout::LatinLower) ? OskLayout::LatinUpper
                                               : OskLayout::LatinLower;
    return (layout == OskLayout::LatinUpper) ? "Uppercase." : "Lowercase.";
}

std::string OskEcho::where()
{
    return nameSpeech() + " On " + keyAt(layout, idx) + ".";
}

std::string OskEcho::exitLine() { return nameSpeech(); }

}  // namespace oga
