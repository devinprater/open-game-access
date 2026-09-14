/*
 * poke_internal.h — the small surface shared between the app glue in
 * pokecore.cpp and the Platform layer in poke_platform.cpp. Not part of the
 * app's API: Swift only ever sees pokecore.h.
 */
#ifndef POKE_INTERNAL_H
#define POKE_INTERNAL_H

namespace melonDS
{
namespace Platform
{
typedef void (*PokeLogForward)(const char* text);
void PokeSetLogForward(PokeLogForward forward);
}
}

/// Called from the Platform layer when the emulated console stops itself.
void poke_internal_signal_stop(int stopReason);

#endif /* POKE_INTERNAL_H */
