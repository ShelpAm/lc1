import ddgi_common;
import ddgi_emission;
import shadow_values;

// Evaluate the low-frequency irradiance once at half resolution. Reject taps
// across depth/normal discontinuities; thin edges fall back to a probe query.
float3 ddgi_screen_irradiance(uint data_index, float2 pixel, float3 position, float3 normal)
{
    ConstantBuffer<DdgiData> data = ResourceDescriptorHeap[data_index];
    Texture2D<float4> values = ResourceDescriptorHeap[data.outputs.w];
    Texture2D<float4> surfaces = ResourceDescriptorHeap[data.resolve.x];
    int2 full_size = int2(data.resolve.yz);
    int2 half_size = (full_size + 1) / 2;
    float2 grid = float2(int2(pixel)) * 0.5;
    int2 base = int2(floor(grid));
    float2 alpha = frac(grid);
    float threshold = max(0.1, length(ddx(position)) + length(ddy(position)));
    float3 sum = float3(0);
    float weight_sum = 0.0;
    for (uint i = 0; i < 4u; ++i) {
        int2 offset = int2(i & 1u, i >> 1u);
        int2 tap = clamp(base + offset, int2(0), half_size - 1);
        float4 irradiance = values.Load(int3(tap, 0));
        float4 surface = surfaces.Load(int3(min(tap * 2, full_size - 1), 0));
        float3 n = unpack_normal(surface.w);
        if (irradiance.a == 0.0 || dot(n, normal) < 0.9 ||
            abs(dot(surface.xyz - position, normal)) > threshold ||
            abs(dot(surface.xyz - position, n)) > threshold)
            continue;
        float2 tri = lerp(1.0 - alpha, alpha, float2(offset));
        float weight = tri.x * tri.y;
        sum += irradiance.rgb * weight;
        weight_sum += weight;
    }
    return weight_sum > 1e-5 ? sum / weight_sum : (ddgi_irradiance(data_index, position, normal, normal) +
                                                       ddgi_cached_near_emission(data_index, position, normal));
}
