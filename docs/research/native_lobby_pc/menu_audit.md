# Remaining menu audit — 2026-10-10

Scope: extracted Xbox disc resources versus current stock PC resources and the native experiment overlay. No live game actions or installation were performed by this audit. Root owns rendered/input checks; this report distinguishes authored layout from native paint behavior.

`menu_resource_inventory.json` (not kept in the repository) records field-level differences for 181 disc resources. Profiles include the platform's WIDE and HIDEF symbols, plus English. The stock input is `build_addon.read_stock`, which honors current update/DLC precedence. ISO resources are evidence rather than blind replacements for newer PC classes. `menu_extras_validation.json` records English/German conversion checks.

## Implemented isolated resource corrections

`tools/packaging/console_menu_extras.py` exposes `generate(read_stock, out_dir)`. It writes eleven resources; the parent builder explicitly integrates it.

| Resource under `resource/ui/l4d360ui/` | Correction | Preserved behavior |
| --- | --- | --- |
| `ingamechapterselect.res` | Retained Xbox branch: dropdown/button heights20, chapter Y145, Select Y170, Cancel Y195, native20px arrows, hidden desktop background | Native vote commands, label tokens, language widths, current native child types |
| `ingamedifficultyselect.res` | Xbox heights20 plus ISO `MainMenuButton` style; choices Y55/75/95/115, Cancel145 in centered180-high modal | Width230, language-dependent frame width, localization, difficulty and cancel commands |
| `ingamevoteflyout*.res` (all five stock variants) | Authored30-unit row pitch compensates PC0.7 reflow; expanded background includes unchanged20-high last row | Current modes/actions, fonts/styles, labels, navigation, widths and enabled-state handling |
| `audio.res`, `video.res`, `multiplayer.res`, `keyboardmouse.res` | Hidden/disabled zero-extent `ImgBackground`; frame content height368; centered520-wide label/value rows; compact Audio/Video/Multiplayer placement | All PC settings, commands, navigation, language/device conditions, fonts, open-flyout width conditions |

The option-page change deliberately leaves the named `L4DMenuBackground` object available for native lookups. A whole Xbox conversion of Keyboard/Mouse would remove PC raw-input and acceleration controls. No added padding competes with native title-safe calculations.

Validation: 44 resource/language/aspect profiles parse successfully (eleven resources, English/German, wide/nonwide). Option-page changes are geometry and the background visibility/enabled fields only. Commands, concommands, labels, tooltips, fonts and navigation remain intact. Ten focused `test_console_menus.py` tests pass, including native vote-reflow bounds at480/720/900/1080/1440/2160p and difficulty hit-box separation. Live ink, navigation and slider use remain root's integration checks.

### Vote flyout reflow compatibility correction

The current and ISO vote resources both author20-high buttons at20-unit pitch. In PC GameUI RVA `0x2D8D0`, the retained FlyoutMenu layout multiplies total panel height and each `vgui::Button` Y by the float0.7 at RVA `0x1683C8`, but leaves button heights intact. The result has14-unit pitch for20-high hit regions. Resource equality therefore does not establish correct console presentation on PC. All five vote variants now author30-unit pitch, yielding approximately21 after native reflow. Their background height is `(rowCount-1)*30+36`;36 compensates the last unchanged20-high row plus five units of padding through the0.7 transform. At supported test heights the row hit bounds remain separate and the final row stays inside the visible parent. No global reflow hook or font substitution is introduced.

The difficulty modal has an independent overlap: its disc-authored15-unit pitch was retained with20-high buttons. The compatibility layout now gives each choice20 units and keeps Cancel separate, expanding/recentering the frame. The chapter selection page already has25-unit pitch for20-high controls and is unchanged by this follow-up. German rendered width and native selection/highlight behavior remain live checks, including the requested borderless4K configuration.

### Separate PC option pages: observed clipping and compatible layout

