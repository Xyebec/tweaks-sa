
#include "AnimBlendAssociation.h"
#include "AnimBlendClumpData.h"
#include "AnimBlendFrameData.h"
#include "AnimManager.h"
#include "Camera.h"
#include "CutsceneMgr.h"
#include "Draw.h"
#include "General.h"
#include "Matrix.h"
#include "MenuManager.h"
#include "Mirrors.h"
#include "Pad.h"
#include "Ped.h"
#include "PedIntelligence.h"
#include "PlayerInfo.h"
#include "PlayerPed.h"
#include "Quaternion.h"
#include "Timer.h"
#include "Vector.h"
#include "Vehicle.h"
#include "VehicleModelInfo.h"
#include "WeaponInfo.h"
#include "World.h"
#include "common.h"
#include "config.h"
#include "eAnimations.h"
#include "ePedBones.h"
#include "ePedState.h"
#include "patcher.h"
#include "safetyhook/easy.hpp"
#include "safetyhook/inline_hook.hpp"
#include "utils.h"
#include <algorithm>
#include <cmath>
#include <utility>

// TODO: refactor CPad
static auto& CPad__padNumber = *reinterpret_cast<uint8_t*>(0xB73400);

static constexpr auto BONE_ROOT = static_cast<ePedBones>(0);

static CAnimBlendClumpData*& RpAnimBlendClumpGetData(RpClump* clump) {
    return *RWPLUGINOFFSET(CAnimBlendClumpData*, clump, ClumpOffset);
}

static constexpr RwV3d operator-(const RwV3d& vec) {
    return { -vec.x, -vec.y, -vec.z };
}

///////////////////////////////////////////////////////////////////////////////
////////////////////////////////// Math utils /////////////////////////////////
///////////////////////////////////////////////////////////////////////////////

// from CAutomobile
static constexpr auto GetRoll(const CMatrix& mat) -> float {
    const auto& right = mat.m_right;
    const auto  rightMag2d = right.Magnitude2D();

    // If up.z < 0.f we're flipped, in which case `right` is more like `left` so we have to negate it.
    return std::atan2(right.z, mat.m_up.z < 0.f ? -rightMag2d : rightMag2d);
}

// from CAutomobile
static constexpr auto GetPitch(const CMatrix& mat) -> float {
    const auto& fwd = mat.m_forward;
    const auto  fwdMag2d = fwd.Magnitude2D();

    // `up.z` < 0 means we're flipped on the roof, which also means `forward` is more like `backward`, so we have to negate it.
    return std::atan2(fwd.z, mat.m_up.z < 0.f ? -fwdMag2d : fwdMag2d);
}

// from CPlaceable
static constexpr auto GetHeading(const CMatrix& mat) -> float {
    const auto& fwd = mat.m_forward;
    return std::atan2(-fwd.x, fwd.y);
}

///////////////////////////////////////////////////////////////////////////////
//////////////////////////// Replace with SA funcs ////////////////////////////
///////////////////////////////////////////////////////////////////////////////

// TODO - builds quat from matrix
static auto D3DXQuaternionRotationMatrixRt(const CMatrix& matrix) -> RtQuat {
    RtQuat out;
    ((RtQuat* (__stdcall*)(RtQuat*, const RwMatrix*))0x768E5F)(&out, reinterpret_cast<const RwMatrix*>(&matrix));
    return out;
}

// TODO - use .Slerp from CQuaternion?
static auto D3DXQuaternionSlerp(const RtQuat& from, const CQuaternion& to, float t) -> RtQuat {
    RtQuat out;
    ((RtQuat* (__stdcall*)(RtQuat*, const RtQuat*, const CQuaternion*, float))0x7694BB)(&out, &from, &to, t);
    return out;
}

///////////////////////////////////////////////////////////////////////////////
//////////////////////////////////// Utils ////////////////////////////////////
///////////////////////////////////////////////////////////////////////////////

static auto IsInAnyVehicle(const CPed* ped) -> bool {
    return ped->m_pVehicle != nullptr
        && ped->bInVehicle
        && ped->m_ePedState != PEDSTATE_IDLE;
}

static auto IsDoingDriveBy(const CPed* ped) -> bool {
    static constexpr std::initializer_list<uint32_t> ANIMS = {
        ANIM_DEFAULT_DRIVEBY_L,
        ANIM_DEFAULT_DRIVEBY_R,
        ANIM_DEFAULT_DRIVEBYL_L,
        ANIM_DEFAULT_DRIVEBYL_R,

        // Works for groups:
        // BIKES, BIKEV, BIKEH, BIKED, WAYFARER, BMX, MTB, CHOPPA, QUAD
        ANIM_BIKED_BIKED_DRIVEBYLHS,
        ANIM_BIKED_BIKED_DRIVEBYRHS,
        ANIM_BIKED_BIKED_DRIVEBYFT,
    };

    return std::ranges::any_of(ANIMS, [&](uint32_t anim) {
        return RpAnimBlendClumpGetAssociation(ped->m_pRwClump, anim) != nullptr;
    });
}

// todo: check for vehicle model too
static auto IsVehicleWithoutRearView(CVehicle* vehicle) -> bool {
    const auto appearance = vehicle->GetVehicleAppearance();
    return appearance == VEHICLE_APPEARANCE_HELI
        || appearance == VEHICLE_APPEARANCE_BOAT
        || appearance == VEHICLE_APPEARANCE_PLANE;
}

static auto FindBoneMatrix(CPed* ped, ePedBones bone) -> RwMatrix* {
    // Update anim hierarchy matrices, otherwise we will get outdated ones
    ped->UpdateRpHAnim();

    const auto* animHierarchy = GetAnimHierarchyFromSkinClump(ped->m_pRwClump);

    for (auto i = 0; i < animHierarchy->numNodes; i++) {
        const auto& node = animHierarchy->pNodeInfo[i];
        if (node.nodeID == bone) {
            return &animHierarchy->pMatrixArray[i];
        }
    }

    return nullptr;
}

