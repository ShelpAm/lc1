#include "lc1/error.hpp"
#include "lc1/scene/model.hpp"

#include <spdlog/spdlog.h>
#include <tiny_gltf.h>

#include <algorithm>
#include <bit>
#include <cmath>
#include <limits>
#include <numeric>
#include <optional>
#include <utility>

namespace lc1::scene {
namespace {
template <typename T> T const &element(std::vector<T> const &values, int index)
{
    if (index < 0 || static_cast<std::size_t>(index) >= values.size())
        fail("glTF array index is out of range: {}", index);
    return values[static_cast<std::size_t>(index)];
}

// Read little-endian components without alignment or aliasing assumptions.
class AccessorReader {
  public:
    AccessorReader(tinygltf::Model const &model, int index, int type)
        : accessor_(element(model.accessors, index))
    {
        if (accessor_.type != type || accessor_.sparse.isSparse)
            fail("unsupported glTF accessor shape or sparse storage");
        component_size_ = tinygltf::GetComponentSizeInBytes(accessor_.componentType);
        components_ = tinygltf::GetNumComponentsInType(type);
        if (component_size_ <= 0 || component_size_ > 4 || components_ <= 0 ||
            (accessor_.componentType == TINYGLTF_COMPONENT_TYPE_FLOAT && accessor_.normalized))
            fail("invalid glTF accessor component type");
        auto const &view = element(model.bufferViews, accessor_.bufferView);
        auto const &buffer = element(model.buffers, view.buffer);
        auto const packed = static_cast<std::size_t>(component_size_ * components_);
        stride_ = view.byteStride == 0 ? packed : view.byteStride;
        if (stride_ < packed || stride_ % static_cast<std::size_t>(component_size_) != 0 ||
            view.byteOffset > buffer.data.size() ||
            view.byteLength > buffer.data.size() - view.byteOffset ||
            accessor_.byteOffset > view.byteLength)
            fail("invalid glTF accessor buffer range or stride");
        auto const available = view.byteLength - accessor_.byteOffset;
        if (accessor_.count != 0 &&
            (available < packed || (accessor_.count - 1) > (available - packed) / stride_))
            fail("glTF accessor exceeds its buffer view");
        data_ = std::span{buffer.data}.subspan(view.byteOffset + accessor_.byteOffset, available);
    }

    std::size_t count() const { return accessor_.count; }

    int component_type() const { return accessor_.componentType; }

    bool normalized() const { return accessor_.normalized; }

    double read(std::size_t index, int component = 0) const
    {
        if (index >= count() || component < 0 || component >= components_)
            fail("glTF accessor element is out of range");
        auto const offset = index * stride_ + static_cast<std::size_t>(component * component_size_);
        std::uint32_t bits = 0;
        for (int byte = 0; byte < component_size_; ++byte)
            bits |= static_cast<std::uint32_t>(data_[offset + static_cast<std::size_t>(byte)])
                    << (8 * byte);
        double value{};
        switch (accessor_.componentType) {
        case TINYGLTF_COMPONENT_TYPE_FLOAT:
            value = std::bit_cast<float>(bits);
            break;
        case TINYGLTF_COMPONENT_TYPE_UNSIGNED_BYTE:
            value = normalized() ? bits / 255.0 : bits;
            break;
        case TINYGLTF_COMPONENT_TYPE_UNSIGNED_SHORT:
            value = normalized() ? bits / 65535.0 : bits;
            break;
        case TINYGLTF_COMPONENT_TYPE_UNSIGNED_INT:
            value = bits;
            break;
        default:
            fail("unsupported glTF accessor component encoding");
        }
        if (!std::isfinite(value))
            fail("glTF accessor contains a non-finite value");
        return value;
    }

    void require_float() const
    {
        if (component_type() != TINYGLTF_COMPONENT_TYPE_FLOAT || normalized())
            fail("glTF attribute requires unnormalized float components");
    }

    glm::vec3 vec3(std::size_t index) const
    {
        return {read(index, 0), read(index, 1), read(index, 2)};
    }

