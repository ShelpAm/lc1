#pragma once

#include "lc1/scene/gpu-model.hpp"
#include "lc1/scene/lights/light.hpp"
#include "lc1/scene/model-instance.hpp"
#include "lc1/vk/render/draw-item.hpp"

#include <memory>
#include <unordered_map>
#include <vector>

namespace lc1::scene {

// Model and material are required borrows, alive and unmoved until removal.
struct SceneModel {
    GpuModel const *model = nullptr;
    ModelInstance pose{definition(model)};
    MaterialInfo const *material = nullptr;
    glm::mat4 world_transform{1.0F};
    bool casts_shadow = true;
    bool depth_bias = false;
    bool visible = true;

    glm::mat4 node_world_matrix(std::size_t node) const;

  private:
    static ModelDefinition const &definition(GpuModel const *model);
};

struct SceneLight {
    Light parameters;
    bool enabled = true;
};

// Persistent CPU presentation state, independent of any selected camera.
// Owns placements, model poses/playback and light parameters, but never borrowed assets.
// Returned pointers borrow scene-owned objects. Additions and removal of other entries
// preserve them; removing the object or destroying the scene invalidates its pointer.
class Scene {
  public:
    SceneLight *add_light(Light light);
    void remove_light(SceneLight const *light);

    SceneModel *add_model(SceneModel model);
    void remove_model(SceneModel const *model);

    void update_animations(float delta_seconds);

    // Value snapshots, with unspecified collection order.
    // Subsequent scene edits cannot change them.
    std::vector<Light> lights() const;
    std::vector<DrawItem> draws() const;
    // Expanded mesh count, including hidden models.
    std::size_t draw_count() const;

  private:
    std::unordered_map<SceneLight const *, std::unique_ptr<SceneLight>> lights_;
    std::unordered_map<SceneModel const *, std::unique_ptr<SceneModel>> models_;
};

} // namespace lc1::scene
