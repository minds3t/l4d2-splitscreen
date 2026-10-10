# Native Xbox controller aiming evidence

The ISO has an explicit native stick slowdown, scoped to the active local
player. The PC still has that mechanism. Changing `sk_autoaim_mode` or
disabling mouse look is not established as necessary by this trace.

## Reproduce

```powershell
python docs/research/xbox_lobby/extract_basic_xex.py xbox_raw/left4dead2/bin/server_360.dll
python docs/research/xbox_lobby/extract_basic_xex.py xbox_raw/bin/inputsystem_360.dll
python docs/research/aim_assist/capture_xbox.py
python docs/research/xbox_lobby/ppc_refs.py docs/research/xbox_lobby/client_360.dll.pe '@0x88383a50' 40
python docs/research/xbox_lobby/ppc_refs.py docs/research/xbox_lobby/server_360.dll.pe '@0x89a7ab18' 107
```

`xbox-evidence.json` (written by `capture_xbox.py`, not kept in the repository)
records module hashes, disassembly,
config lines and 18 weapon comparisons. Xbox decoded client SHA256 is
`3d6b0289cbd0fd4bda79652fa05256d39414f8f82b8756516465ffae61e01703`;
decoded server is
`8d65ef59b23973c97ed856a75adb871a60d34f2eb4836384a5e42d74d481b212`.
These XEX images are already expanded to memory layout; do not apply their
embedded PE raw-section offsets again.

## Configs and defaults

The actual disc's `xbox_ui/left4dead2/cfg/config.360.cfg` selects:

| Setting | Xbox config.360 | Legacy 360controller.cfg |
|---|---:|---:|
| joy_autoaimdampen | 0.3 | 0.3 (final assignment) |
| joy_autoaimdampenrange | 0.85 | 0.85 |
| joy_response_look | 1 | 1 |
| joy_lowend | 0.75 | 0.65 |
| joy_lowmap | 0.15 | 0.15 |
| joy_accelscale | 2.0 | 3.0 |
| joy_accelmax | 1.0 | 4.0 |

The first file sets `cl_mouselook 0` with a historical autoaim comment. The
second sets `sk_autoaim_mode 2` and begins with `unbindall`. Neither entire
file is safe to execute in this mixed-input experiment. In particular,
config.360 also has the original negative Xbox pitch sensitivity, while the
PC controller direction was already corrected separately.

Xbox client constructors register `joy_autoaimdampenrange` as default0,
raw flags0x80 at886DAD50, and `joy_autoaimdampen` as default0, flags0x280 at
886DADA0. Thus the console config supplies the operating values.
`sk_autoaim_mode` is default1, flags0x2080 in both client886D82C0 and
server89E17090. Registration and an old config command do not establish a
live use. No direct static object reference outside registration was found
in this pass; indirect use has not been globally ruled out.

## Per-player slowdown and target production

Xbox client helper88383A50 returns the damping factor only when all apply:
`joy_autoaimdampen > 0`, `abs(stick) < joy_autoaimdampenrange`, and the active
local player's `m_bAutoAimTarget` byte at+126C is true. Otherwise it returns1.
The caller at88383CB8 multiplies the native stick response by this factor;
this is slowdown, not a new view-angle snap.

GetLocalPlayer(-1) at882A56E0 queries the engine's active local slot through
virtual+1FC and indexes the pointer array8878AA00. The target byte is a
networked player-local field (receive registration882A78AC), not a shared
global target for both players.

Xbox server target update89A7AB18 queries `joy_autoaimdampen` for the specific
player through engine virtual+E0. The eligible-player method89A6D540 rejects
the native fake-client flag and requires team2. Unlike the PC method reported
by the input-runtime audit, this Xbox method has no `cl_inputmode` or
`cl_autoaim` userinfo check.

The update requires an active weapon, throttles the target check by0.1s,
calls native autoaim virtual+6F0, and publishes the result to player+1AC5.
Its parameter struct has scale1, max distance from `autoaim_max_dist`,
weapon-default deflection(-1), and direct-hit-only flag+28=1. Target code
89A7D660 sets+2A for a valid crosshair hit, and89A7D6A4 exits before the wider
candidate search when that direct-only flag is set. This separates the
slowdown's crosshair-target signal from the broader native bullet-deflection
path. The inspected path does not implement stick camera lock-on/autosnap;
this is not a claim that every Xbox aiming function has been decoded.

The server also registers `autoaim_max_dist=2160` and
`autoaim_max_deflect=0.99`, raw flags0, at89E1C480 and89E1C4C8. All18 available
weapon scripts' aim fields match the current winning PC VPK scripts exactly:
most deflection fields10.0, hunting/sniper rifles6.0, scale1.0, range0. The
evidence gives no reason to replace these weapon resources.

## Stick processing: matching curve, different extra deadzone

