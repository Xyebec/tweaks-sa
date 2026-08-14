#pragma once

#include "Vector.h"
#include <algorithm>
#include <numbers>

inline constexpr auto PI = std::numbers::pi_v<float>;
// inline constexpr auto RAD_TO_DEG = 180.0f / PI;
// inline constexpr auto DEG_TO_RAD = PI / 180.0f;
inline constexpr auto FRAC_PI_2 = PI / 2.0f;
inline constexpr auto TAU = PI * 2.0f; // Aka 2pi

template <float Min = 0.0f, float Max = TAU>
constexpr auto NormalizeAngle(float angle) -> float {
    while (angle > Max) { angle -= TAU; }
    while (angle < Min) { angle += TAU; }
    return angle;
}

/// Returns radians from a degree literal
constexpr auto operator""_deg(long double degrees) -> float {
    return static_cast<float>(degrees * (std::numbers::pi / 180.0));
}

constexpr auto ToRadians(float degrees) -> float {
    return degrees * (PI / 180.0f);
}

constexpr auto MoveTowards(float value, float target, float step) -> float {
    return std::max(std::min(value + step, target), value - step);
}

constexpr auto MoveTowards(const CVector2D& from, const CVector2D& to, float step) -> CVector2D {
    const auto diff = to - from;
    const auto distSq = diff.SquaredMagnitude();

    if (distSq <= step * step || distSq == 0.0f) {
        return to;
    }

    const auto dist = std::sqrt(distSq);
    return from + diff / dist * step;
}

constexpr auto MoveTowards(const CVector& from, const CVector& to, float step) -> CVector {
    const auto diff = to - from;
    const auto distSq = diff.SquaredMagnitude();

    if (distSq <= step * step || distSq == 0.0f) {
        return to;
    }

    const auto dist = std::sqrt(distSq);
    return from + diff / dist * step;
}
