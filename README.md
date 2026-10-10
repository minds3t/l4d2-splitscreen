# Splitscreen Support for Left 4 Dead 2 (PC)

Two players on one PC and one screen, the way the Xbox 360 version does it: a
shared offline lobby in which each player picks their own survivor, a split
screen with a HUD for each player, and button prompts that match the device
each player holds.

The PC game still contains most of what the console version uses for this. A
small plugin switches those parts back on and fills the few gaps; the menus
and the HUD come from Valve's own layout files. Nothing in the game folder is
changed permanently, and a normal start through Steam is the unchanged game.

- **Players:** get the release package (below) and read its
  [README.txt](release/README.txt).
- **Implementation notes:** [plugin/README.md](plugin/README.md).

## Status

Version 2 (version 1 was the VPK-only Workshop addon), made and tested with
Left 4 Dead 2 **2.2.4.3 (build 10097)** on Windows. The plugin finds its
places in the game by search patterns, so a game update that only moves code
should not stop it; if a place it needs is gone or has changed, it does
nothing. No game update has happened since, so this is not yet proven.

Played and confirmed with one controller plus keyboard and mouse: both players
choosing their character in the lobby, playing independently, a controller
switched on during a game, and player two leaving and rejoining.

Not tested yet: two controllers, the controller as player two
(`KeyboardPlayer=1`) with someone holding it, long sessions, and any PC other
than the author's. See [Limits](#limits).

## Release package

`python tools/packaging/build_release.py` writes `dist/SplitscreenSupport-v2/` and the
same folder as a zip. It needs no Python or compiler on the player's PC.

| File | Purpose |
| --- | --- |
| `Start Local Co-op.cmd` | starts the game with local co-op |
| `Install Shortcuts.cmd` | checks the game build, creates Desktop and Start menu shortcuts |
| `Uninstall.cmd` | removes the shortcuts and anything a session left in the game |
| `config.ini` | the settings |
| `README.txt` | instructions |
| `files/` | launcher script, plugin and its checker, four packages, the researched build's hashes, the licenses |

The launcher leaves nothing behind. For each session it:

