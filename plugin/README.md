# The plugin: implementation notes

`splitscreen_support.dll` is an x86 server plugin for Left 4 Dead 2, researched
in 2.2.4.3 (build 10097). It is loaded with `plugin_load` for one game session and makes
the PC game's retained split-screen parts work for two local players. What it
does for a player is described in the [main README](../README.md); this file
says how, module by module. The findings in the PC and Xbox binaries are
under [docs/research](../docs/research/), and what was checked in the running
game under [docs/test-records](../docs/test-records/).

## Layout

| Path | Contents |
| --- | --- |
| `src/` | the plugin's sources |
| `include/build_10097.h` | every address the plugin uses, as researched in build 10097, one table per game module |
| `include/signatures.inc` | how each of those addresses is found in the game that is running (generated) |
| `include/supported_build.h` | SHA-256 of the six game modules of the researched build |
| `src/check.cpp` | `splitscreen_check.exe`: the same search on the game's files, for the launcher |
| `tests/` | three ABI fixtures (see [tests/README.md](tests/README.md)) |
| `vendor/minhook/` | MinHook 1.3.4 |
| `config.ini` | the default settings, as shipped |
| `build.ps1`, `validate_build.py` | build, tests and the build manifest |

## Build

```powershell
powershell -ExecutionPolicy Bypass -File plugin/build.ps1
```

This needs Visual Studio 2022 C++ Build Tools with the Windows SDK, and the
Python packages from `requirements.txt`. It configures for Win32, always
rebuilds from scratch, and produces `build/Release/splitscreen_support.dll`
with the static C runtime. Then it runs the `guest_abi`, `input_abi` and
`safearea_abi` CTests (assertions stay on in Release) and
`validate_build.py`, which checks that the DLL is a PE32 x86 DLL exporting
`CreateInterface`, that no source is newer than it, and that the CTest log
belongs to exactly this build. The result is written to
`build/Release/validation.json`. That manifest covers the build only; it says
nothing about behaviour in the game.

## Running a development build

`Start Dev Session.cmd` in the repository root runs
`python tools/packaging/build_release.py --dev`, which puts the built plugin
and the packages into `build/dev/` in the layout of a release, and starts
`build/dev/Start Local Co-op.cmd`. A development session therefore runs the
same launcher a user gets (`release/files/SplitscreenSupport.ps1`): it
installs everything for one session and removes it when the game exits.

The only difference is `build/dev/config.ini`: its `GameArguments` line starts
the game with `-novid -netconport 27999 -condebug`, so that
`python tools/game/netcon.py "splitscreen_status"` reaches the game's console.
Other settings made in that file (a window size in `GameArguments`,
`ConsoleUI=0` and so on) are kept from one build to the next.

The plugin logs to the console and to `splitscreen_support.log` beside the
installed DLL; after the session the launcher moves that file into
`build/dev/`. `build/dev/Uninstall.cmd` removes anything a crashed session
left in the game.

## When the plugin acts

It does nothing unless all of these hold:

- the game was started with `-insecure`;
- every address was found in `engine.dll`, `gameui.dll`, `matchmaking.dll`,
  `server.dll`, `client.dll` and `inputsystem.dll` (see below);
- the bytes at each hook address are the expected ones (`hooks.cpp` compares
  them before MinHook touches anything);
- local co-op is switched on (`splitscreen_enable`), and for most hooks the
  current session is the offline session the plugin itself expanded.

### Finding the addresses

`build_10097.h` holds each address as researched. In the plugin they are
variables: while the DLL is loaded, `addresses.cpp` looks every one up in the
loaded modules by its row in `signatures.inc` (`signatures.cpp`), before any
other source file copies an address. A row is one of:

