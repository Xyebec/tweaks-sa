
#include "AnimBlendClumpData.h"
#include "AnimBlendFrameData.h"
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
#include "Timer.h"
#include "Vector.h"
#include "Vehicle.h"
#include "World.h"
#include "common.h"
#include "config.h"
#include "ePedBones.h"
#include "patcher.h"
#include "safetyhook/easy.hpp"
#include "safetyhook/inline_hook.hpp"
#include <algorithm>
#include <numbers>

// TODO: refactor CPad
static auto& CPad__padNumber = *reinterpret_cast<uint8_t*>(0xB73400);

static auto BONE_ROOT = static_cast<ePedBones>(0);

static CAnimBlendClumpData*& RpAnimBlendClumpGetData(RpClump* clump) {
    return *RWPLUGINOFFSET(CAnimBlendClumpData*, clump, ClumpOffset);
}

static constexpr RwV3d operator-(const RwV3d& vec) {
    return { -vec.x, -vec.y, -vec.z };
}

///////////////////////////////////////////////////////////////////////////////
////////////////////////////////// Math utils /////////////////////////////////
///////////////////////////////////////////////////////////////////////////////

static constexpr auto RAD_TO_DEG = 180.0f / std::numbers::pi_v<float>;
static constexpr auto DEG_TO_RAD = std::numbers::pi_v<float> / 180.0f;
static constexpr auto FRAC_PI_2 = std::numbers::pi_v<float> / 2.0f;
static constexpr auto TAU = std::numbers::pi_v<float> * 2.0f;

static auto NormalizeAngle(float angle) -> float {
    while (angle > 360.0f) {
        angle -= 360.0f;
    }

    while (angle < 0.0f) {
        angle += 360.0f;
    }

    return angle;
}

static auto GetHeadingFromXY(float x, float y) -> float {
    return CGeneral::GetATanOfXY(x, y) * RAD_TO_DEG - 90.0f;
}

// todo rename?
static auto MatToEuler(const CMatrix& mat) -> CVector {
    const auto x = NormalizeAngle(360.0f - GetHeadingFromXY(mat.m_forward.z, mat.m_forward.Magnitude2D()));
    const auto y = GetHeadingFromXY(mat.m_right.z, mat.m_right.Magnitude2D());
    const auto z = GetHeadingFromXY(mat.m_forward.x, mat.m_forward.y);
    return {x, y, z};
}

static void CMatrix_SetRotateOnlyDeg(CMatrix& self, float degX, float degY, float degZ) {
    self.SetRotateKeepPos({degX * DEG_TO_RAD, degY * DEG_TO_RAD, degZ * DEG_TO_RAD});
    // self.SetRotateOnly(degX * DEG_TO_RAD, degY * DEG_TO_RAD, degZ * DEG_TO_RAD); // TODO - without useless pos copying
}

// static auto CMatrix_Rotated(const CMatrix& mat, const CMatrix& rotMat) -> CMatrix {
//     CMatrix out;
//     out.m_right   = rotMat.TransformVector(mat.m_right);
//     out.m_forward = rotMat.TransformVector(mat.m_forward);
//     out.m_up      = rotMat.TransformVector(mat.m_up);
//     return out;
// }

///////////////////////////////////////////////////////////////////////////////
//////////////////////////////////// Util2 ////////////////////////////////////
///////////////////////////////////////////////////////////////////////////////

