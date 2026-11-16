// Procedural camera-facing flame/ember sprites; no texture or material binding.
struct PushData {
    float4 transform_0;
    float4 transform_1;
    float4 transform_2;
    float4 transform_3;
    float4 right;
    float4 up;
    float *instances;
};
[[vk::push_constant]] ConstantBuffer<PushData> settings;
struct Varying {
    float4 position : SV_Position;
    float2 uv;
    nointerpolation float4 color_alpha;
    nointerpolation float height;
};
[shader("vertex")]
Varying vertMain(uint id : SV_VertexID, uint instance : SV_InstanceID)
{
    float2 corners[6] = {float2(-1,-1), float2(1,-1), float2(1,1),
                         float2(-1,-1), float2(1,1), float2(-1,1)};
    float2 uv = corners[id];
    uint base = instance * 12;
    float4 center_width = float4(settings.instances[base], settings.instances[base+1],
                                settings.instances[base+2], settings.instances[base+3]);
    float4 color_alpha = float4(settings.instances[base+4], settings.instances[base+5],
                               settings.instances[base+6], settings.instances[base+7]);
    float height = settings.instances[base+8];
    float angle = settings.instances[base+9];
    float2 rotated = float2(cos(angle)*uv.x-sin(angle)*uv.y,
                            sin(angle)*uv.x+cos(angle)*uv.y);
    float3 world = center_width.xyz +
        settings.right.xyz * rotated.x * center_width.w * 0.5 +
        settings.up.xyz * rotated.y * abs(height) * 0.5;
    Varying output;
    output.position = settings.transform_0 * world.x + settings.transform_1 * world.y +
                      settings.transform_2 * world.z + settings.transform_3;
    output.uv = uv;
    output.color_alpha = color_alpha;
    output.height = height;
    return output;
}
[shader("fragment")]
float4 fragMain(Varying input) : SV_Target
{
    float2 uv = input.uv;
    // Small round kernels accumulate into a flame shaped by the emitter distribution.
    float radius = length(uv);
    float alpha = pow(saturate(1.0 - radius), 1.5);
    if (input.height < 0)
        alpha = pow(saturate(1.0 - length(uv)), 0.65);
    return float4(input.color_alpha.rgb, alpha * input.color_alpha.a);
}
