# AGENTS.md

## Project at a glance

This repository preserves and adapts an Arduino-based electronic speedometer calibrator for classic Porsche applications. The firmware measures the period of the transmission speed-sensor signal and emits a replacement square wave whose frequency is scaled by a calibration factor.

The archived reference design is specifically for a **1986 Porsche 944 Turbo**. The maintained sketch describes the adaptation more generally as a classic Porsche calibrator. Do not assume that 944 wiring, voltage levels, pulse counts, connector pins, or gauge behavior apply unchanged to every 911 or other model; verify them on the target vehicle.

This is automotive instrumentation code, not a safety-certified product. Bench-test all firmware and interfaces before connecting them to a vehicle.

## Repository contents

- `firmware/speed/speed.ino` — maintained Nano V3/ATmega328P sketch.
- `firmware/speed/SpeedEstimator.h` — platform-independent period estimator, missing-event recognition, and fixed-point calibration math.
- `tests/test_speed_estimator.cpp` — host tests for timing logic.
- `Makefile` — canonical host-test and Arduino build commands.
- `README.md` — user-facing configuration, build, pin, and bootloader guidance.
- `OriginalSpeedometerCalibrator.html` — locally saved 2013 reference article by Tom M'Guinness. Treat it as source/reference material rather than maintained project code.
- `OriginalSpeedometerCalibrator_files/` — images used by the archived article:
  - `New Schematic.png` — original Arduino Nano/LM2940-10/74C14 circuit.
  - `Speed-calibrator-beta.gif` — prototype hardware photograph.
  - `Speedo Hook-Up.gif` and `speedwire.gif` — original 944 installation guidance.
  - `pcb.gif` — original PCB image.
- `1-of-8-magnets-missing.png` — oscilloscope capture showing an irregular pulse train consistent with one missing event among eight. It suggests missing-pulse behavior is under investigation, but the exact target requirements are not yet documented.

## Original design assumptions

The archived article states that:

- The transmission sensor is effectively an open/grounding sensor.
- It produces eight ground pulses per tire rotation on the referenced 944.
- A stock 225/50R16 tire produces roughly 100 pulse cycles per second at 60 mph.
- The input is conditioned by a 74C14 inverting Schmitt trigger.
- Arduino D3 receives the conditioned signal; D7 drives the calibrated output.
- The prototype uses an LM2940-10 regulator to feed an Arduino Nano through `Vin`.
- `calFactor < 1.0` slows the indicated speed, `1.0` passes the frequency unchanged, and `calFactor > 1.0` increases it.

The original firmware listens for falling edges, stores the latest accepted edge-to-edge interval, then toggles the output every `interval / (2 * calFactor)`. This creates an approximately 50% duty-cycle output with frequency `input_frequency * calFactor` when input pulses are uniformly spaced. The maintained redesign below no longer uses that latest-interval polling architecture.

## Current firmware status

The maintained firmware explicitly targets a **16 MHz Arduino Nano V3 compatible clone with an ATmega328P**. It has a reproducible `arduino-cli` build and host-side C++ tests. The USB-C connector and USB/serial bridge do not affect runtime firmware, but uploads may require either the `atmega328old` or `atmega328` Nano bootloader option.

The redesign addresses startup garbage, first-edge measurement, atomic ISR data transfer, numeric interrupt IDs, floating-point timing, stopped-state output, and polling jitter. Sensor edges are placed in a small ISR-owned queue, foreground code validates and estimates their period, and Timer1 produces the output. Queue overflow deliberately invalidates the estimate and returns the output to idle.

Known limitations requiring verification:

1. D3 rejects intervals at or below 2500 microseconds, limiting accepted input events to below about 400 Hz. Confirm vehicle-speed margin.
2. Period changes greater than 20% per accepted edge are treated as discontinuities and force reacquisition.
3. Missing-event recognition supports gaps from one through four normal periods (up to three consecutive missing events).
4. Startup requires two consistent period measurements (normally three sensor edges) before output starts.
5. Timer1 is dedicated to output scheduling; Servo and PWM on D9/D10 are incompatible.
6. D7 idles `LOW`. Confirm this is electrically correct for the protected gauge interface.
7. D13 mirrors every output transition. This is useful on the bench but may be removed if LED current or ISR overhead matters.
8. There is no persistent diagnostic interface or nonvolatile calibration UI.

