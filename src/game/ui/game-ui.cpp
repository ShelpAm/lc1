#include "lc1/game/ui/game-ui.hpp"

#include "lc1/game/resources/catalog.hpp"
#include "lc1/game/resources/resource-stack.hpp"
#include "lc1/game/ui/music-browser.hpp"
#include "lc1/game/world/world-presentation.hpp"
#include "lc1/text/text-layout.hpp"

#include <algorithm>
#include <array>
#include <cassert>
#include <format>
#include <imgui.h>
#include <ranges>

namespace lc1 {
namespace {
constexpr ImVec4 accent{0.83F, 0.69F, 0.43F, 1.0F};

void panel_heading(char const *title)
{
    ImGui::TextColored(accent, "%s", title);
    ImGui::Separator();
    ImGui::Spacing();
}

void unavailable_button(char const *label, char const *reason)
{
    ImGui::BeginDisabled();
    ImGui::Button(label);
    ImGui::EndDisabled();
    if (ImGui::IsItemHovered(ImGuiHoveredFlags_AllowWhenDisabled))
        ImGui::SetTooltip("%s", reason);
}

std::vector<game::ResourceStack> inventory_items(game::Bag const &bag, int category)
{
    std::vector<game::ResourceStack> items;
    for (auto const &[keyid, quantity] : bag.quantities()) {
        auto const *resource = game::find_resource(keyid);
        assert(resource);
        if (category == 0 || static_cast<int>(resource->category) == category - 1)
            items.push_back({keyid, quantity});
    }
    std::ranges::sort(items, {}, &game::ResourceStack::keyid);
    return items;
}

void inventory_list(game::Bag const &bag)
{
    auto const items = inventory_items(bag, 0);
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
                auto const *resource = game::find_resource(item.keyid);
                assert(resource);
                ImGui::TableNextRow();
                ImGui::TableNextColumn();
                ImGui::TextUnformatted(resource->name.data(),
                                       resource->name.data() + resource->name.size());
                if (ImGui::IsItemHovered()) {
                    ImGui::BeginTooltip();
                    ImGui::TextDisabled("Weight per item: %.2f kg", resource->weight / 1000.0);
                    ImGui::EndTooltip();
                }
                ImGui::TableNextColumn();
                ImGui::Text("%u", item.quantity);
                ImGui::TableNextColumn();
                ImGui::Text("%.2f", static_cast<double>(item.quantity) * resource->weight / 1000.0);
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

game::Resource const *inventory_panel(game::Bag const &bag, ImVec2 size, int &category,
                                      std::optional<std::uint32_t> &resource_id)
{
    game::Resource const *selected = nullptr;
    if (ImGui::BeginChild("Inventory panel", size, ImGuiChildFlags_Borders)) {
        panel_heading("INVENTORY");
        float const footer =
            ImGui::GetFrameHeightWithSpacing() * 2 + ImGui::GetStyle().ItemSpacing.y;
        if (ImGui::BeginChild("Categories", {78, -footer})) {
            constexpr std::array names{"All", "Currency", "Materials", "Equipment"};
            for (int i = 0; i < static_cast<int>(names.size()); ++i)
                if (ImGui::Selectable(names[i], category == i, 0, {0, 32}))
                    category = i;
        }
        ImGui::EndChild();
        ImGui::SameLine();
        auto const items = inventory_items(bag, category);
        auto chosen = std::ranges::find_if(
            items, [&](auto const &item) { return resource_id == item.keyid; });
        if (chosen != items.end())
            selected = game::find_resource(chosen->keyid);
        else if (!items.empty())
            selected = game::find_resource(items.front().keyid);

        if (ImGui::BeginChild("Items", {0, -footer})) {
            for (auto const &item : items) {
                auto const *resource = game::find_resource(item.keyid);
                assert(resource);
                ImGui::PushID(static_cast<int>(resource->keyid));
                auto const label =
                    std::format("{}\nx{}   {:.2f} kg", resource->name, item.quantity,
                                static_cast<double>(item.quantity) * resource->weight / 1000.0);
                if (ImGui::Selectable(label.c_str(), selected == resource, 0, {0, 54}))
                    selected = resource;
                if (ImGui::BeginPopupContextItem("Item actions")) {
                    if (ImGui::MenuItem("Details"))
                        selected = resource;
                    ImGui::BeginDisabled();
                    ImGui::MenuItem("Discard");
                    ImGui::EndDisabled();
                    ImGui::TextDisabled("Item disposal is not available yet.");
                    ImGui::EndPopup();
                }
                ImGui::PopID();
            }
            if (items.empty())
                ImGui::TextWrapped("No items in this category.");
        }
        ImGui::EndChild();

        auto const used = bag.total_weight();
        auto const capacity = bag.max_weight();
        auto const weight = std::format("{:.2f} / {:.2f} kg", used / 1000.0, capacity / 1000.0);
        ImGui::ProgressBar(capacity ? static_cast<float>(used) / capacity : 0.0F, {-1, 0},
                           weight.c_str());
        unavailable_button("Transfer", "Inventory transfers are not available yet.");
    }
    ImGui::EndChild();
    resource_id = selected ? std::optional{selected->keyid} : std::nullopt;
    return selected;
}

void character_preview(std::string_view name, ImVec2 size)
{
    if (ImGui::BeginChild("Character preview", size, ImGuiChildFlags_Borders)) {
        panel_heading("APPEARANCE");
        ImGui::TextUnformatted(name.empty() ? "Unknown player" : name.data());
        auto const origin = ImGui::GetCursorScreenPos();
        auto const available = ImGui::GetContentRegionAvail();
        float const height = std::max(100.0F, available.y - ImGui::GetTextLineHeightWithSpacing());
        float const unit = std::min(available.x / 4.0F, height / 8.0F);
        ImVec2 const center{origin.x + available.x * 0.5F, origin.y + height * 0.5F};
        auto *draw = ImGui::GetWindowDrawList();
        auto const body_color = IM_COL32(155, 159, 155, 255);
        draw->AddCircleFilled({center.x, center.y - unit * 2.6F}, unit * 0.65F, body_color);
        draw->AddRectFilled({center.x - unit * 0.8F, center.y - unit * 1.75F},
                            {center.x + unit * 0.8F, center.y + unit * 0.7F}, body_color,
                            unit * 0.25F);
        for (float side : {-1.0F, 1.0F}) {
            draw->AddLine({center.x + side * unit, center.y - unit * 1.5F},
                          {center.x + side * unit * 1.35F, center.y + unit * 0.7F}, body_color,
                          unit * 0.35F);
            draw->AddLine({center.x + side * unit * 0.4F, center.y + unit * 0.5F},
                          {center.x + side * unit * 0.55F, center.y + unit * 3.1F}, body_color,
                          unit * 0.5F);
        }
        ImGui::Dummy({available.x, height});
        ImGui::TextDisabled("Silhouette preview");
    }
    ImGui::EndChild();
}

void equipment_details(game::Bag const &bag, game::Resource const *resource, ImVec2 size)
{
    if (ImGui::BeginChild("Equipment details", size, ImGuiChildFlags_Borders)) {
        panel_heading("EQUIPMENT");
        if (resource) {
            ImGui::TextWrapped("%s", resource->name.c_str());
            ImGui::TextUnformatted(std::format("Category: {}", resource->category).c_str());
            ImGui::Text("Unit weight: %.2f kg", resource->weight / 1000.0);
            ImGui::Text("Quantity: %u", bag.quantity_of(resource->keyid));
        }
        else {
            ImGui::TextWrapped("Select an inventory item to inspect it.");
        }
        ImGui::Spacing();
        unavailable_button("Equip", "Resource items are not linked to wearable equipment yet.");
        ImGui::SameLine();
        unavailable_button("Save preset", "Equipment presets are not available yet.");
        unavailable_button("Apply preset", "Equipment presets are not available yet.");
    }
    ImGui::EndChild();
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

    if (state_ == State::InGame && ImGui::Button("Character & inventory (B)"))
        toggle_player_info();

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
        if (!player_info_opened_)
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

    if (state_ == State::InGame && player_info_opened_)
        player_info(info);

    return actions;
}

std::vector<ScreenTextItem> GameUi::screen_labels(WorldSnapshot const &world, PlayerId observer,
                                                  GpuFont const &font, FrameExtent extent,
                                                  GameLocation location) const
{
    auto const [w, h] = extent;
    auto const &player = world.players.at(observer);

    std::vector<ScreenTextItem> labels;

    labels.push_back(ScreenTextItem{
        .text = std::format("{} | {} / {}", world.player(observer).name, location.region,
                            location.place),
        .position = {24.0F, 46.0F},
        .line_height = 22.0F,
    });

    labels.push_back(ScreenTextItem{
        .text = std::format("Party: {} companions", player.companion_count),
        .position = {24.0F, 78.0F},
        .line_height = 22.0F,
    });
    labels.push_back(
        ScreenTextItem{.text = save_status_, .position = {24.0F, 108.0F}, .line_height = 22.0F});

    auto const *dialog = player.dialog ? &*player.dialog : nullptr;
    if (!player.interaction_target.empty() && !dialog) {
        ScreenTextItem interact_text{
            .text = std::format("Press F to interact with {}", player.interaction_target),
            .line_height = font.atlas().line_height_for_em(32),
        };
        auto const metrics =
            text::measure_text(font.atlas(), interact_text.text, interact_text.line_height);
        interact_text.position = {(w - metrics.width) * 0.5F, (h - metrics.height) * 0.5F};
        labels.push_back(interact_text);
    }

    if (dialog) {
        ScreenTextItem item{
            .text = std::format("{}: {}", dialog->speaker, dialog->speech),
            .line_height = font.atlas().line_height_for_em(32),
        };
        auto const metrics = text::measure_text(font.atlas(), item.text, item.line_height);
        item.position = {(extent.width - metrics.width) * 0.5F, extent.height * 0.7};
        labels.push_back(item);

        labels.push_back(ScreenTextItem{
            .text = "F: End conversation", .position = {24.0F, 138.0F}, .line_height = 22.0F});
        for (auto const &[index, response] :
             std::views::zip(std::views::iota(0), dialog->responses)) {
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

void GameUi::toggle_player_info()
{
    if (state_ == State::InGame && !save_open_ && !load_open_)
        player_info_opened_ = !player_info_opened_;
}

void GameUi::player_info(GameUiInfo const &info)
{
    auto const *viewport = ImGui::GetMainViewport();
    ImVec2 const max_size{std::max(1.0F, viewport->WorkSize.x - 24.0F),
                          std::max(1.0F, viewport->WorkSize.y - 24.0F)};
    ImGui::SetNextWindowPos({viewport->WorkPos.x + viewport->WorkSize.x * 0.5F,
                             viewport->WorkPos.y + viewport->WorkSize.y * 0.5F},
                            ImGuiCond_FirstUseEver, {0.5F, 0.5F});
    ImGui::SetNextWindowSize({std::min(1240.0F, max_size.x), std::min(780.0F, max_size.y)},
                             ImGuiCond_FirstUseEver);
    ImGui::SetNextWindowSizeConstraints(
        {std::min(760.0F, max_size.x), std::min(580.0F, max_size.y)}, max_size);
    ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, {16, 16});
    ImGui::PushStyleVar(ImGuiStyleVar_ChildRounding, 5.0F);
    ImGui::PushStyleVar(ImGuiStyleVar_ItemSpacing, {12, 8});
    ImGui::PushStyleColor(ImGuiCol_WindowBg, IM_COL32(24, 28, 31, 255));
    ImGui::PushStyleColor(ImGuiCol_ChildBg, IM_COL32(35, 40, 43, 255));
    ImGui::PushStyleColor(ImGuiCol_Header, IM_COL32(104, 86, 55, 255));

    if (ImGui::Begin("Character & inventory", &player_info_opened_,
                     ImGuiWindowFlags_HorizontalScrollbar)) {
        auto available = ImGui::GetContentRegionAvail();
        available.x = std::max(720.0F, available.x);
        float const gap = ImGui::GetStyle().ItemSpacing.x;
        float const left = (available.x - gap * 2) * 0.34F;
        float const middle = (available.x - gap * 2) * 0.28F;
        float const right = available.x - gap * 2 - left - middle;
        float const top = std::clamp(available.y * 0.29F, 150.0F, 210.0F);
        float const bottom = std::max(240.0F, available.y - top - ImGui::GetStyle().ItemSpacing.y);

        if (ImGui::BeginChild("Party info", {left, top}, ImGuiChildFlags_Borders)) {
            panel_heading("PARTY");
            ImGui::TextUnformatted("Party members");
            ImGui::Text("Companions: %zu", info.companion_count);
            ImGui::Spacing();
            ImGui::BulletText("%s", info.player_name.empty() ? "Unknown player"
                                                             : info.player_name.data());
            if (info.companion_count > 0)
                ImGui::TextDisabled("Companion details are shown in world labels.");
            else
                ImGui::TextDisabled("No companions.");
        }
        ImGui::EndChild();

        ImGui::SameLine();
        if (ImGui::BeginChild("Details", {middle, top}, ImGuiChildFlags_Borders)) {
            panel_heading("CHARACTER");
            ImGui::TextUnformatted(info.player_name.empty() ? "Unknown player"
                                                            : info.player_name.data());
            ImGui::Text("Age: --   Level: --");
            ImGui::Spacing();
            ImGui::TextWrapped("Faction: %s",
                               info.player_faction.empty() ? "None" : info.player_faction.data());
            ImGui::TextUnformatted("Party: Active player party");
            ImGui::Spacing();
            ImGui::TextDisabled("Background");
            ImGui::TextWrapped("No background provided.");
        }
        ImGui::EndChild();

        ImGui::SameLine();
        if (ImGui::BeginChild("Attributes", {right, top}, ImGuiChildFlags_Borders)) {
            panel_heading("ATTRIBUTES");
            ImGui::TextUnformatted("Health: --");
            ImGui::TextUnformatted("Armor: --");
            ImGui::TextUnformatted("Strength: --    Agility: --");
            ImGui::Spacing();
            ImGui::TextUnformatted("Order: --");
            ImGui::TextDisabled("-- = not available yet");
        }
        ImGui::EndChild();

        auto const *resource =
            inventory_panel(info.bag, {left, bottom}, inventory_category_, selected_resource_);
        ImGui::SameLine();
        character_preview(info.player_name, {middle, bottom});
        ImGui::SameLine();
        equipment_details(info.bag, resource, {right, bottom});
    }
    ImGui::End();

    ImGui::PopStyleColor(3);
    ImGui::PopStyleVar(3);
}

} // namespace lc1

namespace lc1 {

bool GameUi::gameplay_enabled() const
{
    return state_ == State::InGame && !save_open_ && !load_open_ && !player_info_opened_;
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
