// Layer-based volumetric clouds: one view-ray march through a horizontal density
// layer, with a short sun march at every sample that carries density.
//
// Output is linear radiance in rgb and transmittance in a, so the composite pass
// reconstructs `radiance + transmittance * background` exactly. Nothing here is
// blended or gamma encoded: both intermediate images stay in HDR until tone mapping.

#include "cloud-data.slang"

struct CloudPushData {
    uint cloud;
    uint depth;
    uint shape_noise;
    uint noise_sampler;
    uint detail_noise;
};
[[vk::push_constant]] ConstantBuffer<CloudPushData> push;

float height_profile(float relative_height)
{
    // This envelope shapes the field BEFORE thresholding. Multiplying the final
    // density by it merely fades a slab with the same flat base under every cloud.
    return smoothstep(0.0, 0.3, relative_height) * (1.0 - smoothstep(0.55, 1.0, relative_height));
}

// The single density definition shared by the view march and the sun march.
// Sampling the same field at the same world position from both is what keeps cloud
// bodies and their self shadowing consistent.
float sample_cloud_density(float3 world_position, CloudData data, Texture3D<float> shape_noise,
                           SamplerState shape_filter, Texture3D<float2> detail_noise, float sample_length)
{
    float3 sample_position =
        world_position - data.wind_frequency.xyz * data.camera_position_time.w;
    float relative_height =
        (world_position.y - data.layer.x) / (data.layer.y - data.layer.x);
    if (relative_height <= 0.0 || relative_height >= 1.0)
        return 0.0;

    float3 mass_position = sample_position * data.wind_frequency.w * float3(1.0, 2.0, 1.0);
    float mass = shape_noise.SampleLevel(shape_filter, mass_position, 0.0).r;
    // Independently seeded Worley octaves supply rounded billows and smaller torn
    // edges. The mass field only decides where a cloud forms, never its fine texture.
    float3 detail_position = mass_position * data.detail.x + float3(0.31, 0.17, 0.73);
    // Low-frequency deformation prevents the independent cells reading as a
    // regular field of equal-sized puffs.
    detail_position += (mass - 0.5) * float3(1.3, -0.9, 1.1);
    float2 detail_fields = detail_noise.SampleLevel(shape_filter, detail_position, 0.0);
    float footprint = sample_length * data.wind_frequency.w * data.detail.x * 2.0;
    // Attenuate detail that this integration stratum cannot resolve. Without it,
    // distant clouds break into speckles as the march skips individual billows.
    float billow_weight = 1.0 - smoothstep(0.25, 1.0, footprint * 2.0);
    float detail_weight = 1.0 - smoothstep(0.25, 1.0, footprint * 8.0);
    float billow = lerp(0.5, smoothstep(0.2, 0.8, detail_fields.r), billow_weight);
    float detail = lerp(0.5, detail_fields.g, detail_weight);
    mass = saturate(mass + (billow - 0.5) * 0.2);

    // Coverage remaps the mass field: with a low threshold more of it becomes cloud, so
    // coverage grows and the fraction below the threshold shrinks. A mild curve softens
    // the threshold instead of stamping out hard-edged blobs.
    float coverage = max(data.layer.z, 1e-3);
    float shaped_mass = mass * height_profile(relative_height);
    float remapped = saturate((shaped_mass - (1.0 - coverage)) / coverage);
    // Subtract and remap rather than attenuating density: thin edges can now
    // disappear altogether while dense interiors retain their body.
    float erosion = (1.0 - detail) * data.detail.y * 0.25;
    remapped = saturate((remapped - erosion) / (1.0 - erosion));
    // Reach a dense core before the noise reaches its rare maximum; otherwise
    // almost every surviving mass stays a translucent, blurred wisp.
    float bodies = smoothstep(0.0, 0.45, remapped);
    return bodies * data.layer.w;
}

float henyey_greenstein(float cosine, float anisotropy)
{
    float squared = anisotropy * anisotropy;
    float denominator = max(1.0 + squared - 2.0 * anisotropy * cosine, 1e-4);
    return (1.0 - squared) / (4.0 * 3.14159265359 * pow(denominator, 1.5));
}

