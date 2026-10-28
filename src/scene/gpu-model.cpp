#include "lc1/scene/gpu-model.hpp"

#include "lc1/error.hpp"
#include <utility>

namespace lc1::scene {
namespace {
std::vector<GpuMesh> upload_meshes(Device const &device, Model const &model)
{
    std::vector<GpuMesh> meshes;
    meshes.reserve(model.mesh_count());
    for (std::size_t i = 0; i < model.mesh_count(); ++i)
        meshes.emplace_back(device, model.vertices(i), model.indices(i));
    return meshes;
}
} // namespace

GpuModel::GpuModel(Device const &device, Model model)
    : meshes_(upload_meshes(device, model)), definition_(std::move(model).definition())
{
}

GpuModel::GpuModel(GpuMesh mesh) : definition_(1, {{.name = "Mesh", .mesh = 0}})
{
    meshes_.push_back(std::move(mesh));
}

GpuModel::GpuModel(std::vector<GpuMesh> meshes, ModelDefinition definition)
    : meshes_(std::move(meshes)), definition_(std::move(definition))
{
    for (auto const &node : definition_.nodes())
        if (node.mesh && *node.mesh >= meshes_.size())
            fail("GPU model mesh index is out of range");
}

ModelDefinition const &GpuModel::definition() const &
{
    return definition_;
}

std::span<GpuMesh const> GpuModel::meshes() const &
{
    return meshes_;
}

} // namespace lc1::scene
