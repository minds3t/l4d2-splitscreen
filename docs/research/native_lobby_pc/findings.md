# Native PC offline lobby investigation — 2026-10-10

This is read-only evidence from the installed PC binaries and the extracted Xbox resources. No game files were patched or installed for this investigation. Addresses below are **module-relative RVAs**, not live addresses. Function names marked “inferred” describe behavior; they are not debug symbols.

## Modules

| Module | Installed relative path | SHA-256 |
| --- | --- | --- |
| gameui.dll | bin/gameui.dll | `321ec9b5865582330af61e81eb347da31cc213cc6c95b80d6d98170b8a5fbb40` |
| matchmaking.dll | left4dead2/bin/matchmaking.dll | `b405c7a2b786775e1940eccebcd6756335ceb20b583b43a05df7a457f9fe0e61` |

Installation root: `C:\Program Files (x86)\Steam\steamapps\common\Left 4 Dead 2`.

`disassembly.json` (not kept in the repository) contains the module metadata, instruction bytes, normalized RVA references, and string annotations. Reproduce with `python docs/research/native_lobby_pc/capture_evidence.py` using the current installed modules. Compare hashes before treating an RVA as reusable.

## Main result

The PC UI still has a real **offline shared GameLobby** route. It chooses that route when the session reports more than one member. The PC offline-session initializer, however, explicitly constructs only one matchmaking member. Separate controller character selection has another compiled PC limitation: the GameLobby character command uses local player index zero.

These are separate layers:

1. Offline matchmaking member creation.
2. Shared lobby display and native avatar state.
3. Controller-to-local-profile routing for selection.
4. Slot-specific avatar delivery at game connection.

The existing flyouts/VScript preferences do not restore this prelaunch shared-lobby system. Making two static character dropdowns is also insufficient: the native Xbox resource uses one shared character control, with player identity supplied by code.

## Resource comparison

Relevant files:

- `xbox_ui/left4dead2/resource/ui/l4d360ui/mainmenu.res`: the offline single-player tile and offline split-screen tile both use `SoloPlay` (around lines 125–137). The Xbox resource does not require a different lobby class/command for the second tile.
- `xbox_ui/left4dead2/resource/ui/l4d360ui/gamelobby.res`: `GplSurvivors` is the dynamic player list; `DrpCharacter` is the shared character dropdown; `FlmCharacterFlyout` loads `DropDownCoopCharacters.res`; `BtnStartGame` emits `StartGame`.
- `game_pc/resource/ui/l4d360ui/gamelobby.res`: retains those same controls and commands, with additional modern readiness/cancel-start controls.
- Both versions have `survivorlistitem.res`: native portrait, gamer picture, voice state, and player-dropdown controls. Lobby player rows are constructed by code.
- Both versions have one `DrpCharacter` in `gamesettings.res`; the current PC version adds mode-specific survival handling.

Comparison command: `python tools/analysis/cmpres.py game_pc/resource/ui/l4d360ui/gamelobby.res xbox_ui/left4dead2/resource/ui/l4d360ui/gamelobby.res X360 X360`.

Under the same X360 condition set, differences are mostly current-PC readiness buttons, cancel command, background alpha, summary width, and spinner position. There is no missing Xbox-only second static character selector. Retaining current PC resources is preferable to replacing them wholesale with the older Xbox files.

## Session-ready window selection survives on PC

Module: `gameui.dll`.

- OnMatchSessionUpdate handler (inferred): RVA `0x0000DDA0`.
- Ready-state branch reads network and member count: RVAs `0x0000DE37`–`0x0000DE83`.
- Window 9 construction calls GameLobby constructor: RVA `0x0000AE33`.

Pseudocode:

```cpp
if (event.name == "OnMatchSessionUpdate" && event.state == "ready") {
    settings = matchFramework.GetMatchSession()->GetSessionSettings();
    window = 9; // GameLobby
    if (settings.GetString("System/network", "") == "offline"
        && settings.GetInt("Members/numPlayers", 1) <= 1) {
        window = 11; // GameSettings
    }
    OpenWindow(window, settings);
}
```

This is positive evidence for an offline two-member shared lobby, not a conclusion inferred from online UI. The constructor/resource-loader path remains compiled. The later native prototype below demonstrated the actual two-member offline case.

