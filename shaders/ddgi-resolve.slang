import ddgi_common;
import ddgi_emission;
import shadow_values;
[[vk::push_constant]] uint ddgi_index;

[shader("vertex")]
float4 ddgiVert(uint id : SV_VertexID) : SV_Position
{
    float2 uv = float2((id << 1) & 2, id & 2);
    return float4(uv * 2.0 - 1.0, 0, 1);
}

float4 resolve_irradiance(float4 position, bool candidate_cache)
{
    ConstantBuffer<DdgiData> data = ResourceDescriptorHeap[ddgi_index];
    Texture2D<float4> surfaces = ResourceDescriptorHeap[data.resolve.x];
    Texture2D<uint4> visibility = ResourceDescriptorHeap[data.resolve.w];
    int2 pixel = min(int2(position.xy) * 2, int2(data.resolve.yz) - 1);
    if ((visibility.Load(int3(pixel, 0)).z >> 16u) == 0u)
        return float4(0);
    float4 surface = surfaces.Load(int3(pixel, 0));
    float3 normal = unpack_normal(surface.w);
    float3 emission = candidate_cache
        ? ddgi_cached_near_emission(ddgi_index, surface.xyz, normal)
        : ddgi_near_emission(ddgi_index, surface.xyz, normal);
    return float4(ddgi_irradiance(ddgi_index, surface.xyz, normal, normal) + emission, 1);
}

[shader("fragment")]
float4 ddgiResolve(float4 position : SV_Position) : SV_Target
{
    return resolve_irradiance(position, true);
}

// Independent full traversal for GPU differential tests and isolated timings.
[shader("fragment")]
float4 ddgiResolveReference(float4 position : SV_Position) : SV_Target
{
    return resolve_irradiance(position, false);
}
