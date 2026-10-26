#include "lc1/scene/model.hpp"
#include "lc1/error.hpp"
#include "lc1/tangent-space.hpp"
#include "lc1/vk/resources/gpu-image.hpp"
#include <algorithm>
#include <cmath>
#include <ktx.h>
#include <ktxvulkan.h>
#include <ranges>
#include <tiny_gltf.h>
#include <tiny_obj_loader.h>
#include <unordered_map>
#include <utility>

namespace lc1::scene {

namespace {
struct KtxTextureDeleter {
    void operator()(ktxTexture2 *texture) const noexcept;
};

void KtxTextureDeleter::operator()(ktxTexture2 *texture) const noexcept
{
    ktxTexture2_Destroy(texture);
}
} // namespace

Model Model::load_from_file(std::filesystem::path const &path, Transform correction)
{
    auto extension = path.extension();
    if (extension == ".obj")
        return load_from_obj(path, correction);
    if (extension == ".gltf" || extension == ".glb")
        return load_from_gltf(path, correction);
    fail("unsupported model file extension: {}", path.extension().string());
}

Model Model::load_from_obj(std::filesystem::path const &path, Transform correction)
{
    tinyobj::attrib_t attrib;
    std::vector<tinyobj::shape_t> shapes;
    std::vector<tinyobj::material_t> materials;
    std::string warn;
    std::string err;

    if (!tinyobj::LoadObj(&attrib, &shapes, &materials, &warn, &err, path.string().c_str())) {
        throw std::runtime_error(warn + err);
    }

    Model model;
    std::unordered_map<Vertex, std::uint32_t> index_of_vertex;

    auto position_of = [&](tinyobj::index_t const &index) {
        return glm::vec3{attrib.vertices[3 * index.vertex_index],
                         attrib.vertices[3 * index.vertex_index + 1],
                         attrib.vertices[3 * index.vertex_index + 2]};
    };
    // LoadObj triangulates by default. Missing normals use flat triangle normals;
    // including normals in Vertex equality preserves authored hard edges.
    for (auto const &shape : shapes) {
        for (std::size_t i = 0; i + 2 < shape.mesh.indices.size(); i += 3) {
            auto const face_normal = glm::cross(
                position_of(shape.mesh.indices[i + 1]) - position_of(shape.mesh.indices[i]),
                position_of(shape.mesh.indices[i + 2]) - position_of(shape.mesh.indices[i]));
            float const face_length = glm::length(face_normal);
            for (std::size_t corner = 0; corner < 3; ++corner) {
                auto const &index = shape.mesh.indices[i + corner];
                Vertex vertex{.position = position_of(index), .uv = {0.0F, 0.0F}};
                if (index.texcoord_index >= 0)
                    vertex.uv = {attrib.texcoords[2 * index.texcoord_index],
                                 attrib.texcoords[2 * index.texcoord_index + 1]};
                if (face_length > 0.0F)
                    vertex.normal = face_normal / face_length;
                if (index.normal_index >= 0) {
                    glm::vec3 const normal{attrib.normals[3 * index.normal_index],
                                           attrib.normals[3 * index.normal_index + 1],
                                           attrib.normals[3 * index.normal_index + 2]};
                    float const length = glm::length(normal);
                    if (length > 0.0F)
                        vertex.normal = normal / length;
                }
                auto [it, inserted] =
                    index_of_vertex.insert({vertex, model.meshes_.front().vertices_.size()});
                if (inserted)
                    model.meshes_.front().vertices_.push_back(vertex);
                model.meshes_.front().indices_.push_back(it->second);
            }
        }
    }

    model.apply_correction(correction);
    return model;
}

Model Model::load_from_gltf(std::filesystem::path const &path, Transform correction)
{
    auto extension = path.extension();
    if (extension != ".gltf" && extension != ".glb")
        fail("model extension not supported: {}. Expected .gltf or .glb",
             extension.generic_string());

    tinygltf::Model gltf_model;
    tinygltf::TinyGLTF loader;
    std::string err;
    std::string warn;
    // TinyGLTF takes UTF-8, while filesystem::path is wide on Windows.
    auto const utf8_path = path.u8string();
    std::string const filename{utf8_path.begin(), utf8_path.end()};
    bool ok{};
    if (extension == ".gltf") {
        ok = loader.LoadASCIIFromFile(&gltf_model, &err, &warn, filename);
    }
    if (extension == ".glb") {
        ok = loader.LoadBinaryFromFile(&gltf_model, &err, &warn, filename);
    }

    if (!warn.empty()) {
        spdlog::warn("[gltf] {}", warn);
    }
    if (!err.empty()) {
        spdlog::warn("[gltf] {}", err);
    }
    if (!ok) {
        fail("failed to load glTF/GLB model: {}", path.string());
    }

    Model model;
    for (auto [i, texture] : std::views::enumerate(gltf_model.textures)) {
        // NOLINTBEGIN
        auto const &image = gltf_model.images[texture.source];
        auto name = image.name.empty() ? std::format("texture-{}", i) : image.name;
        if (image.mimeType == "image/ktx2" && image.bufferView >= 0) {
            auto const &buffer_view = gltf_model.bufferViews[image.bufferView];
            auto const &buffer = gltf_model.buffers[buffer_view.buffer];

            std::uint8_t const *data = buffer.data.data() + buffer_view.byteOffset;
            auto size = buffer_view.byteLength;

            ktxTexture2 *raw_ktx_texture;

            ktxResult result = ktxTexture2_CreateFromMemory(
                data, size, KTX_TEXTURE_CREATE_LOAD_IMAGE_DATA_BIT, &raw_ktx_texture);
            if (result != KTX_SUCCESS) {
                fail("failed to load KTX2 texture: {}", ktxErrorString(result));
            }
            std::unique_ptr<ktxTexture2, KtxTextureDeleter> ktx_texture(raw_ktx_texture);

            if (ktx_texture->isCompressed && ktxTexture2_NeedsTranscoding(ktx_texture.get())) {
                result = ktxTexture2_TranscodeBasis(ktx_texture.get(), KTX_TTF_ASTC_4x4_RGBA, 0);
                if (result != KTX_SUCCESS) {
                    fail("failed to transcode KTX2 texture: {}", ktxErrorString(result));
                }
            }

            auto format = ktxTexture2_GetVkFormat(ktx_texture.get());
            vk::Extent3D extent{
                ktx_texture->baseWidth,
                ktx_texture->baseHeight,
                ktx_texture->baseDepth,
            };
            auto mip_levels = ktx_texture->numLevels;
        }
        // NOLINTEND
    }
    fail("glTF geometry and hierarchy import is not implemented: {}", path.string());
}

Model::Model(std::vector<Vertex> vertices, std::vector<std::uint32_t> indices, Transform correction)
    : definition_{1, {{.mesh = 0}}}
{
    meshes_.emplace_back(std::move(vertices), std::move(indices));
    apply_correction(correction);
}

Model::Model(std::vector<Mesh> meshes, std::vector<ModelNode> nodes,
             std::vector<AnimationClip> animations)
    : meshes_(std::move(meshes)),
      definition_{meshes_.size(), std::move(nodes), std::move(animations)}
{
    apply_correction({});
}

std::size_t Model::mesh_count() const
{
    return meshes_.size();
}

ModelDefinition const &Model::definition() const &
{
    return definition_;
}

ModelDefinition Model::definition() &&
{
    return std::move(definition_);
}

Transform const &Model::correction() const
{
    return correction_;
}

std::span<Vertex const> Model::vertices(std::size_t mesh_index) const
{
    return mesh_at(mesh_index).vertices_;
}

std::span<std::uint32_t const> Model::indices(std::size_t mesh_index) const
{
    return mesh_at(mesh_index).indices_;
}

void Model::generate_tangents()
{
    for (auto &mesh : meshes_)
        lc1::generate_tangents(mesh.vertices_, mesh.indices_);
}

GpuMesh Model::gen_mesh(Device const &device, std::size_t mesh_index) const
{
    auto const &mesh = mesh_at(mesh_index);
    return {device, mesh.vertices_, mesh.indices_};
}

Model::Model() : definition_{1, {{.mesh = 0}}}
{
    meshes_.push_back(Mesh{});
}

Mesh const &Model::mesh_at(std::size_t index) const
{
    if (index >= meshes_.size())
        fail("model mesh index is out of range");
    return meshes_[index];
}

void Model::apply_correction(Transform const &correction)
{
    for (auto &mesh : meshes_)
        apply_mesh_correction(mesh, correction);
    correction_ = correction;
}

void Model::apply_mesh_correction(Mesh &mesh, Transform const &correction)
{
    auto finite = [](glm::vec3 value) {
        return std::isfinite(value.x) && std::isfinite(value.y) && std::isfinite(value.z);
    };
    if (!finite(correction.position) || !finite(correction.rotation) || !finite(correction.scale) ||
        correction.scale.x == 0.0F || correction.scale.y == 0.0F || correction.scale.z == 0.0F)
        fail("invalid model correction transform");
    if (mesh.indices_.size() % 3 != 0)
        fail("model indices must contain complete triangles");
    for (auto const index : mesh.indices_)
        if (index >= mesh.vertices_.size())
            fail("model index is outside the vertex array");

    auto const matrix = correction.model_matrix();
    auto const linear = glm::mat3{matrix};
    float const determinant = glm::determinant(linear);
    if (!std::isfinite(determinant) || determinant == 0.0F)
        fail("model correction must have an invertible finite transform");
    auto const normal_matrix = glm::transpose(glm::inverse(linear));
    for (auto &vertex : mesh.vertices_) {
        bool const has_tangent = vertex.tangent.w != 0.0F;
        if (has_tangent && !valid_tangent_frame(vertex))
            fail("model correction requires a valid authored tangent frame");
        vertex.position = glm::vec3{matrix * glm::vec4{vertex.position, 1.0F}};
        auto const normal = normal_matrix * vertex.normal;
        float const length = glm::length(normal);
        if (!finite(vertex.position) || !finite(normal) || !std::isfinite(length) || length == 0.0F)
            fail("model correction produced an invalid vertex");
        vertex.normal = normal / length;
        if (has_tangent) {
            auto const transformed = linear * glm::vec3{vertex.tangent};
            auto const tangent = transformed - vertex.normal * glm::dot(vertex.normal, transformed);
            float const tangent_length = glm::length(tangent);
            if (!finite(tangent) || !std::isfinite(tangent_length) || tangent_length == 0)
                fail("model correction produced an invalid tangent");
            vertex.tangent = glm::vec4{tangent / tangent_length,
                                       vertex.tangent.w * (determinant < 0 ? -1.0F : 1.0F)};
        }
    }
    // A baked reflection no longer appears in the per-draw determinant.
    if (determinant < 0.0F)
        for (std::size_t i = 0; i < mesh.indices_.size(); i += 3)
            std::swap(mesh.indices_[i + 1], mesh.indices_[i + 2]);
}

} // namespace lc1::scene