GameLobby also explicitly handles offline exit without the online leave-confirmation flow at RVA `0x0004B490`, network branch `0x0004B4B4`–`0x0004B502`.

## PC offline initializer forces one member

Module: `matchmaking.dll`.

- `CMatchSessionOfflineCustom` constructor (identified by literal): RVA `0x000255E0`.
- InitializeGameSettings (identified by debug literal): RVA `0x000253E0`.
- The constructor calls initializer at RVA `0x0002563E`.
- First session Update (inferred): RVA `0x00025230`.

Pseudocode for relevant initialization:

```cpp
settings["System/network"] = "offline";
settings["System/access"] = "public";
settings["Options/server"] = "listen";
members["numMachines"] = 1;
members["numPlayers"] = 1;
members["numSlots"] = 1;
machine0["id"] = GetLocalPlayer(0)->GetXUID();
machine0["numPlayers"] = 1;
machine0["dlcmask"] = localDlcMask;
machine0["tuver"] = localTitleUpdate;
machine0["ping"] = 0;
machine0.player0["xuid"] = GetLocalPlayer(0)->GetXUID();
machine0.player0["name"] = GetLocalPlayer(0)->GetName();
matchExtension.PrepareForSessionCreate(settings, "host");
```

Exact count writes: RVAs `0x0002546B`–`0x00025490`; machine0 player count at `0x000254D2`; player0 creation at `0x00025513`–`0x00025575`.

The extension call after initialization can further adjust settings. Therefore `numSlots=1` is the initializer's value, not a claim that the final gameplay session always allows only one survivor slot. The confirmed issue is that this PC initializer has no second-local-player enumeration.

First Update pseudocode:

```cpp
if (state == 0) {
    state = 1;
    Broadcast("OnMatchSessionUpdate", { "state": "ready" });
}
```

This gives a concrete prototype point after initialization and before the first ready notification.

## Offline session interface can carry native updates

`matchmaking.dll` offline-session vtable at RVA `0x00054E60`:

| Byte offset | Target RVA | Observed behavior |
| --- | --- | --- |
| +0x00 | 0x00025380 | Returns null system-data pointer |
| +0x04 | 0x000251E0 | Returns session settings KeyValues |
| +0x08 | 0x000251F0 | Applies session update via extension and KeyValues merge, notifies update |
| +0x0C | 0x00025840 | Command handler (`Start` and other native commands) |
| +0x10 | 0x00025230 | First-update ready notification |

The offline command handler allocates a changed-member array using `Members/numPlayers`, dispatches non-Start commands through the match extension, emits `OnPlayerUpdated` for changed members, and emits `Command::<original command>` (RVAs `0x000259C4`–`0x00025B38`). This route is not restricted to a single array element in its general command code.

## GameLobby already displays multiple players per machine

Module: `gameui.dll`, population function (inferred) RVA `0x0004FA40`.

```cpp
list.Clear();
for (int slot = 0; slot < Members.numSlots; ++slot)
    list.Add(new SurvivorListItem());
for (int machine = 0; machine < Members.numMachines; ++machine)
    for (int player = 0; player < machine.numPlayers; ++player)
        AddOrUpdatePlayer(machine.player[player]); // RVA 0x0004EEE0
```

The per-machine inner loop is at RVAs `0x0004FB60`–`0x0004FBB1`. There is no one-player-per-machine UI filter in this population loop. Two valid members in one machine can produce two native portrait/name rows.

Selected-avatar enumeration at RVA `0x0004DC90` similarly traverses all machine/player records and reads `game/Avatar`, enabling native availability feedback in the shared dropdown.

## GameLobby character command is pinned to local slot 0

Module: `gameui.dll`, `character_` branch RVAs `0x0004F904`–`0x0004F9B7`.

```cpp
if (StartsWith(command, "character_")) {
    KeyValues change("Game::Avatar");
    change["run"] = "host";
    change["xuid"] = playerManager.GetLocalPlayer(0)->GetXUID();
    change["avatar"] = command + strlen("character_");
    session.Command(&change);
}
```

Literal local index zero is pushed at RVA `0x0004F960`. A second path assigning the Infected avatar likewise uses literal zero at RVA `0x0004FF6E`.

