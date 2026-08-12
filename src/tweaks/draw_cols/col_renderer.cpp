#include "col_renderer.hpp"
#include "common.h"
#include "util/dx/dx.hpp"
#include <d3d9types.h>
#include <d3dx9.h>

static auto GetViewProjectionMatrix() -> D3DXMATRIX {
    const auto* view = reinterpret_cast<D3DXMATRIX*>(GetD3DViewTransform());
    const auto* proj = reinterpret_cast<D3DXMATRIX*>(GetD3DProjTransform());
    
    D3DXMATRIX viewProj;
    D3DXMatrixMultiply(&viewProj, view, proj);
    return viewProj;
}

template <typename Index, size_t Extent>
static void InsertIndices(size_t numVertices, std::span<const Index, Extent> indices, std::vector<Index>& outIndices) {
    const auto prevSize = outIndices.size();
    outIndices.resize(prevSize + indices.size());
    for (size_t i = 0; i < indices.size(); i++) {
        outIndices[prevSize + i] = static_cast<Index>(numVertices + indices[i]);
    }
}

auto ColRenderer::Create(IDirect3DDevice9* device) -> ColRenderer {
    static constexpr char SHADER_SPHERE[] = {
        #embed "sphere.hlsl"
        , '\0'
    };

    const auto icosphere = Icosphere::generate(2);

    auto spherePipeline = SpherePipeline{
        .shaders = dx::Shaders::create(device, {
            .layout = dx::LayoutBuilder{}
                .add<0>(D3DDECLTYPE_FLOAT3,  D3DDECLUSAGE_POSITION, 0)
                .add<1>(D3DDECLTYPE_FLOAT4,  D3DDECLUSAGE_TEXCOORD, 0)
                .add<1>(D3DDECLTYPE_UBYTE4N, D3DDECLUSAGE_COLOR,    0)
                .build(),
            .vs = {SHADER_SPHERE, "vs_main"},
            .ps = {SHADER_SPHERE, "ps_main"},
        }).value(),

        .mesh = SphereMesh{
            .vertices = {device, dx::BufferUsage::Static, icosphere.get_vertices()},
            .indices = {device, dx::BufferUsage::Static, icosphere.build_line_list_indices()},
        },

        .instances = dx::DynInstanceBuffer<SphereInstance>{device, 2048},
    };

    static constexpr char SHADER_GEOMETRY[] = {
        #embed "geometry.hlsl"
        , '\0'
    };

    auto geometryShaders = dx::Shaders::create(device, {
        .layout = dx::LayoutBuilder{}
            .add<0>(D3DDECLTYPE_FLOAT3,  D3DDECLUSAGE_POSITION, 0)
            .add<0>(D3DDECLTYPE_UBYTE4N, D3DDECLUSAGE_COLOR,    0)
            .build(),
        .vs = {SHADER_GEOMETRY, "vs_main"},
        .ps = {SHADER_GEOMETRY, "ps_main"},
    }).value();
    
    auto vbo = dx::VertexBuffer<GeometryVertex>{device, dx::BufferUsage::Dynamic, 144000};
    auto ibo = dx::IndexBuffer<GeometryIndex>{device, dx::BufferUsage::Dynamic, 131072};

    auto triVertices = std::vector<GeometryVertex>{};
    triVertices.reserve(144000);
    auto lineVertices = std::vector<GeometryVertex>{};
    lineVertices.reserve(1024);
    auto cubeVertices = std::vector<GeometryVertex>{};
    cubeVertices.reserve(32768);
    auto cubeIndices = std::vector<GeometryIndex>{};
    cubeIndices.reserve(131072);
    auto coneVertices = std::vector<GeometryVertex>{};
    coneVertices.reserve(512);
    auto coneIndices = std::vector<GeometryIndex>{};
    coneIndices.reserve(512);

    return ColRenderer{M{
        .spheres         = std::move(spherePipeline),
        .geometryShaders = std::move(geometryShaders),
        .vbo             = std::move(vbo),
        .ibo             = std::move(ibo),
        .triVertices     = std::move(triVertices),
        .lineVertices    = std::move(lineVertices),
        .cubeVertices    = std::move(cubeVertices),
        .cubeIndices     = std::move(cubeIndices),
        .coneVertices    = std::move(coneVertices),
        .coneIndices     = std::move(coneIndices),
    }};
}

void ColRenderer::AddSphere(const CMatrix& ltm, const CVector& center, float radius, uint32_t color) {
    const auto pos = ltm.TransformPoint(center);
    m.spheres.instances.emplace_back(pos.x, pos.y, pos.z, radius, color);
}

void ColRenderer::AddTriangle(const CMatrix& ltm, const CVector& a, const CVector& b, const CVector& c, uint32_t color) {
    m.triVertices.emplace_back(ltm.TransformPoint(a), color);
    m.triVertices.emplace_back(ltm.TransformPoint(b), color);
    m.triVertices.emplace_back(ltm.TransformPoint(c), color);
}

