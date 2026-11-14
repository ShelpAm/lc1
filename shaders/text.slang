// Blended text: glyph quads built on the CPU sample one atlas. The same shader
// serves world text (view-projection, drawn inside the HDR pass with the scene
// depth) and screen text (a pixel transform, drawn after tone mapping), which
// only differ in the transform pushed below.

struct PushData {
    // The transform's columns, named individually: this pass uses no matrix type,
    // and every matrix multiply here is written out explicitly. The matrix-load
    // issues behind that convention: docs/external-defects.md.
    float4 transform_0;
    float4 transform_1;
    float4 transform_2;
    float4 transform_3;
    uint atlas_index;
    uint sampler_index;
};
[[vk::push_constant]] ConstantBuffer<PushData> push_data;

// A subset of lc1::Vertex: the mesh vertex layout is reused unchanged, so the
// attributes text does not need are simply not declared.
struct VSInput {
    [[vk::location(0)]] float3 position;
    [[vk::location(2)]] float3 color;
    [[vk::location(3)]] float2 uv;
};

struct VSOutput {
    float4 pos : SV_Position;
    float3 color;
    float2 uv;
};

[shader("vertex")]
VSOutput vertMain(VSInput input)
{
    float4 position = float4(input.position, 1.0);
    VSOutput output;
    output.pos = push_data.transform_0 * position.x + push_data.transform_1 * position.y +
        push_data.transform_2 * position.z + push_data.transform_3 * position.w;
    output.color = input.color;
    output.uv = input.uv;
    return output;
}

[shader("fragment")]
float4 fragMain(VSOutput input) : SV_TARGET
{
    Texture2D<float4> atlas = ResourceDescriptorHeap[push_data.atlas_index];
    SamplerState atlas_sampler = SamplerDescriptorHeap[push_data.sampler_index];
    // The atlas stores white RGB with glyph coverage in alpha; level 0 keeps one
    // cell's texels from bleeding into its neighbours.
    float coverage = atlas.SampleLevel(atlas_sampler, input.uv, 0.0).a;
    // World text writes depth so the cloud layer cannot cover a label. The glyph quad
    // covers more than the glyph, so transparent texels have to be dropped here:
    // otherwise every quad writes depth and carves a box-shaped hole in the clouds
    // behind the label, showing as the label's own blue background.
    if (coverage < 0.1)
        discard;
    return float4(input.color, coverage);
}
