#pragma once

#include "lc1/game/continent/continent.hpp"

namespace lc1 {

namespace scene {
class FpsCamera;
} // namespace scene

// Camera lock is independent of cursor capture. All state belongs to this scene.
// Inputs are finite; update/pan use nonnegative simulation time validated by GameWorld.
class MapCameraController {
  public:
    explicit MapCameraController(glm::vec3 player_position);

    void toggle_lock();

    void zoom(float scroll);
    void recenter(glm::vec3 player_position);
    void pan(float delta_seconds, glm::vec2 direction, GroundBounds bounds);
    void update(float delta_seconds, scene::FpsCamera &camera, glm::vec3 player_position);
    void apply(scene::FpsCamera &camera) const;

    bool locked() const;

    glm::vec3 center() const;

    // Target vertical span on the plane through center(), perpendicular to the viewing direction.
    // The displayed span approaches this target with the same smoothing as recenter.
    // Perspective zoom changes camera distance; apparent size still varies with depth.
    float view_height() const;

  private:
    bool locked_ = true;
    glm::vec3 center_;
    glm::vec3 smoothed_center_;
    float view_height_ = 48.0F;
    float smoothed_view_height_ = 48.0F;
};

} // namespace lc1
