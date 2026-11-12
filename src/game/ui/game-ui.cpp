#include "lc1/game/ui/game-ui.hpp"

#include "lc1/game/party/faction.hpp"
#include "lc1/game/party/party.hpp"
#include "lc1/game/resources/catalog.hpp"
#include "lc1/game/resources/resource-stack.hpp"
#include "lc1/game/ui/music-browser.hpp"
#include "lc1/game/world/world-presentation.hpp"
#include "lc1/text/text-layout.hpp"
#include <algorithm>
#include <cassert>
#include <cmath>
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

CharacterSnapshot const *party_info(GameUiInfo const &info, ImVec2 size,
                                    std::optional<std::uint32_t> &party_id,
                                    std::optional<CharacterId> &character_id)
{
    auto const *player = &info.world.character(info.player);
    auto const *selected = player;
    auto const &parties = info.player_parties;
    auto current = std::ranges::find_if(
        parties, [&](auto const *party) { return party_id == party->keyid(); });
    if (current == parties.end())
        current = parties.begin();
    Party const *party = current != parties.end() ? *current : nullptr;
    std::vector<CharacterSnapshot const *> people;
    auto const resolve_selection = [&] {
        people.clear();
        if (party) {
            party_id = party->keyid();
            if (party->leader())
                people.push_back(&info.world.character(*party->leader()));
            for (auto id : party->members())
                people.push_back(&info.world.character(id));
            for (auto id : party->captives())
                people.push_back(&info.world.character(id));
        }
        auto chosen = std::ranges::find_if(
            people, [&](auto const *person) { return character_id == CharacterId{person->keyid}; });
        if (chosen != people.end())
            selected = *chosen;
        else if (std::ranges::find(people, player) != people.end())
            selected = player;
        else if (!people.empty())
            selected = people.front();
    };
    resolve_selection();
    if (ImGui::BeginChild("Party info", size, ImGuiChildFlags_Borders)) {
        panel_heading("PARTY");
        auto const preview = party ? std::format("Party {}", party->keyid()) : "No party";
        ImGui::SetNextItemWidth(-1.0F);
        if (ImGui::BeginCombo("##party", preview.c_str())) {
            for (auto const *candidate : parties) {
                auto const label = std::format("Party {}", candidate->keyid());
                if (ImGui::Selectable(label.c_str(), candidate == party)) {
                    party = candidate;
                    character_id.reset();
                }
            }
            ImGui::EndCombo();
        }
        resolve_selection();
        if (ImGui::BeginChild("Members", {0, 0})) {
            for (auto const *person : people) {
                ImGui::PushID(static_cast<int>(person->keyid));
                auto name = person->name;
                if (person == player)
                    name += " (Me)";
                if (party->has_captive(CharacterId{person->keyid}))
                    name += " (Captive)";
                if (ImGui::Selectable(name.c_str(), person == selected, 0, {0, 25}))
                    selected = person;
                if (CharacterId{person->keyid} == party->leader()) {
                    ImGui::SameLine(ImGui::GetWindowContentRegionMax().x - 22.0F);
                    ImGui::TextColored(accent, "★");
                }
                ImGui::PopID();
            }
            if (people.empty())
                ImGui::TextDisabled("No party members.");
        }
        ImGui::EndChild();
    }
    ImGui::EndChild();
    character_id = CharacterId{selected->keyid};
    return selected;
}

void character_details(CharacterSnapshot const &person, ImVec2 size)
{
    if (ImGui::BeginChild("Details", size, ImGuiChildFlags_Borders)) {
        panel_heading("CHARACTER");
        auto const &config = person;
        ImGui::TextUnformatted(config.name.c_str());
        ImGui::Text("Age: %d   Level: %d", config.age, config.level);
        ImGui::Spacing();
        ImGui::TextWrapped("Faction: %s", person.faction.c_str());
        ImGui::Text("Party: %u", config.party_id);
        ImGui::Spacing();
        ImGui::TextDisabled("Background");
        ImGui::TextWrapped("No background provided.");
    }
    ImGui::EndChild();
}

void character_stats(CharacterSnapshot const &person, ImVec2 size)
{
    if (ImGui::BeginChild("Attributes", size, ImGuiChildFlags_Borders)) {
        panel_heading("ATTRIBUTES");
        float armor = 0;
        for (auto const &[type, equipment] : person.equipment)
            armor += equipment.property().armor;
        ImGui::Text("Health: %.0f / %.0f%s", person.health, person.max_health,
                    person.dead ? " (Dead)" : "");
        ImGui::Text("Armor: %.1f", armor);
        ImGui::TextUnformatted("Strength: --    Agility: --");
        ImGui::Spacing();
        auto const order = person.companion_order;
        ImGui::Text("Order: %s", order == CompanionOrder::Follow ? "Follow"
                                 : order == CompanionOrder::Wait ? "Wait"
                                                                 : "None");
        ImGui::TextDisabled("-- = not available yet");
    }
    ImGui::EndChild();
}

