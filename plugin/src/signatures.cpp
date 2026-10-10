#include "signatures.h"
#include "build_10097.h"
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include <Windows.h>
#include <cstdio>
#include <cstring>
#include <vector>

// The address variables, holding the researched build's values until resolved.
#define SS_SIGNATURE(module, name, known, kind, offset, distance, pattern) namespace rva { namespace module { Rva name = known; } }
#define SS_ANCHOR(module, name, known, kind, offset, distance, pattern) namespace rva { namespace module { Rva name = known; } }
#define SS_DERIVED(module, name, known, base, distance) namespace rva { namespace module { Rva name = known; } }
#define SS_LOCATED(module, name, known, base, distance) namespace rva { namespace module { Rva name = known; } }
#include "signatures.inc"
#undef SS_SIGNATURE
#undef SS_ANCHOR
#undef SS_DERIVED
#undef SS_LOCATED

namespace {
enum class Kind { code, operand, call, derived, located };
struct Row {
    const char* module;
    const char* name;
    std::uint32_t known;
    std::uint32_t* target;
    Kind kind;
    int offset;
    int distance;
    const char* pattern;
    const std::uint32_t* base;
};
#define SS_SIGNATURE(module, name, known, kind, offset, distance, pattern) \
    {#module, #name, known, &rva::module::name, Kind::kind, offset, distance, pattern, nullptr},
#define SS_ANCHOR SS_SIGNATURE
#define SS_DERIVED(module, name, known, base, distance) \
    {#module, #name, known, &rva::module::name, Kind::derived, 0, distance, nullptr, &rva::module::base},
#define SS_LOCATED(module, name, known, base, distance) \
    {#module, #name, known, &rva::module::name, Kind::located, 0, distance, nullptr, &rva::module::base},
const Row rows[] = {
#include "signatures.inc"
};

// [start, end) is what the file provides, [start, limit) the whole section.
struct Section { std::uint32_t start, end, limit; bool code; };

bool Sections(const signatures::Image& image, std::vector<Section>& sections) {
    if (image.size < sizeof(IMAGE_DOS_HEADER)) return false;
    const auto dos = reinterpret_cast<const IMAGE_DOS_HEADER*>(image.data);
    if (dos->e_magic != IMAGE_DOS_SIGNATURE || dos->e_lfanew < 0 ||
        static_cast<std::size_t>(dos->e_lfanew) + sizeof(IMAGE_NT_HEADERS32) > image.size) return false;
    const auto nt = reinterpret_cast<const IMAGE_NT_HEADERS32*>(image.data + dos->e_lfanew);
    if (nt->Signature != IMAGE_NT_SIGNATURE || nt->FileHeader.Machine != IMAGE_FILE_MACHINE_I386) return false;
    const auto first = reinterpret_cast<const std::uint8_t*>(&nt->OptionalHeader) + nt->FileHeader.SizeOfOptionalHeader;
    const std::size_t count = nt->FileHeader.NumberOfSections;
    if (static_cast<std::size_t>(first - image.data) + count * sizeof(IMAGE_SECTION_HEADER) > image.size) return false;
    const auto headers = reinterpret_cast<const IMAGE_SECTION_HEADER*>(first);
    for (std::size_t index = 0; index < count; ++index) {
        const auto& header = headers[index];
        // Only bytes the file provides: the rest of a section is zero-filled.
        const std::uint32_t length = header.Misc.VirtualSize < header.SizeOfRawData ? header.Misc.VirtualSize : header.SizeOfRawData;
        if (header.VirtualAddress > image.size || header.Misc.VirtualSize > image.size - header.VirtualAddress) return false;
        if (header.Characteristics & IMAGE_SCN_MEM_DISCARDABLE) continue;
        sections.push_back({header.VirtualAddress, header.VirtualAddress + length,
                            header.VirtualAddress + header.Misc.VirtualSize,
                            (header.Characteristics & IMAGE_SCN_MEM_EXECUTE) != 0});
    }
    return !sections.empty();
}

struct Pattern { std::vector<std::uint8_t> bytes; std::vector<bool> open; std::size_t anchor = 0; };

Pattern Parse(const char* text) {
    Pattern pattern;
    bool anchored = false;
    for (const char* cursor = text; cursor[0] && cursor[1]; cursor += cursor[2] ? 3 : 2) {
        const bool open = cursor[0] == '?';
        unsigned value = 0;
        if (!open) std::sscanf(cursor, "%2x", &value);
        if (!open && !anchored) { pattern.anchor = pattern.bytes.size(); anchored = true; }
        pattern.bytes.push_back(static_cast<std::uint8_t>(value));
        pattern.open.push_back(open);
    }
    return pattern;
}

// Address of the pattern if it occurs exactly once in the module's code.
bool FindOnce(const signatures::Image& image, const std::vector<Section>& sections, const Pattern& pattern, std::uint32_t& found) {
    const std::size_t length = pattern.bytes.size();
    int count = 0;
    if (!length) return false;
    for (const auto& section : sections) {
        if (!section.code || section.end - section.start < length) continue;
        const std::uint8_t* const begin = image.data + section.start;
        const std::uint8_t* const last = image.data + section.end - length;
        const std::uint8_t first = pattern.bytes[pattern.anchor];
        for (const std::uint8_t* cursor = begin + pattern.anchor; cursor <= last + pattern.anchor;) {
            cursor = static_cast<const std::uint8_t*>(std::memchr(cursor, first, last + pattern.anchor - cursor + 1));
            if (!cursor) break;
            const std::uint8_t* candidate = cursor - pattern.anchor;
            std::size_t index = pattern.anchor + 1;
            while (index < length && (pattern.open[index] || candidate[index] == pattern.bytes[index])) ++index;
            if (index == length) {
                if (++count > 1) return false;
                found = static_cast<std::uint32_t>(candidate - image.data);
            }
            ++cursor;
        }
    }
    return count == 1;
}

bool Read32(const signatures::Image& image, std::uint64_t address, std::uint32_t& value) {
    if (address + 4 > image.size) return false;
    std::memcpy(&value, image.data + address, 4);
    return true;
}

bool Inside(const std::vector<Section>& sections, std::uint64_t address) {
    for (const auto& section : sections)
        if (address >= section.start && address < section.limit) return true;
    return false;
}

bool ResolveRow(const Row& row, const signatures::Image& image, const std::vector<Section>& sections, std::uint32_t& result) {
    const auto base = static_cast<std::uint32_t>(image.base);
    std::uint32_t value = 0;
    if (row.kind == Kind::derived) {
        if (!Read32(image, static_cast<std::uint64_t>(*row.base) + row.distance, value)) return false;
        result = value - base;
        return true;
    }
    if (row.kind == Kind::located) {
        // The one place in the module's data that stores the base row's address.
        const std::uint32_t wanted = base + *row.base;
        int count = 0;
        std::uint32_t place = 0;
        for (const auto& section : sections) {
            if (section.code) continue;
            for (std::uint32_t address = (section.start + 3) & ~3u; address + 4 <= section.end; address += 4) {
                if (!std::memcmp(image.data + address, &wanted, 4)) { place = address; if (++count > 1) return false; }
            }
        }
        if (count != 1 || place < static_cast<std::uint32_t>(row.distance)) return false;
        result = place - row.distance;
        return true;
    }
    std::uint32_t match = 0;
    if (!FindOnce(image, sections, Parse(row.pattern), match)) return false;
    if (row.kind == Kind::code) { result = match + row.offset; return true; }
    if (!Read32(image, static_cast<std::uint64_t>(match) + row.offset, value)) return false;
    result = row.kind == Kind::call ? match + row.offset + 4 + value : value - base - row.distance;
    return true;
}
}

namespace signatures {
Result Resolve(const char* module, const Image& image, Report report) {
    Result result;
    std::vector<Section> sections;
    const bool readable = Sections(image, sections);
    char line[160];
    for (const auto& row : rows) {
        if (std::strcmp(row.module, module)) continue;
        ++result.rows;
        std::uint32_t address = 0;
        if (!readable || !ResolveRow(row, image, sections, address) || !Inside(sections, address)) {
            ++result.failed;
            if (report) {
                std::snprintf(line, sizeof(line), "%s: %s not found", module, row.name);
                report(line);
            }
            continue;
        }
        if (address != row.known) {
            ++result.moved;
            if (report) {
                std::snprintf(line, sizeof(line), "%s: %s at +%x (researched build: +%x)", module, row.name, address, row.known);
                report(line);
            }
        }
        *row.target = address;
    }
    return result;
}
}
