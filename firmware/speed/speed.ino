// Classic Porsche electronic speedometer calibrator
//
// Target: Arduino Nano V3 compatible clone, ATmega328P, 16 MHz.
// The USB-C connector/USB-to-serial chip does not affect this firmware.
//
// IMPORTANT: The vehicle sensor input must be conditioned externally. The
// speedometer output must use an electrically appropriate protected driver;
// do not connect an Arduino GPIO to unknown vehicle wiring.

#include <Arduino.h>
#include <util/atomic.h>

#include "SpeedEstimator.h"

#if !defined(__AVR_ATmega328P__)
#error "This timer implementation requires an ATmega328P Nano V3 compatible board."
#endif

#if F_CPU != 16000000UL
#error "This timer configuration requires a 16 MHz CPU clock."
#endif

namespace {

constexpr uint8_t kSensorPin = 3;             // INT1 on ATmega328P
constexpr uint8_t kSpeedometerOutputPin = 7;
constexpr uint8_t kStatusLedPin = LED_BUILTIN;
constexpr uint8_t kIdleOutputLevel = LOW;

// Output frequency = input event frequency * numerator / denominator.
// 91/100 preserves the original sketch's calFactor of 0.91.
constexpr uint16_t kCalibrationNumerator = 91;
constexpr uint16_t kCalibrationDenominator = 100;
static_assert(kCalibrationNumerator > 0, "Calibration numerator must be nonzero");
static_assert(kCalibrationDenominator > 0,
              "Calibration denominator must be nonzero");
static_assert(static_cast<uint32_t>(kCalibrationNumerator) * 2U >=
                  kCalibrationDenominator,
              "Calibration below 0.5x requires explicit review");
static_assert(kCalibrationNumerator <=
                  static_cast<uint32_t>(kCalibrationDenominator) * 2U,
              "Calibration above 2x requires explicit review");

// The external-interrupt ISR only timestamps edges. Foreground code performs
// debounce, period estimation, and missing-pulse recognition.
constexpr uint8_t kEdgeQueueSize = 8;
constexpr uint8_t kEdgeQueueMask = kEdgeQueueSize - 1U;
static_assert((kEdgeQueueSize & kEdgeQueueMask) == 0,
              "Edge queue size must be a power of two");

volatile uint32_t gEdgeQueueUs[kEdgeQueueSize];
volatile uint8_t gEdgeQueueHead = 0;
volatile uint8_t gEdgeQueueTail = 0;
volatile bool gEdgeQueueOverflow = false;

speedometer::SpeedEstimator gEstimator;

// Timer1 runs in CTC mode with a /8 prescaler. At 16 MHz this gives two timer
// ticks per microsecond. Long half-periods are split into bounded chunks.
constexpr uint8_t kTimerTicksPerMicrosecond = 2;
constexpr uint16_t kMaximumTimerChunkTicks = 60000;

volatile bool gOutputEnabled = false;
volatile bool gOutputHigh = false;
volatile uint32_t gRequestedHalfPeriodTicks = 1;
volatile uint32_t gTicksUntilToggle = 1;
volatile uint16_t gScheduledChunkTicks = kMaximumTimerChunkTicks;

void writeOutputAndLed(const uint8_t level)
{
    digitalWrite(kSpeedometerOutputPin, level);
    digitalWrite(kStatusLedPin, level);
}

void scheduleTimerChunk(const uint32_t remainingTicks)
{
    const uint16_t chunkTicks =
        remainingTicks > kMaximumTimerChunkTicks
            ? kMaximumTimerChunkTicks
            : static_cast<uint16_t>(remainingTicks);
    gScheduledChunkTicks = chunkTicks;
    OCR1A = static_cast<uint16_t>(chunkTicks - 1U);
}

void configureOutputTimer()
{
    ATOMIC_BLOCK(ATOMIC_RESTORESTATE)
    {
        TCCR1A = 0;
        TCCR1B = _BV(WGM12) | _BV(CS11);  // CTC, clock / 8
        TCNT1 = 0;
        OCR1A = static_cast<uint16_t>(kMaximumTimerChunkTicks - 1U);
        TIFR1 = _BV(OCF1A);               // Clear any pending compare match.
        TIMSK1 = _BV(OCIE1A);
    }
}

void stopOutput()
{
    ATOMIC_BLOCK(ATOMIC_RESTORESTATE)
    {
        gOutputEnabled = false;
        gOutputHigh = false;
        gTicksUntilToggle = 1;
        TCNT1 = 0;
        scheduleTimerChunk(kMaximumTimerChunkTicks);
        writeOutputAndLed(kIdleOutputLevel);
    }
}

void setOutputHalfPeriodUs(const uint32_t halfPeriodUs)
{
    if (halfPeriodUs == 0 ||
        halfPeriodUs > UINT32_MAX / kTimerTicksPerMicrosecond) {
        stopOutput();
        return;
    }

    const uint32_t halfPeriodTicks =
        halfPeriodUs * kTimerTicksPerMicrosecond;

    ATOMIC_BLOCK(ATOMIC_RESTORESTATE)
    {
        gRequestedHalfPeriodTicks = halfPeriodTicks;
        if (!gOutputEnabled) {
            gOutputHigh = false;
            writeOutputAndLed(kIdleOutputLevel);
            gTicksUntilToggle = halfPeriodTicks;
            TCNT1 = 0;
            scheduleTimerChunk(gTicksUntilToggle);
            gOutputEnabled = true;
        }
    }
}

bool popCapturedEdge(uint32_t &edgeUs)
{
    bool available = false;
    ATOMIC_BLOCK(ATOMIC_RESTORESTATE)
    {
        if (gEdgeQueueTail != gEdgeQueueHead) {
            edgeUs = gEdgeQueueUs[gEdgeQueueTail];
            gEdgeQueueTail =
                static_cast<uint8_t>((gEdgeQueueTail + 1U) & kEdgeQueueMask);
            available = true;
        }
    }
    return available;
}

bool consumeQueueOverflow()
{
    bool overflowed = false;
    ATOMIC_BLOCK(ATOMIC_RESTORESTATE)
    {
        overflowed = gEdgeQueueOverflow;
        gEdgeQueueOverflow = false;
        if (overflowed) {
            gEdgeQueueTail = gEdgeQueueHead;
        }
    }
    return overflowed;
}

void updateOutputFromEstimate()
{
    if (!gEstimator.ready()) {
        stopOutput();
        return;
    }

    const uint32_t halfPeriodUs = speedometer::calibratedHalfPeriodUs(
        gEstimator.periodUs(), kCalibrationNumerator,
        kCalibrationDenominator);
    setOutputHalfPeriodUs(halfPeriodUs);
}

void onSensorFalling()
{
    const uint32_t edgeUs = micros();
    const uint8_t head = gEdgeQueueHead;
    const uint8_t next = static_cast<uint8_t>((head + 1U) & kEdgeQueueMask);

    if (next == gEdgeQueueTail) {
        gEdgeQueueOverflow = true;
        return;
    }

    gEdgeQueueUs[head] = edgeUs;
    gEdgeQueueHead = next;
}

}  // namespace

