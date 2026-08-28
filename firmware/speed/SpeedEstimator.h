#ifndef SPEED_ESTIMATOR_H
#define SPEED_ESTIMATOR_H

#include <stdint.h>
#include <limits.h>

namespace speedometer {

enum class EdgeResult : uint8_t {
    FirstEdge,
    BounceRejected,
    CandidatePeriod,
    PeriodAccepted,
    MissingPulseGap,
    Discontinuity
};

// Estimates the period of one expected sensor event. Gaps containing up to
// three missing events are divided back to the normal event period.
class SpeedEstimator {
public:
    static constexpr uint32_t kMinimumPeriodUs = 2500UL;
    static constexpr uint32_t kInitialTimeoutUs = 1000000UL;
    static constexpr uint8_t kMaximumGapMultiplier = 4;
    static constexpr uint8_t kTolerancePercent = 20;

    SpeedEstimator() { reset(); }

    void reset()
    {
        haveEdge_ = false;
        haveCandidate_ = false;
        ready_ = false;
        lastEdgeUs_ = 0;
        candidatePeriodUs_ = 0;
        periodUs_ = 0;
        missingPulseCount_ = 0;
    }

    EdgeResult addEdge(const uint32_t edgeUs)
    {
        if (!haveEdge_) {
            haveEdge_ = true;
            lastEdgeUs_ = edgeUs;
            return EdgeResult::FirstEdge;
        }

        const uint32_t gapUs = edgeUs - lastEdgeUs_;
        if (gapUs <= kMinimumPeriodUs) {
            // Do not move lastEdgeUs_: a noise edge must not hide the next
            // legitimate edge.
            return EdgeResult::BounceRejected;
        }

        lastEdgeUs_ = edgeUs;

        if (!ready_) {
            if (gapUs >= kInitialTimeoutUs) {
                beginCandidate(gapUs);
                periodUs_ = 0;
                return EdgeResult::Discontinuity;
            }
            return initializeFromGap(gapUs);
        }

        const uint8_t multiplier = matchingMultiplier(gapUs, periodUs_);
        if (multiplier == 0) {
            beginCandidate(gapUs);
            ready_ = false;
            periodUs_ = 0;
            return EdgeResult::Discontinuity;
        }

        const uint32_t sampleUs = gapUs / multiplier;
        applyFilteredSample(sampleUs);
        missingPulseCount_ += static_cast<uint32_t>(multiplier - 1U);
        return multiplier == 1U ? EdgeResult::PeriodAccepted
                                : EdgeResult::MissingPulseGap;
    }

    bool ready() const { return ready_; }
    bool hasEdge() const { return haveEdge_; }
    uint32_t periodUs() const { return periodUs_; }
    uint32_t lastEdgeUs() const { return lastEdgeUs_; }
    uint32_t missingPulseCount() const { return missingPulseCount_; }

    bool timedOut(const uint32_t nowUs) const
    {
        if (!haveEdge_) {
            return false;
        }

        uint32_t timeoutUs = kInitialTimeoutUs;
        if (ready_) {
            // Permit the configured number of missing events plus one normal
            // period before declaring a stopped sensor at very low speed.
            const uint64_t adaptiveTimeout =
                static_cast<uint64_t>(periodUs_) *
                static_cast<uint64_t>(kMaximumGapMultiplier + 1U);
            if (adaptiveTimeout > timeoutUs) {
                timeoutUs = adaptiveTimeout > UINT32_MAX
                                ? UINT32_MAX
                                : static_cast<uint32_t>(adaptiveTimeout);
            }
        }

        return static_cast<uint32_t>(nowUs - lastEdgeUs_) >= timeoutUs;
    }

private:
    bool haveEdge_;
    bool haveCandidate_;
    bool ready_;
    uint32_t lastEdgeUs_;
    uint32_t candidatePeriodUs_;
    uint32_t periodUs_;
    uint32_t missingPulseCount_;

    static uint32_t absoluteDifference(const uint32_t left,
                                       const uint32_t right)
    {
        return left >= right ? left - right : right - left;
    }

    static bool withinTolerance(const uint32_t value,
                                const uint32_t expected)
    {
        if (expected == 0) {
            return false;
        }

        const uint64_t difference = absoluteDifference(value, expected);
        return difference * 100ULL <=
               static_cast<uint64_t>(expected) * kTolerancePercent;
    }

    static uint8_t matchingMultiplier(const uint32_t gapUs,
                                      const uint32_t basePeriodUs)
    {
        if (basePeriodUs == 0) {
            return 0;
        }

        uint32_t multiplier = static_cast<uint32_t>(
            (static_cast<uint64_t>(gapUs) + basePeriodUs / 2U) /
            basePeriodUs);
        if (multiplier < 1U || multiplier > kMaximumGapMultiplier) {
            return 0;
        }

        const uint32_t expectedUs = static_cast<uint32_t>(
            static_cast<uint64_t>(basePeriodUs) * multiplier);
        return withinTolerance(gapUs, expectedUs)
                   ? static_cast<uint8_t>(multiplier)
                   : 0;
    }

    void beginCandidate(const uint32_t gapUs)
    {
        haveCandidate_ = true;
        candidatePeriodUs_ = gapUs;
    }

    EdgeResult initializeFromGap(const uint32_t gapUs)
    {
        if (!haveCandidate_) {
            beginCandidate(gapUs);
            return EdgeResult::CandidatePeriod;
        }

        const uint32_t smallerUs =
            gapUs < candidatePeriodUs_ ? gapUs : candidatePeriodUs_;
        const uint32_t largerUs =
            gapUs < candidatePeriodUs_ ? candidatePeriodUs_ : gapUs;
        const uint8_t multiplier = matchingMultiplier(largerUs, smallerUs);

        if (multiplier == 0) {
            beginCandidate(gapUs);
            return EdgeResult::Discontinuity;
        }

        const uint32_t normalizedLargerUs = largerUs / multiplier;
        periodUs_ = static_cast<uint32_t>(
            (static_cast<uint64_t>(smallerUs) + normalizedLargerUs + 1U) /
            2U);
        ready_ = true;
        haveCandidate_ = false;
        missingPulseCount_ += static_cast<uint32_t>(multiplier - 1U);
        return multiplier == 1U ? EdgeResult::PeriodAccepted
                                : EdgeResult::MissingPulseGap;
    }

    void applyFilteredSample(const uint32_t sampleUs)
    {
        // A 1/4 IIR update suppresses edge jitter while still tracking gradual
        // acceleration and deceleration.
        if (sampleUs >= periodUs_) {
            periodUs_ += (sampleUs - periodUs_ + 2U) / 4U;
        } else {
            periodUs_ -= (periodUs_ - sampleUs + 2U) / 4U;
        }
    }
};

inline uint32_t calibratedHalfPeriodUs(const uint32_t inputPeriodUs,
                                       const uint16_t frequencyNumerator,
                                       const uint16_t frequencyDenominator)
{
    if (inputPeriodUs == 0 || frequencyNumerator == 0 ||
        frequencyDenominator == 0) {
        return 0;
    }

    const uint64_t divisor =
        static_cast<uint64_t>(frequencyNumerator) * 2ULL;
    const uint64_t scaled =
        static_cast<uint64_t>(inputPeriodUs) * frequencyDenominator;
    const uint64_t rounded = (scaled + divisor / 2ULL) / divisor;

    if (rounded == 0) {
        return 1;
    }
    return rounded > UINT32_MAX ? UINT32_MAX
                                : static_cast<uint32_t>(rounded);
}

}  // namespace speedometer

#endif  // SPEED_ESTIMATOR_H
