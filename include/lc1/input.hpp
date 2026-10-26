#pragma once

#include <glm/glm.hpp>

#include <array>
#include <cstddef>
#include <cstdint>
#include <span>
#include <vector>

// Input, the second non-Vulkan module after window.*: it includes no Vulkan header and,
// more importantly, no GLFW header either. window.* is the only place that knows a
// GLFW_KEY_* exists, which is the same split that keeps vk::Extent2D out of window.hpp
// and produced lc1::FrameExtent.
//
// Three layers, and keeping them apart is the whole design:
//   InputState  -- which switches are down, this frame. No meaning, no game rules.
//   BasicBinding -- one row of data saying "in this mode, this key means that action".
//   BasicInputRouter -- resolves the active mode's bindings into action state.
// Game code asks "is MoveForward held", never "is W down", so rebinding and a second
// input mode cost nothing at the call sites.
namespace lc1 {

// Every key, mouse button and modifier an action can be bound to.
//
// Mouse buttons share the enum deliberately: an action does not care which device
// triggered it, and one vocabulary keeps Binding a flat row instead of a tagged union.
// Unreal's FKey makes the same call (EKeys::LeftMouseButton is an FKey).
//
// The order is not load-bearing -- src/window.cpp's translation table names each Key
// explicitly and is checked by static_assert -- with one exception: the mouse buttons
// stay last, because that table stops at the first of them.
enum class Key : std::uint8_t {
    A,
    B,
    C,
    D,
    E,
    F,
    G,
    H,
    I,
    J,
    K,
    L,
    M,
    N,
    O,
    P,
    Q,
    R,
    S,
    T,
    U,
    V,
    W,
    X,
    Y,
    Z,

    Digit0,
    Digit1,
    Digit2,
    Digit3,
    Digit4,
    Digit5,
    Digit6,
    Digit7,
    Digit8,
    Digit9,

    F1,
    F2,
    F3,
    F4,
    F5,
    F6,
    F7,
    F8,
    F9,
    F10,
    F11,
    F12,

    Left,
    Right,
    Up,
    Down,

    Space,
    Enter,
    Escape,
    Tab,
    Backspace,
    Delete,
    Insert,
    Home,
    End,
    PageUp,
    PageDown,

    LeftShift,
    RightShift,
    LeftControl,
    RightControl,
    LeftAlt,
    RightAlt,
    LeftSuper,
    RightSuper,

    Minus,
    Equal,
    LeftBracket,
    RightBracket,
    Backslash,
    Semicolon,
    Apostrophe,
    Comma,
    Period,
    Slash,
    Grave,

    Keypad0,
    Keypad1,
    Keypad2,
    Keypad3,
    Keypad4,
    Keypad5,
    Keypad6,
    Keypad7,
    Keypad8,
    Keypad9,
    KeypadDecimal,
    KeypadDivide,
    KeypadMultiply,
    KeypadSubtract,
    KeypadAdd,
    KeypadEnter,
    NumLock,

    MouseLeft,
    MouseRight,
    MouseMiddle,
    MouseX1,
    MouseX2,

    Count,
};

constexpr std::size_t key_count = static_cast<std::size_t>(Key::Count);

// Everything before MouseLeft is a keyboard key. src/window.cpp's table has exactly this
// many rows, and the static_asserts beside it turn "forgot one" and "listed one twice"
// into compile errors rather than a key that silently never fires.
constexpr std::size_t keyboard_key_count = static_cast<std::size_t>(Key::MouseLeft);

// The state of the physical devices during the frame the application is in.
//
// Two vocabularies, and the difference matters:
//   down()     -- the switch is held right now
//   pressed()  -- it went down since the last frame boundary
//   released() -- it came up since the last frame boundary
//
// Window::poll_events() calls begin_frame() and then pumps GLFW, so an edge always means
// "happened during this frame's polling" and is never consumed twice. That is also why
// begin_frame() is not the application's job: there is no way to forget it and no way to
// put it in the wrong place.
//
// Cursor coordinates are GLFW screen coordinates -- logical points, not framebuffer
// pixels. On a display with a scale factor the two differ, and nothing here converts,
// because the conversion needs the framebuffer size and only the consumer has it. When a
// cursor position has to become a pixel or a world ray, multiply by
// glfwGetWindowContentScale (exposed on the window, not here).
class InputState {
  public:
    bool down(Key key) const;

