#pragma once

namespace safearea {
// Install exact-build gates around the retained native title-safe calculation.
// The global test cvar and all saved configuration remain untouched.
bool Install();
// Restore live bounds on explicit disable: clear the bridge-enabled flag first
// and call before UI cleanup. Enabled calls are no-ops; new resources initialize
// through the installed gates. Menu reload requires a live matchmaking session.
// Do not call during late process Unload: its UI may already be torn down.
void Refresh();
// Shared hooks must be removed first. This never reloads engine resources.
void Shutdown();
}