Native avatar strings include `gambler`, `producer`, `coach`, `mechanic`, `namvet`, `teengirl`, `biker`, `manager`, `Infected`, or empty for Random. These are matchmaking avatar names, not the display names Nick/Rochelle/etc used by `jointeam` commands.

`GameLobby::OnKeyCodePressed` and central UI controller-identity handling are being traced separately by the input-runtime agent. No claim is made here that forcing `gameui_xbox` alone restores the missing per-controller profile selection.

## Native avatar updates preserve selection state and uniqueness

Module: `matchmaking.dll`.

- `Game::Avatar` dispatch branch: RVA `0x00006ED0`.
- Avatar-update helper (inferred): RVA `0x00006B80`.

The command is checked for a valid host execution context. The helper finds the session member by XUID, reads its prior `Game/avatar`, ignores an unchanged selection, then either clears the avatar for Random or checks availability across all members. A named survivor can be selected once; Infected allows up to four. On success it writes the member's `Game/avatar` and adds that member to the changed-player output used by `OnPlayerUpdated`.

This offers native persistent prelaunch choices, portrait updates, and duplicate-selection handling. It is a stronger foundation than post-load survivor transfers if the two local identities can be supplied correctly.

## Launch avatar propagation: retained code and live prototype

Independent input-runtime investigation located the split-client event route. This note also inspected its avatar-delivery function at matchmaking RVA `0x00003B80` (captured in disassembly.json):

```cpp
avatarinfo["xuid"] = playerManager.GetLocalPlayer(slot)->GetXUID();
member = FindSessionMemberByXUID(session.settings, xuid);
if (member) {
    avatarinfo["avatar"] = member["Game/avatar"];
    avatarinfo["team"] = avatar == "Infected" ? "Infected" : "Survivor";
    // Server/team can override the derived team.
}
oldSlot = engine.GetActiveSplitScreenPlayerSlot();
engine.SetActiveSplitScreenPlayerSlot(slot);
engine.ServerCmdKeyValues(avatarinfo);
engine.SetActiveSplitScreenPlayerSlot(oldSlot);
```

Observed engine interface offsets: +0x1F8 reads active slot, +0x1FC changes it, +0x2B0 sends the KeyValues. The local-player index is the supplied slot, unlike the PC lobby's hardcoded character command index.

The root agent subsequently live-tested native build `cc75d4f0`: the actual offline GameLobby displayed Player and Player (2), with distinct host/guest XUIDs. Native `Game::Avatar` requests updated each portrait independently. Starting that lobby preserved the offline session and profiles, loaded c1m1 with two active humans, and once produced P1 Ellis/P2 Coach matching the lobby choices, with Nick/Rochelle bots. No `ss_character` preferences were set for this test. A repeat launch retained P2 `coach` in matchmaking but spawned P2 Nick. Thus the first matching spawn did not establish reliable propagation and may have been coincidental.

The user later confirmed physical gameplay independence and independent lobby character selection on build `e671`. The root agent also confirmed that the native avatarinfo sender dispatches P2's requested avatar, while the actual engine guest XUID is zero. Independent input-agent tracing found the server avatar receiver rejects zero engine XUID and keys its cache by engine identity; that bridge is being implemented separately. Online guest sessions remain unimplemented/unverified.

## Concrete prototype route

1. Keep the native `SoloPlay_<mode>` entry and native offline session.
2. Provide a real local slot-1 matchmaking profile/guest identity; cloning the host Steam identity is not sufficient for two independent XUID-keyed selections.
3. Extend the offline session's machine0 to contain both local players, with correct total/player/slot counts, after native initialization and before ready notification. Use the native session update API where practical.
4. Let the existing ready handler open GameLobby and its native dynamic player list.
5. Route character commands to the local profile associated with the controller that invoked them. Preserve `Game::Avatar` rather than replacing native selection state.
6. Start using the native lobby command, then verify both slot-specific `avatarinfo` messages and resulting player/character ownership.

This calls for a narrowly scoped native bridge or equivalent supported native entry point. Resources alone cannot supply the missing profile enumeration and index dispatch. The exact Xbox implementations remain the authoritative next comparison before choosing bridge behavior.

## Mouse ownership and the shared dropdown selection

