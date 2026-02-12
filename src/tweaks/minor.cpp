#include "minor.h"
#include "config.h"
#include "patcher.h"

static struct MinorTweaksSettings {
    struct Gameplay {
        bool sprint_everywhere;
    } gameplay;
} settings;

void minor_tweaks::ReadConfig(const Config& config) {
    config.Deserialize("tweaks", settings);
}

void minor_tweaks::Apply() {
    if (settings.gameplay.sprint_everywhere) {
        // Make `SurfaceInfos_c::CantSprintOn` always return `false`
        patch::copy_slice(0x55E870, { 0x31, 0xC0, 0xC2, 0x04, 0x00 }); // xor eax, eax ; ret 4

        // Remove `m_bPlayerSprintDisabled` check for the `CPlayerPed::ControlButtonSprint`
        patch::nop(0x60A667, 2);
        // Remove `m_bPlayerSprintDisabled` check for the `CTaskSimplePlayerOnFoot::PlayerControlZelda`
        patch::nop(0x6885FA, 2);
        // Remove `m_bPlayerSprintDisabled` check for the `CTaskSimpleGoToPoint::ProcessPed`
        patch::nop(0x66D93C, 2);

        // Allow sprint on entities (trains)
        patch::set<uint8_t>(0x6885A5, 0xEB);
    }
}
