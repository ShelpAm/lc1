// Layer-based volumetric clouds: one view-ray march through a horizontal density
// layer, with a short sun march at every sample that carries density.
//
// Output is linear radiance in rgb and transmittance in a, so the composite pass
// reconstructs `radiance + transmittance * background` exactly. Nothing here is
// blended or gamma encoded: both intermediate images stay in HDR until tone mapping.

struct CloudData {
    // World position from clip space, stored column by column: whole-matrix loads
    // through a heap ConstantBuffer are mishandled by the NVIDIA driver here, see
    // docs/external-defects.md.
    float4 inverse_view_projection_0;
    float4 inverse_view_projection_1;
    float4 inverse_view_projection_2;
    float4 inverse_view_projection_3;
    float4 camera_position_time;     // xyz camera position; w wind time in seconds
    float4 sun_direction_anisotropy; // xyz unit vector towards the sun; w phase anisotropy
    float4 sun_radiance_albedo;      // rgb linear sun radiance; w scattering albedo
    float4 ambient_extinction;       // rgb linear ambient radiance; w extinction per metre
    float4 layer;                    // bottom, top, coverage, density scale
    float4 wind_frequency;           // xyz wind velocity in m/s; w shape frequency, 1/m
    float4 march;                    // x max trace distance, y light march distance
    uint4 steps;                     // x view steps, y light steps, z debug mode, w reserved
};

struct CloudPushData {
    uint cloud;
    uint depth;
    uint shape_noise;
    uint noise_sampler;
};
[[vk::push_constant]] ConstantBuffer<CloudPushData> push;

// Copy every field out of the heap buffer before marching: the loops below read the
// parameters hundreds of times per pixel.
CloudData load_cloud_data(ConstantBuffer<CloudData> source)
{
    CloudData data;
    data.inverse_view_projection_0 = source.inverse_view_projection_0;
    data.inverse_view_projection_1 = source.inverse_view_projection_1;
    data.inverse_view_projection_2 = source.inverse_view_projection_2;
    data.inverse_view_projection_3 = source.inverse_view_projection_3;
    data.camera_position_time = source.camera_position_time;
    data.sun_direction_anisotropy = source.sun_direction_anisotropy;
    data.sun_radiance_albedo = source.sun_radiance_albedo;
    data.ambient_extinction = source.ambient_extinction;
    data.layer = source.layer;
    data.wind_frequency = source.wind_frequency;
    data.march = source.march;
    data.steps = source.steps;
    return data;
}

float3 clip_to_world(float2 ndc, float depth, CloudData data)
{
    float4 homogeneous = data.inverse_view_projection_0 * ndc.x +
                               data.inverse_view_projection_1 * ndc.y +
                               data.inverse_view_projection_2 * depth +
                               data.inverse_view_projection_3 * 1.0;
    return homogeneous.xyz / homogeneous.w;
}

float height_profile(float relative_height)
{
    // Smooth base and top, dense middle: a flat layer never shows a hard edge.
    return smoothstep(0.0, 0.15, relative_height) * (1.0 - smoothstep(0.6, 1.0, relative_height));
}

// The single density definition shared by the view march and the sun march.
// Sampling the same field at the same world position from both is what keeps cloud
// bodies and their self shadowing consistent.
float sample_cloud_density(float3 world_position, CloudData data, Texture3D<float> shape_noise,
                           SamplerState shape_filter)
{
    float3 sample_position =
        world_position - data.wind_frequency.xyz * data.camera_position_time.w;
    float relative_height =
        (sample_position.y - data.layer.x) / max(data.layer.y - data.layer.x, 1e-3);
    if (relative_height <= 0.0 || relative_height >= 1.0)
        return 0.0;

    float shape = shape_noise.SampleLevel(shape_filter, sample_position * data.wind_frequency.w,
                                                0.0).r;
    // Coverage remaps the noise: with a low threshold more of the field becomes cloud,
    // so coverage grows and the fraction below the threshold shrinks to a hard wall only
    // at the two ends of the range.
    float coverage = max(data.layer.z, 1e-3);
    float density = saturate((shape - (1.0 - coverage)) / coverage);
    return density * height_profile(relative_height) * data.layer.w;
}

