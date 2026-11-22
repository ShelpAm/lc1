#pragma once

#include "lc1/scene/mesh.hpp"
#include "lc1/scene/model-definition.hpp"
#include "lc1/scene/model-node.hpp"
#include "lc1/scene/pbr-material.hpp"
#include "lc1/scene/skin.hpp"

#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <optional>
#include <span>
#include <vector>

namespace lc1::scene {

struct EmbeddedImage {
    glm::uvec2 extent;
    std::vector<std::uint8_t> rgba;
};

struct EmbeddedMaterial {
    PbrParameters parameters;
    std::optional<std::size_t> base_color;
    std::optional<std::size_t> metallic_roughness;
    std::optional<std::size_t> normal;
    std::optional<std::size_t> emissive;
};

struct EmbeddedMaterials {
    std::vector<EmbeddedImage> images;
    std::vector<EmbeddedMaterial> materials;
    std::vector<std::size_t> mesh_materials;
};

// CPU geometry and shared definition, fully assembled by construction.
class Model {
  public:
    static Model load_from_file(std::filesystem::path const &path);
    static Model load_from_obj(std::filesystem::path const &path);
    static Model load_from_gltf(std::filesystem::path const &path);

    Model(std::vector<Vertex> vertices, std::vector<std::uint32_t> indices);

    // Hierarchical models keep authored node transforms.
    Model(std::vector<Mesh> meshes, std::vector<ModelNode> nodes,
          std::vector<AnimationClip> animations = {}, std::vector<SkinnedMesh> skins = {},
          EmbeddedMaterials materials = {});

    std::size_t mesh_count() const;
    ModelDefinition const &definition() const &;
    ModelDefinition definition() &&;
    ModelDefinition definition() const && = delete;

    std::span<Vertex const> vertices(std::size_t mesh_index = 0) const;

    std::span<std::uint32_t const> indices(std::size_t mesh_index = 0) const;

    std::span<SkinnedMesh const> skins() const;
    EmbeddedMaterials const &embedded_materials() const &;
    EmbeddedMaterials embedded_materials() &&;

    // Preprocess before uploading or borrowing the model from any instance.
    void generate_tangents();

  private:
    std::vector<Mesh> meshes_;
    ModelDefinition definition_;
    std::vector<SkinnedMesh> skins_;
    EmbeddedMaterials materials_;
};

} // namespace lc1::scene