static bool IsInAnyVehicle(const CPed* ped) {
    return ped->m_pVehicle != nullptr
        && ped->bInVehicle
        && ped->m_ePedState != PEDSTATE_IDLE;
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

static void SetPedHeading(CPed* ped, float angleDeg) {
    const auto headingRad = angleDeg * DEG_TO_RAD;
    ped->m_fCurrentRotation = headingRad;
    ped->m_fAimingRotation = headingRad;
    ped->SetHeading(headingRad);
    ped->UpdateRwMatrix();
};

static auto GetTimeDelta() -> float {
    return static_cast<float>(CTimer::m_snTimeInMilliseconds - CTimer::m_snPreviousTimeInMilliseconds) * 0.001f;
}

///////////////////////////////////////////////////////////////////////////////
//////////////////////////// Replace with SA funcs ////////////////////////////
///////////////////////////////////////////////////////////////////////////////

// TODO - builds quat from matrix
// TODO: CQuaternion::Set(const CMatrix&) or CQuaternion::From(const CMatrix&)
static auto D3DXQuaternionRotationMatrix(const CMatrix& matrix) -> CQuaternion {
    CQuaternion out;
    ((CQuaternion* (__stdcall*)(CQuaternion*, const RwMatrix*))0x768E5F)(&out, reinterpret_cast<const RwMatrix*>(&matrix));
    return out;
}
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
















struct FirstPersonFlags {
    bool isOnFoot : 1;
    bool isInCar : 1;
    bool lockCamera : 1;
    bool dontRotateWithCam : 1;
    bool unk_x20 : 1;
    bool unk_slippedOffCurb_x40 : 1;
};

struct FirstPersonState {
    bool isFirstPersonOn{};
    bool enableFirstPersonLater{};
    uint8_t storedPedZoom{};
    uint8_t storedCarZoom{};
    FirstPersonFlags flags{};
    FirstPersonFlags oldFlags{};
    float unkFloatForSlerp{1.0f};
    float slerpLookBackAnimInCar{}; // used only in `UpdateCameraInVehicle`. gets assigned with t for look left/right anim slerp t
    CMatrix cameraMat{}; // RwMatrix
    CMatrix unkMat4{}; // RwMatrix
    CMatrix unkMat6{}; // RwMatrix car related? `g_state.unkMat6.pos = g_state.headMat.pos;`
    CMatrix unkMat7{}; // RwMatrix
    CVector cameraRotOnFoot{}; // todo: related to onfoot?
    CVector unkVec3{}; // looks like camera angle, used only by walking back anim. ONLY Z IS READ
    CVector cameraRotInCar{}; // describes camera angle in degrees; x is vertical, z is horizontal; related to cars only
    
    ////////// 10004D90 HandleCameraLock //////////
    float lockedCamY{};
    float lockedCamX{};
    CVector unusedVec{}; // TODO: write-only variable. og intent was to store pre-driveby angle

    ////////// onfoot and incar look back //////////    
    float lookBackProgress{}; // used by UpdateCameraInVehicle and UpdateCameraOnfoot; used to store t for slerp. Kinda unused in car
    
    ////////// onfoot look back //////////
    bool isLookingBackOverRightShoulder{};
    CQuaternion tempLookBackQuat1{}; // temporary look back quat to slerp between itself and quat2, used by unkMat4
    CQuaternion tempLookBackQuat2{}; // temporary look back quat to slerp between quat1 and itself
    CMatrix tempLookBackMat1{}; // RwMatrix
    CMatrix tempLookBackMat2{}; // RwMatrix
};

struct Settings {
    std::array<CVector, 1000> vehicleOffsets{};
    std::array<CVector, 300> pedOffsets{};
    float nearClip{0.03f};
    float fov{100.0f};
    float sensitivity{1.0f};
    bool rotateWithCamera{false};
    bool rotateAtSidesInCar{true};

    auto GetVehicleOffset(uint32_t modelId) const -> const CVector& {
        return this->vehicleOffsets.at(modelId - 400);
    }

    auto GetPedOffset(uint32_t modelId) const -> const CVector& {
        return this->pedOffsets.at(modelId);
    }
};

// struct GuiData {
//     // uint8_t isMenuKeyHeld;
//     // uint8_t isMenuVisible;
//     // // uint8_t _pad[2];
//     // GUIWindow *pGuiWindow;
//     // GUISlider *pPedOffsetsSliders[3];
//     // GUISlider *pCarOffsetsSliders[3];
//     // GUILabel *pCurrentSkinLabel;
//     // GUILabel *pCurrentCarLabel;
//     // GUISlider *pNearClipSlider;
//     // GUISlider *pFovSlider;
//     // GUISlider *pSensitivitySlider;
//     // GUICheckBox *pRotateWithCameraCheckbox;
//     // GUICheckBox *pRotateAtSidesInCarCheckbox;
//     // // uint8_t _unused3[4];
//     // uint32_t playerModel;
//     // uint32_t carModel;
//     // GUIMain *pGuiMain;
// };

static FirstPersonState g_state;
static Settings g_settings;
// static GuiData g_gui;





/// Initializes the first-person camera with the same
/// direction as the third-person, but limits the angle
static void InitFirstPersonCamera() {
    const auto* playerPed = CWorld::Players[0].m_pPed;

    auto rotation = MatToEuler(TheCamera.m_mCameraMatrix);
    if (rotation.x > 180.0f) {
        rotation.x = -(360.0f - rotation.x);
    }
    
    const auto headingZ = GetHeadingFromXY(playerPed->m_matrix->m_forward.x, playerPed->m_matrix->m_forward.y);
    const auto relativeZ = NormalizeAngle(headingZ - rotation.z);
    if (relativeZ > 90.0f && relativeZ < 270.0f) {
        const auto angle = relativeZ < 180.0f
            ? headingZ - 90.0f
            : headingZ + 90.0f;

        rotation.z = NormalizeAngle(angle);
    }

    g_state.cameraRotOnFoot = rotation;
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
    } else {
        patch::copy_slice(0x52C768, {0x89, 0x8E});

        //patch::copy_slice(0x52C976, {0xE8, 0x95, 0x2A, 0x1D, 0x00});

        patch::set<uint16_t>(0x52B584, MODE_ROCKETLAUNCHER);
        patch::set<uint16_t>(0x52B58A, MODE_ROCKETLAUNCHER_HS);

        patch::copy_slice(0x5542D2, {0x0F, 0x84, 0xB4, 0x00, 0x00, 0x00});

        patch::copy_slice(0x5836A1, {0x0F, 0x84, 0x39, 0x01, 0x00, 0x00});
        
        patch::copy_slice(0x52CB84, {0x89, 0x96, 0x50, 0x01, 0x00, 0x00});
    }
}















// TODO: (re)move
// from CAutomobile
// static auto GetRoll(const CMatrix& mat) -> float {
//     const auto& right = mat.m_right;
//     const auto  rightMag2d = right.Magnitude2D();

//     // If up.z < 0.f we're flipped, in which case `right` is more like `left` so we have to negate it.
//     return std::atan2(right.z, mat.m_up.z < 0.f ? -rightMag2d : rightMag2d);
// }

// from CAutomobile
static auto GetPitch(const CMatrix& mat) -> float {
    const auto& fwd = mat.m_forward;
    const auto  fwdMag2d = fwd.Magnitude2D();

    // `up.z` < 0 means we're flipped on the roof, which also means `forward` is more like `backward`, so we have to negate it.
    return std::atan2(fwd.z, mat.m_up.z < 0.f ? -fwdMag2d : fwdMag2d);
}

// from CPlaceable
static auto GetHeading(const CMatrix& mat) -> float {
    const auto& fwd = mat.m_forward;
    return std::atan2(-fwd.x, fwd.y);
}
