The first live captures showed German Audio volume labels and Keyboard/Mouse analog-stick labels overlapping their slider/value columns. Audio and Video headings were above the visible screen; Multiplayer retained only the subtitle at its top edge. Keyboard/Mouse's heading remained visible. This is a PC-options adaptation rather than an ISO page transplant: the console Options flyout uses the combined `AudioVideo` resource (height330, or350 for several languages), whereas the disc's separate `audio.res` and `video.res` are unused PC-oriented resources with `tall=f0`.

The missing heading is not text owned by `ImgBackground`. Current PC Video's title painter at GameUI RVA `0x747C0` selects `#GameUI_Video` / `#GameUI_VideoAdvanced_Title` and calls the shared title painter at `0xF8C0`. Its ApplySchemeSettings at `0x74810` calls the frame positioning helper at `0xF360`. That helper stores the panel's current height at object+`0x294`, obtains footer bounds, and positions the frame using `(footerY-panelHeight)/2` when a footer is active. Oversized authored heights can therefore place the parent origin and native-painted title above the screen. The exact live origin also depends on current footer/layout state; the resource adjustment requires rendered confirmation.

The new layout uses the working stock Keyboard/Mouse height368 as the common frame baseline. Rows are520 logical units wide and centered; the longest captured German labels gain160 units over the former360-wide row without changing their native fonts. Audio and Video use18-unit row pitch starting at85. The audio meter remains three units above its microphone-test row. Video's native ApplySchemeSettings still places collapsed Recommended/Cancel/Done buttons at20-unit pitch below Advanced; the authored advanced-state Y targets and POSIX alternatives are transformed consistently. Multiplayer retains its64-unit spray image and six-unit clearance, and ends at Done Y345. Keyboard/Mouse's already readable Y positions are unchanged. These adjustments do not write scheme, cvar, input, or session state.

## Difficulty modal frame is authentic, not unwanted PC chrome

The ISO difficulty resource specifies the same centered Frame as current stock: height152, width400 English or450 otherwise. Its constructor also enables the same base-frame flags.

Static binary comparison confirms the grey bordered modal is native Xbox behavior:

- Current PC GameUI difficulty constructor RVA `0x5E6A0` and Xbox GameUI constructor RVA `0x86B48` call the base constructor with the same four boolean arguments and enable matching frame flags afterward.
- PC difficulty background override RVA `0x5E360` jumps to CL4DFrame painter RVA `0xF840`.
- Xbox difficulty vtable RVA `0x143BC`, slot125, points to RVA `0x86C70`, which branches to CL4DFrame painter RVA `0x55CE8`.
- Both painters draw RGB150/150/150 at full panel size, then RGB48/48/48 inside a2px border. Both use full opacity. The raw paint geometry/colors agree; no frame-removal patch is justified.

Modules: PC GameUI SHA256 `321ec9b5865582330af61e81eb347da31cc213cc6c95b80d6d98170b8a5fbb40`; extracted Xbox GameUI memory-image SHA256 `e201797d6992a2c35c3559b1236f57b6941bd9ada1ce3ce04b87921cb5233aa7`. Xbox image is already memory-layout; read RVA offsets directly, not through a second PE file-to-memory remap.

## Coverage and remaining priorities

