#pragma once

namespace hud_prompts {
// Restore per-player device selection for native CBindPanel hints, including
// idle takeover and infected spawn/attack prompts. Select matching native
// Xbox or PC vote instructions per player. Instructor hints (locator targets)
// get the same per-player device selection. No input binding or vote writes.
bool Install();
// Call only after the shared RemoveHooks operation.
void Shutdown();
}
