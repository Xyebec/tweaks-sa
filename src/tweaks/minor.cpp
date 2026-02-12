#include "minor.h"
#include "Camera.h"
#include "Pad.h"
#include "config.h"
#include "patcher.h"

static struct MinorTweaksSettings {
    struct Fixes {
        bool fix_zoom_uncrouching;
    } fixes;
    struct Gameplay {
        bool sprint_everywhere;
    } gameplay;
} settings;

void minor_tweaks::ReadConfig(const Config& config) {
    config.Deserialize("tweaks", settings);
}

void minor_tweaks::Apply() {
    if (settings.fixes.fix_zoom_uncrouching) {
        struct Hook {
            static bool __fastcall CPad__GetSprintNo1stPersonAim(CPad* self, uintptr_t /*edx*/) {
                return TheCamera.Using1stPersonWeaponMode() ? false : self->GetSprint();
            }

            static bool __fastcall CPad__JumpJustDownNo1stPersonAim(CPad* self, uintptr_t /*edx*/) {
                return TheCamera.Using1stPersonWeaponMode() ? false : self->JumpJustDown();
            }
        };

        patch::call(0x688009, Hook::CPad__GetSprintNo1stPersonAim); // Zoom out
        patch::call(0x688018, Hook::CPad__JumpJustDownNo1stPersonAim); // Zoom in
    }
    
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