The mode-1 radial/pegged response at Xbox `88383D50` matches PC client
`12FB10`: the same per-slot/per-axis accumulator, `joy_pegged 0.75`,
`joy_accel_filter 0.2`, scale-times-delta accumulation, cap, reset and final
target damping. Xbox constructors `886DB350` and `886DB3E0` establish those
threshold defaults. This comparison is downstream of axis preprocessing.

Xbox `88384378` obtains the active slot's available accumulated input time,
rejects nonpositive availability, clamps the caller's delta to that amount
and subtracts consumed time (`88384464`–`88384488`). At `88384998`,
`joy_variable_frametime` chooses that clamped input delta or globals frame
time. The FOV sensitivity multiplier is applied at `883849DC`–`883849E8`.
These operations match PC `1300D0`, `1301E1`–`13020B`, `13082D`–`13087C`.
Read-only current PC evidence in
[`live_curve_defaults_408c.json`](live_curve_defaults_408c.json) confirms
variable frame time 1, pegged 0.75 and filter 0.2. An earlier console
"Unknown command" observation did not mean these retained variables were
absent.

Final Xbox yaw `88384C84`–`88384CAC` and pitch `88384E14`–`88384E3C` multiply
the curve by per-slot sensitivity, frame delta, FOV scalar and base turn
rate. Constructors `886DB470` and `886DB4B8` register base yaw 210 and pitch
225, matching PC. Thus copying the console sensitivity magnitudes is
meaningful; copying its negative pitch sign would break the already verified
PC direction. The trial uses positive 0.8 pitch and negative 2.25 yaw on both
slots. Zoom sensitivity remains outside this trial because the native global
zoom ratio also affects the mouse path.

The input boundary is **not entirely identical**. Decoded Xbox InputSystem
SHA256 is `13ca74fca86970b4080841c98fc3d2d1ffb44efbb315b16ce4ddee7a43425b62`
(base `83500000`). Its constructor `83512678` stores right-stick threshold
8689 in each signed-axis field `+50/+54/+58/+5C`, and left threshold 7849 in
`+40/+44/+48/+4C`. Right-stick processing at `83512DDC`–`83512F60` zeros each
axis within the threshold and otherwise subtracts it, scales the remainder
and truncates to integer. Negative and positive scale constants at
`83500C7C`/`83500C78` are respectively `32768/(32768-8689)` and
`32767/(32767-8689)`, rounded to float. The publisher `83512778` routes RX
to analog index `slot*6+8` and inverted RY to `slot*6+7`.

PC InputSystem has the same upstream filtering (independent PC trace in
[`stick-response.md`](stick-response.md)). Xbox client `88384740`–`88384758`
then multiplies all four mapped axes by `1/32768`. PC client
`130509`–`13059F` additionally performs
`sign(axis)*(abs(axis)-threshold*32768)/(32768-threshold*32768)`, or zero
inside that second threshold. Its current yaw threshold is 0, but pitch is
0.1. The extra pitch stage therefore differs from the console pipeline even
though the config text contains the same threshold number. Setting the PC
pitch threshold to 0 is the narrow existing-setting adaptation for this
verified look-axis difference; it does not remove the upstream physical
deadzone. Movement thresholds and the actual controller hardware response
remain separate from this look-axis finding.

## Read-only live probe

```powershell
python docs/research/aim_assist/probe_runtime.py --seconds 30 --output docs/test-records/aim-assist/controller-sweep.json
```

The probe automatically locates the game and verifies the supported client
file SHA256 before reading the relocated player array and cvar references.
It opens only PROCESS_VM_READ|PROCESS_QUERY_INFORMATION. It never writes
memory, injects input, invokes remote functions, executes console commands or
changes cvars. XInputGetState only reads physical pad state.

It samples actual per-player target bytes, both independently lazy native
`cl_autoaim` caches, `cl_inputmode`, and XInput0/1 axes/buttons at20Hz. Names
must match each expected split cvar; null references and incorrectly aliased
names stay explicit unavailable values. The summary records transitions,
target hits and right-stick movement. The8689 threshold in that movement
summary is the standard XInput right-stick deadzone reference, not a claim
about the game's configurable threshold; raw stick values are retained.

A one-second smoke read verified both actual players and cl_autoaim 1, with
P1 inputmode 1/controller connected and P2 inputmode 0/no controller. The
subsequent 90-second trial captured 120 native target-true samples and 75
simultaneous target-plus-stick-movement samples. See
[`trial-sample-analysis.json`](trial-sample-analysis.json) and
[`qa-plan.md`](qa-plan.md) for the counts and the initial baseline's missed
input limitation. Native target activation is confirmed; the angular damping
factor was not measured directly.

The user said the softer three-value response improved aiming, then reported
that general looking still felt odd even with console sensitivity magnitudes.
That is not a subjective feel-parity pass. The extra PC pitch deadzone was
identified afterward. A single controller is available; two-controller feel,
original Xbox hardware and display-latency equivalence have not been tested.
