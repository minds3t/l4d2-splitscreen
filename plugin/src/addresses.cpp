#include "addresses.h"
#include "signatures.h"
#include "supported_build.h"
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include <Windows.h>
#include <cstdio>
#include <cstring>

// The addresses are looked up while this DLL is being loaded, before the
// initializers of the other source files run: those copy addresses into their
// own constants and tables. The game modules are loaded long before a plugin.
#pragma warning(disable : 4073)
#pragma init_seg(lib)

namespace {
constexpr int kModules = sizeof(kSupportedModules) / sizeof(kSupportedModules[0]);
signatures::Result results[kModules];
bool present[kModules];
// What the lookup had to say; the plugin's log does not exist yet.
char notes[4096];
std::size_t noted;

void Note(const char* line) {
    const std::size_t length = std::strlen(line);
    if (noted + length + 2 > sizeof(notes)) return;
    std::memcpy(notes + noted, line, length);
    noted += length;
    notes[noted++] = '\n';
}

struct Startup {
    Startup() {
        for (int index = 0; index < kModules; ++index) {
            char name[64];
            std::snprintf(name, sizeof(name), "%s", kSupportedModules[index].name);
            const HMODULE module = GetModuleHandleA(name);
            name[std::strlen(name) - 4] = 0; // without ".dll"
            if (!module) continue;
            const auto dos = reinterpret_cast<const IMAGE_DOS_HEADER*>(module);
            const auto nt = reinterpret_cast<const IMAGE_NT_HEADERS32*>(reinterpret_cast<const std::uint8_t*>(module) + dos->e_lfanew);
            present[index] = true;
            results[index] = signatures::Resolve(name, {reinterpret_cast<const std::uint8_t*>(module),
                nt->OptionalHeader.SizeOfImage, reinterpret_cast<std::uintptr_t>(module)}, Note);
        }
    }
} startup;

int Index(const char* file) {
    for (int index = 0; index < kModules; ++index)
        if (!std::strcmp(kSupportedModules[index].name, file)) return index;
    return -1;
}
}

namespace addresses {
bool Found(const char* file) {
    const int index = Index(file);
    return index >= 0 && present[index] && results[index].rows > 0 && !results[index].failed;
}
int Moved(const char* file) {
    const int index = Index(file);
    return index >= 0 ? results[index].moved : 0;
}
const char* Notes() { return notes; }
}