static void Update1stPersonState() {
    constexpr auto Enable1stPerson = []() {
        g_state.isFirstPersonOn = true;
        Toggle1stPersonPatches(true);
        
        InitFirstPersonCamera();
        g_state.unkFloatForSlerp = 1.0f;
        g_state.oldFlags = { .isOnFoot = true };
        g_state.flags = { .isOnFoot = true };

        // Center crosshair // TODO: maybe hook rendering code instead
        CCamera::m_f3rdPersonCHairMultX = 0.5f;
        CCamera::m_f3rdPersonCHairMultY = 0.5f;
    };

    constexpr auto Disable1stPerson = []() {
        g_state.isFirstPersonOn = false;
        Toggle1stPersonPatches(false);

        CCamera::m_f3rdPersonCHairMultX = 0.53f;
        CCamera::m_f3rdPersonCHairMultY = 0.4f;

        TheCamera.m_aCams[0].m_fFOV = 70.0f; // 70.0f on foot by default
    };

    // Temporarily disable first person mode during cutscenes or when
    // the camera is not attached to the player (e.g. when using ENEX markers)
    // or when the player controls are disabled (Stowaway takeoff scene)
    if (CCutsceneMgr::ms_running || !TheCamera.m_bLookingAtPlayer || CPad::GetPad(0)->bPlayerSafe) {
        if (g_state.isFirstPersonOn) {
            g_state.enableFirstPersonLater = true;
            Disable1stPerson();
        }
        return;
    }
    
    const auto* playerPed = CWorld::Players[0].m_pPed;

    if (g_state.enableFirstPersonLater) {
        g_state.enableFirstPersonLater = false;
        Enable1stPerson();

        g_state.cameraRotOnFoot.x = GetPitch(*playerPed->m_matrix) * RAD_TO_DEG;
        g_state.cameraRotOnFoot.z = GetHeading(*playerPed->m_matrix) * RAD_TO_DEG;
    }

    // todo: refactor
    if (g_state.isFirstPersonOn) {
        if (IsInAnyVehicle(playerPed)) {
            if (TheCamera.m_nCarZoom == 0 && g_state.storedCarZoom == 1) {
                g_state.storedPedZoom = 5;
                TheCamera.m_nCarZoom = 5;
                TheCamera.m_nPedZoom = 3;

                Disable1stPerson();
            } else {
                g_state.storedCarZoom = TheCamera.m_nCarZoom;
                TheCamera.m_nCarZoom = 1;
            }
        } else {
            if (TheCamera.m_nPedZoom == 3 && g_state.storedPedZoom == 1) {
                g_state.storedPedZoom = 3;
                TheCamera.m_nCarZoom = 3;
                TheCamera.m_nPedZoom = 3;

                Disable1stPerson();
            } else {
                g_state.storedPedZoom = TheCamera.m_nPedZoom;
                TheCamera.m_nPedZoom = 1;
            }
        }
        TheCamera.m_bUseNearClipScript = false;
    } else {
        if (IsInAnyVehicle(playerPed)) {
            if (TheCamera.m_nCarZoom == 0 && g_state.storedCarZoom == 1) {
                g_state.storedCarZoom = 1;
                TheCamera.m_nCarZoom = 1;
                Enable1stPerson();
                return;
            }
            g_state.storedCarZoom = TheCamera.m_nCarZoom;
        } else {
            if (TheCamera.m_nPedZoom == 3 && g_state.storedPedZoom == 1) {
                g_state.storedPedZoom = 3;
                TheCamera.m_nPedZoom = 1;
                Enable1stPerson();
                return;
            }
            g_state.storedPedZoom = TheCamera.m_nPedZoom;
        }
    }
}

// TODO: consider moving switch into this func
// and renaming it to `UpdateOnfootFlags`?
static auto UpdateFlagsOnfoot(const CPlayerPed* ped) -> int {
    if (CPad::GetPad(0)->bPlayerSafe) {
        return 1;
    }

    // Can rotate the camera, but CJ should not rotate with it
    if (ped->m_pIntelligence->GetTaskClimb() != nullptr || ped->m_pIntelligence->GetUsingParachute()) {
        return 2;
    }

    return 0;
}

static void UpdateFlags(const CPlayerPed* ped) {
    g_state.oldFlags = g_state.flags;

    if (!IsInAnyVehicle(ped)) {
        if (g_state.flags.lockCamera) {
            g_state.cameraRotOnFoot.x = 0.0f; // TODO: cj rotates without this crap
        }
    
        const auto action = UpdateFlagsOnfoot(ped);
        switch (action) {
        case 0:
            g_state.flags.isOnFoot = true;
            if (g_state.oldFlags.isInCar && ped->m_pVehicle == nullptr) {
                g_state.flags.isInCar = false;
            }
            break;
        case 1:
            g_state.flags.dontRotateWithCam = false;
            g_state.flags.lockCamera = true;
            break;
        case 2:
            g_state.flags.dontRotateWithCam = true;
            g_state.flags.lockCamera = true;
            break;
        default:
            return;
        }
    } else {
        if (g_state.flags.lockCamera || g_state.flags.isOnFoot) {
            g_state.cameraRotInCar = MatToEuler(TheCamera.m_mCameraMatrix);
            const auto playerRot = MatToEuler(*ped->m_matrix);
            const auto relativeX = NormalizeAngle(g_state.cameraRotInCar.x - playerRot.x);
            if (relativeX > 180.0f) {
                g_state.cameraRotInCar.x = -(360.0f - relativeX);
            } else {
                g_state.cameraRotInCar.x = relativeX;
            }
            g_state.cameraRotInCar.y = 0.0f;
            g_state.cameraRotInCar.z = 0.0f;
        }

        g_state.flags.lockCamera = false;
        g_state.flags.isOnFoot = false;
        g_state.flags.isInCar = true;
    }
}

// Used to prevent CJ from walking in wrong direction when looking back?
static void PatchCameraOrientedCalculations(bool enable) {
    // push 1 to push 0 for CCamera::CalculateDerivedValues (bOriented = false)
    patch::set<uint8_t>(0x52C97F, enable ? 0 : 1);
}

