#include "cloud-data.slang"

// Composite of the cloud radiance/transmittance image onto the HDR scene.
//
//   result = cloud_radiance + cloud_transmittance * background
//
// Both inputs are read at the same pixel: the march ran at full resolution, so this
// pass needs no sampler and no filtering, which keeps geometry edges exact until the
// low-resolution path exists.

struct CompositePushData {
    uint cloud; // rgb accumulated radiance, a accumulated transmittance
    uint hdr;   // linear HDR scene colour, alpha 1 for opaque geometry
    uint data;
    uint depth;
};
[[vk::push_constant]] ConstantBuffer<CompositePushData> push;

[shader("vertex")]
float4 vertMain(uint vertex_id : SV_VertexID) : SV_Position
{
    float2 position = float2((vertex_id << 1) & 2, vertex_id & 2);
    return float4(position * 2.0 - 1.0, 0.0, 1.0);
}

[shader("fragment")]
float4 fragMain(float4 position : SV_Position) : SV_Target
{
    Texture2D<float4> cloud = ResourceDescriptorHeap[push.cloud];
    Texture2D<float4> background = ResourceDescriptorHeap[push.hdr];
    int3 pixel = int3(int2(position.xy), 0);
    float4 cloud_value = cloud.Load(pixel);
    float4 scene_value = background.Load(pixel);
    CloudData data = load_cloud_data(ResourceDescriptorHeap[push.data]);
    Texture2DMS<float> scene_depth = ResourceDescriptorHeap[push.depth];
    uint width, height, sample_count;
    scene_depth.GetDimensions(width, height, sample_count);
    float2 uv = position.xy / float2(width, height);
    float2 ndc = float2(uv.x * 2.0 - 1.0, 1.0 - uv.y * 2.0);
    float3 ray_direction = normalize(clip_to_world(ndc, 1.0, data) - data.camera_position_time.xyz);
    float depth = 1.0;
    for (uint i = 0; i < max(sample_count, 1u); ++i)
        depth = min(depth, scene_depth.Load(int2(position.xy), int(i)));
    // Solar disc plus a compact forward-scattering aureole. These are sky
    // radiances, not a screen overlay: geometry masks them and the cloud result
    // attenuates them together below. Keep the true angular disc size; its bright
    // surroundings, rather than an oversized white circle, convey the glare.
    float3 to_sun = data.sun_direction_anisotropy.xyz;
    float sun_cosine = dot(ray_direction, to_sun);
    float sun_angle = atan2(length(cross(ray_direction, to_sun)), sun_cosine);
    float disc_radius = 0.004625123; // 0.265 degrees.
    float edge_width = max(fwidth(sun_angle), 1e-6);
    float disc = 1.0 - smoothstep(disc_radius - edge_width, disc_radius + edge_width, sun_angle);
    float3 solar_radiance = float3(0.0);
    if (depth >= 1.0 && to_sun.y > 0.0 && data.steps.z == 0) {
        // An artistic optical-airmass approximation: low sunlight loses blue first.
        // Subtract the vertical path so overhead sunlight retains the light's colour.
        float extra_path = 1.0 / max(to_sun.y, 0.08) - 1.0;
        float3 transmission = exp(-float3(0.10, 0.22, 0.48) * extra_path);
        float3 sunlight = data.sun_radiance_albedo.xyz * transmission;
        float inner_glare = exp(-pow(sun_angle / 0.018, 2.0));
        float outer_glare = exp(-sun_angle / 0.060);
        float halo_fade = 1.0 - smoothstep(0.14, 0.24, sun_angle);
        float3 aureole = (6.0 * inner_glare + outer_glare) * halo_fade *
                         lerp(float3(1.0, 0.85, 0.65), float3(1.0, 0.55, 0.25),
                              saturate(extra_path / 8.0));
        solar_radiance = sunlight * (800.0 * disc + aureole);
    }

    if (depth >= 1.0 && data.steps.z == 0 && any(data.sun_radiance_albedo.xyz > 0.0)) {
        // Zenith blue and horizon haze share the scene's day/night sky colour.
        // This is an analytic art approximation, not a full atmosphere simulation.
        float horizon = exp(-max(ray_direction.y, 0.0) * 4.0);
        float3 sky = data.sky_color.rgb * lerp(float3(0.45, 0.65, 1.15),
                                              float3(1.9, 1.5, 1.15), horizon);
        float sunset = 1.0 - smoothstep(0.05, 0.40, to_sun.y);
        float facing_sun = exp(-sun_angle / 0.55);
        sky += data.sun_radiance_albedo.xyz * float3(2.5, 0.9, 0.25) *
               sunset * facing_sun * horizon;
        scene_value.rgb = sky + solar_radiance;
    }
    return float4(cloud_value.rgb + cloud_value.a * scene_value.rgb, scene_value.a);
}
