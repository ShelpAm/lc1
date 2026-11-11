import ddgi_common;
[[vk::push_constant]] uint ddgi_index;

[shader("vertex")]
float4 candidateVert(uint id : SV_VertexID) : SV_Position
{
    float2 uv = float2((id << 1) & 2, id & 2);
    return float4(uv * 2.0 - 1.0, 0, 1);
}

[shader("fragment")]
uint4 candidateFrag(float4 position : SV_Position) : SV_Target
{
    ConstantBuffer<DdgiData> data = ResourceDescriptorHeap[ddgi_index];
    Texture2D<float4> triangles = ResourceDescriptorHeap[data.emitter_images.x];
    uint mask_index = uint(position.x) % data.emitter_images.z;
    uint cell = uint(position.x) / data.emitter_images.z + uint(position.y) * 64u;
    uint cascade = cell / 2048u;
    float3 minimum = data.origin_spacing[cascade].xyz + float3(ddgi_probe_coord(cell)) * data.origin_spacing[cascade].w;
    float3 maximum = minimum + data.origin_spacing[cascade].w;
    float magnitude = max(max(max(abs(minimum.x), abs(minimum.y)), abs(minimum.z)),
                          max(max(abs(maximum.x), abs(maximum.y)), abs(maximum.z)));
    float bias = max(0.002, magnitude * 2e-6);
    minimum -= bias;
    maximum += bias;
    uint4 mask = uint4(0);
    for (uint i = 0; i < 128u; ++i) {
        uint sample = mask_index * 128u + i;
        if (sample >= data.emission_info.x)
            break;
        // Ignore radiance here so animated material/texture brightness does not
        // invalidate a spatial list; the actual query reads current radiance.
        float3 a = triangles.Load(int3(0, sample, 0)).xyz;
        float3 b = triangles.Load(int3(1, sample, 0)).xyz;
        float3 c = triangles.Load(int3(2, sample, 0)).xyz;
        float3 center = (a + b + c) / 3.0;
        float3 delta = center - clamp(center, minimum, maximum);
        // The exact existing near-field weight is zero at eight metres.
        if (dot(delta, delta) <= 64.0001)
            mask[i / 32u] |= 1u << (i % 32u);
    }
    return mask;
}
