#ifndef SPEED_ESTIMATOR_H
#define SPEED_ESTIMATOR_H

#include <stdint.h>

namespace speedometer {

enum class GapResult : uint8_t {
    Candidate,
    Normal,
    OneMissing,
    Rejected
};

// Learns one normal sensor period and ignores a gap caused by one missing
// magnet. This intentionally handles only T and 2T gaps.
class SpeedEstimator {
public:
    static constexpr uint32_t kMinimumGapUs = 2500UL;
    static constexpr uint8_t kTolerancePercent = 20;

    SpeedEstimator() { reset(); }

    void reset()
    {
        haveCandidate_ = false;
        ready_ = false;
        candidateUs_ = 0;
        periodUs_ = 0;
    }

    GapResult addGap(const uint32_t gapUs)
    {
        if (gapUs <= kMinimumGapUs) {
            return GapResult::Rejected;
        }

        if (!ready_) {
            return acquire(gapUs);
        }

        if (near(gapUs, periodUs_)) {
            // Small IIR filter: follow real speed changes without copying every
            // few-microsecond edge variation to the output.
            if (gapUs >= periodUs_) {
                periodUs_ += (gapUs - periodUs_ + 2U) / 4U;
            } else {
                periodUs_ -= (periodUs_ - gapUs + 2U) / 4U;
            }
            return GapResult::Normal;
        }

        if (nearDouble(gapUs, periodUs_)) {
            // One magnet is missing. Keep the learned normal period; the output
            // generator continues through this doubled input gap.
            return GapResult::OneMissing;
        }

        // The signal no longer resembles T or 2T. Stop trusting the old speed
        // and use this gap as the first sample for reacquisition.
        ready_ = false;
        candidateUs_ = gapUs;
        haveCandidate_ = true;
        return GapResult::Rejected;
    }

    bool ready() const { return ready_; }
    uint32_t periodUs() const { return periodUs_; }

private:
    bool haveCandidate_;
    bool ready_;
    uint32_t candidateUs_;
    uint32_t periodUs_;

    static uint32_t difference(const uint32_t left, const uint32_t right)
    {
        return left >= right ? left - right : right - left;
    }

    static bool near(const uint32_t value, const uint32_t expected)
    {
        if (expected == 0) {
            return false;
        }
        return static_cast<uint64_t>(difference(value, expected)) * 100ULL <=
               static_cast<uint64_t>(expected) * kTolerancePercent;
    }

    static bool nearDouble(const uint32_t value, const uint32_t base)
    {
        const uint64_t expected = static_cast<uint64_t>(base) * 2ULL;
        const uint64_t difference =
            value >= expected ? static_cast<uint64_t>(value) - expected
                              : expected - value;
        return expected != 0 &&
               difference * 100ULL <= expected * kTolerancePercent;
    }

    GapResult acquire(const uint32_t gapUs)
    {
        if (!haveCandidate_) {
            candidateUs_ = gapUs;
            haveCandidate_ = true;
            return GapResult::Candidate;
        }

        const uint32_t shorterUs =
            gapUs < candidateUs_ ? gapUs : candidateUs_;
        const uint32_t longerUs =
            gapUs < candidateUs_ ? candidateUs_ : gapUs;

        if (near(longerUs, shorterUs)) {
            periodUs_ = static_cast<uint32_t>(
                (static_cast<uint64_t>(shorterUs) + longerUs + 1U) / 2U);
            ready_ = true;
            haveCandidate_ = false;
            return GapResult::Normal;
        }

        if (nearDouble(longerUs, shorterUs)) {
            const uint32_t normalizedLongerUs = longerUs / 2U;
            periodUs_ = static_cast<uint32_t>(
                (static_cast<uint64_t>(shorterUs) + normalizedLongerUs + 1U) /
                2U);
            ready_ = true;
            haveCandidate_ = false;
            return GapResult::OneMissing;
        }

        candidateUs_ = gapUs;
        return GapResult::Rejected;
    }
};

}  // namespace speedometer

#endif  // SPEED_ESTIMATOR_H
