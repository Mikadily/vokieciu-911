# AGENTS.md

## Purpose

This repository contains a deliberately minimal Arduino repair for the waveform in `1-of-8-magnets-missing.png`. The sensor is assumed to have eight evenly spaced events per revolution, with one physical magnet absent. The seven observed pulses create six normal falling-edge gaps near `T` and one recurring gap near `2T` per revolution.

The maintained firmware learns `T` and emits a continuous 50% duty-cycle output at period `T`. It does not attempt general speedometer calibration or arbitrary missing-tooth decoding.

The archived source design is for a 1986 Porsche 944 Turbo. Do not silently apply its wiring, pulse count, or voltage assumptions to a 911 or another model. The exact target vehicle remains undocumented.

## Maintained files

- `firmware/speed/speed.ino` — Nano V3 sketch and input/output handling.
- `firmware/speed/SpeedEstimator.h` — pure C++ estimator for `T` and `2T` gaps.
- `tests/test_speed_estimator.cpp` — host tests for the estimator.
- `Makefile` — canonical test and Arduino build commands.
- `README.md` — behavior, pinout, build instructions, and limitations.
- `1-of-8-magnets-missing.png` — reference oscilloscope waveform.
- `OriginalSpeedometerCalibrator.html` and `OriginalSpeedometerCalibrator_files/` — preserved historical reference material; avoid modifying it.

## Target

- Arduino Nano V3 compatible clone
- ATmega328P
- 16 MHz
- USB-C connector; USB/serial bridge and bootloader variant are not yet identified

The default build FQBN is `arduino:avr:nano:cpu=atmega328old`. A newer bootloader can be selected with `NANO_FQBN=arduino:avr:nano:cpu=atmega328`. Bootloader choice affects upload protocol, not application timing.

## Algorithm

1. A falling-edge ISR on D3 captures edge-to-edge time using `micros()`.
2. Gaps at or below 2500 microseconds are ignored as noise.
3. Foreground code atomically takes the latest valid gap.
4. Two compatible startup gaps establish `T`:
   - two similar gaps are averaged; or
   - if one is near twice the other, the shorter value is `T`.
5. Later gaps near `T` update the period through a 1/4 fixed-point IIR filter.
6. Gaps near `2T` are identified as the one missing magnet and do not alter `T`.
7. Any gap matching neither `T` nor `2T` disables output and starts reacquisition.
8. Foreground code emits a 50% duty-cycle D7 signal at period `T`.
9. One second without a valid input edge resets acquisition and forces D7 `LOW`.

Comparisons use a 20% tolerance. Timing subtraction is unsigned so `micros()` rollover is handled correctly. Output due-time comparison uses signed modular subtraction and is valid because scheduled intervals are far below 2^31 microseconds.

## Intentional non-features

Do not add these without a concrete requirement and measured waveform:

- the original `0.91` percentage calibration;
- an automatic `8/7` multiplier;
- multiple consecutive missing-event handling;
- Timer1 register ownership;
- interrupt queues;
- floating-point timing;
- serial menus, trim pots, or persistent settings;
- broad cross-board abstraction.

Missing-event repair already restores the expected eight-event cadence. Applying `8/7` on top would over-correct it.

## Known limitations

- The ISR-to-loop mailbox stores only the latest gap. This is acceptable only because `loop()` is non-blocking and accepted edges are at least 2.5 ms apart. Do not add blocking work to `loop()` without replacing the mailbox.
- Polling generates D7 transitions. With the intentionally empty/non-blocking loop this should have small jitter, but it must be measured over the full input-frequency range.
- The estimator handles only isolated `2T` gaps. Two or more adjacent missing magnets are intentionally unsupported.
- A period change outside the 20% acceptance window temporarily stops output while two new gaps are acquired.
- Startup normally requires three falling edges: one reference edge and two measured gaps.
- The fixed one-second timeout means extremely slow valid inputs are unsupported.
- D7 idles `LOW`; the correct electrical idle state remains unverified.
- There is no phase lock to a particular wheel angle. The output repairs cadence, not absolute missing-magnet position.

## Hardware safety

Do not connect Arduino pins directly to unknown automotive wiring. Verify sensor-side and gauge-side idle voltage, active voltage, polarity, pull-ups, and current.

The historical schematic uses a 74C14 input conditioner and LM2940-10 supply. It is reference material, not proof of load-dump, reverse-battery, thermal, or EMC compliance. A real installation should review input clamps/current limiting, a protected open-collector/open-drain output, fuse and reverse-polarity protection, TVS/load-dump suppression, decoupling, fail-safe behavior, enclosure, vibration, and temperature.

## Validation

Run before submitting firmware changes:

```sh
make test
make arduino
make arduino NANO_FQBN=arduino:avr:nano:cpu=atmega328
git diff --check
```

Tests currently cover:

- acquisition from two normal gaps;
- acquisition when startup crosses the missing position;
- recurring one-of-eight `T, T, T, T, T, T, 2T` behavior;
- rejection of short noise;
- filtered small speed changes;
- reacquisition after an unexpected gap.

Host tests cannot validate ISR latency, output jitter, electrical polarity, or vehicle behavior. Bench-test with a signal generator and oscilloscope before installation. A useful simulator trace must actually include a recurring `2T` same-direction edge interval; a uniform 100 Hz trace tests only normal operation.

## Conventions

- Keep the solution focused on exactly one isolated missing magnet.
- Keep ISRs short; no floating point, logging, or unnecessary GPIO reads in an ISR.
- Use fixed-width types and explicit microsecond units.
- Atomically access multi-byte ISR-shared values on the 8-bit AVR.
- Preserve unsigned elapsed-time arithmetic for rollover safety.
- Keep `loop()` non-blocking while the single-value ISR mailbox is used.
- Preserve archived HTML and images unchanged.
- Document measured facts separately from assumptions.
- Never claim vehicle readiness from compilation or simulation alone.

## Useful commands

```sh
git status --short --branch
git log --oneline --decorate -5
nl -ba firmware/speed/speed.ino
make test
make arduino
```
