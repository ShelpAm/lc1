#include "lc1/game/character/character.hpp"

#include "lc1/error.hpp"
#include "lc1/game/continent/continent.hpp"
#include "lc1/game/physics/world-collision.hpp"
#include "lc1/utf8-path.hpp"

#include <rfl/enums.hpp>
#include <rfl/yaml.hpp>

#include <algorithm>
#include <cmath>
#include <fstream>

namespace lc1 {
namespace {

struct MotionClipConfig {
    std::string clip;
    rfl::DefaultVal<float> speed_from;
    rfl::DefaultVal<float> speed_to;
    rfl::DefaultVal<CharacterAnimation> next{CharacterAnimation::Idle};
    rfl::DefaultVal<float> next_phase;
    rfl::DefaultVal<float> turn;
};

struct MotionFile {
    rfl::DefaultVal<std::array<float, 2>> attack_window{
        {combat::windup / combat::swing_duration, combat::active_end / combat::swing_duration}};
    std::map<std::string, MotionClipConfig> actions;
};

bool loops(CharacterAnimation clip)
{
    return clip == CharacterAnimation::Idle || clip == CharacterAnimation::Walk ||
           clip == CharacterAnimation::Sprint;
}

} // namespace

std::optional<AnimationMotion> load_animation_motion(ModelConfig const &config,
                                                     std::filesystem::path const &asset_root)
{
    if (config.path.value().empty())
        return std::nullopt;
    auto path = asset_root / path_from_utf8(config.path.value());
    path.replace_extension(".motion.yaml");
    if (!std::filesystem::exists(path))
        return std::nullopt;
    std::ifstream stream{path};
    if (!stream)
        fail("cannot open animation motion file {}", path.string());
    auto const file = rfl::yaml::read<MotionFile, rfl::NoOptionals, rfl::NoExtraFields>(stream);
    if (!file)
        fail("cannot read animation motion file {}: {}", path.string(), file.error().what());
    auto const [attack_start, attack_end] = file->attack_window.value();
    if (!(attack_start >= 0 && attack_start < attack_end && attack_end <= 1))
        fail("invalid attack window in {}", path.string());
    AnimationMotion motion{.attack_start = attack_start * combat::swing_duration,
                           .attack_end = attack_end * combat::swing_duration};
    auto const model = load_configured_model(config, asset_root, CharacterBody{});
    for (auto const &[name, entry] : file->actions) {
        auto const action = rfl::string_to_enum<CharacterAnimation>(name);
        if (!action || *action == CharacterAnimation::Count || !loops(entry.next.value()) ||
            !(entry.next_phase.value() >= 0 && entry.next_phase.value() < 1) ||
            !std::isfinite(entry.turn.value()) || !std::isfinite(entry.speed_from.value()) ||
            !std::isfinite(entry.speed_to.value()) || entry.speed_from.value() < 0 ||
            entry.speed_to.value() < 0)
            fail("invalid animation motion action {} in {}", name, path.string());
        auto const binding = config.animation_bindings.value().find(name);
        auto const &clip =
            binding == config.animation_bindings.value().end() ? entry.clip : binding->second;
        auto const index = model.definition().find_animatinon(clip);
        if (!index)
            fail("model is missing animation {} for {}", clip, name);
        auto const duration = model.definition().animations()[*index].duration();
        if (!std::isfinite(duration) || duration <= 0)
            fail("animation {} must have a positive duration", clip);
        motion.clips[static_cast<std::size_t>(*action)] = {clip,
                                                           duration,
                                                           entry.speed_from.value(),
                                                           entry.speed_to.value(),
                                                           entry.next.value(),
                                                           entry.next_phase.value(),
                                                           entry.turn.value()};
    }
    for (auto const action :
         {CharacterAnimation::Idle, CharacterAnimation::Walk, CharacterAnimation::Sprint})
        if (motion[action].duration == 0)
            fail("animation motion file {} is missing {}", path.string(),
                 rfl::enum_to_string(action));
    return motion;
}

std::optional<TimedCharacterAnimation> Character::timed_animation() const
{
    if (!timed_motion_)
        return std::nullopt;
    auto const &profile = *config_.animation_motion;
    if (dead())
        return profile[CharacterAnimation::Death].duration > 0
                   ? std::optional{TimedCharacterAnimation{CharacterAnimation::Death,
                                                           reaction_time_}}
                   : std::nullopt;
    if (reacting_to_hit())
        return profile[CharacterAnimation::HitReact].duration > 0
                   ? std::optional{TimedCharacterAnimation{CharacterAnimation::HitReact,
                                                           reaction_time_}}
                   : std::nullopt;
    if (attack_progress() < 1) {
        auto const duration = profile[CharacterAnimation::Attack].duration;
        return duration > 0 ? std::optional{TimedCharacterAnimation{CharacterAnimation::Attack,
                                                                    attack_progress() * duration}}
                            : std::nullopt;
    }
    return timed_motion_;
}

void Character::update_animation_motion(float delta_seconds, Continent const &continent,
                                        CharacterIntent const &intent)
{
    auto &animation = *timed_motion_;
    auto const &profile = *config_.animation_motion;
    auto const start = [&](CharacterAnimation clip, CharacterAnimation fallback) {
        animation = {profile[clip].duration > 0 ? clip : fallback};
        animation_turn_start_ = body_yaw();
    };
    WorldCollision const collision{continent};
    auto const before = position();
    running_ = false;

    if (dead() || reacting_to_hit() || attack_progress() < 1) {
        animation = {};
        motion_.update(delta_seconds, collision, body_, transform_.position, {});
        moving_ = false;
        return;
    }

    glm::vec3 const move{intent.move.x, 0, intent.move.y};
    float const strength = std::min(glm::length(move), 1.0F);
    bool const wants_move = strength > 0;
    auto const facing =
        wants_move && intent.face_movement ? move : intent.body_direction.value_or(glm::vec3{});
    auto const target_yaw = facing.x != 0 || facing.z != 0
                                ? glm::degrees(std::atan2(-facing.x, -facing.z))
                                : intent.body_yaw.value_or(body_yaw());

    while (delta_seconds > 0) {
        float const turn = std::remainder(target_yaw - body_yaw(), 360.0F);
        float const turn_size = std::abs(turn);
        bool const left = turn > 0;
        if (loops(animation.clip)) {
            bool const idle = animation.clip == CharacterAnimation::Idle;
            bool const sprinting = animation.clip == CharacterAnimation::Sprint;
            auto const &loop = profile[animation.clip];
            float const half_cycle = loop.duration * 0.5F;
            bool const left_foot = animation.seconds == 0;
            bool const planted = left_foot || animation.seconds == half_cycle;
            if (idle && !intent.body_yaw && turn_size >= 67.5F) {
                start(turn_size >= 135 ? (left ? CharacterAnimation::TurnLeft180
                                               : CharacterAnimation::TurnRight180)
                                       : (left ? CharacterAnimation::TurnLeft90
                                               : CharacterAnimation::TurnRight90),
                      CharacterAnimation::Idle);
                if (animation.clip == CharacterAnimation::Idle)
                    set_body_yaw(target_yaw);
            }
            else if (idle && wants_move) {
                set_body_yaw(target_yaw);
                start(intent.sprint ? CharacterAnimation::SprintStart
                                    : CharacterAnimation::WalkStart,
                      intent.sprint ? CharacterAnimation::Sprint : CharacterAnimation::Walk);
            }
            else if (!idle && planted &&
                     (!wants_move || (turn_size >= 67.5F && intent.face_movement))) {
                start(sprinting ? (left_foot ? CharacterAnimation::SprintStopLeft
                                             : CharacterAnimation::SprintStopRight)
                                : (left_foot ? CharacterAnimation::WalkStopLeft
                                             : CharacterAnimation::WalkStopRight),
                      CharacterAnimation::Idle);
            }
            else if (!idle && left_foot && sprinting != intent.sprint) {
                start(sprinting ? CharacterAnimation::SprintToWalk
                                : CharacterAnimation::WalkToSprint,
                      sprinting ? CharacterAnimation::Walk : CharacterAnimation::Sprint);
            }
            else if (!idle && left_foot && intent.face_movement && turn_size >= 22.5F) {
                start(sprinting ? (left ? CharacterAnimation::SprintTurnLeft
                                        : CharacterAnimation::SprintTurnRight)
                                : (left ? CharacterAnimation::WalkTurnLeft
                                        : CharacterAnimation::WalkTurnRight),
                      sprinting ? CharacterAnimation::Sprint : CharacterAnimation::Walk);
                if (loops(animation.clip))
                    set_body_yaw(target_yaw);
            }
            else if (idle || (wants_move && (intent.body_yaw || turn_size < 22.5F))) {
                set_body_yaw(target_yaw);
            }
        }

        auto const &clip = profile[animation.clip];
        bool const looping = loops(animation.clip);
        float const boundary = looping && animation.clip != CharacterAnimation::Idle &&
                                       animation.seconds < clip.duration * 0.5F
                                   ? clip.duration * 0.5F
                                   : clip.duration;
        float const step = std::min(delta_seconds, boundary - animation.seconds);
        float const previous = animation.seconds / clip.duration;
        animation.seconds = std::min(boundary, animation.seconds + step);
        float const progress = animation.seconds / clip.duration;
        if (clip.turn != 0) {
            set_body_yaw(animation_turn_start_ +
                         clip.turn * progress * progress * (3 - 2 * progress));
        }
        bool const stopping = clip.speed_from > 0 && clip.speed_to == 0;
        if (wants_move && !stopping && clip.turn == 0) {
            animation_move_direction_ =
                intent.face_movement ? body_heading() : move / glm::length(move);
        }
        else if (clip.turn != 0) {
            animation_move_direction_ = body_heading();
        }
        float const speed = std::lerp(clip.speed_from, clip.speed_to, (previous + progress) * 0.5F);
        motion_.update(step, collision, body_, transform_.position,
                       animation_move_direction_ * speed *
                           (wants_move && !stopping ? strength : 1));
        running_ = speed > profile[CharacterAnimation::Walk].speed_to;
        delta_seconds -= step;
        if (animation.seconds == clip.duration)
            animation = looping ? TimedCharacterAnimation{animation.clip}
                                : TimedCharacterAnimation{
                                      clip.next, clip.next_phase * profile[clip.next].duration};
    }
    auto const displacement = position() - before;
    moving_ = glm::length(glm::vec2{displacement.x, displacement.z}) > 0.0001F;
    ambient_requests_blocked_ |= animation.clip != CharacterAnimation::Idle;
}

} // namespace lc1