  private:
    tinygltf::Accessor const &accessor_;
    std::span<unsigned char const> data_;
    std::size_t stride_{};
    int component_size_{};
    int components_{};
};

NodeTransform matrix_transform(glm::mat4 const &matrix)
{
    NodeTransform result{.translation = glm::vec3{matrix[3]}};
    glm::mat3 basis{matrix};
    for (glm::length_t column = 0; column < 3; ++column) {
        result.scale[column] = glm::length(basis[column]);
        if (!std::isfinite(result.scale[column]) || result.scale[column] == 0)
            fail("glTF node matrix must be finite and invertible");
        basis[column] /= result.scale[column];
    }
    if (glm::determinant(basis) < 0) {
        result.scale.x = -result.scale.x;
        basis[0] = -basis[0];
    }
    result.rotation = glm::quat_cast(basis);
    auto const reconstructed = result.matrix();
    for (glm::length_t column = 0; column < 4; ++column)
        for (glm::length_t row = 0; row < 4; ++row)
            if (!std::isfinite(matrix[column][row]) ||
                std::abs(matrix[column][row] - reconstructed[column][row]) >
                    1e-5F * std::max(1.0F, std::abs(matrix[column][row])))
                fail("glTF node matrix contains unsupported shear or perspective");
    return result;
}

NodeTransform node_transform(tinygltf::Node const &node)
{
    if (!node.matrix.empty()) {
        if (node.matrix.size() != 16 || !node.translation.empty() || !node.rotation.empty() ||
            !node.scale.empty())
            fail("invalid glTF node matrix/TRS combination");
        glm::mat4 matrix{};
        for (std::size_t i = 0; i < 16; ++i)
            matrix[static_cast<glm::length_t>(i / 4)][static_cast<glm::length_t>(i % 4)] =
                static_cast<float>(node.matrix[i]);
        return matrix_transform(matrix);
    }
    NodeTransform result;
    auto assign = [](std::vector<double> const &source, glm::vec3 &target) {
        if (source.empty())
            return;
        if (source.size() != 3)
            fail("invalid glTF node vector");
        target = {source[0], source[1], source[2]};
    };
    assign(node.translation, result.translation);
    assign(node.scale, result.scale);
    if (!node.rotation.empty()) {
        if (node.rotation.size() != 4)
            fail("invalid glTF node quaternion");
        result.rotation = {
            static_cast<float>(node.rotation[3]), static_cast<float>(node.rotation[0]),
            static_cast<float>(node.rotation[1]), static_cast<float>(node.rotation[2])};
    }
    result.matrix();
    return result;
}

Mesh import_primitive(tinygltf::Model const &model, tinygltf::Primitive const &primitive)
{
    if ((primitive.mode != -1 && primitive.mode != TINYGLTF_MODE_TRIANGLES) ||
        !primitive.targets.empty() || !primitive.extensions.empty())
        fail("unsupported glTF primitive mode, morph targets or extensions");
    auto const position = primitive.attributes.find("POSITION");
    if (position == primitive.attributes.end())
        fail("glTF primitive has no POSITION");
    AccessorReader positions{model, position->second, TINYGLTF_TYPE_VEC3};
    positions.require_float();
    if (positions.count() == 0 || positions.count() > std::numeric_limits<std::uint32_t>::max())
        fail("invalid glTF vertex count");
    std::vector<Vertex> vertices(positions.count());
    for (std::size_t i = 0; i < vertices.size(); ++i) {
        vertices[i].position = positions.vec3(i);
        vertices[i].uv = {0, 0};
    }
    bool has_normals = false;
    for (auto const &[name, index] : primitive.attributes) {
        if (name == "POSITION")
            continue;
        int type{};
        if (name == "NORMAL")
            type = TINYGLTF_TYPE_VEC3;
        else if (name == "TANGENT")
            type = TINYGLTF_TYPE_VEC4;
        else if (name == "TEXCOORD_0")
            type = TINYGLTF_TYPE_VEC2;
        else if (name == "COLOR_0")
            type = element(model.accessors, index).type;
        else
            fail("unsupported glTF vertex attribute: {}", name);
        if (name == "COLOR_0" && type != TINYGLTF_TYPE_VEC3 && type != TINYGLTF_TYPE_VEC4)
            fail("invalid glTF vertex color shape");
        AccessorReader values{model, index, type};
        if (values.count() != vertices.size())
            fail("glTF vertex attribute count mismatch");
        if (name == "NORMAL" || name == "TANGENT")
            values.require_float();
        else if (values.component_type() != TINYGLTF_COMPONENT_TYPE_FLOAT &&
                 !((values.component_type() == TINYGLTF_COMPONENT_TYPE_UNSIGNED_BYTE ||
                    values.component_type() == TINYGLTF_COMPONENT_TYPE_UNSIGNED_SHORT) &&
                   values.normalized()))
            fail("invalid glTF UV/color component encoding");
        for (std::size_t i = 0; i < vertices.size(); ++i) {
            if (name == "NORMAL")
                vertices[i].normal = values.vec3(i);
            else if (name == "TANGENT") {
                auto const sign = values.read(i, 3);
                if (sign != -1 && sign != 1)
                    fail("glTF tangent handedness must be -1 or 1");
                vertices[i].tangent = {values.vec3(i), sign};
            }
            else if (name == "TEXCOORD_0")
                vertices[i].uv = {values.read(i, 0), values.read(i, 1)};
            else {
                vertices[i].color = values.vec3(i);
                if (type == TINYGLTF_TYPE_VEC4 && values.read(i, 3) != 1)
                    fail("glTF vertex alpha is unsupported");
            }
        }
        has_normals |= name == "NORMAL";
    }
    std::vector<std::uint32_t> indices;
    if (primitive.indices >= 0) {
        AccessorReader values{model, primitive.indices, TINYGLTF_TYPE_SCALAR};
        if (values.normalized() ||
            (values.component_type() != TINYGLTF_COMPONENT_TYPE_UNSIGNED_BYTE &&
             values.component_type() != TINYGLTF_COMPONENT_TYPE_UNSIGNED_SHORT &&
             values.component_type() != TINYGLTF_COMPONENT_TYPE_UNSIGNED_INT))
            fail("invalid glTF index encoding");
        indices.reserve(values.count());
        for (std::size_t i = 0; i < values.count(); ++i) {
            auto const index = static_cast<std::uint32_t>(values.read(i));
            if (index >= vertices.size())
                fail("glTF index exceeds vertex count");
            indices.push_back(index);
        }
    }
    else {
        indices.resize(vertices.size());
        std::iota(indices.begin(), indices.end(), 0U);
    }
    if (indices.empty() || indices.size() % 3 != 0)
        fail("glTF primitive requires complete triangles");
    if (!has_normals) {
        std::vector<Vertex> flat;
        flat.reserve(indices.size());
        for (std::size_t i = 0; i < indices.size(); i += 3) {
            auto const normal =
                glm::cross(vertices[indices[i + 1]].position - vertices[indices[i]].position,
                           vertices[indices[i + 2]].position - vertices[indices[i]].position);
            if (glm::length(normal) == 0 || !std::isfinite(glm::length(normal)))
                fail("cannot generate glTF normals for a degenerate triangle");
            for (std::size_t corner = 0; corner < 3; ++corner) {
                auto vertex = vertices[indices[i + corner]];
                vertex.normal = glm::normalize(normal);
                flat.push_back(vertex);
            }
        }
        vertices = std::move(flat);
        std::iota(indices.begin(), indices.end(), 0U);
    }
    return {std::move(vertices), std::move(indices)};
}
} // namespace

Model Model::load_from_gltf(std::filesystem::path const &path)
{
    auto const extension = path.extension();
    if (extension != ".gltf" && extension != ".glb")
        fail("expected a .gltf or .glb model");
    tinygltf::Model source;
    tinygltf::TinyGLTF loader;
    std::string error, warning;
    auto const utf8 = path.u8string();
    std::string const filename{utf8.begin(), utf8.end()};
    bool const loaded = extension == ".glb"
                            ? loader.LoadBinaryFromFile(&source, &error, &warning, filename)
                            : loader.LoadASCIIFromFile(&source, &error, &warning, filename);
    if (!warning.empty())
        spdlog::warn("[gltf] {}", warning);
    if (!loaded)
        fail("failed to load glTF model {}: {}", filename, error);
    if (!source.extensionsRequired.empty())
        fail("required glTF extensions are unsupported");
    if (!source.materials.empty())
        spdlog::warn(
            "[gltf] Material bindings are not imported; callers must provide draw materials: {}",
            filename);
    if (!source.skins.empty())
        fail("glTF skinning is not implemented");

    std::vector<Mesh> meshes;
    std::vector<std::vector<std::size_t>> mesh_indices(source.meshes.size());
    for (std::size_t i = 0; i < source.meshes.size(); ++i) {
        auto const &mesh = source.meshes[i];
        if (!mesh.weights.empty())
            fail("glTF morph weights are unsupported");
        for (auto const &primitive : mesh.primitives) {
            mesh_indices[i].push_back(meshes.size());
            meshes.push_back(import_primitive(source, primitive));
        }
    }
    // Keep a common identity root outside authored hierarchy and animation targets.
    std::vector<ModelNode> nodes{{.name = "Import root"}};
    std::vector<std::optional<std::size_t>> mapping(source.nodes.size());
    std::vector<int> roots;
    if (!source.scenes.empty()) {
        auto const scene_index = source.defaultScene < 0 ? 0 : source.defaultScene;
        roots = element(source.scenes, scene_index).nodes;
    }
    else {
        std::vector<bool> children(source.nodes.size());
        for (auto const &node : source.nodes)
            for (auto const child : node.children) {
                element(source.nodes, child);
                children[static_cast<std::size_t>(child)] = true;
            }
        for (std::size_t i = 0; i < children.size(); ++i)
            if (!children[i])
                roots.push_back(static_cast<int>(i));
    }
    std::vector<std::pair<int, std::size_t>> pending;
    for (auto it = roots.rbegin(); it != roots.rend(); ++it)
        pending.emplace_back(*it, 0);
    while (!pending.empty()) {
        auto const [index, parent] = pending.back();
        pending.pop_back();
        auto const &node = element(source.nodes, index);
        if (mapping[static_cast<std::size_t>(index)])
            fail("glTF nodes contain a cycle or multiple parents");
        if (node.skin >= 0 || !node.weights.empty())
            fail("glTF skinning/morph weights are unsupported");
        auto const imported = nodes.size();
        mapping[static_cast<std::size_t>(index)] = imported;
        nodes.push_back({.name = node.name, .parent = parent, .transform = node_transform(node)});
        if (node.mesh >= 0) {
            auto const &primitives = element(mesh_indices, node.mesh);
            if (primitives.size() == 1)
                nodes[imported].mesh = primitives.front();
            else
                for (auto const mesh : primitives)
                    nodes.push_back(
                        {.name = node.name + "/primitive", .parent = imported, .mesh = mesh});
        }
        for (auto it = node.children.rbegin(); it != node.children.rend(); ++it)
            pending.emplace_back(*it, imported);
    }
    if (source.scenes.empty() && std::ranges::any_of(mapping, [](auto const &v) { return !v; }))
        fail("glTF hierarchy contains an unreachable cycle");

    std::vector<AnimationClip> animations;
    for (auto const &animation : source.animations) {
        AnimationClip clip{.name = animation.name, .channels = {}};
        for (auto const &channel : animation.channels) {
            auto const &target = element(source.nodes, channel.target_node);
            auto const mapped = mapping[static_cast<std::size_t>(channel.target_node)];
            if (!mapped)
                continue;
            if (!target.matrix.empty())
                fail("glTF animation cannot target a matrix node");
            auto const &sampler = element(animation.samplers, channel.sampler);
            AnimationChannel imported{
                .node = *mapped, .times = {}, .values = std::vector<glm::vec3>{}};
            if (sampler.interpolation == "STEP")
                imported.interpolation = AnimationChannel::Interpolation::Step;
            else if (sampler.interpolation != "LINEAR")
                fail("unsupported glTF animation interpolation");
            if (channel.target_path == "translation")
                imported.path = AnimationChannel::Path::Translation;
            else if (channel.target_path == "rotation")
                imported.path = AnimationChannel::Path::Rotation;
            else if (channel.target_path == "scale")
                imported.path = AnimationChannel::Path::Scale;
            else
                fail("unsupported glTF animation target: {}", channel.target_path);
            AccessorReader times{source, sampler.input, TINYGLTF_TYPE_SCALAR};
            times.require_float();
            bool const rotation = imported.path == AnimationChannel::Path::Rotation;
            AccessorReader values{source, sampler.output,
                                  rotation ? TINYGLTF_TYPE_VEC4 : TINYGLTF_TYPE_VEC3};
            values.require_float();
            if (times.count() != values.count())
                fail("glTF animation key count mismatch");
            if (rotation)
                imported.values = std::vector<glm::quat>{};
            for (std::size_t i = 0; i < times.count(); ++i) {
                imported.times.push_back(static_cast<float>(times.read(i)));
                if (rotation)
                    std::get<std::vector<glm::quat>>(imported.values)
                        .emplace_back(values.read(i, 3), values.read(i, 0), values.read(i, 1),
                                      values.read(i, 2));
                else
                    std::get<std::vector<glm::vec3>>(imported.values).push_back(values.vec3(i));
            }
            clip.channels.push_back(std::move(imported));
        }
        if (!clip.channels.empty())
            animations.push_back(std::move(clip));
    }
    return Model{std::move(meshes), std::move(nodes), std::move(animations)};
}
} // namespace lc1::scene
