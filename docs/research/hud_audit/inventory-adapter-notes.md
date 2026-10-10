# Native inventory painter comparison

This records static evidence and the specifically identified runtime checks
for `plugin/src/hud_inventory.cpp`; it is not a blanket runtime certification.
Reproduce the PC and decompressed ISO instruction capture with
`python docs/research/hud_audit/inventory_binary_evidence.py`.
The resulting `inventory-binary-evidence.json` (not kept in the repository) records the inspected file hashes.

## Missing PC draw path

The ISO `CHudWeaponSelection::Paint` is at `8852aa98`. Its final block draws the
binding hint, D-pad, flashlight and three directional inventory slots. The
current PC Paint at client RVA `2d11e0` draws the primary/secondary weapons and
ammo, then a vertical item stack. PC ApplyScheme at RVA `2ce080` still resolves
`icon_dpad`, `icon_equip_flashlight` and `icon_equip_flashlight_active`; PC Paint
does not use the D-pad pointer. This is missing compiled drawing, not a
resource visibility problem or a global controller-mode flag.

The workshop hides the native panel and constructs separate icons in
LocalPlayerPanel. Its item textures can improve PC texture sampling, but its
geometry, team placements and removed scoreboard controls depart from the ISO.
The adapter does not copy workshop materials or weapon definitions.

## Xbox arithmetic and native fields

The ISO panel's width/height come from `88638dd8`/`88638e40`. At `8852b640`,
`IconSize` is panel `+4b8`, `RightSideIndent` is `+4b0`. Raw constant `8810efb8`
is float **2.0**. Therefore the D-pad top-left is:

```
x = truncate(panelWidth - RightSideIndent - 2 * IconSize)
y = truncate(panelHeight - 2 * IconSize)
gap = truncate(ScreenHeight / 480)
```

The gap double is stored at `88146d98`. Flashlight is above, throwable left,
first aid right, pills/adrenaline below, each one icon plus the gap away. The
PPC truncates after float arithmetic; the adapter preserves that order.

The resource panel is `scripts/hudlayout.res/HudWeaponSelection`. ISO height is
175; current PC height is 200. ISO `PrimaryBindingYPos` is 38; current PC omits
it. The native-only addon supplies these two original values; the horizontal
`ypos 0` branch and all item/ammo properties remain intact.

PC Paint receives its VGUI panel base; the CHud base is panel minus `0x48`.
The supported exact vtables are client RVAs `5d5734` and `5d5a84`. PC icon
pointers are panel `+514` (D-pad), `+518/+51c` (flashlight), IconSize `+4e8`,
RightSideIndent `+4e0`, PrimaryBindingYPos `+4c8`, BindingsFont `+4f0`.

## Narrow adapter contract

`hud_inventory::Install()` requires the full supported client fingerprint,
explicit bridge enable and its insecure launch policy. Every translated call
is additionally matched against its exact direct call return in native Paint.
The Paint frame requires the two native vtables and an owned offline session.
No global input-mode predicate is changed.

| Native helper RVA | ABI | Adaptation |
| --- | --- | --- |
| `2d11e0` | thiscall(panel), void | Scope the current HUD only; execute original Paint completely. |
| `2ce4c0`, `2ce5b0` | thiscall(root,x,y,w,h), void | Translate only item background calls. |
| `10cf20` | thiscall(icon,x,y,w,h,Color*), void | Translate only item icon calls, retain native color/pulse. |
| `2ce980` | thiscall(root,slot,x,y,w,h), void | Suppress only duplicate PC Steam Input item hints inside the restored cross. |

New D-pad/flashlight drawing runs at the first native item background call,
after original Paint has established the correct split-screen player context.
Flashlight state is the same native player's flag at `+e0`, bit 2. Original
Paint still chooses item textures, missing-item colors, selected backgrounds,
pickup alpha, weapon variants, ammo and the complete primary/secondary draw.
The three item positions follow the stock Xbox D-pad layout. The ISO resolves
its buttons back to slot bindings; arbitrary custom D-pad rebinding is not
implemented by this presentation adapter and bindings are never mutated.

