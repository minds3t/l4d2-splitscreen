#pragma once
#include <cstdint>

// Every module-relative address (RVA) the bridge uses, as researched in one
// game build: Left 4 Dead 2 2.2.4.3, build 10097 (hashes in supported_build.h).
// Nothing else holds an address. In the plugin these are variables: at startup
// each one is looked up in the modules that are really loaded (signatures.inc,
// src/signatures.cpp), so they also hold for a build whose code only moved.
// After changing a row run tools/analysis/make_signatures.py.
// Struct field offsets and vtable slot indexes stay beside the code that
// interprets them, together with the instruction bytes each hook verifies.
namespace rva {
using Rva = std::uint32_t;
#ifdef SS_FIXED_ADDRESSES
// Test fixtures: the researched build's values, as constants.
#define SS_RVA(name, value) inline constexpr Rva name = value;
#else
#define SS_RVA(name, value) extern Rva name;
#endif

namespace matchmaking {
// CMatchSessionOfflineCustom and the framework
SS_RVA(kOfflineUpdate, 0x25230)
SS_RVA(kOfflineDestroy, 0x25650)
SS_RVA(kOfflineEvent, 0x25290)
SS_RVA(kFrameworkFrame, 0x1BFF0)
SS_RVA(kFrameworkFrameGlobal, 0x6B76C)
SS_RVA(kGetLocalPlayer, 0x7810)
// KeyValues
SS_RVA(kKvGetName, 0x301E0)
SS_RVA(kKvFindKey, 0x30A00)
SS_RVA(kKvGetInt, 0x30BD0)
SS_RVA(kKvGetUint64, 0x30C50)
SS_RVA(kKvSetString, 0x30E00)
SS_RVA(kKvSetInt, 0x30F00)
SS_RVA(kKvSetUint64, 0x30F20)
SS_RVA(kKvMakeCopy, 0x30FC0)
SS_RVA(kKvDelete, 0x311F0)
SS_RVA(kKvMergeFrom, 0x31300)
SS_RVA(kKvFromString, 0x316E0)
SS_RVA(kKvGetString, 0x31EA0)
// Player manager and the native PlayerLocal class
SS_RVA(kPlayerManagerGlobal, 0x6494c)
SS_RVA(kPlayerManagerVtable, 0x51a8c)
SS_RVA(kPlayerLocalVtable, 0x52530)
SS_RVA(kPlayerLocalMethod0, 0x9e40)
SS_RVA(kPlayerLocalMethod1, 0x9e50)
SS_RVA(kPlayerLocalMethod2, 0x9e60)
SS_RVA(kPlayerLocalMethod3, 0x9e70)
SS_RVA(kPlayerLocalMethod4, 0x9a80)
SS_RVA(kPlayerLocalMethod5, 0x9aa0)
SS_RVA(kPlayerLocalMethod6, 0x9ad0)
SS_RVA(kPlayerLocalMethod7, 0x9b90)
SS_RVA(kPlayerLocalMethod8, 0x9c10)
SS_RVA(kPlayerLocalMethod9, 0xb2e0)
SS_RVA(kPlayerLocalMethod10, 0xab60)
SS_RVA(kPlayerLocalMethod11, 0xa8a0)
}

namespace gameui {
// Base panel, input identity and the shared lobby
SS_RVA(kGetBasePanel, 0x9A90)
SS_RVA(kSetActiveUser, 0x9B00)
SS_RVA(kGetActiveUser, 0x9B30)
SS_RVA(kDecodeButtonUser, 0xE880)
SS_RVA(kIsModeWithDifficulty, 0x11380)
SS_RVA(kDefaultDifficulty, 0x113B0)
SS_RVA(kLobbyPlayerName, 0x15BA0)
SS_RVA(kDropdownCommand, 0x2D440)
SS_RVA(kOpeningFocusLookupReturn, 0x2D4A8)
SS_RVA(kFindFlyoutButton, 0x2DA40)
SS_RVA(kLobbySummaryLabel, 0x4F050)
SS_RVA(kAvatarLookupReturn, 0x4F964)
SS_RVA(kLobbyUpdateSettings, 0x50010)
SS_RVA(kSettingsControlsNetworkReturn, 0x543D5)
SS_RVA(kSettingsLayoutNetworkReturn, 0x55A76)
SS_RVA(kHybridMousePressed, 0x5ADD0)
SS_RVA(kUiKeyValuesString, 0xC2E10)
SS_RVA(kButtonMouseReleased, 0xE6700)
// Console Audio/Video page: Activate, and DropDownMenu::SetCurrentSelection
SS_RVA(kAudioVideoActivate, 0x25ca0)
SS_RVA(kAudioVideoKeyPressed, 0x25ee0)
SS_RVA(kDropDownSetSelection, 0x2d180)
// Pause menu command handler and GameUI's engine interface pointer
SS_RVA(kInGameMenuCommand, 0x5ee40)
SS_RVA(kEngineInterfaceGlobal, 0x204640)
// GameUI's active split-screen slot, which scopes its engine commands
// Footer: menu-mode predicate callers, button draws, layout
SS_RVA(kFooterVisibleQueryReturn, 0x4284e)
SS_RVA(kFooterGlyphQueryReturn, 0x43b16)
SS_RVA(kFooterLayoutQueryReturn, 0x43f64)
SS_RVA(kFooterDrawQueryReturn, 0x44033)
SS_RVA(kFooterSetButtons, 0x43380)
SS_RVA(kFooterDrawButton, 0x43960)
SS_RVA(kFooterBackgroundReturn, 0x44023)
SS_RVA(kFooterButton0Return, 0x44077)
SS_RVA(kFooterButton1Return, 0x440a4)
SS_RVA(kFooterButton2Return, 0x440d1)
SS_RVA(kFooterButton3Return, 0x440fe)
SS_RVA(kFooterButton4Return, 0x4412b)
SS_RVA(kLobbySetButtonsCall, 0x4b463)
SS_RVA(kLobbySetButtonsReturn, 0x4b468)
SS_RVA(kDrawFooterBackground, 0xf420)
SS_RVA(kFooterVtable, 0x16e7b4)
SS_RVA(kFooterLayoutTable, 0x1f8918)
// Generic confirmation dialog
SS_RVA(kConfirmationPaint, 0x57090)
SS_RVA(kConfirmationModeReturn, 0x570d5)
SS_RVA(kConfirmationVtable, 0x1753a4)
// HybridButton
SS_RVA(kHybridPaint, 0x5b700)
SS_RVA(kHybridPaintWrapper, 0x5c610)
SS_RVA(kHybridPaintReturn, 0x5c618)
SS_RVA(kHybridApplySettings, 0x5c680)
SS_RVA(kHybridButtonVtable, 0x176cec)
// vgui::Panel helpers and globals
SS_RVA(kPanelGetBounds, 0xca680)
SS_RVA(kPanelSetVisible, 0xca750)
SS_RVA(kPanelGetScheme, 0xcac10)
SS_RVA(kPanelFindChild, 0xceb70)
SS_RVA(kSchemeManagerGlobal, 0x22159c)
// Title-safe gate in Panel::ApplySettings and the resource reload
SS_RVA(kReloadMenuResources, 0x16130)
SS_RVA(kMenuSafeAreaGate, 0xd9648)
SS_RVA(kMenuSafeAreaPath, 0xd9651)
SS_RVA(kMenuReloadObjectA, 0x221504)
SS_RVA(kMenuReloadObjectB, 0x1ffb10)
}

namespace inputsystem {
SS_RVA(kInputSystemVtable, 0x142e4)
SS_RVA(kInputSystemSlot27, 0x10b0)
SS_RVA(kMenuModePredicate, 0x4a50)
// XInput polling: Init's "a pad is present" branch, the frame poll's gate, the
// XInputGetState call in the poll, and the joy_xcontroller_found cvar
SS_RVA(kXInputFoundAtInit, 0x270a)
SS_RVA(kXInputPollGate, 0x1481)
SS_RVA(kXInputGetStateCall, 0x626f)
SS_RVA(kXInputGetStatePointer, 0x21074)
// Init's three stores of the resolved XInput functions (state, vibration,
// capabilities) into consecutive pointers starting at the one above
SS_RVA(kXInputStoreState, 0x61e2)
SS_RVA(kXInputStoreVibration, 0x61f5)
SS_RVA(kXInputStoreCapabilities, 0x620f)
SS_RVA(kControllerFoundCvar, 0x1fcd0)
SS_RVA(kConVarSetInt, 0x6d10)
}

namespace engine {
// VEngineClient013
SS_RVA(kEngineClient, 0x425504)
SS_RVA(kEngineClientVtable, 0x359DEC)
SS_RVA(kIsInGame, 0x5F1A0)
SS_RVA(kIsConnected, 0x5F1B0)
SS_RVA(kKeyLookupBindingEx, 0x5e1e0)
// Key bindings
SS_RVA(kKeySlot, 0x1a2470)
SS_RVA(kMatchBinding, 0x1a27d0)
// VEngineServer022 and edicts
SS_RVA(kEngineServerVtable, 0x37d9bc)
SS_RVA(kGetClientXuid, 0x132ac0)
SS_RVA(kNumForEdict, 0x1e0030)
SS_RVA(kEdictCount, 0x64576c)
SS_RVA(kEdictPool, 0x645774)
// Host_WriteConfiguration
SS_RVA(kWriteArchivedVariables, 0x15de80)
SS_RVA(kWriteConfigCall, 0x18fcd9)
SS_RVA(kWriteConfigReturn, 0x18fcde)
// Map start: CHostState::State_NewGame and Host_NewGame
SS_RVA(kStateNewGame, 0x1a0220)
SS_RVA(kStateNewGameRequestRead, 0x1a0236)
SS_RVA(kStateNewGameForward, 0x1a02f4)
SS_RVA(kHostNewGameSplitGate, 0x18edb2)
SS_RVA(kHostNewGameCapacityOperand, 0x18edb9)
SS_RVA(kSplitCapacity, 0x42ffc0)
// CNetChan
SS_RVA(kChannelShutdown, 0x1bb950)
SS_RVA(kSplitMerge, 0x1b9240)
SS_RVA(kSplitMergeCall0, 0x1b9c1d)
SS_RVA(kSplitMergeCall1, 0x1b9c2a)
SS_RVA(kSplitMergeCall2, 0x1b9c37)
// ISplitScreen pointer (slot 7: is this split-screen slot in use)
SS_RVA(kSplitScreenInterface, 0x4268e4)
// Cbuf_AddText(slot, text, delay): one command buffer per split-screen slot
SS_RVA(kCbufAddText, 0x149c20)
}

namespace server {
// Return addresses after GetClientXUID in the Director avatar cache
SS_RVA(kAvatarInfoReceiveReturn, 0x274faa)
SS_RVA(kAvatarRecordLookupReturn, 0x26cded)
SS_RVA(kAvatarPreferenceReturn0, 0x26cf23)
SS_RVA(kAvatarPreferenceReturn1, 0x26da98)
}

namespace client {
// C_BasePlayer::GetLocalPlayer(slot) and the per-view screen-effect query
SS_RVA(kLocalPlayer, 0x634e0)
SS_RVA(kScreenEffectQuery, 0x257830)
// Globals
SS_RVA(kEngineInterfaceGlobal, 0x735e3c)
SS_RVA(kInForceUserCvar, 0x75a4a8)
SS_RVA(kHudReloadObject, 0x88b7ec)
SS_RVA(kSurfaceGlobal, 0x88b8f4)
// Title-safe gate in Panel::ApplySettings and the HUD reload
SS_RVA(kReloadHud, 0xc2bc0)
SS_RVA(kHudSafeAreaGate, 0x3f3128)
SS_RVA(kHudSafeAreaPath, 0x3f3131)
// vgui::Panel helpers
SS_RVA(kLabelSetVisible, 0x3e4740)
SS_RVA(kPanelGetWide, 0x3e59c0)
SS_RVA(kPanelGetTall, 0x3e59f0)
SS_RVA(kPanelFindChild, 0x3e8af0)
SS_RVA(kGetScreenSize, 0x1b2a80)
SS_RVA(kGetLocalPlayer, 0x251a50)
// CHudWeaponSelection
SS_RVA(kWeaponSelectionVtable, 0x5d5734)
SS_RVA(kWeaponSelectionRootVtable, 0x5d5a84)
SS_RVA(kWeaponSelectionPaint, 0x2d11e0)
SS_RVA(kBindingListener, 0x2ce2c0)
SS_RVA(kDrawInactiveBox, 0x2ce4c0)
SS_RVA(kDrawActiveBox, 0x2ce5b0)
SS_RVA(kDrawItemGlyph, 0x2ce980)
SS_RVA(kHudTextureDraw, 0x10cf20)
// Return addresses of the item draws inside CHudWeaponSelection::Paint
SS_RVA(kFlashlightInactiveReturn, 0x2d1bd2)
SS_RVA(kFlashlightIconReturn, 0x2d1c08)
SS_RVA(kFlashlightGlyphReturn, 0x2d1c1f)
SS_RVA(kThrowableActiveReturn, 0x2d1c90)
SS_RVA(kThrowableInactiveReturn, 0x2d1ca1)
SS_RVA(kThrowableIconReturn, 0x2d1cfd)
SS_RVA(kThrowableGlyphReturn, 0x2d1d13)
SS_RVA(kFirstAidActiveReturn, 0x2d1d85)
SS_RVA(kFirstAidInactiveReturn, 0x2d1d97)
SS_RVA(kFirstAidIconReturn, 0x2d1df3)
SS_RVA(kFirstAidGlyphReturn, 0x2d1e09)
SS_RVA(kPillsActiveReturn, 0x2d1e78)
SS_RVA(kPillsInactiveReturn, 0x2d1e8a)
SS_RVA(kPillsIconReturn, 0x2d1ee6)
SS_RVA(kPillsGlyphReturn, 0x2d1efc)
// CBindPanel::Update binding lookup
SS_RVA(kBindPanelLookupCall, 0x2d68de)
SS_RVA(kBindPanelLookupReturn, 0x2d68ec)
// %+command% replacement in hint and tip text (push sequence and return)
SS_RVA(kHintTextLookupCall, 0xb4a54)
SS_RVA(kHintTextLookupReturn, 0xb4a62)
// CLocatorTarget::SetBinding: vtable load, call and return of each lookup
SS_RVA(kLocatorLookup0Load, 0x2b1b59)
SS_RVA(kLocatorLookup0Call, 0x2b1b71)
SS_RVA(kLocatorLookup0Return, 0x2b1b73)
SS_RVA(kLocatorLookup1Load, 0x2b1b8e)
SS_RVA(kLocatorLookup1Call, 0x2b1bae)
SS_RVA(kLocatorLookup1Return, 0x2b1bb0)
SS_RVA(kLocatorLookup2Load, 0x2b1bd9)
SS_RVA(kLocatorLookup2Call, 0x2b1bf0)
SS_RVA(kLocatorLookup2Return, 0x2b1bf2)
// CHudVote
SS_RVA(kVoteStart, 0x2db130)
SS_RVA(kVoteHudVtable, 0x5dab34)
SS_RVA(kVoteHudElementVtable, 0x5daae4)
}
}