void ColRenderer::AddLine(const CMatrix& ltm, const CVector& start, const CVector& end, uint32_t color) {
    m.lineVertices.emplace_back(ltm.TransformPoint(start), color);
    m.lineVertices.emplace_back(ltm.TransformPoint(end),   color);
}

void ColRenderer::AddCube(const CMatrix& ltm, const CVector& min, const CVector& max, uint32_t color) {
    static constexpr auto INDICES = std::array<GeometryIndex, 24>{
        0, 1,  1, 2,  2, 3,  3, 0, // Front
        4, 5,  5, 6,  6, 7,  7, 4, // Back
        0, 4,  1, 5,  2, 6,  3, 7, // Connecting edges
    };

    const GeometryVertex vertices[] = {
        { .position = ltm.TransformPoint({min.x, max.y, min.z}), .color = color },
        { .position = ltm.TransformPoint({max.x, max.y, min.z}), .color = color },
        { .position = ltm.TransformPoint({max.x, min.y, min.z}), .color = color },
        { .position = ltm.TransformPoint({min.x, min.y, min.z}), .color = color },
        { .position = ltm.TransformPoint({min.x, max.y, max.z}), .color = color },
        { .position = ltm.TransformPoint({max.x, max.y, max.z}), .color = color },
        { .position = ltm.TransformPoint({max.x, min.y, max.z}), .color = color },
        { .position = ltm.TransformPoint({min.x, min.y, max.z}), .color = color },
    };

    InsertIndices(m.cubeVertices.size(), std::span{INDICES}, m.cubeIndices);
    m.cubeVertices.insert_range(m.cubeVertices.end(), vertices);
}

void ColRenderer::AddCylinder(const CMatrix& ltm, const CVector& center, float radius, const CVector& direction, float thickness, uint32_t color) {
    const auto normal = direction.Normalized();

    auto rAxisX = (std::abs(normal.z) < 0.999f)
        ? CVector{0.0f, 0.0f, 1.0f}
        : CVector{1.0f, 0.0f, 0.0f};

    rAxisX = normal.Cross(rAxisX).Normalized();
    const auto rAxisY = normal.Cross(rAxisX);

    const auto offset = normal * thickness;

    const auto worldCenter = ltm.TransformPoint(center);
    const auto worldOffset = ltm.TransformVector(offset);
    const auto worldAxisX  = ltm.TransformVector(rAxisX) * radius;
    const auto worldAxisY  = ltm.TransformVector(rAxisY) * radius;

    const auto topCenter    = worldCenter + worldOffset;
    const auto bottomCenter = worldCenter - worldOffset;

    constexpr auto DISK_SEGMENTS = size_t{16};

    constexpr auto TOP_CENTER_IDX     = size_t{0};
    constexpr auto TOP_CAP_OFFSET     = size_t{1};
    constexpr auto BOTTOM_CENTER_IDX  = DISK_SEGMENTS + 1;
    constexpr auto BOTTOM_CAP_OFFSET  = BOTTOM_CENTER_IDX + 1;
    constexpr auto TOP_WALL_OFFSET    = (DISK_SEGMENTS + 1) * 2;
    constexpr auto BOTTOM_WALL_OFFSET = TOP_WALL_OFFSET + DISK_SEGMENTS;
    constexpr auto NUM_VERTICES       = BOTTOM_WALL_OFFSET + DISK_SEGMENTS;

    static constexpr auto TRIG_TABLE = []() {
        constexpr auto step = (std::numbers::pi_v<float> * 2.0f) / static_cast<float>(DISK_SEGMENTS);

        auto table = std::array<std::pair<float, float>, DISK_SEGMENTS>{};
        for (size_t i = 0; i < DISK_SEGMENTS; i++) {
            const auto angle = static_cast<float>(i) * step;
            table[i] = { std::cos(angle), std::sin(angle) };
        }
        return table;
    }();

    static constexpr auto INDICES = []() {
        constexpr auto NUM_INDICES = (DISK_SEGMENTS * 3 * 2) + (DISK_SEGMENTS * 6);
        auto indices = std::array<GeometryIndex, NUM_INDICES>{};
        size_t idx = 0;

        for (size_t i = 0; i < DISK_SEGMENTS; i++) {
            const auto next = (i + 1) % DISK_SEGMENTS;

            indices[idx++] = static_cast<GeometryIndex>(TOP_CENTER_IDX);
            indices[idx++] = static_cast<GeometryIndex>(TOP_CAP_OFFSET + next);
            indices[idx++] = static_cast<GeometryIndex>(TOP_CAP_OFFSET + i);

            indices[idx++] = static_cast<GeometryIndex>(BOTTOM_CENTER_IDX);
            indices[idx++] = static_cast<GeometryIndex>(BOTTOM_CAP_OFFSET + i);
            indices[idx++] = static_cast<GeometryIndex>(BOTTOM_CAP_OFFSET + next);

            indices[idx++] = static_cast<GeometryIndex>(TOP_WALL_OFFSET + i);
            indices[idx++] = static_cast<GeometryIndex>(TOP_WALL_OFFSET + next);
            indices[idx++] = static_cast<GeometryIndex>(BOTTOM_WALL_OFFSET + i);

            indices[idx++] = static_cast<GeometryIndex>(TOP_WALL_OFFSET + next);
            indices[idx++] = static_cast<GeometryIndex>(BOTTOM_WALL_OFFSET + next);
            indices[idx++] = static_cast<GeometryIndex>(BOTTOM_WALL_OFFSET + i);
        }
        return indices;
    }();

    InsertIndices(m.coneVertices.size(), std::span{INDICES.data(), INDICES.size()}, m.coneIndices);

    const auto vertexOffset = m.coneVertices.size();
    m.coneVertices.resize(vertexOffset + NUM_VERTICES);
    auto* vertices = &m.coneVertices[vertexOffset];

    vertices[TOP_CENTER_IDX].position = topCenter;
    vertices[BOTTOM_CENTER_IDX].position = bottomCenter;

    for (size_t i = 0; i < NUM_VERTICES; i++) {
        vertices[i].color = color;
    }

    for (size_t i = 0; i < DISK_SEGMENTS; i++) {
        const auto& [cos, sin] = TRIG_TABLE[i];
        const auto localRadius = (worldAxisX * cos) + (worldAxisY * sin);

        const auto topPos    = topCenter + localRadius;
        const auto bottomPos = bottomCenter + localRadius;

        vertices[TOP_CAP_OFFSET + i].position     = topPos;
        vertices[BOTTOM_CAP_OFFSET + i].position  = bottomPos;
        vertices[TOP_WALL_OFFSET + i].position    = topPos;
        vertices[BOTTOM_WALL_OFFSET + i].position = bottomPos;
    }
}