static auto FindAnimBlendFrameData(const CPed* ped, ePedBones bone) -> AnimBlendFrameData* {
    const auto* animHierarchy = GetAnimHierarchyFromSkinClump(ped->m_pRwClump);

    for (auto i = 0; i < animHierarchy->numNodes; i++) {
        const auto& node = animHierarchy->pNodeInfo[i];
        if (node.nodeID == bone) {
            return &RpAnimBlendClumpGetData(ped->m_pRwClump)->m_pFrames[i];
        }
    }

    return nullptr;
}

static auto GetHeadMatrix(CPed* ped) -> CMatrix {
    const auto* headMat = FindBoneMatrix(ped, BONE_NECK);

    CMatrix out;
    out.m_right   = -headMat->at;
    out.m_forward =  headMat->up;
    out.m_up      =  headMat->right;
    out.m_pos     =  headMat->pos;
    return out;
}

static void SetHeadRotation(CPed* ped, const CMatrix& rotation) {
    const auto* neckMat = FindBoneMatrix(ped, BONE_UPPERTORSO);
    const auto invNeckMat = reinterpret_cast<const CMatrix*>(neckMat)->Inverted();
    auto localRot = invNeckMat * rotation;
    
    // Rotate 90 degrees left (otherwise the head will lay on the right shoulder)
    const auto left = localRot.GetLeft();
    localRot.m_right = localRot.m_up;
    localRot.m_up = left;
    
    auto* neck = FindAnimBlendFrameData(ped, BONE_NECK)->KeyFrame;
    neck->orientation = D3DXQuaternionRotationMatrixRt(localRot);
}

static void SetPedHeading(CPed* ped, float angleRad) {
    ped->m_fCurrentRotation = angleRad;
    ped->m_fAimingRotation = angleRad;
    ped->SetHeading(angleRad);
    ped->UpdateRwMatrix();
};

static auto GetTimeDelta() -> float {
    return static_cast<float>(CTimer::m_snTimeInMilliseconds - CTimer::m_snPreviousTimeInMilliseconds) * 0.001f;
}

static auto GetMouseDelta() -> CVector2D {
    if (FrontEndMenuManager.m_nController != 0) {
        const auto* pad = CPad::GetPad(CPad__padNumber);
        return {
            static_cast<float>(pad->NewState.RightStickX) * -0.02_deg,
            static_cast<float>(pad->NewState.RightStickY) * -0.02_deg,
        };
    } else {
        return {
            CPad::NewMouseControllerState.x * -0.1_deg,
            CPad::NewMouseControllerState.y *  0.1_deg,
        };
    }
}



static void Toggle1stPersonPatches(bool enable) {
    if (enable) {
        // Skip TheCamera.m_mCameraMatrix update in CCamera::Process
        patch::copy_slice(0x52C768, {0xEB, 0x6D});

        // NOP CDraw::SetFOV in CCamera::Process
        // patch::nop(0x52C976, 5);

        // Replace MODE_ROCKETLAUNCHER with MODE_1STPERSON in CCamera::CamControl
        patch::set<uint16_t>(0x52B584, MODE_1STPERSON);
        // Replace MODE_ROCKETLAUNCHER_HS with MODE_1STPERSON in CCamera::CamControl
        patch::set<uint16_t>(0x52B58A, MODE_1STPERSON);

        // Replace jz loc_55438C with jmp (E9 A3 00 00 00 90) in CRenderer::SetupEntityVisibility
        patch::copy_slice(0x5542D2, {0xE9, 0xA3, 0x00, 0x00, 0x00, 0x90});

        // Replace jz loc_5837E0 with jmp (E9 3A 01 00 00 90) in CRadar::CalculateCachedSinCos
        patch::copy_slice(0x5836A1, {0xE9, 0x3A, 0x01, 0x00, 0x00, 0x90});

        // NOP this->m_fOrientation = v122 in CCamera::Process
        patch::nop(0x52CB84, 6);
        
        // Center crosshair // TODO: maybe hook rendering code instead
        CCamera::m_f3rdPersonCHairMultX = 0.5f;
        CCamera::m_f3rdPersonCHairMultY = 0.5f;
    } else {
        patch::copy_slice(0x52C768, {0x89, 0x8E});

        //patch::copy_slice(0x52C976, {0xE8, 0x95, 0x2A, 0x1D, 0x00});

        patch::set<uint16_t>(0x52B584, MODE_ROCKETLAUNCHER);
        patch::set<uint16_t>(0x52B58A, MODE_ROCKETLAUNCHER_HS);

        patch::copy_slice(0x5542D2, {0x0F, 0x84, 0xB4, 0x00, 0x00, 0x00});

        patch::copy_slice(0x5836A1, {0x0F, 0x84, 0x39, 0x01, 0x00, 0x00});
        
        patch::copy_slice(0x52CB84, {0x89, 0x96, 0x50, 0x01, 0x00, 0x00});
        
        CCamera::m_f3rdPersonCHairMultX = 0.53f;
        CCamera::m_f3rdPersonCHairMultY = 0.4f;
    }
}

// Used to prevent CJ from walking in wrong direction when looking back?
static void PatchCameraOrientedCalculations(bool enable) {
    // push 1 to push 0 for CCamera::CalculateDerivedValues (bOriented = false)
    patch::set<uint8_t>(0x52C97F, enable ? 0 : 1);
}



static constexpr CQuaternion QUATS_CAR_LOOK_LEFT[11] = {
    { 0.20542972f,   -0.16829863f,    0.1445148f,     0.95319974f},
    { 0.27121231f,    0.0039980016f,  0.098171972f,   0.95749158f},
    {-0.0026869487f,  0.0095231645f,  0.25834918f,    0.96600115f},
    {-0.58520657f,    0.42785013f,    0.63334274f,    0.27085793f},
    { 0.19052893f,   -0.42913774f,    0.34122187f,    0.81430089f},
    { 0.47306183f,   -0.66187f,      -0.073283173f,   0.57685673f},
    {-0.12186258f,    0.0086613372f, -0.0010634785f,  0.99252576f},
    { 0.77943605f,   -0.18453734f,    0.56903672f,    0.18607087f},
    {-0.24026594f,   -0.28998435f,   -0.0073963781f, -0.92635125f},
    {-5.1408989e-7f, -2.9802314e-8f,  0.94874758f,   -0.31603435f},
    {-0.65195382f,   -0.077403449f,   0.0068373494f,  0.75426626f},
};

