#pragma once
#include <cstddef>
#include <cstdint>

// Finds the addresses of build_10097.h in the game modules that are actually
// there, by the rows of signatures.inc. A game update that only moves code
// leaves every row with exactly one answer; anything else is a failure, and
// the caller then leaves the game alone.
namespace signatures {
// A module laid out as the loader maps it. `base` is the address its relocated
// operands refer to: the module handle for a loaded module, the header's
// preferred base for a file read from disk.
struct Image {
    const std::uint8_t* data;
    std::size_t size;
    std::uintptr_t base;
};
struct Result {
    int rows = 0;
    int failed = 0;
    int moved = 0; // found, at another address than in the researched build
};
using Report = void (*)(const char* line);
// Resolves the rows of one module ("engine", "client", ...) and stores each
// address in its rva:: variable. Failed rows keep the researched address.
Result Resolve(const char* module, const Image& image, Report report);
}
