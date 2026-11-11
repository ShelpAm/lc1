import shadow_values;

// Four nested 16x8x16 volumes, at 4/16/64/256m spacing. Each octahedral tile has a 6x6 interior
// and one mirrored border texel, allowing ordinary bilinear texture sampling.
struct DdgiGeometry {
    float *vertices; // Packed lc1::Vertex: 15 scalar floats, no std430 vec3 padding.
    uint *indices; // uint16 pairs or uint32; 16-bit buffers are padded to 4 bytes.
    uint object_index;
    uint material_index;
    uint index_stride;
    uint padding;
};

struct DdgiData {
    float4 origin_spacing[4];
    float4 previous_origin[4];
    float4 sky_radiance;
    uint4 update[4];
    uint4 inputs;
    uint4 outputs;
    DdgiGeometry *geometry;
    uint64_t scene;
    uint4 resolve;
    float4 directions[64];
    uint2 *emitters;
    uint2 emission_info;
    uint4 emitter_images;
};

float2 ddgi_sign(float2 v)
{
    return float2(v.x >= 0.0 ? 1.0 : -1.0, v.y >= 0.0 ? 1.0 : -1.0);
}

float2 ddgi_oct_encode(float3 direction)
{
    direction /= max(abs(direction.x) + abs(direction.y) + abs(direction.z), 1e-8);
    float2 p = direction.xy;
    if (direction.z < 0.0)
        p = (1.0 - abs(p.yx)) * ddgi_sign(p);
    return p * 0.5 + 0.5;
}

float3 ddgi_oct_decode(float2 p)
{
    float3 n = float3(p, 1.0 - abs(p.x) - abs(p.y));
    if (n.z < 0.0)
        n.xy = (1.0 - abs(n.yx)) * ddgi_sign(n.xy);
    return normalize(n);
}

uint3 ddgi_probe_coord(uint index)
{
    index %= 2048u;
    return uint3(index % 16u, (index / 16u) % 8u, index / 128u);
}

uint ddgi_probe_index(uint3 coord)
{
    return coord.x + coord.y * 16u + coord.z * 128u;
}

uint2 ddgi_tile(uint index)
{
    return uint2(index % 64u, index / 64u) * 8u;
}

float2 ddgi_uv(uint index, float3 direction)
{
    return (float2(ddgi_tile(index)) + 1.0 + ddgi_oct_encode(direction) * 6.0) /
           float2(512, 1024);
}

float3 ddgi_ray_direction(uint data_index, uint index)
{
    ConstantBuffer<DdgiData> data = ResourceDescriptorHeap[data_index];
    return data.directions[index].xyz;
}

float4 ddgi_level_irradiance(uint data_index, uint cascade, float3 position, float3 geometric_normal, float3 normal)
{
    ConstantBuffer<DdgiData> data = ResourceDescriptorHeap[data_index];
    Texture2D<float4> irradiance = ResourceDescriptorHeap[data.outputs.x];
    Texture2D<float4> distance_moments = ResourceDescriptorHeap[data.outputs.y];
    SamplerState sampler = SamplerDescriptorHeap[data.inputs.w];
    float spacing = data.origin_spacing[cascade].w;
    // A small normal bias prevents a receiver from testing visibility against itself.
    float3 biased_position = position + geometric_normal * 0.2;
    float3 grid = (biased_position - data.origin_spacing[cascade].xyz) / spacing;
    if (any(grid < 0.0) || any(grid > float3(15, 7, 15)))
        return float4(0);
    int3 base = min(int3(floor(grid)), int3(14, 6, 14));
    float3 alpha = grid - float3(base);
    float3 sum = float3(0);
    float weight_sum = 0.0;
    float ready_weight = 0.0;
    for (uint corner = 0; corner < 8u; ++corner) {
        int3 offset = int3(corner & 1u, (corner >> 1u) & 1u, (corner >> 2u) & 1u);
        int3 coord = base + offset;
        uint probe = cascade * 2048u + ddgi_probe_index(uint3(coord));
        float3 probe_position = data.origin_spacing[cascade].xyz + float3(coord) * spacing;
        float3 to_probe = probe_position - biased_position;
        float distance = length(to_probe);
        float3 direction = to_probe / max(distance, 1e-6);
        float3 tri = lerp(1.0 - alpha, alpha, float3(offset));
        float weight = tri.x * tri.y * tri.z;
        float wrap = saturate(dot(normal, direction) * 0.5 + 0.5);
        weight *= wrap * wrap + 0.05;
        float2 moments = distance_moments.SampleLevel(sampler, ddgi_uv(probe, -direction), 0).xy;
        if (distance > moments.x) {
            float variance = max(moments.y - moments.x * moments.x, 0.0);
            float delta = distance - moments.x;
            float visibility = variance / max(variance + delta * delta, 1e-6);
            weight *= visibility * visibility * visibility;
        }
        // Suppress the small visibility tails that otherwise leak through walls.
        if (weight < 0.2)
            weight *= weight * weight / 0.04;
        float4 value = irradiance.SampleLevel(sampler, ddgi_uv(probe, normal), 0);
        // Alpha: -1 is not initialized, 0 is classified inside geometry, 1 is valid.
        // Missing near probes may use a coarser cascade; classified interior probes
        // remain dark rather than falling back to potentially unoccluded far probes.
        ready_weight += tri.x * tri.y * tri.z * saturate(value.a + 1.0);
        weight *= saturate(value.a);
        sum += value.rgb * weight;
        weight_sum += weight;
    }
    float edge = saturate(min(min(min(grid.x, grid.y), grid.z),
                              min(min(15.0 - grid.x, 7.0 - grid.y), 15.0 - grid.z)));
    float3 value = weight_sum > 1e-5 ? max(sum / weight_sum, float3(0)) : float3(0);
    return float4(value, smoothstep(0.0, 1.0, edge) * saturate(ready_weight));
}

float3 ddgi_irradiance(uint data_index, float3 position, float3 geometric_normal, float3 normal)
{
    float3 result = float3(0);
    float remaining = 1.0;
    for (uint cascade = 0; cascade < 4u; ++cascade) {
        float4 value = ddgi_level_irradiance(data_index, cascade, position, geometric_normal, normal);
        result += value.rgb * (value.a * remaining);
        remaining *= 1.0 - value.a;
        if (remaining < 1e-4)
            break;
    }
    return result;
}
