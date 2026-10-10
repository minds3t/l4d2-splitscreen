# Xbox UI coverage audit, 2026-10-10

Every stock PC file under `resource`, `scripts` and `cfg` that contains a
platform conditional (262 files) was evaluated as Xbox and as PC with
`tools/packaging/kv.py` and compared with the 93 files the native package shipped at the
time. `audit.json` (not kept in the repository) holds the per-file result; `audit.py` is the
script that produces it (it expects `tools/` on `sys.path`).

The in-game HUD was complete: no `resource/ui/hud/*`, `hudanimations`,
`hud_textures`, `mod_textures` or `weapon_*.txt` file had an unshipped Xbox
difference, and all `if_split_screen_*` blocks survive the conversion.

## Acted on (version 33)

| Finding | Change |
| --- | --- |
| `scripts/tips.txt`: gamepad hunter tip | shipped (HUD package) |
| `controllerbindlistitem.res`: "Press A button" | shipped (menu package) |
| Split direction only in the unreachable v27 flyout | rows added to the native modes flyout |
| "View Gamer Card" prompt on `scoreboard.res` / `survivalmodescoreboard.res` | hidden |
| Build ignored the mounted `4k_ui` mod | mounted mods are the build input (`--stock-only` to opt out) |
| Dead v27 preference flyouts and their main-menu anchors | removed from the native package |

## Deliberately unchanged

- **`gamesettings.res`** stays PC layout. It is only shown for a one-player
  offline session, which exists only while local co-op is off; the Xbox footer
  that its console branch relies on is not drawn there.
- **`loadingprogress.res`**. The Xbox branch moves the campaign name to
  `r100` and adds a hidden tip panel. The community Xbox HUD in `ext/xboxhud`
  keeps the PC title position, and with that kept the conversion changes
  nothing visible.
- **`instructor_lessons.txt`**. The one Xbox difference shows the `+lookspin`
  binding for "infected behind you". Keyboard players have no such binding
  and would see a "not bound" icon.
- **Achievements, found games, leaderboards, team lobby, password entry**:
  Steam features with different control contracts; see
  `docs/research/native_lobby_pc/menu_audit.md`.

## Closed in version 34

- **Pause-menu Enable/Disable Splitscreen.** PC GameUI still has the Xbox
  handler, but it only confirms and exits to the main menu, and it hides
  buttons carrying those commands. The pause menu now has its own two rows
  (`PlayerTwoJoin` / `PlayerTwoLeave`) that add or drop player two in place.
- **`%+command%` key names** in hint text go through the per-player device
  lookup (call at client `+b4a54`). Not yet seen on screen.
- **Controller options for player two.** The dialog edits the slot of the
  user who last gave menu input (`CGameUIConVarRef` builds `name2`). Menu
  input during a game is now attributed to the right player for the keyboard
  as well. With one pad this could only be traced, not operated.

## Changed in version 35

A comparison with the disc's own `mainmenu.res` (`xbox_ui/`) and the PC build's
retained code replaced several of this project's own menu routes:

| Was | Now |
| --- | --- |
| Own tile with a flyout of modes, split direction and on/off | Valve's offline tile, both disc variants, switched by `[$X360SPLITSCREEN]` (settable through vstdlib's KeyValues system) |
| Split direction as three flyout rows running `ss_splitmode` | The console Audio/Video page's own dropdown, other controls inert |
| Local co-op on/off as two flyout rows | One Options row conditioned on the same symbol |
| No offline entry for Versus and Scavenge outside our flyout | The other modes' Single Player row added to both flyouts |

Found while doing so: the PC build disables the split-direction dropdown
unconditionally (only the non-widescreen branch survived) and cannot close
that page after a change (its profile write, `gameui+9fc0`, is a stub); and
the main menu's Options flyout kept the Xbox `InitialFocus` although its rows
are the PC ones.

## Still open

- Teammate panels differ from the disc by 1-3 units and a hidden status label.
- Mutation and team-mode settings screens are PC layout; only relevant if
  those modes get a local entry.
- A disabled pause-menu row can still take the focus (stock behaviour of
  disabled flyout rows); pressing it does nothing.
- Vertical split: the teammate panels form one row across the middle of the
  whole screen, between the two players' own panels (two bots: one panel in
  each half). A player who is idle shows up there as a third panel, so the row
  then reaches further into both halves; it does not collide with anything.
  Left as it is.
- The Mutation tile has no offline row (some mutations make no sense for two
  local players). Realism Versus and Versus Survival got one after version 35.
  The disc itself has no offline row in any mode flyout, only the tile; the
  rows are named after the tile's two variants.
- `env_hudhint` text did not appear at all in a test, so the `%+command%`
  replacement is still unseen; an instructor hint with a binding was seen and
  showed the keyboard player's key.