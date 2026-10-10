# Xbox stick-response profile

Read-only comparison and native curve trace, 2026-10-10. Client DLL SHA-256:
`c0a8e1e88f7312db11bcfd4b0ec574d224a34b2d9a615d6088b28335eeb4af1f`.
All code addresses below are RVAs in this exact PC `client.dll`.

## Configuration evidence

The console startup profile is `xbox_ui/left4dead2/cfg/config.360.cfg`, lines
45–52. The disc also contains `cfg/360controller.cfg`, whose response values
match the PC profile; that second file must not be mistaken for console startup.

| Variable | PC before profile, live query reported by root | Console startup |
| --- | ---: | ---: |
| `joy_lowend` | 0.65 | 0.75 |
| `joy_accelscale` | 3 | 2 |
| `joy_accelmax` | 4 | 1 |
| `joy_lowmap` | 0.15 | 0.15 |
| `joy_response_look` | 1 | 1 |
| `joy_response_move` | 5 | 5 |

The saved PC configuration corroborates these values. `joy_autoaimdampen=0.3`
and `joy_autoaimdampenrange=0.85` also already match. Pitch/forward/side
deadzones are 0.1 and yaw deadzone is 0 in both saved profiles. The current
addon sets the same advanced axis mapping as Xbox when the local pair is ready.
Saved `joy_advanced=0`, unmapped axes and `joystick=0` are the restored preference,
not evidence that active gameplay has those settings.

An initial console query reported **Unknown command** for
`joy_variable_frametime`. That did not establish its absence: the PC constructor
at `0x500b90` and active reader at `0x13082d` remain present. Read-only live memory
evidence in `live_curve_defaults_408c.json` confirms its value is already **1**,
matching Xbox. It also confirms `joy_pegged=0.75`, `joy_accel_filter=0.2`,
`joy_lowmap=0.15`, response mode 1, and the active three-value Xbox profile.
No timing/filter change is warranted.

## Retained PC curve

Dispatcher RVA `0x12fd00` calls the radial/pegged helper at `0x12fb10` for
response mode 1. Other modes use `0x12f900`; confusing these helpers would give
the wrong meaning to `joy_accelmax` in the active configuration.

For mode 1, let `x` be the absolute normalized axis value passed to this helper,
`r` the magnitude of the two look axes, and `a` its per-slot/per-axis acceleration
state. Before later sensitivity, sign and target dampening:

```text
if r >= joy_pegged and x > joy_accel_filter:
    a = min(joy_accelmax, a + joy_accelscale * frameTime)
    output = joy_lowmap + (max(x, joy_lowmap) - joy_lowmap) * a
else:
    a = 0
    output = x / joy_lowend * joy_lowmap
```

The registered defaults of `joy_pegged` and `joy_accel_filter` are 0.75 and 0.2
(constructors `0x501430` and `0x501490`). The live read above confirms both values;
neither is changed by the profile.

The helper reads lowend at `0x12fc69`, lowmap at `0x12fc73`/`0x12fb83`, acceleration
scale at `0x12fbaa`, and cap at `0x12fb95`. State is indexed by local slot and
look axis, not shared between both players. The three tuning variables
themselves are global joystick settings, with archive flag `0x80`.

Consequences with the otherwise matching profile:

- Fine-stick slope changes from `0.15/0.65` to `0.15/0.75`: approximately 13.3%
  gentler output in the unpegged branch.
- The acceleration accumulator grows at 2 per second instead of 3.
- Sustained normalized full-axis input eventually maps to 1.0 with Xbox's cap,
  versus 3.55 with the PC cap. These are helper outputs, **not final degrees per
  second**; sensitivity and native target dampening still apply.

This provides a concrete explanation for a substantial stick-feel difference.
It does not by itself prove whether every Xbox target-assist path works on PC.

## Sensitivity and pitch convention

The full profile also restores the console's sensitivity magnitudes for both
local controller slots:

| Setting | Baseline P1 | Baseline P2 | Native Xbox-equivalent PC profile |
| --- | ---: | ---: | ---: |
| `joy_pitchsensitivity` / `joy_pitchsensitivity2` | +1 | +1 | +0.8 / +0.8 |
| `joy_yawsensitivity` / `joy_yawsensitivity2` | -1.5 | -1 | -2.25 / -2.25 |