static void UpdateCameraOnFoot(CPlayerPed* ped) {
    constexpr auto SlerpBodyRotation = [](const CPed* ped) {
        static constexpr auto QUAT_45_DEG_LEFT  = CQuaternion{ 0.38268343f, 0.0f, 0.0f, 0.92387956f}; // xyz:  45.0, 0.0, 0.0
        static constexpr auto QUAT_45_DEG_RIGHT = CQuaternion{-0.38268343f, 0.0f, 0.0f, 0.92387956f}; // xyz: -45.0, 0.0, 0.0

        CQuaternion quat;
        quat.Slerp(g_state.tempLookBackQuat1, g_state.tempLookBackQuat2, g_state.lookBackProgress);
        const auto pos = g_state.unkMat4.m_pos; // todo: remove?
        g_state.unkMat4.SetRotate(quat);
        g_state.unkMat4.m_pos = pos; // todo: remove?
        auto* spine = FindAnimBlendFrameData(ped, BONE_PELVIS)->KeyFrame;
        auto* spine1 = FindAnimBlendFrameData(ped, BONE_SPINE1)->KeyFrame;
        if (g_state.isLookingBackOverRightShoulder) {
            spine->orientation = D3DXQuaternionSlerp(spine->orientation, QUAT_45_DEG_RIGHT, g_state.lookBackProgress);
            spine1->orientation = D3DXQuaternionSlerp(spine1->orientation, QUAT_45_DEG_RIGHT, g_state.lookBackProgress);
        } else {
            spine->orientation = D3DXQuaternionSlerp(spine->orientation, QUAT_45_DEG_LEFT, g_state.lookBackProgress);
            spine1->orientation = D3DXQuaternionSlerp(spine1->orientation, QUAT_45_DEG_LEFT, g_state.lookBackProgress);
        }
    };

    const auto delta = GetTimeDelta() * 5.0f; // 5 per second

    const auto headingZ = GetHeadingFromXY(ped->m_matrix->m_forward.x, ped->m_matrix->m_forward.y);

    // look back
    if (TheCamera.m_aCams[0].m_nDirectionWasLooking == 0) {
        if (g_state.lookBackProgress == 0.0f) {
            PatchCameraOrientedCalculations(true);
            const auto zDeg = NormalizeAngle(headingZ - 179.0f);
            g_state.isLookingBackOverRightShoulder = NormalizeAngle(g_state.cameraRotOnFoot.z - zDeg) < 180.0f;
            CMatrix_SetRotateOnlyDeg(g_state.tempLookBackMat1, g_state.cameraRotOnFoot.x, 0.0f, g_state.cameraRotOnFoot.z);
            CMatrix_SetRotateOnlyDeg(g_state.tempLookBackMat2, 0.0f, 0.0f, zDeg);
            g_state.tempLookBackQuat1 = D3DXQuaternionRotationMatrix(g_state.tempLookBackMat1);
            g_state.tempLookBackQuat2 = D3DXQuaternionRotationMatrix(g_state.tempLookBackMat2);
            g_state.lookBackProgress = std::min(g_state.lookBackProgress + delta, 1.0f);
        } else {
            g_state.lookBackProgress += delta;
            if (g_state.lookBackProgress > 1.0f) {
                g_state.lookBackProgress = 1.0f;
                CMatrix_SetRotateOnlyDeg(g_state.tempLookBackMat1, 0.0f, 0.0f, g_state.cameraRotOnFoot.z);
                g_state.tempLookBackQuat1 = D3DXQuaternionRotationMatrix(g_state.tempLookBackMat1);
                g_state.cameraRotOnFoot.x = 0.0f;
            }
        }
        SlerpBodyRotation(ped);
        return;
    }
    
    if (g_state.lookBackProgress > 0.0f) {
        g_state.lookBackProgress -= delta;
        if (g_state.lookBackProgress <= 0.0f) {
            g_state.lookBackProgress = 0.0f;
            PatchCameraOrientedCalculations(false);
        }

        SlerpBodyRotation(ped);
        return;
    }
    // look back
    
    const auto [offsetX, offsetY] = [&] {
        if (FrontEndMenuManager.m_nController) {
            const auto* pad = CPad::GetPad(CPad__padNumber);
            const auto offsetX = static_cast<float>(pad->NewState.RightStickX) * -0.02f * g_settings.sensitivity;
            const auto offsetY = static_cast<float>(pad->NewState.RightStickY) * -0.02f * g_settings.sensitivity;
            return std::tuple{offsetX, offsetY};
        } else {
            const auto offsetX = CPad::NewMouseControllerState.x * -0.1f * g_settings.sensitivity;
            const auto offsetY = CPad::NewMouseControllerState.y *  0.1f * g_settings.sensitivity;
            return std::tuple{offsetX, offsetY};
        }
    }();
    
    g_state.cameraRotOnFoot.x = std::clamp(g_state.cameraRotOnFoot.x + offsetY, -70.0f, 80.0f);
    
    float v70{};
    if (CPad::NewMouseControllerState.x < 0.0f) {
        v70 = NormalizeAngle(headingZ + 90.0f);
        
        if (offsetX > NormalizeAngle(v70 - g_state.cameraRotOnFoot.z)) {
            if (g_settings.rotateWithCamera && !g_state.flags.lockCamera) {
                g_state.cameraRotOnFoot.z += offsetX;
                SetPedHeading(ped, NormalizeAngle(g_state.cameraRotOnFoot.z - 90.0f));
            } else {
                g_state.cameraRotOnFoot.z = v70;
            }
        } else {
            g_state.cameraRotOnFoot.z += offsetX;
        }
    } else if (CPad::NewMouseControllerState.x > 0.0f) {
        v70 = NormalizeAngle(headingZ - 90.0f);

        if (-offsetX <= NormalizeAngle(g_state.cameraRotOnFoot.z - v70)) {
            g_state.cameraRotOnFoot.z += offsetX;
        } else if (g_settings.rotateWithCamera && !g_state.flags.lockCamera) {
            g_state.cameraRotOnFoot.z += offsetX;
            SetPedHeading(ped, NormalizeAngle(g_state.cameraRotOnFoot.z + 90.0f));
        } else {
            g_state.cameraRotOnFoot.z = v70;
        }
    } else {
        g_state.cameraRotOnFoot.z += offsetX;
    }
    
    //if (CPad::NewMouseControllerState.X != 0.0f && false) {
    //auto v57 = NormalizeAngle(v70 - g_state.cameraRotOnFoot.z);
    //if (v57 > 90.0f && v57 < 270.0f) {
    //    float v58;
    //    if (v57 < 180.0f || v57 == 180.0f) {
    //        v58 = v70 - 90.0f;
    //    } else {
    //        v58 = v70 + 90.0f;
    //    }
    //    g_state.cameraRotOnFoot.z = NormalizeAngle(v58);
    //}
    //}

    //const auto v57 = NormalizeAngle(v70 - g_state.cameraRotOnFoot.z);
    //if (v57 > 90.0f && v57 < 270.0f) {
    //    const auto v58 = v57 <= 180.0f
    //        ? v70 - 90.0f
    //        : v70 + 90.0f;
    //    g_state.cameraRotOnFoot.z = NormalizeAngle(v58);
    //}

    const auto headMat = GetHeadMatrix(ped);
    
    CMatrix v72; // TODO: CMatrix::From?
    CMatrix_SetRotateOnlyDeg(v72, NormalizeAngle(g_state.cameraRotOnFoot.x), 0.0f, g_state.cameraRotOnFoot.z);
    v72.m_pos = ped->m_matrix->m_pos;
    
    const auto v59 = v72.TransformPoint(CVector{ 0.0f, 10.0f, 0.0f });
    const auto v60 = v59 - headMat.m_pos;
    
    const auto v62 = v60.Magnitude2D();
    const auto v63 = GetHeadingFromXY(v62, v60.z) + 90.0f;
    g_state.unkVec3.x = NormalizeAngle(v63);
    g_state.unkVec3.y = 0.0f;
    g_state.unkVec3.z = GetHeadingFromXY(v60.x, v60.y);

    CMatrix_SetRotateOnlyDeg(g_state.unkMat4, NormalizeAngle(g_state.cameraRotOnFoot.x), 0.0f, g_state.cameraRotOnFoot.z);
    g_state.unkMat4.m_pos = headMat.m_pos;
}

static auto IsVehicleWithoutRearView(CVehicle* vehicle) -> bool {
    const auto appearance = vehicle->GetVehicleAppearance();
    return appearance == VEHICLE_APPEARANCE_HELI
        || appearance == VEHICLE_APPEARANCE_BOAT
        || appearance == VEHICLE_APPEARANCE_PLANE;
}

