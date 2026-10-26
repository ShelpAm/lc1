#include "lc1/window.hpp"

#include <array>
#include <cstdio>
#include <optional>
#include <print>
#include <string>
#include <utility>

namespace lc1 {
namespace {

// Set before glfwInit so that a failure *inside* initialization is reported
// through it as well.
void glfw_error_callback(int code, char const *description)
{
    std::println(stderr, "[glfw] error {}: {}", code, description);
}

// Key -> GLFW key code, one row per keyboard Key. Listed explicitly rather than derived
// from either enum's numeric order, because GLFW's codes are not contiguous and this
// translation unit is the only place allowed to know they exist.
//
// The two static_asserts below turn both ways this table can rot into compile errors.
// Together with naming the Key on every row they make it a bijection -- row count, no code
// twice, no row for GLFW_KEY_UNKNOWN -- so a Key cannot silently lose its key and a GLFW
// code cannot silently answer for the wrong one.
constexpr std::array glfw_key_codes{
    std::pair{Key::A, GLFW_KEY_A},
    std::pair{Key::B, GLFW_KEY_B},
    std::pair{Key::C, GLFW_KEY_C},
    std::pair{Key::D, GLFW_KEY_D},
    std::pair{Key::E, GLFW_KEY_E},
    std::pair{Key::F, GLFW_KEY_F},
    std::pair{Key::G, GLFW_KEY_G},
    std::pair{Key::H, GLFW_KEY_H},
    std::pair{Key::I, GLFW_KEY_I},
    std::pair{Key::J, GLFW_KEY_J},
    std::pair{Key::K, GLFW_KEY_K},
    std::pair{Key::L, GLFW_KEY_L},
    std::pair{Key::M, GLFW_KEY_M},
    std::pair{Key::N, GLFW_KEY_N},
    std::pair{Key::O, GLFW_KEY_O},
    std::pair{Key::P, GLFW_KEY_P},
    std::pair{Key::Q, GLFW_KEY_Q},
    std::pair{Key::R, GLFW_KEY_R},
    std::pair{Key::S, GLFW_KEY_S},
    std::pair{Key::T, GLFW_KEY_T},
    std::pair{Key::U, GLFW_KEY_U},
    std::pair{Key::V, GLFW_KEY_V},
    std::pair{Key::W, GLFW_KEY_W},
    std::pair{Key::X, GLFW_KEY_X},
    std::pair{Key::Y, GLFW_KEY_Y},
    std::pair{Key::Z, GLFW_KEY_Z},

    std::pair{Key::Digit0, GLFW_KEY_0},
    std::pair{Key::Digit1, GLFW_KEY_1},
    std::pair{Key::Digit2, GLFW_KEY_2},
    std::pair{Key::Digit3, GLFW_KEY_3},
    std::pair{Key::Digit4, GLFW_KEY_4},
    std::pair{Key::Digit5, GLFW_KEY_5},
    std::pair{Key::Digit6, GLFW_KEY_6},
    std::pair{Key::Digit7, GLFW_KEY_7},
    std::pair{Key::Digit8, GLFW_KEY_8},
    std::pair{Key::Digit9, GLFW_KEY_9},

    std::pair{Key::F1, GLFW_KEY_F1},
    std::pair{Key::F2, GLFW_KEY_F2},
    std::pair{Key::F3, GLFW_KEY_F3},
    std::pair{Key::F4, GLFW_KEY_F4},
    std::pair{Key::F5, GLFW_KEY_F5},
    std::pair{Key::F6, GLFW_KEY_F6},
    std::pair{Key::F7, GLFW_KEY_F7},
    std::pair{Key::F8, GLFW_KEY_F8},
    std::pair{Key::F9, GLFW_KEY_F9},
    std::pair{Key::F10, GLFW_KEY_F10},
    std::pair{Key::F11, GLFW_KEY_F11},
    std::pair{Key::F12, GLFW_KEY_F12},

    std::pair{Key::Left, GLFW_KEY_LEFT},
    std::pair{Key::Right, GLFW_KEY_RIGHT},
    std::pair{Key::Up, GLFW_KEY_UP},
    std::pair{Key::Down, GLFW_KEY_DOWN},

    std::pair{Key::Space, GLFW_KEY_SPACE},
    std::pair{Key::Enter, GLFW_KEY_ENTER},
    std::pair{Key::Escape, GLFW_KEY_ESCAPE},
    std::pair{Key::Tab, GLFW_KEY_TAB},
    std::pair{Key::Backspace, GLFW_KEY_BACKSPACE},
    std::pair{Key::Delete, GLFW_KEY_DELETE},
    std::pair{Key::Insert, GLFW_KEY_INSERT},
    std::pair{Key::Home, GLFW_KEY_HOME},
    std::pair{Key::End, GLFW_KEY_END},
    std::pair{Key::PageUp, GLFW_KEY_PAGE_UP},
    std::pair{Key::PageDown, GLFW_KEY_PAGE_DOWN},

    std::pair{Key::LeftShift, GLFW_KEY_LEFT_SHIFT},
    std::pair{Key::RightShift, GLFW_KEY_RIGHT_SHIFT},
    std::pair{Key::LeftControl, GLFW_KEY_LEFT_CONTROL},
    std::pair{Key::RightControl, GLFW_KEY_RIGHT_CONTROL},
    std::pair{Key::LeftAlt, GLFW_KEY_LEFT_ALT},
    std::pair{Key::RightAlt, GLFW_KEY_RIGHT_ALT},
    std::pair{Key::LeftSuper, GLFW_KEY_LEFT_SUPER},
    std::pair{Key::RightSuper, GLFW_KEY_RIGHT_SUPER},

    std::pair{Key::Minus, GLFW_KEY_MINUS},
    std::pair{Key::Equal, GLFW_KEY_EQUAL},
    std::pair{Key::LeftBracket, GLFW_KEY_LEFT_BRACKET},
    std::pair{Key::RightBracket, GLFW_KEY_RIGHT_BRACKET},
    std::pair{Key::Backslash, GLFW_KEY_BACKSLASH},
    std::pair{Key::Semicolon, GLFW_KEY_SEMICOLON},
    std::pair{Key::Apostrophe, GLFW_KEY_APOSTROPHE},
    std::pair{Key::Comma, GLFW_KEY_COMMA},
    std::pair{Key::Period, GLFW_KEY_PERIOD},
    std::pair{Key::Slash, GLFW_KEY_SLASH},
    std::pair{Key::Grave, GLFW_KEY_GRAVE_ACCENT},

    std::pair{Key::Keypad0, GLFW_KEY_KP_0},
    std::pair{Key::Keypad1, GLFW_KEY_KP_1},
    std::pair{Key::Keypad2, GLFW_KEY_KP_2},
    std::pair{Key::Keypad3, GLFW_KEY_KP_3},
    std::pair{Key::Keypad4, GLFW_KEY_KP_4},
    std::pair{Key::Keypad5, GLFW_KEY_KP_5},
    std::pair{Key::Keypad6, GLFW_KEY_KP_6},
    std::pair{Key::Keypad7, GLFW_KEY_KP_7},
    std::pair{Key::Keypad8, GLFW_KEY_KP_8},
    std::pair{Key::Keypad9, GLFW_KEY_KP_9},
    std::pair{Key::KeypadDecimal, GLFW_KEY_KP_DECIMAL},
    std::pair{Key::KeypadDivide, GLFW_KEY_KP_DIVIDE},
    std::pair{Key::KeypadMultiply, GLFW_KEY_KP_MULTIPLY},
    std::pair{Key::KeypadSubtract, GLFW_KEY_KP_SUBTRACT},
    std::pair{Key::KeypadAdd, GLFW_KEY_KP_ADD},
    std::pair{Key::KeypadEnter, GLFW_KEY_KP_ENTER},
    std::pair{Key::NumLock, GLFW_KEY_NUM_LOCK},
};

// "Listed one twice" and "forgot one" together would keep the row count right, so the
// count alone is not enough. Also rejects a row for GLFW_KEY_UNKNOWN, which is the value
// GLFW passes for a key it has no code for: a row holding it would make every unrecognized
// key resolve to that row's Key.
constexpr bool key_codes_are_usable()
{
    for (std::size_t i = 0; i < glfw_key_codes.size(); ++i) {
        if (glfw_key_codes[i].second == GLFW_KEY_UNKNOWN)
            return false;
        for (std::size_t j = i + 1; j < glfw_key_codes.size(); ++j) {
            if (glfw_key_codes[i].second == glfw_key_codes[j].second)
                return false;
        }
    }
    return true;
}

static_assert(glfw_key_codes.size() == keyboard_key_count,
              "one row per keyboard Key; the rows stop where the mouse buttons start");
static_assert(key_codes_are_usable(),
              "a GLFW key code is either GLFW_KEY_UNKNOWN or claimed by two Keys");

std::optional<Key> key_from_glfw(int glfw_key)
{
    for (auto const &[key, code] : glfw_key_codes) {
        if (code == glfw_key)
            return key;
    }
    return std::nullopt;
}

// Mouse buttons are not GLFW key codes and occupy their own small range, so they get their
// own table rather than rows above, where two of them would look like unmapped keys.
constexpr std::array glfw_mouse_buttons{
    std::pair{Key::MouseLeft, GLFW_MOUSE_BUTTON_LEFT},
    std::pair{Key::MouseRight, GLFW_MOUSE_BUTTON_RIGHT},
    std::pair{Key::MouseMiddle, GLFW_MOUSE_BUTTON_MIDDLE},
    std::pair{Key::MouseX1, GLFW_MOUSE_BUTTON_4},
    std::pair{Key::MouseX2, GLFW_MOUSE_BUTTON_5},
};

std::optional<Key> mouse_key_from_glfw(int glfw_button)
{
    for (auto const &[key, button] : glfw_mouse_buttons) {
        if (button == glfw_button)
            return key;
    }
    return std::nullopt;
}

} // namespace

Window::Window(int width, int height, std::string_view title) : title_(title)
{
    glfwSetErrorCallback(glfw_error_callback);
    if (glfwInit() != GLFW_TRUE)
        fail("glfwInit failed");

    if (glfwVulkanSupported() != GLFW_TRUE)
        fail("GLFW reports no Vulkan support");

    // Must come after glfwInit and before glfwCreateWindow.
    glfwWindowHint(GLFW_CLIENT_API, GLFW_NO_API);

    handle_ = glfwCreateWindow(width, height, title_.c_str(), nullptr, nullptr);
    if (handle_ == nullptr)
        fail("glfwCreateWindow failed");

    // The user pointer goes on first: every callback reads it, so registering a callback
    // before it would leave a window whose callback finds no Window.
    glfwSetWindowUserPointer(handle_, this);
    glfwSetFramebufferSizeCallback(handle_, &Window::framebuffer_size_callback);
    glfwSetKeyCallback(handle_, &Window::key_callback);
    glfwSetMouseButtonCallback(handle_, &Window::mouse_button_callback);
    glfwSetCursorPosCallback(handle_, &Window::cursor_position_callback);
    glfwSetScrollCallback(handle_, &Window::scroll_callback);
    glfwSetCursorEnterCallback(handle_, &Window::cursor_enter_callback);
    glfwSetWindowFocusCallback(handle_, &Window::focus_callback);
}

Window::~Window()
{
    glfwDestroyWindow(handle_);
}

Window::GlfwLifetime::~GlfwLifetime()
{
    glfwTerminate();
}

Window *Window::owner_of(GLFWwindow *window)
{
    return static_cast<Window *>(glfwGetWindowUserPointer(window));
}

void Window::framebuffer_size_callback(GLFWwindow *window, int /*width*/, int /*height*/)
{
    // Only sets a flag. Recreation happens at the top of the frame, before
    // acquire -- never from inside the callback, where it would destroy the
    // swapchain out from under an image that may already be acquired.
    auto *self = owner_of(window);
    if (self == nullptr)
        return;
    self->framebuffer_resized_ = true;
}

void Window::key_callback(GLFWwindow *window, int key, int /*scancode*/, int action, int /*mods*/)
{
    // GLFW_REPEAT is not an edge. Passing it through would make a held key look like a
    // fresh press every few frames -- right for a text field, and wrong for every game
    // action, where a held W would stutter. A text field wants the repeat rate and a
    // context of its own, not a change here.
    if (action != GLFW_PRESS && action != GLFW_RELEASE)
        return;

    auto *self = owner_of(window);
    if (self == nullptr)
        return;

    // Unmapped keys are dropped rather than given a Key::Unknown: no action can be bound
    // to "some key GLFW knows and we do not", so there is nothing for it to mean.
    auto const mapped = key_from_glfw(key);
    if (!mapped)
        return;

    self->input_.on_key(*mapped, action == GLFW_PRESS);
}

void Window::mouse_button_callback(GLFWwindow *window, int button, int action, int /*mods*/)
{
    auto *self = owner_of(window);
    if (self == nullptr)
        return;

    auto const mapped = mouse_key_from_glfw(button);
    if (!mapped)
        return;

    self->input_.on_key(*mapped, action == GLFW_PRESS);
}

void Window::cursor_position_callback(GLFWwindow *window, double x, double y)
{
    auto *self = owner_of(window);
    if (self == nullptr)
        return;

    // Left as GLFW reports them -- screen coordinates, not framebuffer pixels. See
    // InputState: the conversion needs the framebuffer size, which only the consumer has.
    self->input_.on_cursor_position({static_cast<float>(x), static_cast<float>(y)});
}

void Window::scroll_callback(GLFWwindow *window, double x_offset, double y_offset)
{
    auto *self = owner_of(window);
    if (self == nullptr)
        return;

    self->input_.on_scroll({static_cast<float>(x_offset), static_cast<float>(y_offset)});
}

void Window::cursor_enter_callback(GLFWwindow *window, int /*entered*/)
{
    auto *self = owner_of(window);
    if (self == nullptr)
        return;

    // Either direction: leaving and coming back puts the pointer somewhere the tracker did
    // not follow it to, so the next position is a baseline. Does not fire at all while the
    // cursor is captured, which is why set_cursor_captured resets the baseline itself.
    self->input_.invalidate_cursor();
}

void Window::focus_callback(GLFWwindow *window, int focused)
{
    auto *self = owner_of(window);
    if (self == nullptr)
        return;

    if (focused == GLFW_FALSE) {
        // The window stops receiving key events while it is unfocused, so nothing will ever
        // report these keys coming back up. Dropping them now is the only reason W is not
        // still held after an Alt-Tab away and back.
        self->input_.release_all();
    }
    else {
        self->input_.invalidate_cursor();
    }
}

void Window::set_cursor_captured(bool captured)
{
    if (captured == cursor_captured_)
        return;
    cursor_captured_ = captured;

    if (captured) {
        // Cursor mode first: GLFW honours raw mouse motion only while the cursor is
        // disabled, and this is the call that removes the platform's pointer acceleration.
        glfwSetInputMode(handle_, GLFW_CURSOR, GLFW_CURSOR_DISABLED);
        if (glfwRawMouseMotionSupported() == GLFW_TRUE)
            glfwSetInputMode(handle_, GLFW_RAW_MOUSE_MOTION, GLFW_TRUE);
    }
    else {
        if (glfwRawMouseMotionSupported() == GLFW_TRUE)
            glfwSetInputMode(handle_, GLFW_RAW_MOUSE_MOTION, GLFW_FALSE);
        glfwSetInputMode(handle_, GLFW_CURSOR, GLFW_CURSOR_NORMAL);
    }

    // Wherever the pointer reappears, it did not travel there as camera motion.
    input_.invalidate_cursor();
}

std::vector<char const *> Window::required_instance_extensions() const
{
    uint32_t count = 0;
    char const **const extensions = glfwGetRequiredInstanceExtensions(&count);
    if (extensions == nullptr) {
        fail("glfwGetRequiredInstanceExtensions returned null "
             "(no Vulkan surface support for this platform)");
    }
    return {extensions, extensions + count};
}

bool Window::set_title(std::string_view title)
{
    if (title == title_)
        return false;
    title_ = title;
    glfwSetWindowTitle(handle_, title_.c_str());
    return true;
}

bool Window::should_close() const
{
    return glfwWindowShouldClose(handle_) != GLFW_FALSE;
}

bool Window::take_resize_event()
{
    bool const resized = framebuffer_resized_;
    framebuffer_resized_ = false;
    return resized;
}

void Window::poll_events()
{
    // The input frame boundary: edges cleared first, then this frame's events pumped in, so
    // pressed()/released() describe exactly what happened since the previous call.
    input_.begin_frame();
    glfwPollEvents();
}

void Window::wait_events(double timeout_seconds)
{
    glfwWaitEventsTimeout(timeout_seconds);
}

glm::vec2 Window::content_size() const
{
    int width = 0;
    int height = 0;
    glfwGetWindowSize(handle_, &width, &height);
    return {static_cast<float>(width), static_cast<float>(height)};
}

bool Window::focused() const
{
    return glfwGetWindowAttrib(handle_, GLFW_FOCUSED) == GLFW_TRUE;
}

bool Window::hovered() const
{
    return glfwGetWindowAttrib(handle_, GLFW_HOVERED) == GLFW_TRUE;
}

FrameExtent Window::framebuffer_extent() const
{
    int width = 0;
    int height = 0;
    glfwGetFramebufferSize(handle_, &width, &height);

    if (width <= 0 || height <= 0)
        return FrameExtent{};

    return FrameExtent{.width = static_cast<uint32_t>(width),
                       .height = static_cast<uint32_t>(height)};
}

} // namespace lc1

namespace lc1 {

GLFWwindow *Window::handle() const
{
    return handle_;
}

InputState const &Window::input() const
{
    return input_;
}

bool Window::cursor_captured() const
{
    return cursor_captured_;
}

} // namespace lc1
