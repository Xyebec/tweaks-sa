#include "Font.h"
#include "MenuManager.h"
#include "Radar.h"
#include "Text.h"
#include "config.h"
#include "Vector2D.h"
#include "patcher.h"
#include "safetyhook/context.hpp"
#include "safetyhook/easy.hpp"

static auto TransformScreenSpaceToRadarPoint(const CVector2D& in) -> CVector2D {
    return CVector2D{
        (in.x - FrontEndMenuManager.m_fMapBaseX) / FrontEndMenuManager.m_fMapZoom,
        (FrontEndMenuManager.m_fMapBaseY - in.y) / FrontEndMenuManager.m_fMapZoom,
    };
}

static auto TransformRadarSpaceToRealWorldPoint(const CVector2D& in) -> CVector2D {
    CVector2D out;
    CRadar::TransformRadarPointToRealWorldSpace(out, in);
    return out;
}

static struct FancyMap {
    bool enabled;
    bool remove_bg;
    bool drag_to_move;
    bool fix_speed; // TODO: add speed multiplier var instead
    bool cursor_without_crosshair;
    bool hide_help_text;
} settings;

namespace fancy_map {

extern void ReadConfig(const Config& config) {
    config.Deserialize("fancy-map", settings);
}

extern void Apply() {
    if (!settings.enabled) {
        return;
    }

    // Remove the gray outline
    patch::nop(0x575BF6, 5); // Top
    patch::nop(0x575C40, 5); // Bottom
    patch::nop(0x575C84, 5); // Left
    patch::nop(0x575CCE, 5); // Right

    // Remove the black frame
    patch::nop(0x575D1F, 5); // Top
    patch::nop(0x575D6F, 5); // Bottom
    patch::nop(0x575DC2, 5); // Left
    patch::nop(0x575E12, 5); // Right

    // Extend limits of the `CRadar::DrawRadarSectionMap`
    patch::set<uint8_t>(0x575301, 0xEB); // Bottom (skip RsGlobal.maximumHeight == 448)
    patch::nop(0x575311, 6); // Bottom // TODO: shifts the zone name
    patch::set<uint8_t>(0x575351, 0xEB); // Right (skip RsGlobal.maximumWidth == 640)
    patch::nop(0x575361, 6); // Right
    patch::set<uint8_t>(0x5754D6, 0xEB); // Left (skip RsGlobal.maximumWidth == 640)
    patch::nop(0x5754EC, 6); // Left
    patch::set<uint8_t>(0x575521, 0xEB); // Top (skip RsGlobal.maximumHeight == 448)
    patch::nop(0x575537, 6); // Top

    // Keep rendering the 'crosshair' even when cursor is out of (previously removed) bounds
    patch::jmp(0x5881BE, 0x5881E5);

    // Do not limit 'crosshair' position
    // patch::nop(0x58822D, 5); // TODO: SilentPatch conflict: https://github.com/CookiePLMonster/SilentPatch/blob/4c0256e90beb1ef4025553cdb31bfd7ac71ae1ed/SilentPatchSA/SilentPatchSA.cpp#L5086

    // Skip checking cursor rendering bounds when dragging
    patch::jmp(0x57C00B, 0x57C043);
        
    // Expand the clickable area of the map
    // Also keeps updating the 'crosshair' position (`m_vMousePos` (world space))
    patch::nop(0x577C89, 6); // Top
    patch::nop(0x577CA2, 6); // Bottom
    patch::nop(0x577CC5, 6); // Left
    patch::nop(0x577CDE, 6); // Right

    if (settings.hide_help_text) {
        // Remove map help text
        patch::nop(0x5762E7, 10); // FEH_MPB (blip options menu)
        patch::nop(0x5762FD, 10); // FEH_MPH (general map help)
    } else {
        struct Hook {
            static void CALLCONV_FASTCALL CMenuManager__DisplayHelperText(CMenuManager* self, uintptr_t /*edx*/, const char* key) {
                const auto scaleX = self->StretchX(0.4f);
                const auto scaleY = self->StretchY(0.5f);
                CFont::SetScale(scaleX, scaleY);
                CFont::SetFontStyle(eFontStyle::FONT_MENU);
                CFont::SetOrientation(eFontAlignment::ALIGN_RIGHT);
                CFont::SetEdge(1);
                CFont::SetColor(CRGBA{0xFF, 0xFF, 0xFF, 0xFF});
                
                const auto x = static_cast<float>(RsGlobal.maximumWidth) - self->StretchX(30.0f);
                const auto y = static_cast<float>(RsGlobal.maximumHeight) - self->StretchY(10.0f);
                const auto* text = TheText.Get(key);
                CFont::PrintStringFromBottom(x, y, text);
            }

            static void CFont__PrintString(float /*x*/, float /*y*/, const char* text) {
                const auto x = static_cast<float>(RsGlobal.maximumWidth) - FrontEndMenuManager.StretchX(30.0f);
                const auto y = static_cast<float>(RsGlobal.maximumHeight) - FrontEndMenuManager.StretchY(80.0f);
                CFont::PrintString(x, y, text);
            }
        };
        
        // Unfortunately, simply setting `CFont::SetEdge` to `1` in `CMenuManager::DisplayHelperText`
        // causes a segfault at 0x7FBD4A upon exiting the redefine controls menu,
        // so it's safer to hook like this

        // Add outline to the helper text
        patch::call(0x5762EC, Hook::CMenuManager__DisplayHelperText); // FEH_MPB (blip options menu)
        patch::call(0x576302, Hook::CMenuManager__DisplayHelperText); // FEH_MPH (general map help)

        // Fix position for the zone name
        patch::call(0x575F89, Hook::CFont__PrintString);
    }

    if (settings.remove_bg) {
        // Remove the blue map background
        patch::nop(0x57549D, 5);
        // Remove the black rectangle for the "Please wait..." text
        patch::nop(0x575469, 5);
    }

    if (settings.drag_to_move) {
        static auto hook = safetyhook::create_mid(0x577D43, [](safetyhook::Context& ctx) {
            const auto cursorDeltaX = static_cast<float>(FrontEndMenuManager.m_nMousePosX - FrontEndMenuManager.m_nMouseOldPosX);
            const auto cursorDeltaY = static_cast<float>(FrontEndMenuManager.m_nMousePosY - FrontEndMenuManager.m_nMouseOldPosY);

            FrontEndMenuManager.m_fMapBaseX += cursorDeltaX / (static_cast<float>(RsGlobal.maximumWidth) / 640.0f);
            FrontEndMenuManager.m_fMapBaseY += cursorDeltaY / (static_cast<float>(RsGlobal.maximumHeight) / 448.0f);

            ctx.eip = 0x578785;
        });
    }

    // TODO: now speed is fps dependant
    if (settings.fix_speed) {
        // Remove 20ms map zoom delay
        patch::nop(0x577659, 6); // In
        patch::nop(0x57798C, 2); // Out
        // Remove 20ms map base movement delay (far from borders)
        patch::nop(0x577E5D, 2); // Right
        patch::nop(0x57830B, 2); // Left
        patch::nop(0x5784A2, 2); // Down
        patch::nop(0x57863B, 2); // Up
        // Remove 20ms 'crosshair' movement delay (near borders)
        patch::nop(0x578283, 2); // Right
        patch::nop(0x578416, 2); // Left
        patch::nop(0x5785AC, 2); // Down
        patch::nop(0x57874B, 2); // Up
    }

    if (settings.cursor_without_crosshair) {
        // Render 'crosshair' only when mouse cursor is not visible
        patch::jmp(0x5881A2, 0x5881D8);
    }

    // Fix R*'s nonsensical 'crosshair' pos calculation
    static auto hook = safetyhook::create_mid(0x577F95, [](safetyhook::Context& ctx) {
        const auto screenSpace = CVector2D{
            static_cast<float>(FrontEndMenuManager.m_nMousePosX) / (static_cast<float>(RsGlobal.maximumWidth) / 640.0f),
            static_cast<float>(FrontEndMenuManager.m_nMousePosY) / (static_cast<float>(RsGlobal.maximumHeight) / 448.0f),
        };
        const auto radarSpace = TransformScreenSpaceToRadarPoint(screenSpace);
        const auto worldSpace = TransformRadarSpaceToRealWorldPoint(radarSpace);
        FrontEndMenuManager.m_vMousePos = worldSpace;

        ctx.eip = 0x578785;
    });
}

}