std::vector<game::ResourceStack> inventory_items(game::Bag const &bag, int category,
                                                 std::vector<std::uint32_t> const &order)
{
    std::vector<game::ResourceStack> items;
    for (auto const &[keyid, quantity] : bag.quantities()) {
        auto const *resource = game::find_resource(keyid);
        bool const equipment = resource->category == game::ResourceCategory::Equipment;
        if (category == 0 || (category == 1 && equipment) || (category == 2 && !equipment))
            items.push_back({keyid, quantity});
    }
    std::ranges::sort(items, [&](auto const &left, auto const &right) {
        auto const l = std::ranges::find(order, left.keyid);
        auto const r = std::ranges::find(order, right.keyid);
        if (l != r && (l != order.end() || r != order.end()))
            return l < r;
        auto const *a = game::find_resource(left.keyid);
        auto const *b = game::find_resource(right.keyid);
        if (a->category != b->category)
            return a->category < b->category;
        if (a->weight != b->weight)
            return a->weight < b->weight;
        return left.keyid < right.keyid;
    });
    return items;
}

game::Resource const *inventory_panel(CharacterSnapshot const &person, ImVec2 size, int &category,
                                      std::optional<std::uint32_t> &resource_id,
                                      std::vector<std::uint32_t> &order)
{
    auto const &bag = person.bag;
    auto const *selected =
        resource_id && bag.quantity_of(*resource_id) ? game::find_resource(*resource_id) : nullptr;
    if (ImGui::BeginChild("Inventory panel", size, ImGuiChildFlags_Borders)) {
        panel_heading("INVENTORY");
        auto const used = bag.total_weight();
        auto const capacity = bag.max_weight();
        auto const weight = std::format("{:.2f} / {:.2f} kg", used / 1000.0, capacity / 1000.0);
        ImGui::ProgressBar(capacity ? static_cast<float>(used) / capacity : 0.0F, {-1, 0},
                           weight.c_str());
        if (ImGui::IsItemHovered())
            ImGui::SetTooltip("Backpack capacity. Changing backpacks is not available yet.");
        constexpr std::array names{"All", "Equipment", "Items"};
        for (int i = 0; i < static_cast<int>(names.size()); ++i) {
            if (i)
                ImGui::SameLine();
            if (ImGui::Selectable(names[i], category == i, 0,
                                  {std::max(1.0F, (size.x - 56) / 3), 26})) {
                category = i;
                selected = nullptr;
                order.clear();
            }
        }
        auto const items = inventory_items(bag, category, order);
        float const footer = ImGui::GetFrameHeightWithSpacing();
        if (ImGui::BeginChild("Items", {0, -footer}, ImGuiChildFlags_None,
                              ImGuiWindowFlags_AlwaysVerticalScrollbar)) {
            for (auto const &item : items) {
                auto const *resource = game::find_resource(item.keyid);
                ImGui::PushID(static_cast<int>(item.keyid));
                auto const position = ImGui::GetCursorScreenPos();
                if (ImGui::Selectable("##item", selected == resource, 0, {0, 54}))
                    selected = resource;
                if (ImGui::BeginDragDropSource()) {
                    ImGui::SetDragDropPayload("INVENTORY_ORDER", &item.keyid, sizeof(item.keyid));
                    ImGui::TextUnformatted(resource->name.c_str());
                    ImGui::EndDragDropSource();
                }
                if (ImGui::BeginDragDropTarget()) {
                    if (auto const *payload = ImGui::AcceptDragDropPayload("INVENTORY_ORDER")) {
                        auto const source = *static_cast<std::uint32_t const *>(payload->Data);
                        if (source != item.keyid &&
                            std::ranges::find(items, source, &game::ResourceStack::keyid) !=
                                items.end()) {
                            order.clear();
                            for (auto const &entry : items)
                                if (entry.keyid != source) {
                                    if (entry.keyid == item.keyid)
                                        order.push_back(source);
                                    order.push_back(entry.keyid);
                                }
                        }
                    }
                    ImGui::EndDragDropTarget();
                }
                bool show_details = false;
                if (ImGui::BeginPopupContextItem("Item actions")) {
                    show_details = ImGui::MenuItem("Details");
                    ImGui::BeginDisabled();
                    ImGui::MenuItem("Discard");
                    ImGui::EndDisabled();
                    ImGui::TextDisabled("World item drops are not available yet.");
                    ImGui::EndPopup();
                }
                if (show_details)
                    ImGui::OpenPopup("Item details");
                if (ImGui::BeginPopup("Item details")) {
                    ImGui::TextUnformatted(resource->name.c_str());
                    ImGui::Separator();
                    ImGui::Text("Unit weight: %.2f kg", resource->weight / 1000.0);
                    ImGui::Text("Quantity: %u", item.quantity);
                    ImGui::Text("Total weight: %.2f kg",
                                item.quantity * (resource->weight / 1000.0));
                    ImGui::EndPopup();
                }
                auto *draw = ImGui::GetWindowDrawList();
                draw->AddRectFilled({position.x + 5, position.y + 7},
                                    {position.x + 39, position.y + 41},
                                    IM_COL32(155, 159, 155, 255), 4);
                draw->AddText({position.x + 48, position.y + 5}, ImGui::GetColorU32(ImGuiCol_Text),
                              resource->name.c_str());
                auto const detail = std::format("x{}   {:.2f} kg", item.quantity,
                                                item.quantity * (resource->weight / 1000.0));
                draw->AddText({position.x + 48, position.y + 28}, ImGui::GetColorU32(ImGuiCol_Text),
                              detail.c_str());
                ImGui::PopID();
            }
            if (items.empty())
                ImGui::TextWrapped("No items in this category.");
        }
        ImGui::EndChild();
        unavailable_button("Transfer", "Inventory transfers are not available yet.");
    }
    ImGui::EndChild();
    resource_id = selected ? std::optional{selected->keyid} : std::nullopt;
    return selected;
}

