#pragma once

// User settings from splitscreen_support.ini beside the plugin. A missing file or key
// means the default; nothing is ever written back.
namespace settings {
struct Values {
    // The console look: Xbox menu footer and button prompts, the HUD's item
    // cross, console HUD and menu layouts (those are files; the launcher
    // installs the matching packages). Off: the PC's own look, with the
    // functional parts kept.
    bool consoleUi = true;
    // Console title-safe margins around the HUD. Only with the console look.
    bool safeArea = true;
    // Player (1 or 2) that keyboard and mouse belong to while a controller is
    // on. 1 also makes the first controller player two's.
    int keyboardPlayer = 2;
    // Xbox stick response and look sensitivity during local co-op, instead of
    // the user's own controller settings.
    bool xboxSticks = true;
};
inline Values values;
// siblingPath: any file in the plugin's directory. Call before the modules
// that read the values are set up.
void Load(const char* siblingPath);
}
