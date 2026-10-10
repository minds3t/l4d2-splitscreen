# Idle, spawn and action prompt device selection

The reported P1 idle prompt used Mouse1 because native `CBindPanel::Update`
passed device−1 to the PC engine's binding lookup, which ignores its supplied
local-user argument and finds the keyboard/mouse binding first. The original
Xbox control also passes its stored local user, confirming the intended
ownership contract; the Xbox engine lookup itself was not independently
decoded in this pass.

The isolated adapter is [`plugin/src/hud_prompts.cpp`](../../../plugin/src/hud_prompts.cpp)
with `Install()` / `Shutdown()` declared in its adjacent header. Root owns
integration; shared `RemoveHooks()` must run before `Shutdown()`. Frozen
source SHA256 after the vote HUD selector below:
`51033b401442b0387ef643048ea555deda8cedb96322e9db0e1733c3e9253486`.

It intercepts engine `VEngineClient013` virtual155 at RVA `5e1e0` only when
the caller is client `2d68ec`, inside native CBindPanel. Verified module
fingerprints, interface objects/vtables, caller instructions and helper
prefixes are required. Runtime adaptation additionally requires the enabled
bridge, an owned offline session and local slot0/1. Connected native XInput
devices retain their slot's own bindings; otherwise the read-only
`in_forceuser` owner receives keyboard/mouse hints. The verified native
button-code classifier and command matcher supply actual names. Missing
bindings are omitted. Original rendering, glyph assets, localization,
layout, input actions and bindings stay native.

This naturally covers the resource controls in `takeoversurvivorbar`,
`hudghostpanel`, `zombiepanel`, `spawnmodemenu` and the six infected spawn
menus. It does not modify confirmation dialogs or unrelated instructor
messages. CBindPanel's own half-second refresh handles the keyboard owner
being initialized after an early startup lookup.

Detailed offsets and source comparison are in
[`prompt-adapter-notes.md`](../hud_audit/prompt-adapter-notes.md).
Run `python docs/research/hud_audit/prompt_binary_evidence.py` to regenerate
`prompt-binary-evidence.json` (not kept in the repository).

The standalone x86 fixture covers the EDI matcher ABI, controller0 versus
keyboard, independently rebound controller1, device-preserving duck alias
fallback, unbound omission, and disabled/non-owned/wrong-callsite forwarding.
It does not load game libraries. From a Visual Studio **x86 Native Tools**
prompt at the repository root:

```bat
cl /nologo /std:c++17 /EHsc /W4 /MT /Od /UNDEBUG /DWIN32_LEAN_AND_MEAN /DNOMINMAX /D_CRT_SECURE_NO_WARNINGS /Foplugin/build/prompt_abi_test.obj /Feplugin/build/prompt_abi_test.exe /Iplugin/include docs/research/hud_audit/prompt_abi_test.cpp
native\build\prompt_abi_test.exe
```

Compilation and assertions passed. This fixture is separate from the three
existing CTests. Root's intermediate 1600x900 runtime screenshot
[`idle-900.jpg`](../../test-records/prompts-votes/idle-900.jpg)
shows the native RT bitmap for P1, replacing the Mouse1 in
[`idle-before.jpg`](../../test-records/prompts-votes/idle-before.jpg).
That verifies the observed intermediate idle prompt, not every shared control.
The final 4K prompt and physical RT rejoin also passed, as recorded below.
A second physical controller remains unavailable.

## Actual Yes/No vote HUD overlap

The user clarified that the overlapping popup was the active in-game vote,
not the menu used to call a vote. The live 4K capture
[`votehud_before_4k.jpg`](../../test-records/prompts-votes/votehud_before_4k.jpg)
shows PC F1/F2 instructions and Xbox Start/Back instructions occupying the
same rows. This is distinct from the earlier vote-menu layout adjustments.

Xbox specialization omitted `YesPCLabel` and `NoPCLabel`. Their native PC
controls nevertheless exist in the observed game, and VoteStart writes both
names at client `2db5d7` / `2db624`. The setter `3ff650` only finds a child and
posts SetText; it does not create a missing control. The exact creation/load
lifecycle of those visible controls is not established; retaining their
resource entries makes initial visibility explicit. The supplied workshop
resource documents the same omission problem
and explicitly retains hidden PC labels. Its evaluated layout otherwise
matches ours; see the independently captured
`vote-hud-resource-comparison-before.json` (not kept in the repository).

