#include "Camera.h"
#include "Enums/eModelID.h"
#include "Vehicle.h"
#include "config.h"
#include "safetyhook/context.hpp"
#include "safetyhook/easy.hpp"
#include "utils.h"

static struct DefinitiveDriveBy {
    bool enabled;
    float angle_front;
    float angle_back;
} settings;

static void Hook_DefinitiveDriveBy(safetyhook::Context& ctx) {
    auto* vehicle = *reinterpret_cast<CVehicle**>(ctx.esp + 0x34 - 0x10);
    
    // Do nothing if vehicle has stuff bound to shooting button
    if (vehicle->m_nModelIndex == MODEL_PREDATOR
        || vehicle->m_nModelIndex == MODEL_FIRETRUK
        || vehicle->m_nModelIndex == MODEL_SWATVAN)
    {
        return;
    }
    
    const auto isShooting = *reinterpret_cast<bool*>(ctx.esp + 0x34 - 0x22);
    if (!isShooting) {
        return;
    }

    const auto angle = NormalizeAngle<-PI, PI>(vehicle->GetHeading() - TheCamera.GetHeading());

    const auto minL = -90.0_deg - settings.angle_back;
    const auto maxL = -90.0_deg + settings.angle_front;

    const auto minR = 90.0_deg - settings.angle_front;
    const auto maxR = 90.0_deg + settings.angle_back;

    if (angle >= minL && angle <= maxL) {
        *reinterpret_cast<bool*>(ctx.esp + 0x34 - 0x1C) = true; // Left
    } else if (angle >= minR && angle <= maxR) {
        *reinterpret_cast<bool*>(ctx.esp + 0x34 - 0x20) = true; // Right
    }
}

namespace definitive_driveby {

extern void ReadConfig(const Config& config) {
    config.Deserialize("definitive-drive-by", settings);
}

extern void Apply() {
    if (!settings.enabled) {
        return;
    }

    settings.angle_front = ToRadians(settings.angle_front);
    settings.angle_back = ToRadians(settings.angle_back);

    static auto hook = safetyhook::create_mid(0x742120, Hook_DefinitiveDriveBy);
}

}
