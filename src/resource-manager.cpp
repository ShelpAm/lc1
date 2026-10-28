#include "lc1/resource-manager.hpp"

#include "lc1/scene/model.hpp"

namespace lc1 {
std::filesystem::path ResourceManager::to_resource_key(std::filesystem::path const &id)
{
    auto const key = id.lexically_normal();
    if (id.has_root_path() || key.empty() || key == "." || *key.begin() == "..") {
        fail("resource ID must be a path relative to the asset root: {}", id.string());
    }
    return key;
}

ResourceManager::ResourceManager(Device const &device, std::filesystem::path const &asset_root)
    : device_{&device}, asset_root_{std::filesystem::absolute(asset_root).lexically_normal()}
{
}

GpuMesh const &ResourceManager::load_mesh(std::filesystem::path const &path)
{
    auto const key = to_resource_key(path);
    if (auto const found = meshes_.find(key); found != meshes_.end()) {
        return *found->second;
    }
    auto const model = scene::Model::load_from_file(asset_root_ / key);
    auto mesh = std::make_unique<GpuMesh>(*device_, model.vertices(), model.indices());
    // Only cache completed resources; a failed load leaves no empty entry.
    auto const [entry, inserted] = meshes_.emplace(key, std::move(mesh));
    return *entry->second;
}

} // namespace lc1

namespace lc1 {
Resource::~Resource() = default;
}