// Optical depth towards the sun through the layer, capped by the trace budget. This
// is the density integral; extinction turns it into a transmittance.
float sun_optical_depth(float3 position, CloudData data, Texture3D<float> shape_noise,
                        SamplerState shape_filter, Texture3D<float2> detail_noise)
{
    float3 to_sun = data.sun_direction_anisotropy.xyz;
    // A source at or below the horizon does not light the layer from above.
    if (to_sun.y <= 0.0)
        return 1e4;

    float to_top = (data.layer.y - position.y) / to_sun.y;
    float distance = clamp(min(data.march.y, to_top), 0.0, data.march.y);
    uint samples = max(data.steps.y, 1u);
    float optical_depth = 0.0;
    for (uint i = 0; i < samples; ++i) {
        // Quadratic strata resolve the near surface without ignoring the full
        // column above it. Integrate each stratum with its actual length.
        float a = float(i) / float(samples);
        float b = float(i + 1) / float(samples);
        float start = distance * a * a;
        float end = distance * b * b;
        float step_length = end - start;
        float3 sample = position + to_sun * (0.5 * (start + end));
        optical_depth +=
            sample_cloud_density(sample, data, shape_noise, shape_filter, detail_noise, step_length) * step_length;
    }
    return optical_depth;
}

float sun_transmittance(float optical_depth, CloudData data)
{
    return exp(-data.ambient_extinction.w * optical_depth);
}

// Dual-lobe single scattering and two broad multiple-scattering octaves. Each
// later octave has reduced extinction and energy, modelling light redistributed
// through the cloud. This remains an approximation, not an energy-conserving solver.
float3 scattered_sunlight(float optical_depth, float cosine, CloudData data)
{
    float phase = lerp(henyey_greenstein(cosine, -0.2),
                       henyey_greenstein(cosine, data.sun_direction_anisotropy.w), 0.9);
    float single = sun_transmittance(optical_depth, data) * phase;
    float multiple = 0.0;
    float energy = 0.5;
    float extinction_scale = 0.2;
    for (uint octave = 0; octave < 2; ++octave) {
        multiple += energy * exp(-data.ambient_extinction.w * extinction_scale * optical_depth);
        energy *= 0.5 * data.sun_radiance_albedo.w;
        extinction_scale *= 0.2;
    }
    return data.sun_radiance_albedo.xyz * data.march.z * (single + multiple / 3.14159265359);
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
    Texture3D<float2> detail_noise = ResourceDescriptorHeap[push.detail_noise];
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
    // A fixed screen-space offset decorrelates the strata between neighbouring
    // rays. Identical midpoints expose the 48 integration slices as contour bands
    // on the newly shaped bottoms. Keep it frame invariant to avoid flicker.
    float sample_offset = frac(52.9829189 * frac(dot(position.xy, float2(0.06711056, 0.00583715))));

    float3 radiance = float3(0.0);
    float transmittance = 1.0;
    uint marched = 0;
    float peak_density = 0.0;
    float peak_sun = 0.0;
    for (uint i = 0; i < view_steps; ++i) {
        marched = i + 1;
        float distance = enter + (float(i) + sample_offset) * step_length;
        float3 sample = camera_position + ray_direction * distance;
        // Fade towards the far cut instead of clipping the layer at a plane. The fade
        // starts at half the trace distance so the horizon dissolves into haze rather
        // than ending in a band where the layer is out of range.
        float distance_fade = 1.0 - smoothstep(0.5, 1.0, distance / data.march.x);
        float density = sample_cloud_density(sample, data, shape_noise, shape_filter, detail_noise, step_length) * distance_fade;
        if (density <= 0.0)
            continue;

        float optical_depth = sun_optical_depth(sample, data, shape_noise, shape_filter, detail_noise);
        // Debug views report the densest sample of the ray, which is what reads as "is
        // there cloud here"; the first hit is only the layer's thin base.
        if (density > peak_density) {
            peak_density = density;
            peak_sun = sun_transmittance(optical_depth, data);
        }
        // Skylight reaches the upper cloud more easily than its underside; a
        // constant ambient term flattens both into the same grey even in shadow.
        float relative_height = saturate((sample.y - data.layer.x) / (data.layer.y - data.layer.x));
        float3 ambient = data.ambient_extinction.xyz * lerp(0.45, 1.0, relative_height);
        float3 source =
            data.sun_radiance_albedo.w *
            (scattered_sunlight(optical_depth, cosine, data) + ambient);
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
