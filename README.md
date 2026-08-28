# Classic Porsche speedometer calibrator

Arduino firmware that conditions the timing of a classic Porsche electronic speedometer signal. The maintained implementation targets a **16 MHz Arduino Nano V3 / ATmega328P-compatible clone**. The repository also preserves the original 2013 Porsche 944 reference article and images.

> **Not vehicle-ready by compilation alone.** Verify all voltages and pulse behavior on the target vehicle, use protected automotive input/output interfaces, and bench-test with a signal generator and oscilloscope before installation.

## Firmware behavior

The sketch:

- timestamps falling sensor edges on D3;
- rejects edges separated by 2.5 ms or less;
- requires two consistent measured periods before enabling output;
- estimates the normal event period with a fixed-point IIR filter;
- recognizes gaps containing up to three missing sensor events;
- generates a calibrated 50% duty-cycle output on D7 using Timer1;
- returns D7 and the built-in LED to `LOW` after signal timeout or queue overflow;
- handles `micros()` rollover using unsigned subtraction.

The default frequency ratio is `91/100`, matching the original `calFactor = 0.91`. Change `kCalibrationNumerator` and `kCalibrationDenominator` in `firmware/speed/speed.ino` only after measuring the required correction. Missing-event reconstruction already restores the normal event cadence; do not automatically multiply the calibration by `8/7` for one missing magnet.

## Pin and timer use

| Resource | Use |
| --- | --- |
| D3 / INT1 | Conditioned sensor input, falling edge |
| D7 | Calibrated speedometer output |
| D13 / `LED_BUILTIN` | Mirrors output for bench observation |
| Timer1 | Deterministic output scheduling |

Timer1 ownership means libraries/features using Timer1, including Servo and PWM on D9/D10, are incompatible with this sketch.

`INPUT_PULLUP` is enabled on D3, but the expected input remains the clean logic-level output of an external Schmitt-trigger stage. D7 must feed an electrically appropriate protected output stage; it is not proof that direct connection to a gauge is safe.

## Build and test

Requirements:

- `g++` with C++11 support
- `arduino-cli`
- Arduino AVR core (`arduino:avr`)

```sh
make test
make arduino
```

The default board identifier uses the bootloader commonly found on older Nano clones:

```text
arduino:avr:nano:cpu=atmega328old
```

The bootloader selection changes upload protocol, not the generated ATmega328P application logic. If this USB-C clone uses the newer Nano bootloader, build/upload with:

```sh
make arduino NANO_FQBN=arduino:avr:nano:cpu=atmega328
arduino-cli upload -p /dev/ttyUSB0 \
  --fqbn arduino:avr:nano:cpu=atmega328 firmware/speed
```

Use the actual serial device and bootloader variant for the board. USB-C does not itself identify which bootloader is installed.

## Missing information to verify

Before vehicle installation, record:

- exact Porsche model and year;
- sensor type and expected events per wheel/transmission revolution;
- input waveform, idle/active voltage, polarity, and maximum frequency;
- gauge input voltage, pull-up, and current requirements;
- desired behavior for the captured one-of-eight missing event;
- calibrated ratio derived from GPS or a traceable speed reference;
- correct electrical idle output level;
- behavior when the calibrator is unpowered.

See `AGENTS.md` for implementation details, known limitations, and automotive hardware cautions.