void ColRenderer::Render(IDirect3DDevice9* device) {
    const auto _ = dx::ScopedState{device, {
        {D3DRS_ZENABLE, D3DZB_TRUE},
        {D3DRS_ZWRITEENABLE, TRUE},
        {D3DRS_ZFUNC, D3DCMP_LESSEQUAL},
        {D3DRS_STENCILENABLE, FALSE},

        {D3DRS_DEPTHBIAS, -0.00005f},

        {D3DRS_ALPHABLENDENABLE, FALSE},
        {D3DRS_CULLMODE, D3DCULL_NONE},
        {D3DRS_LIGHTING, FALSE},

        {D3DRS_FILLMODE, D3DFILL_WIREFRAME},
    }};

    const auto viewProj = GetViewProjectionMatrix();
    device->SetVertexShaderConstantF(0, reinterpret_cast<const float*>(&viewProj), 4);

    if (!m.spheres.instances.empty()) {
        m.spheres.instances.sync(device);

        m.spheres.shaders.bind(device);
        m.spheres.mesh.vertices.bind_instanced(device, 0, m.spheres.instances.size());
        m.spheres.instances.get_gpu().bind(device, 1);
        m.spheres.mesh.indices.bind(device);

        device->DrawIndexedPrimitive(D3DPT_LINELIST, 0, 0, m.spheres.mesh.vertices.size(), 0, m.spheres.mesh.indices.size() / 2);

        device->SetStreamSourceFreq(0, 1);
        device->SetStreamSourceFreq(1, 1);
    }

    m.geometryShaders.bind(device);

    if (!m.triVertices.empty()) {
        m.vbo.assign(device, m.triVertices);
        m.vbo.bind(device, 0);
        device->DrawPrimitive(D3DPT_TRIANGLELIST, 0, m.vbo.size() / 3);
    }

    if (!m.lineVertices.empty()) {
        m.vbo.assign(device, m.lineVertices);
        device->DrawPrimitive(D3DPT_LINELIST, 0, m.vbo.size() / 2);
    }

    if (!m.cubeVertices.empty()) {
        m.vbo.assign(device, m.cubeVertices);
        m.vbo.bind(device, 0);

        m.ibo.assign(device, m.cubeIndices);
        m.ibo.bind(device);
        device->DrawIndexedPrimitive(D3DPT_LINELIST, 0, 0, m.vbo.size(), 0, m.ibo.size() / 2);
    }

    if (!m.coneVertices.empty()) {
        m.vbo.assign(device, m.coneVertices);
        m.vbo.bind(device, 0);
        m.ibo.assign(device, m.coneIndices);
        m.ibo.bind(device);
        device->DrawIndexedPrimitive(D3DPT_TRIANGLELIST, 0, 0, m.vbo.size(), 0, m.ibo.size() / 3);
    }
}

void ColRenderer::ClearBuffers() {
    m.spheres.instances.clear();
    m.triVertices.clear();
    m.lineVertices.clear();
    m.cubeVertices.clear();
    m.cubeIndices.clear();
    m.coneVertices.clear();
    m.coneIndices.clear();
}