void character_preview(CharacterSnapshot const &person, ImVec2 size)
{
    if (ImGui::BeginChild("Character preview", size, ImGuiChildFlags_Borders)) {
        panel_heading("APPEARANCE");
        ImGui::TextUnformatted(person.name.c_str());
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

void equipment_details(CharacterSnapshot const &person, ImVec2 size, game::Resource const *resource,
                       int &slot, float &slot_position, float &slot_target, GameUiInfo const &info,
                       GameUiActions &actions)
{
    auto const armor =
        resource ? info.world.equipments.find(resource->keyid) : info.world.equipments.end();
    auto const weapon =
        resource ? info.world.weapons.find(resource->keyid) : info.world.weapons.end();
    if (ImGui::BeginChild("Equipment details", size, ImGuiChildFlags_Borders)) {
        panel_heading("EQUIPMENT");
        float const gap = ImGui::GetStyle().ItemSpacing.x;
        float const slots_width = 86;
        float const cards_width =
            std::max(100.0F, ImGui::GetContentRegionAvail().x - slots_width - gap);
        float const content_height = std::max(160.0F, ImGui::GetContentRegionAvail().y -
                                                          ImGui::GetFrameHeightWithSpacing() * 2);
        constexpr std::array slot_names{"Head", "Body", "Feet", "Main hand"};
        // Draw slots first so a click updates both detail cards in the same frame.
        auto const start = ImGui::GetCursorPos();
        ImGui::SetCursorPosX(start.x + cards_width + gap);
        if (ImGui::BeginChild("Slots", {slots_width, content_height})) {
            ImGui::TextDisabled("SLOTS");
            // Keep positions unwrapped so crossing Head/Ranged follows the same short slide.
            slot_position += (slot_target - slot_position) *
                             (1.0F - std::exp(-14.0F * ImGui::GetIO().DeltaTime));
            if (std::abs(slot_target - slot_position) < 0.001F)
                slot_position = slot_target;
            float const step = 58 + ImGui::GetStyle().ItemSpacing.y;
            if (ImGui::BeginChild("Slot wheel", {0, step * 3}, ImGuiChildFlags_None,
                                  ImGuiWindowFlags_NoScrollbar |
                                      ImGuiWindowFlags_NoScrollWithMouse)) {
                auto const origin = ImGui::GetCursorPos();
                int const center = static_cast<int>(std::floor(slot_position));
                int const count = static_cast<int>(slot_names.size());
                for (int position = center - 2; position <= center + 3; ++position) {
                    int const i = (position % count + count) % count;
                    float const y = origin.y + (position - slot_position + 1) * step;
                    if (y + 58 <= origin.y || y >= origin.y + step * 3)
                        continue;
                    ImGui::SetCursorPos({origin.x, y});
                    ImGui::PushID(position);
                    if (ImGui::Selectable(slot_names[i], slot == i, 0, {0, 58})) {
                        slot = i;
                        slot_target = static_cast<float>(position);
                    }
                    ImGui::PopID();
                }
            }
            ImGui::EndChild();
        }
        ImGui::EndChild();
        ImGui::SetCursorPos(start);
        if (ImGui::BeginChild("Item cards", {cards_width, content_height})) {
            float const card_height =
                std::max(70.0F, (ImGui::GetContentRegionAvail().y - gap) * 0.5F);
            constexpr std::array equipment_types{EquipmentType::Head, EquipmentType::Body,
                                                 EquipmentType::Foot};
            bool const has_current =
                slot >= 0 && (slot < 3 ? person.equipment.contains(equipment_types[slot])
                                       : person.main_hand.has_value());
            if (has_current) {
                if (ImGui::BeginChild("Equipped item", {0, card_height}, ImGuiChildFlags_Borders)) {
                    ImGui::TextColored(accent, "CURRENT / %s", slot_names[slot]);
                    auto const &config = person;
                    if (slot < 3) {
                        constexpr std::array types{EquipmentType::Head, EquipmentType::Body,
                                                   EquipmentType::Foot};
                        auto const found = config.equipment.find(types[slot]);
                        if (found != config.equipment.end()) {
                            ImGui::TextUnformatted(
                                game::find_resource(found->second.keyid())->name.c_str());
                            ImGui::Text("Armor: %.1f", found->second.property().armor);
                        }
                        else
                            ImGui::TextDisabled("Empty slot");
                    }
                    else {
                        if (config.main_hand) {
                            auto const &weapon = *config.main_hand;
                            ImGui::TextUnformatted(
                                game::find_resource(weapon.keyid())->name.c_str());
                            if (weapon.action() == WeaponAction::Torch)
                                ImGui::TextUnformatted(person.dead ? "Extinguished" : "Burning");
                            else if (weapon.action() == WeaponAction::ToggleLight)
                                ImGui::TextUnformatted(person.flashlight_on ? "Light: on"
                                                                            : "Light: off");
                            else {
                                ImGui::Text("Attack: %.1f", weapon.property().attack);
                                ImGui::Text("Range: %.1f m", weapon.property().range);
                            }
                        }
                    }
                }
                ImGui::EndChild();
            }
            if (resource && resource->category == game::ResourceCategory::Equipment) {
                if (ImGui::BeginChild("Selected equipment", {0, 0}, ImGuiChildFlags_Borders)) {
                    ImGui::TextColored(accent, "SELECTED EQUIPMENT");
                    ImGui::TextWrapped("%s", resource->name.c_str());
                    if (armor != info.world.equipments.end()) {
                        ImGui::Text("Slot: %s",
                                    std::string{to_string(armor->second.type())}.c_str());
                        ImGui::Text("Armor: %.1f", armor->second.property().armor);
                    }
                    if (weapon != info.world.weapons.end()) {
                        ImGui::TextUnformatted("Slot: Main hand");
                        if (weapon->second.action() == WeaponAction::ToggleLight)
                            ImGui::TextUnformatted("Left click: toggle light");
                        else {
                            ImGui::Text("Attack: %.1f", weapon->second.property().attack);
                            ImGui::Text("Range: %.1f m", weapon->second.property().range);
                        }
                    }
                    ImGui::Text("Unit weight: %.2f kg", resource->weight / 1000.0);
                    ImGui::Text("Quantity: %u", person.bag.quantity_of(resource->keyid));
                }
                ImGui::EndChild();
            }
        }
        ImGui::EndChild();
        bool controllable = person.keyid == info.player.value;
        for (auto const &party : info.world.parties)
            if (party.leader() == info.player && party.has_member(CharacterId{person.keyid}))
                controllable = std::ranges::none_of(info.world.players, [&](auto const &entry) {
                    return entry.second.character_id == CharacterId{person.keyid};
                });
        controllable = controllable && !person.dead && !info.world.character(info.player).dead;
        ImGui::BeginDisabled(!controllable || (armor == info.world.equipments.end() &&
                                               weapon == info.world.weapons.end()));
        if (ImGui::Button("Equip"))
            actions.equipment = EquipmentRequest{CharacterId{person.keyid}, resource->keyid, true};
        ImGui::EndDisabled();
        ImGui::SameLine();
        std::optional<std::uint32_t> current;
        constexpr std::array types{EquipmentType::Head, EquipmentType::Body, EquipmentType::Foot};
        if (slot >= 0 && slot < 3) {
            if (auto const found = person.equipment.find(types[slot]);
                found != person.equipment.end())
                current = found->second.keyid();
        }
        else if (slot == 3 && person.main_hand)
            current = person.main_hand->keyid();
        bool const fits = current && game::find_resource(*current)->weight <=
                                         person.bag.max_weight() - person.bag.total_weight();
        ImGui::BeginDisabled(!controllable || !fits);
        if (ImGui::Button("Unequip"))
            actions.equipment = EquipmentRequest{CharacterId{person.keyid}, *current, false};
        ImGui::EndDisabled();
        if (current && !fits && ImGui::IsItemHovered(ImGuiHoveredFlags_AllowWhenDisabled))
            ImGui::SetTooltip("Not enough backpack capacity.");
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
    auto const minutes = static_cast<int>(info.time_of_day * 60.0);
    ImGui::Text("Time: %02d:%02d", minutes / 60, minutes % 60);
    ImGui::TextUnformatted("Time rate");
    ImGui::SameLine();
    float rate = info.time_rate;
    ImGui::SetNextItemWidth(90.0F);
    bool changed = ImGui::InputFloat("##time-rate-input", &rate, 0, 0, "%.2f");
    ImGui::SameLine();
    ImGui::SetNextItemWidth(180.0F);
    changed |= ImGui::SliderFloat("##time-rate-slider", &rate, 0.01F, 100.0F, "%.2fx",
                                  ImGuiSliderFlags_Logarithmic | ImGuiSliderFlags_AlwaysClamp);
    if (changed && std::isfinite(rate))
        actions.time_rate = std::clamp(rate, 0.01F, 100.0F);
    int density = info.grass_density;
    auto const density_label = std::format("{}%%", density * 25);
    if (ImGui::SliderInt("Grass density", &density, 0, 8, density_label.c_str(),
                         ImGuiSliderFlags_AlwaysClamp))
        actions.grass_density = std::clamp(density, 0, 8);
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
        player_info(info, actions);

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

void GameUi::player_info(GameUiInfo const &info, GameUiActions &actions)
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
        auto const &controlled = info.world.character(info.player);
        ImGui::SetNextItemWidth(260.0F);
        if (ImGui::BeginCombo("Control", controlled.name.c_str())) {
            for (auto const &candidate : info.world.characters) {
                bool const occupied =
                    std::ranges::any_of(info.world.players, [&](auto const &entry) {
                        return entry.second.character_id == CharacterId{candidate.keyid} &&
                               candidate.keyid != info.player.value;
                    });
                ImGui::BeginDisabled(candidate.dead || occupied);
                auto const label = std::format("{}##{}", candidate.name, candidate.keyid);
                if (ImGui::Selectable(label.c_str(), candidate.keyid == info.player.value))
                    actions.control_target = CharacterId{candidate.keyid};
                ImGui::EndDisabled();
            }
            ImGui::EndCombo();
        }
        auto available = ImGui::GetContentRegionAvail();
        available.x = std::max(720.0F, available.x);
        float const gap = ImGui::GetStyle().ItemSpacing.x;
        float const left = (available.x - gap * 2) * 0.34F;
        float const middle = (available.x - gap * 2) * 0.28F;
        float const right = available.x - gap * 2 - left - middle;
        float const top = std::clamp(available.y * 0.29F, 150.0F, 210.0F);
        float const bottom = std::max(240.0F, available.y - top - ImGui::GetStyle().ItemSpacing.y);
        auto const previous_character = selected_character_;
        auto const *person = party_info(info, {left, top}, selected_party_, selected_character_);
        if (previous_character != selected_character_) {
            selected_resource_.reset();
            inventory_order_.clear();
            selected_slot_ = -1;
            slot_position_ = slot_target_ = 0;
        }
        ImGui::SameLine();
        character_details(*person, {middle, top});
        ImGui::SameLine();
        character_stats(*person, {right, top});
        auto const previous_resource = selected_resource_;
        auto const *resource = inventory_panel(*person, {left, bottom}, inventory_category_,
                                               selected_resource_, inventory_order_);
        if (resource && previous_resource != selected_resource_) {
            if (auto const found = info.world.equipments.find(resource->keyid);
                found != info.world.equipments.end())
                selected_slot_ = static_cast<int>(found->second.type());
            else if (auto const found = info.world.weapons.find(resource->keyid);
                     found != info.world.weapons.end())
                selected_slot_ = 3;
            if (selected_slot_ >= 0)
                slot_target_ = static_cast<float>(selected_slot_);
        }
        ImGui::SameLine();
        character_preview(*person, {middle, bottom});
        ImGui::SameLine();
        equipment_details(*person, {right, bottom}, resource, selected_slot_, slot_position_,
                          slot_target_, info, actions);
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