static constexpr CQuaternion QUATS_CAR_LOOK_RIGHT[11] = {
    {-0.1950172f,   -0.044677973f, 0.13980378f,  0.96975756f},
    {-0.43435326f,  -0.06066069f,  0.14851025f,  0.88636589f},
    { 0.016443243f,  0.010501062f, 0.10465454f,  0.99436891f},
    { 0.76354951f,  -0.26704305f, -0.54747486f, -0.21429139f},
    { 0.044519477f, -0.3322463f,   0.021234699f, 0.9419167f },
    { 0.069367804f, -0.69145089f, -0.091091432f, 0.71329451f},
    { 0.0f,          0.0f,         0.0f,         1.0f       },
    { 0.65587789f,  -0.43751812f,  0.59923971f,  0.13898955f},
    {-0.32365149f,   0.30713385f,  0.64914447f,  0.61604917f},
    {-0.35325879f,   0.72129303f, -0.52492952f,  0.2817612f },
    { 0.0f,          0.0f,         0.0f,         1.0f       },
};

static constexpr CQuaternion QUATS_BIKE_LOOK_LEFT[11] = {
    { 0.35651129f,   -0.096049979f,  -0.14029428f,   0.91869152f},
    { 0.023543783f,   0.014388934f,   0.22509839f,   0.97390264f},
    { 0.0f,           0.0f,           0.0f,          1.0f       },
    {-0.6300717f,     0.32103762f,    0.64593834f,   0.28762221f},
    { 0.47823596f,   -0.20827475f,    0.18539238f,   0.83282864f},
    {-0.0025797237f, -0.14779228f,   -0.017261336f,  0.9888292f },
    {-0.12186258f,    0.0086613372f, -0.0010634785f, 0.99252576f},
    { 0.46826389f,   -0.32363901f,    0.80039054f,   0.18799908f},
    { 0.39423639f,    0.30099326f,    0.06046157f,   0.86616635f},
    { 0.023913963f,  -0.010647182f,  -0.40659946f,   0.91322744f},
    {-0.42264521f,    0.0f,           0.0f,          0.90625f   },
};

static constexpr CQuaternion QUATS_BIKE_LOOK_RIGHT[11] = {
    {-0.35651129f,   0.096049979f, -0.14029428f,   0.91869152f},
    {-0.023543783f, -0.014388934f,  0.22509839f,   0.97390264f},
    { 0.0f,          0.0f,          0.0f,          1.0f       },
    {-0.48962134f,   0.35608217f,   0.78109372f,   0.1527297f },
    { 0.3923243f,   -0.23252647f,   0.17731664f,   0.87206537f},
    { 0.037228547f, -0.3430483f,   -0.050637223f,  0.93723953f},
    {-0.17362785f,   0.017187871f, -0.0030306855f, 0.98462296f},
    { 0.56742144f,  -0.34379613f,   0.70251709f,   0.25749975f},
    {-0.39646748f,   0.176952f,     0.052186947f,  0.89935416f},
    { 0.0030306855f, 0.17362785f,  -0.017187871f,  0.98462296f},
    { 0.0f,          0.0f,          0.0f,          1.0f       },
};

static void BlendLookingAroundInCarAnimation(const CPed* ped, std::span<const CQuaternion, 11> bones, float t) {
    if (t <= 0.0f) {
        return;
    }
    
    t = std::min(t, 1.0f);

    static constexpr std::array<ePedBones, 11> BONES = {
        BONE_PELVIS,
        BONE_SPINE1,
        BONE_UPPERTORSO,
        BONE_RIGHTUPPERTORSO,
        BONE_RIGHTSHOULDER,
        BONE_RIGHTELBOW,
        BONE_RIGHTWRIST,
        BONE_LEFTUPPERTORSO,
        BONE_LEFTSHOULDER,
        BONE_LEFTELBOW,
        BONE_LEFTWRIST,
    };
    
    for (size_t i = 0; i < BONES.size(); i++) {
        auto* frame = FindAnimBlendFrameData(ped, BONES.at(i))->KeyFrame;
        frame->orientation = D3DXQuaternionSlerp(frame->orientation, bones[i], t);
    }
}



struct Settings {
    bool enabled{true};
    std::array<CVector, 300> vehicle_offsets{}; // was 1000; total vehicles: 212
    std::array<CVector, 300> ped_offsets{};
    float near_clip{0.03f};
    float fov{100.0f};
    float sensitivity{1.0f};
    bool look_towards_drive_by{true};
    bool center_camera_in_car{true};

    auto GetVehicleOffset(uint32_t modelId) const -> const CVector& {
        const auto index = modelId - 400;
        return (index < vehicle_offsets.size()) ? vehicle_offsets[index] : vehicle_offsets[0];
    }

    auto GetPedOffset(uint32_t modelId) const -> const CVector& {
        return (modelId < ped_offsets.size()) ? ped_offsets[modelId] : ped_offsets[0];
    }
};

static Settings g_settings;

struct FirstPersonFlags {
    bool isOnFoot : 1;
    bool limitCamAngle : 1;
};

enum class FirstPersonMode : uint8_t {
    Disabled,
    Enabled,
    TemporarilyDisabled,
};

class FirstPersonState {
public:
    auto IsEnabled() const -> bool {
        return m_mode == FirstPersonMode::Enabled;
    }

    auto GetCameraMat() -> CMatrix& {
        return m_cameraMat;
    }

