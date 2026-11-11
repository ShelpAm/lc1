import ddgi_common;
import shadow_values;
[[vk::push_constant]] uint ddgi_index;

[shader("vertex")]
float4 ddgiVert(uint id : SV_VertexID) : SV_Position
{
    float2 uv = float2((id << 1) & 2, id & 2);
    return float4(uv * 2.0 - 1.0, 0, 1);
}

struct DdgiOutput {
    float4 irradiance : SV_Target0;
    float4 distance : SV_Target1;
};

[shader("fragment")]
DdgiOutput ddgiBlend(float4 position : SV_Position)
{
    ConstantBuffer<DdgiData> data = ResourceDescriptorHeap[ddgi_index];
    Texture2D<float4> previous_irradiance = ResourceDescriptorHeap[data.inputs.x];
    Texture2D<float4> previous_distance = ResourceDescriptorHeap[data.inputs.y];
    Texture2D<float4> rays = ResourceDescriptorHeap[data.inputs.z];
    uint2 pixel = uint2(position.xy);
    uint probe = (pixel.x / 8u) + (pixel.y / 8u) * 64u;
    uint cascade = probe / 2048u;
    uint local_probe = probe % 2048u;
    uint3 coord = ddgi_probe_coord(probe);
    int3 previous_coord = int3(coord) + int3(round((data.origin_spacing[cascade].xyz - data.previous_origin[cascade].xyz) /
                                                   data.origin_spacing[cascade].w));
    bool has_history = data.update[cascade].w != 0u && all(previous_coord >= 0) && all(previous_coord < int3(16, 8, 16));
    DdgiOutput output;
    output.irradiance = float4(0, 0, 0, -1);
    output.distance = float4(0);
    if (has_history) {
        int2 previous_pixel = int2(ddgi_tile(cascade * 2048u + ddgi_probe_index(uint3(previous_coord)))) + int2(pixel % 8u);
        output.irradiance = previous_irradiance.Load(int3(previous_pixel, 0));
        output.distance = previous_distance.Load(int3(previous_pixel, 0));
    }
    if (local_probe < data.update[cascade].x || local_probe >= data.update[cascade].x + data.update[cascade].y)
        return output;
    // Mirror the octahedral seam into each tile's one-texel border.
    int2 interior = int2(pixel % 8u) - 1;
    if (interior.x < 0 || interior.x >= 6) {
        interior.x = clamp(interior.x, 0, 5);
        interior.y = 5 - interior.y;
    }
    if (interior.y < 0 || interior.y >= 6) {
        interior.y = clamp(interior.y, 0, 5);
        interior.x = 5 - interior.x;
    }
    float3 direction = ddgi_oct_decode((float2(interior) + 0.5) / 6.0 * 2.0 - 1.0);
    float3 irradiance = float3(0);
    float2 moments = float2(0);
    float distance_weight = 0.0;
    uint backfaces = 0;
    for (uint i = 0; i < 64u; ++i) {
        float4 value = rays.Load(int3(i + cascade * data.emission_info.y, local_probe - data.update[cascade].x, 0));
        backfaces += value.w < 0.0 ? 1u : 0u;
        float cosine = max(dot(direction, ddgi_ray_direction(ddgi_index, i)), 0.0);
        irradiance += value.rgb * cosine;
        float weight = pow(cosine, 8.0);
        float distance = abs(value.w);
        moments += float2(distance, distance * distance) * weight;
        distance_weight += weight;
    }
    irradiance *= 4.0 * 3.14159265359 / 64.0;
    for (uint i = 0; i < data.emission_info.x; ++i) {
        float4 value = rays.Load(int3(64u + i + cascade * data.emission_info.y,
                                     local_probe - data.update[cascade].x, 0));
        // All directional texels of a probe read the same sample. Reject its
        // zero energy before unpacking/normalizing the direction.
        if (any(value.rgb > 0.0))
            irradiance += value.rgb * max(dot(direction, unpack_normal(value.w)), 0.0);
    }
    moments /= max(distance_weight, 1e-6);
    float valid = backfaces < 16u ? 1.0 : 0.0;
    // Near probes refresh every 32 frames, farther cascades every 64/128/128. Keep a modest history so sun
    // motion and extinguished emitters converge rather than persisting for seconds.
    float history_weight = has_history && output.irradiance.a > 0.0 && valid > 0.0 ? 0.5 : 0.0;
    output.irradiance = float4(lerp(irradiance, output.irradiance.rgb, history_weight), valid);
    output.distance = float4(lerp(moments, output.distance.xy, history_weight), 0, 0);
    return output;
}
