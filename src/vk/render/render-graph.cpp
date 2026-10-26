#include "lc1/vk/render/render-graph.hpp"

namespace lc1 {

void RenderGraph::import_resource(std::string name)
{
    invalidate();
    imports_.insert(std::move(name));
}

void RenderGraph::add_pass(std::string name, std::vector<std::string> inputs,
                           std::vector<std::string> outputs,
                           std::function<void(CommandBuffer &)> record_commands)
{
    invalidate();
    passes_.emplace_back(std::move(name), std::move(inputs), std::move(outputs),
                         std::move(record_commands));
}

std::expected<void, std::string> RenderGraph::compile()
{
    invalidate();
    if (imports_.contains(""))
        return std::unexpected("Render graph imports an empty resource name.");

    std::unordered_set<std::string> pass_names;
    std::unordered_map<std::string, std::size_t> resource_writer;
    for (std::size_t pass_index = 0; pass_index < passes_.size(); ++pass_index) {
        auto const &pass = passes_[pass_index];
        if (pass.name.empty())
            return std::unexpected(std::format("Render graph pass {} has no name.", pass_index));
        if (!pass_names.insert(pass.name).second)
            return std::unexpected(
                std::format("Render graph has duplicate pass name '{}'.", pass.name));
        if (!pass.record_commands)
            return std::unexpected(std::format("Pass '{}' has no recording callback.", pass.name));

        for (auto const &output : pass.outputs) {
            if (output.empty())
                return std::unexpected(
                    std::format("Pass '{}' declares an empty output resource.", pass.name));
            if (imports_.contains(output))
                return std::unexpected(std::format(
                    "Pass '{}' writes imported resource '{}'; use a distinct name for its new "
                    "version.",
                    pass.name, output));
            auto const [writer, inserted] = resource_writer.emplace(output, pass_index);
            if (!inserted)
                return std::unexpected(std::format(
                    "Resource '{}' has duplicate writers '{}' and '{}'; declare each output "
                    "once and use distinct names for successive resource versions.",
                    output, passes_[writer->second].name, pass.name));
        }
    }

    std::vector<std::vector<std::size_t>> dependents(passes_.size());
    std::vector<std::size_t> in_degree(passes_.size());
    for (std::size_t pass_index = 0; pass_index < passes_.size(); ++pass_index) {
        auto const &pass = passes_[pass_index];
        std::unordered_set<std::size_t> dependencies;
        for (auto const &input : pass.inputs) {
            if (input.empty())
                return std::unexpected(
                    std::format("Pass '{}' declares an empty input resource.", pass.name));
            auto const writer = resource_writer.find(input);
            if (writer == resource_writer.end()) {
                if (imports_.contains(input))
                    continue;
                return std::unexpected(
                    std::format("Pass '{}' reads resource '{}' with no producing pass or import.",
                                pass.name, input));
            }
            if (writer->second == pass_index)
                return std::unexpected(std::format(
                    "Pass '{}' reads and writes resource '{}'; use distinct input and output "
                    "version names.",
                    pass.name, input));
            if (dependencies.insert(writer->second).second) {
                ++in_degree[pass_index];
                dependents[writer->second].push_back(pass_index);
            }
        }
    }

    // Among ready passes, preserve declaration order. Hash-table iteration never
    // determines the recording order, and multiple inputs from one writer form one edge.
    std::priority_queue<std::size_t, std::vector<std::size_t>, std::greater<>> ready;
    for (std::size_t pass_index = 0; pass_index < passes_.size(); ++pass_index) {
        if (in_degree[pass_index] == 0)
            ready.push(pass_index);
    }

    std::vector<std::size_t> order;
    order.reserve(passes_.size());
    while (!ready.empty()) {
        auto const pass_index = ready.top();
        ready.pop();
        order.push_back(pass_index);
        for (auto const dependent : dependents[pass_index]) {
            if (--in_degree[dependent] == 0)
                ready.push(dependent);
        }
    }
    if (order.size() != passes_.size()) {
        std::string error = "Render graph has a dependency cycle; remaining pass dependencies:";
        for (std::size_t pass_index = 0; pass_index < passes_.size(); ++pass_index) {
            if (in_degree[pass_index] == 0)
                continue;
            for (auto const &input : passes_[pass_index].inputs) {
                auto const writer = resource_writer.find(input);
                if (writer != resource_writer.end() && in_degree[writer->second] != 0)
                    error +=
                        std::format("\n  Pass '{}' waits for resource '{}' from pass '{}'.",
                                    passes_[pass_index].name, input, passes_[writer->second].name);
            }
        }
        return std::unexpected(std::move(error));
    }

    pass_order_ = std::move(order);
    compiled_ = true;
    return {};
}

void RenderGraph::execute(CommandBuffer &commands) const
{
    if (!compiled_)
        fail("RenderGraph::execute requires successful compile() after the last graph mutation");
    for (auto const index : pass_order_)
        passes_[index].record_commands(commands);
}

void RenderGraph::invalidate()
{
    compiled_ = false;
    pass_order_.clear();
}

} // namespace lc1
