#include "lc1/game/ui/game-ui.hpp"

#include "lc1/game/party/party.hpp"
#include "lc1/game/ui/music-browser.hpp"
#include "lc1/game/world/world-presentation.hpp"
#include "lc1/text/text-layout.hpp"
#include <algorithm>
#include <format>
#include <imgui.h>
#include <ranges>

namespace lc1 {
namespace {
void inventory_list(game::Bag const &bag)
{
    std::vector<game::ResourceStack> items;
    for (auto const &stack : bag.resource_stacks())
        items.push_back(stack);
    std::ranges::sort(items, {}, [](auto const &item) { return item.resource->keyid; });
    auto const *viewport = ImGui::GetMainViewport();
    ImGui::SetNextWindowPos(
        {viewport->WorkPos.x + viewport->WorkSize.x - 24.0F, viewport->WorkPos.y + 180.0F},
        ImGuiCond_FirstUseEver, {1.0F, 0.0F});
    ImGui::SetNextWindowSize({420.0F, 0.0F}, ImGuiCond_Always);
    ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, {20.0F, 18.0F});
    ImGui::PushStyleVar(ImGuiStyleVar_WindowRounding, 10.0F);
    ImGui::PushStyleVar(ImGuiStyleVar_CellPadding, {10.0F, 12.0F});
    ImGui::PushStyleColor(ImGuiCol_WindowBg, IM_COL32(20, 25, 30, 242));
    ImGui::PushStyleColor(ImGuiCol_Text, IM_COL32(231, 226, 212, 255));
    ImGui::PushStyleColor(ImGuiCol_PlotHistogram, IM_COL32(191, 155, 88, 255));
    if (ImGui::Begin("Inventory", nullptr,
                     ImGuiWindowFlags_NoTitleBar | ImGuiWindowFlags_AlwaysAutoResize |
                         ImGuiWindowFlags_NoSavedSettings)) {
        ImGui::TextColored({0.84F, 0.70F, 0.42F, 1.0F}, "INVENTORY");
        ImGui::TextDisabled("Travel supplies");
        ImGui::Spacing();
        if (ImGui::BeginTable("items", 3,
                              ImGuiTableFlags_RowBg | ImGuiTableFlags_BordersInnerH |
                                  ImGuiTableFlags_SizingStretchProp)) {
            ImGui::TableSetupColumn("Item", ImGuiTableColumnFlags_WidthStretch);
            ImGui::TableSetupColumn("Quantity", ImGuiTableColumnFlags_WidthFixed, 65.0F);
            ImGui::TableSetupColumn("Weight (kg)", ImGuiTableColumnFlags_WidthFixed, 65.0F);
            ImGui::TableHeadersRow();
            for (auto const &item : items) {
                ImGui::TableNextRow();
                ImGui::TableNextColumn();
                ImGui::TextUnformatted(item.resource->name.data(),
                                       item.resource->name.data() + item.resource->name.size());
                if (ImGui::IsItemHovered()) {
                    ImGui::BeginTooltip();
                    ImGui::TextDisabled("Weight per item: %.2f kg", item.resource->weight / 1000.0);
                    ImGui::EndTooltip();
                }
                ImGui::TableNextColumn();
                ImGui::Text("%u", item.quantity);
                ImGui::TableNextColumn();
                ImGui::Text("%.2f",
                            static_cast<double>(item.quantity) * item.resource->weight / 1000.0);
            }
            ImGui::EndTable();
        }
        auto const used_weight = bag.total_weight();
        auto const capacity = bag.max_weight();
        if (items.empty())
            ImGui::TextDisabled("Your inventory is empty.");
        ImGui::Spacing();
        auto const label =
            std::format("Weight: {:.2f} / {:.2f} kg", used_weight / 1000.0, capacity / 1000.0);
        ImGui::ProgressBar(capacity == 0 ? 0.0F : static_cast<float>(used_weight) / capacity,
                           {-1.0F, 18.0F}, label.c_str());
    }
    ImGui::End();
    ImGui::PopStyleColor(3);
    ImGui::PopStyleVar(3);
}
} // namespace

void GameUi::open_save(std::string_view name)
{
    save_name_.fill('\0');
    std::ranges::copy(name.substr(0, save_name_.size() - 1), save_name_.begin());
    save_open_ = true;
    load_open_ = false;
}