`tools/packaging/console_hud.py` now adds the two named PC labels explicitly with
`visible=0`, `enabled=1`, initially blank text, and their normal native row
geometry. These are noninteractive Labels; the first live selector test
confirmed that `enabled=0` made the selected keyboard text dim.

The same test showed the German controller No instruction truncated. The
disc localization says `um mit NEIN zu stimmen`, while current PC localization
says `um mit NEIN abzustimmen`; retaining disc bounds alone is insufficient.
For German only, the two rows transfer 20 logical units from their left
columns to their right columns: left width118→98, icon x128→108, right
x152→132 and width118→138. The measured left text needs about75 units.
The right edge stays270, and panel, dividers, selection backgrounds, glyphs,
fonts, row heights, English and all other languages remain unchanged.
Final `votehud_release_4k.jpg` and fresh900 `votehud_verified_900.jpg` confirm
complete German instructions and bright keyboard text without stacking.

The second hook in `hud_prompts` runs after original VoteStart at client
`2db130` (`thiscall(root, bf_read*)`, native `ret4`). It requires exact
CHudVote and embedded CHudElement vtables `5dab34` / `5daae4`, owned offline
scope, and slot0/1 at root `+190` (the native CHudElement accessor reads
`+14` from the embedded object at `+17c`). It resolves eight existing named
labels in VoteActive (`root+1b8`) using native finder `3e8af0` and verifies
their SetVisible method is native `3e4740` before changing any visibility.
The connected controller sees the six native Xbox text/icon pieces; the
keyboard owner sees the two native PC labels, whose localized bound-key text
was already produced by the original handler. Unowned devices show neither.
No input dispatch, binding, vote outcome, timer, count or background highlight
is changed. Header/HeaderCaller remain untouched: native code deliberately
selects which of those two overlapping author-time fields contains text.

All 28 existing Python tests passed. Focused generated-resource checks passed
for idempotent roundtripping, enabled-but-hidden PC defaults, unchanged
English/French geometry, and exactly the German column changes above.
The standalone x86 fixture additionally rejects
foreign vote roots, slot3, disabled bridge and unowned sessions without child
queries or mutations. Independent native trace confirmed the calling
conventions and slot offsets. The 4K live selector test passed: the controller
viewport shows only Start/Back and the keyboard viewport only F1/F2, with no
overlap. See
[`votehud_fixed_4k.jpg`](../../test-records/prompts-votes/votehud_fixed_4k.jpg).
That capture precedes the keyboard brightness and German width adjustment.

## German result headings

The fresh 1600x900 capture
[`votehud_failed_900.jpg`](../../test-records/prompts-votes/votehud_failed_900.jpg)
shows the wrapped German failure heading clipped above and below. The stock
result title is only17 logical units high, while DefaultLarge is18 and the
current German text occupies two lines. This is separate from active-vote
instruction visibility.

The native-only generator reserves36 units for the German titles in
VoteFailed and CallVoteFailed. Their explanation moves from y29
to y48, keeps its40-unit height, and the panel grows to98 for10 units of bottom
padding. Title y10, width, font, wrap, icon and all localization strings stay
native. The panels remain at y35 inside CHudVote's200-unit height. Other
languages retain their previous geometry. The success heading fits one line
in the fresh4K capture, so VotePassed remains entirely stock; the earlier
three-panel candidate unnecessarily centered that line below its icon.
Independent review and focused checks confirmed no title/explanation overlap,
containment and exact stock success-panel preservation; all28 existing
Python tests pass.

## Final verified runtime

Installed DLL `a11932a9` and VPK `d5f86206` pass the immutable
[`package audit`](../../test-records/prompts-votes/package-audit-20261010T023059396209Z.json).
Final4K `votehud_success_release_4k.jpg` shows the stock compact success panel,
complete German heading and aligned green checkmark. Fresh900
`votehud_failed_fixed_900.jpg` shows the complete two-line failure heading and
separate explanation. Direct block comparison confirms those failure and
active panels are identical in the final package. Native No and Yes outcomes
were checked; Normal was restored through a P2 call and P1 Yes vote.
CallVoteFailed remains statically checked but unobserved live: the misnamed
`votehud_cooldown_900.jpg` was an accepted normal vote.

`idle_release_4k.jpg` shows the native RT hint for idle P1 while P2 remains
active. The user then physically confirmed: "Yes, RT rejoins the top player."
This establishes the real controller rejoin on the final build. The game is
left active at4K on Normal, with native input active, guard1, joystick1 and
split configuration2. A second controller remains untested.
