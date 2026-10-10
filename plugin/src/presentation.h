#pragma once

namespace presentation {
// Installs only after explicit enable and exact module fingerprint checks.
// Restores the retained native footer, Xbox anchors and strip proportions in
// the bridge's local menu context, plus paint-scoped Xbox button text insets.
// Custom PC footer layouts and compact flyout/game-mode insets stay unchanged.
// With the console look switched off (splitscreen_support.ini) only the split-direction
// page is made usable; footer, prompts and insets stay the PC's.
bool Install();
// Shared hooks must be removed before this clears their trampoline pointers.
void Shutdown();
}