static void BlendLookBackAnimation(std::span<const CQuaternion, 11> bones, float t) {
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

    const auto* playerPed = CWorld::Players[0].m_pPed;
    
    for (size_t i = 0; i < BONES.size(); i++) {
        auto* frame = FindAnimBlendFrameData(playerPed, BONES.at(i))->KeyFrame;
        frame->orientation = D3DXQuaternionSlerp(frame->orientation, bones[i], t);
    }
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

static auto HasDriveByAnimBlendAssociation(const CPed* ped) -> bool {
    auto* rwClump = ped->m_pRwClump;

    return RpAnimBlendClumpGetAssociation(rwClump, "DrivebyL_L") != nullptr
        || RpAnimBlendClumpGetAssociation(rwClump, "DrivebyL_R") != nullptr
        || RpAnimBlendClumpGetAssociation(rwClump, "Driveby_L") != nullptr
        || RpAnimBlendClumpGetAssociation(rwClump, "Driveby_R") != nullptr
        || RpAnimBlendClumpGetAssociation(rwClump, "Biked_DrivebyLHS") != nullptr
        || RpAnimBlendClumpGetAssociation(rwClump, "Biked_DrivebyRHS") != nullptr
        || RpAnimBlendClumpGetAssociation(rwClump, "Bikeh_DrivebyLHS") != nullptr
        || RpAnimBlendClumpGetAssociation(rwClump, "Bikeh_DrivebyRHS") != nullptr
        || RpAnimBlendClumpGetAssociation(rwClump, "Bikes_DrivebyLHS") != nullptr
        || RpAnimBlendClumpGetAssociation(rwClump, "Bikes_DrivebyRHS") != nullptr
        || RpAnimBlendClumpGetAssociation(rwClump, "Bikev_DrivebyLHS") != nullptr
        || RpAnimBlendClumpGetAssociation(rwClump, "Bikev_DrivebyRHS") != nullptr
        || RpAnimBlendClumpGetAssociation(rwClump, "BMX_Driveby_LHS") != nullptr
        || RpAnimBlendClumpGetAssociation(rwClump, "BMX_Driveby_RHS") != nullptr
        || RpAnimBlendClumpGetAssociation(rwClump, "Biked_DrivebyFT") != nullptr
        || RpAnimBlendClumpGetAssociation(rwClump, "Bikeh_DrivebyFT") != nullptr
        || RpAnimBlendClumpGetAssociation(rwClump, "Bikes_DrivebyFT") != nullptr
        || RpAnimBlendClumpGetAssociation(rwClump, "Bikev_DrivebyFT") != nullptr
        || RpAnimBlendClumpGetAssociation(rwClump, "BMX_DrivebyFT") != nullptr;
}

static void UpdateCameraInVehicle() {
    auto* playerPed = CWorld::Players[0].m_pPed;
    auto* vehicle = playerPed->m_pVehicle; // todo: const?

    const auto label_111 = [&]() {
        auto* headBlend = FindAnimBlendFrameData(playerPed, BONE_NECK)->KeyFrame;
        const auto& offset = g_settings.GetVehicleOffset(vehicle->m_nModelIndex);
        headBlend->translation.x += offset.z;
        headBlend->translation.y += offset.y;
        headBlend->translation.z -= offset.x;

        const auto headMat = GetHeadMatrix(playerPed);

        CMatrix cameraRot;
        const auto zDeg = NormalizeAngle(g_state.cameraRotInCar.z);
        const auto xDeg = NormalizeAngle(g_state.cameraRotInCar.x);
        CMatrix_SetRotateOnlyDeg(cameraRot, xDeg, 0.0f, zDeg); // TODO: no point keeping pos - it's 0 by default // TODO: CMatrix::FromRotation()?
        g_state.unkMat6 = *vehicle->m_matrix * cameraRot;
        g_state.unkMat6.m_pos = headMat.m_pos;

        if (TheCamera.m_aCams[0].m_nMode != MODE_AIMWEAPON_FROMCAR && !HasDriveByAnimBlendAssociation(playerPed)) {
            patch::copy_slice(0x6B8559, {0x0F, 0x84}); // if (this->m_bPedLeftHandFixed)
            patch::copy_slice(0x6B8202, {0x0F, 0x84}); // if (this->m_bPedRightHandFixed)
        
            const auto appearance = vehicle->GetVehicleAppearance();
            const auto v88 = appearance == VEHICLE_APPEARANCE_BIKE ? 90.0f : 60.0f;
        
            if (v88 < g_state.cameraRotInCar.z || -v88 > g_state.cameraRotInCar.z) {
                if (appearance == VEHICLE_APPEARANCE_BIKE) {
                    if (g_state.cameraRotInCar.z >= 0.0f) {
                        const auto blendValue = (g_state.cameraRotInCar.z - v88) / 90.0f;
                        g_state.slerpLookBackAnimInCar = blendValue; // TODO: inline
                        BlendLookBackAnimation(QUATS_BIKE_LOOK_LEFT, blendValue);
                        patch::copy_slice(0x6B8559, {0x90, 0xE9}); // Disable left hand fixing; TODO: move to hook
                    } else {
                        const auto blendValue = (-g_state.cameraRotInCar.z - v88) / 90.0f;
                        g_state.slerpLookBackAnimInCar = blendValue; // TODO: inline
                        BlendLookBackAnimation(QUATS_BIKE_LOOK_RIGHT, blendValue);
                        patch::copy_slice(0x6B8202, {0x90, 0xE9}); // Disable right hand fixing; TODO: move to hook
                    }
                } else if (g_state.cameraRotInCar.z >= 0.0f) {
                    const auto blendValue = (g_state.cameraRotInCar.z - v88) / 80.0f; // TODO: why 80?
                    BlendLookBackAnimation(QUATS_CAR_LOOK_LEFT, blendValue);
                    
                    g_state.slerpLookBackAnimInCar = std::min(blendValue, 1.0f);

                    if (g_state.slerpLookBackAnimInCar > 0.0f) {
                        static constexpr auto result = CVector{-0.3f, 0.0f, 0.113f};
                        static constexpr auto v90 = CQuaternion{0.11877283f, -0.19613053f, 0.67650485f, 0.6998335f};
                        auto* frame = FindAnimBlendFrameData(playerPed, BONE_ROOT)->KeyFrame;
                        frame->translation = Lerp(frame->translation, result, g_state.slerpLookBackAnimInCar);
                        frame->orientation = D3DXQuaternionSlerp(frame->orientation, v90, g_state.slerpLookBackAnimInCar);
                    } else {
                        g_state.slerpLookBackAnimInCar = 0.0f;
                    }
                } else {
                    const auto blendValue = (-g_state.cameraRotInCar.z - v88) / 80.0f; // TODO: why 80?
                    g_state.slerpLookBackAnimInCar = blendValue;
                    BlendLookBackAnimation(QUATS_CAR_LOOK_RIGHT, g_state.slerpLookBackAnimInCar);
                    auto* frame = FindAnimBlendFrameData(playerPed, BONE_ROOT)->KeyFrame;
                    static constexpr auto end = CVector{ 0.3f, 0.0f, 0.0f };
                    frame->translation = Lerp(frame->translation, end, g_state.slerpLookBackAnimInCar);
                }
            }
        }
    };

    if (TheCamera.m_aCams[0].m_nMode != MODE_AIMWEAPON_FROMCAR
        && (TheCamera.m_aCams[0].m_bLookingLeft
            || TheCamera.m_aCams[0].m_bLookingRight
            || TheCamera.m_aCams[0].m_bLookingBehind
            || g_state.lookBackProgress > 0.0f)
        && playerPed->m_ePedState != PEDSTATE_NONE
    ) {
        if (g_state.lookBackProgress == 0.0f) {
            g_state.unusedVec = g_state.cameraRotInCar;
        }

        const auto deltaVer = GetTimeDelta() * 180.0f; // 180 degrees per second
        const auto deltaHor = deltaVer * 4.0f; // 720 degrees per second
        
        if (TheCamera.m_aCams[0].m_bLookingBehind && !IsVehicleWithoutRearView(vehicle)) {
            g_state.cameraRotInCar.z = std::max(g_state.cameraRotInCar.z - deltaHor, -180.0f);

            if (g_state.cameraRotInCar.x > 0.0f) {
                g_state.cameraRotInCar.x = std::max(g_state.cameraRotInCar.x - deltaVer, 0.0f);
            } else if (g_state.cameraRotInCar.x < 0.0f) {
                g_state.cameraRotInCar.x = std::min(g_state.cameraRotInCar.x + deltaVer, 0.0f);
            }

            g_state.lookBackProgress = 1.0f;

            label_111();
            return;
        }

        if (TheCamera.m_aCams[0].m_bLookingLeft && g_settings.rotateAtSidesInCar) {
            if (g_state.cameraRotInCar.z > 90.0f) {
                g_state.cameraRotInCar.z = std::max(g_state.cameraRotInCar.z - deltaHor, 90.0f);
            } else if (g_state.cameraRotInCar.z < 90.0f) {
                g_state.cameraRotInCar.z = std::min(g_state.cameraRotInCar.z + deltaHor, 90.0f);
            }
      
            if (g_state.cameraRotInCar.x > 0.0f) {
                g_state.cameraRotInCar.x = std::max(g_state.cameraRotInCar.x - deltaVer, 0.0f);
            } else if (g_state.cameraRotInCar.x < 0.0f) {
                g_state.cameraRotInCar.x = std::min(g_state.cameraRotInCar.x + deltaVer, 0.0f);
            }

            g_state.lookBackProgress = 0.5f;
            label_111();
            return;
        }

        if (TheCamera.m_aCams[0].m_bLookingRight && g_settings.rotateAtSidesInCar) {
            if (g_state.cameraRotInCar.z < -90.0f) {
                g_state.cameraRotInCar.z = std::min(g_state.cameraRotInCar.z + deltaHor, -90.0f);
            } else if (g_state.cameraRotInCar.z > -90.0f) {
                g_state.cameraRotInCar.z = std::max(g_state.cameraRotInCar.z - deltaHor, -90.0f);
            }

            if (g_state.cameraRotInCar.x > 0.0f) {
                g_state.cameraRotInCar.x = std::max(g_state.cameraRotInCar.x - deltaVer, 0.0f);
            } else if (g_state.cameraRotInCar.x < 0.0f) {
                g_state.cameraRotInCar.x = std::min(g_state.cameraRotInCar.x + deltaVer, 0.0f);
            }
            
            g_state.lookBackProgress = 0.5f;
            label_111();
            return;
        }
    
        if (g_state.lookBackProgress > 0.0f) {
            if (g_state.cameraRotInCar.z < 0.0f) {
                g_state.cameraRotInCar.z += deltaHor;
                if (g_state.cameraRotInCar.z >= 0.0f) {
                    g_state.cameraRotInCar.z = 0.0f;
                    g_state.lookBackProgress = 0.0f;
                }
            }
      
            if (g_state.cameraRotInCar.z > 0.0f) {
                g_state.cameraRotInCar.z -= deltaHor;
                if (g_state.cameraRotInCar.z <= 0.0f) {
                    g_state.cameraRotInCar.z = 0.0f;
                    g_state.lookBackProgress = 0.0f;
                }
            }
        }
    } else {
        g_state.lookBackProgress = 0.0f;
        if (FrontEndMenuManager.m_nController != 0) {
            const auto* pad = CPad::GetPad(CPad__padNumber);
            g_state.cameraRotInCar.z -= static_cast<float>(pad->NewState.RightStickX) * 0.02f * g_settings.sensitivity;
            g_state.cameraRotInCar.x -= static_cast<float>(pad->NewState.RightStickY) * 0.02f * g_settings.sensitivity;
        } else {
            g_state.cameraRotInCar.z -= CPad::NewMouseControllerState.x * 0.1f * g_settings.sensitivity;
            g_state.cameraRotInCar.x += CPad::NewMouseControllerState.y * 0.1f * g_settings.sensitivity;
        }
    
        if (TheCamera.m_aCams[0].m_nMode == MODE_AIMWEAPON_FROMCAR) {
            if (g_state.cameraRotInCar.z > 180.0f) {
                g_state.cameraRotInCar.z = -(360.0f - NormalizeAngle(g_state.cameraRotInCar.z));
            }
            if (g_state.cameraRotInCar.z < 180.0f) {
                g_state.cameraRotInCar.z = NormalizeAngle(g_state.cameraRotInCar.z);
            }
        } else {
            // Limit horizontal camera angle to [-180.0f, 180.0f]
            g_state.cameraRotInCar.z = std::clamp(g_state.cameraRotInCar.z, -180.0f, 180.0f);
        }
    
        // Limit vertical camera angle to [-70.0f, 80.0f]
        g_state.cameraRotInCar.x = std::clamp(g_state.cameraRotInCar.x, -70.0f, 80.0f);

        // Limit horizontal camera angle to [-90.0f, 90.0f] for certain vehicle types or when getting out of car
        if (playerPed->m_ePedState == PEDSTATE_NONE || IsVehicleWithoutRearView(vehicle)) {
            g_state.cameraRotInCar.z = std::clamp(g_state.cameraRotInCar.z, -90.0f, 90.0f);
        }
    }

    label_111();
}

// TODO: refactor, rename
static void HandleCameraLock() {
    auto* playerPed = CWorld::Players[0].m_pPed;

    if (g_state.flags.dontRotateWithCam) {
        g_state.lockedCamX -= CPad::NewMouseControllerState.x * CCamera::m_fMouseAccelHorzntal * 80.0f;
        g_state.lockedCamY += CPad::NewMouseControllerState.y * CCamera::m_fMouseAccelHorzntal * 80.0f;
        
        g_state.lockedCamY = std::clamp(g_state.lockedCamY, -60.0f, 60.0f);
        g_state.lockedCamX = std::clamp(g_state.lockedCamX, -90.0f, 90.0f);
    } else {
        const auto delta = GetTimeDelta() * 90.0f; // 90 degrees per second

        if (g_state.lockedCamY < 0.0f) {
            g_state.lockedCamY = std::min(g_state.lockedCamY + delta, 0.0f);
        } else if (g_state.lockedCamY > 0.0f) {
            g_state.lockedCamY = std::max(0.0f, g_state.lockedCamY - delta);
        }

        if (g_state.lockedCamX < 0.0f) {
            g_state.lockedCamX = std::min(g_state.lockedCamX + delta, 0.0f);
        } else if (g_state.lockedCamX > 0.0f) {
            g_state.lockedCamX = std::max(0.0f, g_state.lockedCamX - delta);
        }
    }

    const auto headMat = GetHeadMatrix(playerPed);
    CMatrix camMat;
    CMatrix_SetRotateOnlyDeg(camMat, NormalizeAngle(g_state.lockedCamY), 0.0f, NormalizeAngle(g_state.lockedCamX));
    g_state.unkMat7 = headMat * camMat;
    g_state.unkMat7.m_pos = headMat.m_pos;
}

// TODO: refactor, rename
static void sub_100032F0() {
    const auto label_22 = []() {
        if (g_state.flags.lockCamera) {
            if (!g_state.oldFlags.lockCamera && g_state.oldFlags.isInCar) {
                g_state.unkFloatForSlerp = 1.0f;
                
                g_state.flags.isInCar = false;
                g_state.flags.unk_x20 = false;
                g_state.flags.unk_slippedOffCurb_x40 = false;
                // g_state.flags.lockCamera = true; // todo: pointless: already set
                
                g_state.cameraMat = g_state.unkMat7;
            }
        }
    };

    const auto label_26 = []() {
        if (g_state.unkFloatForSlerp < 1.0f) {
            CQuaternion start;
            CQuaternion end;
            if (g_state.flags.unk_x20) {
                start = D3DXQuaternionRotationMatrix(g_state.unkMat4);
                end = D3DXQuaternionRotationMatrix(g_state.unkMat7);
            } else if (g_state.flags.unk_slippedOffCurb_x40) {
                start = D3DXQuaternionRotationMatrix(g_state.unkMat7);
                end = D3DXQuaternionRotationMatrix(g_state.unkMat4);
            }

            CQuaternion a1;
            a1.Slerp(start, end, g_state.unkFloatForSlerp);
            g_state.cameraMat.SetRotate(a1);
            g_state.cameraMat.m_pos = g_state.unkMat7.m_pos;
            return;
        }

        if (g_state.flags.isOnFoot && g_state.oldFlags.isOnFoot) {
            g_state.cameraMat = g_state.unkMat4;
        }
        if (g_state.flags.lockCamera) {
            g_state.cameraMat = g_state.unkMat7;
        }
        if (g_state.flags.isInCar) {
            g_state.cameraMat = g_state.unkMat6;
        }
    };

    if (g_state.unkFloatForSlerp < 1.0f) {
        const auto delta = GetTimeDelta() * 5.0f; // 5 per second
        g_state.unkFloatForSlerp += delta;
        
        if (g_state.unkFloatForSlerp >= 1.0f) {
            g_state.unkFloatForSlerp = 1.0f;
            if (g_state.flags.unk_slippedOffCurb_x40) {
                g_state.flags.lockCamera = false;
                g_state.flags.dontRotateWithCam = false;
            } else {
                if (g_state.flags.unk_x20) {
                    g_state.flags.isOnFoot = false;
                }
            }
            g_state.flags.unk_x20 = false;
            g_state.flags.unk_slippedOffCurb_x40 = false;
        }
        label_26();
        return;
    }

    if (g_state.flags.lockCamera && !g_state.oldFlags.lockCamera && g_state.oldFlags.isOnFoot) {
        g_state.flags.unk_x20 = true;
        g_state.unkFloatForSlerp = 1.0f - g_state.unkFloatForSlerp;
        label_26();
        return;
    }

    if (!g_state.flags.isOnFoot) {
        label_22();
        label_26();
        return;
    }

    if (g_state.oldFlags.isOnFoot || !g_state.oldFlags.lockCamera) {
        if (g_state.flags.isOnFoot) {
            if (!g_state.oldFlags.isOnFoot && g_state.oldFlags.isInCar) {
                g_state.unkFloatForSlerp = 1.0f;

                g_state.flags.isInCar = false;
                g_state.flags.unk_x20 = false;
                g_state.flags.unk_slippedOffCurb_x40 = false;
                // g_state.flags.isOnFoot = true; // todo: kinda pointless? already set

                InitFirstPersonCamera();
                g_state.cameraMat = g_state.unkMat6;
                label_26();
                return;
            }
        }

        label_22();
        label_26();
        return;
    }
    
    g_state.flags.unk_slippedOffCurb_x40 = true;
    g_state.unkFloatForSlerp = 1.0f - g_state.unkFloatForSlerp;
    InitFirstPersonCamera();
    g_state.cameraRotOnFoot.x = 0.0f;

    label_26();
}

// TODO: name vars, refactor?
static void UpdateTheCamera(const CMatrix& mat) {
    TheCamera.m_mCameraMatrix = mat;
    TheCamera.m_mCameraMatrix.m_right *= -1.0f;
    TheCamera.m_aCams[0].m_vecSource = mat.m_pos;
    TheCamera.m_aCams[0].m_vecUp     = mat.m_up;
    TheCamera.m_aCams[0].m_vecFront  = mat.m_forward;

    const auto out = MatToEuler(mat);

    const auto horizontal = out.z > 90.0f || out.z < 270.0f
        ? out.z * DEG_TO_RAD
        : (360.0f - out.z) * -DEG_TO_RAD;
    TheCamera.m_aCams[0].m_fHorizontalAngle = horizontal - FRAC_PI_2;

    TheCamera.m_aCams[0].m_fVerticalAngle = out.x < 180.0f || out.x > 360.0f
        ? out.x * DEG_TO_RAD
        : (360.0f - out.x) * -DEG_TO_RAD;
}

// todo: const?
static void SetHeadRotation(CPed* ped, const CMatrix& mat) {
    const auto* neckMat = FindBoneMatrix(ped, BONE_UPPERTORSO);
    const auto invNeckMat = reinterpret_cast<const CMatrix*>(neckMat)->Inverted();
    auto localRot = invNeckMat * mat;
    
    // Align axes TODO DESCRIPTION
    // Rotate 90 degrees left (otherwise the head will lay on the right shoulder)
    const auto left = localRot.GetLeft();
    localRot.m_right = localRot.m_up;
    localRot.m_up = left;
    
    auto* headFrame = FindAnimBlendFrameData(ped, BONE_NECK)->KeyFrame;
    headFrame->orientation = D3DXQuaternionRotationMatrixRt(localRot);
}
















static safetyhook::InlineHook Orig_CCam__Process;
static void CALLCONV_FASTCALL Hook_CCam__Process(CCam* self, uintptr_t /*edx*/) {
    // Handle first person mode switching and hotkeys
    Update1stPersonState();
    // HandleGUIHotkeys(); // todo?

    Orig_CCam__Process.unsafe_thiscall(self);

    if (!g_state.isFirstPersonOn) {
        return;
    }

    /////////////////////////

    auto* playerPed = CWorld::Players[0].m_pPed;

    UpdateFlags(playerPed);

    if (g_state.flags.isOnFoot) {
        UpdateCameraOnFoot(playerPed);
    }

    if (g_state.flags.isInCar) {
        UpdateCameraInVehicle();
    }

    if (g_state.flags.lockCamera) {
        HandleCameraLock();
    }

    sub_100032F0();

    /////////////////////////

    SetHeadRotation(playerPed, g_state.cameraMat);

    TheCamera.m_bUseNearClipScript = true;
    TheCamera.m_fNearClipScript = g_settings.nearClip;

    const auto camMode = TheCamera.m_aCams[0].m_nMode;
    if (camMode != MODE_SNIPER && camMode != MODE_CAMERA) {
        CDraw::ms_fFOV = g_settings.fov;
        TheCamera.m_aCams[0].m_fFOV = g_settings.fov;
    }
}

static void Hook_CMirrors__BeforeConstructRenderList() {
    if (g_state.isFirstPersonOn) {
        auto* playerPed = CWorld::Players[0].m_pPed;

        auto camOffset = g_settings.GetPedOffset(playerPed->m_nModelIndex);
        camOffset.y += 0.09f;
        camOffset.z += 0.09f;

        const auto headMat = GetHeadMatrix(playerPed);
        g_state.cameraMat.m_pos = headMat.TransformPoint(camOffset);
        UpdateTheCamera(g_state.cameraMat);
    }

    CMirrors::BeforeConstructRenderList();
}

static safetyhook::InlineHook Orig_CPed__PreRenderAfterTest;
static void CALLCONV_FASTCALL Hook_CPed__PreRenderAfterTest(CPed* self, uintptr_t /*edx*/) {
    Orig_CPed__PreRenderAfterTest.unsafe_thiscall(self);

    if (!g_state.isFirstPersonOn || self != CWorld::Players[0].m_pPed) {
        return;
    }

    auto camOffset = g_settings.GetPedOffset(self->m_nModelIndex);
    camOffset.y += 0.09f;
    camOffset.z += 0.09f;

    const auto headMat = GetHeadMatrix(self);
    g_state.cameraMat.m_pos = headMat.TransformPoint(camOffset);
    UpdateTheCamera(g_state.cameraMat);

    TheCamera.CopyCameraMatrixToRWCam(false);
}

static safetyhook::InlineHook Orig_CEntity__GetIsOnScreen;
static bool CALLCONV_FASTCALL Hook_CEntity__GetIsOnScreen(CEntity* self) {
    if (self == CWorld::Players[0].m_pPed) {
        return true;
    }

    return Orig_CEntity__GetIsOnScreen.unsafe_thiscall<bool>(self);
}























namespace first_person {

extern void ReadConfig(const Config& config) {
    // config.Deserialize("first-person", settings);
}

extern void Apply() {
    //patches.call(0x53C0DF, hooks_asi::Hook_CPopCycle__Update);
    // AddHook(Before_CInformFriendsEventQueue__Init, InitSomeShit);// before CInformFriendsEventQueue::Init // 0x469376
    //patches.call(0x52B90A, hooks_asi::Hook_CCam__Process);
    //patches.call(0x53BEF0, hooks_asi::Hook_CLoadMonitor__BeginFrame);
    //patches.nop(0x5E8A27, 2); // Part of PreRenderAfterTest hook
    //patches.call(0x5E8A29, hooks_asi::Hook_CPed__PreRenderAfterTest);

    // Always return true for player from CEntity::GetIsOnScreen
    Orig_CEntity__GetIsOnScreen = safetyhook::create_inline(0x534540, Hook_CEntity__GetIsOnScreen);

    // NOP `!this->m_nFlags.m_bOffscreen` check in CVehicle::ProcessDrivingAnims
    // Keeps updating driving anims in first person
    patch::nop(0x6DF4AF, 6);

    // Main hook
    Orig_CCam__Process = safetyhook::create_inline(0x526FC0, Hook_CCam__Process);
    // Sort of a fix for mirrors. Still bad.
    patch::call(0x555854, Hook_CMirrors__BeforeConstructRenderList);
    // TODO
    Orig_CPed__PreRenderAfterTest = safetyhook::create_inline(0x5E65A0, Hook_CPed__PreRenderAfterTest);
}

}
