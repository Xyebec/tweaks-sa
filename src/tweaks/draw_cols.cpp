#include "ColBox.h"
#include "ColLine.h"
#include "ColModel.h"
#include "ColSphere.h"
#include "ColTriangle.h"
#include "CollisionData.h"
#include "CompressedVector.h"
#include "Entity.h"
#include "Matrix.h"
#include "ModelInfo.h"
#include "Renderer.h"
#include "VisibilityPlugins.h"
#include "config.h"
#include "rwcore.h"
#include "rwplcore.h"
#include "safetyhook/easy.hpp"
#include "safetyhook/inline_hook.hpp"
#include <span>
#include <vector>

class ImmRenderer final {
public:
    void AddPrimitive(
        std::span<const RwImVertexIndex> indices,
        std::span<const RwIm3DVertex> vertices
    ) {
        if (m_vertices.size() + vertices.size() > std::numeric_limits<RwImVertexIndex>::max()) {
            RenderAndFlush();
        }

        const auto offset = static_cast<RwImVertexIndex>(m_vertices.size());

        const auto prevSize = m_indices.size();
        m_indices.resize(prevSize + indices.size());
        for (size_t i = 0; i < indices.size(); i++) {
            m_indices[prevSize + i] = offset + indices[i];
        }

        m_vertices.insert(m_vertices.end(), vertices.begin(), vertices.end());
    }

    void AddCube(const CMatrix& matrix, const CVector& min, const CVector& max, RwUInt32 color) {
        static constexpr RwImVertexIndex indices[] = {
            0, 1,    0, 2,    0, 3,    4, 1,
            4, 7,    4, 3,    5, 1,    5, 7,
            5, 2,    6, 7,    6, 2,    6, 3,
        };

        const RwIm3DVertex vertices[] = {
            { .objVertex = matrix.TransformPoint(min), .color = color },
            { .objVertex = matrix.TransformPoint({min.x, min.y, max.z}), .color = color },
            { .objVertex = matrix.TransformPoint({max.x, min.y, min.z}), .color = color },
            { .objVertex = matrix.TransformPoint({min.x, max.y, min.z}), .color = color },
            { .objVertex = matrix.TransformPoint({min.x, max.y, max.z}), .color = color },
            { .objVertex = matrix.TransformPoint({max.x, min.y, max.z}), .color = color },
            { .objVertex = matrix.TransformPoint({max.x, max.y, min.z}), .color = color },
            { .objVertex = matrix.TransformPoint(max), .color = color },
        };

        AddPrimitive(indices, vertices);
    }

    void AddTriangle(const CMatrix& matrix, const CVector& a, const CVector& b, const CVector& c, RwUInt32 color) {
        static constexpr RwImVertexIndex indices[] = {
            0, 1,    1, 2,    2, 0
        };

        const RwIm3DVertex vertices[] = {
            { .objVertex = matrix.TransformPoint(a), .color = color },
            { .objVertex = matrix.TransformPoint(b), .color = color },
            { .objVertex = matrix.TransformPoint(c), .color = color },
        };

        AddPrimitive(indices, vertices);
    }

    void AddSphere(const CMatrix& matrix, const CVector& sphereCenter, float radius, RwUInt32 color) {
        const auto center = matrix.TransformPoint(sphereCenter);

        static constexpr RwImVertexIndex indices[] = {
            5, 0,    0, 4,    4, 1,    1, 5,
            5, 2,    2, 4,    4, 3,    3, 5,
        };
        
        const RwIm3DVertex vertices[] = {
            { .objVertex = {center.x - radius, center.y, center.z}, .color = color },
            { .objVertex = {center.x + radius, center.y, center.z}, .color = color },
            { .objVertex = {center.x, center.y - radius, center.z}, .color = color },
            { .objVertex = {center.x, center.y + radius, center.z}, .color = color },
            { .objVertex = {center.x, center.y, center.z - radius}, .color = color },
            { .objVertex = {center.x, center.y, center.z + radius}, .color = color },
        };

        AddPrimitive(indices, vertices);
    }

    void AddLine(const CMatrix& matrix, const CVector& start, const CVector& end, RwUInt32 color) {
        static constexpr RwImVertexIndex indices[] = {
            0, 1
        };

        const RwIm3DVertex vertices[] = {
            { .objVertex = matrix.TransformPoint(start), .color = color },
            { .objVertex = matrix.TransformPoint(end),   .color = color },
        };

        AddPrimitive(indices, vertices);
    }

    void RenderAndFlush() {
        if (m_vertices.empty()) {
            return;
        }

        if (RwIm3DTransform(m_vertices.data(), m_vertices.size(), nullptr, 0) != nullptr) {
            RwIm3DRenderIndexedPrimitive(rwPRIMTYPELINELIST, m_indices.data(), static_cast<RwInt32>(m_indices.size()));
            RwIm3DEnd();
        }
        
        m_vertices.clear();
        m_indices.clear();
    }

    void ReserveBuffers(size_t numVertices, size_t numIndices) {
        m_vertices.reserve(numVertices);
        m_indices.reserve(numIndices);
    }

    void ResetBuffers() {
        m_vertices = {};
        m_indices = {};
    }

private:
    std::vector<RwIm3DVertex>    m_vertices{};
    std::vector<RwImVertexIndex> m_indices{};
};

