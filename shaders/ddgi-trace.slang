import ddgi_emission;
import ddgi_common;
import lights;
import ray_shadows;

[[vk::push_constant]] uint ddgi_index;

[shader("vertex")]
float4 ddgiVert(uint id : SV_VertexID) : SV_Position
{
    float2 uv = float2((id << 1) & 2, id & 2);
    return float4(uv * 2.0 - 1.0, 0, 1);
}

[shader("fragment")]
float4 ddgiTrace(float4 pixel : SV_Position) : SV_Target
{
    ConstantBuffer<DdgiData> data = ResourceDescriptorHeap[ddgi_index];
    uint cascade = uint(pixel.x) / data.emission_info.y;
    if (uint(pixel.y) >= data.update[cascade].y)
        return float4(0);
    uint ray_index = uint(pixel.x) % data.emission_info.y;
    uint probe = data.update[cascade].x + uint(pixel.y);
    float3 origin = data.origin_spacing[cascade].xyz + float3(ddgi_probe_coord(probe)) * data.origin_spacing[cascade].w;
    if (ray_index >= 64u)
        return ddgi_emission(ddgi_index, ray_index - 64u, origin, false, float3(0));
    float3 direction = ddgi_ray_direction(ddgi_index, ray_index);
    RaytracingAccelerationStructure scene = RaytracingAccelerationStructure(data.scene);
    RayDesc ray;
    ray.Origin = origin;
    ray.Direction = direction;
    ray.TMin = 0.002;
    ray.TMax = 16.0 * data.origin_spacing[cascade].w;
    RayQuery<RAY_FLAG_FORCE_OPAQUE> query;
    query.TraceRayInline(scene, RAY_FLAG_NONE, 0xff, ray);
    while (query.Proceed()) {}
    if (query.CommittedStatus() == COMMITTED_NOTHING)
        return float4(data.sky_radiance.rgb, ray.TMax);

    uint instance = query.CommittedInstanceID();
    DdgiGeometry geometry = data.geometry[instance];
    uint triangle = query.CommittedPrimitiveIndex() * 3u;
    uint a = ddgi_index_at(geometry, triangle);
    uint b = ddgi_index_at(geometry, triangle + 1u);
    uint c = ddgi_index_at(geometry, triangle + 2u);
    float2 bary = query.CommittedTriangleBarycentrics();
    float3 weights = float3(1.0 - bary.x - bary.y, bary.x, bary.y);
    ConstantBuffer<DdgiObject> object = ResourceDescriptorHeap[NonUniformResourceIndex(geometry.object_index)];
    float3 pa = ddgi_vertex3(geometry, a, 0);
    float3 pb = ddgi_vertex3(geometry, b, 0);
    float3 pc = ddgi_vertex3(geometry, c, 0);
    float3 face = cross(pb - pa, pc - pa);
    float3 geometric_normal = normalize(object.normal_transform[0].xyz * face.x +
                                       object.normal_transform[1].xyz * face.y +
                                       object.normal_transform[2].xyz * face.z);
    float distance = query.CommittedRayT();
    if (dot(geometric_normal, direction) > 0.0)
        return float4(0, 0, 0, -distance); // Backface: classify interior probes, do not emit.
    float3 n = ddgi_vertex3(geometry, a, 3) * weights.x +
               ddgi_vertex3(geometry, b, 3) * weights.y + ddgi_vertex3(geometry, c, 3) * weights.z;
    float3 normal = normalize(object.normal_transform[0].xyz * n.x +
                             object.normal_transform[1].xyz * n.y + object.normal_transform[2].xyz * n.z);
    float3 vertex_color = ddgi_vertex3(geometry, a, 6) * weights.x +
                          ddgi_vertex3(geometry, b, 6) * weights.y +
                          ddgi_vertex3(geometry, c, 6) * weights.z;
    float2 uv = ddgi_vertex3(geometry, a, 9).xy * weights.x +
                ddgi_vertex3(geometry, b, 9).xy * weights.y +
                ddgi_vertex3(geometry, c, 9).xy * weights.z;
    Material material = load_material(NonUniformResourceIndex(geometry.material_index));
    float3 base_color = saturate(material.base_color_factor.rgb * vertex_color *
        material.base_color_texture.SampleLevel(material.base_color_texture_sampler, uv, 0).rgb);
    float metallic = saturate(material.metallic_factor *
        material.metallic_roughness_texture.SampleLevel(material.metallic_roughness_texture_sampler, uv, 0).b);
    // Emission is integrated separately; adding it here would double count it.
    float3 radiance = float3(0);
    ConstantBuffer<DdgiLights> lights = ResourceDescriptorHeap[data.outputs.z];
    float3 position = origin + direction * distance;
    float bias = max(0.002, max(max(abs(position.x), abs(position.y)), abs(position.z)) * 2e-6);
    for (uint i = 0; i < min(lights.count, 10u); ++i) {
        Light light;
        light.position = lights.lights[i].position;
        light.type = lights.lights[i].type;
        light.direction = lights.lights[i].direction;
        light.radius = lights.lights[i].radius;
        light.color = lights.lights[i].color;
        light.intensity = lights.lights[i].intensity;
        light.spot_angles = lights.lights[i].spot_angles;
        light.shadow_sample_count = 1;
        light.padding = 0;
        LightSample sample = sample_light(light, position);
        float cosine = saturate(dot(normal, sample.direction));
        if (cosine <= 0.0 || dot(geometric_normal, sample.direction) <= 0.0 || !any(sample.radiance > 0.0))
            continue;
        float max_distance = light.type == 0 ? 1e7 : max(length(light.position - position) - bias, 0.0);
        float visibility = trace_shadow_visibility(scene, position + geometric_normal * bias,
                                                   sample.direction, max_distance);
        radiance += base_color * (1.0 - metallic) * sample.radiance * (cosine * visibility / 3.14159265359);
    }
    return float4(min(radiance, float3(65504.0)), distance);
}
