# Vote-menu compatibility correction

This audit covers presentation geometry and packaged-resource integrity. Root owns runtime, input and borderless4K checks. The native confirmation-prompt change and HUD prompt adapter have separate source owners.

## Reproduced static defect

Current PC GameUI SHA256: `321ec9b5865582330af61e81eb347da31cc213cc6c95b80d6d98170b8a5fbb40`.

The extracted Xbox `ingamevoteflyout.res`, `ingamevoteflyoutversus.res` and `ingamevoteflyoutsurvival.res` specify20-high buttons at20-unit pitch. Current PC resources keep that geometry, add AllTalk and include SurvivalSolo and VersusSurvival variants.

PC FlyoutMenu layout at RVA `0x2D8D0` changes the geometry:

- `0x2D90C`–`0x2D928`: multiplies the PnlBackground height by the single-precision0.7 constant at RVA `0x1683C8`; truncates to an integer.
- `0x2D960`–`0x2D97C`: enumerates children and casts to `vgui::Button`.
- `0x2D985`–`0x2D9AF`: obtains each button's position, multiplies Y by0.7, and writes the new position.
- The function does not reduce button heights. Its resulting14-unit pitch overlaps20-high hit bounds. Copying unchanged Xbox resources cannot correct this PC behavior.

## Resource-only fix

`tools/packaging/console_menu_extras.py` now generates all five `ingamevoteflyout*.res` resources. Each row keeps its original20-unit height, native FlyoutMenuButton style, width, localization, command, navigation and enabled/visible conditions. Authored pitch is30 units, yielding approximately21 after native reflow. The background authored height is `(rowCount-1)*30+36`, where36 compensates the last unchanged20-high row and five units of bottom padding through0.7 scaling.

The separate difficulty modal also had20-high buttons at15-unit pitch. Its choices now begin at Y55/75/95/115, with Cancel at145. The modal is180 high and centered with `c-90`; localized widths400/450 and230-wide buttons are unchanged. MainMenuButton is the real disc style previously restored. Chapter selection already has25 pitch for20-high controls and is unchanged.

No input binding, vote callback, permission, gameplay cvar, native reflow hook or global font is changed by these resources.

## Verification and package delta

Ten tests in `tests/test_console_menus.py` pass. The focused regression checks preserve action/style/navigation contracts and simulate the PC single-precision reflow at480/720/900/1080/1440/2160p. Rows do not overlap and the final row stays inside the parent. Difficulty bounds are separate, fit the frame, and preserve English/German widths. `docs/research/native_lobby_pc/menu_extras_validation.json` records44 English/German wide/nonwide conversion profiles.

The built candidate VPK SHA256 `838088227427362b7a863fb0863e516d32133cee6ef8cb50a8d96b56c552c6ca` was compared to the saved prompts/votes baseline SHA256 `911258e36b16848d4f5d6284d6ffb49c21a93015a8811f4e6e9a21775bb57500`. The delta is exactly:

- Five added vote flyout resources.
- Changed `addoninfo.txt` and `resource/ui/l4d360ui/ingamedifficultyselect.res`.
- No removals and no other changed entries. Candidate CRCs and byte equality with `build/native-addon` pass.

Run `python research/prompts_votes/verify_candidate.py` for the integrated DLL/source-manifest/CTest/VPK audit. It accepts explicit `--baseline`, `--package`, `--dll` and `--evidence` paths; `--check-installed` additionally asserts installed file equality. Every successful run writes a new timestamped JSON in `docs/test-records/prompts-votes`, leaving previous HUD/menu evidence untouched. A strict source-hash mismatch aborts rather than certifying a stale build; rebuild the DLL after native source changes before rerunning.

Runtime ink width, expanded-parent painting, mouse/controller selection and final4K presentation are not established by these static checks.

## In-game vote HUD clarification and independent review

The user's later clarification identifies the extreme overlap as the in-game Yes/No popup, not the pause-menu flyout. The flyout and difficulty hit-bound defects above are separate, valid corrections. The popup fix lives in `tools/packaging/console_hud.py` and `plugin/src/hud_prompts.cpp`, owned by the HUD adapter agent.

`vote-hud-resource-comparison-before.json` (not kept in the repository) preserves the pre-fix comparison. The native Xbox-specialized resource omitted `YesPCLabel` and `NoPCLabel`; the supplied workshop retained both explicitly hidden. All other evaluated workshop/native vote-resource fields matched. Current PC VoteStart still addresses those two PC labels. The retained `SetControlString` implementation (client RVA `0x3FF650`) looks up an existing child and posts its text; it does not create a missing child. The precise prior resource/control lifecycle is therefore not established by the static comparison and must not be described as proven creation-at-origin.

The native adapter runs after VoteStart (`client+0x2DB130`, thiscall with one message pointer, callee pops four bytes). It checks the exact CHudVote and embedded CHudElement vtables, reads local slot from root+`0x190`, then resolves and validates all eight instruction labels before changing any visibility. It shows the two native PC instructions for the keyboard owner or the six Xbox text/icon parts for a connected controller. The native vote state, counts, localized binding text, message handling and input actions remain intact. Review confirmed the existing-child finder (`0x3E8AF0`) and SetVisible virtual slot33 (`0x3E4740`) ABI.

