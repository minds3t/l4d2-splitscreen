#pragma once

namespace hud_inventory {
// Exact-build, opt-in, owned-offline HUD presentation only. Native inventory,
// ammo, item availability, selected colors and pickup pulses remain native.
bool Install();
// Call only after shared RemoveHooks has removed all detours.
void Shutdown();
}
