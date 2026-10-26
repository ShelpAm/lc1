#pragma once

#include "lc1/scene/mesh.hpp"
#include "lc1/scene/model-definition.hpp"
#include "lc1/scene/model-node.hpp"
#include "lc1/scene/transform.hpp"
#include "lc1/vk/resources/gpu-mesh.hpp"

#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <span>
#include <vector>

namespace lc1::scene {

class Model {
  public:
    static Model load_from_file(std::filesystem::path const &path, Transform correction = {});

    static Model load_from_obj(std::filesystem::path const &path, Transform correction = {});

    static Model load_from_gltf(std::filesystem::path const &path, Transform correction = {});

    Model(std::vector<Vertex> vertices, std::vector<std::uint32_t> indices,
          Transform correction = {});

    // Hierarchical models keep authored node transforms; this constructor applies no import
    // correction.
    Model(std::vector<Mesh> meshes, std::vector<ModelNode> nodes,
          std::vector<AnimationClip> animations = {});

    std::size_t mesh_count() const;
    ModelDefinition const &definition() const &;
    ModelDefinition definition() &&;
    ModelDefinition definition() const && = delete;

    // Static-model correction is baked once, before uploading or sharing a mesh.
    // Character/world transforms do not contain import-axis or unit conversions.
    Transform const &correction() const;

    std::span<Vertex const> vertices(std::size_t mesh_index = 0) const;

    std::span<std::uint32_t const> indices(std::size_t mesh_index = 0) const;

    // Preprocess before uploading or borrowing the model from any instance.
    void generate_tangents();

    GpuMesh gen_mesh(Device const &device, std::size_t mesh_index = 0) const;

  private:
    Model();

    Mesh const &mesh_at(std::size_t index) const;

    void apply_correction(Transform const &correction);

    static void apply_mesh_correction(Mesh &mesh, Transform const &correction);

    Transform correction_;
    std::vector<Mesh> meshes_;
    ModelDefinition definition_;
};

} // namespace lc1::scene