The disc uses pitch -0.8; positive PC pitch retains the physically verified
normal look direction. `joy_inverty` remains the independent per-slot inversion
preference (PC sign flip `0x1305d7`–`0x1305fa`). It is not changed.

PC yaw `0x130b5e`–`0x130b7c` and pitch `0x130d03`–`0x130d18` multiply the curve
output by the selected local player's sensitivity, elapsed time, the FOV/zoom
scalar, and the corresponding base speed. Xbox code uses the same base speeds:
210 for yaw and 225 for pitch. See `xbox-evidence.json` for the console
registration and final-rate instructions. At steady full-axis input, no zoom
or target dampening, the console profile's nominal rates are 472.5 degrees/sec
yaw and 180 degrees/sec pitch. These are derived rates, not physical timings.

This restores the console's faster-horizontal/slower-vertical balance;
changing only the three curve values left a different balance in place.

`config.360.cfg` additionally sets `zoom_sensitivity_ratio=0.375`. PC defaults
to 1.0. Its FOV update `0x1bb132`–`0x1bb160` contributes to both joystick and
mouse sensitivity, despite the mouse-oriented help text. It remains unchanged
to preserve the keyboard/mouse player's zoom response. Exact scoped zoom parity
is therefore not claimed.

## Pre-curve normalization and the extra PC deadzone

The native XInput paths on both platforms already remove an axial right-stick
deadzone of 8689, then rescale to the full signed integer range. PC
`inputsystem.dll` initialization is RVA `0x5310`; right X/Y processing starts at
`0x5c8e` / `0x5eca`. The negative scale is `32768/(32768-8689)` and positive
scale is `32767/(32767-8689)`; integer conversion truncates the result. X preserves
its sign and Y flips sign. `pc-precurve.json` (not kept in the repository) contains exact binary hashes,
constants and disassembly. The Xbox counterpart uses the same thresholds,
scales, truncation and Y flip, as recorded by the independent Xbox audit.

The clients then differ. Xbox at virtual address `0x88384740`–`0x88384758`
multiplies these integers directly by `1/32768` (constant `0x88143030`). PC at
RVA `0x130509`–`0x13059f` applies the configured pitch/yaw threshold again:

```text
p = InputSystem's already-deadzoned integer axis
t = joy_pitchthreshold or joy_yawthreshold
normalized = 0                                      if abs(p) <= 32768*t
normalized = sign(p)*(abs(p)-32768*t)/(32768-32768*t) otherwise
```

With pitch threshold 0.1, PC vertical input starts at approximately 33.9% of
physical axis travel instead of the upstream 26.5%. Yaw threshold is already
zero. A temporary `joy_pitchthreshold=0` removes precisely this additional PC
layer and reproduces the audited Xbox pre-curve normalization; the original
8689-unit native hardware deadzone remains. This is a functional platform
adaptation even though the disc's unused client threshold setting says 0.1.

## Scope and preference preservation

The full sensitivity trial used the three curve values and four per-slot
sensitivity values above. The final installed profile adds the pitch-threshold
correction, for eight values with one owner: the native input module for the
explicitly enabled bridge lifetime. The user could not perform another physical
test of that last adjustment; its feel remains unconfirmed. That owner snapshots,
applies once, restores, and substitutes the original archived values during
config serialization. There is no VScript profile or second map-scoped owner.
The fallback package remains unchanged.

The native Controller menu can still change each player's sensitivity during
the session; these edits are session-only, and the user's pre-enable archived
preferences are preserved. Inversion, axes, bindings, mouse controls and the
`cl_autoaim` preference remain unchanged. Copying Xbox's negative pitch or
movement sensitivity would conflate different platform axis conventions with
this correction. The profile affects joystick processing, not the mouse path.

The user accepted using the audited console logic/settings but still described
the feel as unusual. This is **not** a subjective feel-parity pass. Controller
hardware, driver/Steam Input processing before XInput, display latency, frame
pacing and scoped-zoom behavior are not certified identical by this audit.