ISR(TIMER1_COMPA_vect)
{
    if (!gOutputEnabled) {
        scheduleTimerChunk(kMaximumTimerChunkTicks);
        return;
    }

    const uint16_t elapsedTicks = gScheduledChunkTicks;
    if (gTicksUntilToggle > elapsedTicks) {
        gTicksUntilToggle -= elapsedTicks;
        scheduleTimerChunk(gTicksUntilToggle);
        return;
    }

    gOutputHigh = !gOutputHigh;
    writeOutputAndLed(gOutputHigh ? HIGH : LOW);
    gTicksUntilToggle = gRequestedHalfPeriodTicks;
    scheduleTimerChunk(gTicksUntilToggle);
}

void setup()
{
    pinMode(kStatusLedPin, OUTPUT);
    pinMode(kSpeedometerOutputPin, OUTPUT);
    pinMode(kSensorPin, INPUT_PULLUP);
    writeOutputAndLed(kIdleOutputLevel);

    configureOutputTimer();
    attachInterrupt(digitalPinToInterrupt(kSensorPin), onSensorFalling, FALLING);
}

void loop()
{
    if (consumeQueueOverflow()) {
        // Lost timestamps make the period estimate untrustworthy. Return to a
        // known idle state and require a fresh pair of consistent periods.
        gEstimator.reset();
        stopOutput();
    }

    uint32_t edgeUs = 0;
    while (popCapturedEdge(edgeUs)) {
        const speedometer::EdgeResult result = gEstimator.addEdge(edgeUs);
        if (result != speedometer::EdgeResult::BounceRejected) {
            updateOutputFromEstimate();
        }
    }

    const uint32_t nowUs = micros();
    if (gEstimator.timedOut(nowUs)) {
        gEstimator.reset();
        stopOutput();
    }
}