Module and fingerprint are unchanged. New evidence ranges are in `disassembly.json`.

- HybridButton::OnMousePressed: RVA `0x0005ADD0`, `thiscall(button, MouseCode)`.
- Inherited Button::OnMouseReleased: RVA `0x000E6700`, same signature.
- DropDownMenu::OnCommand: RVA `0x0002D440`, `thiscall(dropdown, const char* command)`.
- FlyoutMenu::FindButtonByCommand: RVA `0x0002DA40`, `thiscall(flyout, const char* command) -> button`.
- The dropdown opening-focus lookup returns to RVA `0x0002D4A8`.

The mouse path invokes VGUI button commands independently of the joystick/keyboard decoder. The bridge now marks mouse press/release as P2 in its owned offline prelaunch session and keeps that active identity for deferred VGUI command delivery. During synchronous mouse dispatch the decoder preserves that source even if a control synthesizes a button code. Real pad0/pad1 decoding is unchanged outside this mouse scope; keyboard codes below the native controller range continue routing to P2.

The character dropdown stores one shared last-command buffer at `DropDownMenu+0x18C`; opening it passes that remembered command to `FindButtonByCommand`, then sends the returned native button as initial focus to the real flyout. It does not look up the active player's avatar. Therefore a correct P2 character commit can still reopen with the prior P1 choice focused.

The bridge scopes an opening context to the exact `FlmCharacterFlyout` / `FlmVersusCharacterFlyout` command. Only the opening-focus lookup at the return site above substitutes `character_<active member's Game/avatar>`, with native Random fallback. Native buttons, occupancy, portrait update, navigation, and command handling remain in control. It deliberately does not call `DropDownMenu::SetCurrentSelection` (RVA `0x2D180`), because that method also invokes the parent command and would mutate character choices merely by opening the menu.

The root live-tested these mouse hooks on build `e671`: clicking Coach changed member P2 to `coach`, preserved P1 `mechanic`, and kept active local user1. The user then confirmed both physical input devices independently select their own characters. The bridge logs distinct decoded input codes with native/routed slots to diagnose physical ownership separately from session/avatar correctness.

## Intentional return to the native lobby

The stock pause action `ReturnToLobby` sends `callvote ReturnToLobby;` at GameUI RVA `0x5F0CC`. The offline session's event handler at matchmaking RVA `0x25290` explicitly handles `OnEngineEndGame` by issuing `disconnect` and queuing `mmF->CloseSession`. An ordinary signon transition from >=2 to <2 also queues CloseSession. Root's live test confirmed the stock result was main menu with both session and guest removed.

The new return bridge captures a native KeyValues MakeCopy only for `OnEngineEndGame` on the exact owned, running offline session (`session+4` event-sink subobject; native state2). It preserves Game/Options and mission metadata, removes stale Server/Members and System/lock from the private copy, then lets all native disconnect/close handling run. It never arms from an ordinary disconnect.

An independent native event subscriber observes host-slot signon falling below2, even after the old offline session has unsubscribed. The exact framework RunFrame hook (RVA `0x1BFF0`, framework vtable slot5) waits for that event, no active session, and one empty frame before calling the normal CreateSession API. First-update expansion restores the two saved avatar strings before the native ready event. A different new session, explicit close/disable, or 15-second teardown timeout cancels the pending return. All event subscriptions, private KeyValues, and hooks are cleaned on shutdown.

