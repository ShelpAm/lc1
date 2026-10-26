#include "lc1/scene/camera.hpp"

namespace lc1::scene {

glm::mat4 FpsCamera::view_matrix() const
{
    return glm::lookAt(position_, position_ + forward(), world_up);
}

glm::mat4 FpsCamera::projection_matrix() const
{
    return glm::perspective(glm::radians(fov_y_), aspect_ratio_, znear_, zfar_);
}

glm::mat4 FpsCamera::view_projection_matrix() const
{
    return projection_matrix() * view_matrix();
}

glm::vec3 FpsCamera::forward() const
{
    glm::vec3 direction;
    direction.x = glm::cos(glm::radians(yaw_)) * glm::cos(glm::radians(pitch_));
    direction.y = glm::sin(glm::radians(pitch_));
    direction.z = glm::sin(glm::radians(yaw_)) * glm::cos(glm::radians(pitch_));
    return glm::normalize(direction);
}

glm::vec3 FpsCamera::right() const
{
    return glm::normalize(glm::cross(forward(), world_up));
}

glm::vec3 FpsCamera::up() const
{
    return glm::normalize(glm::cross(right(), forward()));
}

glm::vec3 FpsCamera::heading() const
{
    glm::vec3 direction;
    direction.x = glm::cos(glm::radians(yaw_));
    direction.y = 0.0F;
    direction.z = glm::sin(glm::radians(yaw_));
    return glm::normalize(direction);
}

void FpsCamera::add_yaw(float delta)
{
    yaw_ = std::fmod(yaw_ + delta + 180.0F, 360.0F);
    if (yaw_ < 0.0F)
        yaw_ += 360.0F;
    yaw_ -= 180.0F;
}

void FpsCamera::add_pitch(float delta)
{
    pitch_ += delta;
    pitch_ = std::clamp(pitch_, -89.0F, 89.0F);
}

bool FpsCamera::look_at(glm::vec3 target)
{
    glm::vec3 diff = target - position_;
    float len = glm::length(diff);
    if (len < 1e-6F)
        return false;
    glm::vec3 direction = diff / len;

    // Clamp rounding errors outside [-1, 1] to keep asin from returning NaN.
    float y = glm::clamp(direction.y, -1.0F, 1.0F);
    pitch_ = glm::clamp(glm::degrees(std::asin(y)), -89.0F, 89.0F);

    // Preserve yaw when the horizontal direction is too small to define it.
    float horizontal_sq = direction.x * direction.x + direction.z * direction.z;
    if (horizontal_sq > 1e-12F)
        yaw_ = glm::degrees(std::atan2(direction.z, direction.x));
    return true;
}

void FpsCamera::set_znear(float znear)
{
    znear_ = znear;
}

void FpsCamera::set_zfar(float zfar)
{
    zfar_ = zfar;
}

void FpsCamera::set_fov_y(float fov_y)
{
    fov_y_ = fov_y;
}

void FpsCamera::set_aspect_ratio(float aspect_ratio)
{
    aspect_ratio_ = aspect_ratio;
}

glm::vec3 FpsCamera::position() const
{
    return position_;
}

void FpsCamera::set_position(glm::vec3 position)
{
    position_ = position;
}

void FpsCamera::move_groud(glm::vec3 groud_delta)
{
    position_ += right() * groud_delta.x + world_up * groud_delta.y + heading() * groud_delta.z;
}

void FpsCamera::move_local(glm::vec3 local_delta)
{
    position_ += right() * local_delta.x + up() * local_delta.y + forward() * local_delta.z;
}

} // namespace lc1::scene
