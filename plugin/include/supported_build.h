#pragma once

// The game build the addresses in this bridge were researched in:
// Left 4 Dead 2 2.2.4.3, build 10097. SHA-256 of each module file on disk.
// The modules of another build are accepted when every address is found in
// them (signatures.inc). tools/packaging/build_release.py turns these rows into
// the table by which the launcher knows the researched build without asking
// the checker; keep one module per line.
struct SupportedModule {
    const char* name;
    const char* path; // relative to the game directory
    const char* sha256;
};
inline constexpr SupportedModule kSupportedModules[] = {
    {"matchmaking.dll", "left4dead2/bin/matchmaking.dll", "b405c7a2b786775e1940eccebcd6756335ceb20b583b43a05df7a457f9fe0e61"},
    {"gameui.dll", "bin/gameui.dll", "321ec9b5865582330af61e81eb347da31cc213cc6c95b80d6d98170b8a5fbb40"},
    {"engine.dll", "bin/engine.dll", "8a6d468059394bf4cfce32f11dcba12ef45de88da1373084d5a2916d0bf7ba86"},
    {"server.dll", "left4dead2/bin/server.dll", "a93ec916a8f26fbea85688c321d2ba8dd346d435fab09a0b926dd950c2e1ea12"},
    {"client.dll", "left4dead2/bin/client.dll", "c0a8e1e88f7312db11bcfd4b0ec574d224a34b2d9a615d6088b28335eeb4af1f"},
    {"inputsystem.dll", "bin/inputsystem.dll", "f8a6284832659402ed2a627de9064b273cc529eec983616e32c31e002e2f85a3"},
};
