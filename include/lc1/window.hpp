#pragma once

#include "lc1/error.hpp"
#include "lc1/input.hpp"

#include <GLFW/glfw3.h>

#include <cstdint>
#include <string>
#include <string_view>
#include <vector>

namespace lc1 {

// A framebuffer size in pixels. {0, 0} means "nothing to render to" -- the
// window is minimized or the surface has no usable extent.
//
// Not vk::Extent2D: this header must not depend on Vulkan.
struct FrameExtent {
    uint32_t width = 0;
    uint32_t height = 0;
};

// The GLFW window, and with it the GLFW library lifetime.
//
// One Window per process: glfwInit/glfwTerminate are process-wide state, so
// copies and moves are deleted. With no moved-from state, handle_ is never
// null in a constructed Window.
class Window {
  public:
    Window(int width, int height, std::string_view title);
    ~Window();

    Window(Window const &) = delete;
    Window &operator=(Window const &) = delete;
    Window(Window &&) = delete;
    Window &operator=(Window &&) = delete;

    // Throws instead of returning an empty vector: an Instance built without
    // VK_KHR_surface fails much later and with a much worse message.
    std::vector<char const *> required_instance_extensions() const;

    // Borrowed, for glfwCreateWindowSurface in Device.
    GLFWwindow *handle() const;

    bool should_close() const;
    // Returns true when the title changed and GLFW was called.
    bool set_title(std::string_view title);

    // Device state, valid for the frame poll_events() most recently opened. Always wired:
    // the callbacks are registered in the constructor, so there is no state where this
    // exists but receives nothing.
    InputState const &input() const;

    // Captured means the cursor is hidden, held inside the window and reported as
    // unaccelerated raw motion when supported. UI interaction needs a visible
    // pointer, and which one is wanted is a decision only the application can
    // make, so this is an explicit call rather than something Window infers from focus.
    //
    // Re-enabling the cursor does not move it back: where it reappears is the platform's
    // choice, which is why the delta baseline is reset either way.
    void set_cursor_captured(bool captured);

    bool cursor_captured() const;

    // Reads and clears the flag the resize callback sets. The caller must
    // consume it unconditionally: `recreate_requested ||
    // window.take_resize_event()` short-circuits, leaves the flag set, and
    // forces a second rebuild on the next frame.
    bool take_resize_event();

    // Not const: dispatching events runs the callbacks, which write into input_ and
    // framebuffer_resized_.
    //
    // This is the input frame boundary: it clears the edge flags and then pumps GLFW, so
    // "pressed" describes exactly the events since the previous call. It lives here rather
    // than in the application because a boundary the caller has to remember is a boundary
    // that will eventually be in the wrong place.
    void poll_events();
    // Bounded so the application can also check signal-based shutdown.
    // timeout_seconds must be positive and finite.
    //
    // Deliberately not a frame boundary: the application calls this only while it has
    // nothing to draw and reads no input, so anything it pumps is dropped by the next
    // poll_events rather than leaking into a later frame.
    void wait_events(double timeout_seconds);

    // {0, 0} when the window is minimized or the framebuffer is unusable.
    FrameExtent framebuffer_extent() const;

    // GLFW cursor positions use logical window coordinates, not framebuffer pixels.
    glm::vec2 window_size() const;
    bool focused() const;
    bool hovered() const;

  private:
    // Every callback starts here. The user pointer is the only channel GLFW gives a C
    // callback back to the Window that owns the callback, and it is set before any
    // callback is registered.
    static Window *owner_of(GLFWwindow *window);

    static void framebuffer_size_callback(GLFWwindow *window, int width, int height);
    static void key_callback(GLFWwindow *window, int key, int scancode, int action, int mods);
    static void mouse_button_callback(GLFWwindow *window, int button, int action, int mods);
    static void cursor_position_callback(GLFWwindow *window, double x, double y);
    static void scroll_callback(GLFWwindow *window, double x_offset, double y_offset);
    static void cursor_enter_callback(GLFWwindow *window, int entered);
    static void focus_callback(GLFWwindow *window, int focused);

    struct GlfwLifetime {
        ~GlfwLifetime();
    };

    // Declared first so it is destroyed last, after ~Window's
    // glfwDestroyWindow. A member rather than a call in ~Window so that it
    // also runs when the constructor throws; glfwTerminate is a no-op when
    // GLFW is not initialized.
    GlfwLifetime glfw_lifetime_;

    // Owning: GLFW has no raii form, so ~Window is the single destroy site.
    GLFWwindow *handle_ = nullptr;
    std::string title_;
    bool framebuffer_resized_ = false;

    // Window owns the device state because it owns the callbacks that fill it. It does not
    // interpret any of it: what a key means, and therefore whether ESC closes the window,
    // is application policy, and a hardcoded ESC-to-close would make ESC unusable as the
    // pause/back key every strategy game needs.
    InputState input_;
    bool cursor_captured_ = false;
};

} // namespace lc1
