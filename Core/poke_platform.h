/*
 * poke_platform.h — the piece of the Platform layer that only this app knows
 * about: a hook so the core's own log lines reach the app's reading log.
 */
#ifndef POKE_PLATFORM_H
#define POKE_PLATFORM_H

namespace melonDS
{
namespace Platform
{
typedef void (*PokeLogForward)(const char* text);
void PokeSetLogForward(PokeLogForward forward);
}
}

#endif /* POKE_PLATFORM_H */
