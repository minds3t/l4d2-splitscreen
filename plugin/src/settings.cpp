#include "settings.h"
#include "bridge.h"

#include <cstdio>
#include <cstring>

namespace settings {
void Load(const char* siblingPath) {
    values = Values{};
    char path[MAX_PATH]{};
    std::snprintf(path, sizeof(path), "%s", siblingPath ? siblingPath : "");
    char* slash = std::strrchr(path, '\\');
    if (!slash || slash + sizeof("splitscreen_support.ini") >= path + sizeof(path)) return;
    std::strcpy(slash + 1, "splitscreen_support.ini");
    if (GetFileAttributesA(path) == INVALID_FILE_ATTRIBUTES) {
        bridge::Log("[settings] No splitscreen_support.ini beside the plugin; using the defaults");
        return;
    }
    values.consoleUi = GetPrivateProfileIntA("Display", "ConsoleUI", 1, path) != 0;
    values.safeArea = values.consoleUi && GetPrivateProfileIntA("Display", "SafeArea", 1, path) != 0;
    values.keyboardPlayer = GetPrivateProfileIntA("Input", "KeyboardPlayer", 2, path) == 1 ? 1 : 2;
    values.xboxSticks = GetPrivateProfileIntA("Input", "XboxSticks", 1, path) != 0;
    bridge::Log("[settings] splitscreen_support.ini: ConsoleUI=%d SafeArea=%d KeyboardPlayer=%d XboxSticks=%d",
        values.consoleUi, values.safeArea, values.keyboardPlayer, values.xboxSticks);
}
}