GameUiActions GameUi::update(GameUiInfo const &info, MusicBrowser &music)
{
    GameUiActions actions;
    ImGui::Begin("lc1 debug info");
    auto [w, h] = info.extent;
    ImGui::TextUnformatted(std::format("Window size: {}x{}", w, h).c_str());
    ImGui::TextUnformatted(std::format("FPS: {}", ImGui::GetIO().Framerate).c_str());
    ImGui::TextUnformatted(std::format("Lighting: {}", info.lighting_name).c_str());
    ImGui::Text("Exposure: %.2f", info.exposure);
    if (ImGui::Button("Save game (F6)"))
        open_save(info.current_save);
    if (ImGui::Button("Load game")) {
        load_open_ = true;
        save_open_ = false;
        actions.refresh_saves = true;
    }
    ImGui::TextUnformatted(save_status_.c_str());
    ImGui::TextUnformatted(
        std::format("Showcase motion: {}", info.showcase_animated ? "on" : "off").c_str());
    ImGui::TextUnformatted(std::format("interaction_target: {}", info.interaction_target.empty()
                                                                     ? "not found"
                                                                     : info.interaction_target)
                               .c_str());

    if (state_ == State::InGame && ImGui::Button("Main menu"))
        state_ = State::MainMenu;

    music.controls();

    ImGui::End();
    music.update();

    switch (state_) {
    case State::MainMenu:
        ImGui::Begin("lc1 main menu##menu");
        if (ImGui::Button("Start Game"))
            actions.start = true; // TODO
        if (ImGui::Button("Load Game")) {
            load_open_ = true;
            save_open_ = false;
            actions.refresh_saves = true;
        }
        if (ImGui::Button("Settings"))
            state_ = State::InSettings;
        if (ImGui::Button("Quit"))
            actions.quit = true;
        ImGui::End();
        break;
    case State::InSettings:
        ImGui::Begin("lc1 settings##menu");
        if (ImGui::SliderFloat("Volume", &volume_, 0, 100))
            actions.volume = volume_;
        if (ImGui::Button("Back to Main Menu"))
            state_ = State::MainMenu;
        ImGui::End();
        break;
    case State::InGame:
        inventory_list(info.bag);
        break;
    }
    if (save_open_) {
        ImGui::Begin("Save game##save", &save_open_, ImGuiWindowFlags_AlwaysAutoResize);
        ImGui::InputText("Save name", save_name_.data(), save_name_.size());
        auto const name = std::string_view{save_name_.data()};
        bool const overwrite = std::ranges::find(info.saves, name) != info.saves.end();
        if (overwrite)
            ImGui::TextUnformatted("This name already exists. Saving replaces its checkpoint.");
        ImGui::BeginDisabled(name.empty());
        if (ImGui::Button(overwrite ? "Overwrite save" : "Save"))
            actions.save = std::string{name};
        ImGui::EndDisabled();
        ImGui::SameLine();
        if (ImGui::Button("Cancel"))
            save_open_ = false;
        ImGui::TextUnformatted(save_status_.c_str());
        ImGui::End();
    }
    if (load_open_) {
        ImGui::Begin("Load game##load", &load_open_, ImGuiWindowFlags_AlwaysAutoResize);
        if (ImGui::Button("Refresh"))
            actions.refresh_saves = true;
        if (info.saves.empty())
            ImGui::TextUnformatted("No saves found.");
        for (auto const &name : info.saves) {
            ImGui::PushID(name.c_str());
            ImGui::TextUnformatted(name.c_str());
            ImGui::SameLine();
            if (ImGui::Button("Load"))
                actions.load = name;
            ImGui::PopID();
        }
        ImGui::TextUnformatted("Loading replaces the current unsaved game.");
        ImGui::TextUnformatted(save_status_.c_str());
        if (ImGui::Button("Cancel"))
            load_open_ = false;
        ImGui::End();
    }
    return actions;
}

std::vector<ScreenTextItem> GameUi::screen_labels(GameWorld const &world, GpuFont const &font,
                                                  FrameExtent extent, GameLocation location) const
{
    auto const [w, h] = extent;

    std::vector<ScreenTextItem> labels;

    labels.push_back(ScreenTextItem{
        .text = std::format("{} | {} / {}", world.player().config().name, location.region,
                            location.place),
        .position = {24.0F, 46.0F},
        .line_height = 22.0F,
    });

    labels.push_back(ScreenTextItem{
        .text =
            std::format("Party: {} companions", world.player().config().party->members().size()),
        .position = {24.0F, 78.0F},
        .line_height = 22.0F,
    });
    labels.push_back(
        ScreenTextItem{.text = save_status_, .position = {24.0F, 108.0F}, .line_height = 22.0F});

    auto const *dialog = world.dialogs().find(world.player());
    if (world.interaction_target() && !dialog) {
        ScreenTextItem interact_text{
            .text = std::format("Press F to interact with {}",
                                world.interaction_target()->config().name),
            .line_height = font.atlas().line_height_for_em(32),
        };
        auto const metrics =
            text::measure_text(font.atlas(), interact_text.text, interact_text.line_height);
        interact_text.position = {(w - metrics.width) * 0.5F, (h - metrics.height) * 0.5F};
        labels.push_back(interact_text);
    }

    if (dialog) {
        ScreenTextItem item{
            .text = std::format("{}: {}", dialog->speaker()->config().name,
                                dialog->current_speech_text()),
            .line_height = font.atlas().line_height_for_em(32),
        };
        auto const metrics = text::measure_text(font.atlas(), item.text, item.line_height);
        item.position = {(extent.width - metrics.width) * 0.5F, extent.height * 0.7};
        labels.push_back(item);

        labels.push_back(ScreenTextItem{
            .text = "F: End conversation", .position = {24.0F, 138.0F}, .line_height = 22.0F});
        if (dialog->respondent() == &world.player())
            for (auto const &[index, response] :
                 std::views::zip(std::views::iota(0), dialog->candidate_responses())) {
                ScreenTextItem response_item{
                    .text = std::format("{}. {}", index + 1, response),
                    .line_height = font.atlas().line_height_for_em(24),
                };
                auto const response_metrics =
                    font.atlas().measure(response_item.text, response_item.line_height);
                response_item.position = {(extent.width - response_metrics.width) * 0.5F,
                                          extent.height * 0.7 + metrics.height +
                                              index * response_metrics.height};
                labels.push_back(response_item);
            }
    }

    return labels;
}

} // namespace lc1

namespace lc1 {

bool GameUi::gameplay_enabled() const
{
    return state_ == State::InGame && !save_open_ && !load_open_;
}

void GameUi::open_load()
{
    load_open_ = true;
    save_open_ = false;
}

void GameUi::saved()
{
    save_open_ = false;
}

void GameUi::start_game()
{
    state_ = State::InGame;
}

void GameUi::set_save_status(std::string status)
{
    save_status_ = std::move(status);
}

} // namespace lc1