    void Update1stPersonState(CPed* ped) {
        const auto ThirdToFirstPersonCameraRotation = [](const CPed* ped) -> CVector {
            auto rotX = GetPitch(TheCamera.m_mCameraMatrix);
            if (rotX > 180.0_deg) {
                rotX -= 360.0_deg;
            }

            if (IsInAnyVehicle(ped)) {
                const auto rotZ = NormalizeAngle<-PI, PI>(TheCamera.GetHeading() - ped->m_pVehicle->GetHeading());
                return CVector{rotX, 0.0f, rotZ};
            } else {
                auto rotZ = TheCamera.GetHeading();

                const auto pedHeading = GetHeading(*ped->m_matrix);
                const auto relativeZ = NormalizeAngle(pedHeading - rotZ);
                if (relativeZ > 90.0_deg && relativeZ < 270.0_deg) {
                    const auto offset = (relativeZ < 180.0_deg) ? -90.0_deg : 90.0_deg;
                    rotZ = NormalizeAngle(pedHeading + offset);
                }

                return CVector{rotX, 0.0f, rotZ};
            }
        };

        const auto Enable1stPerson = [this](const CPed* ped) {
            m_mode = FirstPersonMode::Enabled;
            Toggle1stPersonPatches(true);
            
            m_lookBack.progress = 0.0f;
            m_cameraCenteringTimer = 0.0f;
            m_flags = m_oldFlags = { .isOnFoot = !IsInAnyVehicle(ped) };
        };

        const auto Disable1stPerson = [this]() {
            m_mode = FirstPersonMode::Disabled;
            Toggle1stPersonPatches(false);

            TheCamera.m_aCams[0].m_fFOV = 70.0f; // 70.0f on foot by default
        };

        // Temporarily disable first person mode during cutscenes or when
        // the camera is not attached to the player (e.g. when using ENEX markers)
        // or when the player controls are disabled (Stowaway takeoff scene)
        if (CCutsceneMgr::ms_running
            || !TheCamera.m_bLookingAtPlayer
            || CPad::GetPad(0)->bPlayerSafe
            || ped->m_ePedState == PEDSTATE_ARRESTED
            || ped->m_ePedState == PEDSTATE_DIE
            || ped->m_ePedState == PEDSTATE_DEAD
        ) {
            if (m_mode == FirstPersonMode::Enabled) {
                Disable1stPerson();
                m_mode = FirstPersonMode::TemporarilyDisabled;
            }
            return;
        }
        
        if (m_mode == FirstPersonMode::TemporarilyDisabled) {
            Enable1stPerson(ped);
            m_cameraRot = CVector{ 0.0f, 0.0f, (m_flags.isOnFoot ? GetHeading(*ped->m_matrix) : 0.0f) };
        }

        // todo: refactor
        if (m_mode == FirstPersonMode::Enabled) {
            if (IsInAnyVehicle(ped)) {
                if (TheCamera.m_nCarZoom == 0 && m_storedCarZoom == 1) {
                    m_storedPedZoom = 5;
                    TheCamera.m_nCarZoom = 5;
                    TheCamera.m_nPedZoom = 3;

                    Disable1stPerson();
                } else {
                    m_storedCarZoom = TheCamera.m_nCarZoom;
                    TheCamera.m_nCarZoom = 1;
                }
            } else {
                if (TheCamera.m_nPedZoom == 3 && m_storedPedZoom == 1) {
                    m_storedPedZoom = 3;
                    TheCamera.m_nCarZoom = 3;
                    TheCamera.m_nPedZoom = 3;

                    Disable1stPerson();
                } else {
                    m_storedPedZoom = TheCamera.m_nPedZoom;
                    TheCamera.m_nPedZoom = 1;
                }
            }
            TheCamera.m_bUseNearClipScript = false;
        } else {
            if (IsInAnyVehicle(ped)) {
                if (TheCamera.m_nCarZoom == 0 && m_storedCarZoom == 1) {
                    m_storedCarZoom = 1;
                    TheCamera.m_nCarZoom = 1;
                    Enable1stPerson(ped);
                    m_cameraRot = ThirdToFirstPersonCameraRotation(ped);
                    return;
                }
                m_storedCarZoom = TheCamera.m_nCarZoom;
            } else {
                if (TheCamera.m_nPedZoom == 3 && m_storedPedZoom == 1) {
                    m_storedPedZoom = 3;
                    TheCamera.m_nPedZoom = 1;
                    Enable1stPerson(ped);
                    m_cameraRot = ThirdToFirstPersonCameraRotation(ped);
                    return;
                }
                m_storedPedZoom = TheCamera.m_nPedZoom;
            }
        }
    }

    void Update(CPed* ped) {
        const auto deltaTime = GetTimeDelta();
        const auto mouseDelta = GetMouseDelta() * g_settings.sensitivity;

        UpdateFlags(ped);
        HandleTransition(ped, deltaTime);

        if (m_flags.isOnFoot) {
            HandleCameraOnFoot(ped, mouseDelta, deltaTime);
        } else {
            HandleCameraInCar(ped, mouseDelta, deltaTime);
        }
    }

