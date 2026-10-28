#include "lc1/scene/model-instance.hpp"
#include "lc1/scene/model.hpp"

#include <tiny_gltf.h>

#include <bit>
#include <chrono>
#include <filesystem>
#include <iostream>
#include <limits>
#include <stdexcept>

namespace {
void check(bool condition, char const *message)
{
    if (!condition)
        throw std::runtime_error(message);
}

template <typename F> void rejects(F action)
{
    try {
        action();
    }
    catch (lc1::Error const &) {
        return;
    }
    throw std::runtime_error("expected glTF import failure");
}

class Fixture {
  public:
    Fixture()
    {
        directory_ = std::filesystem::temp_directory_path() /
                     ("lc1-model-loading-" +
                      std::to_string(std::chrono::steady_clock::now().time_since_epoch().count()));
        std::filesystem::create_directory(directory_);
    }

    ~Fixture()
    {
        std::error_code error;
        std::filesystem::remove_all(directory_, error);
    }

    std::filesystem::path write(tinygltf::Model model, bool binary = false,
                                bool embed_buffers = true)
    {
        auto const path = directory_ / (binary ? "model.glb" : "model.gltf");
        tinygltf::TinyGLTF writer;
        check(
            writer.WriteGltfSceneToFile(&model, path.string(), true, embed_buffers, false, binary),
            "write fixture");
        return path;
    }

