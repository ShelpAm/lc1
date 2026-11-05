#pragma once

namespace lc1 {

// Body proportions in metres, plus walking and running speed. The placeholder mesh and the
// collision body are both built from these numbers, so the two cannot silently disagree.
struct CharacterBody {
    float half_width = 0.3F;
    float height = 1.8F;
    float eye_height = 1.65F;
    float walk_speed = 4.5F;
    float run_speed = 18.0F;
    // Maximum ledge/ramp rise followed while grounded, and maximum distance the body
    // snaps downward to remain attached to a slope.
    float step_height = 0.35F;
    float ground_snap_distance = 0.35F;
};

} // namespace lc1
