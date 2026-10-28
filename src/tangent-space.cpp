#include "lc1/tangent-space.hpp"

#include "lc1/error.hpp"

#include <mikktspace.h>

#include <cmath>
#include <limits>
#include <span>
#include <unordered_map>

namespace lc1 {
namespace {

bool finite(glm::vec3 value)
{
    return std::isfinite(value.x) && std::isfinite(value.y) && std::isfinite(value.z);
}

struct TangentContext {
    std::span<Vertex const> vertices;
    std::span<std::uint32_t const> indices;
    std::span<glm::vec4> corners;
};

// MikkTSpace borrows this stack context for the duration of genTangSpaceDefault.
TangentContext &context(SMikkTSpaceContext const *value)
{
    return *static_cast<TangentContext *>(value->m_pUserData);
}

Vertex const &vertex_at(SMikkTSpaceContext const *value, int face, int corner)
{
    auto const &input = context(value);
    return input.vertices[input.indices[static_cast<std::size_t>(face) * 3 + corner]];
}

void copy_vector(std::span<float, 3> output, glm::vec3 value)
{
    output[0] = value.x;
    output[1] = value.y;
    output[2] = value.z;
}

} // namespace

bool valid_tangent_frame(Vertex const &vertex)
{
    if (vertex.tangent.w != 1.0F && vertex.tangent.w != -1.0F)
        return false;
    float const normal_length = glm::length(vertex.normal);
    float const tangent_length = glm::length(glm::vec3{vertex.tangent});
    if (!std::isfinite(normal_length) || !std::isfinite(tangent_length) || normal_length <= 0 ||
        tangent_length <= 0)
        return false;
    auto const cross =
        glm::cross(vertex.normal / normal_length, glm::vec3{vertex.tangent} / tangent_length);
    return glm::dot(cross, cross) > 1e-8F;
}

void generate_tangents(std::vector<Vertex> &vertices, std::vector<std::uint32_t> &indices)
{
    if (indices.empty() || indices.size() % 3 != 0 ||
        indices.size() > static_cast<std::size_t>(std::numeric_limits<int>::max()))
        fail("tangent generation requires a nonempty triangle mesh within 32-bit limits");
    for (auto const index : indices) {
        if (index >= vertices.size())
            fail("tangent generation: vertex index {} is out of bounds", index);
        auto const &vertex = vertices[index];
        float const length = glm::length(vertex.normal);
        if (!finite(vertex.position) || !std::isfinite(length) || length <= 0 ||
            !std::isfinite(vertex.uv.x) || !std::isfinite(vertex.uv.y))
            fail("tangent generation: vertex {} requires finite position, normal and UV", index);
    }
    for (std::size_t i = 0; i < indices.size(); i += 3) {
        auto const &a = vertices[indices[i]];
        auto const &b = vertices[indices[i + 1]];
        auto const &c = vertices[indices[i + 2]];
        auto const u = glm::dvec2{b.uv} - glm::dvec2{a.uv};
        auto const v = glm::dvec2{c.uv} - glm::dvec2{a.uv};
        double const uv_area = u.x * v.y - u.y * v.x;
        if (std::abs(uv_area) <= 1e-12 * glm::length(u) * glm::length(v))
            fail("tangent generation: triangle {} has degenerate UVs", i / 3);
        auto const edge1 = glm::dvec3{b.position} - glm::dvec3{a.position};
        auto const edge2 = glm::dvec3{c.position} - glm::dvec3{a.position};
        if (glm::length(glm::cross(edge1, edge2)) <=
            1e-12 * glm::length(edge1) * glm::length(edge2))
            fail("tangent generation: triangle {} has degenerate geometry", i / 3);
    }

    std::vector<glm::vec4> corners(indices.size(), glm::vec4{0});
    TangentContext input{vertices, indices, corners};
    SMikkTSpaceInterface interface{
        .m_getNumFaces =
            [](SMikkTSpaceContext const *value) {
                return static_cast<int>(context(value).indices.size() / 3);
            },
        .m_getNumVerticesOfFace = [](SMikkTSpaceContext const *, int) { return 3; },
        .m_getPosition =
            [](SMikkTSpaceContext const *value, float output[], int face, int corner) {
                copy_vector(std::span<float, 3>{output, 3},
                            vertex_at(value, face, corner).position);
            },
        .m_getNormal =
            [](SMikkTSpaceContext const *value, float output[], int face, int corner) {
                copy_vector(std::span<float, 3>{output, 3},
                            glm::normalize(vertex_at(value, face, corner).normal));
            },
        .m_getTexCoord =
            [](SMikkTSpaceContext const *value, float output[], int face, int corner) {
                auto const uv = vertex_at(value, face, corner).uv;
                output[0] = uv.x;
                output[1] = uv.y;
            },
        .m_setTSpaceBasic =
            [](SMikkTSpaceContext const *value, float const tangent[], float sign, int face,
               int corner) {
                context(value).corners[static_cast<std::size_t>(face) * 3 + corner] = {
                    tangent[0], tangent[1], tangent[2], sign};
            },
        .m_setTSpace = nullptr,
    };
    SMikkTSpaceContext generator{.m_pInterface = &interface, .m_pUserData = &input};
    if (!genTangSpaceDefault(&generator))
        fail("MikkTSpace tangent generation failed");

    // MikkTSpace returns a frame per corner. Welding by the old index alone would
    // overwrite distinct frames on mirrored UV islands or other tangent seams.
    std::vector<Vertex> result_vertices;
    std::vector<std::uint32_t> result_indices;
    std::unordered_map<Vertex, std::uint32_t> index_of_vertex;
    result_indices.reserve(indices.size());
    for (std::size_t i = 0; i < indices.size(); ++i) {
        auto vertex = vertices[indices[i]];
        vertex.tangent = corners[i];
        if (!valid_tangent_frame(vertex))
            fail("MikkTSpace produced an invalid tangent at corner {}", i);
        auto [entry, inserted] =
            index_of_vertex.try_emplace(vertex, static_cast<std::uint32_t>(result_vertices.size()));
        if (inserted)
            result_vertices.push_back(vertex);
        result_indices.push_back(entry->second);
    }
    vertices = std::move(result_vertices);
    indices = std::move(result_indices);
}

} // namespace lc1
