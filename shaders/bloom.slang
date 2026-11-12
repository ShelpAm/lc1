struct PushData {
    uint image_index;
    uint sampler_index;
    uint mode; // 0: downsample and soft threshold; 1/2: horizontal/vertical blur.
    uint reserved;
    float4 dimensions; // Source width/height, target width/height.
};
[[vk::push_constant]] ConstantBuffer<PushData> settings;
[shader("vertex")]
float4 vertMain(uint id : SV_VertexID) : SV_Position
{
    float2 p = float2((id << 1) & 2, id & 2);
    return float4(p * 2 - 1, 0, 1);
}
[shader("fragment")]
float4 fragMain(float4 position : SV_Position) : SV_Target
{
    Texture2D<float4> image = ResourceDescriptorHeap[settings.image_index];
    SamplerState filter = SamplerDescriptorHeap[settings.sampler_index];
    float2 uv = position.xy / settings.dimensions.zw;
    float2 texel = 1.0 / settings.dimensions.xy;
    if (settings.mode == 0) {
        float3 value = float3(0);
        for (int y = -1; y <= 1; y += 2)
            for (int x = -1; x <= 1; x += 2) {
                float3 color = image.SampleLevel(filter, uv + float2(x,y) * texel * 0.5, 0).rgb;
                float brightness = max(color.r, max(color.g, color.b));
                // Soft knee around linear HDR 1; no exposure feedback or tone mapping here.
                float knee = clamp(brightness - 0.5, 0.0, 1.0);
                float contribution = max(brightness - 1.0, knee * knee * 0.5);
                value += color * contribution / max(brightness, 0.0001);
            }
        return float4(value * 0.25, 1);
    }
    float2 step = settings.mode == 1 ? float2(texel.x, 0) : float2(0, texel.y);
    float3 value = image.SampleLevel(filter, uv, 0).rgb * 0.227027;
    value += (image.SampleLevel(filter, uv + step * 1.384615, 0).rgb +
              image.SampleLevel(filter, uv - step * 1.384615, 0).rgb) * 0.316216;
    value += (image.SampleLevel(filter, uv + step * 3.230769, 0).rgb +
              image.SampleLevel(filter, uv - step * 3.230769, 0).rgb) * 0.070270;
    return float4(value, 1);
}