| Kind | The address is |
| --- | --- |
| `code` | where a byte pattern occurs, or a fixed distance into it |
| `operand` | an operand of the instruction the pattern starts with (a global, a vtable) |
| `call` | the target of the near call the pattern starts with (a function too small to recognise) |
| `derived` | stored a fixed distance behind another address (a vtable slot, an object's vtable) |
| `located` | a fixed distance before the one place that stores another address (a vtable known by one of its functions) |

Patterns leave open what changes when code moves: relocated addresses and the
distances of near calls and jumps. Each pins at least twelve bytes and must
occur exactly once in the module's code; otherwise the module counts as
unsupported and the plugin does nothing. In the researched build (same file
hash) every address must also come out where it was researched.

`python tools/analysis/make_signatures.py` writes `signatures.inc` from
`build_10097.h` and the installed game; run it after changing an address.
`build.ps1` then runs `splitscreen_check.exe <game> --researched`, which
requires all rows to resolve to the researched addresses, both with the
modules at their preferred base and shifted to another one, as most are in
the running game.

What this does not cover: struct offsets, vtable slot numbers and the
instruction bytes some hooks compare stay literal in the sources. An update
that changes a function the plugin relies on makes a row or such a comparison
fail, and the affected part or the whole plugin stays off until the address
is researched again. No game update has been released since this was
written, so how well the patterns survive one is not known yet.

## Console commands

| Command | Purpose |
| --- | --- |
| `splitscreen_enable` / `splitscreen_disable` | install the hooks, or close the owned session and restore everything |
| `splitscreen_status` / `splitscreen_diag` | the plugin's state and the local profiles |
| `splitscreen_input_status` | controller-config guard, keyboard owner, stick profile |
| `splitscreen_lobby_status` | session settings, member identities and avatars |
| `splitscreen_identity_status` | client edicts and engine identities |
| `splitscreen_create <mode>` | create an offline lobby without the menu (`coop`, `realism`, `survival`, `versus`, `scavenge`, `mutation12`, `mutation15`) |
| `splitscreen_avatar <slot> <name>` | request a character; slot 1 is player two |
| `splitscreen_start` / `splitscreen_close` | start or close the owned offline session |

The menus only use `splitscreen_enable` and `splitscreen_disable` (the Options
row); the others are for development.

## Modules

**`plugin.cpp`** implements `ISERVERPLUGINCALLBACKS002`, registers the
commands through `VEngineCvar007` and obtains `MATCHFRAMEWORK_001`. While
local co-op is on it sets the menu condition `X360SPLITSCREEN` through the
KeyValues system's own setter (vstdlib `IKeyValuesSystem` slot 8) and reloads
the bare main menu after a switch. Valve's menu files use that condition for
the offline tile; the package's Options row uses it too.

**`settings.cpp`** reads `splitscreen_support.ini` beside the plugin at every
enable (Win32 profile functions; a missing file or key means the default).
`[Input] XboxSticks=0` makes `input.cpp` skip its eight response and
sensitivity writes; the axis mapping and the guard values are still written.
`[Display] ConsoleUI=0` makes `plugin.cpp` skip `hud_inventory` and makes
`presentation.cpp` install only its two split-direction hooks.
`[Display] SafeArea` gates both title-safe branches in `safearea.cpp`.
`[Input] KeyboardPlayer=1` makes `devices.cpp` trade controllers 1 and 2.

**`guest.cpp`** provides an independent local profile for player two.
**`lobby.cpp`** expands only an owned offline session (to eight slots in the
team modes) and routes character choices by the acting UI user. Its two
GameSettings presentation overrides leave the real session network value
offline. Member choices travel through Valve's `avatarinfo` path. When player
two leaves or comes back during a game, it issues the engine's own
`ss_disconnect` and `ss_connect`.

**`identity.cpp`** addresses a PC mismatch: the split-screen guest's engine
XUID is zero. Only four verified server character-cache call sites receive the
local guest identity, and only for the genuine guest attached to this
machine's host. Account and statistics callers, bots, unrelated clients and
identity diagnostics keep their own results.

**`join.cpp`** sets the retained split-screen request byte in
`CHostState::State_NewGame` for an owned offline session, so `Host_NewGame`
issues `connect_splitscreen localhost:<port> 2` itself, and guards net-channel
shutdown against the engine's stale player-two sub-channel. Nothing else
connects player two. It also holds the guard for the engine's drop-in code
when player two joins a running game.

**`devices.cpp`** reads the game's XInput records and starts the game's own
XInput polling when a controller appears after launch. Keyboard and mouse
belong to player two only while controller 1 is present. For
`KeyboardPlayer=1` it uses the fact that InputSystem resolves
`XInputGetState`, `XInputSetState` and `XInputGetCapabilities` once at Init
into three consecutive pointers (`inputsystem+21074`, stores at `+61e2`,
`+61f5`, `+620f`): the plugin verifies those stores and replaces the pointers
with wrappers that swap index 0 and 1, and puts the originals back on disable.
The game's poll then sees one pad unplugged and another plugged in. Everything
above that is the game's own controller-2-is-player-2 handling; the keyboard
goes to player one because the game's first controller is absent.

**`input.cpp`** snapshots settings and raises the controller-config guard
before enabling joystick input. It never executes `360controller.cfg`.
Together with the guard it holds `joy_xcontroller_found` and
`controller_popup_shown` at 1: with the guard set but no controller found, the
client's joystick setup (`client+12fdb0`) and the main menu (`gameui+67db0`)
would each run `undo360controller.cfg`, which unbinds everything.

It maps the gamepad axes as that file would, applies Valve's
`joy_preset_N.cfg` pad bindings per player (`cmd1` / `cmd2 exec`, queued in
player one's command buffer) whenever player two becomes active on a map, and
writes `in_forceuser` when the keyboard's owner changes. It also snapshots
`cl_viewmodelfovsurvivor`, which the game overwrites from
`splitscreen_config.txt` in split-screen, keeps the single-player value in
`config.cfg` and restores it, both on disable and as soon as player two's view
goes away during a game.

With `XboxSticks=1` it applies the disc's response curve (`joy_lowend=.75`,
`joy_accelscale=2`, `joy_accelmax=1`), default sensitivity magnitudes for both
slots (PC pitch `+.8`, yaw `-2.25`) and `joy_pitchthreshold=0`. Both
platforms' InputSystem already subtracts and rescales the same 8689-unit
right-stick deadzone; the PC then applies the configured pitch threshold
again, while the Xbox normalizes directly. Zero skips that extra PC stage. The
profile is applied once per enable, so option edits stay live during that
session.

A scoped hook on the engine's configuration writer serializes the user's
original joystick, curve, threshold, axis and per-slot sensitivity values and
restores the live values immediately afterwards, so none of the session's
values reach `config.cfg`. This also covers quit, where client cvars disappear
before the plugin unloads. A failed restore keeps the snapshot for a retry.
Mouse settings and per-player Auto-Aim are untouched, and no aim-target or
camera hook is installed. See [the aiming
evidence](../docs/research/aim_assist/xbox-findings.md) and the [stick-response
comparison](../docs/research/aim_assist/stick-response.md).

**`safearea.cpp`** enters the retained Xbox title-safe calculation only for
the owned local HUD and console menu context. Resource `usetitlesafe` values
select 5% or 7.5% of the full display; the game's viewport logic excludes
interior split edges. Resources without that flag stay unchanged, and the
global `panel_test_title_safe` cvar is untouched. Enable performs no resource
reload. Disable clears the scope and restores live HUD bounds; a menu reload
additionally requires a current matchmaking session, which avoids rebuilding a
stale GameLobby after its session has closed. See [the binary evidence and
lifecycle](../docs/research/native_lobby_pc/safe_area.json).

**`presentation.cpp`** enables the retained footer drawing in the scoped local
menu and pause context and restores the Xbox footer strip and button
placement. The main UI scheme is cached before addon overrides become active,
so a narrow paint hook restores the disc's text offsets for the MainMenu,
Default, DropDown and Dialog styles and puts the cached value back
immediately. Generic confirmations use the retained controller-mode updater
and a scoped paint hook that hides the four desktop button and arrow children.
It also holds the two hooks behind the split direction page, which are the
only ones installed for the PC look.

**`hud_prompts.cpp`** adapts the `CBindPanel` binding lookup to the device of
the player who owns the panel, and selects the existing PC or Xbox vote
instruction labels after the game's `VoteStart` handler. Vote counts, timers,
outcomes and input actions are unchanged. See the [binding and vote
notes](../docs/research/prompts_votes/native-action-prompts.md).

**`hud_inventory.cpp`** draws the Xbox item cross in place of the PC item
list, and shows each player the button their own device has bound to "last
weapon". Item and ammo state come from the game unchanged.

**`hooks.cpp`** wraps MinHook: a hook is installed only while local co-op is
on and only after the bytes at its address matched.

## The packages' side

`tools/packaging/console_menus.py` produces the console menu layouts from the
current PC files' retained Xbox branches: carousel, shared lobby and player
rows, character and settings flyouts, settings pages, footer and pause menu.
Language conditions survive. PC child controls the game's code looks up remain
in the files, hidden and inert; newer commands and mouse hit-testing are
preserved. A few values deliberately stay the PC's: pause buttons keep the PC
text width (long German captions were clipped at the Xbox width), the lobby
settings caption keeps the PC's wider non-English bounds, and character
portraits keep the PC's vertical offsets. `build_packages.py` describes the
rest, including the second pair of packages for the PC look.

## Not covered

A second physical controller, remote play, LAN guests, public matches and
dedicated servers are untested. This is not a second Steam login and not an
Xbox Live sign-in. No Xbox executable code is transplanted into the PC
process.

## Public references

- [L4D2 plugin interface](https://github.com/alliedmodders/hl2sdk/blob/l4d2/public/engine/iserverplugin.h)
- [ConCommand/CCommand ABI](https://github.com/alliedmodders/hl2sdk/blob/l4d2/public/tier1/convar.h) and [ICvar](https://github.com/alliedmodders/hl2sdk/blob/l4d2/public/icvar.h)
- [MinHook 1.3.4](https://github.com/TsudaKageyu/minhook/tree/v1.3.4), with its license in `vendor/minhook/LICENSE.txt`
- Binary evidence: [docs/research/xbox_lobby](../docs/research/xbox_lobby/) and [docs/research/native_lobby_pc](../docs/research/native_lobby_pc/)