    void UpdateWalkingBackwards(CPed* ped, CPad* pad) {
        constexpr auto ResetBlends = [](RpClump* rwClump, std::span<const edefaultAnimGroup> anims) {
            for (const auto animId : anims) {
                if (auto* assoc = RpAnimBlendClumpGetAssociation(rwClump, animId)) {
                    assoc->m_fBlendAmount = 0.0f;
                }
            }
        };

        if (m_mode != FirstPersonMode::Enabled
            || m_flags.limitCamAngle
            || !m_flags.isOnFoot
            || ped->bIsInTheAir
            || (ped->m_pIntelligence->GetTaskJetPack() != nullptr)
            || (ped->m_pIntelligence->GetTaskSwim() != nullptr)
        ) {
            if (m_resetAllBlends) {
                static constexpr auto ANIMS = {
                    ANIM_DEFAULT_GUNMOVE_L,
                    ANIM_DEFAULT_GUNMOVE_R,
                    ANIM_DEFAULT_GUNMOVE_BWD,
                    ANIM_DEFAULT_FIGHTSH_LEFT,
                    ANIM_DEFAULT_FIGHTSH_RIGHT,
                    ANIM_DEFAULT_FIGHTSH_BWD,
                };
                ResetBlends(ped->m_pRwClump, ANIMS);

                m_resetAllBlends = false;
            }
            return;
        }

        if (m_resetGunMoveBlends) {
            static constexpr auto ANIMS = {
                ANIM_DEFAULT_GUNMOVE_L,
                ANIM_DEFAULT_GUNMOVE_R,
                ANIM_DEFAULT_GUNMOVE_BWD,
            };
            ResetBlends(ped->m_pRwClump, ANIMS);
        }

        edefaultAnimGroup animId{};
        const auto* taskUseGun = ped->m_pIntelligence->GetTaskUseGun();
        if (taskUseGun != nullptr) {
            const auto weaponSkill = ped->GetWeaponSkill();
            const auto& currentWeapon = ped->m_aWeapons[ped->m_nSelectedWepSlot]; // NOLINT
            const auto* weaponInfo = CWeaponInfo::GetWeaponInfo(currentWeapon.m_eWeaponType, weaponSkill);

            if (!weaponInfo->m_nFlags.bAimWithArm) {
                return;
            }

            animId = ANIM_DEFAULT_GUNMOVE_L;
            m_resetGunMoveBlends = true;
        } else {
            animId = ANIM_DEFAULT_FIGHTSH_LEFT;
            m_resetGunMoveBlends = false;
        }

        auto moveY = [&] {
            if (pad->NewState.LeftStickY < 0) {
                return 1.0f; // Forward
            } else if (pad->NewState.LeftStickY > 0) {
                return -1.0f; // Backward
            } else {
                return 0.0f;
            }
        }();
        
        auto moveX = [&] {
            if (pad->NewState.LeftStickX == 0 || moveY >= 0.0f) {
                return 0.0f;
            } else if (pad->NewState.LeftStickX < 0) {
                return -1.0f; // S + A
            } else {
                return 1.0f; // S + D
            }
        }();
        
        const auto magnitude = std::sqrt(moveX * moveX + moveY * moveY);
        if (magnitude > 0.0f) {
            const auto recip = 1.0f / magnitude;
            moveX *= recip;
            moveY *= recip;
        }

        if (moveY < 0.0f) {
            pad->NewState.LeftStickY = 0;
            if (moveX != 0.0f) {
                pad->NewState.LeftStickX = 0;
            }
        }

        auto* animL   = RpAnimBlendClumpGetAssociation(ped->m_pRwClump, animId);     // GUNMOVE_L   or FIGHTSH_LEFT
        auto* animBwd = RpAnimBlendClumpGetAssociation(ped->m_pRwClump, animId + 1); // GUNMOVE_BWD or FIGHTSH_BWD
        auto* animR   = RpAnimBlendClumpGetAssociation(ped->m_pRwClump, animId + 2); // GUNMOVE_R   or FIGHTSH_RIGHT
        if (animL   != nullptr) { animL->m_fBlendAmount   = 0.0f; }
        if (animR   != nullptr) { animR->m_fBlendAmount   = 0.0f; }
        if (animBwd != nullptr) { animBwd->m_fBlendAmount = 0.0f; }

        if (moveX > 0.0f) {
            if (animR == nullptr) {
                animR = CAnimManager::AddAnimation(ped->m_pRwClump, ANIM_GROUP_DEFAULT, animId + 2); // GUNMOVE_R or FIGHTSH_RIGHT
            }
            animR->m_fBlendAmount = moveX;
            m_resetAllBlends = true;
        } else if (moveX < 0.0f) {
            if (animL == nullptr) {
                animL = CAnimManager::AddAnimation(ped->m_pRwClump, ANIM_GROUP_DEFAULT, animId); // GUNMOVE_L or FIGHTSH_LEFT
            }
            animL->m_fBlendAmount = -moveX;
            m_resetAllBlends = true;
        }
        
        if (moveY >= 0.0f) {
            return;
        }

        if (animBwd == nullptr) {
            animBwd = CAnimManager::AddAnimation(ped->m_pRwClump, ANIM_GROUP_DEFAULT, animId + 1); // GUNMOVE_BWD or FIGHTSH_BWD
        }
        animBwd->m_fBlendAmount = -moveY;
        m_resetAllBlends = true;

        SetPedHeading(ped, m_cameraRot.z);
    }

private:
    void UpdateFlags(const CPed* ped) {
        m_oldFlags = m_flags;
            
        if (!IsInAnyVehicle(ped)) {
            m_flags.limitCamAngle = ped->m_pIntelligence->GetTaskClimb() != nullptr
                || ped->m_pIntelligence->GetUsingParachute()
                || ped->m_pIntelligence->FindTaskByType(TASK_COMPLEX_ENTER_CAR_AS_DRIVER) != nullptr;

            m_flags.isOnFoot = true;
        } else {
            m_flags.limitCamAngle = false;
            m_flags.isOnFoot = false;
        }
    }

    /// Handles transition from onfoot to incar and vice versa
    void HandleTransition(const CPed* ped, float deltaTime) {
        if (m_flags.isOnFoot != m_oldFlags.isOnFoot) {
            if (m_flags.isOnFoot) {
                m_cameraRot.x = GetPitch(m_cameraMat);
                m_cameraRot.y = -GetRoll(m_cameraMat);
                m_cameraRot.z = GetHeading(m_cameraMat);
            } else {
                const auto relativeMat = ped->m_pVehicle->m_matrix->Inverted() * m_cameraMat;

                m_cameraRot.x = GetPitch(relativeMat);
                m_cameraRot.y = -GetRoll(relativeMat);
                m_cameraRot.z = GetHeading(relativeMat);
            }
        }

        if (m_cameraRot.y != 0.0f) {
            const auto step = deltaTime * 5.0f;

            m_cameraRot.y = m_cameraRot.y > 0.0f
                ? std::max(0.0f, m_cameraRot.y - step)
                : std::min(0.0f, m_cameraRot.y + step);
        }
    }

