/*
 * osk_echo.h — universal PPSSPP on-screen-keyboard input-echo engine.
 *
 * WHY THIS EXISTS (docs/research/dissidia-name-entry.md): every PSP game that
 * needs text entry calls the same system dialog (sceUtilityOsk), which PPSSPP
 * HLEs as one shared dialog (Core/Dialog/PSPOskDialog.*). The highlight cursor
 * (selectedChar) and the typed text (inputChars) live in host-side C++, so no
 * guest-RAM cursor can exist by construction — but the KEY GRID is PPSSPP-fixed
 * per language (PSPOskConstants oskKeys) and the cursor arithmetic is fixed
 * (selectedChar = row*cols+col, left/right wrap in-row, up/down wrap mod 60,
 * verified live). That makes input-echo the one viable reader design for ANY
 * game on this OSK: track the cursor from the D-pad the game also received,
 * mirror the text from Cross/Circle/Square, and ground both against the game's
 * guest outtext buffer (which tracks typing live) after every edit.
 *
 * This engine is game-independent: it holds no Host pointer and reads no RAM.
 * A per-game adapter embeds one OskEcho, feeds it the player's buttons, speaks
 * the returned lines, and supplies the entry/exit gates plus the guest-buffer
 * grounding reads (see the Dissidia wiring for the reference implementation).
 */
#ifndef OGA_OSK_ECHO_H
#define OGA_OSK_ECHO_H

#include <string>

namespace oga {

/// PPSSPP OSK keyboards this engine tracks. Latin pair only: full-width and
/// kana layouts are unmapped follow-up work (same arithmetic, new tables).
enum class OskLayout {
    LatinLower,   // PPSSPP OSK_KEYBOARD_LATIN_LOWERCASE (oskKeys[0])
    LatinUpper,   // PPSSPP OSK_KEYBOARD_LATIN_UPPERCASE (oskKeys[1])
};

struct OskEcho {
    static constexpr int kCols = 12;
    static constexpr int kRows = 5;
    static constexpr int kCount = kCols * kRows;  // 60 cells, indices 0..59

    bool live = false;                 // set by enter(), cleared on exit
    int idx = 0;                       // flat cursor 0..59 (== selectedChar)
    OskLayout layout = OskLayout::LatinLower;
    std::string text;                  // UTF-8 text mirror
    int maxChars = 12;                 // game outtextlength-1; adapter sets it

    /// UTF-8 label of one grid cell, or "" when out of range. Tables are
    /// verbatim PPSSPP oskKeys[0..1] (see the .cpp).
    static const char* keyAt(OskLayout layout, int i);

    /// "P P S S P P 1": UTF-8 code points joined with spaces.
    static std::string spell(const std::string& utf8);
    /// Code-point count (the cap is in characters, not bytes).
    static int charCount(const std::string& utf8);

    void reset();                      // stop tracking, cursor home, text kept
    std::string enter();               // start tracking; "Player name. ..."
    void seed(const std::string& t);   // prefill mirror (guest buffer read)
    void resync(const std::string& t); // adopt RAM text after a mismatch

    std::string keySpeech() const;     // "<key>. Row R of 5. Column C of 12."
    std::string nameSpeech() const;    // "Name <spelled>." / "Name is empty."
    std::string moveUp();
    std::string moveDown();
    std::string moveLeft();
    std::string moveRight();
    std::string type();    // Cross: "<key>. Name <spelled>." / at cap: "Full. ..."
    std::string erase();   // Circle: "Deleted <c>. Name <s>." / "Name is empty."
    std::string space();   // Square: "Space. Name <s>."
    std::string shift();   // Select: toggle case table, "Uppercase."/"Lowercase."
    std::string where();   // Where-Is: "Name <name>. On <key>."
    std::string exitLine();// "Name <spelled>." for the exit announcement
};

}  // namespace oga

#endif  // OGA_OSK_ECHO_H