The weapon-switch hint uses each player's actual `lastinv` binding. The
retained native Reset (`client+2cdcc0`) uses an unfiltered lookup which finds
the keyboard binding first. Its listener (`client+2ce2c0`) then requests
`#GameUI_Icons_<key>`: stock localization defines `Y_BUTTON` but no keyboard
`Q` token. Thus simply drawing the retained native buffer leaves the hint
absent. The Xbox painter does not condition this draw on primary inventory.

The adapter reads each native XInput record's connected state and user ID
(`InputSystem+3bf4+90h*slot`, fields `+8`/`+0`), or the actual `in_forceuser`
keyboard owner. For a controller it uses the engine's code-to-controller
function (`1a2470`) and native command matcher (`1a27d0`, code in EDI, command
on the stack) to find that controller's real binding, including compound
commands. It then uses the original listener to localize the actual button
name, saving and restoring the 64-wchar panel buffer at `+32c` with `__finally`.
For keyboard it uses verified engine interface slot155's keyboard-filtered
lookup and the panel's existing `PrimaryAmmoFont` (`HudAmmo`) to display the real
key name. It does not assume Y or Q, does not change controls, and omits an
unknown/unbound hint. Binding results refresh every 500ms and on device/HUD
changes; glyphs retain BindingsFont and the ISO anchor.

These queries additionally require the supported engine/inputsystem hashes,
their exact interface objects/vtables and verified helper entry bytes.

Install validates each complete call-table target before adding hooks. Paint
adaptation stays inactive until every hook succeeds. Nested Paint restores its
previous thread-local frame even on exception, and ineligible nested panels
cannot inherit it. Shared `RemoveHooks()` must precede `Shutdown()`, which
clears trampolines and state. No cvars or bindings are modified; the listener's
temporary display-buffer write is restored before drawing.

## Verification boundaries

The translation unit and promoted binding candidate compiled with MSVC x86
C++17 `/W4` without warnings. Independent reviews verified the adapter and
the candidate's lookup/helper calling conventions and buffer restoration.
The exact call destinations, helper stack cleanup (`ret 10h` / `ret 14h`),
resource properties, icon pointers and PPC arithmetic are captured above.
Root's live checks passed the cross in horizontal and vertical split, item
pickup (P1 axe and medkit), selected medkit glow, and flashlight on/off. The
read-only `probe_inventory.py` found both HUD binding buffers empty while
BindingsFont=170, binding Y=71 and IconSize=45 were valid. Initially both
players had only secondary weapons. The user then tested The Parish with
primary and secondary weapons and confirmed the Y hint remained absent.
This separates the user-observed result from the unconditional-draw finding
in the static Xbox code.

After that user test, the reviewed candidate was promoted unchanged to
`plugin/src/hud_inventory.cpp`, SHA256
`728fb53f796d25ccac6a5d867094f6584afe6603684bc6694d9b96aa4d8eb4fa`.
Live build `92fedfcc` then displayed the correct yellow Y for the controller
and Q for the keyboard guest (`hud_verified_parish_hints.jpg`). Q was too
small: the original fallback at panel `+15c` is correctly `TextFont`, but its
`HudSelectionText` definition is tall11. The final follow-up changes only the
keyboard font to cached `PrimaryAmmoFont` at `+438` (`HudAmmo`, tall18), already
used for readable native ammo in the same viewport. The registration at
client `2d0120` and accessor `2cd880` verify its name and offset. Controller
glyph font, positions, bindings and all other drawing remain unchanged.
This font follow-up still requires a live screenshot after rebuilding.
Exact resource equality alone is not evidence that a native draw branch runs.

Final runtime8e498ecb: root inspected hud_verified_final.jpg and hud_verified_final_vertical.jpg. ControllerY and keyboardQ are readable at both orientations after the cached HudAmmo font change. Game left on horizontal Parish. Earlier primary+secondary test confirmed the original PC buffer stayed empty; this correction changes presentation only.
