# Controller aim profile QA

The initial candidate replaces the native experiment's three stick response
values with the ISO `config.360.cfg` values: `joy_lowend 0.75`,
`joy_accelscale 2`, `joy_accelmax 1`. The next live trial also uses the disc's
sensitivity magnitudes for both slots: `joy_pitchsensitivity[2] +0.8` and
`joy_yawsensitivity[2] -2.25`. Positive PC pitch preserves the already verified
direction; the Xbox coordinate sign must not be copied blindly. At this
checkpoint the sensitivity extension and additional
`joy_pitchthreshold 0` compatibility adjustment are built. Both
platform InputSystems already apply the same physical stick deadzone; PC
client code adds a second pitch deadzone that the Xbox client omits. The zero
removes only that extra PC stage. The user's full sensitivity trial still
felt odd, so code/settings agreement must not be labeled a subjective parity
pass.

Aim-target selection, bullet behavior, mouse sensitivity, pitch direction,
bindings and the v27 fallback are outside this behavior change. A single
native input-module owner applies, snapshots, restores and protects config
serialization for the enabled bridge lifetime. The same owner must handle
all eight profile values, including
distinct original values for each split slot. There is no new VScript helper
or lifecycle injection. The user's prior archived values must survive config
writes and be restored on exit.

## Baseline evidence and its limits

The user first reported feeling some slowdown and, after trying the softer
profile, confirmed that aiming feels better. They subsequently clarified
that general looking still feels too twitchy or accelerates oddly. The
three-value profile is therefore an intermediate improvement, not completion
of Xbox controller-feel parity. The subsequent binary comparison found the
same downstream response algorithm, acceleration thresholds, time
accumulation/clamping, frame-time selector and base turn rates in both builds.
It subsequently found PC's extra pitch deadzone described above. Read-only live evidence
also confirms `joy_variable_frametime 1`, `joy_pegged 0.75` and
`joy_accel_filter 0.2`. No change to those values is indicated. The final
static artifact audit passed the eight-value build; installed/runtime checks
remain separate below.

[`controller-sweep-before.json`](../../test-records/aim-assist/controller-sweep-before.json)
contains1800 samples per player over90.0005 seconds. It missed the manual
sweep: the connected controller's packet stays8756, and its right stick is
exactly(-262,-302) throughout. Thus there is no recorded controller action
against which to assess slowdown. Both target flags are false throughout;
this cannot demonstrate broken aim assistance.

The baseline correctly records P1 cl_inputmode1, P2 cl_inputmode0, global
cl_autoaim1, dampen0.3 and range0.85. It used the probe's first version, which
read only one lazy cl_autoaim cache; that cache stayed uninitialized. The
current probe checks both native response caches and a one-second reader
smoke confirmed cl_autoaim1 for both slots. The old capture remains intact,
with its limitation recorded rather than reinterpreted.

The second90-second capture,
[`controller-sweep-xbox-trial.json`](../../test-records/aim-assist/controller-sweep-xbox-trial.json),
does contain actual input:926 distinct P1 XInput packets,589 samples with the
right stick beyond the standard XInput reference deadzone,120 native target
true samples, and75 samples with both target true and stick movement. In66
of those75, both raw axes are below0.85×32768. P1 remains inputmode1 and
cl_autoaim1; P2 remains inputmode0/cl_autoaim1 with no target activation.
[`trial-sample-analysis.json`](trial-sample-analysis.json) records these counts.
This establishes native target activation during the trial. It does not
measure the final angular multiplier: raw stick fractions are not identical
to the game's post-deadzone helper arguments. The untouched first capture
cannot support a before/after effectiveness comparison.

## Static package acceptance

Compare the candidate against the saved
[`baseline-native-splitscreen_support.vpk`](../../test-records/aim-assist/baseline-native-splitscreen_support.vpk):

- Only addoninfo AddonVersion31→32 may change in the VPK; no added or removed
  entries, script edits, or new helper are allowed.
- Validate every VPK entry CRC and exact equality with the generated stage.
  Verify all previous HUD/menu assets, portraits, prompts and native character
  observer are otherwise identical.
- Confirm the agreed profile values, applied by the native input module for
  the explicitly enabled bridge: three response cvars, four sensitivity
  cvars and the pitch-threshold adaptation. No other feel adjustment is
  implied. Restore on native disable and ordinary quit, and retain distinct
  original slot values across repeated activation.
- Check the v27 VPK remains SHA256
  `93812da20f57c0208216939501e65f9418f31405136a0e801bb2caa35b42a123`.
- Check the candidate DLL/source manifest and mandatory tests, plus installed
  hashes only after root has installed. These are static checks, not runtime
  certification.

## Runtime checks owned by root

Completed runtime evidence is in
[runtime-checks.txt](../../test-records/aim-assist/runtime-checks.txt).
The installed audit at 03:00:19 UTC verifies final DLL/VPK equality. Fresh
startup, disable/re-enable, map setup, live configuration writing and normal
quit passed. Original preference values and ordered bindings survived; the
full config differs only in numeric formatting. The game is closed. The user
could not physically test the final deadzone correction, so that feel check
and a second-controller test remain unverified. The planned checks below
describe the broader checklist, not an assertion that every step was run.

Static checkpoint: [package-audit-20261010T025945931253Z.json](../../test-records/aim-assist/package-audit-20261010T025945931253Z.json)
verifies DLL `8c172117da20171af5431aed776c9eae4ba1fb659f8ff6bea7a9cd33c48b2c0c`
and VPK `1dcd6d64360a5ac6f10b2e24d34c34437b65654aae0ac147deb4325dab01e583`.
The 93-entry package changes only its version metadata. Native source changes
are exactly `input.cpp`, `input.h` and `tests/input_abi.cpp`; all other native
sources match the saved version-31 manifest. All three mandatory native tests
passed. This checkpoint predates installation of the final DLL.

1. At a fresh owned local game, read back 0.75/2/1, pitch +0.8/yaw -2.25
   for both slots, and extra pitch threshold 0. Verify existing look response 1,
   dampen 0.3/range 0.85, positive pitch direction, per-player inputmode and
   keyboard ownership remain as before.
2. Start the updated read-only probe while the user is present. Confirm its
   capture actually includes changing XInput packet/axes and a controlled
   sweep across an infected. Record any target true transitions separately
   from the subjective feel comparison. Lack of a target hit is inconclusive
   without observing a suitable aim target.
3. Ask the user to compare fine aiming and a large turn with the same weapon,
   recording the exact sensitivity used in each trial. Verify P2 mouse
   movement still behaves normally. A second
   physical controller remains untested because only one is available.
4. Validate chapter/restart/return paths retain the profile while the bridge
   is enabled. Check native disable restores prior values and repeated
   enable/disable does not replace the original snapshot
   with the temporary Xbox values.
5. While the profile is active, exercise normal config serialization and
   ordinary quit. Compare only expected formatting normalization against the
   saved config; the user's original acceleration, both slots' sensitivity,
   pitch threshold, keyboard/mouse bindings and unrelated settings must persist. Verify a
   fresh ordinary launch sees those original values.

No native aim-target, bullet or camera behavior change should be claimed
unless separately implemented, reviewed and tested. A nicer response curve
is not proof that a missing aim-assist implementation was restored.