float henyey_greenstein(float cosine, float anisotropy)
{
    float squared = anisotropy * anisotropy;
    float denominator = max(1.0 + squared - 2.0 * anisotropy * cosine, 1e-4);
    return (1.0 - squared) / (4.0 * 3.14159265359 * pow(denominator, 1.5));
}

// Optical depth towards the sun over a short segment, clamped to the layer top. This
// is the density integral; extinction turns it into a transmittance.
float sun_optical_depth(float3 position, CloudData data, Texture3D<float> shape_noise,
                        SamplerState shape_filter)
{
    float3 to_sun = data.sun_direction_anisotropy.xyz;
    // A source at or below the horizon does not light the layer from above.
    if (to_sun.y <= 0.0)
        return 1e4;

    float to_top = (data.layer.y - position.y) / to_sun.y;
    float distance = clamp(min(data.march.y, to_top), 0.0, data.march.y);
    uint samples = max(data.steps.y, 1u);
    float step_length = distance / float(samples);

    float optical_depth = 0.0;
    for (uint i = 0; i < samples; ++i) {
        float3 sample = position + to_sun * ((float(i) + 0.5) * step_length);
        optical_depth +=
            sample_cloud_density(sample, data, shape_noise, shape_filter) * step_length;
    }
    return optical_depth;
}

float sun_transmittance(float optical_depth, CloudData data)
{
    return exp(-data.ambient_extinction.w * optical_depth);
}

// Sunlight reaching the eye from one sample: single scattering plus a two-octave
// approximation of multiple scattering. Light that has scattered more than once leaves
// the cloud nearly isotropic and survives a longer path, so each further octave assumes
// half the extinction and carries half the energy of the previous one, normalised as a
// diffuse emitter (1/pi) instead of by the single-scattering phase function. Without it
// an optically thick cloud saturates at the phase function and reads as a dark grey
// sheet instead of a white one. Self shadowing survives: every octave still measures
// its own optical depth towards the sun.
float3 scattered_sunlight(float optical_depth, float cosine, CloudData data)
{
    float single = sun_transmittance(optical_depth, data) *
                   henyey_greenstein(cosine, data.sun_direction_anisotropy.w);
    float multiple = 0.0;
    float energy = 0.5;
    float extinction_scale = 0.5;
    for (uint octave = 0; octave < 2; ++octave) {
        multiple += energy * exp(-data.ambient_extinction.w * extinction_scale * optical_depth);
        energy *= 0.5;
        extinction_scale *= 0.5;
    }
    return data.sun_radiance_albedo.xyz * (single + multiple / 3.14159265359);
}

// Entry and exit distances of the ray through the layer's two height planes.
// Returns false when the ray never enters the layer, including the near-horizontal
// case where the divisor degenerates: a ray parallel to the layer either lies inside
// it, which this milestone does not observe from, or never meets it.
bool intersect_layer(float3 origin, float3 direction, float bottom, float top, float max_distance,
                     out float enter, out float leave)
{
    enter = 0.0;
    leave = 0.0;
    if (abs(direction.y) < 1e-6)
        return false;

    float to_bottom = (bottom - origin.y) / direction.y;
    float to_top = (top - origin.y) / direction.y;
    float near_plane = min(to_bottom, to_top);
    float far_plane = max(to_bottom, to_top);
    enter = max(near_plane, 0.0);
    leave = min(far_plane, max_distance);
    return leave > enter;
}

[shader("vertex")]
float4 vertMain(uint vertex_id : SV_VertexID) : SV_Position
{
    float2 position = float2((vertex_id << 1) & 2, vertex_id & 2);
    return float4(position * 2.0 - 1.0, 0.0, 1.0);
}

