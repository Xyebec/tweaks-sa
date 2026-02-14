#pragma once

#include <cmath>
#include <numbers>

inline constexpr auto PI = std::numbers::pi_v<float>;
inline constexpr auto PI2 = PI * 2.0f;

constexpr auto ToRadians(float degrees) -> float {
    return degrees * (PI / 180.0f);
}

constexpr auto NormalizeAngle(float angleRad) -> float {
    angleRad = std::fmod(angleRad, PI2);
    if (angleRad > PI) {
        angleRad -= PI2;
    } else if (angleRad < -PI) {
        angleRad += PI2;
    }

    return angleRad;
}
