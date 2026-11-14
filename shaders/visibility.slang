import pass_data;
import lights;
import ray_shadows;
import shadow_values;

// SamplerDescriptorHeap
// ResourceDescriptorHeap

struct VSInput {
    float3 position;
    float3 normal;
    float3 color;
    float2 uv;
    float4 tangent;
};

struct Camera {
    float4x4 view;
    float4x4 proj;
    float4 position;
    float4 fog_radiance;
    float4 fog_distances;
};

struct LightsData {
    uint32_t count;
    Light lights[10];
};

struct ShadowSceneData {
    uint64_t address;
};


struct ObjectData {
    float4x4 model;
    float4x4 normal_transform;
    uint4 shadow_identity;
};



struct VSOutput {
    float4 pos : SV_Position;
    float3 world_position;
    float2 uv;
    float3 color;
    float3 world_normal;
    float4 world_tangent;
};

[shader("vertex")]
VSOutput vertMain(VSInput input)
{
    ConstantBuffer<Camera> camera = ResourceDescriptorHeap[pass_data.camera];
    ConstantBuffer<ObjectData> object_data = ResourceDescriptorHeap[pass_data.object];
    VSOutput output;
    // Read GLM's stored columns explicitly: whole-matrix loads through heap
    // (untyped Uniform) pointers are mishandled here; see docs/external-defects.md.
    float4 world_position = object_data.model[0] * input.position.x +
        object_data.model[1] * input.position.y + object_data.model[2] * input.position.z + object_data.model[3];
    float4 view_position = camera.view[0] * world_position.x + camera.view[1] * world_position.y + camera.view[2] * world_position.z + camera.view[3] * world_position.w;
    output.pos = camera.proj[0] * view_position.x + camera.proj[1] * view_position.y + camera.proj[2] * view_position.z + camera.proj[3] * view_position.w;
    output.world_position = world_position.xyz;
    output.uv = input.uv;
    output.color = input.color;
    output.world_normal = (object_data.normal_transform[0].xyz * input.normal.x + object_data.normal_transform[1].xyz * input.normal.y + object_data.normal_transform[2].xyz * input.normal.z);
    float handedness = dot(object_data.model[0].xyz, cross(object_data.model[1].xyz, object_data.model[2].xyz)) < 0.0 ? -1.0 : 1.0;
    output.world_tangent =
        float4((object_data.model[0].xyz * input.tangent.x + object_data.model[1].xyz * input.tangent.y + object_data.model[2].xyz * input.tangent.z), input.tangent.w * handedness);
    return output;
}

// Single-sampled raster pass: cache visibility at pixel frequency, 8 bits per light.
// The MSAA material pass reuses these values instead of tracing per sample.
[shader("fragment")]
[earlydepthstencil]
ShadowOutput visibilityMain(VSOutput input)
{
    ConstantBuffer<LightsData> lights = ResourceDescriptorHeap[pass_data.lights];
    ConstantBuffer<ObjectData> object_data = ResourceDescriptorHeap[pass_data.object];
    ConstantBuffer<TemporalShadowData> temporal = ResourceDescriptorHeap[pass_data.temporal];
    ConstantBuffer<ShadowSceneData> scene_data = ResourceDescriptorHeap[pass_data.scene];
    RaytracingAccelerationStructure shadow_scene = RaytracingAccelerationStructure(scene_data.address);
    float3 normal =
        input.world_normal * rsqrt(max(dot(input.world_normal, input.world_normal), 1e-8));
    uint4 visibility = uint4(0xffffffffu);
    for (uint i = 0; i < min(lights.count, 10u); ++i) {
        // Read fields before passing the light by value: nested arrays read
        // through a heap ConstantBuffer are mishandled; see docs/external-defects.md.
        Light light;
        light.position = lights.lights[i].position;
        light.type = lights.lights[i].type;
        light.direction = lights.lights[i].direction;
        light.radius = lights.lights[i].radius;
        light.color = lights.lights[i].color;
        light.intensity = lights.lights[i].intensity;
        light.spot_angles = lights.lights[i].spot_angles;
        light.shadow_sample_count = lights.lights[i].shadow_sample_count;
        light.padding = 0;
        LightSample sample = sample_light(light, input.world_position);
        if (!any(sample.radiance > 0.0) || dot(normal, sample.direction) <= 0.0)
            continue;
        float value =
            shadow_visibility(shadow_scene, input.world_position, normal, light, sample.direction,
                              uint2(input.pos.xy), i, temporal.frame_info.y);
        pack_visibility(visibility, i, value);
    }
    visibility.z = (visibility.z & 65535u) | (object_data.shadow_identity.x << 16);
    visibility.w = object_data.shadow_identity.y;
    ShadowOutput output;
    output.visibility = visibility;
    output.surface = float4(input.world_position, pack_normal(normal));
    return output;
}
