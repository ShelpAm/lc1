// Shared cloud/sky uniform layout and explicit column-wise reconstruction.
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
    float4 wind_frequency;           // xyz wind velocity in m/s; w mass frequency, 1/m
    float4 detail;                   // x detail frequency multiplier, y erosion strength
    float4 march;                    // x max trace distance, y light march distance,
                                     // z radiance scale
    uint4 steps;                     // x view steps, y light steps, z debug mode, w reserved
    float4 sky_color; // unmodified scene background, independent of cloud ambient scale
};

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
    data.detail = source.detail;
    data.march = source.march;
    data.steps = source.steps;
    data.sky_color = source.sky_color;
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