The initial integrated popup candidate VPK `b4e523739925939669dc236286bff9ecdb46c74223db5b915fb823ac5f80eaab` and DLL `a11932a956ee86e980fd854ab6f60eb1631e53fb180b68954ff029ec99476b64` passed the strict package audit, including installed-file equality. Evidence is `docs/test-records/prompts-votes/package-audit-20261010T021919463893Z.json`. Its only additional baseline delta is `resource/ui/hud/votehud.res`; the five added flyouts and two earlier changed files are unchanged in scope.

Independent inspection of `votehud_fixed_4k.jpg` confirms that the instruction sets no longer stack. It also shows two remaining presentation defects: disabled-grey keyboard labels and a truncated German controller No instruction. The labels can stay initially hidden while enabled, since Label controls do not themselves accept vote actions. German widths need additional right-text space because the disc's `um mit NEIN zu stimmen` became current PC `um mit NEIN abzustimmen`; `BACK wählen,` also became `ZURÜCK wählen`. Preserve the current localization and overall panel sizes, and reallocate only the measured language's three-part row widths. A subsequent package audit will record the polished resource separately.

The polished candidate is VPK `de1ec3cbcba461b3167c4ed4f10ba80992f6b5d91809b7a8e2dc34518e6070fe`, with unchanged DLL `a11932a956ee86e980fd854ab6f60eb1631e53fb180b68954ff029ec99476b64`. `package-audit-20261010T022332618752Z.json` records installed equality, all CRCs, current source manifest, three recorded CTests, unchanged v27 and the exact five-added/three-changed/no-removed baseline delta. Its independent VoteHUD AST contract requires initially hidden enabled PC labels, then permits only German left widths98, icon X108, right X132/width138. This retains the right edge270, all other language branches, panel/background/count/result/issue geometry and every remaining node/condition. The verifier does not invoke the resource generator to establish that contract. Final live ink/brightness checks remain root's responsibility.

The final confirmation paint hook is separately covered in `docs/research/native_lobby_pc/generic_confirmation.json`. Independent inspection of `startup_fixed_4k.jpg` confirms native A/OK and no disabled desktop Cancel row. Root's diagnostic mask is0xf and all cached/found child pointers match, so the earlier stale-pointer hypothesis is unsupported; successful paint-time enforcement does not prove a particular prior load-order cause.

## German result heading follow-up

Fresh 900p `votehud_failed_900.jpg` shows the native wrapped German failure heading clipped at both ends inside the original17-unit centered title. The next resource revision changes only German geometry in VotePassed, VoteFailed and CallVoteFailed: title height36 at unchangedY10, detailY48 with unchanged height40, parent height98. The title ends46; the explanation occupies48–88 with10 bottom padding. ParentY35+98 stays within the200-unit CHudVote root. Fonts, widths, localization, wrapping, colors, icons and all other languages remain unchanged. Inspected native passed/failed message paths set localized result/state/timers and events; no bounds setters were observed in those paths.

Installed VPK `a21f3e90f383b9e0615ac37fc1f0c4a5b80e166180dbeebd0c2721d4dce225ad` with unchanged a11932a9 DLL passes `package-audit-20261010T022738581049Z.json`. That intermediate AST contract permitted exactly those nine German result scalar changes, preserving all remaining nodes and conditions. This added no further changed package entries. Subsequent runtime evidence narrowed the final scope below.

Subsequent independent image inspection of final-build `votehud_verified_900.jpg` and `votehud_failed_fixed_900.jpg` confirms clean active prompts and a complete two-line German failure heading with a separate readable explanation. Root verified the native No outcome left difficulty Normal. `votehud_cooldown_900.jpg` is misnamed: the call was accepted as a normal vote, so it does not verify CallVoteFailed. That result variant remains statically checked but unobserved live.

The next fresh 4K check showed that German success text fits one line (`votehud_passed_fixed_4k.jpg`), so the enlarged VotePassed title unnecessarily lowered it relative to the checkmark. The final intended scope is therefore only VoteFailed and CallVoteFailed; VotePassed returns entirely to its baseline resource. The semantic verifier now enforces that narrower two-panel contract. Existing 900p failure evidence remains applicable because those failure bounds do not change.

Final installed VPK `d5f86206e77116bda894fe0d20a57ee59d5db31f81e37fee305b5ebf3ad41d83` passes `package-audit-20261010T023059396209Z.json` with the unchanged a11932a9 DLL. Direct comparison with the saved a21 archive (`vote-result-scope-d5f86206.json`) confirms VoteActive, VoteFailed and CallVoteFailed are identical ASTs with identical serialized-block hashes. Only VotePassed reverted to baseline. This preserves the fresh900 active/failure evidence and the final two-panel scope without broadening other resources.

Final4K `votehud_release_4k.jpg` verifies the active overlay, and independently inspected `votehud_success_release_4k.jpg` verifies the compact stock success panel with full German text and aligned checkmark. Root restored Normal through a native P2 call/P1 Yes vote. `confirmation_final_4k.jpg` has only native A/OK and B/Cancel, with Escape returning safely to pause. The final idle screenshot shows P1's RT prompt and active P2; the user physically confirmed that RT rejoins the top player. The final game remains running at4K on Normal. CallVoteFailed and a second physical controller remain outside the observed runtime checks.
