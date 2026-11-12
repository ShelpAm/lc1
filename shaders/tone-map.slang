import pass_data;
struct ToneMapData {
    float4 output; // Exposure, manual sRGB encoding, Bloom strength, reserved.
};

[shader("vertex")]
float4 vertMain(uint vertex_id : SV_VertexID) : SV_Position
{
    float2 position = float2((vertex_id << 1) & 2, vertex_id & 2);
    return float4(position * 2.0 - 1.0, 0.0, 1.0);
}

float3 linear_to_srgb(float3 value)
{
    return select(value <= 0.0031308, value * 12.92,
                  1.055 * pow(value, float3(1.0 / 2.4)) - 0.055);
}

[shader("fragment")]
float4 fragMain(float4 position : SV_Position) : SV_Target
{
    ConstantBuffer<ToneMapData> settings = ResourceDescriptorHeap[pass_data.tone_map];
    Texture2D<float4> hdr_color = ResourceDescriptorHeap[pass_data.hdr_image];

    float3 value = max(hdr_color.Load(int3(int2(position.xy), 0)).rgb, float3(0));
    if (settings.output.z > 0) {
        Texture2D<float4> bloom = ResourceDescriptorHeap[pass_data.bloom_image];
        SamplerState filter = SamplerDescriptorHeap[pass_data.bloom_sampler];
        uint width, height;
        hdr_color.GetDimensions(width, height);
        value += bloom.SampleLevel(filter, position.xy / float2(width, height), 0).rgb * settings.output.z;
    }
    // Equivalent to x/(1+x), also finite if a very large exposure overflows x.
    float3 mapped = 1.0 - 1.0 / (1.0 + value * settings.output.x);
    if (settings.output.y > 0.5)
        mapped = linear_to_srgb(mapped);
    return float4(mapped, 1.0);
}