The SDK documents `OnEngineEndGame` as the game-ending/return-to-lobby event and distinguishes it from disconnect reason and signon events: [IMatchEvents](https://raw.githubusercontent.com/alliedmodders/hl2sdk/l4d2/public/matchmaking/imatchevents.h). The public framework provides RunFrame, event subscription, CreateSession, and CloseSession: [IMatchFramework](https://raw.githubusercontent.com/alliedmodders/hl2sdk/l4d2/public/matchmaking/imatchframework.h). Native RVAs and KeyValues copy/link layout are independently confirmed in the captured disassembly.

Root live-tested build `60de5a28`: intentional ReturnToLobby did recreate the real owned two-member lobby and preserve both avatar strings. Two follow-ups were identified: native initialization reset the saved No Mercy campaign to Dead Center, and gameplay had left the menu's joystick/guard values disabled despite the bridge input snapshot still being active.

The follow-up source applies the saved member-free settings through native `IMatchSession::UpdateSessionSettings` (vtable slot2, RVA `0x251F0`) after creation and before the first ready update. It makes a separate constructor input copy so initialization cannot alter the preserved snapshot. The native update path retains the title extension's validation and KeyValues merge. Diagnostics now print campaign/chapter/difficulty/map. While the bridge is enabled, the frame hook also calls input's `MaintainMenu` for the exact owned prelaunch lobby or a null matchmaking session after teardown, preserving controller access from the main menu. Other matchmaking sessions are untouched. That helper restores guard before joystick while preserving the original activation snapshot.

Root's `bece97bb` live retest confirmed menu input returns to guard1/joystick1/split2, but the flat settings payload still left the default campaign. Further disassembly established why: native merge RVA `0x31300` with type0 consumes an `Update` / `Delete` / `Borrow` envelope, so a flat Game/Options tree is ignored. The corrected source builds a native `Settings { Update { ...saved settings... } }` delta using the same merge helper's type1, then passes that envelope to the public session API. No timing change is involved.

Root live-tested the correction in `f0aaadea`: Realism, No Mercy, chapter2, Easy, P1 Bill/P2 Louis launched with read-only observer confirming human namvet/manager and keyboard owner1. ReturnToLobby preserved `Game/mode=realism`, `Game/campaign=L4D2C8`, chapter2, Easy, and both avatars `gambler`/`coach`. The native lobby displayed the same mission/chapter/difficulty, and menu input remained guard1/joystick1/split2. This verifies native settings and avatar preservation across intentional return without post-load character transfers in that test.

## Editing an existing offline shared lobby

Root's native-VPK live test reached a real two-member lobby through OFFLINE CO-OP and mode choice. However, Change Game Settings opened the solo layout (Character and Start Game), and Back destroyed the session while leaving a visually populated GameLobby. The following two GameUI classifications account for both problems:

- ApplyScheme reads `System/network` at RVA `0x55A71` (return RVA `0x55A76`). An offline result forces generic GameSettings.res or its scripted-solo counterpart. Otherwise the existing navigation parent is inspected, and a GameLobby parent selects native `GameSettings_<mode>Edit.res`.
- UpdateControls reads the same value at RVA `0x543D0` (return RVA `0x543D5`). Its offline classification sets the session-ownership byte at `GameSettings+0x2F4` and prevents the existing-lobby editing flag at `+0x2EC`. OnClose at RVA `0x53650` checks that ownership byte and calls framework CloseSession. The shared lobby was consequently left without a backing session.

The source fix hooks the native GameUI KeyValues string accessor at RVA `0xC2E10`. Only when the receiver is the exact owned session settings pointer, the session is offline and prelaunch, the key/value are `System/network` / `offline`, and the return address is one of those two UI sites does it return a temporary `lan` classification. It changes no KeyValues and does not alter the actual offline matchmaking or launch settings. Other readers, including the offline title, retain the original value.

This reuses the existing PC mode-specific Edit resources and existing live-update/back lifecycle: the original native controls edit the same session, and returning does not destroy it. The checked PC resources include Edit variants for coop, realism, survival, versus, and scavenge. The GameSettings settings pointer is the live session pointer in this path (native assignment at RVAs `0x53221` and `0x53228`), rather than a UI copy. No global offline label or resource replacement is required.

Status: root live-tested build `60de5a28`: native Edit controls show campaign, chapter, difficulty, a disabled server-type row, mode change, and Back. Solo Start/Character controls are absent. Changing to No Mercy and returning retained the owned offline session, two members, and the changed mission. The L4D1 native character flyout then showed Bill/Zoey/Louis/Francis.

## Character diagnostic aliases across casts

The native flyout keeps `character_gambler`, `character_producer`, `character_coach`, and `character_mechanic` commands while changing displayed names for the current survivor cast. Root clicked Zoey and observed member avatar `producer`. GameUI RVA `0x66AA0` independently builds the L4D1 label array in Bill, Zoey, Louis, Francis order against the L4D2 Nick, Rochelle, Coach, Ellis slot order; the same function explicitly pairs namvet/gambler, teengirl/producer, manager/coach, and biker/mechanic at RVAs `0x66C5E` through `0x66D0E`.

The diagnostic `ss_native_avatar` alias table is corrected in source to emit those four native lobby slots for either cast's names. The earlier diagnostic `Bill -> namvet` path did not match what the actual GameLobby emits. Actual character menu input and native session command handling are unchanged by this diagnostic-only correction. As in the native lobby, the campaign determines which cast the selected slot represents.

## Native competitive team choice is capacity-gated

Root observed a Scavenge lobby with only the four survivor choices and Random, although diagnostic native `Game::Avatar(Infected)` successfully produced a human infected player in Versus gameplay. Stock `dropdownversuscharacters.res` already provides a localized Infected button with `character_infected` and full keyboard/controller navigation. GameLobby contains both `DrpCharacter`/`FlmCharacterFlyout` and `DrpVersusCharacter`/`FlmVersusCharacterFlyout`; no new UI flow is required.

The compiled GameLobby selects these controls using `Members/numSlots > 4`, not a direct mode-name comparison. The state/selection routine reads at GameUI RVA `0x4DCEC` and compares at `0x4DCF1`; the visibility setup reads at `0x4FDC1` and compares at `0x4FDC6`. At <=4 it selects the cooperative dropdown; above4 it selects the Versus dropdown. The previous bridge expanded the native offline capacity to at least2, preserving the offline initializer's small capacity even in competitive modes.

The corrected source sets proper nominal capacity8 for owned Versus/Scavenge sessions before ready while retaining exactly two actual members. Other modes keep their previous minimum2 behavior. Root live-tested `f0aaadea` in Scavenge: the actual native Infected entry appeared, P2 selected it with the mouse, and starting produced human P1 Nick on team2 and human P2 Boomer on team3, keyboard owner1, with sv_cheats0. Return preserved the settings, capacity8, and avatars. Physical controller0 then changed only P1 to Infected in the returned lobby, confirmed by the native input/character log. Enabled-lobby quit retained the prior archived joystick value (text formatting changed from0 to0.000000 only).

This validates the retained native competitive character/team UI and per-input ownership in the tested local offline session. It does not establish online guest support or two physical-controller behavior, which remain separate tests.

## Xbox presentation comparison and safe resource scope

This comparison uses the extracted disc resources, not a live Xbox capture. The shared GameLobby/session behavior is now restored, but build `f0aaadea` still uses the PC GameLobby presentation. The actual disc's Xbox layout and the retained current-PC X360 branches agree on the main geometry:

| Element | Current PC branch | Xbox branch / disc reference |
| --- | --- | --- |
| GameLobby panel height | 440 proportional units | 380 |
| Main action and dropdown height | 15 | 20 |
| Main action icons | 15 × 15 | 20 × 20 |
| Left action column, English | center −240 | center −260 |
| Character / settings / start vertical position | 226 / 206 / 306 | 235 / 210 / 310 |
| PC chat history, text entry, voice button | present | absent |
| Leave lobby action | explicit PC button | console back flow |
| Dropdown button ActivationType | unset | 1 |

Under X360 evaluation, the current PC GameLobby resource differs from the actual ISO mainly in newer request-start/cancel controls, updated cancel command/navigation, summary width240 rather than140, spinner position, and a background alpha that is irrelevant while the console branch hides that background. Preserving current command/control compatibility while selecting its retained Xbox geometry is therefore preferable to replacing the current file wholesale with the older disc resource. The ISO mainmenu also contains a malformed language conditional ending in an unmatched `)`, further arguing against blind transplantation.

Native GameLobby ApplyScheme at GameUI RVA `0x50D30` constructs `Resource/UI/L4D360UI/GameLobby.res` (TeamLobby for team modes) and calls LoadControlSettings through vtable byteoffset `0x34C` at `0x50DB2`, with all three optional arguments zero. It supplies no built-in offline condition. A generated resource overlay consequently affects that resource wherever the native experiment package is active; a strictly owned-lobby-only resource would require a filename/load hook rather than merely adding `?condition?offline` blocks. No such hook is needed for the first resource overlay prototype.

The PC GameLobby constructor creates ChatInputLine at RVA `0x50CF7` and LobbyChatHistory at `0x50D15` even if those sections are absent from the resource. An Xbox conversion must therefore retain explicit hidden/inert definitions for these PC objects rather than merely dropping the WIN32 blocks and leaving constructor defaults active. Retaining other PC-only child controls hidden/inert likewise avoids invalid native lookups. Native action names, current readiness controls, dynamic player rows, and the already proven per-input avatar/session routing should remain unchanged.

Presentation follow-up is in progress: build agent owns the native-only resource generator; input agent owns footer/back/focus behavior. There is no claim of exact rendered Xbox equivalence until the converted menus are visually and physically tested.

### Native child and font audit

SurvivorListItem ApplyScheme at GameUI RVA `0x4D2A0` resolves DrpPlayer, ImgPlayerPortrait, PnlGamerPic, LblPlayerVoiceStatus, LblPartyIcon and LblEmptySlotAd into cached fields. Every shown lookup is null-checked and initially hidden/disabled. The generated overlay retains each PC-only control with zero dimensions as well as hidden/disabled state, so subsequent native visibility changes cannot expose desktop-only layout. GplSurvivors remains the native code-created list; real member rows and their native state are retained.

HybridButton style2 (FlyoutMenuButton) selects DefaultMedium / DefaultMediumBlur on both builds: current PC GameUI RVA `0x5C765` and Xbox GameUI RVA `0x8499C`. Style1 selects MainBold / MainBoldBlur on both. The native package scheme defines DefaultMedium at tall16 and MainBold at tall20 using Stubble bold. A resource-only font change from flyout style2 to style1 would be a readability adaptation rather than restoration of a platform-specific code branch.

### Footer presentation scope

`UseConsoleMenuPresentation` uses the real VEngineClient013 factory interface, verifying the known static object at engine RVA `0x425504`, vtable `0x359DEC`, IsInGame entry26 (`0x5F1A0`) and IsConnected entry27 (`0x5F1B0`). Native IsConnected reads the selected engine client signon state and returns true from state2; this excludes loading and gameplay from the no-session main-menu presentation scope. The helper also requires explicit bridge activation and either the exact owned prelaunch offline session or no matchmaking session. An unrelated session never receives the presentation override.

The public interface reference is `public/cdll_int.h` in the AlliedModders hl2sdk, branch l4d2. Both slot ordering and implementation were independently verified in the fingerprinted engine.

The later pause-menu resource conversion also needs this footer. Its only consumers are the exact native CL4DFooterPanel paint queries in presentation.cpp; the helper now accepts the owned offline session while its game is connected as well. This enables footer painting for its active pause UI without changing gameplay input. A disconnected no-session menu still passes the engine connection check, and unrelated sessions still fail.

### Truthful offline access summary

GameLobby UpdateSettings at GameUI RVA `0x50010` reads `System/access` and uses it both for the summary text (`#L4D360UI_Access_%s`) and the real `GameAccess_*` dropdown command. Overriding that getter merely to change the caption would therefore alter behavior. The source instead hooks the whole update, calls the original unchanged, then verifies an owned prelaunch offline session and the panel's exact settings pointer at +0x2F0. It obtains the existing access label through native summary lookup RVA `0x4F050` and invokes the same Label::SetText virtual byteoffset0x350 used by the original at `0x50167`.

The replacement text is the existing localized token `#L4D360UI_Mode_offline_SS` (English: Offline Co-op). The label finder reads mode/base and LobbyDetailsSummary.res to identify an existing LblSummaryLine; it does not change layout or advance an index. Neither System/network nor System/access, the access dropdown, native commands, settings lifetime, or actual session policy is modified. The Xbox video reference showing System Link Game describes a LAN session and is not an accurate caption for this owned offline session. This new display-only hook awaits live validation.

### Gameplay Escape and hidden lobby chat audit

Root reported physical Escape failing to open gameplay pause after console presentation was enabled, while explicit gameui_activate worked and Escape closed that pause UI. This is not yet attributed to the resource conversion.

Engine's keyboard path at RVA `0x21FD67` normalizes the button, checks Escape (0x46), then if GameUI is not already visible checks the integer value of `cl_chat_active` through its ConVarRef at `0x21FDEF`. A nonzero value consumes the event without queuing gameui_activate. The cvar is a real client.dll ConVar at RVA `0x7579B8`, constructed at `0x4FFC90`; its secondary IConVar starts at +0x18. Client gameplay CHudChat start at `0x115B20` sets the value from its mode argument, while stop/reset paths at `0x115EA4`, `0x117C53`, and `0x118215` clear it.

The GameUI lobby ChatInputLine is a separate GameUI class constructed at RVA `0x51AF0`, with its own ChatInput TextEntry. GameUI.dll does not contain the cl_chat_active string/reference. The lobby focus check at `0x4EC06` only finds that child and tests focus before selecting Start Game. Thus no direct path from merely hiding that resource to setting the gameplay chat cvar has been found. Both ChatInputLine and LobbyChatHistory remain explicitly hidden/disabled and zero-sized in the generated lobby resource. This excludes a direct cvar write hypothesis, but does not by itself rule out a stale VGUI focus issue; live input tracing is owned by the input agent/root.

Resolution: the user physically confirmed Escape reopens gameplay pause. Root identified the failed synthetic-key test as an automation limitation; no input or presentation source changes were needed. The tested DLL remains 2449683f.

### Gamertag descender clipping

The ISO SurvivorListItem resource uses MainMenuButton for BtnDropButton while leaving DrpPlayer and its button at height15 within a height28 row. The native resource conversion retained this combination. In the generated Xbox scheme MainMenuButton selects MainBold20 with a logical text inset1; however, later live cache inspection found stock PC scheme values, so this does not establish the actual rendered font size. The verified constraint is that the retained PC renderer clips to the control's rectangle. This can remove the descender from a name such as Player.

PC CPlayerItem's native painting at GameUI RVA `0x4C574` reads BtnDropButton's text, measures it with the row's cached DefaultBold font, obtains DrpPlayer's current position, and calls SetPos at `0x4C64E` with unchanged X and Y=(row height - measured text height)/2. It does not resize the parent or button. Therefore changing the resource's authored Y=12 alone cannot correct the settled row; native painting overwrites it. The expected Y would be roughly5 with the generated scheme's font height18, but actual cached font metrics must be measured before claiming that live value. The Xbox row ApplyScheme at RVA `0x77228` independently caches DefaultBold in the same way while its resource selects MainMenuButton for the child.

The resource owner is applying the narrow PC compatibility correction: height22 for both DrpPlayer and BtnDropButton, preserving row/portrait height28, font/style, X offsets and native Y positioning. This was sized to accommodate the generated font20 + inset1 + clearance; actual cached metrics may differ. No native code change was made for this fix, and visual validation remains with root.

### Early menu scheme cache versus addon menu resources

BaseModPanel's constructor at GameUI RVA `0xD6F0` calls SchemeManager LoadSchemeFromFileEx for resource/L4D360UIScheme.res at `0xD9AE`, saving the handle at panel+0x27C. The later base-panel path at `0xD1CF` issues update_addon_paths; mission_reload and sends AddOnLoadComplete messages. The engine command registration identifies update_addon_paths as the addon search-path reload. This is consistent with an early stock scheme surviving later addon mounting, although it does not independently prove every initial filesystem mount's order.

The input agent's read-only live cache inspection settled the practical issue: the existing L4D360UIScheme handle contained stock PC text insets (MainMenu/Default/DropDown/Dialog0, Flyout2, GameMode0), despite the different generated addon values. The ui_reloadscheme command's help and implementation at GameUI RVA `0x16130` reload the active frame/footer resource settings; they do not reload the root scheme file. Thus successful menu-resource reload does not demonstrate that its inherited scheme has been replaced.

Direct per-control resource textinsety is not an alternative for this custom renderer. Label ApplySettings reads textinsetx/y at `0xE3CC0` into its own +0x170/+0x174 fields, whereas HybridButton painting uses +0x1FC/+0x200. HybridButton ApplySettings reads its style.TextInsetY from the inherited cached scheme and writes +0x200 at `0x5CB29` and `0x5CB49`; the only other writer found in the HybridButton range is constructor initialization. The reliable native proportional conversion is the same one used there: button GetScheme virtual byteoffset0x50, then SchemeManager GetProportionalScaledValueEx virtual byteoffset0x34. A scoped temporary paint-time inset can therefore preserve live fonts, scheme state, and resource ownership while correcting only the affected custom text positioning.
