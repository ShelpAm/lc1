#pragma once

#include "lc1/error.hpp"

#include <cstddef>
#include <expected>
#include <format>
#include <functional>
#include <queue>
#include <string>
#include <unordered_map>
#include <unordered_set>
#include <utility>
#include <vector>

namespace lc1 {

class CommandBuffer;

// Schedules synchronous command recording from logical resource dependencies.
// Each resource name denotes one value/version, supplied by an import or one pass.
// Reusing a physical resource requires distinct names for its successive versions.
// Imports do not transfer ownership or track Vulkan state: callbacks retain resource
// allocation, layout transitions and memory barriers, including across submissions.
class RenderGraph {
  public:
    void import_resource(std::string name);

    void add_pass(std::string name, std::vector<std::string> inputs,
                  std::vector<std::string> outputs,
                  std::function<void(CommandBuffer &)> record_commands);

    std::expected<void, std::string> compile();

    // Callbacks run before execute returns. Their borrowed resources must remain alive,
    // and callbacks must not mutate this graph during recording.
    void execute(CommandBuffer &commands) const;

  private:
    struct Pass {
        std::string name;
        std::vector<std::string> inputs;
        std::vector<std::string> outputs;
        std::function<void(CommandBuffer &)> record_commands;
    };

    void invalidate();

    std::vector<Pass> passes_;
    std::unordered_set<std::string> imports_;
    std::vector<std::size_t> pass_order_;
    bool compiled_ = false;
};

} // namespace lc1
