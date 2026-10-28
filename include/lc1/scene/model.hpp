#pragma once

#include "lc1/scene/mesh.hpp"
#include "lc1/scene/model-definition.hpp"
#include "lc1/scene/model-node.hpp"

#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <span>
#include <vector>

namespace lc1::scene {

// CPU geometry and shared definition, fully assembled by construction.
class Model {
  public:
    static Model load_from_file(std::filesystem::path const &path);
    static Model load_from_obj(std::filesystem::path const &path);
    static Model load_from_gltf(std::filesystem::path const &path);

    Model(std::vector<Vertex> vertices, std::vector<std::uint32_t> indices);

    // Hierarchical models keep authored node transforms.
    Model(std::vector<Mesh> meshes, std::vector<ModelNode> nodes,
          std::vector<AnimationClip> animations = {});

    std::size_t mesh_count() const;
    ModelDefinition const &definition() const &;
    ModelDefinition definition() &&;
    ModelDefinition definition() const && = delete;

    std::span<Vertex const> vertices(std::size_t mesh_index = 0) const;

    std::span<std::uint32_t const> indices(std::size_t mesh_index = 0) const;

    // Preprocess before uploading or borrowing the model from any instance.
    void generate_tangents();

  private:
    std::vector<Mesh> meshes_;
    ModelDefinition definition_;
};

} // namespace lc1::scene
