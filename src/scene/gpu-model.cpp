#include "lc1/scene/gpu-model.hpp"

#include "lc1/error.hpp"

#include <array>
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

GpuModel::GpuModel(Device const &device, Model model, MaterialInfo const &texture_overrides)
    : meshes_(upload_meshes(device, model)), definition_(std::move(model).definition()),
      skins_(model.skins().begin(), model.skins().end())
{
    auto source = std::move(model).embedded_materials();
    mesh_materials_ = std::move(source.mesh_materials);
    std::vector<std::array<GpuTexture const *, 2>> uploaded(source.images.size());
    auto const upload = [&](std::optional<std::size_t> image_index, TextureColorSpace space,
                            MaterialTexture override) -> MaterialTexture {
        if (override.texture)
            return override;
        if (!image_index)
            return {};
        auto const &image = source.images.at(*image_index);
        auto &texture = uploaded[*image_index][space == TextureColorSpace::Srgb ? 0 : 1];
        if (!texture) {
            material_textures_.push_back(std::make_unique<GpuTexture>(
                device, vk::Extent2D{image.extent.x, image.extent.y}, image.rgba, space));
            texture = material_textures_.back().get();
        }
        return {.texture = texture};
    };
    materials_.reserve(source.materials.size());
    for (auto const &material : source.materials) {
        materials_.push_back({
            .parameters = material.parameters,
            .base_color =
                upload(material.base_color, TextureColorSpace::Srgb, texture_overrides.base_color),
            .metallic_roughness = upload(material.metallic_roughness, TextureColorSpace::Linear,
                                         texture_overrides.metallic_roughness),
            .normal = upload(material.normal, TextureColorSpace::Linear, texture_overrides.normal),
            .emissive =
                upload(material.emissive, TextureColorSpace::Srgb, texture_overrides.emissive),
        });
    }
}

GpuModel::GpuModel(GpuMesh mesh) : definition_(1, {{.name = "Mesh", .mesh_index = 0}})
{
    meshes_.push_back(std::move(mesh));
}

GpuModel::GpuModel(std::vector<GpuMesh> meshes, ModelDefinition definition)
    : meshes_(std::move(meshes)), definition_(std::move(definition))
{
    for (auto const &node : definition_.nodes())
        if (node.mesh_index && *node.mesh_index >= meshes_.size())
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

bool GpuModel::has_embedded_materials() const
{
    return !mesh_materials_.empty();
}

MaterialInfo const &GpuModel::embedded_material(std::size_t mesh_index) const
{
    return materials_[mesh_materials_[mesh_index]];
}

} // namespace lc1::scene
