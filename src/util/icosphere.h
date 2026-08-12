#pragma once

#include <cstdint>
#include <span>
#include <vector>

class Icosphere final {
public:
    using Index = uint16_t;
    struct Vertex { float x, y, z; };
    struct Triangle { Index v1, v2, v3; };

public:
    static auto generate(size_t subdivisions) -> Icosphere;

    auto get_vertices() const -> std::span<const Vertex>;
    auto get_triangles() const -> std::span<const Triangle>;

    auto build_line_list_indices() const -> std::vector<Index>;
    auto get_tri_list_indices() const -> std::span<const Index>;

private:
    std::vector<Vertex> m_vertices{};
    std::vector<Triangle> m_triangles{};
};
