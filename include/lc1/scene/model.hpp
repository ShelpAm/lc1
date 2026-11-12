#pragma once

#include "lc1/scene/mesh.hpp"
#include "lc1/scene/model-definition.hpp"
#include "lc1/scene/model-node.hpp"
#include "lc1/scene/skin.hpp"

#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <span>
#include <vector>

namespace lc1::scene {

// Geometry import does not import glTF materials. Callers supplying draw materials
// explicitly can acknowledge that contract instead of receiving the default warning.
enum class GltfMaterialPolicy { WarnIfPresent, UseExternal };

// CPU geometry and shared definition, fully assembled by construction.
class Model {
  public:
    static Model load_from_file(std::filesystem::path const &path,
                                GltfMaterialPolicy materials = GltfMaterialPolicy::WarnIfPresent);
    static Model load_from_obj(std::filesystem::path const &path);
    static Model load_from_gltf(std::filesystem::path const &path,
                                GltfMaterialPolicy materials = GltfMaterialPolicy::WarnIfPresent);

    Model(std::vector<Vertex> vertices, std::vector<std::uint32_t> indices);

    // Hierarchical models keep authored node transforms.
    Model(std::vector<Mesh> meshes, std::vector<ModelNode> nodes,
          std::vector<AnimationClip> animations = {}, std::vector<SkinnedMesh> skins = {});

    std::size_t mesh_count() const;
    ModelDefinition const &definition() const &;
    ModelDefinition definition() &&;
    ModelDefinition definition() const && = delete;

    std::span<Vertex const> vertices(std::size_t mesh_index = 0) const;

    std::span<std::uint32_t const> indices(std::size_t mesh_index = 0) const;

    std::span<SkinnedMesh const> skins() const;

    // Preprocess before uploading or borrowing the model from any instance.
    void generate_tangents();

  private:
    std::vector<Mesh> meshes_;
    ModelDefinition definition_;
    std::vector<SkinnedMesh> skins_;
};

} // namespace lc1::scene
