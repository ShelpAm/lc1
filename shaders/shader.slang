import lights;
import shadow_values;
import modules.pbr;
#include "generated/modules/material.slang"
import modules.normal;
import ddgi_screen;

struct PushData {
    uint frame_buffer_index;
};

[[vk::push_constant]]
PushData push_data;

struct FrameBufferObject {
    [[deprecated("no longer used")]]
    float4 clear_color; // TODO: For test only.

    uint camera_index; // view projection
    uint lights_index;
    uint object_data_index; // model
    uint material_index;
    uint shadow_visibility_index;
    uint diffuse_gi_index;
};

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
};

struct LightsData {
    uint32_t count;
    Light lights[10];
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
    ConstantBuffer<FrameBufferObject> frame_buffer =
        ResourceDescriptorHeap[push_data.frame_buffer_index];
    ConstantBuffer<Camera> camera = ResourceDescriptorHeap[frame_buffer.camera_index];
    ConstantBuffer<ObjectData> object_data = ResourceDescriptorHeap[frame_buffer.object_data_index];
    VSOutput output;
    // Read GLM's stored columns explicitly: whole-matrix loads through heap
    // (untyped Uniform) pointers are mishandled here; see docs/external-defects.md.
    float4 world_position = object_data.model[0] * input.position.x +
                            object_data.model[1] * input.position.y +
                            object_data.model[2] * input.position.z + object_data.model[3];
    float4 view_position = camera.view[0] * world_position.x + camera.view[1] * world_position.y +
                           camera.view[2] * world_position.z + camera.view[3] * world_position.w;
    output.pos = camera.proj[0] * view_position.x + camera.proj[1] * view_position.y +
                 camera.proj[2] * view_position.z + camera.proj[3] * view_position.w;
    output.world_position = world_position.xyz;
    output.uv = input.uv;
    output.color = input.color;
    output.world_normal = (object_data.normal_transform[0].xyz * input.normal.x +
                           object_data.normal_transform[1].xyz * input.normal.y +
                           object_data.normal_transform[2].xyz * input.normal.z);
    float handedness = dot(object_data.model[0].xyz,
                           cross(object_data.model[1].xyz, object_data.model[2].xyz)) < 0.0
                           ? -1.0
                           : 1.0;
    output.world_tangent = float4((object_data.model[0].xyz * input.tangent.x +
                                   object_data.model[1].xyz * input.tangent.y +
                                   object_data.model[2].xyz * input.tangent.z),
                                  input.tangent.w * handedness);
    return output;
}

[shader("fragment")]
[earlydepthstencil]
float4 fragMain(VSOutput input, uint sample_index: SV_SampleIndex) : SV_TARGET
{
    ConstantBuffer<FrameBufferObject> frame_buffer =
        ResourceDescriptorHeap[push_data.frame_buffer_index];
    ConstantBuffer<Camera> camera = ResourceDescriptorHeap[frame_buffer.camera_index];
    ConstantBuffer<LightsData> lights = ResourceDescriptorHeap[frame_buffer.lights_index];
    // Shader objects have no sampleShadingEnable state. A static SampleId read
    // enables full sample shading; evaluate UVs at that sample for texture detail.
    input.uv = EvaluateAttributeAtSample(input.uv, sample_index);
    Material material = load_material(frame_buffer.material_index);
    Texture2D<uint4> shadow_visibility_values =
        ResourceDescriptorHeap[frame_buffer.shadow_visibility_index];
    uint4 visibility = shadow_visibility_values.Load(int3(int2(input.pos.xy), 0));
    float3 base_color = saturate(
        material.base_color_texture.Sample(material.base_color_texture_sampler, input.uv).rgb *
        material.base_color_factor.rgb * input.color);
    float4 mr = material.metallic_roughness_texture.Sample(
        material.metallic_roughness_texture_sampler, input.uv);
    float metallic = saturate(material.metallic_factor * mr.b);
    float roughness = saturate(material.roughtness_factor * mr.g);
    float3 surface_normal =
        input.world_normal * rsqrt(max(dot(input.world_normal, input.world_normal), 1e-8));
    float3 normal = surface_normal;
    // Missing maps and zero scale use the unperturbed mesh normal, with no sample.
    if ((material.flags & 1u) != 0 && material.normal_scale > 0.0)
        normal = mapped_normal(
            surface_normal, input.world_tangent,
            material.normal_texture.Sample(material.normal_texture_sampler, input.uv).rgb,
            material.normal_scale);
    float3 to_view = camera.position.xyz - input.world_position;
    float3 view_direction = to_view * rsqrt(max(dot(to_view, to_view), 1e-8));
    // Direct lighting until IBL is added. Emission is independent of shadows.
    float3 color =
        material.emissive_texture.Sample(material.emissive_texture_sampler, input.uv).rgb *
        material.emissive_factor;
    if (frame_buffer.diffuse_gi_index != 0xffffffffu) {
        float3 irradiance = ddgi_screen_irradiance(frame_buffer.diffuse_gi_index,
                                                 input.pos.xy, input.world_position, surface_normal);
        color += irradiance * base_color * (1.0 - metallic) / 3.14159265359;
    }
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
        float n_dot_l = saturate(dot(normal, sample.direction));
        // Visibility uses the unperturbed normal. Do not let a normal map light
        // the back of that surface using a skipped/default-white shadow value.
        if (!any(sample.radiance > 0.0) || n_dot_l <= 0.0 ||
            dot(surface_normal, sample.direction) <= 0.0 ||
            dot(surface_normal, view_direction) <= 0.0)
            continue;

        float3 brdf =
            evaluate_pbr(base_color, metallic, roughness, normal, view_direction, sample.direction);
        color += unpack_visibility(visibility, i) * sample.radiance * brdf * n_dot_l;
    }

    // Opaque output; preserve HDR up to the largest finite RGBA16F value.
    return float4(min(color, float3(65504.0)), 1.0);
}

