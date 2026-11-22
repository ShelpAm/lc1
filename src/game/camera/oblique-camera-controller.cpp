#include "lc1/game/camera/oblique-camera-controller.hpp"

#include "lc1/error.hpp"
#include "lc1/scene/camera.hpp"

#include <algorithm>
#include <cmath>

namespace lc1 {
namespace {

constexpr glm::vec3 offset{32.0F, 48.0F, 32.0F};
constexpr glm::vec3 aim_height{0.0F, 0.9F, 0.0F};
constexpr float fov_y = 45.0F;
auto const view_direction_offset = glm::normalize(offset);
float const span_per_distance = 2.0F * std::tan(glm::radians(fov_y) * 0.5F);

} // namespace

ObliqueCameraController::ObliqueCameraController(glm::vec3 player_position)
    : center_(player_position + aim_height), smoothed_center_(center_)
{
}

void ObliqueCameraController::apply(scene::FpsCamera &camera) const
{
    // Frame the target at the requested span using distance, with a fixed lens and angle.
    float const distance = smoothed_view_height_ / span_per_distance;
    camera.set_position(smoothed_center_ + view_direction_offset * distance);
    camera.look_at(smoothed_center_);
    camera.set_fov_y(fov_y);
}

void ObliqueCameraController::set_height_limits(float min_height, float max_height)
{
    float const span_per_height = span_per_distance / view_direction_offset.y;
    float const min_span = min_height * span_per_height;
    float const max_span = max_height * span_per_height;
    if (!(min_span > 0.0F && min_span <= max_span && std::isfinite(max_span)))
        fail("oblique camera height limits must be positive, finite, and ordered");

    min_view_height_ = min_span;
    max_view_height_ = max_span;
    view_height_ = std::clamp(view_height_, min_view_height_, max_view_height_);
    smoothed_view_height_ = std::clamp(smoothed_view_height_, min_view_height_, max_view_height_);
}

void ObliqueCameraController::zoom(float scroll)
{
    scroll = std::clamp(scroll, -100.0F, 100.0F);
    view_height_ =
        std::clamp(view_height_ * std::exp(-scroll * 0.12F), min_view_height_, max_view_height_);
}

void ObliqueCameraController::recenter(glm::vec3 player_position)
{
    center_ = player_position + aim_height;
}

void ObliqueCameraController::set_recenter_held(bool held)
{
    recenter_held_ = held;
}

void ObliqueCameraController::pan(float delta_seconds, glm::vec2 direction, GroundBounds bounds)
{
    if (locked_ || recenter_held_)
        return;
    direction /= std::max(glm::length(direction), 1.0F);
    auto const heading = glm::normalize(glm::vec3{-offset.x, 0, -offset.z});
    auto const right = glm::cross(heading, glm::vec3{0, 1, 0});
    center_ += (right * direction.x + heading * direction.y) * smoothed_view_height_ * 0.7F *
               delta_seconds;
    center_.x = std::clamp(center_.x, bounds.min.x, bounds.max.x);
    center_.z = std::clamp(center_.z, bounds.min.y, bounds.max.y);
}

void ObliqueCameraController::update(float delta_seconds, scene::FpsCamera &camera,
                                     glm::vec3 player_position)
{
    if (locked_ || recenter_held_)
        recenter(player_position);
    float const blend = std::clamp(delta_seconds * 12.0F, 0.0F, 1.0F);
    smoothed_center_ = glm::mix(smoothed_center_, center_, blend);
    smoothed_view_height_ = glm::mix(smoothed_view_height_, view_height_, blend);
    apply(camera);
}

} // namespace lc1

namespace lc1 {

void ObliqueCameraController::toggle_lock()
{
    locked_ = !locked_;
}

bool ObliqueCameraController::locked() const
{
    return locked_;
}

glm::vec3 ObliqueCameraController::center() const
{
    return center_;
}

float ObliqueCameraController::view_height() const
{
    return view_height_;
}

} // namespace lc1