    void HandleCameraOnFoot(CPed* ped, const CVector2D& mouseDelta, float deltaTime) {
        const auto headingZ = ped->GetHeading();
        const auto newRotZ = NormalizeAngle(m_cameraRot.z + mouseDelta.x);
        const auto relativeZ = NormalizeAngle<-PI, PI>(headingZ - newRotZ);

        // Right
        if (relativeZ >= 90.0_deg) {
            if (!m_flags.limitCamAngle) {
                m_cameraRot.z = newRotZ;
                SetPedHeading(ped, NormalizeAngle(m_cameraRot.z + 90.0_deg));
            } else {
                m_cameraRot.z = NormalizeAngle(headingZ - 90.0_deg);
            }
        }
        // Left
        else if (relativeZ <= -90.0_deg) {
            if (!m_flags.limitCamAngle) {
                m_cameraRot.z = newRotZ;
                SetPedHeading(ped, NormalizeAngle(m_cameraRot.z - 90.0_deg));
            } else {
                m_cameraRot.z = NormalizeAngle(headingZ + 90.0_deg);
            }
        }
        // Within bounds - just update camera
        else {
            m_cameraRot.z = newRotZ;
        }
        
        m_cameraMat.SetRotate(m_cameraRot);

        const auto UpdateLookingBack = [&](float delta) {
            static constexpr auto QUAT_45_DEG_LEFT  = CQuaternion{ 0.38268343f, 0.0f, 0.0f, 0.92387956f}; // xyz:  45, 0, 0
            static constexpr auto QUAT_45_DEG_RIGHT = CQuaternion{-0.38268343f, 0.0f, 0.0f, 0.92387956f}; // xyz: -45, 0, 0

            const auto angle = std::lerp(0.0f, m_lookBack.rightShoulder ? -179.0_deg : 179.0_deg, m_lookBack.progress);
            m_cameraMat.RotateZ(angle);

            m_cameraRot.x = MoveTowards(m_cameraRot.x, 0.0f, delta);

            const auto& target = m_lookBack.rightShoulder
                ? QUAT_45_DEG_RIGHT : QUAT_45_DEG_LEFT;

            auto* spine = FindAnimBlendFrameData(ped, BONE_PELVIS)->KeyFrame;
            auto* spine1 = FindAnimBlendFrameData(ped, BONE_SPINE1)->KeyFrame;
            spine->orientation = D3DXQuaternionSlerp(spine->orientation, target, m_lookBack.progress);
            spine1->orientation = D3DXQuaternionSlerp(spine1->orientation, target, m_lookBack.progress);
        };

        const auto delta = deltaTime * 5.0f;

        if (TheCamera.m_aCams[0].m_nDirectionWasLooking == 0) {
            if (m_lookBack.progress == 0.0f) {
                PatchCameraOrientedCalculations(true);
                const auto zDeg = NormalizeAngle(headingZ - 179.0_deg);
                m_lookBack.rightShoulder = NormalizeAngle(m_cameraRot.z - zDeg) < 180.0_deg;
            }

            m_lookBack.progress = std::min(m_lookBack.progress + delta, 1.0f);

            UpdateLookingBack(delta);
            return;
        }
        
        if (m_lookBack.progress > 0.0f) {
            m_lookBack.progress -= delta;
            if (m_lookBack.progress <= 0.0f) {
                m_lookBack.progress = 0.0f;
                PatchCameraOrientedCalculations(false);
            }

            UpdateLookingBack(delta);
            return;
        }

        m_cameraRot.x = std::clamp(m_cameraRot.x + mouseDelta.y, -70.0_deg, 80.0_deg);
    }

    void HandleCameraInCar(CPed* ped, const CVector2D& mouseDelta, float deltaTime) {
        auto* vehicle = ped->m_pVehicle;

        const auto UpdateCamMat = [&]() {
            const auto& offset = g_settings.GetVehicleOffset(vehicle->m_nModelIndex);
            auto* neck = FindAnimBlendFrameData(ped, BONE_NECK)->KeyFrame;
            neck->translation.x += offset.z;
            neck->translation.y += offset.y;
            neck->translation.z -= offset.x;

            const auto cameraRot = CMatrix::FromRotation(m_cameraRot);
            m_cameraMat = *vehicle->m_matrix * cameraRot;
        };

        const auto& cam = TheCamera.m_aCams[0];
        
        if (HandleLookingAroundInCar(cam, ped, vehicle, deltaTime)
            || HandleCameraCenteringInCar(cam, vehicle, mouseDelta, deltaTime)
        ) {
            BlendLookingAroundInCarPose(cam, ped, vehicle);
            UpdateCamMat();
            return;
        }

        m_cameraRot.z += mouseDelta.x;
        m_cameraRot.x += mouseDelta.y;
    
        if (cam.m_nMode == MODE_AIMWEAPON_FROMCAR) {
            m_cameraRot.z = NormalizeAngle(m_cameraRot.z + 180.0_deg) - 180.0_deg;
        } else {
            // Limit horizontal camera angle to [-180.0f, 180.0f]
            m_cameraRot.z = std::clamp(m_cameraRot.z, -180.0_deg, 180.0_deg);
        }
    
        // Limit vertical camera angle to [-70.0f, 80.0f]
        m_cameraRot.x = std::clamp(m_cameraRot.x, -70.0_deg, 80.0_deg);

        // Limit horizontal camera angle to [-90.0f, 90.0f] for certain vehicle types or when getting out of car
        if (ped->m_ePedState == PEDSTATE_NONE || IsVehicleWithoutRearView(vehicle)) {
            m_cameraRot.z = std::clamp(m_cameraRot.z, -90.0_deg, 90.0_deg);
        }

        BlendLookingAroundInCarPose(cam, ped, vehicle);
        UpdateCamMat();
    }

private:
    auto HandleLookingAroundInCar(const CCam& cam, const CPed* ped, CVehicle* vehicle, float deltaTime) -> bool {
        const auto isLookingAround = cam.m_nMode != MODE_AIMWEAPON_FROMCAR
            && (cam.m_bLookingLeft || cam.m_bLookingRight || cam.m_bLookingBehind || m_lookBack.progress > 0.0f)
            && ped->m_ePedState != PEDSTATE_NONE;
        
        if (!isLookingAround) {
            return false;
        }

        const auto deltaVer = deltaTime * 180.0_deg;
        const auto deltaHor = deltaVer * 4.0f;

        if (cam.m_bLookingBehind && !IsVehicleWithoutRearView(vehicle)) {
            m_cameraRot.z = (m_cameraRot.z > 0)
                ? std::min(m_cameraRot.z + deltaHor,  180.0_deg)
                : std::max(m_cameraRot.z - deltaHor, -180.0_deg);

            m_cameraRot.x = MoveTowards(m_cameraRot.x, 0.0f, deltaVer);

            m_lookBack.progress = 1.0f;
            return true;
        }

        if (cam.m_bLookingLeft && g_settings.look_towards_drive_by) {
            m_cameraRot.z = MoveTowards(m_cameraRot.z, 90.0_deg, deltaHor);
            m_cameraRot.x = MoveTowards(m_cameraRot.x, 0.0f, deltaVer);

            m_lookBack.progress = 0.5f;
            return true;
        }

        if (cam.m_bLookingRight && g_settings.look_towards_drive_by) {
            m_cameraRot.z = MoveTowards(m_cameraRot.z, -90.0_deg, deltaHor);
            m_cameraRot.x = MoveTowards(m_cameraRot.x, 0.0f, deltaVer);
            
            m_lookBack.progress = 0.5f;
            return true;
        }
    
        if (m_lookBack.progress > 0.0f) {
            if (m_cameraRot.z != 0.0f) {
                const auto sign = (m_cameraRot.z > 0.0f) ? 1.0f : -1.0f;
                m_cameraRot.z -= sign * deltaHor;

                if ((m_cameraRot.z * sign) <= 0.0f) {
                    m_cameraRot.z = 0.0f;
                    m_lookBack.progress = 0.0f;
                }

                return true;
            } else {
                m_lookBack.progress = 0.0f;
            }
        }

        return false;
    }

