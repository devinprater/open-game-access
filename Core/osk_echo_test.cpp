/*
 * osk_echo_test.cpp — host unit test for the universal PPSSPP OSK echo engine.
 *
 * Pure logic (no emulator, no RAM): cursor arithmetic must match the verified
 * PPSSPP selectedChar model (docs/research/dissidia-name-entry.md §OSK), and
 * every speech line must match the reader proposal word-for-word.
 */
#include "osk_echo.h"

#include <stdio.h>
#include <string.h>

static int failures = 0;
#define CHECK(cond, msg) do { \
    if (!(cond)) { printf("FAIL: %s (line %d)\n", msg, __LINE__); failures++; } \
    else { printf("ok: %s\n", msg); } \
} while (0)
#define CHECKSTR(got, want, msg) do { \
    std::string g__ = (got); std::string w__ = (want); \
    if (g__ != w__) { printf("FAIL: %s\n  got  [%s]\n  want [%s] (line %d)\n", \
        msg, g__.c_str(), w__.c_str(), __LINE__); failures++; } \
    else { printf("ok: %s\n", msg); } \
} while (0)

int main(void)
{
    using oga::OskEcho;
    using oga::OskLayout;

    // Tables: spot-check every row of both layouts (verbatim PPSSPP oskKeys).
    CHECK(!strcmp(OskEcho::keyAt(OskLayout::LatinLower, 0), "1"), "lower[0]=1");
    CHECK(!strcmp(OskEcho::keyAt(OskLayout::LatinLower, 11), "+"), "lower[11]=+");
    CHECK(!strcmp(OskEcho::keyAt(OskLayout::LatinLower, 12), "q"), "lower[12]=q");
    CHECK(!strcmp(OskEcho::keyAt(OskLayout::LatinLower, 23), "]"), "lower[23]=]");
    CHECK(!strcmp(OskEcho::keyAt(OskLayout::LatinLower, 24), "a"), "lower[24]=a");
    CHECK(!strcmp(OskEcho::keyAt(OskLayout::LatinLower, 36), "z"), "lower[36]=z");
    CHECK(!strcmp(OskEcho::keyAt(OskLayout::LatinLower, 48), "="), "lower[48]===");
    CHECK(!strcmp(OskEcho::keyAt(OskLayout::LatinLower, 59), ")"), "lower[59]=)");
    CHECK(!strcmp(OskEcho::keyAt(OskLayout::LatinLower, 52), "\u20AC"), "lower[52]=euro");
    CHECK(!strcmp(OskEcho::keyAt(OskLayout::LatinUpper, 0), "!"), "upper[0]=!");
    CHECK(!strcmp(OskEcho::keyAt(OskLayout::LatinUpper, 11), "+"), "upper[11]=+");
    CHECK(!strcmp(OskEcho::keyAt(OskLayout::LatinUpper, 12), "Q"), "upper[12]=Q");
    CHECK(!strcmp(OskEcho::keyAt(OskLayout::LatinUpper, 23), "}"), "upper[23]=}");
    CHECK(!strcmp(OskEcho::keyAt(OskLayout::LatinUpper, 33), ":"), "upper[33]=colon");
    CHECK(!strcmp(OskEcho::keyAt(OskLayout::LatinUpper, 34), "\""), "upper[34]=dquote");
    CHECK(!strcmp(OskEcho::keyAt(OskLayout::LatinUpper, 47), "|"), "upper[47]=pipe");
    CHECK(!strcmp(OskEcho::keyAt(OskLayout::LatinUpper, 56), "-"), "upper[56]=dash");
    CHECK(!strcmp(OskEcho::keyAt(OskLayout::LatinUpper, 57), "["), "upper[57]=lbracket");
    CHECK(!strcmp(OskEcho::keyAt(OskLayout::LatinLower, 60), ""), "oob empty");
    CHECK(!strcmp(OskEcho::keyAt(OskLayout::LatinLower, -1), ""), "neg empty");

    // Cursor walk: the verified live walk (Left 0->11, Right 11->0,
    // Down 0->12, Up 12->0, Down 11->23).
    {
        OskEcho e;
        CHECKSTR(e.enter(), "Type your name.", "enter line");
        CHECK(e.live && e.idx == 0, "enter homes cursor");
        CHECKSTR(e.moveLeft(), "+. Row 1 of 5. Column 12 of 12.", "left wraps 0->11");
        CHECKSTR(e.moveRight(), "1. Row 1 of 5. Column 1 of 12.", "right wraps 11->0");
        CHECKSTR(e.moveDown(), "q. Row 2 of 5. Column 1 of 12.", "down 0->12");
        CHECKSTR(e.moveUp(), "1. Row 1 of 5. Column 1 of 12.", "up 12->0");
        CHECK(e.idx == 0, "back home");
    }
    // Mod-60 vertical wrap at the far end.
    {
        OskEcho e;
        e.enter();
        e.moveLeft();  // 11
        CHECKSTR(e.moveDown(), "]. Row 2 of 5. Column 12 of 12.", "down 11->23");
        CHECKSTR(e.moveUp(), "+. Row 1 of 5. Column 12 of 12.", "up 23->11");
        e.idx = 59;
        CHECKSTR(e.moveDown(), "+. Row 1 of 5. Column 12 of 12.", "down 59->11 mod60");
        e.idx = 0;
        CHECKSTR(e.moveUp(), "=. Row 5 of 5. Column 1 of 12.", "up 0->48");
    }
    // Typing, delete, space, cap.
    {
        OskEcho e;
        e.enter();
        e.seed("PPSSPP");
        CHECKSTR(e.type(), "1. Name P P S S P P 1.", "type appends + spells");
        CHECKSTR(e.type(), "1. Name P P S S P P 1 1.", "type again");
        CHECKSTR(e.erase(), "Deleted 1. Name P P S S P P 1.", "delete drops + names char");
        CHECKSTR(e.space(), "Space. Name P P S S P P 1  .", "space appends");
        CHECKSTR(e.erase(), "Deleted  . Name P P S S P P 1.", "delete drops space");
        e.maxChars = 7;
        CHECKSTR(e.type(), "Full. Name P P S S P P 1.", "cap refuses + says full");
        e.resync("AB");
        CHECKSTR(e.where(), "Name A B. On 1.", "resync + where");
    }
    // Empty-buffer paths.
    {
        OskEcho e;
        e.enter();
        CHECKSTR(e.erase(), "Name empty.", "delete on empty");
        CHECKSTR(e.where(), "Name empty. On 1.", "where on empty");
        CHECKSTR(e.exitLine(), "Name empty.", "exit on empty");
    }
    // Shift toggles the case table (symbols shift too, per PPSSPP).
    {
        OskEcho e;
        e.enter();
        CHECKSTR(e.shift(), "Uppercase.", "shift up line");
        CHECKSTR(e.moveRight(), "@. Row 1 of 5. Column 2 of 12.", "upper[1]=@");
        CHECKSTR(e.shift(), "Lowercase.", "shift down line");
        CHECKSTR(e.moveLeft(), "1. Row 1 of 5. Column 1 of 12.", "lower again after left");
    }
    // spell/charCount handle multibyte.
    CHECKSTR(OskEcho::spell("a\xE2\x82\xAC"), "a \u20AC", "spell multibyte");
    CHECK(OskEcho::charCount("a\xE2\x82\xAC") == 2, "charCount multibyte");

    if (failures) { printf("%d FAILURES\n", failures); return 1; }
    printf("osk_echo: all pass\n");
    return 0;
}
