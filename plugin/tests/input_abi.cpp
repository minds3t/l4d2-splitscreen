#include "../src/input.cpp"
#include <cassert>
#include <cstdarg>
#include <cstdio>
#include <vector>
#include <string>
#include <array>
bool bridgeEnabled = true;
std::vector<std::string> commands;
namespace bridge {
void Log(const char*, ...) {}
HMODULE Engine() { return nullptr; }
bool IsLocalSplitEnabled() { return bridgeEnabled; }
bool InstallHook(const char*, HMODULE, uint32_t, const uint8_t*, size_t, void*, void**) { return false; }
bool PrimaryCommand(const char* text) { commands.emplace_back(text); return true; }
}
struct FakeVar {
    void* table{}; void* next{}; int registered{}; const char* name{};
    const char* help{}; int flags{}; void* secondary{}; FakeVar* parent{};
    const char* initial{}; char* text{}; int textSize{}; float number{}; int integer{};
};
static_assert(offsetof(FakeVar, secondary)==0x18 && offsetof(FakeVar, text)==0x24 && offsetof(FakeVar, number)==0x2c);
// 0 guard, 1 joystick, 2 keyboard owner, 3-9 response/sensitivity, 10-11
// inversion (never written), 12 pitch threshold, 13-19 gamepad axes, 20-21
// each player's controller preset (read only), 22 InputSystem's found flag,
// 23 the main menu's once-per-launch controller check, 24 the weapon-model
// field of view the game overwrites in split-screen (never written by Enable).
constexpr int kVars = 25;
FakeVar fake[kVars];
std::vector<std::string> writes;
bool missingGuard=false, failJoystick=false, failRestore=false;
int failVar=-1;
float failValue=0;
bool failSerialize=false;
std::vector<std::array<float,10>> archived;
std::vector<float> archivedFov;
void __fastcall Serialize(void* self, void*, void* buffer, int slot, bool all) {
    assert(self==fake && buffer==fake+1 && slot==-1 && !all);
    archived.push_back({fake[1].number,fake[3].number,fake[4].number,fake[5].number,
        fake[6].number,fake[7].number,fake[8].number,fake[9].number,fake[12].number,fake[14].number});
    archivedFov.push_back(fake[24].number);
    if(failSerialize)RaiseException(0xe0401234,0,0,nullptr);
}
void* __fastcall Find(void*, void*, const char* name) {
    for(auto& v:fake)if(!std::strcmp(v.name,name))return missingGuard && &v==fake ? nullptr : &v;
    return nullptr;
}
void __fastcall SetFloat(void* secondary, void*, float value) {
    auto v=reinterpret_cast<FakeVar*>(static_cast<unsigned char*>(secondary)-0x18);
    if(v==fake+1 && ((failJoystick && value==1) || (failRestore && value==0)))return;
    if(failVar>=0 && v==fake+failVar && value==failValue)return;
    v->number=value; v->integer=static_cast<int>(value);
    char row[128];std::snprintf(row,sizeof(row),"%s=%.9g",v->name,value);writes.emplace_back(row);
}
bool SerializeWithException() {
    __try { input::WriteVariables(fake,fake+1,-1,false,true); }
    __except(GetExceptionCode()==0xe0401234 ? EXCEPTION_EXECUTE_HANDLER : EXCEPTION_CONTINUE_SEARCH) { return true; }
    return false;
}
void AssertOriginalProfile() {
    assert(fake[3].number==0.65f && fake[4].number==3 && fake[5].number==4);
    assert(fake[6].number==1 && fake[7].number==-1.5f && fake[8].number==1.25f && fake[9].number==-1);
    assert(fake[12].number==0.1f);
    for(int i=13;i<20;++i)assert(fake[i].number==0);
}
void AssertXboxAxes() {
    // cfg/360controller.cfg: advanced 1, x 3, y 1, z 0, r 2, u 4, v 0.
    const float expected[]={1,3,1,0,2,4,0};
    for(int i=0;i<7;++i)assert(fake[13+i].number==expected[i]);
}
void AssertXboxProfile() {
    assert(fake[3].number==0.75f && fake[4].number==2 && fake[5].number==1);
    assert(fake[6].number==0.8f && fake[7].number==-2.25f && fake[8].number==0.8f && fake[9].number==-2.25f);
    assert(fake[12].number==0);
    AssertXboxAxes();
}
void AssertArchiveOriginals() {
    assert(archived.back()[0]==0 && archived.back()[1]==0.65f && archived.back()[2]==3 && archived.back()[3]==4);
    assert(archived.back()[4]==1 && archived.back()[5]==-1.5f && archived.back()[6]==1.25f && archived.back()[7]==-1);
    assert(archived.back()[8]==0.1f && archived.back()[9]==0);
}
void AssertEditedProfile() {
    assert(fake[3].number==0.6f && fake[4].number==2.5f && fake[5].number==1.5f);
    assert(fake[6].number==0.9f && fake[7].number==-1.75f && fake[8].number==1.1f && fake[9].number==-2.5f);
    assert(fake[12].number==0.05f);
    AssertXboxAxes();
}
int main(){
    void* primary[14]{}; primary[13]=reinterpret_cast<void*>(&Find);
    void* registry[1]{primary};
    void* second[4]{};second[2]=reinterpret_cast<void*>(&SetFloat);
    const char* names[kVars]={"joy_xcontroller_cfg_loaded","joystick","in_forceuser","joy_lowend","joy_accelscale","joy_accelmax",
        "joy_pitchsensitivity","joy_yawsensitivity","joy_pitchsensitivity2","joy_yawsensitivity2","joy_inverty","joy_inverty2","joy_pitchthreshold",
        "joy_advanced","joy_advaxisx","joy_advaxisy","joy_advaxisz","joy_advaxisr","joy_advaxisu","joy_advaxisv",
        "joy_cfg_preset","joy_cfg_preset2","joy_xcontroller_found","controller_popup_shown","cl_viewmodelfovsurvivor"};
    for(int i=0;i<kVars;++i){fake[i].name=names[i];fake[i].parent=&fake[i];fake[i].secondary=second;}
    fake[3].number=0.65f;fake[4].number=3;fake[5].number=4;
    fake[6].number=1;fake[7].number=-1.5f;fake[8].number=1.25f;fake[9].number=-1;
    fake[10].number=0;fake[11].number=1;
    fake[12].number=0.1f;
    fake[24].number=51;
    fake[20].number=2;fake[21].number=7;  // player 1: third preset; player 2: out of range
    missingGuard=true;assert(!input::Initialize(registry));assert(!input::Enable());assert(writes.empty());
    // Split ConVars must resolve to their own named parent, never slot0 aliases.
    missingGuard=false;fake[8].parent=&fake[6];assert(!input::Initialize(registry));assert(writes.empty());fake[8].parent=&fake[8];
    missingGuard=false;assert(input::Initialize(registry));assert(input::Enable());assert(input::Enable());
    // The main menu's controller check is switched off first, then the guard,
    // the found flag (no pad at launch: 0), joystick, 8 response values and
    // 7 axes. The keyboard owner is not touched until a game has both players.
    assert(writes.size()==19 && writes[0]=="controller_popup_shown=1" && writes[1]=="joy_xcontroller_cfg_loaded=1" &&
        writes[2]=="joy_xcontroller_found=1" && writes[3]=="joystick=1");
    assert(fake[2].number==0 && commands.empty());
    AssertXboxProfile();
    assert(input::MaintainMenu() && writes.size()==19);
    fake[0].number=0;fake[1].number=0;
    assert(input::MaintainMenu());
    assert(writes.size()==22 && writes[19]=="joy_xcontroller_cfg_loaded=1" && writes[20]=="joy_xcontroller_found=1" && writes[21]=="joystick=1");
    // The found flag alone dropping is re-armed too.
    fake[22].number=0;assert(input::MaintainMenu() && writes.size()==25 && fake[22].number==1);
    AssertXboxProfile();

    // Player two's view goes away: the split layout's weapon-model field of
    // view (set by the game, here 35) returns to the single-player value.
    writes.clear();
    input::TrackSplitView(false);assert(writes.empty());
    input::TrackSplitView(true);fake[24].number=35;
    input::TrackSplitView(true);assert(writes.empty() && fake[24].number==35);
    input::TrackSplitView(false);
    assert(writes.size()==1 && writes[0]=="cl_viewmodelfovsurvivor=51" && fake[24].number==51);
    input::TrackSplitView(false);assert(writes.size()==1);
    input::TrackSplitView(true);input::TrackSplitView(false);assert(writes.size()==1);  // already the single-player value

    // Keyboard owner: written on change only.
    writes.clear();
    assert(input::SetKeyboardSlot(0) && writes.empty());
    assert(input::SetKeyboardSlot(1) && writes.size()==1 && writes[0]=="in_forceuser=1" && fake[2].number==1);
    assert(input::SetKeyboardSlot(1) && writes.size()==1);

    // Per-player controller setup: scheduled, runs once, in player contexts.
    assert(!input::RunPlayerSetup(true) && commands.empty());
    input::GuestActive();
    assert(!input::RunPlayerSetup(true) && commands.empty());  // not due yet
    Sleep(550);
    assert(input::RunPlayerSetup(true) && commands.size()==1);
    assert(commands[0]=="cmd1 exec joy_preset_3.cfg; cmd2 exec joy_preset_1.cfg; joyadvancedupdate; cmd1 +jlook; cmd2 +jlook\n");
    assert(!input::RunPlayerSetup(true) && commands.size()==1);
    input::GuestActive();Sleep(550);
    assert(!input::RunPlayerSetup(false) && !input::RunPlayerSetup(true) && commands.size()==1);  // player two gone: dropped
    for(const auto& write:writes)assert(write.find("joy_cfg_preset")==std::string::npos);

    writes.clear();
    input::Restore();
    assert(writes.size()==21 && writes[0]=="joystick=0" && writes[1]=="in_forceuser=0" &&
        writes[17]=="joy_xcontroller_found=0" && writes[18]=="cl_viewmodelfovsurvivor=51" &&
        writes[19]=="joy_xcontroller_cfg_loaded=0" &&
        writes[20]=="controller_popup_shown=0");
    AssertOriginalProfile();assert(fake[2].number==0 && fake[22].number==0 && fake[23].number==0);
    assert(!input::MaintainMenu() && !input::SetKeyboardSlot(1) && writes.size()==21);
    // A controller that appeared during the session stays found afterwards.
    assert(input::Enable());input::ControllerFound();input::Restore();
    assert(fake[22].number==1 && fake[0].number==0 && fake[1].number==0 && !input::active);
    input::ControllerFound();assert(input::found.previous==1);  // inactive: no change
    fake[22].number=0;
    // The user's own stick settings (splitscreen_support.ini XboxSticks=0): only the
    // guard values, joystick and the 7 axes are written; Restore still works.
    settings::values.xboxSticks=false;writes.clear();
    assert(input::Enable() && writes.size()==11);
    for(const auto& write:writes)assert(write.find("sensitivity")==std::string::npos &&
        write.find("joy_lowend")==std::string::npos && write.find("joy_accel")==std::string::npos &&
        write.find("joy_pitchthreshold")==std::string::npos);
    assert(fake[3].number==0.65f && fake[4].number==3 && fake[5].number==4 && fake[6].number==1 &&
        fake[7].number==-1.5f && fake[8].number==1.25f && fake[9].number==-1 && fake[12].number==0.1f);
    AssertXboxAxes();
    input::Restore();AssertOriginalProfile();assert(!input::active && fake[0].number==0 && fake[1].number==0);
    settings::values.xboxSticks=true;
    writes.clear();failJoystick=true;assert(!input::Enable());
    assert(fake[0].number==0 && fake[1].number==0 && fake[2].number==0);
    AssertOriginalProfile();
    failJoystick=false;
    failVar=4;failValue=2;assert(!input::Enable());AssertOriginalProfile();
    assert(!input::active && fake[0].number==0 && fake[1].number==0);
    failVar=9;failValue=-2.25f;assert(!input::Enable());AssertOriginalProfile();
    assert(!input::active && fake[0].number==0 && fake[1].number==0);
    failVar=12;failValue=0;assert(!input::Enable());AssertOriginalProfile();
    assert(!input::active && fake[0].number==0 && fake[1].number==0);
    failVar=17;failValue=2;assert(!input::Enable());AssertOriginalProfile();  // an axis
    assert(!input::active && fake[0].number==0 && fake[1].number==0);
    failVar=-1;assert(input::Enable());
    input::originalWriteVariables=reinterpret_cast<input::WriteVariablesFn>(&Serialize);
    writes.clear();
    input::WriteVariables(fake,fake+1,-1,false,false);
    assert(archived.back()[0]==1 && archived.back()[1]==0.75f && archived.back()[9]==3 && writes.empty());
    bridgeEnabled=false;input::WriteVariables(fake,fake+1,-1,false,true);
    assert(archived.back()[0]==1 && archived.back()[1]==0.75f && writes.empty());bridgeEnabled=true;
    input::WriteVariables(fake,fake+1,-1,false,true);
    AssertArchiveOriginals();AssertXboxProfile();assert(fake[1].number==1 && fake[0].number==1);
    // Guard, then 14 changed values staged (joystick, 8 response, 5 axes) and
    // the same 14 put back; two axes already had their Xbox value.
    assert(writes.size()==29 && writes[0]=="joy_xcontroller_cfg_loaded=1" && writes[1]=="joystick=0" && writes.back()=="joystick=1");
    assert(input::joystick.previous==0 && !input::serializing);
    // The game's split-screen layout overwrites the weapon-model field of
    // view: the config keeps the single-player value, the game keeps its own.
    assert(archivedFov.back()==51);
    fake[24].number=35;writes.clear();input::WriteVariables(fake,fake+1,-1,false,true);
    assert(archivedFov.back()==51 && fake[24].number==35 && writes.size()==31);
    fake[24].number=51;

    // In-session edits stay live; archive still preserves the pre-enable
    // preferences, even when joystick already equals its original value.
    fake[1].number=0;fake[3].number=0.6f;fake[4].number=2.5f;fake[5].number=1.5f;
    fake[6].number=0.9f;fake[7].number=-1.75f;fake[8].number=1.1f;fake[9].number=-2.5f;
    fake[12].number=0.05f;
    writes.clear();input::WriteVariables(fake,fake+1,-1,false,true);
    AssertArchiveOriginals();AssertEditedProfile();assert(fake[1].number==0);
    assert(writes.size()==26);
    assert(input::MaintainMenu());
    AssertEditedProfile();assert(fake[1].number==1);

    // A failed profile substitution must roll back all attempted variables
    // before the original writer runs, with no partial original/live mixture.
    failVar=4;failValue=3;writes.clear();
    input::WriteVariables(fake,fake+1,-1,false,true);
    assert(archived.back()[0]==1 && archived.back()[1]==0.6f && archived.back()[2]==2.5f && archived.back()[3]==1.5f);
    assert(archived.back()[4]==0.9f && archived.back()[5]==-1.75f && archived.back()[6]==1.1f && archived.back()[7]==-2.5f);
    assert(archived.back()[8]==0.05f && archived.back()[9]==3);
    AssertEditedProfile();assert(fake[1].number==1 && !input::serializing);
    // Failure after the first slot is staged also restores both distinct slots.
    failVar=9;failValue=-1;input::WriteVariables(fake,fake+1,-1,false,true);
    assert(archived.back()[4]==0.9f && archived.back()[5]==-1.75f && archived.back()[6]==1.1f && archived.back()[7]==-2.5f);
    AssertEditedProfile();assert(fake[1].number==1 && !input::serializing);
    failVar=12;failValue=0.1f;input::WriteVariables(fake,fake+1,-1,false,true);
    assert(archived.back()[4]==0.9f && archived.back()[6]==1.1f && archived.back()[8]==0.05f);
    AssertEditedProfile();assert(fake[1].number==1 && !input::serializing);
    failVar=-1;
    failSerialize=true;assert(SerializeWithException());failSerialize=false;
    AssertArchiveOriginals();AssertEditedProfile();assert(fake[1].number==1 && !input::serializing);

    failRestore=true;input::Restore();assert(fake[0].number==1 && fake[1].number==1 && input::active && input::restorePending);assert(!input::MaintainMenu());
    assert(fake[23].number==1);  // guard retained: the main menu's check stays off
    AssertOriginalProfile();
    writes.clear();input::WriteVariables(fake,fake+1,-1,false,true);assert(archived.back()[0]==1 && writes.empty());
    failRestore=false;input::Restore();assert(fake[0].number==0 && fake[1].number==0 && !input::active);
    assert(input::Enable());AssertXboxProfile();
    failVar=4;failValue=3;input::Restore();
    assert(input::active && input::restorePending && fake[4].number==2 && fake[1].number==0 && fake[0].number==0);
    failVar=-1;input::Restore();AssertOriginalProfile();assert(!input::active && !input::restorePending);
    assert(input::Enable());failVar=12;failValue=0.1f;input::Restore();
    assert(input::active && input::restorePending && fake[12].number==0);
    failVar=-1;input::Restore();AssertOriginalProfile();assert(!input::active && !input::restorePending);
    assert(input::Enable());failVar=8;failValue=1.25f;input::Restore();
    assert(input::active && input::restorePending && fake[8].number==0.8f && fake[6].number==1);
    failVar=-1;input::Restore();AssertOriginalProfile();assert(!input::active && !input::restorePending);
    assert(input::Enable());failVar=14;failValue=0;input::Restore();  // an axis fails to restore
    assert(input::active && input::restorePending && fake[14].number==3);
    failVar=-1;input::Restore();AssertOriginalProfile();assert(!input::active && !input::restorePending);
    // Positive PC pitch preserves native normal/inverted modes independently.
    assert(fake[10].number==0 && fake[11].number==1);
    for(const auto& write:writes)assert(write.find("joy_inverty")==std::string::npos);
    input::Shutdown();assert(!input::originalWriteVariables);
    std::puts("PASS: native input ABI, guard-first activation, once-only Xbox curve/sensitivity/axis mapping, keyboard owner on change, per-player preset setup once per join, distinct split-slot snapshots, inversion preserved, archive originals/live edits preserved, profile-only config write, partial-stage rollback, exceptional writer cleanup, exact restore and failed-restore retry.");
}