    void BlendLookingAroundInCarPose(const CCam& cam, const CPed* ped, CVehicle* vehicle) const {
        if (cam.m_nMode == MODE_AIMWEAPON_FROMCAR || IsDoingDriveBy(ped)) {
            return;
        }

        patch::copy_slice(0x6B8559, {0x0F, 0x84}); // restore if (this->m_bPedLeftHandFixed)
        patch::copy_slice(0x6B8202, {0x0F, 0x84}); // restore if (this->m_bPedRightHandFixed)
    
        const auto appearance = vehicle->GetVehicleAppearance();
        // Angle at which look back anim should start
        const auto blendStartAngle = appearance == VEHICLE_APPEARANCE_BIKE ? 90.0_deg : 60.0_deg;

        if (m_cameraRot.z < blendStartAngle && m_cameraRot.z > -blendStartAngle) {
            return;
        }

        if (appearance == VEHICLE_APPEARANCE_BIKE) {
            if (m_cameraRot.z >= 0.0f) {
                const auto blendValue = (m_cameraRot.z - blendStartAngle) / 90.0_deg;
                BlendLookingAroundInCarAnimation(ped, QUATS_BIKE_LOOK_LEFT, blendValue);
                patch::copy_slice(0x6B8559, {0x90, 0xE9}); // Disable left hand fixing; TODO: move to hook
            } else {
                const auto blendValue = (-m_cameraRot.z - blendStartAngle) / 90.0_deg;
                BlendLookingAroundInCarAnimation(ped, QUATS_BIKE_LOOK_RIGHT, blendValue);
                patch::copy_slice(0x6B8202, {0x90, 0xE9}); // Disable right hand fixing; TODO: move to hook
            }
            return;
        }
        
        // Automobile/heli/boat/plane:
        if (m_cameraRot.z >= 0.0f) {
            auto blendValue = (m_cameraRot.z - blendStartAngle) / 80.0_deg; // TODO: why 80?
            BlendLookingAroundInCarAnimation(ped, QUATS_CAR_LOOK_LEFT, blendValue);
            
            if (blendValue <= 0.0f) {
                return;
            }

            blendValue = std::min(blendValue, 1.0f); // todo: move to blendValue init

            static constexpr auto POSE_OFFSET = CVector{-0.3f, 0.0f, 0.113f};
            static constexpr auto POSE_ORIENTATION = CQuaternion{0.11877283f, -0.19613053f, 0.67650485f, 0.6998335f}; // todo
            auto* root = FindAnimBlendFrameData(ped, BONE_ROOT)->KeyFrame;
            root->translation = Lerp(root->translation, POSE_OFFSET, blendValue);
            root->orientation = D3DXQuaternionSlerp(root->orientation, POSE_ORIENTATION, blendValue);
        } else {
            const auto blendValue = (-m_cameraRot.z - blendStartAngle) / 80.0_deg; // TODO: why 80?
            BlendLookingAroundInCarAnimation(ped, QUATS_CAR_LOOK_RIGHT, blendValue);
            auto* root = FindAnimBlendFrameData(ped, BONE_ROOT)->KeyFrame;
            static constexpr auto POSE_OFFSET = CVector{0.3f, 0.0f, 0.0f};
            root->translation = Lerp(root->translation, POSE_OFFSET, blendValue);
        }
    }

    auto HandleCameraCenteringInCar(const CCam& cam, const CVehicle* vehicle, const CVector2D& mouseDelta, float deltaTime) -> bool {
        if (!g_settings.center_camera_in_car) {
            return false;
        }
        
        if (cam.m_nMode == MODE_AIMWEAPON_FROMCAR) {
            return false;
        }

        if (!mouseDelta.IsZero()) {
            m_cameraCenteringTimer = 50.0f;
        }

        if (m_cameraCenteringTimer > 0.0f) {
            m_cameraCenteringTimer = std::max(0.0f, m_cameraCenteringTimer - CTimer::ms_fTimeStep);
        }

        const auto isAircraft = vehicle->m_nVehicleSubClass == VEHICLE_HELI || vehicle->m_nVehicleSubClass == VEHICLE_PLANE;
        const auto isMouseSteering = isAircraft ? CVehicle::m_bEnableMouseFlying : CVehicle::m_bEnableMouseSteering;

        const auto shouldCenter =
               (isMouseSteering && CPad::GetPad(0)->NewState.m_bVehicleMouseLook == 0)
            || (m_cameraCenteringTimer <= 0.0f && vehicle->m_vecMoveSpeed.SquaredMagnitude2D() > 0.02f);

        if (!shouldCenter) {
            return false;
        }
        
        m_cameraRot = MoveTowards(m_cameraRot, CVector{}, deltaTime * 3.0f);
        return true;
    }

public:
    FirstPersonMode m_mode{FirstPersonMode::Disabled};
    uint8_t m_storedPedZoom{};
    uint8_t m_storedCarZoom{};

    FirstPersonFlags m_flags{};
    FirstPersonFlags m_oldFlags{};
    
