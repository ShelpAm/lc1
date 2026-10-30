#include "lc1/game/physics/character-motion.hpp"

#include "lc1/error.hpp"
#include "lc1/game/physics/world-collision.hpp"

#include <algorithm>
#include <cmath>

namespace lc1 {
namespace {

bool finite(glm::vec3 value)
{
    return std::isfinite(value.x) && std::isfinite(value.y) && std::isfinite(value.z);
}

} // namespace

CharacterMotion::CharacterMotion(WorldCollision const &collision, CharacterBody const &body,
                                 glm::vec3 &position, CharacterMotionSettings settings)
    : settings_(settings)
{
    if (!std::isfinite(settings_.gravity) || settings_.gravity <= 0.0F ||
        !std::isfinite(settings_.jump_height) || settings_.jump_height < 0.0F ||
        !std::isfinite(settings_.max_step_seconds) || settings_.max_step_seconds <= 0.0 ||
        static_cast<float>(settings_.max_step_seconds) == 0.0F)
        fail("invalid character motion settings");
    auto const support =
        collision.support_below(position + glm::vec3{0, body.step_height, 0},
                                body.step_height + body.ground_snap_distance, body.half_width);
    if (support) {
        auto candidate = position;
        candidate.y = support->height;
        if (collision.can_stand(candidate, body.half_width, body.height)) {
            position = candidate;
            on_ground_ = true;
        }
    }
}

void CharacterMotion::request_jump()
{
    jump_requested_ = true;
}

bool CharacterMotion::on_ground() const
{
    return on_ground_;
}

void CharacterMotion::update(float delta_seconds, WorldCollision const &collision,
                             CharacterBody const &body, glm::vec3 &position,
                             glm::vec3 desired_velocity)
{
    if (!std::isfinite(delta_seconds) || delta_seconds < 0.0F || !finite(desired_velocity) ||
        desired_velocity.y != 0.0F)
        fail("invalid character motion update");
    // Zero time preserves pending actions. Gameplay has already applied facing changes.
    double remaining = delta_seconds;
    while (remaining > 0.0) {
        double const step_seconds = std::min(remaining, settings_.max_step_seconds);
        // Fail explicitly if subtraction cannot represent progress, rather than loop forever.
        if (remaining - step_seconds == remaining)
            fail("character motion time exceeds stepping precision");
        step(static_cast<float>(step_seconds), collision, body, position, desired_velocity);
        remaining -= step_seconds;
    }
}

void CharacterMotion::step(float delta_seconds, WorldCollision const &collision,
                           CharacterBody const &body, glm::vec3 &position,
                           glm::vec3 desired_velocity)
{
    move(collision, body, position, desired_velocity * delta_seconds);
    if (jump_requested_ && on_ground_) {
        vertical_velocity_ = std::sqrt(2.0F * settings_.gravity * settings_.jump_height);
        on_ground_ = false;
    }
    jump_requested_ = false;

    if (on_ground_) {
        auto const support =
            collision.support_below(position + glm::vec3{0, body.step_height, 0},
                                    body.step_height + body.ground_snap_distance, body.half_width);
        auto candidate = position;
        if (support)
            candidate.y = support->height;
        if (support && collision.can_stand(candidate, body.half_width, body.height)) {
            position = candidate;
            vertical_velocity_ = 0.0F;
        }
        else {
            on_ground_ = false;
        }
    }

    if (!on_ground_) {
        // Horizontal movement can make terrain rise into an airborne character.
        auto const terrain = collision.ground_height({position.x, position.z});
        if (terrain && position.y < *terrain) {
            position.y = *terrain;
            vertical_velocity_ = 0.0F;
            on_ground_ = true;
        }
        else {
            advance_vertical(delta_seconds, collision, body, position);
        }
    }
}

void CharacterMotion::advance_vertical(float delta_seconds, WorldCollision const &collision,
                                       CharacterBody const &body, glm::vec3 &position)
{
    float const gravity = settings_.gravity;
    if (vertical_velocity_ > 0.0F) {
        // Split at the apex so the descending sweep starts at the highest reached point.
        float const rise_seconds = std::min(delta_seconds, vertical_velocity_ / gravity);
        float const rise =
            vertical_velocity_ * rise_seconds - 0.5F * gravity * rise_seconds * rise_seconds;
        auto const ceiling =
            collision.ceiling_above(position + glm::vec3{0, body.height, 0}, rise, body.half_width);
        if (ceiling) {
            float const distance = std::max(0.0F, *ceiling - position.y - body.height);
            float const impact_speed = std::sqrt(std::max(
                0.0F, vertical_velocity_ * vertical_velocity_ - 2.0F * gravity * distance));
            // Rationalized quadratic root avoids cancellation for a nearby ceiling.
            float const impact_seconds = 2.0F * distance / (vertical_velocity_ + impact_speed);
            position.y = *ceiling - body.height;
            vertical_velocity_ = 0.0F;
            delta_seconds -= std::min(impact_seconds, delta_seconds);
        }
        else {
            position.y += rise;
            vertical_velocity_ = std::max(0.0F, vertical_velocity_ - gravity * rise_seconds);
            delta_seconds -= rise_seconds;
        }
    }

    if (delta_seconds <= 0.0F)
        return;
    float const previous_height = position.y;
    position.y +=
        vertical_velocity_ * delta_seconds - 0.5F * gravity * delta_seconds * delta_seconds;
    vertical_velocity_ -= gravity * delta_seconds;
    auto const support =
        collision.support_below({position.x, previous_height, position.z},
                                previous_height - position.y + 0.001F, body.half_width);
    if (support && position.y <= support->height) {
        position.y = support->height;
        vertical_velocity_ = 0.0F;
        on_ground_ = true;
    }
}

void CharacterMotion::move(WorldCollision const &collision, CharacterBody const &body,
                           glm::vec3 &position, glm::vec3 displacement)
{
    if (!finite(displacement) || displacement.y != 0.0F)
        fail("ground character displacement must be finite and horizontal");
    auto const feet = position;
    glm::vec2 const horizontal{displacement.x, displacement.z};
    float const requested_distance = glm::length(horizontal);

    // Horizontal collision is height-aware. The resolved Y is the input feet height;
    // step() follows terrain and walkable surfaces vertically.
    auto const normal = collision.move(feet, horizontal, body.half_width, body.height);
    float const normal_progress =
        glm::distance(glm::vec2{normal.x, normal.z}, glm::vec2{feet.x, feet.z});

    // A walkable step can block the normal sweep even though its top is within the
    // body's step height. Retry from a raised feet position and accept only if it
    // makes progress onto a support surface above the current feet.
    if (on_ground_ && normal_progress + 0.001F < requested_distance) {
        glm::vec3 raised = feet;
        raised.y += body.step_height;
        if (!collision.ceiling_above(feet + glm::vec3{0, body.height, 0}, body.step_height,
                                     body.half_width) &&
            collision.can_stand(raised, body.half_width, body.height)) {
            auto const stepped = collision.move(raised, horizontal, body.half_width, body.height);
            float const stepped_progress =
                glm::distance(glm::vec2{stepped.x, stepped.z}, glm::vec2{feet.x, feet.z});
            if (stepped_progress > normal_progress + 0.001F) {
                auto const support = collision.support_below(
                    {stepped.x, raised.y, stepped.z}, body.step_height + body.ground_snap_distance,
                    body.half_width);
                if (support && support->height > feet.y + 0.001F &&
                    support->height <= feet.y + body.step_height + 0.001F) {
                    glm::vec3 const candidate{stepped.x, support->height, stepped.z};
                    if (collision.can_stand(candidate, body.half_width, body.height)) {
                        position = candidate;
                        return;
                    }
                }
            }
        }
    }

    position = normal;
}

} // namespace lc1