  private:
    std::filesystem::path directory_;
};

void append(std::vector<unsigned char> &data, std::uint32_t bits, int size = 4)
{
    for (int i = 0; i < size; ++i)
        data.push_back(static_cast<unsigned char>((bits >> (i * 8)) & 255));
}

void append_float(std::vector<unsigned char> &data, float value)
{
    append(data, std::bit_cast<std::uint32_t>(value));
}

tinygltf::Model triangle()
{
    tinygltf::Model source;
    source.asset.version = "2.0";
    source.buffers.emplace_back();
    auto &data = source.buffers[0].data;
    // Interleaved positions/normals; the nonzero buffer and accessor offsets are intentional.
    append_float(data, 42);
    for (auto const position : {glm::vec3{0, 0, 0}, glm::vec3{1, 0, 0}, glm::vec3{0, 1, 0}}) {
        for (int i = 0; i < 3; ++i)
            append_float(data, position[i]);
        for (float normal : {0.0F, 0.0F, 1.0F})
            append_float(data, normal);
    }
    tinygltf::BufferView view;
    view.buffer = 0;
    view.byteOffset = 4;
    view.byteLength = 72;
    view.byteStride = 24;
    source.bufferViews.push_back(view);
    tinygltf::Accessor accessor;
    accessor.bufferView = 0;
    accessor.componentType = TINYGLTF_COMPONENT_TYPE_FLOAT;
    accessor.type = TINYGLTF_TYPE_VEC3;
    accessor.count = 3;
    source.accessors.push_back(accessor);
    accessor.byteOffset = 12;
    source.accessors.push_back(accessor);
    view.byteOffset = data.size();
    view.byteLength = 6;
    view.byteStride = 0;
    source.bufferViews.push_back(view);
    for (auto index : {0U, 1U, 2U})
        append(data, index, 2);
    accessor.bufferView = 1;
    accessor.byteOffset = 0;
    accessor.count = 3;
    accessor.componentType = TINYGLTF_COMPONENT_TYPE_UNSIGNED_SHORT;
    accessor.type = TINYGLTF_TYPE_SCALAR;
    source.accessors.push_back(accessor);
    while (data.size() % 4)
        data.push_back(0);
    view.byteOffset = data.size();
    view.byteLength = 8;
    source.bufferViews.push_back(view);
    append_float(data, 0);
    append_float(data, 1);
    accessor.bufferView = 2;
    accessor.count = 2;
    accessor.componentType = TINYGLTF_COMPONENT_TYPE_FLOAT;
    source.accessors.push_back(accessor);
    view.byteOffset = data.size();
    view.byteLength = 24;
    source.bufferViews.push_back(view);
    for (float value : {0.0F, 0.0F, 0.0F, 2.0F, 0.0F, 0.0F})
        append_float(data, value);
    accessor.bufferView = 3;
    accessor.type = TINYGLTF_TYPE_VEC3;
    source.accessors.push_back(accessor);
    tinygltf::Primitive primitive;
    primitive.attributes = {{"POSITION", 0}, {"NORMAL", 1}};
    primitive.indices = 2;
    primitive.mode = TINYGLTF_MODE_TRIANGLES;
    tinygltf::Mesh mesh;
    mesh.primitives.push_back(primitive);
    source.meshes.push_back(mesh);
    // Child precedes parent in the file; import must reorder and remap animation targets.
    tinygltf::Node child;
    child.name = "child";
    child.mesh = 0;
    tinygltf::Node parent;
    parent.name = "parent";
    parent.children = {0};
    parent.translation = {0, 3, 0};
    source.nodes = {child, parent};
    tinygltf::Scene scene;
    scene.nodes = {1};
    source.scenes.push_back(scene);
    source.defaultScene = 0;
    tinygltf::Animation animation;
    animation.name = "Move";
    tinygltf::AnimationSampler sampler;
    sampler.input = 3;
    sampler.output = 4;
    sampler.interpolation = "LINEAR";
    animation.samplers.push_back(sampler);
    tinygltf::AnimationChannel channel;
    channel.sampler = 0;
    channel.target_node = 0;
    channel.target_path = "translation";
    animation.channels.push_back(channel);
    source.animations.push_back(animation);
    return source;
}

bool near(glm::vec3 a, glm::vec3 b)
{
    return glm::length(a - b) < 1e-5F;
}

void files_and_animation(Fixture &fixture)
{
    for (bool binary : {false, true}) {
        auto source = triangle();
        tinygltf::Node correction;
        correction.name = "Axis and unit correction";
        correction.scale = {2, 2, 2};
        correction.children = source.scenes[0].nodes;
        source.scenes[0].nodes = {static_cast<int>(source.nodes.size())};
        source.nodes.push_back(std::move(correction));
        auto model = lc1::scene::Model::load_from_file(fixture.write(source, binary));
        check(model.mesh_count() == 1 && model.indices().size() == 3, "triangle geometry");
        check(near(model.vertices()[1].position, {1, 0, 0}), "strided positions");
        check(near(model.vertices()[1].normal, {0, 0, 1}), "strided normals with accessor offset");
        auto definition = std::move(model).definition();
        lc1::scene::ModelInstance instance{definition};
        auto const child = definition.find_node("child");
        check(child.has_value(), "node name retained");
        instance.play_animation(0, false);
        instance.update_animation(0.5F);
        auto draws = instance.mesh_instances(glm::mat4{1});
        check(draws.size() == 1, "one draw");
        check(near(glm::vec3{instance.node_world_matrix(*child, glm::mat4{1})[3]}, {2, 6, 0}),
              "animation target remapping and correction order");
    }
}

void geometry_variants(Fixture &fixture)
{
    auto source = triangle();
    source.meshes[0].primitives[0].attributes.erase("NORMAL");
    source.meshes[0].primitives[0].indices = -1;
    source.meshes[0].primitives.push_back(source.meshes[0].primitives[0]);
    source.nodes[1].mesh = 0;
    source.nodes[0].matrix = {1, 0, 0, 0, 0, 1, 0, 0, 0, 0, 1, 0, 4, 0, 0, 1};
    source.animations.clear();
    auto const model = lc1::scene::Model::load_from_file(fixture.write(source));
    check(model.mesh_count() == 2 && model.definition().draw_count() == 4,
          "shared multi-primitive meshes");
    check(near(model.vertices()[0].normal, {0, 0, 1}), "generated normals");
    lc1::scene::ModelInstance instance{model.definition()};
    check(near(glm::vec3{instance.node_world_matrix(*model.definition().find_node("child"),
                                                    glm::mat4{1})[3]},
               {4, 3, 0}),
          "matrix node");
    source.scenes.clear();
    source.defaultScene = -1;
    check(lc1::scene::Model::load_from_file(fixture.write(source)).definition().draw_count() == 4,
          "root inference without scenes");
}

void attribute_encodings(Fixture &fixture)
{
    for (auto const type :
         {TINYGLTF_COMPONENT_TYPE_UNSIGNED_BYTE, TINYGLTF_COMPONENT_TYPE_UNSIGNED_INT}) {
        auto source = triangle();
        auto &data = source.buffers[0].data;
        auto &view = source.bufferViews[1];
        view.byteOffset = data.size();
        int const size = type == TINYGLTF_COMPONENT_TYPE_UNSIGNED_BYTE ? 1 : 4;
        view.byteLength = static_cast<std::size_t>(3 * size);
        for (auto index : {0U, 1U, 2U})
            append(data, index, size);
        source.accessors[2].componentType = type;
        tinygltf::BufferView colors;
        colors.buffer = 0;
        colors.byteOffset = data.size();
        colors.byteLength = 12;
        source.bufferViews.push_back(colors);
        for (int i = 0; i < 3; ++i)
            for (auto value : {255U, 128U, 0U, 255U})
                data.push_back(static_cast<unsigned char>(value));
        tinygltf::Accessor accessor;
        accessor.bufferView = 4;
        accessor.componentType = TINYGLTF_COMPONENT_TYPE_UNSIGNED_BYTE;
        accessor.type = TINYGLTF_TYPE_VEC4;
        accessor.normalized = true;
        accessor.count = 3;
        source.accessors.push_back(accessor);
        source.meshes[0].primitives[0].attributes["COLOR_0"] = 5;
        auto const model = lc1::scene::Model::load_from_file(fixture.write(source, false, false));
        check(model.indices()[2] == 2, "unsigned index encodings and external buffer");
        check(near(model.vertices()[0].color, {1, 128.0F / 255.0F, 0}), "normalized vertex colors");
    }
}

void invalid_files(Fixture &fixture)
{
    auto invalid = [&](auto change) {
        auto source = triangle();
        change(source);
        rejects([&] { lc1::scene::Model::load_from_file(fixture.write(source)); });
    };
    invalid([](auto &s) { s.bufferViews[0].byteLength = 8; });
    invalid([](auto &s) { s.accessors[0].count = std::numeric_limits<std::size_t>::max(); });
    invalid([](auto &s) { s.buffers[0].data[76] = 9; });
    invalid([](auto &s) { s.nodes[0].children = {1}; });
    invalid([](auto &s) { s.scenes[0].nodes = {1, 0}; });
    invalid([](auto &s) { s.nodes[1].children = {99}; });
    invalid([](auto &s) { s.skins.emplace_back(); });
    invalid([](auto &s) { s.animations[0].samplers[0].interpolation = "CUBICSPLINE"; });
    invalid([](auto &s) { s.animations[0].channels[0].target_path = "weights"; });
    invalid([](auto &s) { s.meshes[0].primitives[0].mode = TINYGLTF_MODE_LINE; });
    invalid([](auto &s) { s.meshes[0].primitives[0].attributes["JOINTS_0"] = 0; });
    invalid([](auto &s) { s.extensionsRequired = {"KHR_draco_mesh_compression"}; });
    invalid([](auto &s) { s.nodes[0].matrix = {1, 0, 0, 0, 1, 1, 0, 0, 0, 0, 1, 0, 0, 0, 0, 1}; });
    rejects([&] { lc1::scene::Model::load_from_file("missing-model.glb"); });
    rejects([&] { lc1::scene::Model::load_from_file("unsupported.fbx"); });
}
} // namespace

int main()
{
    try {
        Fixture fixture;
        files_and_animation(fixture);
        geometry_variants(fixture);
        attribute_encodings(fixture);
        invalid_files(fixture);
        std::cout << "Model loading tests passed\n";
    }
    catch (std::exception const &error) {
        std::cerr << error.what() << '\n';
        return 1;
    }
}
