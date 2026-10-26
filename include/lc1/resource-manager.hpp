#pragma once

#include "lc1/vk/render/material.hpp"
#include "lc1/vk/resources/gpu-mesh.hpp"

#include <concepts>
#include <filesystem>
#include <memory>
#include <string>
#include <string_view>
#include <typeindex>
#include <unordered_map>

namespace lc1 {

class Device;

class Resource {
  public:
    Resource() = default;
    Resource(Resource const &) = delete;
    Resource(Resource &&) = default;
    Resource &operator=(Resource const &) = delete;
    Resource &operator=(Resource &&) = default;
    virtual ~Resource() = 0;
};

template <typename T>
concept HasLoadFromFile = requires(Device const &device, std::filesystem::path const &p) {
    { T::load_from_file(device, p) } -> std::same_as<T>;
};

// Scene-scoped, exclusive resource ownership. Returned references/pointers remain
// valid until that resource is unloaded or the manager is destroyed; loading more
// resources does not invalidate them. Before unloading or destroying resources,
// callers must stop using their borrows and wait for GPU uses to complete.
// Device must outlive this manager. Use from the render thread only.
class ResourceManager {
  public:
    ResourceManager(Device const &device, std::filesystem::path const &asset_root);
    ~ResourceManager() = default;
    ResourceManager(ResourceManager const &) = delete;
    ResourceManager &operator=(ResourceManager const &) = delete;
    ResourceManager(ResourceManager &&) = delete;
    ResourceManager &operator=(ResourceManager &&) = delete;

    template <std::derived_from<Resource> T>
        requires HasLoadFromFile<T>
    T &load(std::string_view id);

    template <typename T> void unload(std::string_view id);

    // IDs are normalized paths relative to asset_root, never machine-specific paths.
    GpuMesh const &load_mesh(std::filesystem::path const &path);

  private:
    static std::filesystem::path to_resource_key(std::filesystem::path const &id);

    // Non-owning, non-null; established by the constructor references.
    Device const *device_;
    std::filesystem::path asset_root_;

    // type_index, resource id -> resource
    std::unordered_map<std::type_index, std::unordered_map<std::string, std::unique_ptr<Resource>>>
        resources_;

    std::unordered_map<std::filesystem::path, std::unique_ptr<GpuMesh>> meshes_;
};

template <std::derived_from<Resource> T>
    requires HasLoadFromFile<T>
inline T &ResourceManager::load(std::string_view id)
{
    auto const type_key = std::type_index(typeid(T));
    auto &type_resources = resources_[type_key];
    auto const resource_key = to_resource_key(id).generic_string();
    if (auto const found = type_resources.find(resource_key); found != type_resources.end()) {
        // Each type bucket contains only resources created as T by this function.
        return static_cast<T &>(*found->second);
    }
    auto ptr = std::make_unique<T>(T::load_from_file(*device_, asset_root_ / resource_key));
    auto const [it, inserted] = type_resources.emplace(resource_key, std::move(ptr));
    return static_cast<T &>(*it->second);
}

template <typename T> inline void ResourceManager::unload(std::string_view id)
{
    auto const type_key = std::type_index(typeid(T));
    auto const resouce_key = to_resource_key(id).generic_string();
    resources_[type_key].extract(resouce_key);
}

} // namespace lc1
