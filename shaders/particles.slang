// Procedural camera-facing flame/ember sprites; no texture or material binding.
struct PushData {
    float4 transform_0;
    float4 transform_1;
    float4 transform_2;
    float4 transform_3;
    float4 center_width;
    float4 color_alpha;
    float4 right_height;
    float4 up_rotation;
};
[[vk::push_constant]] ConstantBuffer<PushData> settings;
struct Varying {
    float4 position : SV_Position;
    float2 uv;
};
[shader("vertex")]
Varying vertMain(uint id : SV_VertexID)
{
    float2 corners[6] = {float2(-1,-1), float2(1,-1), float2(1,1),
                         float2(-1,-1), float2(1,1), float2(-1,1)};
    float2 uv = corners[id];
    float angle = settings.up_rotation.w;
    float2 rotated = float2(cos(angle)*uv.x-sin(angle)*uv.y,
                            sin(angle)*uv.x+cos(angle)*uv.y);
    float3 world = settings.center_width.xyz +
        settings.right_height.xyz * rotated.x * settings.center_width.w * 0.5 +
        settings.up_rotation.xyz * rotated.y * abs(settings.right_height.w) * 0.5;
    Varying output;
    output.position = settings.transform_0 * world.x + settings.transform_1 * world.y +
                      settings.transform_2 * world.z + settings.transform_3;
    output.uv = uv;
    return output;
}
[shader("fragment")]
float4 fragMain(Varying input) : SV_Target
{
    float2 uv = input.uv;
    // Flame lobes taper upward; the analytic profile has no hard rectangular edge.
    float width = lerp(0.9, 0.42, saturate(uv.y * 0.5 + 0.5));
    float radius = length(float2(uv.x / width, uv.y));
    float alpha = pow(saturate(1.0 - radius), 1.5);
    if (settings.right_height.w < 0)
        alpha = pow(saturate(1.0 - length(uv)), 0.65);
    return float4(settings.color_alpha.rgb, alpha * settings.color_alpha.a);
}
