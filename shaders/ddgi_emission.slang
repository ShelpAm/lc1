import ddgi_common;
import shadow_values;
import lights;
import ray_shadows;
#include "generated/modules/material.slang"

struct DdgiObject {
    float4x4 model;
    float4x4 normal_transform;
    uint4 shadow_identity;
};
struct DdgiLights {
    uint count;
    Light lights[10];
};
uint ddgi_index_at(DdgiGeometry geometry, uint index)
{
    uint word = geometry.indices[(index * geometry.index_stride) / 4u];
    return geometry.index_stride == 2u ? (word >> ((index & 1u) * 16u)) & 65535u : word;
}

float3 ddgi_vertex3(DdgiGeometry geometry, uint vertex, uint offset)
{
    uint i = vertex * 15u + offset;
    return float3(geometry.vertices[i], geometry.vertices[i + 1u], geometry.vertices[i + 2u]);
}

// Integrate emitters explicitly: uniform sphere rays frequently miss small LEDs.
// Four fixed subtriangles avoid temporal sampling noise and bound near-field energy
// by solid angle rather than a centroid's area/distance-squared singularity.
float4 ddgi_emission(uint data_index, uint sample_index, float3 origin, bool near_field, float3 receiver_normal)
{
    ConstantBuffer<DdgiData> data = ResourceDescriptorHeap[data_index];
    Texture2D<float4> triangles = ResourceDescriptorHeap[data.emitter_images.x];
    float3 radiance = triangles.Load(int3(3, sample_index, 0)).rgb;
    if (!any(radiance > 0.0))
        return float4(0);
    float3 p0 = triangles.Load(int3(0, sample_index, 0)).xyz;
    float3 p1 = triangles.Load(int3(1, sample_index, 0)).xyz;
    float3 p2 = triangles.Load(int3(2, sample_index, 0)).xyz;
    float3 face = cross(p1 - p0, p2 - p0);
    if (dot(face, origin - p0) <= 0.0)
        return float4(0);
    float3 target = (p0 + p1 + p2) / 3.0;
    float3 to_emitter = target - origin;
    float distance = length(to_emitter);
    // A surface query owns the near field; the probe field owns the far field.
    // Smoothly split the same integral to avoid both a gap and double energy.
    float near_weight = 1.0 - smoothstep(4.0, 8.0, distance);
    float partition = near_field ? near_weight : 1.0 - near_weight;
    if (partition == 0.0)
        return float4(0);
    float3 direction = to_emitter / max(distance, 1e-6);
    if (near_field && dot(receiver_normal, direction) <= 0.0)
        return float4(0);
    RaytracingAccelerationStructure scene = RaytracingAccelerationStructure(data.scene);
    if (trace_shadow_visibility(scene, origin, direction, max(distance - 0.004, 0.0)) == 0.0)
        return float4(0);
    float3 u = normalize(p0 - origin);
    float3 v = normalize(p1 - origin);
    float3 w = normalize(p2 - origin);
    float solid_angle = 2.0 * atan2(abs(dot(u, cross(v, w))),
                                  1.0 + dot(u, v) + dot(v, w) + dot(w, u));
    return float4(radiance * (solid_angle * partition), pack_normal(direction));
}

float3 ddgi_near_emission(uint data_index, float3 position, float3 normal)
{
    ConstantBuffer<DdgiData> data = ResourceDescriptorHeap[data_index];
    float3 irradiance = float3(0);
    float bias = max(0.002, max(max(abs(position.x), abs(position.y)), abs(position.z)) * 2e-6);
    float3 origin = position + normal * bias;
    Texture2D<float4> triangles = ResourceDescriptorHeap[data.emitter_images.x];
    for (uint triangle = 0; triangle < data.emission_info.x; triangle += 4u) {
        float4 emission = triangles.Load(int3(3, triangle, 0));
        float3 center = float3(triangles.Load(int3(0, triangle, 0)).w,
                               triangles.Load(int3(1, triangle, 0)).w,
                               triangles.Load(int3(2, triangle, 0)).w);
        float radius = emission.w;
        float3 to_center = center - origin;
        // Conservative bounds: no near-field sample of this triangle can
        // contribute beyond 8m or entirely behind the receiving hemisphere.
        if (dot(to_center, to_center) >= (8.0 + radius) * (8.0 + radius) ||
            dot(normal, to_center) <= -radius)
            continue;
        for (uint sample = 0; sample < 4u; ++sample) {
            float4 value = ddgi_emission(data_index, triangle + sample, origin, true, normal);
            irradiance += value.rgb * max(dot(normal, unpack_normal(value.w)), 0.0);
        }
    }
    return irradiance;
}

// Share spatial candidate lists across nearby receivers and consecutive frames.
// Outside the cache volume the complete query preserves near-field lighting.
float3 ddgi_cached_near_emission(uint data_index, float3 position, float3 normal)
{
    ConstantBuffer<DdgiData> data = ResourceDescriptorHeap[data_index];
    if (data.emission_info.x == 0u)
        return float3(0);
    Texture2D<uint4> candidates = ResourceDescriptorHeap[data.emitter_images.y];
    uint index = 8192u;
    for (uint cascade = 0; cascade < 4u; ++cascade) {
        int3 cell = int3(floor((position - data.origin_spacing[cascade].xyz) / data.origin_spacing[cascade].w));
        if (all(cell >= 0) && all(cell < int3(16, 8, 16))) {
            index = cascade * 2048u + ddgi_probe_index(uint3(cell));
            break;
        }
    }
    if (index == 8192u)
        return ddgi_near_emission(data_index, position, normal);
    uint2 tile = uint2(index % 64u, index / 64u);
    float bias = max(0.002, max(max(abs(position.x), abs(position.y)), abs(position.z)) * 2e-6);
    float3 origin = position + normal * bias;
    float3 irradiance = float3(0);
    for (uint group = 0; group < data.emitter_images.z; ++group) {
        uint4 mask = candidates.Load(int3(tile.x * data.emitter_images.z + group, tile.y, 0));
        for (uint word = 0; word < 4u; ++word) {
            uint bits = mask[word];
            while (bits != 0u) {
                uint bit = firstbitlow(bits);
                uint sample = group * 128u + word * 32u + bit;
                float4 value = ddgi_emission(data_index, sample, origin, true, normal);
                irradiance += value.rgb * max(dot(normal, unpack_normal(value.w)), 0.0);
                bits &= bits - 1u;
            }
        }
    }
    return irradiance;
}
