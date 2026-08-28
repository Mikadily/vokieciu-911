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

}  // namespace

int main()
{
    testTwoNormalGapsAcquirePeriod();
    testMissingGapDuringStartup();
    testRecurringOneOfEightMissingPattern();
    testShortNoiseIsRejected();
    testSmallSpeedChangeIsFiltered();
    testUnexpectedGapForcesReacquisition();

    if (gFailures != 0) {
        std::cerr << gFailures << " test(s) failed\n";
        return EXIT_FAILURE;
    }

    std::cout << "All speed estimator tests passed\n";
    return EXIT_SUCCESS;
}
