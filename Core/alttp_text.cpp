/*
 * alttp_text.cpp — font tile indices to characters.
 *
 * The mapping was measured, not guessed: the downloaded save's message 388
 * decodes to "Start From Link's House, Start From Sanctuary." with
 * A-Z = 0x00-0x19, a-z = 0x1A-0x33, digits 0x34-0x3D, space = 0x59,
 * apostrophe = 0x51, comma = 0x75, period = 0x72, and commands at 0x67+.
 *
 * ⛔ COMMANDS TAKE PARAMETERS. kTextCommandStart_US is 0x67 in the decomp and
 * kText_CommandLengths_US says how many bytes follow each command; skipping only
 * the command byte would read its parameter as text.
 *
 * ⛔ THE PUNCTUATION IS CHECKED FIRST. 0x75 and 0x72 sit ABOVE the command start
 * and are nonetheless the comma and period this message uses, so the order of
 * these branches is load-bearing.
 */
#include "alttp_text.h"

#include <string.h>

namespace oga {

int AlttpDecodeText(const uint8_t* buf, int len, char* out, int cap)
{
    if (!buf || !out || cap <= 1) return 0;

    static const uint8_t kCmdLen[25] = {
        0, 0, 0, 0, 1, 1, 1, 1, 0, 0, 0, 0, 0, 0, 0, 0, 1, 1, 1, 1, 0, 0, 0, 0, 0
    };

    int n = 0;
    bool pending_space = false;
    for (int i = 0; i < len; i++) {
        uint8_t b = buf[i];
        char c = 0;

        if (b == 0x7F) break;                 /* end of message */
        if (b == 0x75)      c = ',';
        else if (b == 0x72) c = '.';
        else if (b == 0x51) c = '\'';
        else if (b == 0x59) c = ' ';
        else if (b <= 0x19) c = (char) ('A' + b);
        else if (b <= 0x33) c = (char) ('a' + (b - 0x1A));
        else if (b <= 0x3D) c = (char) ('0' + (b - 0x34));
        else if (b >= 0x67) {
            int idx = (int) b - 0x67;
            i += (idx < 25) ? kCmdLen[idx] : 0;
            pending_space = true;
            continue;
        } else {
            /* ⛔ AN UNKNOWN TILE BETWEEN TWO WORDS IS A WORD BREAK, and skipping
             * it silently runs the words together: the measured intro message
             * decoded to "dungeonof the castle" and "Help mePlease help me". The
             * game encodes those breaks with bytes this table does not name, so
             * the safe reading is a space. */
            pending_space = true;
            continue;
        }

        if (c == ' ') { pending_space = true; continue; }

        /* Never open with a space, never double one, and never put one before
         * punctuation that belongs to the previous word. */
        if (pending_space && n > 0 && out[n - 1] != ' ' &&
            c != ',' && c != '.' && c != '\'' && c != '!' && c != '?' && c != ':') {
            if (n >= cap - 1) break;
            out[n++] = ' ';
        }
        pending_space = false;

        if (n >= cap - 1) break;
        out[n++] = c;
    }

    while (n > 0 && out[n - 1] == ' ') n--;
    out[n] = 0;
    return n;
}

} // namespace oga
