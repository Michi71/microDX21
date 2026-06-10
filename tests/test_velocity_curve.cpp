// tests/test_velocity_curve.cpp
//
// Unit test for microdx21::applyVelocityCurve and the velocity-curve
// math in microdx21/velocitycurve.h.
//
// Tests:
//   1. Endpoints preserved (vel=0 → 0, vel=127 → 127) for all curves.
//   2. Monotonicity: output is non-decreasing in input for all curves.
//   3. Linear curve is the identity (vel_out == vel_in for all vel).
//   4. Soft / DX21 / Softest curves reduce low-mid velocities.
//   5. Hard curve boosts low-mid velocities.
//   6. Sample values at standard velocities to document the response.

#include "microdx21/velocitycurve.h"
#include <cstdio>
#include <cstdlib>
#include <cmath>
#include <initializer_list>

using namespace microdx21;

static int failures = 0;
#define EXPECT(cond) do { \
    if (!(cond)) { \
        std::fprintf(stderr, "FAIL: %s @ %s:%d\n", #cond, __FILE__, __LINE__); \
        ++failures; \
    } \
} while (0)

// For each curve, verify that velocity 0 → 0 and 127 → 127.
static void test_endpoints() {
    std::fprintf(stderr, "test endpoints...\n");
    int test_vels[] = { 0, 1, 64, 127, 100 };
    VelocityCurve allCurves[] = { kVelCurveLinear, kVelCurveSoft, kVelCurveHard,
                                       kVelCurveDX21, kVelCurveSoftest };
    for (VelocityCurve c : allCurves) {
        EXPECT(applyVelocityCurve(0, c) == 0);
        EXPECT(applyVelocityCurve(127, c) == 127);
        for (int v : test_vels) {
            int r = applyVelocityCurve(v, c);
            EXPECT(r >= 0 && r <= 127);
        }
    }
}

// Linear curve should be the identity.
static void test_linear_identity() {
    std::fprintf(stderr, "test linear identity...\n");
    for (int v = 0; v <= 127; ++v) {
        EXPECT(applyVelocityCurve(v, kVelCurveLinear) == v);
    }
}

// Output is non-decreasing in input for all curves (monotonicity).
static void test_monotonic() {
    std::fprintf(stderr, "test monotonicity...\n");
    VelocityCurve allCurves2[] = { kVelCurveLinear, kVelCurveSoft, kVelCurveHard,
                                        kVelCurveDX21, kVelCurveSoftest };
    for (VelocityCurve c : allCurves2) {
        for (int v = 1; v <= 127; ++v) {
            int prev = applyVelocityCurve(v - 1, c);
            int cur  = applyVelocityCurve(v, c);
            EXPECT(cur >= prev);
        }
    }
}

// Soft, DX21, Softest should reduce low-mid velocities compared to linear.
static void test_soft_curves_reduce_low() {
    std::fprintf(stderr, "test soft curves reduce low velocities...\n");
        int softTestVels[] = { 16, 32, 48, 64, 80, 96 };
    for (int v : softTestVels) {
        int linear  = applyVelocityCurve(v, kVelCurveLinear);
        int soft    = applyVelocityCurve(v, kVelCurveSoft);
        int dx21    = applyVelocityCurve(v, kVelCurveDX21);
        int softest = applyVelocityCurve(v, kVelCurveSoftest);
        EXPECT(soft    <= linear);
        EXPECT(dx21    <= linear);
        EXPECT(softest <= linear);
        // Softness order: Softest (most) < DX21 < Soft < Linear (least)
        EXPECT(softest <= dx21);
        EXPECT(dx21    <= soft);
    }
}

// Hard should boost low-mid velocities compared to linear.
static void test_hard_curve_boosts_low() {
    std::fprintf(stderr, "test hard curve boosts low velocities...\n");
    int softTestVels[] = { 16, 32, 48, 64, 80, 96 };
    for (int v : softTestVels) {
        int linear = applyVelocityCurve(v, kVelCurveLinear);
        int hard   = applyVelocityCurve(v, kVelCurveHard);
        EXPECT(hard >= linear);
    }
}

// Document the actual response at standard test velocities, for the
// record. This is informational; failures here would mean the math
// changed unexpectedly.
static void test_sample_values() {
    std::fprintf(stderr, "test sample values (informational)...\n");
    // Sample values computed from the curve formula:
    //   vel_out = round((vel_in/127)^p * 127)  with p from velocityCurveExponent
    // Manually computed reference values for verification.
    struct { int v; int linear; int soft; int hard; int dx21; int softest; } samples[] = {
        //   v | linear | soft  | hard  | DX21  | softest
        {   1,   1,     0,      4,      0,      0 },
        {  16,  16,     9,     30,      6,      2 },
        {  32,  32,    21,     48,     16,      8 },
        {  48,  48,    36,     64,     30,     18 },
        {  64,  64,    52,     79,     45,     32 },
        {  80,  80,    70,     92,     63,     50 },
        {  96,  96,    88,    104,     83,     73 },
        { 112, 112,   108,    116,    105,     99 },
    };
    for (auto& s : samples) {
        EXPECT(applyVelocityCurve(s.v, kVelCurveLinear)  == s.linear);
        EXPECT(applyVelocityCurve(s.v, kVelCurveSoft)    == s.soft);
        EXPECT(applyVelocityCurve(s.v, kVelCurveHard)    == s.hard);
        EXPECT(applyVelocityCurve(s.v, kVelCurveDX21)    == s.dx21);
        EXPECT(applyVelocityCurve(s.v, kVelCurveSoftest) == s.softest);
    }
}

// Test the applyVelocityCurveId wrapper with out-of-range IDs (should
// clamp to Linear).
static void test_curve_id_bounds() {
    std::fprintf(stderr, "test curve id bounds...\n");
    for (int id : { -1, -100, 5, 100, 1000 }) {
        // Out of range should fall back to Linear
        EXPECT(applyVelocityCurveId(64, id) == 64);
        EXPECT(applyVelocityCurveId(32, id) == 32);
    }
    for (int id = 0; id < (int)kVelCurveCount; ++id) {
        // In range: should match applyVelocityCurve directly
        EXPECT(applyVelocityCurveId(64, id) == applyVelocityCurve(64, (VelocityCurve)id));
    }
}

int main() {
    test_endpoints();
    test_linear_identity();
    test_monotonic();
    test_soft_curves_reduce_low();
    test_hard_curve_boosts_low();
    test_sample_values();
    test_curve_id_bounds();

    // Print the response table for documentation
    std::fprintf(stderr, "\n=== Velocity curve response (informational) ===\n");
    std::fprintf(stderr, "  v_in | linear | soft  | hard  | DX21  | softest\n");
    std::fprintf(stderr, "  -----+--------+-------+-------+-------+--------\n");
    int printVels[] = { 1, 16, 32, 48, 64, 80, 96, 112, 127 };
    for (int v : printVels) {
        std::fprintf(stderr, "  %4d | %6d | %5d | %5d | %5d | %6d\n",
            v,
            applyVelocityCurve(v, kVelCurveLinear),
            applyVelocityCurve(v, kVelCurveSoft),
            applyVelocityCurve(v, kVelCurveHard),
            applyVelocityCurve(v, kVelCurveDX21),
            applyVelocityCurve(v, kVelCurveSoftest));
    }

    if (failures == 0) {
        std::fprintf(stderr, "\nALL TESTS PASSED\n");
        return 0;
    } else {
        std::fprintf(stderr, "\n%d TEST(S) FAILED\n", failures);
        return 1;
    }
}
