#pragma once

class Config;

namespace fancy_map {
    void ReadConfig(const Config& config);
    void Apply();
}
