#include "icosphere.h"
#include <cmath>
#include <unordered_map>
#include <unordered_set>

static auto normalize(const Icosphere::Vertex& v) -> Icosphere::Vertex {
    const auto length = std::sqrt(v.x * v.x + v.y * v.y + v.z * v.z);
    return { v.x / length, v.y / length, v.z / length };
}

static auto get_middle_point(std::unordered_map<int64_t, Icosphere::Index>& cache, std::vector<Icosphere::Vertex>& vertices, Icosphere::Index p1, Icosphere::Index p2) -> Icosphere::Index {
    const int64_t smaller_idx = std::min(p1, p2);
    const int64_t greater_idx = std::max(p1, p2);
    const int64_t key = (smaller_idx << 32) | greater_idx;

    if (const auto it = cache.find(key); it != cache.end()) {
        return it->second;
    }

    const auto& v1 = vertices[p1];
    const auto& v2 = vertices[p2];
    const auto middle = Icosphere::Vertex{
        (v1.x + v2.x) / 2.0f,
        (v1.y + v2.y) / 2.0f,
        (v1.z + v2.z) / 2.0f,
    };

    const auto index = static_cast<uint32_t>(vertices.size());
    vertices.emplace_back(normalize(middle));
    cache[key] = index;

    return index;
}

auto Icosphere::generate(size_t subdivisions) -> Icosphere {
    auto icosphere = Icosphere{};

    constexpr auto t = std::numbers::phi_v<float>;

    icosphere.m_vertices = {
        {-1,  t,  0}, { 1,  t,  0}, {-1, -t,  0}, { 1, -t,  0},
        { 0, -1,  t}, { 0,  1,  t}, { 0, -1, -t}, { 0,  1, -t},
        { t,  0, -1}, { t,  0,  1}, {-t,  0, -1}, {-t,  0,  1},
    };

    for (auto& vertex : icosphere.m_vertices) {
        vertex = normalize(vertex);
    }

    icosphere.m_triangles = {
        {0, 11, 5}, {0,  5,  1}, { 0,  1,  7}, { 0, 7, 10}, {0, 10, 11},
        {1,  5, 9}, {5, 11,  4}, {11, 10,  2}, {10, 7,  6}, {7,  1,  8},
        {3,  9, 4}, {3,  4,  2}, { 3,  2,  6}, { 3, 6,  8}, {3,  8,  9},
        {4,  9, 5}, {2,  4, 11}, { 6,  2, 10}, { 8, 6,  7}, {9,  8,  1},
    };

    auto middlePointCache = std::unordered_map<int64_t, Index>{};

    for (size_t i = 0; i < subdivisions; i++) {
        auto newTriangles = std::vector<Triangle>{};
        for (const auto& tri : icosphere.m_triangles) {
            const auto a = get_middle_point(middlePointCache, icosphere.m_vertices, tri.v1, tri.v2);
            const auto b = get_middle_point(middlePointCache, icosphere.m_vertices, tri.v2, tri.v3);
            const auto c = get_middle_point(middlePointCache, icosphere.m_vertices, tri.v3, tri.v1);

            newTriangles.emplace_back(tri.v1, a, c);
            newTriangles.emplace_back(tri.v2, b, a);
            newTriangles.emplace_back(tri.v3, c, b);
            newTriangles.emplace_back(     a, b, c);
        }
        icosphere.m_triangles = std::move(newTriangles);
    }

    return icosphere;
}

auto Icosphere::get_vertices() const -> std::span<const Vertex> {
    return m_vertices;
}

auto Icosphere::get_triangles() const -> std::span<const Triangle> {
    return m_triangles;
}

auto Icosphere::build_line_list_indices() const -> std::vector<Index> {
    auto indices = std::vector<Index>{};
    indices.reserve(m_triangles.size() * 3); 

    auto edge_cache = std::unordered_set<int64_t>{};

    for (const auto& tri : m_triangles) {
        const std::pair<uint32_t, uint32_t> edges[3] = {
            {tri.v1, tri.v2},
            {tri.v2, tri.v3},
            {tri.v3, tri.v1},
        };

        for (const auto& edge : edges) {
            const int64_t smallerIdx = std::min(edge.first, edge.second);
            const int64_t greaterIdx = std::max(edge.first, edge.second);
            const int64_t key = (smallerIdx << 32) | greaterIdx;

            if (edge_cache.insert(key).second) {
                indices.push_back(smallerIdx);
                indices.push_back(greaterIdx);
            }
        }
    }

    return indices;
}

auto Icosphere::get_tri_list_indices() const -> std::span<const Index> {
    static_assert(sizeof(Triangle) == sizeof(Index) * 3);
    return std::span{reinterpret_cast<const Index*>(m_triangles.data()), m_triangles.size() * 3};
}
