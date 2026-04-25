# GoySDK Fixes — Index

Each fix is a self-contained `.md` file under one of four severity folders. Read the fix end-to-end before starting; every file lists exact file paths, line numbers, before/after snippets, verification steps, and common mistakes to avoid.

Severity rubric:
- **P0 (critical)** — silent breakage, data corruption, or admin-context RCE. Fix before next release.
- **P1 (high)** — data races, dead user-facing features, latent crashes. Fix this sprint.
- **P2 (medium)** — quiet quality-of-life bugs, subtly wrong behavior. Fix when nearby.
- **P3 (low)** — tidy-ups, dead code, typos, build-config drift.

---

## P0 — Critical

| File | Issue |
|---|---|
| [P0_critical/01_deflate_silent_stub.md](P0_critical/01_deflate_silent_stub.md) | DEFLATE'd model files load as a silent no-op bot |
| [P0_critical/02_is_initialized_lies.md](P0_critical/02_is_initialized_lies.md) | `Bot::is_initialized()` always returns true even on load failure |
| [P0_critical/03_path_traversal_admin_rce.md](P0_critical/03_path_traversal_admin_rce.md) | GitHub release `asset.Name` is path-traversal sink, run as admin |
| [P0_critical/04_unverified_installer_execution.md](P0_critical/04_unverified_installer_execution.md) | Downloaded installers run elevated with no signature check |
| [P0_critical/05_action_mask_after_sampling.md](P0_critical/05_action_mask_after_sampling.md) | Action mask defeats stochastic policy sampling |
| [P0_critical/06_action_table_duplication.md](P0_critical/06_action_table_duplication.md) | Action tables duplicated in two libraries with no compile-time link |

## P1 — High

| File | Issue |
|---|---|
| [P1_high/01_padstates_data_race.md](P1_high/01_padstates_data_race.md) | `padStates_` written under one mutex, read under a different mutex |
| [P1_high/02_kickoff_timer_multislot.md](P1_high/02_kickoff_timer_multislot.md) | Kickoff timer ticks once per slot, runs N× too fast |
| [P1_high/03_gameevent_use_after_free.md](P1_high/03_gameevent_use_after_free.md) | `gameEvent_` deref without revalidation in auto-skip / auto-forfeit |
| [P1_high/04_dead_join_press_countdown.md](P1_high/04_dead_join_press_countdown.md) | `joinPressCountdown_` negative branch is unreachable |
| [P1_high/05_dead_auto_requeue_chat.md](P1_high/05_dead_auto_requeue_chat.md) | `autoRequeue_` and `autoChat_` are pure UI placebos |
| [P1_high/06_zip_integer_overflow.md](P1_high/06_zip_integer_overflow.md) | ZIP-parser bounds checks use `a + b > c` (overflow) |
| [P1_high/07_overlay_atomic_state.md](P1_high/07_overlay_atomic_state.md) | `IsInGame` and `CurrentGameEvent` are non-atomic across threads |

## P2 — Medium

| File | Issue |
|---|---|
| [P2_medium/01_humanizer_deadzone_clobbers_policy.md](P2_medium/01_humanizer_deadzone_clobbers_policy.md) | Humanizer deadzone snaps model output to zero |
| [P2_medium/02_humanizer_state_leak_on_swap.md](P2_medium/02_humanizer_state_leak_on_swap.md) | `AssignModel` doesn't reset humanizer state |
| [P2_medium/03_x64_hmodule_truncation.md](P2_medium/03_x64_hmodule_truncation.md) | `InjectLoadLibrary` returns 32-bit truncated HMODULE on x64 |
| [P2_medium/04_enum_modules_null_arg.md](P2_medium/04_enum_modules_null_arg.md) | `EnumProcessModulesEx` first call passes `null` to `[Out]` array |
| [P2_medium/05_filedownloader_retry_useless.md](P2_medium/05_filedownloader_retry_useless.md) | Download retry budget (3.2s) dwarfed by per-attempt 30-min timeout |

## P3 — Low

| File | Issue |
|---|---|
| [P3_low/01_was_active_unused.md](P3_low/01_was_active_unused.md) | Unused `bool wasActive` in `AssignModel` |
| [P3_low/02_dead_for_loop_overlay.md](P3_low/02_dead_for_loop_overlay.md) | Empty body for-loop in `OverlayRenderer::PlayerTickCalled` |
| [P3_low/03_dllmain_member_function_cast.md](P3_low/03_dllmain_member_function_cast.md) | `Core.InitializeGlobals` cast to `LPTHREAD_START_ROUTINE` |
| [P3_low/04_boost_diag_first_car_only.md](P3_low/04_boost_diag_first_car_only.md) | `boostDiagDone_` printed only for first car with boost component |
| [P3_low/05_xor_obfuscation_theatre.md](P3_low/05_xor_obfuscation_theatre.md) | `ProcessFinder` XOR obfuscation gives false confidence |
| [P3_low/06_nullcheck_macro_safety.md](P3_low/06_nullcheck_macro_safety.md) | `nullcheck` macro unused, lacks `do { } while(0)` wrapping |
| [P3_low/07_typo_initalized.md](P3_low/07_typo_initalized.md) | "Initalized" typo in OverlayRenderer log |
| [P3_low/08_license_bootstrap.md](P3_low/08_license_bootstrap.md) | LICENSE file is the Bootstrap CSS license |
| [P3_low/09_duplicate_iostream_pch.md](P3_low/09_duplicate_iostream_pch.md) | Duplicate `#include <iostream>` in pch.hpp |
| [P3_low/10_dead_oncreate_ondestroy.md](P3_low/10_dead_oncreate_ondestroy.md) | Empty `OnCreate`/`OnDestroy` in OverlayRenderer |
| [P3_low/11_cmake_glob_recurse.md](P3_low/11_cmake_glob_recurse.md) | `GLOB_RECURSE` doesn't trigger reconfigure on new files |
| [P3_low/12_cpp_standard_mismatch.md](P3_low/12_cpp_standard_mismatch.md) | CMake says C++17, README says C++20 |
| [P3_low/13_passthrough_observed_on_ground.md](P3_low/13_passthrough_observed_on_ground.md) | `ComputeObservedOnGround` is a passthrough |

---

## Order of operations (suggested)

1. P0/02 (`is_initialized` honest) — single-line change, unblocks meaningful errors for everything else.
2. P0/01 (DEFLATE) — depends on P0/02 to surface errors.
3. P0/06 (action table de-dup) — must precede any future training change.
4. P0/05 (mask before sampling) — same change touches `Bot::forward`, do alongside P0/06.
5. P0/03 + P0/04 (loader supply chain) — gate the loader release on these.
6. P1/01–P1/07 — order doesn't matter, mostly orthogonal.
7. P2 / P3 — opportunistic.

## Conventions used in every fix file

- **File path** is always absolute under `/Users/carterbarker/Documents/GoySDK/`.
- **Line numbers** were captured at the time of writing; verify with `grep -n` before editing — line numbers drift.
- **Before/after** snippets show the literal text to find and the literal text to replace it with. Match indentation exactly.
- **Verification** lists at minimum a build step and a runtime check.
- **Don't do** lists known wrong-turns the AI might attempt.
