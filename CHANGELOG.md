# Changelog

All notable changes to this project will be documented in this file.

## [0.1.6] - 2026-02-17

Changes since `0.1.5` (`980e2ea..HEAD`).

### Highlights

- Added raw video pipe workflows:
  - `--background-video -` reads `stdin` as raw `rgb24`
  - `--output -` writes raw `rgb24` to `stdout`
- Added Valentine heart support for snowflakes:
  - `--shape heart`
  - `--color-mode valentine|red`
  - `--heart-spin`
- Improved multi-effect controls with normalized fade/warmup behavior and per-stage max fade support.
- Improved CLI help and metadata output with basic/advanced option grouping and enumerated choices.

### New

- Added pipe input/output examples and format docs to `README.md`.
- Advanced option model now supports:
  - `advanced` visibility
  - `choices` for constrained values
  - clearer color type handling (`string.color`)
- Twinkle effect can now track stars.

### Improvements

- Color option behavior is more consistent across effects.
- Coordinate normalization improvements in effects.
- Better documentation around warmup behavior and minimum values.

### Fixes

- Fixed wave-effect regression related to global warmup.
- Fixed looping background video behavior in wave effect.


## [0.1.5] - 2026-02-12

### Highlights
- Added a new `flame` effect with a full fluid-style simulation and extensive controls.
- Added flame presets including `smallcandle`, `candle`, `campfire`, `bonfire`, `smoketrail`, and `mist`.
- Added support for multi-effect pipelines by repeating `--effect` (order-sensitive).
- Added multi-threaded multi-effect processing in the render pipeline.
- Added pipe workflows:
  - `--background-video -` to read rawvideo `rgb24` from stdin
  - `--output -` to write rawvideo `rgb24` to stdout
- Added global `--warmup` pre-roll and `--show` to print resolved effect config.
- Added Valentine-style snowflake modes:
  - `--shape heart`
  - `--color-mode pink|red|valentine`
  - `--heart-spin` option
- Upgraded laser visuals with better beam behavior and new controls (hardness/highlight/shadow/saturation/pulse tuning).
- Improved coordinate/parameter behavior in effects (including normalized handling updates and twinkle ground-threshold in pixels).

### Changes
- Version updated to `0.1.5`.
- Documentation expanded with multi-effect and pipe examples.
- Effect/runtime logging was shifted to stderr in multiple places to keep stdout clean for rawvideo piping.
- Sparkle defaults were tuned for smaller stars/spot sizes and wider hotspot separation.

### Breaking / Migration Notes
- The standalone `mist` effect file was removed from the build.
  - Use: `--effect flame --preset mist`
- CLI semantics now center around one or more `--effect` stages; effect options are parsed per active stage.

## [0.1.4] 2026-01-29

*Compared to 0.1.1 (Dec 29, 2025), this release focuses on new effects and cross‑platform robustness.*

## Added

* New sparkle effect: edge‑aware sparkles that track moving edges/corners, with star/spot variants, twinkle, rotation, brightness bias, and configurable parameters.
* New mist effect: soft drifting mist/smoke using layered noise with warp, height bias, and tint controls.
* Build integration for new effects (Makefile sources updated).

## Improved

* Sparkle visuals refined for more realistic/beautiful results (multiple iterations since initial addition).
* FFmpeg/FFprobe discovery more robust: PATH lookup, quoted env var handling, and Windows .exe suffix support.

## Compatibility / Refactor

* Process handling refactor for better Windows compatibility: custom spawn/close for FFmpeg/FFprobe instead of popen, proper pipe management, and platform‑specific process cleanup.
* FFmpeg invocation now built via argument vectors (avoids shell quoting pitfalls and improves reliability).
* FFMPEG_PARAMETERS parsing now uses a simple shell‑style split, allowing quoted args without breaking.

## UX / Messaging

* When output exists without --overwrite, error message now explicitly suggests --overwrite.``


# [0.1.1] - 2025-12-29

Second Alpha Release

* Repaired fireworks effect, packaged for Window, Mac, and Linux

# [0.1.0] = 2025-12-28

First Alpha Release

* Basic effect generator with a small set of plugins

