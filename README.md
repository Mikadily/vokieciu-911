# One-missing-magnet speedometer repair

Minimal Arduino firmware for the waveform shown in `1-of-8-magnets-missing.png`. It targets a **16 MHz Arduino Nano V3 / ATmega328P-compatible clone**.

The measured input is an active-high waveform with one high pulse absent at every eighth expected magnet position. Its rising edges produce six normal intervals of approximately `T` and one interval of approximately `2T` per revolution. The sketch learns `T` and emits a continuous active-high 50% duty-cycle signal with period `T`, thereby filling the missing event.

> Compilation does not make the circuit vehicle-safe. Use a conditioned logic-level input and a protected output interface, then verify the result on a bench before installation.

## Deliberately limited behavior

The implementation only handles:

- normal gaps close to `T`;
- one missing magnet represented by a gap close to `2T`;
- input edges more than 2.5 ms apart;
- loss of signal, with a one-second timeout.

It does not support percentage calibration, multiple consecutive missing magnets, arbitrary pulse multiplication, serial configuration, or Timer1-specific hardware. Keeping those features out makes the failure modes easier to understand.

Startup uses two measured gaps. If either startup gap crosses the missing position, the shorter gap is recognized as `T`. Output then free-runs at `T`; a `2T` input gap is ignored, so output pulses continue through the missing position. Normal gaps update `T` through a small fixed-point filter.

A second, independent limiter prevents the generated frequency from changing instantaneously even if the estimator makes a large change after reacquisition. D7 may move toward the input-derived target by at most 100% of its current frequency per second. This limit is based on elapsed microseconds, so it does not become weaker at higher pulse rates. An unrecognized gap still stops D7, but reacquisition resumes near the last trusted output frequency and approaches the new target gradually. A one-second signal timeout clears both the estimate and the retained output frequency, allowing a fresh startup.

## Pins

| Pin | Purpose |
| --- | --- |
| D3 | Rising-edge input for active-high pulses from an external Schmitt trigger |
| D7 | Repaired active-high 50% duty-cycle output |

The conditioned D3 waveform must be actively driven and idle `LOW`; the sketch therefore uses `INPUT` without the internal pull-up. D7 also idles `LOW` after startup, timeout, or an unrecognized period change. Verify that these electrical choices match the real conditioning and gauge interfaces.

## Build and test

Requirements are `g++`, `arduino-cli`, and the `arduino:avr` core.

```sh
make test
make arduino
```

The default build uses the old Nano bootloader commonly found on clones:

```text
arduino:avr:nano:cpu=atmega328old
```

For a board with the newer bootloader:

```sh
make arduino NANO_FQBN=arduino:avr:nano:cpu=atmega328
```

USB-C does not identify the installed bootloader; it only describes the connector.

## What must still be measured

Before installing it, verify:

- the exact vehicle, sensor, and expected eight-event pattern;
- that the conditioned rising-edge gaps really consist of normal `T` gaps and one recurring `2T` gap;
- minimum and maximum `T` across the vehicle speed range;
- D3 voltage levels and polarity;
- D7 interface voltage, current, and correct idle level;
- behavior when the Arduino is unpowered;
- output timing with a signal generator and oscilloscope;
- that the 100%/second output-frequency slew limit follows the fastest real wheel acceleration without unacceptable gauge lag.