static ImmRenderer s_renderer;

enum class DrawColsMode {
    Disabled,
    WorldAndWireframes,
    WireframesOnly,
};

static struct DrawCols {
    bool enabled;
    bool draw_bound_boxes;
    bool draw_bound_spheres;
    bool draw_spheres;
    bool draw_lines;
    bool draw_boxes;
    bool draw_triangles;
    bool draw_shadow_triangles;
    uint32_t hotkey;
    // A R G B colors
    RwUInt32 color_bound_box;
    RwUInt32 color_bound_sphere;
    RwUInt32 color_sphere;
    RwUInt32 color_line;
    RwUInt32 color_box;
    RwUInt32 color_triangle;
    RwUInt32 color_shadow_triangle;
} settings;

static void DrawColModel(const CMatrix& matrix, const CColModel& colModel) {
    if (settings.draw_bound_boxes) {
        const auto& boundBox = colModel.m_boundBox;
        s_renderer.AddCube(matrix, boundBox.m_vecMin, boundBox.m_vecMax, settings.color_bound_box);
    }

    if (settings.draw_bound_spheres) {
        const auto& boundSphere = colModel.m_boundSphere;
        s_renderer.AddSphere(matrix, boundSphere.m_vecCenter, boundSphere.m_fRadius, settings.color_bound_sphere);
    }

    auto* colData = colModel.m_pColData;
    if (colData == nullptr) {
        return;
    }

    if (settings.draw_spheres) {
        for (const auto& sphere : std::span{colData->m_pSpheres, colData->m_nNumSpheres}) {
            s_renderer.AddSphere(matrix, sphere.m_vecCenter, sphere.m_fRadius, settings.color_sphere);
        }
    }

    if (settings.draw_lines) {
        for (const auto& line : std::span{colData->m_pLines, colData->m_nNumLines}) {
            s_renderer.AddLine(matrix, line.m_vecStart, line.m_vecEnd, settings.color_line);
        }
    }
    
    if (settings.draw_boxes) {
        for (const auto& box : std::span{colData->m_pBoxes, colData->m_nNumBoxes}) {
            s_renderer.AddCube(matrix, box.m_vecMin, box.m_vecMax, settings.color_box);
        }
    }

    if (settings.draw_triangles) {
        for (const auto& triangle : std::span{colData->m_pTriangles, colData->m_nNumTriangles}) {
            // `CCollisionData::GetTrianglePoint` calls inlined
            const auto v1 = UncompressVector(colData->m_pVertices[triangle.m_nVertA]);
            const auto v2 = UncompressVector(colData->m_pVertices[triangle.m_nVertB]);
            const auto v3 = UncompressVector(colData->m_pVertices[triangle.m_nVertC]);
            s_renderer.AddTriangle(matrix, v1, v2, v3, settings.color_triangle);
        }
    }

    if (settings.draw_shadow_triangles && colData->m_pShadowTriangles != nullptr) {
        for (const auto& triangle : std::span{colData->m_pShadowTriangles, colData->m_nNumShadowTriangles}) {
            // `CCollisionData::GetShadTrianglePoint` calls inlined
            const auto v1 = UncompressVector(colData->m_pShadowVertices[triangle.m_nVertA]);
            const auto v2 = UncompressVector(colData->m_pShadowVertices[triangle.m_nVertB]);
            const auto v3 = UncompressVector(colData->m_pShadowVertices[triangle.m_nVertC]);
            s_renderer.AddTriangle(matrix, v1, v2, v3, settings.color_shadow_triangle);
        }
    }
}

static void DrawEntity(CEntity* entity) {
    const auto* matrix = entity->GetMatrix();
    if (matrix == nullptr) {
        return;
    }

    const auto index = entity->m_nModelIndex;
    if (CModelInfo::ms_modelInfoPtrs[index] == nullptr) {
        return;
    }

    const auto* colModel = CModelInfo::ms_modelInfoPtrs[index]->m_pColModel;
    if (colModel == nullptr) {
        return;
    }

    DrawColModel(*matrix, *colModel);
};

static void RenderCollisionLines() {
    RwRenderStateSet(rwRENDERSTATETEXTURERASTER, nullptr);

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

    s_renderer.RenderAndFlush();
}

static safetyhook::InlineHook Orig_RenderScene;
static void Hook_RenderScene() {
    static auto s_mode = DrawColsMode::Disabled;
    static auto s_keystate = false;
    if ((GetAsyncKeyState(static_cast<int>(settings.hotkey)) & 0x8000) != 0) {
        if (!s_keystate) {
            s_keystate = true;
            
            switch (s_mode) {
                case DrawColsMode::Disabled:
                    s_renderer.ReserveBuffers(80000, 160000); // 2.75MiB for vertices, 312KiB for indices
                    s_mode = DrawColsMode::WorldAndWireframes;
                    break;
                case DrawColsMode::WorldAndWireframes:
                    s_mode = DrawColsMode::WireframesOnly;
                    break;
                case DrawColsMode::WireframesOnly:
                    s_mode = DrawColsMode::Disabled;
                    s_renderer.ResetBuffers();
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
        RenderCollisionLines();
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
