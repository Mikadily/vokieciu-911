// Repair a speedometer signal with one missing magnet out of eight.
// Target: 16 MHz Arduino Nano V3 / ATmega328P compatible clone.
//
// D3 must receive a clean logic signal from the external Schmitt trigger.
// D7 must drive the speedometer through an electrically suitable interface.

#include <Arduino.h>
#include <util/atomic.h>

#include "SpeedEstimator.h"

namespace {

constexpr uint8_t kSensorPin = 3;
constexpr uint8_t kOutputPin = 7;
constexpr uint32_t kSignalTimeoutUs = 1000000UL;
constexpr uint8_t kIdleLevel = LOW;

// ISR-owned input state. The foreground loop copies it atomically.
volatile bool gHaveInputEdge = false;
volatile bool gNewGap = false;
volatile uint32_t gLastInputEdgeUs = 0;
volatile uint32_t gLatestGapUs = 0;

speedometer::SpeedEstimator gEstimator;

bool gOutputRunning = false;
bool gOutputHigh = false;
uint32_t gHalfPeriodUs = 0;
uint32_t gNextToggleUs = 0;

void stopOutput()
{
    gOutputRunning = false;
    gOutputHigh = false;
    digitalWrite(kOutputPin, kIdleLevel);
}

void startOrUpdateOutput(const uint32_t periodUs)
{
    const uint32_t halfPeriodUs = periodUs / 2U;
    if (halfPeriodUs == 0) {
        stopOutput();
        return;
    }

    gHalfPeriodUs = halfPeriodUs;
    if (!gOutputRunning) {
        gOutputHigh = false;
        digitalWrite(kOutputPin, kIdleLevel);
        gNextToggleUs = micros() + gHalfPeriodUs;
        gOutputRunning = true;
    }
}

void updateOutput(const uint32_t nowUs)
{
    if (!gOutputRunning ||
        static_cast<int32_t>(nowUs - gNextToggleUs) < 0) {
        return;
    }

    // Normally one step is due. Accounting for more than one avoids a burst of
    // late transitions if an interrupt briefly delayed loop().
    const uint32_t steps =
        static_cast<uint32_t>(nowUs - gNextToggleUs) / gHalfPeriodUs + 1U;
    gNextToggleUs += steps * gHalfPeriodUs;
    if ((steps & 1U) != 0U) {
        gOutputHigh = !gOutputHigh;
        digitalWrite(kOutputPin, gOutputHigh ? HIGH : LOW);
    }
}

void resetAfterTimeout()
{
    ATOMIC_BLOCK(ATOMIC_RESTORESTATE)
    {
        gHaveInputEdge = false;
        gNewGap = false;
    }
    gEstimator.reset();
    stopOutput();
}

void onSensorFalling()
{
    // Guard: some clones fire on both edges despite FALLING mode.
    // After a true falling edge the pin reads LOW; skip if HIGH.
    if (PIND & _BV(PIND3)) return;

    const uint32_t nowUs = micros();
    if (!gHaveInputEdge) {
        gLastInputEdgeUs = nowUs;
        gHaveInputEdge = true;
        return;
    }

    const uint32_t gapUs = nowUs - gLastInputEdgeUs;
    if (gapUs <= speedometer::SpeedEstimator::kMinimumGapUs) {
        return;
    }

    gLastInputEdgeUs = nowUs;
    gLatestGapUs = gapUs;
    gNewGap = true;
}

}  // namespace

void setup()
{
    pinMode(kOutputPin, OUTPUT);
    pinMode(kSensorPin, INPUT_PULLUP);
    digitalWrite(kOutputPin, kIdleLevel);
    attachInterrupt(digitalPinToInterrupt(kSensorPin), onSensorFalling, FALLING);
}

void loop()
{
    bool haveInputEdge = false;
    bool haveNewGap = false;
    uint32_t lastInputEdgeUs = 0;
    uint32_t gapUs = 0;

    ATOMIC_BLOCK(ATOMIC_RESTORESTATE)
    {
        haveInputEdge = gHaveInputEdge;
        lastInputEdgeUs = gLastInputEdgeUs;
        haveNewGap = gNewGap;
        if (haveNewGap) {
            gapUs = gLatestGapUs;
            gNewGap = false;
        }
    }

    if (haveNewGap) {
        gEstimator.addGap(gapUs);
        if (gEstimator.ready()) {
            startOrUpdateOutput(gEstimator.periodUs());
        } else {
            stopOutput();
        }
    }

    const uint32_t nowUs = micros();
    if (haveInputEdge &&
        static_cast<uint32_t>(nowUs - lastInputEdgeUs) >= kSignalTimeoutUs) {
        resetAfterTimeout();
        return;
    }

    updateOutput(nowUs);
}