[shader("fragment")]
float4 fragMain(float4 position : SV_Position) : SV_Target
{
    CloudData data = load_cloud_data(ResourceDescriptorHeap[push.cloud]);
    Texture3D<float> shape_noise = ResourceDescriptorHeap[push.shape_noise];
    SamplerState shape_filter = SamplerDescriptorHeap[push.noise_sampler];
    Texture2DMS<float> scene_depth = ResourceDescriptorHeap[push.depth];

    uint width, height, sample_count;
    scene_depth.GetDimensions(width, height, sample_count);
    float2 uv = position.xy / float2(width, height);
    // Framebuffer rows run downwards while the projection's clip-space y points up,
    // which the main pass expresses with its negative-height viewport.
    float2 ndc = float2(uv.x * 2.0 - 1.0, 1.0 - uv.y * 2.0);

    // Nearest of the covered samples bounds the march most tightly.
    int2 pixel = int2(position.xy);
    float depth = 1.0;
    for (uint i = 0; i < max(sample_count, 1u); ++i)
        depth = min(depth, scene_depth.Load(pixel, int(i)));

    float3 camera_position = data.camera_position_time.xyz;
    float3 ray_direction =
        normalize(clip_to_world(ndc, 1.0, data) - camera_position);
    // An untouched depth buffer holds the cleared far value: there is no geometry, so
    // the layer is bounded by the trace distance instead of by a far plane.
    float scene_distance =
        depth >= 1.0 ? data.march.x : length(clip_to_world(ndc, depth, data) - camera_position);

    float enter, leave;
    if (!intersect_layer(camera_position, ray_direction, data.layer.x, data.layer.y, data.march.x,
                         enter, leave))
        return float4(0.0, 0.0, 0.0, 1.0);
    leave = min(leave, scene_distance);
    if (leave <= enter)
        return float4(0.0, 0.0, 0.0, 1.0);

    uint debug = data.steps.z;
    uint view_steps = max(data.steps.x, 1u);
    float step_length = (leave - enter) / float(view_steps);
    float cosine = dot(ray_direction, data.sun_direction_anisotropy.xyz);

    float3 radiance = float3(0.0);
    float transmittance = 1.0;
    uint marched = 0;
    float peak_density = 0.0;
    float peak_sun = 0.0;
    for (uint i = 0; i < view_steps; ++i) {
        marched = i + 1;
        float distance = enter + (float(i) + 0.5) * step_length;
        float3 sample = camera_position + ray_direction * distance;
        // Fade towards the far cut instead of clipping the layer at a plane. The fade
        // starts at half the trace distance so the horizon dissolves into haze rather
        // than ending in a band where the layer is out of range.
        float distance_fade = 1.0 - smoothstep(0.5, 1.0, distance / data.march.x);
        float density = sample_cloud_density(sample, data, shape_noise, shape_filter) * distance_fade;
        if (density <= 0.0)
            continue;

        float optical_depth = sun_optical_depth(sample, data, shape_noise, shape_filter);
        // Debug views report the densest sample of the ray, which is what reads as "is
        // there cloud here"; the first hit is only the layer's thin base.
        if (density > peak_density) {
            peak_density = density;
            peak_sun = sun_transmittance(optical_depth, data);
        }
        float3 source =
            data.sun_radiance_albedo.w *
            (scattered_sunlight(optical_depth, cosine, data) + data.ambient_extinction.xyz);
        // Segment-constant approximation: density and source hold across the step.
        float segment = exp(-data.ambient_extinction.w * density * step_length);
        radiance += transmittance * (1.0 - segment) * source;
        transmittance *= segment;
        if (transmittance < 0.005)
            break;
    }

    if (debug == 1)
        return float4(peak_density, peak_density, peak_density, 0.0);
    if (debug == 2)
        return float4(peak_sun, peak_sun, peak_sun, 0.0);
    if (debug == 3)
        return float4(1.0 - transmittance, 1.0 - transmittance, 1.0 - transmittance, 0.0);
    if (debug == 4) {
        float fraction = float(marched) / float(view_steps);
        return float4(fraction, fraction, fraction, 0.0);
    }
    return float4(radiance, transmittance);
}
