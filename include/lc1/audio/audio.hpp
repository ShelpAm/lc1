#pragma once

#include <AL/al.h>

#include <filesystem>
#include <memory>

namespace lc1::audio {

namespace detail {
struct AudioContextState;
struct SoundBuffer;
} // namespace detail

// Owns the OpenAL device and context and must outlive every Listener, Sound and Source.
class AudioContext {
  public:
    AudioContext();
    ~AudioContext();

    AudioContext(AudioContext const &) = delete;
    AudioContext &operator=(AudioContext const &) = delete;
    AudioContext(AudioContext &&) = delete;
    AudioContext &operator=(AudioContext &&) = delete;

  private:
    std::unique_ptr<detail::AudioContextState> state_;

    friend class Listener;
    friend class Sound;
    friend class Source;
};

// Lightweight OpenAL listener state. Borrows its AudioContext.
class Listener {
  public:
    explicit Listener(AudioContext const &context) noexcept;

    void set_position(float x, float y, float z);
    void set_orientation(float forward_x, float forward_y, float forward_z, float up_x, float up_y,
                         float up_z);
    // Master gain applied to the whole context, on top of each Source's own gain.
    void set_gain(float gain);

  private:
    detail::AudioContextState *context_ = nullptr; // Borrowed; the AudioContext outlives this.
};

// A shareable, immutable decoded audio resource backed by an OpenAL buffer.
class Sound {
  public:
    [[nodiscard]] static Sound load_from_file(AudioContext const &context,
                                              std::filesystem::path const &path);

    Sound() = default;

    [[nodiscard]] explicit operator bool() const noexcept;

  private:
    explicit Sound(std::shared_ptr<detail::SoundBuffer> buffer) noexcept;

    std::shared_ptr<detail::SoundBuffer> buffer_;

    friend class Source;
};

// An individual OpenAL source. It is movable, but not copyable.
class Source {
  public:
    explicit Source(AudioContext const &context);
    ~Source() noexcept;

    Source(Source const &) = delete;
    Source &operator=(Source const &) = delete;
    Source(Source &&other) noexcept;
    Source &operator=(Source &&other) noexcept;

    void play(Sound const &sound);
    void stop();
    void pause();

    void set_looping(bool looping);
    void set_gain(float gain);
    void set_position(float x, float y, float z);

    [[nodiscard]] bool is_playing() const;

  private:
    void reset() noexcept;
    void require_valid() const;

    detail::AudioContextState *context_ = nullptr; // Borrowed; the AudioContext outlives this.
    ALuint source_{};
    Sound sound_; // Keeps the bound buffer alive while this source references it.
};

} // namespace lc1::audio
