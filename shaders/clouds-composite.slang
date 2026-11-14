// Composite of the cloud radiance/transmittance image onto the HDR scene.
//
//   result = cloud_radiance + cloud_transmittance * background
//
// Both inputs are read at the same pixel: the march ran at full resolution, so this
// pass needs no sampler and no filtering, which keeps geometry edges exact until the
// low-resolution path exists.

struct CompositePushData {
    uint cloud; // rgb accumulated radiance, a accumulated transmittance
    uint hdr;   // linear HDR scene colour, alpha 1 for opaque geometry
};
[[vk::push_constant]] ConstantBuffer<CompositePushData> push;

[shader("vertex")]
float4 vertMain(uint vertex_id : SV_VertexID) : SV_Position
{
    float2 position = float2((vertex_id << 1) & 2, vertex_id & 2);
    return float4(position * 2.0 - 1.0, 0.0, 1.0);
}

[shader("fragment")]
float4 fragMain(float4 position : SV_Position) : SV_Target
{
    Texture2D<float4> cloud = ResourceDescriptorHeap[push.cloud];
    Texture2D<float4> background = ResourceDescriptorHeap[push.hdr];
    int3 pixel = int3(int2(position.xy), 0);
    float4 cloud_value = cloud.Load(pixel);
    float4 scene_value = background.Load(pixel);
    return float4(cloud_value.rgb + cloud_value.a * scene_value.rgb, scene_value.a);
}
