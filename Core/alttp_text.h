/*
 * alttp_text.h — the game's message text, decoded from its own font order.
 *
 * ⛔ THE GAME DECODES ITS OWN TEXT FIRST. messaging_text_buffer (g_ram+0x11200,
 * absolute 0x7F1200) holds the current message AFTER the game has turned its
 * compressed form into font tile indices. The decoder here maps those tiles to
 * characters; it does not implement the decompression, and it never ships game
 * text. See docs/design/alttp-memory.md for the measured bytes and the mapping.
 */
#ifndef OGA_ALTTP_TEXT_H
#define OGA_ALTTP_TEXT_H

#include <stdint.h>

namespace oga {

/* Returns the decoded length (0 when nothing printable), writes a NUL-terminated
 * UTF-8 string to out. Never overruns cap. */
int AlttpDecodeText(const uint8_t* buf, int len, char* out, int cap);

} // namespace oga

#endif
