// microdx21/velocitycurve.h
//
// Velocity-curve shaping for incoming MIDI NoteOn events. The raw MIDI
// velocity (0..127) is reshaped by a power-curve of the form:
//
//   vel_out = round( (vel_in / 127)^p × 127 )
//
// where p is a curve-specific exponent:
//   p = 1.0  → Linear    (passthrough)
//   p = 1.3  → Soft      (slightly convex, low-velocity reduced)
//   p = 0.7  → Hard      (concave, low-velocity boosted)
//   p = 1.5  → DX21      (closest to the original DX21's feel)
//   p = 2.0  → Softest   (piano-like, very low velocities much softer)
//
// The "DX21" curve is an approximation — the real DX21 doesn't expose
// its exact velocity curve in the manual, and the OPP (YM2164) chip's
// velocity response is internal. The power curve with p=1.5 captures
// the convex shape that players associate with the original hardware
// (low notes are noticeably softer, high notes are about the same).
//
// The conversion is lossless at the endpoints (vel=0 → 0, vel=127 → 127)
// and monotonic, so polyphonic chords stay correctly ordered.

#pragma once

#include <cmath>
#include <cstdint>

namespace microdx21 {

enum VelocityCurve : int {
    kVelCurveLinear  = 0,
    kVelCurveSoft    = 1,
    kVelCurveHard    = 2,
    kVelCurveDX21    = 3,
    kVelCurveSoftest = 4,
    kVelCurveCount   = 5,
};

constexpr const char* velocityCurveName(VelocityCurve c) {
    switch (c) {
        case kVelCurveLinear:  return "Linear";
        case kVelCurveSoft:    return "Soft";
        case kVelCurveHard:    return "Hard";
        case kVelCurveDX21:    return "DX21";
        case kVelCurveSoftest: return "Softest";
        case kVelCurveCount:   break;  // sentinel
    }
    return "?";
}

constexpr float velocityCurveExponent(VelocityCurve c) {
    switch (c) {
        case kVelCurveLinear:  return 1.0f;
        case kVelCurveSoft:    return 1.3f;
        case kVelCurveHard:    return 0.7f;
        case kVelCurveDX21:    return 1.5f;
        case kVelCurveSoftest: return 2.0f;
        case kVelCurveCount:   break;  // sentinel
    }
    return 1.0f;
}

// Apply the velocity curve and return the reshaped MIDI velocity (0..127).
// Clamps the output to the valid MIDI range.
inline int applyVelocityCurve(int vel, VelocityCurve curve) {
    if (vel <= 0)   return 0;
    if (vel >= 127) return 127;

    float p = velocityCurveExponent(curve);
    float ratio = static_cast<float>(vel) / 127.0f;
    float curved = std::pow(ratio, p) * 127.0f;
    int result = static_cast<int>(curved + 0.5f);  // round half-up
    if (result < 0)   result = 0;
    if (result > 127) result = 127;
    return result;
}

// Variant that takes the curve as a raw int (matching the existing
// m_VelocityCurve member). Bounds-checks before applying.
inline int applyVelocityCurveId(int vel, int curveId) {
    if (curveId < 0)                    curveId = 0;
    if (curveId >= (int)kVelCurveCount) curveId = 0;
    return applyVelocityCurve(vel, static_cast<VelocityCurve>(curveId));
}

} // namespace microdx21
