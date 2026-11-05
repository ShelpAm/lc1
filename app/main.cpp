#include "application.hpp"

#include "lc1/error.hpp"

#include <spdlog/spdlog.h>

#include <charconv>
#include <csignal>
#include <cstdio>
#include <cstdlib>
#include <exception>
#include <print>
#include <string_view>

namespace {

// Signal handlers only set a flag; GLFW calls are not async-signal-safe.
std::sig_atomic_t volatile interrupted = 0;

void interrupt_handler(int /*unused*/)
{
    interrupted = 1;
}

} // namespace

int main()
{
    try {
        spdlog::set_pattern("[%^%l%$] %v");
        spdlog::set_level(spdlog::level::info);
        std::signal(SIGINT, interrupt_handler);
        std::signal(SIGTERM, interrupt_handler);

        lc1::app::ApplicationConfig config;
#ifdef NDEBUG
        config.enable_validation = false;
#endif
        if (std::getenv("LC1_NO_VALIDATION") != nullptr)
            config.enable_validation = false;
        if (auto const *asset_root = std::getenv("LC1_ASSET_ROOT"))
            config.asset_root = asset_root;
        if (auto const *save_root = std::getenv("LC1_SAVE_ROOT"))
            config.save_root = save_root;

        if (auto const *player = std::getenv("LC1_PLAYER_ID")) {
            std::string_view const text{player};
            auto const [end, error] =
                std::from_chars(text.data(), text.data() + text.size(), config.local_player.value);
            if (error != std::errc{} || end != text.data() + text.size() ||
                config.local_player.value == 0)
                lc1::fail("LC1_PLAYER_ID must be a nonzero player id");
        }

        lc1::app::Application application{config};
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
