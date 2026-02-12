#include "minor.h"
#include "Bike.h"
#include "Camera.h"
#include "CarAI.h"
#include "EntryExit.h"
#include "Game.h"
#include "Pad.h"
#include "Timer.h"
#include "common.h"
#include "config.h"
#include "patcher.h"
#include "safetyhook/context.hpp"
#include "safetyhook/easy.hpp"

static struct MinorTweaksSettings {
    struct Fixes {
        bool dont_cull_world_on_enex;
        bool fix_car_generator_blockage;
        bool fix_police_bike_siren;
        bool fix_zoom_uncrouching;
        bool ganton_garage_four_slots;
    } fixes;
    struct Gameplay {
        bool sprint_everywhere;
    } gameplay;
} settings;

void minor_tweaks::ReadConfig(const Config& config) {
    config.Deserialize("tweaks", settings);
}

void minor_tweaks::Apply() {
    if (settings.fixes.dont_cull_world_on_enex) {
        // Disable `CGame::currArea = CEntryExit::ms_spawnPoint->m_nArea`
        // for `CEntryExitManager::ms_exitEnterState == 0`
        patch::nop(0x440601, 6);
        // Set area code later
        static auto hook = safetyhook::create_mid(0x4406AD, [](safetyhook::Context& /*ctx*/) {
            CGame::currArea = CEntryExit::ms_spawnPoint->m_nArea;
        });
    }
    
    // TODO: figure out a better approach
    if (settings.fixes.fix_car_generator_blockage) {
        static auto hook = safetyhook::create_mid(0x6F32FD, [](safetyhook::Context& ctx) {
            auto *const radius = reinterpret_cast<float*>(ctx.ecx + 0x24);
            *reinterpret_cast<float*>(ctx.esp + 0x48 - 0x38) = *radius * 0.05f;
            ctx.eip = 0x6F3304;
        });
    }

    if (settings.fixes.fix_police_bike_siren) {
        static auto hook = safetyhook::create_mid(0x6BBC18, [](safetyhook::Context& ctx) {
            auto* self = reinterpret_cast<CBike*>(ctx.esi);
        
            // Code taken from `CAutomobile::ProcessControl` (0x6B2BB1)
            if (self->bSirenOrAlarm
                && (CTimer::m_FrameCounter & 7) == 5
                && self->UsesSiren()
                // && self->m_nModelIndex != MODEL_MRWHOOP
                && FindPlayerVehicle(-1, false) == self
            ) {
                CCarAI::MakeWayForCarWithSiren(self);
            }
        });
    }

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
    
    if (settings.fixes.ganton_garage_four_slots) {
        patch::copy_slice(0x44BF1D, { 0xB1, 0x01, 0x90 }); // mov cl, 1 ; nop
        patch::copy_slice(0x44BD90, { 0xB0, 0x01, 0x90 }); // mov al, 1 ; nop
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
