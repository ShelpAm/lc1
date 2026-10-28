#include "lc1/scene/model.hpp"
#include "lc1/scene/model-instance.hpp"

#include <iostream>
#include <limits>
#include <stdexcept>
#include <type_traits>

static_assert(!std::is_constructible_v<lc1::scene::ModelInstance, lc1::scene::ModelDefinition &&>);
static_assert(
    !std::is_constructible_v<lc1::scene::ModelInstance, lc1::scene::ModelDefinition const &&>);

namespace {
void check(bool condition, char const *message)
{
    if (!condition)
        throw std::runtime_error(message);
}

template <typename F> void rejects(F &&action)
{
    try {
        action();
    }
    catch (lc1::Error const &) {
        return;
    }
    throw std::runtime_error("expected model validation failure");
}

bool near(glm::vec3 a, glm::vec3 b)
{
    return glm::length(a - b) < 1e-5F;
}

void hierarchy_and_instances()
{
    lc1::scene::Model const model{
        std::vector<lc1::scene::Mesh>{lc1::scene::Mesh{{}, {}}},
        {{.name = "body", .transform = {.translation = {1, 0, 0}}, .mesh = 0},
         {.name = "hand", .parent = 0, .transform = {.translation = {0, 2, 0}}, .mesh = 0},
         {.name = "attachment", .parent = 1, .transform = {.translation = {0, 0, 3}}},
         {.name = "other root", .transform = {.translation = {-1, 0, 0}}}}};
    lc1::scene::ModelInstance first{model.definition()};
    lc1::scene::ModelInstance second{model.definition()};
    auto matrices = first.model_matrices();
    check(near(glm::vec3{matrices[2][3]}, {1, 2, 3}), "three-level translation composition");
    check(near(glm::vec3{matrices[3][3]}, {-1, 0, 0}), "multiple independent roots");
    first.set_local_transform(
        0,
        {.rotation = glm::angleAxis(glm::radians(90.0F), glm::vec3{0, 0, 1}), .scale = {2, 2, 2}});
    matrices = first.model_matrices();
    check(near(glm::vec3{matrices[2][3]}, {-4, 0, 6}),
          "parent rotation and scale affect descendants");
    check(near(glm::vec3{second.model_matrices()[2][3]}, {1, 2, 3}),
          "instances have independent poses");
    check(near(model.definition().nodes()[0].transform.translation, {1, 0, 0}),
          "shared default pose is unchanged");
    auto const world = glm::translate(glm::mat4{1}, glm::vec3{10, 0, 0});
    check(near(glm::vec3{(world * matrices[2])[3]}, {6, 0, 6}),
          "external world transform is composed once");
    first.reset_pose();
    check(near(glm::vec3{first.model_matrices()[2][3]}, {1, 2, 3}),
          "reset restores the default pose");
    rejects([&] { first.set_local_transform(4, {}); });
    rejects([&] { first.set_local_transform(0, {.scale = {0, 1, 1}}); });
    check(near(glm::vec3{first.model_matrices()[2][3]}, {1, 2, 3}), "failed edit preserves pose");
}

void invalid_models()
{
    rejects([] { lc1::scene::Model model{std::vector<lc1::scene::Mesh>{}, {{.parent = 0}}}; });
    rejects([] {
        lc1::scene::Model model{std::vector<lc1::scene::Mesh>{}, {{.parent = 1}, {.parent = 0}}};
    });
    rejects([] { lc1::scene::Model model{std::vector<lc1::scene::Mesh>{}, {{.mesh = 0}}}; });
    rejects([] {
        lc1::scene::Model model{std::vector<lc1::scene::Mesh>{},
                                {{.transform = {.rotation = {0, 0, 0, 0}}}}};
    });
    rejects([] {
        lc1::scene::Model model{
            std::vector<lc1::scene::Mesh>{},
            {{.transform = {.translation = {std::numeric_limits<float>::infinity(), 0, 0}}}}};
    });
    rejects([] {
        lc1::scene::Model model{std::vector<lc1::scene::Mesh>{lc1::scene::Mesh{{}, {0, 1, 2}}},
                                {{.mesh = 0}}};
    });
    lc1::scene::Model const empty{std::vector<lc1::scene::Mesh>{},
                                  std::vector<lc1::scene::ModelNode>{}};
    check(lc1::scene::ModelInstance{empty.definition()}.model_matrices().empty(),
          "empty model has an empty pose");
    rejects([&] { empty.vertices(); });
}

void shared_meshes()
{
    std::vector<lc1::scene::Mesh> meshes;
    meshes.emplace_back(std::vector<lc1::Vertex>{{.position = {1, 0, 0}, .uv = {0, 0}}},
                        std::vector<std::uint32_t>{});
    meshes.emplace_back(std::vector<lc1::Vertex>{{.position = {2, 0, 0}, .uv = {0, 0}}},
                        std::vector<std::uint32_t>{});
    lc1::scene::Model const model{std::move(meshes), {{.mesh = 1}, {.mesh = 0}, {.mesh = 1}}};
    check(model.mesh_count() == 2 &&
              model.definition().nodes()[0].mesh == model.definition().nodes()[2].mesh,
          "multiple nodes can reference one mesh without duplicating its geometry");
    check(near(model.vertices(0)[0].position, {1, 0, 0}) &&
              near(model.vertices(1)[0].position, {2, 0, 0}),
          "mesh index selects the correct geometry");
    rejects([&] { model.indices(2); });
}

void single_mesh_compatibility()
{
    lc1::scene::Model const model{std::vector<lc1::Vertex>{{.position = {1, 0, 0}, .uv = {0, 0}}},
                                  std::vector<std::uint32_t>{}};
    check(model.mesh_count() == 1 && model.definition().nodes().size() == 1 &&
              model.definition().nodes()[0].mesh == 0,
          "legacy geometry creates one mesh and an identity root");
    lc1::scene::ModelInstance const instance{model.definition()};
    auto const point = instance.model_matrices()[0] * glm::vec4{model.vertices()[0].position, 1};
    check(near(glm::vec3{point}, {1, 0, 0}),
          "single-mesh construction preserves geometry under its identity root");
}
} // namespace

int main()
{
    try {
        hierarchy_and_instances();
        invalid_models();
        single_mesh_compatibility();
        shared_meshes();
        std::cout << "Model tests passed\n";
    }
    catch (std::exception const &error) {
        std::cerr << error.what() << '\n';
        return 1;
    }
}
