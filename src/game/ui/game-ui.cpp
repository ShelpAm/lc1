#include "lc1/game/ui/game-ui.hpp"

#include "lc1/game/party/faction.hpp"
#include "lc1/game/party/party.hpp"
#include "lc1/game/resources/catalog.hpp"
#include "lc1/game/resources/resource-stack.hpp"
#include "lc1/game/ui/music-browser.hpp"
#include "lc1/game/world/world-presentation.hpp"
#include "lc1/text/text-layout.hpp"
#include <algorithm>
#include <array>
#include <cassert>
#include <cctype>
#include <cmath>
#include <cstdint>
#include <format>
#include <fstream>
#include <imgui.h>
#include <ranges>
#include <rfl/yaml.hpp>
#include <spdlog/spdlog.h>
#include <utility>

namespace lc1 {
namespace {
constexpr ImVec4 accent{0.83F, 0.69F, 0.43F, 1.0F};
constexpr float default_ui_scale = 1.3F;
constexpr float minimum_ui_scale = 1.0F;
constexpr float maximum_ui_scale = 2.0F;

struct UiSettingsFile {
    float ui_scale;
};

float ui_scale()
{
    return ImGui::GetStyle().FontScaleMain;
}

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

CharacterSnapshot const *party_info(GameUiContext const &info, ImVec2 size,
                                    std::optional<std::uint32_t> &party_id,
                                    std::optional<CharacterId> &character_id)
{
    auto const player_id = info.world.players.at(info.player_id).character_id;
    auto const *player = &info.world.character(player_id);
    auto const *selected = player;
    auto parties = info.world.parties | std::views::filter([player_id](Party const &party) {
                       return party.leader() == player_id || party.has_member(player_id);
                   });
    auto current = std::ranges::find_if(
        parties, [&](Party const &party) { return party_id == party.keyid(); });
    if (current == parties.end())
        current = parties.begin();
    Party const *party = current != parties.end() ? &*current : nullptr;
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
            for (auto const &candidate : parties) {
                auto const label = std::format("Party {}", candidate.keyid());
                if (ImGui::Selectable(label.c_str(), &candidate == party)) {
                    party = &candidate;
                    character_id.reset();
                }
            }
            ImGui::EndCombo();
        }
        resolve_selection();
        if (ImGui::BeginChild("Members", {0, 0})) {
            float const row_height = std::max(25.0F, ImGui::GetFrameHeight());
            for (auto const *person : people) {
                ImGui::PushID(static_cast<int>(person->keyid));
                auto name = person->name;
                if (person == player)
                    name += " (Me)";
                if (party->has_captive(CharacterId{person->keyid}))
                    name += " (Captive)";
                if (ImGui::Selectable(name.c_str(), person == selected, 0, {0, row_height}))
                    selected = person;
                if (CharacterId{person->keyid} == party->leader()) {
                    ImGui::SameLine(ImGui::GetWindowContentRegionMax().x - ImGui::GetFrameHeight());
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

// Resolution-independent inventory artwork shared by rows, tooltips and detail cards.
void item_icon(game::Resource const &resource, ImVec2 origin, float size)
{
    auto *draw = ImGui::GetWindowDrawList();
    auto const p = [=](float x, float y) {
        return ImVec2{origin.x + x * size, origin.y + y * size};
    };
    auto const line = [=](float x, float y, float x2, float y2, ImU32 color, float width = 0.035F) {
        draw->AddLine(p(x, y), p(x2, y2), color, size * width);
    };
    constexpr auto steel = IM_COL32(174, 192, 201, 255);
    constexpr auto edge = IM_COL32(223, 232, 227, 255);
    constexpr auto leather = IM_COL32(143, 92, 51, 255);
    constexpr auto gold = IM_COL32(221, 177, 85, 255);
    draw->AddRectFilled(origin, p(1, 1), IM_COL32(25, 31, 35, 255), size * 0.12F);
    switch (resource.keyid) {
    case 1:
        for (float y : {0.68F, 0.52F, 0.36F}) {
            draw->AddEllipseFilled(p(0.5F, y), {size * 0.29F, size * 0.14F}, gold);
            draw->AddEllipse(p(0.5F, y), {size * 0.23F, size * 0.09F}, IM_COL32(130, 88, 31, 255));
        }
        break;
    case 2:
        for (float y : {0.36F, 0.60F}) {
            line(0.25F, y + 0.1F, 0.7F, y - 0.05F, leather, 0.22F);
            draw->AddCircleFilled(p(0.25F, y + 0.1F), size * 0.11F, gold);
            draw->AddCircle(p(0.25F, y + 0.1F), size * 0.06F, leather);
        }
        break;
    case 3: {
        std::array points{p(0.17F, 0.65F), p(0.32F, 0.3F), p(0.74F, 0.25F), p(0.86F, 0.56F),
                          p(0.69F, 0.75F)};
        draw->AddConvexPolyFilled(points.data(), points.size(), steel);
        line(0.32F, 0.3F, 0.4F, 0.58F, edge);
        line(0.4F, 0.58F, 0.86F, 0.56F, edge);
        break;
    }
    case 4:
        draw->AddCircleFilled(p(0.5F, 0.46F), size * 0.29F, steel);
        draw->AddRectFilled(p(0.21F, 0.43F), p(0.79F, 0.78F), steel, size * 0.04F);
        line(0.29F, 0.51F, 0.71F, 0.51F, IM_COL32(30, 37, 42, 255), 0.08F);
        line(0.5F, 0.24F, 0.5F, 0.71F, edge, 0.05F);
        break;
    case 5: {
        std::array points{p(0.29F, 0.2F), p(0.43F, 0.28F), p(0.57F, 0.28F), p(0.71F, 0.2F),
                          p(0.8F, 0.43F), p(0.68F, 0.5F),  p(0.71F, 0.8F),  p(0.29F, 0.8F),
                          p(0.32F, 0.5F), p(0.2F, 0.43F)};
        draw->AddPolyline(points.data(), points.size(), steel, ImDrawFlags_Closed, size * 0.1F);
        draw->AddRectFilled(p(0.34F, 0.36F), p(0.66F, 0.77F), steel, size * 0.05F);
        line(0.5F, 0.35F, 0.5F, 0.73F, edge);
        break;
    }
    case 6:
        for (float x : {0.22F, 0.54F}) {
            draw->AddRectFilled(p(x, 0.22F), p(x + 0.18F, 0.7F), leather, size * 0.03F);
            draw->AddRectFilled(p(x, 0.61F), p(x + 0.27F, 0.8F), leather, size * 0.05F);
            line(x, 0.31F, x + 0.18F, 0.31F, gold);
        }
        break;
    case 1004:
        draw->PathArcTo(p(0.28F, 0.5F), size * 0.34F, -1.3F, 1.3F);
        draw->PathStroke(leather, 0, size * 0.055F);
        line(0.37F, 0.17F, 0.37F, 0.83F, edge, 0.018F);
        line(0.23F, 0.5F, 0.8F, 0.5F, steel, 0.02F);
        draw->AddTriangleFilled(p(0.86F, 0.5F), p(0.73F, 0.44F), p(0.73F, 0.56F), steel);
        break;
    case 1005:
        line(0.3F, 0.76F, 0.6F, 0.38F, steel, 0.18F);
        line(0.52F, 0.32F, 0.73F, 0.47F, gold, 0.16F);
        line(0.68F, 0.24F, 0.78F, 0.12F, gold, 0.025F);
        break;
    case 1006:
        line(0.37F, 0.84F, 0.58F, 0.38F, leather, 0.11F);
        draw->AddTriangleFilled(p(0.42F, 0.44F), p(0.65F, 0.1F), p(0.75F, 0.47F),
                                IM_COL32(239, 103, 35, 255));
        draw->AddTriangleFilled(p(0.49F, 0.46F), p(0.62F, 0.26F), p(0.68F, 0.48F),
                                IM_COL32(255, 216, 106, 255));
        break;
    default:
        line(0.31F, 0.73F, 0.74F, 0.23F, steel, 0.09F);
        draw->AddTriangleFilled(p(0.7F, 0.24F), p(0.8F, 0.16F), p(0.77F, 0.3F), edge);
        line(0.27F, 0.59F, 0.48F, 0.76F, gold, 0.06F);
        line(0.21F, 0.86F, 0.33F, 0.72F, leather, 0.07F);
        break;
    }
}

void item_summary(game::Resource const &resource, std::uint32_t quantity)
{
    auto const origin = ImGui::GetCursorScreenPos();
    float const available = ImGui::GetContentRegionAvail().x;
    bool const side_by_side = available >= 230.0F;
    float const size = std::min(96.0F, available);
    item_icon(resource, origin, size);
    ImGui::Dummy({size, size});
    if (side_by_side)
        ImGui::SameLine();
    ImGui::BeginGroup();
    ImGui::TextWrapped("%s", resource.name.c_str());
    ImGui::Text("Quantity: %u", quantity);
    ImGui::Text("Each: %.2f kg", resource.weight / 1000.0);
    ImGui::Text("Stack: %.2f kg", quantity * (resource.weight / 1000.0));
    ImGui::EndGroup();
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
                                      std::vector<std::uint32_t> &order,
                                      std::array<char, 64> &search)
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
        float const tab_height = std::max(26.0F, ImGui::GetFrameHeight());
        for (int i = 0; i < static_cast<int>(names.size()); ++i) {
            if (i)
                ImGui::SameLine();
            if (ImGui::Selectable(
                    names[i], category == i, 0,
                    {std::max(1.0F, (size.x - 56.0F * ui_scale()) / 3), tab_height})) {
                category = i;
                selected = nullptr;
                order.clear();
            }
        }
        ImGui::SetNextItemWidth(-1);
        ImGui::InputTextWithHint("##inventory-search", "Search items...", search.data(),
                                 search.size());
        auto items = inventory_items(bag, category, order);
        if (search[0]) {
            std::string_view const query{search.data()};
            std::erase_if(items, [&](auto const &item) {
                auto const &name = game::find_resource(item.keyid)->name;
                return std::search(name.begin(), name.end(), query.begin(), query.end(),
                                   [](unsigned char a, unsigned char b) {
                                       return std::tolower(a) == std::tolower(b);
                                   }) == name.end();
            });
        }
        float const footer = ImGui::GetFrameHeightWithSpacing();
        if (ImGui::BeginChild("Items", {0, -footer}, ImGuiChildFlags_None,
                              ImGuiWindowFlags_AlwaysVerticalScrollbar)) {
            float const scale = ui_scale();
            float const icon_size = 34.0F * scale;
            float const icon_margin = 5.0F * scale;
            float const text_offset = 48.0F * scale;
            float const text_margin = 5.0F * scale;
            float const row_height =
                std::max({54.0F * scale, icon_size + icon_margin * 2.0F,
                          text_margin * 2.0F + ImGui::GetTextLineHeight() * 2.0F});
            for (auto const &item : items) {
                auto const *resource = game::find_resource(item.keyid);
                ImGui::PushID(static_cast<int>(item.keyid));
                auto const position = ImGui::GetCursorScreenPos();
                if (ImGui::Selectable("##item", selected == resource, 0, {0, row_height}))
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
                    item_summary(*resource, item.quantity);
                    ImGui::EndPopup();
                }
                auto *draw = ImGui::GetWindowDrawList();
                item_icon(*resource, {position.x + icon_margin, position.y + icon_margin},
                          icon_size);
                if (ImGui::IsItemHovered() && !ImGui::IsMouseDragging(ImGuiMouseButton_Left)) {
                    ImGui::BeginTooltip();
                    item_summary(*resource, item.quantity);
                    ImGui::EndTooltip();
                }
                draw->AddText({position.x + text_offset, position.y + text_margin},
                              ImGui::GetColorU32(ImGuiCol_Text), resource->name.c_str());
                auto const detail = std::format("x{}   {:.2f} kg", item.quantity,
                                                item.quantity * (resource->weight / 1000.0));
                draw->AddText({position.x + text_offset,
                               position.y + text_margin + ImGui::GetTextLineHeight()},
                              ImGui::GetColorU32(ImGuiCol_Text), detail.c_str());
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

bool draw_character_preview(CharacterSnapshot const &person, game::Resource const *item,
                            bool &preview_item, ImVec2 size, std::uint64_t texture, float &yaw,
                            float &zoom)
{
    bool visible = ImGui::BeginChild("Character preview", size, ImGuiChildFlags_Borders);
    if (visible) {
        if (ImGui::RadioButton("Character", !preview_item))
            preview_item = false;
        ImGui::SameLine();
        ImGui::BeginDisabled(!item);
        if (ImGui::RadioButton("Item", preview_item))
            preview_item = true;
        ImGui::EndDisabled();
        ImGui::Separator();
        ImGui::TextUnformatted(preview_item && item ? item->name.c_str() : person.name.c_str());
        auto const available = ImGui::GetContentRegionAvail();
        float const height =
            std::min(std::max(1.0F, available.y - ImGui::GetFrameHeightWithSpacing()),
                     available.x * (768.0F / 512.0F));
        float const width = height * (512.0F / 768.0F);
        ImGui::SetCursorPosX(ImGui::GetCursorPosX() + (available.x - width) * 0.5F);
        auto const image_origin = ImGui::GetCursorScreenPos();
        if (texture)
            ImGui::Image(texture, {width, height});
        ImGui::SetCursorScreenPos(image_origin);
        ImGui::InvisibleButton("Rotate character", {width, height});
        if (ImGui::IsItemHovered()) {
            if (ImGui::IsItemActive() && ImGui::IsMouseDragging(ImGuiMouseButton_Left))
                yaw = std::remainder(yaw + ImGui::GetIO().MouseDelta.x * 0.6F, 360.0F);
            zoom = std::clamp(zoom + ImGui::GetIO().MouseWheel * 0.08F, 0.7F, 1.5F);
        }
        if (ImGui::Button("Reset view")) {
            yaw = -20;
            zoom = 1;
        }
        ImGui::SameLine();
        ImGui::TextDisabled("Drag to rotate");
    }
    ImGui::EndChild();
    return visible;
}

void equipment_details(CharacterSnapshot const &person, ImVec2 size, game::Resource const *resource,
                       int &slot, float &slot_position, float &slot_target,
                       GameUiContext const &info, GameUiActions &actions)
{
    auto const armor =
        resource ? info.world.equipments.find(resource->keyid) : info.world.equipments.end();
    auto const weapon =
        resource ? info.world.weapons.find(resource->keyid) : info.world.weapons.end();
    if (ImGui::BeginChild("Equipment details", size, ImGuiChildFlags_Borders)) {
        float const scale = ui_scale();
        panel_heading("EQUIPMENT");
        float const gap = ImGui::GetStyle().ItemSpacing.x;
        float const slots_width = 86.0F * scale;
        float const cards_width =
            std::max(100.0F, ImGui::GetContentRegionAvail().x - slots_width - gap);
        float const content_height =
            std::max(160.0F * scale,
                     ImGui::GetContentRegionAvail().y - ImGui::GetFrameHeightWithSpacing() * 2);
        constexpr std::array slot_names{"Head", "Body", "Feet", "Main hand"};
        float const slot_height = 58.0F * scale;
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
            float const step = slot_height + ImGui::GetStyle().ItemSpacing.y;
            if (ImGui::BeginChild("Slot wheel", {0, step * 3}, ImGuiChildFlags_None,
                                  ImGuiWindowFlags_NoScrollbar |
                                      ImGuiWindowFlags_NoScrollWithMouse)) {
                auto const origin = ImGui::GetCursorPos();
                int const center = static_cast<int>(std::floor(slot_position));
                int const count = static_cast<int>(slot_names.size());
                for (int position = center - 2; position <= center + 3; ++position) {
                    int const i = (position % count + count) % count;
                    float const y = origin.y + (position - slot_position + 1) * step;
                    if (y + slot_height <= origin.y || y >= origin.y + step * 3)
                        continue;
                    ImGui::SetCursorPos({origin.x, y});
                    ImGui::PushID(position);
                    if (ImGui::Selectable(slot_names[i], slot == i, 0, {0, slot_height})) {
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
            float const card_height = std::clamp((ImGui::GetContentRegionAvail().y - gap) * 0.34F,
                                                 125.0F * scale, 145.0F * scale);
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
            if (resource) {
                if (ImGui::BeginChild("Selected equipment", {0, 0}, ImGuiChildFlags_Borders)) {
                    ImGui::TextColored(accent, "SELECTED ITEM");
                    item_summary(*resource, person.bag.quantity_of(resource->keyid));
                    if (resource->keyid == game::wood.keyid)
                        ImGui::TextWrapped("Timber for forging and crafting.");
                    else if (resource->keyid == game::pig_iron.keyid)
                        ImGui::TextWrapped("Raw iron for the blacksmith's forge.");
                    else if (resource->keyid == game::moneta.keyid)
                        ImGui::TextWrapped("Coins used for goods and services.");
                    if (armor != info.world.equipments.end()) {
                        ImGui::Text("Slot: %s",
                                    std::string{to_string(armor->second.type())}.c_str());
                        ImGui::Text("Armor: %.1f", armor->second.property().armor);
                    }
                    if (weapon != info.world.weapons.end()) {
                        ImGui::TextUnformatted("Slot: Main hand");
                        if (weapon->second.action() == WeaponAction::ToggleLight)
                            ImGui::TextUnformatted("Left click: toggle light");
                        else if (weapon->second.action() == WeaponAction::Torch)
                            ImGui::TextWrapped("Burns while held. Lights the surrounding area.");
                        else {
                            ImGui::Text("Attack: %.1f", weapon->second.property().attack);
                            ImGui::Text("Range: %.1f m", weapon->second.property().range);
                        }
                    }
                }
                ImGui::EndChild();
            }
        }
        ImGui::EndChild();
        auto const player_id = info.world.players.at(info.player_id).character_id;
        bool controllable = person.keyid == player_id.value;
        for (auto const &party : info.world.parties)
            if (party.leader() == player_id && party.has_member(CharacterId{person.keyid}))
                controllable = std::ranges::none_of(info.world.players, [&](auto const &entry) {
                    return entry.second.character_id == CharacterId{person.keyid};
                });
        controllable = controllable && !person.dead && !info.world.character(player_id).dead;
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

// Layer-based volumetric cloud controls. Values are clamped to what the renderer
// validates at registration: an out-of-range edit must not turn into a fatal error on
// the next frame.
void clouds(lc1::CloudSettings *settings)
{
    if (settings == nullptr)
        return;
    auto &clouds = *settings;
    if (!ImGui::CollapsingHeader("Volumetric clouds"))
        return;
    ImGui::Checkbox("Enabled", &clouds.enabled);
    ImGui::SetNextItemWidth(180.0F * ui_scale());
    ImGui::SliderFloat("Coverage", &clouds.coverage, 0.02F, 1.0F, "%.2f",
                       ImGuiSliderFlags_AlwaysClamp);
    ImGui::SetNextItemWidth(180.0F * ui_scale());
    ImGui::SliderFloat("Density", &clouds.density, 0.0F, 3.0F, "%.2f",
                       ImGuiSliderFlags_AlwaysClamp);
    ImGui::SetNextItemWidth(180.0F * ui_scale());
    ImGui::SliderFloat("Brightness", &clouds.radiance_scale, 0.5F, 8.0F, "x%.1f",
                       ImGuiSliderFlags_AlwaysClamp);
    ImGui::SetNextItemWidth(180.0F * ui_scale());
    ImGui::SliderFloat("Cloud bottom", &clouds.bottom, 200.0F, 7900.0F, "%.0f m",
                       ImGuiSliderFlags_AlwaysClamp);
    ImGui::SetNextItemWidth(180.0F * ui_scale());
    ImGui::SliderFloat("Cloud top", &clouds.top, 300.0F, 8000.0F, "%.0f m",
                       ImGuiSliderFlags_AlwaysClamp);
    // The renderer requires a layer with thickness; keep the two fields ordered here
    // instead of letting the next registration fail.
    clouds.top = std::max(clouds.top, clouds.bottom + 100.0F);
    ImGui::SetNextItemWidth(180.0F * ui_scale());
    ImGui::SliderFloat("Shape scale", &clouds.noise_scale, 1000.0F, 120000.0F, "%.0f m",
                       ImGuiSliderFlags_Logarithmic | ImGuiSliderFlags_AlwaysClamp);
    ImGui::SetNextItemWidth(180.0F * ui_scale());
    ImGui::SliderFloat("Detail", &clouds.detail_strength, 0.0F, 1.0F, "%.2f",
                       ImGuiSliderFlags_AlwaysClamp);
    ImGui::SetNextItemWidth(180.0F * ui_scale());
    ImGui::SliderFloat("Extinction", &clouds.extinction, 0.002F, 0.08F, "%.3f /m",
                       ImGuiSliderFlags_Logarithmic | ImGuiSliderFlags_AlwaysClamp);
    ImGui::SetNextItemWidth(180.0F * ui_scale());
    ImGui::SliderFloat("Anisotropy", &clouds.anisotropy, -0.9F, 0.9F, "%.2f",
                       ImGuiSliderFlags_AlwaysClamp);
    ImGui::SetNextItemWidth(180.0F * ui_scale());
    ImGui::SliderFloat3("Wind (m/s)", &clouds.wind_velocity.x, -40.0F, 40.0F, "%.1f",
                        ImGuiSliderFlags_AlwaysClamp);
    int view_steps = static_cast<int>(clouds.view_steps);
    ImGui::SetNextItemWidth(180.0F * ui_scale());
    if (ImGui::SliderInt("View steps", &view_steps, 8, 256, "%d", ImGuiSliderFlags_AlwaysClamp))
        clouds.view_steps = static_cast<std::uint32_t>(view_steps);
    int light_steps = static_cast<int>(clouds.light_steps);
    ImGui::SetNextItemWidth(180.0F * ui_scale());
    if (ImGui::SliderInt("Light steps", &light_steps, 1, 16, "%d", ImGuiSliderFlags_AlwaysClamp))
        clouds.light_steps = static_cast<std::uint32_t>(light_steps);
    constexpr std::array<char const *, 5> const debug_names{"Off", "Density", "Sun transmittance",
                                                            "View transmittance", "Step count"};
    int debug = static_cast<int>(clouds.debug);
    ImGui::SetNextItemWidth(180.0F * ui_scale());
    if (ImGui::Combo("Debug view", &debug, debug_names.data(),
                     static_cast<int>(debug_names.size())))
        clouds.debug = static_cast<lc1::CloudSettings::Debug>(debug);
}

} // namespace

GameUi::GameUi(std::filesystem::path settings_file) : settings_file_(std::move(settings_file))
{
    if (settings_file_.empty())
        return;

    auto stream = std::ifstream{settings_file_};
    if (!stream) {
        persist_ui_scale();
        return;
    }

    auto settings = rfl::yaml::read<UiSettingsFile, rfl::NoOptionals>(stream);
    if (!settings) {
        spdlog::warn("[ui] cannot read {}: {}", settings_file_.string(), settings.error().what());
        return;
    }
    if (!std::isfinite(settings->ui_scale)) {
        spdlog::warn("[ui] ignoring non-finite scale in {}", settings_file_.string());
        return;
    }
    ui_scale_ = std::clamp(settings->ui_scale, minimum_ui_scale, maximum_ui_scale);
}

void GameUi::persist_ui_scale() const
{
    if (settings_file_.empty())
        return;

    std::error_code error;
    auto const parent = settings_file_.parent_path();
    if (!parent.empty())
        std::filesystem::create_directories(parent, error);
    if (error) {
        spdlog::warn("[ui] cannot create {}: {}", parent.string(), error.message());
        return;
    }

    auto stream = std::ofstream{settings_file_, std::ios::binary | std::ios::trunc};
    if (!stream) {
        spdlog::warn("[ui] cannot write {}", settings_file_.string());
        return;
    }
    rfl::yaml::write(UiSettingsFile{.ui_scale = ui_scale_}, stream);
    if (!stream)
        spdlog::warn("[ui] failed while writing {}", settings_file_.string());
}

void GameUi::open_save(std::string_view name)
{
    save_name_.fill('\0');
    std::ranges::copy(name.substr(0, save_name_.size() - 1), save_name_.begin());
    save_open_ = true;
    load_open_ = false;
}

void GameUi::toggle_menu()
{
    // One key both opens and closes the overlay; opening it never quits.
    if (state_ == State::InGame) {
        save_open_ = false;
        load_open_ = false;
        player_info_opened_ = false;
        state_ = State::MainMenu;
    }
    else {
        state_ = State::InGame;
    }
}

GameUiActions GameUi::update(GameUiContext const &info)
{
    character_preview_.reset();
    GameUiActions actions;

    // Rebuild from the unscaled defaults so changing the slider never accumulates rounding error.
    ImGuiStyle scaled_style{};
    std::ranges::copy(ImGui::GetStyle().Colors, scaled_style.Colors);
    scaled_style.ScaleAllSizes(ui_scale_);
    scaled_style.FontScaleMain = ui_scale_;
    ImGui::GetStyle() = scaled_style;

    // The debug page belongs to the overlay: during play the screen shows the game, and
    // Esc brings both the page and the menu back together.
    if (state_ != State::InGame) {
        float const scale = ui_scale();
        ImGui::SetNextWindowSize({380.0F * scale, 710.0F * scale}, ImGuiCond_FirstUseEver);
        ImGui::SetNextWindowSizeConstraints({300.0F * scale, 0.0F}, {FLT_MAX, FLT_MAX});
        ImGui::Begin("lc1 debug info");
        auto const [width, height] = info.window.framebuffer_extent();
        ImGui::TextUnformatted(std::format("Window size: {}x{}", width, height).c_str());
        ImGui::TextUnformatted(std::format("FPS: {}", ImGui::GetIO().Framerate).c_str());
        ImGui::TextUnformatted(
            std::format("Lighting: {}", info.presentation.lighting_name()).c_str());
        ImGui::Text("Exposure: %.2f", info.presentation.output().exposure);
        auto const minutes = static_cast<int>(info.presentation.time_of_day() * 60.0);
        ImGui::Text("Time: %02d:%02d", minutes / 60, minutes % 60);
        ImGui::TextUnformatted("Time rate");
        ImGui::SameLine();
        float rate = info.presentation.time_rate();
        ImGui::SetNextItemWidth(90.0F * ui_scale_);
        bool changed = ImGui::InputFloat("##time-rate-input", &rate, 0, 0, "%.2f");
        ImGui::SameLine();
        ImGui::SetNextItemWidth(180.0F * ui_scale_);
        changed |= ImGui::SliderFloat("##time-rate-slider", &rate, 0.01F, 100.0F, "%.2fx",
                                      ImGuiSliderFlags_Logarithmic | ImGuiSliderFlags_AlwaysClamp);
        if (changed && std::isfinite(rate))
            actions.time_rate = std::clamp(rate, 0.01F, 100.0F);
        float flight_speed = info.debug_flight_speed;
        if (ImGui::SliderFloat("Flight speed", &flight_speed, 0.1F, 100.0F, "%.2fx",
                               ImGuiSliderFlags_Logarithmic | ImGuiSliderFlags_AlwaysClamp))
            actions.debug_flight_speed = flight_speed;
        int density = info.presentation.grass_density();
        auto const density_label = std::format("{}%%", density * 25);
        if (ImGui::SliderInt("Grass density", &density, 0, 8, density_label.c_str(),
                             ImGuiSliderFlags_AlwaysClamp))
            actions.grass_density = std::clamp(density, 0, 8);
        if (info.fog) {
            auto &fog = *info.fog;
            ImGui::Checkbox("Fog", &fog.enabled);
            ImGui::BeginDisabled(!fog.enabled);
            float start = fog.start;
            float end = fog.end;
            bool fog_changed = ImGui::DragFloat("Fog start (m)", &start, 10, 0, end - 1, "%.0f",
                                                ImGuiSliderFlags_AlwaysClamp);
            fog_changed |= ImGui::DragFloat("Fog end (m)", &end, 25, start + 1, 1000000, "%.0f",
                                            ImGuiSliderFlags_AlwaysClamp);
            if (fog_changed && std::isfinite(start) && std::isfinite(end) && start >= 0 &&
                end > start) {
                fog.start = start;
                fog.end = end;
            }
            ImGui::EndDisabled();
        }
        clouds(info.clouds);
        if (ImGui::Button("Save game (F6)"))
            open_save(info.current_save);
        if (ImGui::Button("Load game")) {
            load_open_ = true;
            save_open_ = false;
            actions.refresh_saves = true;
        }
        ImGui::TextUnformatted(save_status_.c_str());
        ImGui::TextUnformatted(
            std::format("Showcase motion: {}", info.presentation.showcase_animated() ? "on" : "off")
                .c_str());
        auto const &interaction_target = info.world.players.at(info.player_id).interaction_target;
        ImGui::TextUnformatted(std::format("interaction_target: {}", interaction_target.empty()
                                                                         ? "not found"
                                                                         : interaction_target)
                                   .c_str());

        if (ImGui::Button("Close (Esc)"))
            state_ = State::InGame;

        info.music.controls();

        ImGui::End();
    }
    info.music.update();

    constexpr ImGuiWindowFlags menu_flags =
        ImGuiWindowFlags_AlwaysAutoResize | ImGuiWindowFlags_NoCollapse | ImGuiWindowFlags_NoResize;
    switch (state_) {
    case State::MainMenu:
        ImGui::Begin("lc1 main menu##menu", nullptr, menu_flags);
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
    case State::InSettings: {
        ImGui::Begin("lc1 settings##menu", nullptr, menu_flags);
        if (ImGui::SliderFloat("Volume", &volume_, 0, 100))
            actions.volume = volume_;
        float ui_scale_percent = ui_scale_ * 100.0F;
        ImGui::SetNextItemWidth(180.0F * ui_scale_);
        if (ImGui::SliderFloat("UI scale", &ui_scale_percent, 100.0F, 200.0F, "%.0f%%",
                               ImGuiSliderFlags_AlwaysClamp))
            ui_scale_ = std::clamp(ui_scale_percent / 100.0F, minimum_ui_scale, maximum_ui_scale);
        if (ImGui::IsItemDeactivatedAfterEdit())
            persist_ui_scale();
        if (ImGui::Button("Reset scale")) {
            ui_scale_ = default_ui_scale;
            persist_ui_scale();
        }
        if (ImGui::Button("Back to Main Menu"))
            state_ = State::MainMenu;
        ImGui::End();
        break;
    }
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

    if (player_info_opened_ && !save_open_ && !load_open_)
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

    if (world.player(observer).pose.debug_flying)
        labels.push_back(ScreenTextItem{.text = "Flight | Space: up | Ctrl: down | P: off",
                                        .position = {24.0F, 138.0F},
                                        .line_height = 22.0F});

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
    // A gameplay panel: the pause overlay closes it, and B opens it again in game.
    if (state_ == State::InGame && !save_open_ && !load_open_)
        player_info_opened_ = !player_info_opened_;
}

void GameUi::player_info(GameUiContext const &info, GameUiActions &actions)
{
    float const scale = ui_scale();
    auto const *viewport = ImGui::GetMainViewport();
    ImVec2 const max_size{std::max(1.0F, viewport->WorkSize.x - 24.0F * scale),
                          std::max(1.0F, viewport->WorkSize.y - 24.0F * scale)};
    ImGui::SetNextWindowPos({viewport->WorkPos.x + viewport->WorkSize.x * 0.5F,
                             viewport->WorkPos.y + viewport->WorkSize.y * 0.5F},
                            ImGuiCond_FirstUseEver, {0.5F, 0.5F});
    ImGui::SetNextWindowSize(
        {std::min(1240.0F * scale, max_size.x), std::min(780.0F * scale, max_size.y)},
        ImGuiCond_FirstUseEver);
    ImGui::SetNextWindowSizeConstraints(
        {std::min(760.0F * scale, max_size.x), std::min(580.0F * scale, max_size.y)}, max_size);
    ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, {16 * scale, 16 * scale});
    ImGui::PushStyleVar(ImGuiStyleVar_ChildRounding, 5.0F * scale);
    ImGui::PushStyleVar(ImGuiStyleVar_ItemSpacing, {12 * scale, 8 * scale});
    ImGui::PushStyleColor(ImGuiCol_WindowBg, IM_COL32(24, 28, 31, 255));
    ImGui::PushStyleColor(ImGuiCol_ChildBg, IM_COL32(35, 40, 43, 255));
    ImGui::PushStyleColor(ImGuiCol_Header, IM_COL32(104, 86, 55, 255));
    if (ImGui::Begin("Character & inventory", &player_info_opened_,
                     ImGuiWindowFlags_HorizontalScrollbar)) {
        auto const &controlled = info.world.player(info.player_id);
        ImGui::SetNextItemWidth(260.0F * ui_scale());
        if (ImGui::BeginCombo("Control", controlled.name.c_str())) {
            for (auto const &candidate : info.world.characters) {
                bool const occupied =
                    std::ranges::any_of(info.world.players, [&](auto const &entry) {
                        return entry.second.character_id == CharacterId{candidate.keyid} &&
                               candidate.keyid != controlled.keyid;
                    });
                ImGui::BeginDisabled(candidate.dead || occupied);
                auto const label = std::format("{}##{}", candidate.name, candidate.keyid);
                if (ImGui::Selectable(label.c_str(), candidate.keyid == controlled.keyid))
                    actions.control_target = CharacterId{candidate.keyid};
                ImGui::EndDisabled();
            }
            ImGui::EndCombo();
        }
        if (auto giant = std::ranges::find(info.world.characters, 10U, &CharacterSnapshot::keyid);
            giant != info.world.characters.end() &&
            selected_character_.value_or(CharacterId{controlled.keyid}) ==
                CharacterId{giant->keyid}) {
            auto proportions = giant->proportions;
            ImGui::SetNextItemWidth(220);
            bool changed =
                ImGui::SliderFloat("Giant body scale", &proportions.scale, 0.001F, 100, "%.3fx",
                                   ImGuiSliderFlags_Logarithmic | ImGuiSliderFlags_AlwaysClamp);
            ImGui::SameLine();
            ImGui::SetNextItemWidth(220);
            changed |= ImGui::SliderFloat("Giant leg height", &proportions.leg_height_scale, 0.5F,
                                          3, "%.2fx", ImGuiSliderFlags_AlwaysClamp);
            if (changed)
                actions.proportions = ProportionsRequest{CharacterId{giant->keyid}, proportions};
        }
        auto available = ImGui::GetContentRegionAvail();
        available.x = std::max(720.0F * scale, available.x);
        float const gap = ImGui::GetStyle().ItemSpacing.x;
        float const left = (available.x - gap * 2) * 0.34F;
        float const middle = (available.x - gap * 2) * 0.28F;
        float const right = available.x - gap * 2 - left - middle;
        float const top = std::clamp(available.y * 0.29F, 150.0F * scale, 210.0F * scale);
        float const bottom =
            std::max(240.0F * scale, available.y - top - ImGui::GetStyle().ItemSpacing.y);
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
        auto const *resource =
            inventory_panel(*person, {left, bottom}, inventory_category_, selected_resource_,
                            inventory_order_, inventory_search_);
        if (!resource)
            preview_item_ = false;
        if (resource && previous_resource != selected_resource_) {
            preview_item_ = true;
            preview_yaw_ = -20;
            preview_zoom_ = 1;
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
        if (draw_character_preview(*person, resource, preview_item_, {middle, bottom},
                                   info.character_preview_texture, preview_yaw_, preview_zoom_))
            character_preview_ = CharacterPreviewRequest{
                CharacterId{person->keyid}, preview_yaw_, preview_zoom_,
                preview_item_ && resource ? std::optional{resource->keyid} : std::nullopt};
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

std::optional<CharacterPreviewRequest> GameUi::character_preview() const
{
    return character_preview_;
}

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
