#include "lc1/platform/input.hpp"

#include <array>
#include <cstdlib>
#include <iostream>
#include <stdexcept>

namespace {
enum class TestAction { Move, Other, Count };
enum class TestContext { Play, Menu };
using Binding = lc1::BasicBinding<TestAction, TestContext>;
using Router = lc1::BasicInputRouter<TestAction, TestContext>;

void check(bool condition, char const *message)
{
    if (!condition)
        throw std::runtime_error(message);
}

void routing()
{
    std::array const bindings{Binding{TestContext::Play, TestAction::Move, lc1::Key::W},
                              Binding{TestContext::Play, TestAction::Move, lc1::Key::Up},
                              Binding{TestContext::Menu, TestAction::Other, lc1::Key::W}};
    Router router{bindings, TestContext::Play};
    lc1::InputState state;
    state.on_key(lc1::Key::W, true);
    router.update(state);
    router.update(state);
    check(router.held(TestAction::Move) && router.pressed(TestAction::Move),
          "repeated updates preserve the same press");
    state.begin_frame();
    state.on_key(lc1::Key::Up, true);
    router.update(state);
    check(router.pressed(TestAction::Move), "second binding still reports a new press");
    state.begin_frame();
    state.on_key(lc1::Key::W, false);
    router.update(state);
    check(router.held(TestAction::Move) && !router.released(TestAction::Move),
          "releasing one binding preserves the action held by another");
    state.begin_frame();
    state.on_key(lc1::Key::Up, false);
    router.update(state);
    check(!router.held(TestAction::Move) && router.released(TestAction::Move),
          "last release ends the action");
    state.begin_frame();
    state.on_key(lc1::Key::W, true);
    router.push(TestContext::Menu);
    router.update(state);
    check(!router.held(TestAction::Move) && router.held(TestAction::Other),
          "only top context resolves bindings");
    router.pop();
    router.pop();
    router.update(state);
    check(router.active_context() == TestContext::Play && router.held(TestAction::Move) &&
              !router.held(TestAction::Other),
          "pop restores base context without stale actions");
    state.begin_frame();
    state.on_key(lc1::Key::W, false);
    state.on_key(lc1::Key::Up, true);
    state.on_key(lc1::Key::Up, false);
    router.update(state);
    check(router.pressed(TestAction::Move) && router.released(TestAction::Move) &&
              !router.held(TestAction::Move),
          "press and release in one frame preserve both edges");
    check(!router.held(static_cast<TestAction>(999)), "invalid action reads are bounded");
}
} // namespace

int main()
{
    try {
        routing();
    }
    catch (std::exception const &error) {
        std::cerr << error.what() << '\n';
        return EXIT_FAILURE;
    }
    return EXIT_SUCCESS;
}
