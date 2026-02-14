#pragma once

class Config;

namespace definitive_driveby {
    void ReadConfig(const Config& config);
    void Apply();
}
