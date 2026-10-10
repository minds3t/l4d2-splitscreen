# Native per-player action prompts

The user screenshot `docs/test-records/prompts-votes/idle-before.jpg`
shows a left mouse button beside P1's German idle takeover text, although
P1 is using the controller. The hint belongs to `CTakeoverBar`'s `FireBind`
child (`CBindPanel`), not the weapon-selection painter. The original Xbox
and PC `takeoversurvivorbar.res` both use this native control. PC takeover
ApplyScheme at client `2e5fea` supplies `+attack` and the active local slot.

`CBindPanel::Update` at client `2d65c0` asks engine virtual155 for the command
stored at panel `+14f`, local slot `+458`, binding index0 and device−1 (any).
Its exact return address is client `2d68ec`. The current PC engine helper
`1a28d0` ignores the local-user argument; its ascending button-code search
finds MOUSE1 before R_TRIGGER. Xbox Update at `88530af8` similarly supplies
the stored local user through its engine interface. This comparison confirms
that the control was designed to request a particular local user's binding;
this trace does not independently reverse-engineer the Xbox engine lookup.
The resource itself does not choose the mouse glyph.

`plugin/src/hud_prompts.cpp` adapts only that exact CBindPanel lookup callsite,
under the verified engine/client/inputsystem fingerprints, explicit enabled
bridge and owned offline session. A connected native XInput controller owns
its matching slot; otherwise only the actual read-only `in_forceuser` slot
gets keyboard/mouse prompts. Native button-controller classification at
engine `1a2470` and command matcher `1a27d0` supply actual bound key names.
The matcher retains Source's semicolon and +command interpretation; the
adapter also preserves native duck/zoom aliases while keeping device/slot
filtering. The PC helper's alias fallback otherwise resets the device filter.

All prompt rendering remains native. Existing CBindPanel recognizes Xbox
button names when joystick is enabled and localizes `#GameUI_Icons_<name>`;
mouse icons and keyboard keycaps remain native for the keyboard player.
No fixed trigger/key, new glyph material, display-buffer mutation, binding
change or global input predicate is introduced. Unbound/unowned devices
return no binding. Other lookups, foreign interface objects, invalid local
slots, non-owned sessions and disabled bridge calls forward unchanged.
Shared CBindPanel controls in infected spawn/attack/continue resources use
the same corrected lookup; unrelated instructor/confirmation systems do not.

The native panel already refreshes every 0.5 game seconds: vtable slot101
calls `2d6a00`, comparing current time with panel `+568`; Update reschedules
that field using the float0.5 at client `53d16c`. A startup lookup before the
map config assigns the keyboard owner can report device=none, then update
naturally after routing initializes. No custom invalidation is needed.

Static evidence is reproducible with `prompt_binary_evidence.py` and saved
as `prompt-binary-evidence.json`. The standalone MSVC x86 `/W4` compile passed.
`prompt_abi_test.cpp` passed with assertions enabled, covering the native EDI
matcher ABI, separate controller/keyboard and two-pad bindings, aliases,
unbound omission, and scope forwarding. No game libraries are loaded by
that fixture. Root's intermediate 1600x900 check (`idle-900.jpg`, build c14f)
shows the correct native RT bitmap for P1 instead of Mouse1. Final frozen
source and 4K runtime verification remain separate checks by the root task.
