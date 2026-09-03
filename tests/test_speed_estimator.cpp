#include <cstdlib>
#include <iostream>

#include "SpeedEstimator.h"

namespace {

int gFailures = 0;

void check(const bool condition, const char *expression, const int line)
{
    if (!condition) {
        std::cerr << "line " << line << ": check failed: " << expression
                  << '\n';
        ++gFailures;
    }
}

#define CHECK(expression) check((expression), #expression, __LINE__)

using speedometer::GapResult;
using speedometer::OutputFrequencyLimiter;
using speedometer::SpeedEstimator;

void testTwoNormalGapsAcquirePeriod()
{
    SpeedEstimator estimator;
    CHECK(estimator.addGap(10000) == GapResult::Candidate);
    CHECK(!estimator.ready());
    CHECK(estimator.addGap(10000) == GapResult::Normal);
    CHECK(estimator.ready());
    CHECK(estimator.periodUs() == 10000);
}

void testMissingGapDuringStartup()
{
    SpeedEstimator estimator;
    CHECK(estimator.addGap(20000) == GapResult::Candidate);
    CHECK(estimator.addGap(10000) == GapResult::OneMissing);
    CHECK(estimator.ready());
    CHECK(estimator.periodUs() == 10000);

    estimator.reset();
    CHECK(estimator.addGap(10000) == GapResult::Candidate);
    CHECK(estimator.addGap(20000) == GapResult::OneMissing);
    CHECK(estimator.ready());
    CHECK(estimator.periodUs() == 10000);
}

void testRecurringOneOfEightMissingPattern()
{
    SpeedEstimator estimator;
    const uint32_t gaps[] = {
        10000, 10000, 10000, 10000, 10000, 10000, 20000,
        10000, 10000, 10000, 10000, 10000, 10000, 20000
    };

    unsigned missingGaps = 0;
    for (const uint32_t gapUs : gaps) {
        if (estimator.addGap(gapUs) == GapResult::OneMissing) {
            ++missingGaps;
        }
    }

    CHECK(estimator.ready());
    CHECK(estimator.periodUs() == 10000);
    CHECK(missingGaps == 2);
}

void testShortNoiseIsRejected()
{
    SpeedEstimator estimator;
    CHECK(estimator.addGap(1000) == GapResult::Rejected);
    CHECK(!estimator.ready());
    estimator.addGap(10000);
    estimator.addGap(10000);
    CHECK(estimator.ready());
}

void testSmallSpeedChangeIsFiltered()
{
    SpeedEstimator estimator;
    estimator.addGap(10000);
    estimator.addGap(10000);
    CHECK(estimator.addGap(9600) == GapResult::Normal);
    CHECK(estimator.periodUs() == 9900);
}

void testUnexpectedGapForcesReacquisition()
{
    SpeedEstimator estimator;
    estimator.addGap(10000);
    estimator.addGap(10000);

    CHECK(estimator.addGap(15000) == GapResult::Rejected);
    CHECK(!estimator.ready());
    CHECK(estimator.addGap(15000) == GapResult::Normal);
    CHECK(estimator.ready());
    CHECK(estimator.periodUs() == 15000);
}

void testOutputFrequencyIncreaseIsSlewLimited()
{
    OutputFrequencyLimiter limiter;
    CHECK(limiter.update(10000, 100000) == 10000);

    // The target doubles from 100 Hz to 200 Hz. After 10 ms, the configured
    // 100%/s limit permits only a 1% rise, to 101 Hz.
    CHECK(limiter.update(5000, 110000) == 9901);
    CHECK(limiter.update(5000, 120000) == 9803);
}

void testOutputFrequencyDecreaseIsSlewLimited()
{
    OutputFrequencyLimiter limiter;
    CHECK(limiter.update(10000, 100000) == 10000);

    // A 50 Hz target is likewise reached gradually from 100 Hz.
    CHECK(limiter.update(20000, 110000) == 10101);
}

void testRejectedTimeDoesNotPermitJumpOnReacquisition()
{
    OutputFrequencyLimiter limiter;
    CHECK(limiter.update(10000, 100000) == 10000);
    limiter.hold(500000);

    // Only the 10 ms since hold() contributes to the allowed change.
    CHECK(limiter.update(5000, 510000) == 9901);
}

void testOutputLimiterHandlesMicrosRollover()
{
    OutputFrequencyLimiter limiter;
    CHECK(limiter.update(10000, 0xFFFFFF00UL) == 10000);
    CHECK(limiter.update(5000, 9744) == 9901);
}

void testOutputLimiterResetAllowsFreshStartup()
{
    OutputFrequencyLimiter limiter;
    CHECK(limiter.update(10000, 100000) == 10000);
    CHECK(limiter.update(5000, 110000) == 9901);
    limiter.reset();
    CHECK(limiter.update(20000, 120000) == 20000);
}

}  // namespace

int main()
{
    testTwoNormalGapsAcquirePeriod();
    testMissingGapDuringStartup();
    testRecurringOneOfEightMissingPattern();
    testShortNoiseIsRejected();
    testSmallSpeedChangeIsFiltered();
    testUnexpectedGapForcesReacquisition();
    testOutputFrequencyIncreaseIsSlewLimited();
    testOutputFrequencyDecreaseIsSlewLimited();
    testRejectedTimeDoesNotPermitJumpOnReacquisition();
    testOutputLimiterHandlesMicrosRollover();
    testOutputLimiterResetAllowsFreshStartup();

    if (gFailures != 0) {
        std::cerr << gFailures << " test(s) failed\n";
        return EXIT_FAILURE;
    }

    std::cout << "All speed estimator tests passed\n";
    return EXIT_SUCCESS;
}