| Menu family | Evidence and remaining difference | Decision |
| --- | --- | --- |
| Main menu, pause, shared GameLobby, member rows, footer, mode Edit settings | Already covered by `console_menus.py` and scoped presentation code | Preserve proven routing and current compatibility adaptations |
| Campaign/chapter/difficulty/mode/round-limit/server/access dropdowns | Existing overlay selects retained Xbox branches. PC FlyoutMenu still scales menu height and button Y by0.7 at GameUI RVA `0x2D8D0`; this is code behavior, not missing resource geometry | Do not enlarge arbitrary fonts or introduce new per-menu margins. Any further expansion needs native reflow audit and rendered comparison |
| `ingamevoteflyout*.res` | Current PC/Xbox evaluations agree, but PC native0.7 reflow overlaps the unchanged button heights; newer stock also includes AllTalk and additional mode variants | Compensate geometry in all five resources while retaining current native actions |
| `optionsflyout.res` | Current experiment retains PC Video/Audio/Keyboard/Mouse/Multiplayer/Cloud and adds native Controller. Xbox version uses unsupported Storage and console AudioVideo actions | Keep supported PC action list; its row height is already20 |
| `controller.res`, `controlleroptionsbuttons.res`, `controlleroptionssticks.res` | No platform or disc-resource differences | No correction needed |
| `controlleroptions.res` | No stock platform difference; experiment intentionally hides unsupported Duck Mode and unsafe Defaults, keeps working PC pitch behavior | Preserve tested compatibility corrections |
| `choosecampaign.res`, `choosedifficulty.res`, `changeteam.res`, `changeclass.res`, `genericwaitscreen.res`, `loadingposterdefault.res` | No current platform or disc-resource differences | No correction needed |
| `loadingprogress.res` | HD PC/Xbox progress bar and loading text geometry agree. Xbox puts LocalizedCampaignName at `r100` rather than0 and includes a hidden LoadingTipPanel (450×80, title-safe1) | Native poster/title setup must be traced before moving the title; no speculative transplant |
| `genericconfirmation.res` | Current PC/Xbox resource branches agree, but147 field differences from old ISO. New class uses HybridButton OK/Cancel plus newer checkbox/message controls; ISO used hidden plain buttons plus separate glyph labels | Requires separate current native dialog audit, not an old resource replacement |
| `passwordentry.res` | Xbox hides visible OK/Cancel buttons and shows separate A/B labels | Keep mouse-accessible current dialog until this online-oriented path is explicitly tested |
| Achievements/list items | Xbox resource substitutes GamerScore for Steam completion controls and shortens layout; substantial disc/current drift | Do not turn supported Steam features into nonfunctional Xbox Live UI |
| Public/FoundGames, TeamLobby, leaderboard, invites, stock online-mode flyouts | Major platform-specific Steam/Xbox Live actions and control contracts | Outside owned offline presentation target. Preserve supported PC functionality |

Known intentional PC adaptations remain: current localized button widths, retained hidden PC child objects, safe Steam invitation actions, ready-icon offsets,22-high player dropdown clipping bounds, and character portrait PC Y offsets to account for native0.7 flyout reflow. These are compatibility decisions rather than evidence of missing Xbox assets.

No claim of pixel-identical console rendering follows from matching resources alone. Shared scheme loading, current cached fonts, native title-safe math, and final native paint paths all affect the result; the native inset hook remains unchanged by this work.

## Independent final screenshot review

Reviewed root's `docs/test-records/hud-menus/menus_final_difficulty.jpg` and `hud_final_horizontal.jpg`, `hud_final_vertical.jpg`, `hud_final_item_selected.jpg`, `hud_final_scoreboard.jpg` with image inspection. The German difficulty heading fits inside the modal. All five ISO-styled choices have legible ink without visible clipping or overlap; the grey modal matches the native code comparison above. Static screenshots do not establish mouse hit rectangles or controller navigation.

The inventory cross, active medkit glow, and scoreboard text show no visible edge clipping in these captures. Exterior HUD margins remain visible, while vertical interior-edge elements stay within their respective halves. The shared teammate name bands still extend across the horizontal split divider; this was already part of the shared HUD arrangement, not a newly cropped per-player panel. No lastinv/Y binding cue is visibly rendered above either weapon in these screenshots, so that added native-buffer draw remains visually unverified. Input behavior and broader HUD fidelity are tracked by the root/HUD audit.

The subsequent `menus_verified_audio.jpg`, `menus_verified_keyboardmouse.jpg`, and `menus_verified_multiplayer.jpg` confirm restored complete headings and readable German label/value columns. The microphone meter and spray image remain aligned with their controls. `menus_verified_video_advanced.jpg` also has a complete heading and no setting-column overlap, but its third-party credit and footer-help text have only a few pixels of visible separation at1600×900. The final resource revision uses17-unit pitch for Video's last four actions: Windows Recommended317, Cancel334, Done351 and Credits368. Their15-high hit bounds remain separate; the credits gain about9 rendered pixels from the footer help at900p. POSIX alternatives retain their corresponding earlier row. Functional navigation and native expanded/collapsed state still belong to root's live verification.
