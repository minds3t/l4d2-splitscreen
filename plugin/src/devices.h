#pragma once

namespace devices {
// Verifies the fingerprinted InputSystem object once. Read-only afterwards,
// except for the controller order when the settings give keyboard and mouse
// to player one: then controllers 1 and 2 trade places until Shutdown.
bool Initialize();
void Shutdown();
// Native XInput connection record for controller index 0..3.
bool PadConnected(int slot);
// Local player that keyboard and mouse should drive: player 2 while the game's
// first controller is present, otherwise player 1 so a game is never
// unplayable. (With traded controllers the only pad is the game's second.)
int KeyboardSlot();
// Call every frame. The game only polls XInput when a controller was present
// at launch; this notices one that is switched on later and starts the
// game's own polling.
bool WatchForController();
}
