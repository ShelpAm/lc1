import ddgi_common;
import ddgi_emission;
[[vk::push_constant]] uint ddgi_index;

[shader("vertex")]
float4 emitterVert(uint id : SV_VertexID) : SV_Position
{
    float2 uv = float2((id << 1) & 2, id & 2);
    return float4(uv * 2.0 - 1.0, 0, 1);
}

[shader("fragment")]
float4 emitterPrepare(float4 position : SV_Position) : SV_Target
{
    ConstantBuffer<DdgiData> data = ResourceDescriptorHeap[ddgi_index];
    uint sample_index = uint(position.y);
    if (sample_index >= data.emission_info.x)
        return float4(0);
    uint2 emitter = data.emitters[sample_index / 4u];
    DdgiGeometry geometry = data.geometry[emitter.x];
    uint triangle = emitter.y * 3u;
    uint a = ddgi_index_at(geometry, triangle);
    uint b = ddgi_index_at(geometry, triangle + 1u);
    uint c = ddgi_index_at(geometry, triangle + 2u);
    ConstantBuffer<DdgiObject> object = ResourceDescriptorHeap[NonUniformResourceIndex(geometry.object_index)];
    float3 pa = ddgi_vertex3(geometry, a, 0);
    float3 pb = ddgi_vertex3(geometry, b, 0);
    float3 pc = ddgi_vertex3(geometry, c, 0);
    pa = (object.model[0] * pa.x + object.model[1] * pa.y + object.model[2] * pa.z + object.model[3]).xyz;
    pb = (object.model[0] * pb.x + object.model[1] * pb.y + object.model[2] * pb.z + object.model[3]).xyz;
    pc = (object.model[0] * pc.x + object.model[1] * pc.y + object.model[2] * pc.z + object.model[3]).xyz;
    float3 wa, wb, wc;
    switch (sample_index % 4u) {
    case 0: wa = float3(1,0,0); wb = float3(.5,.5,0); wc = float3(.5,0,.5); break;
    case 1: wa = float3(.5,.5,0); wb = float3(0,1,0); wc = float3(0,.5,.5); break;
    case 2: wa = float3(.5,0,.5); wb = float3(0,.5,.5); wc = float3(0,0,1); break;
    default: wa = float3(.5,.5,0); wb = float3(0,.5,.5); wc = float3(.5,0,.5); break;
    }
    float3 p0 = pa * wa.x + pb * wa.y + pc * wa.z;
    float3 p1 = pa * wb.x + pb * wb.y + pc * wb.z;
    float3 p2 = pa * wc.x + pb * wc.y + pc * wc.z;
    if (dot(object.model[0].xyz, cross(object.model[1].xyz, object.model[2].xyz)) < 0.0) {
        float3 swap_position = p1;
        p1 = p2;
        p2 = swap_position;
    }
    float3 bary = (wa + wb + wc) / 3.0;
    float2 uv = ddgi_vertex3(geometry, a, 9).xy * bary.x +
                ddgi_vertex3(geometry, b, 9).xy * bary.y +
                ddgi_vertex3(geometry, c, 9).xy * bary.z;
    Material material = load_material(NonUniformResourceIndex(geometry.material_index));
    float3 radiance = material.emissive_factor *
        material.emissive_texture.SampleLevel(material.emissive_texture_sampler, uv, 0).rgb;
    float3 center = (pa + pb + pc) / 3.0;
    float radius = max(max(length(pa - center), length(pb - center)), length(pc - center));
    switch (uint(position.x)) {
    case 0: return float4(p0, center.x);
    case 1: return float4(p1, center.y);
    case 2: return float4(p2, center.z);
    default: return float4(radiance, radius);
    }
}
