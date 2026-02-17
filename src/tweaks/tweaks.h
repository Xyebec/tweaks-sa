#pragma once

class Config;

namespace definitive_driveby {
    void ReadConfig(const Config& config);
    void Apply();
}

namespace draw_cols {
    void ReadConfig(const Config& config);
    void Apply();
}

namespace fancy_map {
    void ReadConfig(const Config& config);
    void Apply();
}

namespace minor_tweaks {
    void ReadConfig(const Config& config);
    void Apply();
}

inline void ReadConfig(const Config& config) {
    definitive_driveby::ReadConfig(config);
    draw_cols::ReadConfig(config);
    fancy_map::ReadConfig(config);
    minor_tweaks::ReadConfig(config);
}

inline void ApplyTweaks() {
    definitive_driveby::Apply();
    draw_cols::Apply();
    fancy_map::Apply();
    minor_tweaks::Apply();
}