    bool pressed(Key key) const;

    bool released(Key key) const;

    glm::vec2 cursor() const;

    // Movement accumulated since the last frame boundary. Zero when the pointer did not
    // move, and also zero on the frame that capture or focus changed -- see
    // invalidate_cursor().
    glm::vec2 cursor_delta() const;

    glm::vec2 scroll() const;

    // The frame boundary. Called by Window::poll_events() before pumping GLFW, and by
    // nothing else.
    void begin_frame();

    // Fed by Window's GLFW callbacks. Public only because the alternative -- befriending
    // Window -- would couple this header to it for no gain. Treat them as private.
    void on_key(Key key, bool is_down);
    void on_cursor_position(glm::vec2 position);
    void on_scroll(glm::vec2 offset);

    // The next cursor position becomes a baseline instead of a delta, and the delta
    // accumulated so far is dropped. Without this, enabling capture or regaining focus
    // moves the view by however far the pointer travelled while it was not being tracked --
    // one huge jump on the frame it returns.
    void invalidate_cursor();

    // Alt-Tab must not leave a key stuck down. GLFW stops reporting keys to an unfocused
    // window, so "still held" is unknowable and the only safe answer is "none". Every
    // released key reports a release, so a drag cannot survive losing focus half-done.
    void release_all();

  private:
    static bool flag(std::array<bool, key_count> const &flags, Key key);

    std::array<bool, key_count> down_{};
    std::array<bool, key_count> pressed_{};
    std::array<bool, key_count> released_{};

    glm::vec2 cursor_{};
    glm::vec2 cursor_delta_{};
    glm::vec2 scroll_{};
    // False until the first position arrives, and false again after invalidate_cursor().
    bool cursor_valid_ = false;
};

// ActionEnum must use contiguous values starting at zero and end with Count.
// ContextEnum and all binding policy belong to the caller. Only the top context resolves.
template <typename ActionEnum, typename ContextEnum> struct BasicBinding {
    ContextEnum context;
    ActionEnum action;
    Key key;
};

template <typename ActionEnum, typename ContextEnum> class BasicInputRouter {
  public:
    using Binding = BasicBinding<ActionEnum, ContextEnum>;

    BasicInputRouter(std::span<Binding const> bindings, ContextEnum initial_context)
        : bindings_(bindings.begin(), bindings.end()), stack_{initial_context}
    {
    }

    void push(ContextEnum context) { stack_.push_back(context); }

    void pop()
    {
        if (stack_.size() > 1)
            stack_.pop_back();
    }

    ContextEnum active_context() const { return stack_.back(); }

    // Recomputes action state from scratch; repeated updates are idempotent.
    // Call after device events and context changes, before querying actions.
    void update(InputState const &input)
    {
        held_.fill(false);
        pressed_.fill(false);
        released_.fill(false);
        for (auto const &binding : bindings_) {
            if (binding.context != active_context())
                continue;
            auto const index = static_cast<std::size_t>(binding.action);
            if (index >= action_count)
                continue;
            held_[index] = held_[index] || input.down(binding.key);
            pressed_[index] = pressed_[index] || input.pressed(binding.key);
            released_[index] = released_[index] || input.released(binding.key);
        }
        // Releasing one binding does not end an action held by another binding.
        // A new press remains visible even while another binding is held.
        for (std::size_t index = 0; index < action_count; ++index)
            released_[index] = released_[index] && !held_[index];
    }

    bool held(ActionEnum action) const { return flag(held_, action); }

    bool pressed(ActionEnum action) const { return flag(pressed_, action); }

    bool released(ActionEnum action) const { return flag(released_, action); }

  private:
    static constexpr std::size_t action_count = static_cast<std::size_t>(ActionEnum::Count);

    static bool flag(std::array<bool, action_count> const &flags, ActionEnum action)
    {
        auto const index = static_cast<std::size_t>(action);
        return index < action_count && flags[index];
    }

    std::vector<Binding> bindings_;
    std::vector<ContextEnum> stack_;
    std::array<bool, action_count> held_{};
    std::array<bool, action_count> pressed_{};
    std::array<bool, action_count> released_{};
};

} // namespace lc1
