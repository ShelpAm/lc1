#include "lc1/scene/model.hpp"
#include "lc1/error.hpp"
#include "lc1/tangent-space.hpp"
#include <algorithm>
#include <cmath>
#include <ranges>
#include <tiny_obj_loader.h>
#include <unordered_map>
#include <utility>

namespace lc1::scene {

Model Model::load_from_file(std::filesystem::path const &path)
{
    auto extension = path.extension();
    if (extension == ".obj")
        return load_from_obj(path);
    if (extension == ".gltf" || extension == ".glb")
        return load_from_gltf(path);
    fail("unsupported model file extension: {}", path.extension().string());
}

Model Model::load_from_obj(std::filesystem::path const &path)
{
    tinyobj::attrib_t attrib;
    std::vector<tinyobj::shape_t> shapes;
    std::vector<tinyobj::material_t> materials;
    std::string warn;
    std::string err;

    if (!tinyobj::LoadObj(&attrib, &shapes, &materials, &warn, &err, path.string().c_str())) {
        throw std::runtime_error(warn + err);
    }

    std::vector<Vertex> vertices;
    std::vector<std::uint32_t> indices;
    std::unordered_map<Vertex, std::uint32_t> index_of_vertex;

    auto position_of = [&](tinyobj::index_t const &index) {
        auto idx = static_cast<std::size_t>(index.vertex_index);
        return glm::vec3{attrib.vertices[3 * idx], attrib.vertices[3 * idx + 1],
                         attrib.vertices[3 * idx + 2]};
    };
    // LoadObj triangulates by default. Missing normals use flat triangle normals;
    // including normals in Vertex equality preserves authored hard edges.
    for (auto const &shape : shapes) {
        for (std::size_t i = 0; i + 2 < shape.mesh.indices.size(); i += 3) {
            auto const face_normal = glm::cross(
                position_of(shape.mesh.indices[i + 1]) - position_of(shape.mesh.indices[i]),
                position_of(shape.mesh.indices[i + 2]) - position_of(shape.mesh.indices[i]));
            float const face_length = glm::length(face_normal);
            for (std::size_t corner = 0; corner < 3; ++corner) {
                auto const &index = shape.mesh.indices[i + corner];
                Vertex vertex{.position = position_of(index), .uv = {0.0F, 0.0F}};
                if (index.texcoord_index >= 0)
                    vertex.uv = {attrib.texcoords[2 * index.texcoord_index],
                                 attrib.texcoords[2 * index.texcoord_index + 1]};
                if (face_length > 0.0F)
                    vertex.normal = face_normal / face_length;
                if (index.normal_index >= 0) {
                    glm::vec3 const normal{attrib.normals[3 * index.normal_index],
                                           attrib.normals[3 * index.normal_index + 1],
                                           attrib.normals[3 * index.normal_index + 2]};
                    float const length = glm::length(normal);
                    if (length > 0.0F)
                        vertex.normal = normal / length;
                }
                auto [it, inserted] = index_of_vertex.insert({vertex, vertices.size()});
                if (inserted)
                    vertices.push_back(vertex);
                indices.push_back(it->second);
            }
        }
    }

    return Model{std::move(vertices), std::move(indices)};
}

Model::Model(std::vector<Vertex> vertices, std::vector<std::uint32_t> indices)
    : Model({Mesh(std::move(vertices), std::move(indices))},
            {ModelNode{.name = "root (from raw mesh)", .mesh = 0}})
{
}

Model::Model(std::vector<Mesh> meshes, std::vector<ModelNode> nodes,
             std::vector<AnimationClip> animations)
    : meshes_(std::move(meshes)),
      definition_{meshes_.size(), std::move(nodes), std::move(animations)}
{
}

std::size_t Model::mesh_count() const
{
    return meshes_.size();
}

ModelDefinition const &Model::definition() const &
{
    return definition_;
}

ModelDefinition Model::definition() &&
{
    return std::move(definition_);
}

std::span<Vertex const> Model::vertices(std::size_t mesh_index) const
{
    if (mesh_index >= meshes_.size())
        fail("model mesh index is out of range");
    return meshes_[mesh_index].vertices_;
}

std::span<std::uint32_t const> Model::indices(std::size_t mesh_index) const
{
    if (mesh_index >= meshes_.size())
        fail("model mesh index is out of range");
    return meshes_[mesh_index].indices_;
}

void Model::generate_tangents()
{
    for (auto &mesh : meshes_)
        lc1::generate_tangents(mesh.vertices_, mesh.indices_);
}

} // namespace lc1::scene
