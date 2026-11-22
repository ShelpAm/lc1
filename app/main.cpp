#include "application.hpp"

#include "lc1/error.hpp"

#include <spdlog/spdlog.h>

#include <csignal>
#include <cstdio>
#include <cstdlib>
#include <exception>
#include <print>

namespace {

// Signal handlers only set a flag; GLFW calls are not async-signal-safe.
std::sig_atomic_t volatile interrupted = 0;

void interrupt_handler(int /*unused*/)
{
    interrupted = 1;
}

} // namespace

int main(int argc, char **argv)
{
    try {
        spdlog::set_pattern("[%^%l%$] %v");
        spdlog::set_level(spdlog::level::info);
        std::signal(SIGINT, interrupt_handler);
        std::signal(SIGTERM, interrupt_handler);

        lc1::app::Application application{argc, argv};
        application.run([] { return interrupted != 0; });
    }
    catch (lc1::Error const &error) {
        std::println(stderr, "[lc1] fatal: {}", error.diagnostic());
        // Keep the original throw site visible in Debug builds.
#ifndef NDEBUG
        throw;
#endif
        return EXIT_FAILURE;
    }
    // Vulkan exceptions already name the failing call and VkResult.
    catch (std::exception const &error) {
        std::println(stderr, "[lc1] fatal: {}", error.what());
#ifndef NDEBUG
        throw;
#endif
        return EXIT_FAILURE;
    }

    return EXIT_SUCCESS;
}
