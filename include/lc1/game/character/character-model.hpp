#pragma once

#include "lc1/scene/model-instance.hpp"
#include "lc1/scene/model.hpp"

#include <array>

namespace lc1 {

struct CharacterBody;

namespace scene {
class ModelInstance;
}

// Per-instance Remiel presentation state; does not affect gameplay or shared clips.
class RemielMotion {
  public:
    explicit RemielMotion(scene::ModelDefinition const &model);
    void restore(scene::ModelInstance &pose);
    void update(scene::ModelInstance &pose, glm::mat4 const &world, float delta_seconds, bool idle,
                bool dead, bool pointing = false, glm::vec3 point_direction = {0, 0, -1},
                float prone_amount = 0);

  private:
    struct Spring {
        std::size_t node;
        std::optional<std::size_t> auxiliary;
        glm::vec3 previous_anchor{};
        glm::vec3 previous_velocity{};
        glm::vec3 offset{};
        glm::vec3 velocity{};
    };

    std::vector<Spring> springs_;
    std::vector<scene::NodeTransform> base_pose_;
    std::vector<scene::NodeTransform> displayed_pose_;
    std::optional<std::size_t> head_;
    std::optional<std::size_t> torso_;
    std::vector<std::pair<std::size_t, glm::vec3>> pointing_pose_;
    std::vector<std::pair<std::size_t, std::size_t>> pointing_chain_;
    float time_ = 0;
    bool initialized_ = false;
};

// Remiel lower-body vertical stretch; call on the current unmodified animation pose.
void apply_remiel_leg_height(scene::ModelInstance &pose, float scale, float prone_amount = 0);
// CPU-only placeholder content, shared by the game and rendering tests.
scene::Model make_character_model(CharacterBody const &body);
scene::Model make_flashlight_model();
scene::Model make_torch_model();
scene::Model make_helmet_model();
scene::Model make_inventory_model(std::uint32_t resource);
// Visual-only rigid recoil (0.3 s) and forward fall (0.6 s, final pose held).
glm::mat4 character_reaction_transform(bool dead, float elapsed, CharacterBody const &body);

} // namespace lc1
