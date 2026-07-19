#include "Font.h"
#include "MenuManager.h"
#include "config.h"
#include "patcher.h"
#include "safetyhook/easy.hpp"

static struct TransparentMenu {
    bool enabled;
    uint8_t bg_alpha;
} settings;

namespace transparent_menu {

extern void ReadConfig(const Config& config) {
    config.Deserialize("transparent-menu", settings);
}

extern void Apply() {
    if (!settings.enabled) {
        return;
    }
    
    // Set menu background alpha
    static auto hook = safetyhook::create_mid(0x57B987, [](safetyhook::Context& ctx) {
        if (!FrontEndMenuManager.m_bGameNotLoaded) {
            *reinterpret_cast<uint32_t*>(ctx.esp) = settings.bg_alpha;
        }
    });

    struct Hook {
        static void CALLCONV_FASTCALL CSprite2d__Draw(CSprite2d* self, uintptr_t /*edx*/, const CRect& posn, const CRGBA& color) {
            if (FrontEndMenuManager.m_bGameNotLoaded) {
                self->Draw(posn, color);
            }
        }

        static void Render2dStuff() {
            if (!FrontEndMenuManager.m_bMenuActive) {
                // TODO: add Render2dStuff to the SDK
                reinterpret_cast<void(*)()>(0x53E230)();
            }
        }
    };

    // Do not render background sprite when paused
    patch::call(0x57B9FD, Hook::CSprite2d__Draw);

    // Keep rendering world in the menu
    patch::nop(0x53E9B3, 6);

    // Do not fill screen with black in the `DoRWStuffStartOfFrame`
    // Fixes 1-frame black flickering upon entering menu
    patch::nop(0x53D78D, 5);

    // Skip `Render2dStuff` when menu is active
    // Fixes map segments not loading due to the `CHud::Draw`
    patch::call(0x53EB12, Hook::Render2dStuff);

    // Prevent `CHeli::AddHeliSearchLight` buffer overflow due to
    // `CHeli::NumberOfSearchLights` not being reset each frame
    const char* fix =
        "\xC7\x05\x6C\xC9\xC1\x00\x00\x00\x00\x00" // mov [0xC1C96C], 0 ; CHeli::NumberOfSearchLights
        "\xB8\x34\xC2\x53\x00"                     // mov eax, 0x53C234
        "\xFF\xE0";                                // jmp eax
    patch::set<int32_t>(0x53BF96 + 2, reinterpret_cast<int32_t>(fix - 0x53BF96 - 6));
    patch::set<int32_t>(0x53BFA3 + 2, reinterpret_cast<int32_t>(fix - 0x53BFA3 - 6));

    // Reorder `DoFade` so it does not conflict with the transparent menu
    patch::nop(0x53EB9D, 5); // Remove original `DoFade` from `Idle`
    patch::nop(0x53E60D, 6); // Remove pause check from `DoFade` // TODO: nop 18 bytes to handle `codePause` too?
    patch::jmp(0x53EB7E,
        "\xB9\x00\xE6\x53\x00" // mov ecx, 0x53E600 ; DoFade
        "\xFF\xD1"             // call ecx

        "\xA0\xA4\x67\xBA\x00" // mov al, [0xBA67A4] ; FrontEndMenuManager.m_bMenuActive

        "\xB9\x83\xEB\x53\x00" // mov ecx, 0x53EB83
        "\xFF\xE1"             // jmp ecx
    );

    // Redefine controls:
    {
        // Remove bluish background
        patch::nop(0x57F877, 5);

        // Move selection slightly further from the right edge
        patch::set<float>(0x57EA5B + 1, 35.0f);
        // And extend it to the left up to the key name
        patch::set<float>(0x57EA88 + 1, 35.0f);
        // Make it transparent
        patch::set<uint32_t>(0x57EAA8 + 1, 50);

        // Add outline to the FET_CCN (Joypad), FET_SCN (Mouse + Keys), FET_CCR (Vehicle Controls) and FET_CFT (Foot Controls)
        patch::set<uint8_t>(0x57F6BB + 1, 1);
        // And to the FEDS_TB (Back)
        patch::set<uint8_t>(0x57FCF8 + 1, 1);
        // And to the keys too
        static auto hook = safetyhook::create_mid(0x57E7A0, [](safetyhook::Context& /*ctx*/) {
            CFont::SetEdge(1);
        });

        // Bluish color for the selected keybind's text
        patch::set<uint32_t>(0x57EAE4 + 1, 0xF1); // B
        patch::set<uint32_t>(0x57EAE9 + 1, 0xCB); // G
        patch::set<uint32_t>(0x57EAEE + 1, 0xAC); // R
        // Bluish color for the "???" (FEC_QUE)
        patch::set<uint8_t>(0x57ED0F + 1, 0xF1); // B
        patch::set<uint8_t>(0x57ED11 + 1, 0xCB); // G
        patch::set<uint8_t>(0x57ED13 + 1, 0xAC); // R
    }

    // Keep the game from centering cursor in menu
    patch::nop(0x53E9F1, 5);
}

}
