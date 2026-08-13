#include "ColBox.h"
#include "ColDisk.h"
#include "ColLine.h"
#include "ColModel.h"
#include "ColSphere.h"
#include "ColTriangle.h"
#include "CollisionData.h"
#include "CompressedVector.h"
#include "Entity.h"
#include "Renderer.h"
#include "VehicleModelInfo.h"
#include "VisibilityPlugins.h"
#include "col_renderer.hpp"
#include "config.h"
#include "eEntityType.h"
#include "safetyhook/easy.hpp"

// TODO: Add to the SDK
static const auto CPedModelInfo_AnimatePedColModelSkinned = reinterpret_cast<CColModel* __thiscall(*)(CBaseModelInfo*, RpClump*)>(0x4C6F70);

struct AbgrFromRgb {
    uint32_t color{};

    constexpr operator uint32_t() const noexcept {
        return color;
    }
};

template<>
struct Config::De<AbgrFromRgb> {
    static auto Deserialize(const Context& ctx) -> AbgrFromRgb {
        const auto argb = De<uint32_t>::Deserialize(ctx);

        const auto r = (argb >> 16) & 0xFF;
        const auto g = (argb >> 8)  & 0xFF;
        const auto b =  argb        & 0xFF;

        return { 0xFF000000 | (b << 16) | (g << 8) | r };
    }
};

static auto s_renderer = std::optional<ColRenderer>{};

enum class DrawColsMode {
    Disabled,
    WorldAndWireframes,
    WireframesOnly,
};

static struct DrawCols {
    bool enabled{true};
    bool draw_bound_boxes{true};
    bool draw_bound_spheres{true};
    bool draw_spheres{true};
    bool draw_lines{true};
    bool draw_boxes{true};
    bool draw_triangles{true};
    bool draw_shadow_triangles{true};
    bool draw_hit_spheres{true};
    uint32_t hotkey{VK_F11};
    AbgrFromRgb color_bound_box;
    AbgrFromRgb color_bound_sphere;
    AbgrFromRgb color_sphere;
    AbgrFromRgb color_line;
    AbgrFromRgb color_box;
    AbgrFromRgb color_triangle;
    AbgrFromRgb color_shadow_triangle;
    AbgrFromRgb color_hit_sphere;
} settings;

static void DrawEntity(CEntity* entity) {
    const auto* matrix = entity->GetMatrix();
    if (matrix == nullptr) {
        return;
    }

    if (settings.draw_hit_spheres) {
        if (entity->m_nType == ENTITY_TYPE_PED) {
            const auto* colModel = CPedModelInfo_AnimatePedColModelSkinned(
                CModelInfo::ms_modelInfoPtrs[entity->m_nModelIndex],
                entity->m_pRwClump);

            for (const auto& sphere : std::span{colModel->m_pColData->m_pSpheres, colModel->m_pColData->m_nNumSpheres}) {
                s_renderer->AddSphere(*matrix, sphere.m_vecCenter, sphere.m_fRadius, settings.color_hit_sphere);
            }
        } else if (entity->m_nType == ENTITY_TYPE_VEHICLE) {
            const auto* modelInfo = reinterpret_cast<CVehicleModelInfo*>(CModelInfo::ms_modelInfoPtrs[entity->m_nModelIndex]);
            const auto& gasTankPos = modelInfo->m_pVehicleStruct->m_avDummyPos[8];
            if (!gasTankPos.IsZero()) {
                s_renderer->AddSphere(*matrix, gasTankPos, 0.25f, settings.color_hit_sphere);
            }
        }
    }

    const auto* colModel = entity->GetColModel();
    if (colModel == nullptr) {
        return;
    }

    if (settings.draw_bound_boxes) {
        const auto& boundBox = colModel->m_boundBox;
        s_renderer->AddCube(*matrix, boundBox.m_vecMin, boundBox.m_vecMax, settings.color_bound_box);
    }

    if (settings.draw_bound_spheres) {
        const auto& boundSphere = colModel->m_boundSphere;
        s_renderer->AddSphere(*matrix, boundSphere.m_vecCenter, boundSphere.m_fRadius, settings.color_bound_sphere);
    }

    const auto* colData = colModel->m_pColData;
    if (colData == nullptr) {
        return;
    }

    if (settings.draw_spheres) {
        for (const auto& sphere : std::span{colData->m_pSpheres, colData->m_nNumSpheres}) {
            s_renderer->AddSphere(*matrix, sphere.m_vecCenter, sphere.m_fRadius, settings.color_sphere);
        }
    }

    if (settings.draw_lines) {
        if (colData->m_nFlags.bUsesDisks) {
            for (const auto& cone : std::span{colData->m_pDisks, colData->m_nNumLines}) {
                s_renderer->AddCylinder(*matrix, cone.m_vecStart, cone.m_fStartRadius, cone.m_vecEnd, cone.m_fEndRadius, settings.color_line);
            }
        } else {
            for (const auto& line : std::span{colData->m_pLines, colData->m_nNumLines}) {
                s_renderer->AddLine(*matrix, line.m_vecStart, line.m_vecEnd, settings.color_line);
            }
        }
    }

    if (settings.draw_boxes) {
        for (const auto& box : std::span{colData->m_pBoxes, colData->m_nNumBoxes}) {
            s_renderer->AddCube(*matrix, box.m_vecMin, box.m_vecMax, settings.color_box);
        }
    }

    if (settings.draw_triangles) {
        for (const auto& triangle : std::span{colData->m_pTriangles, colData->m_nNumTriangles}) {
            // `CCollisionData::GetTrianglePoint` calls inlined
            const auto v1 = UncompressVector(colData->m_pVertices[triangle.m_nVertA]);
            const auto v2 = UncompressVector(colData->m_pVertices[triangle.m_nVertB]);
            const auto v3 = UncompressVector(colData->m_pVertices[triangle.m_nVertC]);
            s_renderer->AddTriangle(*matrix, v1, v2, v3, settings.color_triangle);
        }
    }
    
    if (settings.draw_shadow_triangles && colData->m_pShadowTriangles != nullptr) {
        for (const auto& triangle : std::span{colData->m_pShadowTriangles, colData->m_nNumShadowTriangles}) {
            // `CCollisionData::GetShadTrianglePoint` calls inlined
            const auto v1 = UncompressVector(colData->m_pShadowVertices[triangle.m_nVertA]);
            const auto v2 = UncompressVector(colData->m_pShadowVertices[triangle.m_nVertB]);
            const auto v3 = UncompressVector(colData->m_pShadowVertices[triangle.m_nVertC]);
            s_renderer->AddTriangle(*matrix, v1, v2, v3, settings.color_shadow_triangle);
        }
    }
};

