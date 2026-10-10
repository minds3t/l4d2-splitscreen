#pragma once

// The outcome of looking up build_10097.h's addresses in the loaded game
// modules, done once while the plugin DLL was loaded (addresses.cpp).
namespace addresses {
// Every address of this module ("engine.dll") was found exactly once.
bool Found(const char* file);
// How many of them sit elsewhere than in the researched build.
int Moved(const char* file);
// One line per address that was not found or has moved.
const char* Notes();
}
