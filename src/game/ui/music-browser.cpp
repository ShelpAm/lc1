#include "lc1/game/ui/music-browser.hpp"
#include "lc1/error.hpp"
#include <algorithm>
#include <cctype>
#include <cstdlib>
#include <format>
#include <imgui.h>
#include <spdlog/spdlog.h>
#include <utility>

namespace lc1 {
namespace {
// The debug UI is the only place that needs a filesystem location before the user picks one.
// Prefer the platform's home directory; fall back to the current working directory.
std::filesystem::path default_music_directory()
{
    if (char const *const home = std::getenv("HOME"); home != nullptr && *home != '\0')
        return home;
    if (char const *const profile = std::getenv("USERPROFILE");
        profile != nullptr && *profile != '\0')
        return profile;

    std::error_code error;
    auto current = std::filesystem::current_path(error);
    return error ? std::filesystem::path{} : current;
}

bool is_supported_audio_file(std::filesystem::path const &path)
{
    auto extension = path.extension().string();
    for (char &character : extension)
        character = static_cast<char>(std::tolower(static_cast<unsigned char>(character)));
    return extension == ".wav" || extension == ".mp3" || extension == ".ogg";
}

float ui_scale()
{
    return ImGui::GetStyle().FontScaleMain;
}

} // namespace

MusicBrowser::MusicBrowser(audio::AudioContext const &context)
    : audio_context_(&context), audio_source_(context),
      music_browser_path_(default_music_directory())
{
    // Preserve the prototype's optional startup track; failures appear in the browser.
    play_music("/home/shelpam/Music/Adele - Make You Feel My Love.mp3");
}

void MusicBrowser::controls()
{
    ImGui::SeparatorText("Music");
    auto const music_name = current_music_path_.filename().string();
    ImGui::Text("Now playing: %s", music_name.empty() ? "(none)" : music_name.c_str());
    if (!current_music_path_.empty() && ImGui::IsItemHovered())
        ImGui::SetTooltip("%s", current_music_path_.string().c_str());
    if (ImGui::Button("Choose music..."))
        music_browser_open_ = true;
    if (audio_music_) {
        ImGui::SameLine();
        if (ImGui::Button("Stop"))
            audio_source_.stop();
    }
    if (!music_error_.empty())
        ImGui::TextWrapped("Music error: %s", music_error_.c_str());
}

void MusicBrowser::update()
{
    if (!music_browser_open_)
        return;

    if (!music_browser_loaded_) {
        refresh_music_browser();
        music_browser_loaded_ = true;
    }

    float const scale = ui_scale();
    ImGui::SetNextWindowSize({480.0F * scale, 360.0F * scale}, ImGuiCond_FirstUseEver);
    ImGui::SetNextWindowSizeConstraints({320.0F * scale, 240.0F * scale}, {FLT_MAX, FLT_MAX});
    if (!ImGui::Begin("Choose music", &music_browser_open_)) {
        ImGui::End();
        return;
    }

    bool const has_parent = music_browser_path_.has_parent_path() &&
                            music_browser_path_.parent_path() != music_browser_path_;
    if (!has_parent)
        ImGui::BeginDisabled();
    if (ImGui::Button("Up"))
        navigate(music_browser_path_.parent_path());
    if (!has_parent)
        ImGui::EndDisabled();

    ImGui::SameLine();
    if (ImGui::Button("Home"))
        navigate(default_music_directory());
    ImGui::SameLine();
    if (ImGui::Button("Refresh"))
        refresh_music_browser();

    if (music_browser_path_.empty()) {
        ImGui::TextUnformatted("(no directory)");
    }
    else {
        ImGui::TextWrapped("%s", music_browser_path_.string().c_str());
    }

    if (!music_error_.empty())
        ImGui::TextWrapped("%s", music_error_.c_str());

    ImGui::Separator();
    if (ImGui::BeginChild("music files", ImVec2{0.0F, 0.0F}, ImGuiChildFlags_Borders)) {
        bool navigated = false;
        for (std::size_t index = 0; index < music_browser_entries_.size(); ++index) {
            auto const &entry = music_browser_entries_[index];
            std::string const display_name = entry.path.filename().string();
            std::string const label =
                entry.is_directory ? std::format("[dir] {}", display_name) : display_name;

            ImGui::PushID(static_cast<int>(index));
            if (ImGui::Selectable(label.c_str())) {
                if (entry.is_directory) {
                    navigate(entry.path);
                    navigated = true;
                }
                else {
                    play_music(entry.path);
                }
            }
            ImGui::PopID();
            if (navigated)
                break;
        }
    }
    ImGui::EndChild();

    ImGui::End();
}

void MusicBrowser::navigate(std::filesystem::path path)
{
    music_browser_path_ = std::move(path);
    refresh_music_browser();
}

void MusicBrowser::refresh_music_browser()
{
    music_browser_entries_.clear();
    music_error_.clear();

    if (music_browser_path_.empty()) {
        music_error_ = "No directory selected";
        return;
    }

    std::error_code error;
    std::filesystem::directory_iterator iterator{
        music_browser_path_,
        std::filesystem::directory_options::skip_permission_denied,
        error,
    };
    if (error) {
        music_error_ =
            std::format("Cannot open {}: {}", music_browser_path_.string(), error.message());
        return;
    }

    std::filesystem::directory_iterator const end;
    while (iterator != end) {
        auto const &entry = *iterator;
        std::error_code type_error;
        bool const directory = entry.is_directory(type_error);
        if (!type_error) {
            if (directory || is_supported_audio_file(entry.path()))
                music_browser_entries_.push_back({.path = entry.path(), .is_directory = directory});
        }

        iterator.increment(error);
        if (error) {
            music_error_ =
                std::format("Cannot read {}: {}", music_browser_path_.string(), error.message());
            break;
        }
    }

    std::ranges::sort(music_browser_entries_,
                      [](MusicBrowserEntry const &left, MusicBrowserEntry const &right) {
                          if (left.is_directory != right.is_directory)
                              return left.is_directory;
                          return left.path.filename() < right.path.filename();
                      });
}

void MusicBrowser::play_music(std::filesystem::path const &path)
{
    try {
        audio::Sound music = audio::Sound::load_from_file(*audio_context_, path);
        audio_source_.play(music);
        audio_music_ = std::move(music);
        current_music_path_ = path;
        music_error_.clear();
        spdlog::info("[music] playing {}", path.string());
    }
    catch (Error const &error) {
        music_error_ = error.what();
        spdlog::warn("[music] cannot play {}: {}", path.string(), error.diagnostic());
    }
    catch (std::exception const &error) {
        music_error_ = error.what();
        spdlog::warn("[music] cannot play {}: {}", path.string(), error.what());
    }
}

} // namespace lc1
