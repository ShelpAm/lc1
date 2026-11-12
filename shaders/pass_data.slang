struct PassData {
    uint camera;
    uint lights;
    uint object;
    uint temporal;
    uint scene;
    uint current_visibility;
    uint current_surface;
    uint history_visibility;
    uint history_surface;
    uint resolved_visibility;
    uint tone_map;
    uint hdr_image;
    uint bloom_image;
    uint bloom_sampler;
};
[[vk::push_constant]] ConstantBuffer<PassData> pass_data;