static auto s_mode = DrawColsMode::Disabled;

static void RenderCollision(IDirect3DDevice9* device) {
    for (auto* entity : std::span{CRenderer::ms_aVisibleEntityPtrs, CRenderer::ms_nNoOfVisibleEntities}) {
        DrawEntity(entity);
    }

    for (
        auto* link = CVisibilityPlugins::m_alphaEntityList.usedListTail.prev;
        link != &CVisibilityPlugins::m_alphaEntityList.usedListHead;
        link = link->prev
    ) {
        auto* entity = static_cast<CEntity*>(link->data.pObj);
        DrawEntity(entity);
    }

    s_renderer->Render(device);
    s_renderer->ClearBuffers();
}

static safetyhook::InlineHook Orig_RenderScene;
static void Hook_RenderScene() {
    static auto s_keystate = false;
    if ((GetAsyncKeyState(static_cast<int>(settings.hotkey)) & 0x8000) != 0) {
        if (!s_keystate) {
            s_keystate = true;
            
            switch (s_mode) {
                case DrawColsMode::Disabled:
                    if (!s_renderer.has_value()) {
                        auto* device = *reinterpret_cast<IDirect3DDevice9**>(0xC97C28);
                        s_renderer = ColRenderer::Create(device);
                    }
                    s_mode = DrawColsMode::WorldAndWireframes;
                    break;
                case DrawColsMode::WorldAndWireframes:
                    s_mode = DrawColsMode::WireframesOnly;
                    break;
                case DrawColsMode::WireframesOnly:
                    // s_renderer = std::nullopt;
                    s_mode = DrawColsMode::Disabled;
                    break;
            }
        }
    } else {
        s_keystate = false;
    }

    if (s_mode != DrawColsMode::WireframesOnly) {
        Orig_RenderScene.unsafe_ccall();
    }

    if (s_mode != DrawColsMode::Disabled) {
        auto* device = reinterpret_cast<IDirect3DDevice9*>(RwD3D9GetCurrentD3DDevice());
        RenderCollision(device);
    }
}

namespace draw_cols {

extern void ReadConfig(const Config& config) {
    config.Deserialize("debug.draw-cols", settings);
}

extern void Apply() {
    if (!settings.enabled) {
        return;
    }
    
    Orig_RenderScene = safetyhook::create_inline(0x53DF40, Hook_RenderScene);
}

}
