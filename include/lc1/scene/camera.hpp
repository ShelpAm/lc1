#pragma once

#include <algorithm>
#include <glm/glm.hpp>
#include <glm/gtc/matrix_transform.hpp>

namespace lc1::scene {

class FpsCamera {
  public:
    FpsCamera() = default;

    glm::mat4 view_matrix() const;

    glm::mat4 projection_matrix() const;

    glm::mat4 view_projection_matrix() const;

    glm::vec3 forward() const;
    glm::vec3 right() const;
    glm::vec3 up() const;

    // Without pitch
    glm::vec3 heading() const;

    void add_yaw(float delta);

    void add_pitch(float delta);

    /// @return False if the target is too close to the camera to compute a direction. Otherwise,
    /// returns true.
    bool look_at(glm::vec3 target);

    void set_znear(float znear);
    void set_zfar(float zfar);
    void set_fov_y(float fov_y);
    void set_aspect_ratio(float aspect_ratio);

    glm::vec3 position() const;
    void set_position(glm::vec3 position);

    // +x->right +y->world_up -z->heading, in camera space.
    void move_groud(glm::vec3 groud_delta);

    void move_local(glm::vec3 local_delta);

  private:
    static constexpr glm::vec3 world_up{0, 1, 0};
    // initialized to look along the negative Z-axis
    float yaw_{-90.0F};
    float pitch_{0.0F};

    float znear_{0.1F};
    float zfar_{10.0F};
    float fov_y_{45.0F};
    float aspect_ratio_{16.0F / 9.0F};

    glm::vec3 position_{};
};

} // namespace lc1::scene