Unsigned subtraction of `micros()` values provides rollover-safe elapsed times; preserve that property.

## Missing-pulse behavior

`SpeedEstimator` estimates the normal event period and recognizes gaps close to integer multiples of it. A two-period gap is treated as one missing event; gaps up to four periods represent up to three consecutive missing events. Timer1 continues generating the estimated normal cadence through those gaps, so the default behavior reconstructs expected event frequency rather than preserving the observed seven-of-eight average.

Do not additionally set calibration to `8/7` merely because one event is missing. The calibration ratio is for gauge/tire percentage correction after event reconstruction.

This behavior is covered by host tests but still requires real waveform captures across low and high speeds. Confirm whether the target should recreate eight physical positions per revolution or only correct average speed/odometer rate. The estimator is frequency-oriented and does not learn the absolute angular location of a missing magnet.

## Hardware and automotive concerns

Do not connect Arduino pins directly to unknown vehicle wiring. Before implementation, measure the sensor-side and gauge-side signals with suitable high-impedance/protected equipment and establish:

- idle and active voltages;
- polarity;
- source/sink current;
- whether the gauge supplies its own pull-up, and to what voltage;
- expected duty cycle and pulse count per revolution;
- behavior when the calibrator is unpowered.

For a robust design, review:

- input current limiting, Schmitt hysteresis, filtering, and over/undervoltage clamps;
- a protected open-collector/open-drain output that emulates the original sensor instead of directly exposing a GPIO;
- fuse protection, reverse-battery protection, TVS/load-dump suppression, filtering, regulator thermal limits, and correct capacitor ESR/layout;
- decoupling at every IC and defined levels on unused CMOS inputs;
- fail-safe bypass behavior if the calibrator loses power or crashes;
- enclosure, vibration, temperature, moisture, and connector requirements.

The archived LM2940-10 circuit is historical reference, not proof of modern automotive transient compliance.

## Recommended next steps

1. Document the exact vehicle/model/year, sensor type, tire size, observed pulse count, voltage levels, electrical idle state, and desired correction. These remain unknown.
2. Preserve the archived HTML and its image directory unchanged unless deliberately fixing the archive.
3. Expand host tests whenever estimator behavior changes.
4. Add recorded waveform replay tests once captures are available.
5. Bench-test startup, stop, acceleration, noise, missing events, and maximum frequency with a signal generator and oscilloscope.
6. Verify both Nano bootloader variants and record the one used by the physical USB-C clone.
7. Record measured input/output traces and calibration calculations in the repository.

## Working conventions for future agents

- Read this file, `README.md`, `firmware/speed/speed.ino`, `firmware/speed/SpeedEstimator.h`, and the relevant section of `OriginalSpeedometerCalibrator.html` before changing behavior.
- Use clear units in names or comments, for example `periodUs`, `lastEdgeUs`, and `timeoutUs`.
- Prefer fixed-width integer types for timing and explicitly document ISR ownership of shared state.
- Make atomic snapshots of ISR-shared multi-byte values.
- Use named pin constants and `digitalPinToInterrupt()` rather than numeric interrupt IDs.
- Keep hardware assumptions visible in code and documentation; do not silently generalize 944 details to a 911.
- Do not claim vehicle readiness based only on compilation or simulation.
- Do not overwrite provenance/reference assets when creating revised schematics or firmware; add clearly named new files.
- After changes, report the target board, build command, test command, and any hardware assumptions that remain unverified.

## Useful initial commands

```sh
# Inspect repository state and history
git status --short --branch
git log --oneline --decorate -5

# Review firmware with line numbers
nl -ba firmware/speed/speed.ino

# Run host tests and compile for the Nano clone
make test
make arduino

# Locate relevant statements in the archived page
rg -n -i 'sensor|pulse|schmitt|LM2940|calFactor|firmware' \
  OriginalSpeedometerCalibrator.html

# Inventory files
find . -maxdepth 3 -type f -not -path './.git/*' -print | sort
```

Canonical validation is `make test && make arduino`. The default Arduino FQBN is `arduino:avr:nano:cpu=atmega328old`; override `NANO_FQBN` for a clone with the newer Nano bootloader.
