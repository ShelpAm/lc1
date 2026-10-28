#pragma once

#include "lc1/scene/model.hpp"
#include "lc1/vk/resources/gpu-mesh.hpp"

namespace lc1::scene {

// Shared runtime asset. Uploads every mesh and releases the supplied CPU geometry.
// Device must outlive this owner; wait for GPU uses before destruction. Instances
// borrow the definition and meshes, so this owner stays at a stable address.
class GpuModel {
  public:
    GpuModel(Device const &device, Model model);
    // Adopt an uploaded mesh as a model with one identity root node.
    explicit GpuModel(GpuMesh mesh);
    // Adopt runtime resources together; every mesh-bearing node must resolve.
    GpuModel(std::vector<GpuMesh> meshes, ModelDefinition definition);
    GpuModel(GpuModel const &) = delete;
    GpuModel &operator=(GpuModel const &) = delete;
    GpuModel(GpuModel &&) = delete;
    GpuModel &operator=(GpuModel &&) = delete;
    ~GpuModel() = default;

    ModelDefinition const &definition() const &;
    ModelDefinition const &definition() const && = delete;
    std::span<GpuMesh const> meshes() const &;
    std::span<GpuMesh const> meshes() const && = delete;

  private:
    // Upload before extracting the definition from the CPU model.
    std::vector<GpuMesh> meshes_;
    ModelDefinition definition_;
};

} // namespace lc1::scene
