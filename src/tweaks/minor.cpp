#include "Bike.h"
#include "Camera.h"
#include "CarAI.h"
#include "EntryExit.h"
#include "Game.h"
#include "Pad.h"
#include "Ped.h"
#include "Timer.h"
#include "common.h"
#include "config.h"
#include "patcher.h"
#include "safetyhook/context.hpp"
#include "safetyhook/easy.hpp"

static struct MinorTweaks {
    struct Fixes {
        bool dont_cull_world_on_enex;
        bool fix_car_generator_blockage;
        bool fix_police_bike_siren;
        bool fix_zoom_uncrouching;
        bool ganton_garage_four_slots;
        bool scorched_cj;
    } fixes;
    struct Gameplay {
        bool climb_everywhere;
        bool disable_extra_air_resistance;
        bool duck_with_any_weapon;
        bool jump_with_heavy_weapons;
        bool sprint_everywhere;
        bool always_warp_gang_with_player;
        struct ClimbableVehicles {
            bool enabled;
            bool ignore_col_spheres;
            bool allow_vaulting;
        } climbable_vehicles;
    } gameplay;
    struct Hud {
        bool always_show_ammo;
    } hud;
    struct Visuals {
        bool disable_heat_haze;
        bool disable_speed_blur;
    } visuals;
} settings;

namespace minor_tweaks {

extern void ReadConfig(const Config& config) {
    config.Deserialize("tweaks", settings);
}

extern void Apply() {
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
    
    if (settings.fixes.scorched_cj) {
        // Skip `!CPed::IsPlayer` check
        patch::nop(0x53A669, 6);
    }

    if (settings.gameplay.climb_everywhere) {
        // Skip `!CGame::currArea` check in the `CTaskSimpleJump::ProcessPed`
        patch::nop(0x680CA4, 2);
        // Skip `!CGame::currArea` check in the `CTaskSimpleInAir::ProcessPed`
        patch::nop(0x680AC4, 6);
    }

    if (settings.gameplay.disable_extra_air_resistance) {
        patch::copy_slice(0x72DDD0, { 0x31, 0xC0, 0xC3 }); // xor eax, eax ; ret
    }

    if (settings.gameplay.duck_with_any_weapon) {
        // Skip all weapon-related checks
        patch::set<uint8_t>(0x692651, 0xEB);
    }
    
    if (settings.gameplay.jump_with_heavy_weapons) {
        // Remove `CWeaponInfo.flags.bHeavy` check
        patch::nop(0x6886F8, 6);
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
    
    if (settings.gameplay.always_warp_gang_with_player) {
        // Unconditionally call `CEntryExit::WarpGangWithPlayer`
        patch::nop(0x440A1C, 2);
    }
    
    // TODO: rearrange OG code so it checks both vehicles and objects
    if (settings.gameplay.climbable_vehicles.enabled) {
        // Skip vehicle type checks in `CTaskSimpleClimb::ScanToGrabSectorList`
        patch::jmp_short(0x67DF84, 0x67DFB4);

        // Do not abort `CTaskSimpleClimb::ProcessPed` on moving trains
        patch::set<uint8_t>(0x680E46, 0xEB);

        if (settings.gameplay.climbable_vehicles.ignore_col_spheres) {
            // Ignore collision spheres for other vehicle types too
            patch::nop(0x67E027, 2);
        }

        // Disable vehicle-to-ped collision
        static auto hook1 = safetyhook::create_mid(0x680E86, [](safetyhook::Context& ctx) {
            auto* ped = reinterpret_cast<CPed*>(ctx.edi);
            auto* vehicle = reinterpret_cast<CVehicle*>(ctx.ecx);

            if (!vehicle->m_pEntityIgnoredCollision) {
                vehicle->m_pEntityIgnoredCollision = ped;
            }
        });

        // Re-enable vehicle-to-ped collision
        static constexpr auto ReenableCollision = [](CPed* ped) {
            auto* vehicle = static_cast<CVehicle*>(ped->m_pEntityIgnoredCollision);

            if (vehicle->m_pEntityIgnoredCollision == ped) {
                vehicle->m_pEntityIgnoredCollision = nullptr;
            }
        };

        static auto hook2 = safetyhook::create_mid(0x680DFE, [](safetyhook::Context& ctx) {
            auto* ped = reinterpret_cast<CPed*>(ctx.eax);
            ReenableCollision(ped);
        });
        static auto hook3 = safetyhook::create_mid(0x6814CE, [](safetyhook::Context& ctx) {
            auto* ped = reinterpret_cast<CPed*>(ctx.edi);
            ReenableCollision(ped);
        });

        if (settings.gameplay.climbable_vehicles.allow_vaulting) {
            // Remove `!m_ClimbEntity->IsVehicle()` check for `CTaskSimpleClimb::TestForVault`
            patch::nop(0x68052D, 2);
        }
    }
    
    if (settings.hud.always_show_ammo) {
        // Show ammo even if `totalAmmo >= 9999`
        patch::nop(0x58955A, 6);
        patch::nop(0x58956B, 6);

        // Prevent overriding actual ammo value with `9999`
        patch::nop(0x589436, 2); // Flamethrower
        patch::nop(0x589472, 5); // Other weapons

        // Show actual flamethrower ammo (the game divides by 10 by default)
        patch::set<uint8_t>(0x58940E, 0xEB);
    }
    
    if (settings.visuals.disable_heat_haze) {
        patch::nop(0x705116, 5);
    }

    if (settings.visuals.disable_speed_blur) {
        patch::nop(0x704E8A, 5);
    }
}

}
