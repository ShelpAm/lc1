#include "lc1/game/camera/map-camera-controller.hpp"

#include "lc1/scene/camera.hpp"

#include <algorithm>
#include <cmath>

namespace lc1 {
namespace {

constexpr glm::vec3 offset{32.0F, 48.0F, 32.0F};
constexpr glm::vec3 aim_height{0.0F, 0.9F, 0.0F};
constexpr float fov_y = 45.0F;

} // namespace

MapCameraController::MapCameraController(glm::vec3 player_position)
    : center_(player_position + aim_height), smoothed_center_(center_)
{
}

void MapCameraController::apply(scene::FpsCamera &camera) const
{
    // Frame the target at the requested span using distance, with a fixed lens and angle.
    float const distance = smoothed_view_height_ / (2.0F * std::tan(glm::radians(fov_y) * 0.5F));
    camera.set_position(smoothed_center_ + glm::normalize(offset) * distance);
    camera.look_at(smoothed_center_);
    camera.set_fov_y(fov_y);
}

void MapCameraController::zoom(float scroll)
{
    scroll = std::clamp(scroll, -100.0F, 100.0F);
    view_height_ = std::clamp(view_height_ * std::exp(-scroll * 0.12F), 16.0F, 64 * 96.0F);
}

void MapCameraController::recenter(glm::vec3 player_position)
{
    center_ = player_position + aim_height;
}

void MapCameraController::pan(float delta_seconds, glm::vec2 direction, GroundBounds bounds)
{
    if (locked_)
        return;
    direction /= std::max(glm::length(direction), 1.0F);
    float const elapsed = std::clamp(delta_seconds, 0.0F, 0.1F);
    auto const heading = glm::normalize(glm::vec3{-offset.x, 0, -offset.z});
    auto const right = glm::cross(heading, glm::vec3{0, 1, 0});
    center_ +=
        (right * direction.x + heading * direction.y) * smoothed_view_height_ * 0.7F * elapsed;
    center_.x = std::clamp(center_.x, bounds.min.x, bounds.max.x);
    center_.z = std::clamp(center_.z, bounds.min.y, bounds.max.y);
}

void MapCameraController::update(float delta_seconds, scene::FpsCamera &camera,
                                 glm::vec3 player_position)
{
    if (locked_)
        recenter(player_position);
    float const elapsed = std::clamp(delta_seconds, 0.0F, 0.1F);
    float const blend = std::clamp(elapsed * 12.0F, 0.0F, 1.0F);
    smoothed_center_ = glm::mix(smoothed_center_, center_, blend);
    smoothed_view_height_ = glm::mix(smoothed_view_height_, view_height_, blend);
    apply(camera);
}

} // namespace lc1

namespace lc1 {

void MapCameraController::toggle_lock()
{
    locked_ = !locked_;
}

bool MapCameraController::locked() const
{
    return locked_;
}

glm::vec3 MapCameraController::center() const
{
    return center_;
}

float MapCameraController::view_height() const
{
    return view_height_;
}

} // namespace lc1