    // todo: Matrix3x3
    CMatrix m_cameraMat{};
    CVector m_cameraRot{};
    
    /// Same as the OG timer at `0xB70118`
    float m_cameraCenteringTimer{};

    struct {
        // On foot & in car:
        float progress{};
        // On foot only:
        bool rightShoulder{};
    } m_lookBack;

    // Walking backwards
    bool m_resetGunMoveBlends{};
    bool m_resetAllBlends{};
};

static FirstPersonState g_state;



static safetyhook::InlineHook Orig_CCam__Process;
static void CALLCONV_FASTCALL Hook_CCam__Process(CCam* self, uintptr_t /*edx*/) {
    auto* playerPed = FindPlayerPed(-1);

    // Handle first person mode switching and hotkeys
    g_state.Update1stPersonState(playerPed);

    // Workaround for CCam::Process_AimWeapon messing with m_fHorizontalAngle and m_fVerticalAngle
    CVector2D storedMouse;
    if (g_state.IsEnabled() && g_state.m_flags.isOnFoot) {
        storedMouse = {
            std::exchange(CPad::NewMouseControllerState.x, 0.0f),
            std::exchange(CPad::NewMouseControllerState.y, 0.0f),
        };
    }

    Orig_CCam__Process.unsafe_thiscall(self);

    if (g_state.IsEnabled()) {
        if (g_state.m_flags.isOnFoot) {
            CPad::NewMouseControllerState.x = storedMouse.x;
            CPad::NewMouseControllerState.y = storedMouse.y;
        }

        g_state.Update(playerPed);

        SetHeadRotation(playerPed, g_state.GetCameraMat());

        // todo: nearclip breaks light from light poles
        // todo: TheCamera.SetNearClipScript(g_settings.near_clip);
        TheCamera.m_bUseNearClipScript = true;
        TheCamera.m_fNearClipScript = g_settings.near_clip;

        const auto camMode = TheCamera.m_aCams[0].m_nMode;
        if (camMode != MODE_SNIPER && camMode != MODE_CAMERA) {
            CDraw::ms_fFOV = g_settings.fov;
            TheCamera.m_aCams[0].m_fFOV = g_settings.fov;
        }
    }
}

static void UpdateTheCamera(CPed* ped) {
    auto camOffset = g_settings.GetPedOffset(ped->m_nModelIndex);
    camOffset.y += 0.09f;
    camOffset.z += 0.09f;

    const auto headMat = GetHeadMatrix(ped);
    const auto camPos = headMat.TransformPoint(camOffset);

    const auto& camMat = g_state.GetCameraMat();
    
    TheCamera.m_mCameraMatrix = camMat;
    TheCamera.m_mCameraMatrix.m_right *= -1.0f;
    TheCamera.m_mCameraMatrix.m_pos = camPos;

    TheCamera.m_aCams[0].m_vecSource = camPos;
    TheCamera.m_aCams[0].m_vecUp     = camMat.m_up;
    TheCamera.m_aCams[0].m_vecFront  = camMat.m_forward;

    TheCamera.m_aCams[0].m_fVerticalAngle   = GetPitch(camMat);
    TheCamera.m_aCams[0].m_fHorizontalAngle = GetHeading(camMat) - 90.0_deg;
}

static safetyhook::InlineHook Orig_CMirrors__BeforeConstructRenderList;
static void Hook_CMirrors__BeforeConstructRenderList() {
    if (g_state.IsEnabled()) {
        auto* playerPed = FindPlayerPed(-1);
        UpdateTheCamera(playerPed);
    }

    Orig_CMirrors__BeforeConstructRenderList.unsafe_ccall();
}

static safetyhook::InlineHook Orig_CPed__PreRenderAfterTest;
static void CALLCONV_FASTCALL Hook_CPed__PreRenderAfterTest(CPed* self, uintptr_t /*edx*/) {
    Orig_CPed__PreRenderAfterTest.unsafe_thiscall(self);

    if (g_state.IsEnabled() && self == FindPlayerPed(-1)) {
        UpdateTheCamera(self);
        TheCamera.CopyCameraMatrixToRWCam(false);
    }
}

static safetyhook::InlineHook Orig_CEntity__GetIsOnScreen;
static bool CALLCONV_FASTCALL Hook_CEntity__GetIsOnScreen(CEntity* self) {
    return self == FindPlayerPed(-1)
        || Orig_CEntity__GetIsOnScreen.unsafe_thiscall<bool>(self);
}

static safetyhook::InlineHook Orig_CLoadMonitor__BeginFrame;
static void CALLCONV_FASTCALL Hook_CLoadMonitor__BeginFrame(class CLoadMonitor* self, uintptr_t /*edx*/) {
    Orig_CLoadMonitor__BeginFrame.unsafe_thiscall(self);

    auto* playerPed = FindPlayerPed(-1);
    auto* pad = CPad::GetPad(0);

    g_state.UpdateWalkingBackwards(playerPed, pad);
}

namespace first_person {

extern void ReadConfig(const Config& config) {
    // config.Deserialize("first-person", settings);
}

extern void Apply() {
    if (!g_settings.enabled) {
        return;
    }

    // Always return true for player from CEntity::GetIsOnScreen
    Orig_CEntity__GetIsOnScreen = safetyhook::create_inline(0x534540, Hook_CEntity__GetIsOnScreen);

    // NOP `!this->m_nFlags.m_bOffscreen` check in CVehicle::ProcessDrivingAnims
    // Keeps updating driving anims in first person
    patch::nop(0x6DF4AF, 6);

    // Main hook
    Orig_CCam__Process = safetyhook::create_inline(0x526FC0, Hook_CCam__Process);
    // Keep mirrors in sync with camera's position
    Orig_CMirrors__BeforeConstructRenderList = safetyhook::create_inline(0x726DF0, Hook_CMirrors__BeforeConstructRenderList);
    // Keep camera's position in sync with player's position
    Orig_CPed__PreRenderAfterTest = safetyhook::create_inline(0x5E65A0, Hook_CPed__PreRenderAfterTest);
    // Walking backwards
    Orig_CLoadMonitor__BeginFrame = safetyhook::create_inline(0x53D030, Hook_CLoadMonitor__BeginFrame);
}

}