1. finds Steam and the game (registry, Steam's library list) and refuses a
   game in which the plugin would not find its places (for a game that is
   not the researched build, `splitscreen_check.exe` runs the plugin's
   search on the game's files);
2. copies the plugin, the settings and the two packages of the chosen look
   into `left4dead2/addons` and lists the packages in `addonlist.txt`;
3. lifts Valve's addon restriction for the team modes this PC hosts
   (`cfg/addonconfig.cfg`);
4. starts the game through Steam with `-insecure`, loads the plugin and
   switches local co-op on;
5. waits in the background for the game to exit and undoes steps 2 and 3,
   each only if the file is still the one it put there, and moves the
   plugin's log into the package folder.

A session that could not clean up (crash, power loss) is undone by the next
start or by the uninstaller. The packages have no `addoninfo.txt`, so the
game's Add-ons list stays empty.

`-insecure` is required for any plugin: in a session started this way the
game cannot join VAC-protected servers.

## Playing

1. Start through the launcher.
2. Main menu: the **OFFLINE CO-OP** tile (one step left of Campaign) opens the
   campaign lobby. Other modes have an **Offline Co-op** row in their own
   tile: Realism, Survival, Versus, Scavenge, Realism Versus, Versus Survival.
3. The lobby shows both players; by default player two is named after player
   one with "(2)". Each player opens **Select Character** with their own device. In the
   team modes the same menu offers the Infected side.
4. **Start Game.** Controller 1 is player one, keyboard and mouse are player
   two. Without a controller, keyboard and mouse are player one; a controller
   switched on later is picked up and the keyboard moves to player two.

During a game, **pause menu > Options > Disable / Enable Splitscreen** lets
player two leave and come back. Leaving hands their survivor to a bot with its
health and items; coming back puts them on the same survivor.

**Main menu > Options** has two more rows: **Splitscreen Direction**
(default, horizontal, vertical) and **Disable / Enable Splitscreen**, which
switches local co-op off and on without leaving the game. While it is off the
tile reads SINGLE PLAYER and the game behaves as stock.

## Settings

`config.ini` in the package folder. Changes apply at the next start through
the launcher.

| Setting | Default | Other value |
| --- | --- | --- |
| `[Input] KeyboardPlayer` | `2`: the controller is player one, keyboard and mouse are player two | `1`: keyboard and mouse are player one, the controller is player two |
| `[Input] XboxSticks` | `1`: the Xbox game's stick response and look sensitivity during local co-op | `0`: your own controller settings stay |
| `[Display] ConsoleUI` | `1`: the Xbox look | `0`: the PC look |
| `[Display] SafeArea` | `1`: the HUD keeps the console's distance from the screen edge (Xbox look only) | `0`: the HUD uses the whole screen |
| `[Launcher] GameArguments` | empty | extra game options, for example `-novid` |

| | Xbox look | PC look |
| --- | --- | --- |
| Menus | console lobby and option pages, button bar at the bottom | Valve's PC lobby (with chat and voice controls) and option pages, no button bar |
| HUD | console layouts, item cross, screen margins | PC layouts, PC item list, no margins |

Both looks have the offline tile and rows, the co-op switch, the split
direction page, player two's pause-menu rows, the controller options, and
hints and vote prompts in each player's own device's buttons.

## How it works

Three parts work together.

**The plugin** (`splitscreen_support.dll`, sources in `plugin/`) is loaded
with `plugin_load` for one session. It only acts when it found every place
it needs in six game modules exactly once, only when the game runs with
`-insecure`, and only for an offline session it created itself. It
patches nothing on disk. What it does:

- **Lobby.** The PC's offline session is created with one local member,
  although the lobby code still handles several. The plugin adds the second
  member before the lobby opens, attributes menu input to the right player,
  and gives the server a local identity for player two so that the chosen
  survivor survives the change from lobby to game.
- **Joining.** It makes the engine open its own two-player connection when the
  map starts, so nothing reconnects during loading. In a running game it uses
  the engine's own split-screen connect and disconnect commands for player
  two, with guards for two crashes in the game's teardown and drop-in code.
- **Input.** It switches the gamepad on without running Valve's controller
  configs (they erase all key bindings), maps the stick axes, applies Valve's
  controller presets per player, and routes keyboard and mouse to the right
  player. It notices a controller that is switched on after launch. Settings
  it changes for the session are kept out of the saved game configuration
  and restored when local co-op is switched off.
- **Prompts.** Idle, spawn, instructor and vote prompts look up each player's
  own device instead of one device for everybody.
- **Xbox look.** The menu button bar and confirmation prompts, the HUD's item
  cross and the console screen margins are Valve's own code paths, switched
  on for the local session.

**Two packages** (VPK files, built by `tools/packaging/build_packages.py` from the
game's current files) carry layouts and one script:

- the menu package: Valve's offline tile, the offline rows, the co-op switch,
  the split direction page and player two's pause-menu rows, in the console
  layout or the PC layout;
- the HUD package: the console HUD layouts (Xbox look) and a small script
  that seats a returning player two and lets two local players share a team
  in the team modes.

Valve ships one set of layout files for both platforms, with the console
variant behind conditions the PC never selects. The build evaluates those
files as the console would, so most of the Xbox look is Valve's own data
rather than something redrawn.

**The launcher** installs both for one session and removes them, as described
above.

## Limits

- Offline play on one PC only: no online games, no LAN guests.
- A game update that changes one of the functions the plugin relies on stops
  the package until it has been adapted: it then refuses to start.
- Two controllers are untested, including the controller options of player
  two's controller.
- Mutations have no offline entry.
- If player two leaves while player one is idle, player two's survivor is
  replaced by a fresh bot instead of keeping its state.
- The split direction page is the console's and has no button to leave it with
  the mouse (Escape does).
- It reproduces the Xbox presentation where Valve's code and files allow it.
  It is not a complete copy of the console UI, and nothing of Xbox Live.

## Development

Requirements: Windows, the game installed through Steam, Python 3,
Visual Studio 2022 Build Tools with the C++ x86 tools.

```powershell
python -m pip install -r requirements.txt
powershell -ExecutionPolicy Bypass -File plugin/build.ps1   # plugin, its tests, build manifest
python tools/packaging/build_packages.py                    # the four packages into build/
python -m unittest discover -s tests                        # tests of the package and release build
python tools/packaging/build_release.py                     # dist/SplitscreenSupport-v<version> and its zip
```

`Start Dev Session.cmd` starts the game with the working build. It runs
`build_release.py --dev`, which assembles `build/dev/` exactly like a release
folder, and starts it. A development session therefore uses the launcher that
ships; only `build/dev/config.ini` differs: it adds a console port and a
console log for the test helpers, and keeps whatever else is set in it (a
window size, the PC look) from one build to the next.

The tools find the game through Steam's registry entry and library list, as
the launcher does; `L4D2_DIR` overrides that. `build_packages.py` builds on
top of mods mounted in the game's `gameinfo.txt`, so that they keep working on
the development PC. `--stock-only` uses Valve's files alone, and
`build_release.py` does that for the release.

| Folder | Contents |
| --- | --- |
| `plugin/` | the plugin: sources, ABI tests, build script, `config.ini` (default settings); see its [README](plugin/README.md) |
| `packages/` | files that go into the packages as they are: player two's script |
| `release/` | files that go into the release folder as they are: launcher, starters, README.txt |
| `tools/packaging/` | builds the packages from the game's files, and the release folder |
| `tools/game/` | drives a running development session: console commands, key and mouse input, screenshots |
| `tools/analysis/` | disassembly, cross-reference and crash-dump helpers for the game's binaries; extractors |
| `tests/` | Python tests for `tools/packaging/` and the build manifest |
| `docs/` | findings in the PC and Xbox binaries (`research/`), records of checks in the running game (`test-records/`) |

Extracted game files are not part of the repository; some tests are skipped
without them. `python tools/analysis/extract_game.py game_pc resource/ scripts/ cfg/`
creates them from the installed game. The Xbox disc's files, which the notes
in `docs/research/` refer to, are not included either, and neither are the
disassembly listings and resource inventories those notes were written from:
the scripts beside the notes produce them from the game's own files.

## License

The source is public so that anyone can read and check it. It is not open
source: you may read it, build it for your own use and use the released
package, but not copy, redistribute or publish modified versions without
permission. The exact terms are in [LICENSE](LICENSE); open an issue to ask.

Uses [MinHook](plugin/vendor/minhook/LICENSE.txt) by Tsuda Kageyu. Left 4 Dead
2 is a trademark of Valve Corporation; this is an unofficial modification.
