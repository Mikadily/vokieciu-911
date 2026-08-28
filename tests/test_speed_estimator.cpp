#include <cstdlib>
#include <iostream>
#include <limits>

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

using speedometer::EdgeResult;
using speedometer::SpeedEstimator;

void testStartupRequiresConsistentPeriods()
{
    SpeedEstimator estimator;
    CHECK(estimator.addEdge(1000) == EdgeResult::FirstEdge);
    CHECK(!estimator.ready());
    CHECK(estimator.addEdge(11000) == EdgeResult::CandidatePeriod);
    CHECK(!estimator.ready());
    CHECK(estimator.addEdge(21000) == EdgeResult::PeriodAccepted);
    CHECK(estimator.ready());
    CHECK(estimator.periodUs() == 10000);
}

void testBounceDoesNotMoveAcceptedEdge()
{
    SpeedEstimator estimator;
    CHECK(estimator.addEdge(1000) == EdgeResult::FirstEdge);
    CHECK(estimator.addEdge(2000) == EdgeResult::BounceRejected);
    CHECK(estimator.addEdge(11000) == EdgeResult::CandidatePeriod);
    CHECK(estimator.addEdge(21000) == EdgeResult::PeriodAccepted);
    CHECK(estimator.periodUs() == 10000);
}

void testSingleMissingPulseIsNormalized()
{
    SpeedEstimator estimator;
    estimator.addEdge(1000);
    estimator.addEdge(11000);
    estimator.addEdge(21000);

    CHECK(estimator.addEdge(41000) == EdgeResult::MissingPulseGap);
    CHECK(estimator.ready());
    CHECK(estimator.periodUs() == 10000);
    CHECK(estimator.missingPulseCount() == 1);
    CHECK(estimator.addEdge(51000) == EdgeResult::PeriodAccepted);
    CHECK(estimator.periodUs() == 10000);
}

void testMultipleMissingPulsesAreNormalized()
{
    SpeedEstimator estimator;
    estimator.addEdge(1000);
    estimator.addEdge(11000);
    estimator.addEdge(21000);

    CHECK(estimator.addEdge(51000) == EdgeResult::MissingPulseGap);
    CHECK(estimator.periodUs() == 10000);
    CHECK(estimator.missingPulseCount() == 2);
}

void testMissingPulseDuringInitialization()
{
    SpeedEstimator estimator;
    estimator.addEdge(1000);
    CHECK(estimator.addEdge(21000) == EdgeResult::CandidatePeriod);
    CHECK(estimator.addEdge(31000) == EdgeResult::MissingPulseGap);
    CHECK(estimator.ready());
    CHECK(estimator.periodUs() == 10000);
    CHECK(estimator.missingPulseCount() == 1);
}

void testGradualAccelerationIsTracked()
{
    SpeedEstimator estimator;
    estimator.addEdge(1000);
    estimator.addEdge(11000);
    estimator.addEdge(21000);

    CHECK(estimator.addEdge(30500) == EdgeResult::PeriodAccepted);
    CHECK(estimator.addEdge(39500) == EdgeResult::PeriodAccepted);
    CHECK(estimator.ready());
    CHECK(estimator.periodUs() < 10000);
    CHECK(estimator.periodUs() > 9000);
}

void testDiscontinuityRequiresReacquisition()
{
    SpeedEstimator estimator;
    estimator.addEdge(1000);
    estimator.addEdge(11000);
    estimator.addEdge(21000);

    CHECK(estimator.addEdge(36000) == EdgeResult::Discontinuity);
    CHECK(!estimator.ready());
    CHECK(estimator.addEdge(51000) == EdgeResult::PeriodAccepted);
    CHECK(estimator.ready());
    CHECK(estimator.periodUs() == 15000);
}

void testMicrosRollover()
{
    SpeedEstimator estimator;
    const uint32_t first = std::numeric_limits<uint32_t>::max() - 4999U;
    CHECK(estimator.addEdge(first) == EdgeResult::FirstEdge);
    CHECK(estimator.addEdge(5000) == EdgeResult::CandidatePeriod);
    CHECK(estimator.addEdge(15000) == EdgeResult::PeriodAccepted);
    CHECK(estimator.periodUs() == 10000);
}

void testTimeout()
{
    SpeedEstimator estimator;
    estimator.addEdge(1000);
    CHECK(!estimator.timedOut(1000000));
    CHECK(estimator.timedOut(1001000));

    estimator.reset();
    estimator.addEdge(1000);
    estimator.addEdge(601000);
    estimator.addEdge(1201000);
    CHECK(estimator.ready());
    CHECK(!estimator.timedOut(4000000));
    CHECK(estimator.timedOut(4201000));
}

void testCalibrationMath()
{
    CHECK(speedometer::calibratedHalfPeriodUs(10000, 1, 1) == 5000);
    CHECK(speedometer::calibratedHalfPeriodUs(10000, 91, 100) == 5495);
    CHECK(speedometer::calibratedHalfPeriodUs(10000, 8, 7) == 4375);
    CHECK(speedometer::calibratedHalfPeriodUs(0, 1, 1) == 0);
    CHECK(speedometer::calibratedHalfPeriodUs(10000, 0, 1) == 0);
}

}  // namespace

int main()
{
    testStartupRequiresConsistentPeriods();
    testBounceDoesNotMoveAcceptedEdge();
    testSingleMissingPulseIsNormalized();
    testMultipleMissingPulsesAreNormalized();
    testMissingPulseDuringInitialization();
    testGradualAccelerationIsTracked();
    testDiscontinuityRequiresReacquisition();
    testMicrosRollover();
    testTimeout();
    testCalibrationMath();

    if (gFailures != 0) {
        std::cerr << gFailures << " test(s) failed\n";
        return EXIT_FAILURE;
    }

    std::cout << "All speed estimator tests passed\n";
    return EXIT_SUCCESS;
}
