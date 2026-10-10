Run from the workspace root:

```powershell
./plugin/tests/run.ps1
```

This uses the same MSVC Win32 toolchain as the plugin, builds the source fixtures,
and runs CTest. It does not load, modify or control the game. Assertions remain
enabled in Release builds.

- `guest_abi.cpp`: raw native virtual-slot calls, 64-bit identity return,
  independent profile/title storage, write bounds, RTTI rejection, native
  release/destructor ABI, foreign-slot refusal and cleanup.
- `input_abi.cpp`: native ICvar/IConVar calling conventions, missing-guard
  refusal, guard-before-joystick ordering, one preserved snapshot, exact numeric
  restoration and retry after a failed joystick restore.
- `safearea_abi.cpp`: 32 scoped menu/HUD instruction-hook dispatches, preserving
  general registers, flags, stack, x87 and SSE state even when the ownership
  query disturbs floating-point state; missing-module refusal and shutdown.

The fixtures include their implementation file directly and run against isolated
memory and interface doubles. They do not establish native lobby, avatar or input
correctness or safe-area geometry in the running game; those require the separate
runtime checks. No physical input test is implied by these fixtures.
