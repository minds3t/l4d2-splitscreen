// Standalone x86 ABI/lifecycle smoke test. No game modules/process are loaded.
#include "../src/guest.cpp"
#include <cassert>
#include <iostream>

class PlayerLocal : public IPlayerLocal {};
template<class Fn> Fn slot(void* p, int n) {
    return reinterpret_cast<Fn>((*reinterpret_cast<void***>(p))[n]);
}
int main() {
    using namespace guest;
    auto module = static_cast<unsigned char*>(VirtualAlloc(nullptr, 0x65000,
        MEM_COMMIT | MEM_RESERVE, PAGE_READWRITE));
    assert(module);
    g_module = reinterpret_cast<std::uintptr_t>(module);
    void* manager[6]{};
    manager[0] = module + kManagerVtable;
    *reinterpret_cast<void**>(module + kManagerGlobal) = manager;
    // A native-shaped host fixture with its own storage and virtual methods.
    OfflineGuest host;
    host.identity = 0x0110000100000042ULL;
    host.controller = 0;
    std::strcpy(host.name, "Fixture Host");
    host.profile.sensitivity = 17;
    host.profile.titlecred = 1234;
    std::memcpy(module + kLocalVtable, *reinterpret_cast<void***>(&host), 12 * 4);
    *reinterpret_cast<void***>(&host) = reinterpret_cast<void**>(module + kLocalVtable);
    manager[2] = &host;
    assert(Register(manager, &host));
    void* p = GuestProfile();
    assert(p == manager[3] && p != &host);
    assert(slot<std::uint64_t (__thiscall*)(void*)>(p, 0)(p) == GuestXuid());
    assert(slot<int (__thiscall*)(void*)>(p, 1)(p) == 1);
    assert(std::strcmp(slot<const char* (__thiscall*)(void*)>(p, 2)(p), "Fixture Host (2)") == 0);
    assert(slot<int (__thiscall*)(void*)>(p, 3)(p) == 0);
    const auto& data = slot<const UserProfileData& (__thiscall*)(void*)>(p, 4)(p);
    assert(data.sensitivity == 17 && data.titlecred == 0);
    const unsigned char input[] = {9,8,7,6};
    slot<void (__thiscall*)(void*,int,const void*,int,int)>(p, 6)(p, 2, input, 996, 4);
    const auto title = static_cast<const unsigned char*>(slot<const void* (__thiscall*)(void*,int)>(p, 5)(p, 2));
    assert(title && !std::memcmp(title + 996, input, 4));
    assert(!g_guest.titleDirty[0] && !g_guest.titleDirty[1] && !g_guest.titleDirty[2]);
    slot<void (__thiscall*)(void*,int,const void*,int,int)>(p, 6)(p, 2, input, 999, 4);
    assert(!std::memcmp(title + 996, input, 4));
    assert(slot<const void* (__thiscall*)(void*,int)>(p, 5)(p, 3) == nullptr);
    slot<void (__thiscall*)(void*,void*)>(p, 7)(p, nullptr);
    slot<void (__thiscall*)(void*,void*)>(p, 8)(p, nullptr);
    slot<void (__thiscall*)(void*)>(p, 10)(p);
    assert(dynamic_cast<PlayerLocal*>(static_cast<IPlayerLocal*>(p)) == nullptr);
    slot<void (__thiscall*)(void*)>(p, 11)(p);
    assert(!GuestProfile() && !manager[3] && manager[2] == &host);
    assert(Register(manager, &host));
    assert(slot<void* (__thiscall*)(void*,unsigned)>(p, 9)(p, 1) == p);
    assert(!manager[3] && !GuestProfile());
    manager[3] = &host;
    assert(!Register(manager, &host) && manager[3] == &host);
    manager[3] = nullptr;
    assert(Register(manager, &host));
    Unregister();
    assert(manager[2] == &host && !manager[3]);
    Shutdown();
    VirtualFree(module, 0, MEM_RELEASE);
    std::cout << "PASS: 12 native virtual slots, uint64 identity ABI, independent profile/title data, bounded title writes, RTTI rejection, native release/dtor, foreign slot refusal, unregister.\n";
}

