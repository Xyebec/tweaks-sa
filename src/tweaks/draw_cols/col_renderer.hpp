#pragma once

#include "Matrix.h"
#include "Vector.h"
#include "util/dx/dx.hpp"
#include "util/icosphere.h"

struct SphereInstance {
    float x, y, z, radius;
    D3DCOLOR color;
};

using SphereMesh = dx::Mesh<Icosphere::Vertex, Icosphere::Index>;

struct GeometryVertex {
    CVector position;
    D3DCOLOR color;
};

using GeometryIndex = uint16_t;

class ColRenderer final {
public:
    static auto Create(IDirect3DDevice9* device) -> ColRenderer;

    void AddSphere(const CMatrix& ltm, const CVector& center, float radius, uint32_t color);
    void AddTriangle(const CMatrix& ltm, const CVector& a, const CVector& b, const CVector& c, uint32_t color);
    void AddCube(const CMatrix& ltm, const CVector& min, const CVector& max, uint32_t color);
    void AddLine(const CMatrix& ltm, const CVector& start, const CVector& end, uint32_t color);
    void AddCylinder(const CMatrix& ltm, const CVector& center, float radius, const CVector& direction, float thickness, uint32_t color);
    void Render(IDirect3DDevice9* device);
    void ClearBuffers();

private:
    struct SpherePipeline {
        dx::Shaders shaders;
        SphereMesh mesh;
        dx::DynInstanceBuffer<SphereInstance> instances;
    };

    struct M {
        SpherePipeline spheres;

        dx::Shaders geometryShaders;
        dx::VertexBuffer<GeometryVertex> vbo;
        dx::IndexBuffer<GeometryIndex> ibo;

        std::vector<GeometryVertex> triVertices;

        std::vector<GeometryVertex> lineVertices;

        std::vector<GeometryVertex> cubeVertices;
        std::vector<GeometryIndex> cubeIndices;

        std::vector<GeometryVertex> coneVertices;
        std::vector<GeometryIndex> coneIndices;
    } m;

    ColRenderer(M&& m) : m{std::move(m)} {}
};
