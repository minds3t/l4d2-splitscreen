// splitscreen_check.exe <game folder> [--researched]
//
// Tells the launcher, before anything is copied into the game, whether the
// plugin can find all its addresses in the game modules on disk. Exit code 0:
// every address was found exactly once. With --researched they must also sit
// where they were researched, also when the modules are loaded at another base
// than the files ask for, as most are in the running game (the plugin build's
// own check against the game build it was made from).
#include "signatures.h"
#include "supported_build.h"
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include <Windows.h>
#include <cstdio>
#include <cstring>
#include <string>
#include <vector>

namespace {
// The module as the loader would map it at its preferred base plus `shift`.
bool MapFile(const std::string& path, std::uint32_t shift, std::vector<std::uint8_t>& image, std::uintptr_t& base) {
    std::FILE* file = std::fopen(path.c_str(), "rb");
    if (!file) return false;
    std::vector<std::uint8_t> raw;
    std::uint8_t chunk[65536];
    for (std::size_t read; (read = std::fread(chunk, 1, sizeof(chunk), file)) > 0;) raw.insert(raw.end(), chunk, chunk + read);
    std::fclose(file);
    if (raw.size() < sizeof(IMAGE_DOS_HEADER)) return false;
    const auto dos = reinterpret_cast<const IMAGE_DOS_HEADER*>(raw.data());
    if (dos->e_magic != IMAGE_DOS_SIGNATURE || dos->e_lfanew < 0 ||
        static_cast<std::size_t>(dos->e_lfanew) + sizeof(IMAGE_NT_HEADERS32) > raw.size()) return false;
    const auto nt = reinterpret_cast<const IMAGE_NT_HEADERS32*>(raw.data() + dos->e_lfanew);
    if (nt->Signature != IMAGE_NT_SIGNATURE || nt->FileHeader.Machine != IMAGE_FILE_MACHINE_I386) return false;
    const std::size_t headers = nt->OptionalHeader.SizeOfHeaders, size = nt->OptionalHeader.SizeOfImage;
    if (headers > raw.size() || headers > size || size > (256u << 20)) return false;
    image.assign(size, 0);
    std::memcpy(image.data(), raw.data(), headers);
    const auto first = reinterpret_cast<const std::uint8_t*>(&nt->OptionalHeader) + nt->FileHeader.SizeOfOptionalHeader;
    if (static_cast<std::size_t>(first - raw.data()) + nt->FileHeader.NumberOfSections * sizeof(IMAGE_SECTION_HEADER) > headers) return false;
    const auto sections = reinterpret_cast<const IMAGE_SECTION_HEADER*>(first);
    for (unsigned index = 0; index < nt->FileHeader.NumberOfSections; ++index) {
        const auto& section = sections[index];
        const std::size_t length = section.SizeOfRawData < section.Misc.VirtualSize ? section.SizeOfRawData : section.Misc.VirtualSize;
        if (section.PointerToRawData > raw.size() || length > raw.size() - section.PointerToRawData ||
            section.VirtualAddress > size || length > size - section.VirtualAddress) return false;
        std::memcpy(image.data() + section.VirtualAddress, raw.data() + section.PointerToRawData, length);
    }
    base = nt->OptionalHeader.ImageBase + shift;
    const auto& directory = nt->OptionalHeader.DataDirectory[IMAGE_DIRECTORY_ENTRY_BASERELOC];
    if (!shift || !directory.Size) return true;
    if (directory.VirtualAddress > size || directory.Size > size - directory.VirtualAddress) return false;
    for (std::size_t block = directory.VirtualAddress, end = block + directory.Size; block + 8 <= end;) {
        std::uint32_t page, length;
        std::memcpy(&page, image.data() + block, 4);
        std::memcpy(&length, image.data() + block + 4, 4);
        if (length < 8 || length > end - block) return false;
        for (std::size_t entry = block + 8; entry + 2 <= block + length; entry += 2) {
            std::uint16_t value;
            std::memcpy(&value, image.data() + entry, 2);
            if (value >> 12 != IMAGE_REL_BASED_HIGHLOW) continue;
            const std::size_t place = page + (value & 0xfff);
            if (place + 4 > size) return false;
            std::uint32_t address;
            std::memcpy(&address, image.data() + place, 4);
            address += shift;
            std::memcpy(image.data() + place, &address, 4);
        }
        block += length;
    }
    return true;
}
void Print(const char* line) { std::printf("%s\n", line); }
}

int main(int argc, char** argv) {
    if (argc < 2) { std::printf("usage: splitscreen_check <game folder> [--researched]\n"); return 2; }
    const bool researched = argc > 2 && !std::strcmp(argv[2], "--researched");
    int rows = 0, failed = 0, moved = 0;
    for (const std::uint32_t shift : {0u, 0x01230000u}) {
        if (shift && !researched) break;
        rows = 0;
        for (const auto& module : kSupportedModules) {
            std::vector<std::uint8_t> image;
            std::uintptr_t base = 0;
            const std::string path = std::string(argv[1]) + "/" + module.path;
            if (!MapFile(path, shift, image, base)) { std::printf("%s: cannot read %s\n", module.name, path.c_str()); ++failed; continue; }
            std::string name = module.name;
            name.resize(name.size() - 4); // without ".dll"
            const auto result = signatures::Resolve(name.c_str(), {image.data(), image.size(), base}, Print);
            rows += result.rows; failed += result.failed; moved += result.moved;
        }
    }
    std::printf("%d addresses, %d not found, %d moved\n", rows, failed, moved);
    return failed || (researched && moved) ? 1 : 0;
}
