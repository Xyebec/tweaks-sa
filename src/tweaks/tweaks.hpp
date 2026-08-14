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

namespace first_person {
    void ReadConfig(const Config& config);
    void Apply();
}

namespace minor_tweaks {
    void ReadConfig(const Config& config);
    void Apply();
}

namespace transparent_menu {
    void ReadConfig(const Config& config);
    void Apply();
}

inline void ReadConfig(const Config& config) {
    definitive_driveby::ReadConfig(config);
    draw_cols::ReadConfig(config);
    fancy_map::ReadConfig(config);
    first_person::ReadConfig(config);
    minor_tweaks::ReadConfig(config);
    transparent_menu::ReadConfig(config);
}

inline void ApplyTweaks() {
    definitive_driveby::Apply();
    draw_cols::Apply();
    fancy_map::Apply();
    first_person::Apply();
    minor_tweaks::Apply();
    transparent_menu::Apply();
}
