#include "lc1/audio/audio.hpp"

#define DR_MP3_IMPLEMENTATION
#define DR_WAV_IMPLEMENTATION

#include <AL/alc.h>
#include <dr_mp3.h>
#include <dr_wav.h>
#include <stb_vorbis.c>

#include <array>
#include <cctype>
#include <cmath>
#include <cstdint>
#include <cstdlib>
#include <limits>
#include <memory>
#include <stdexcept>
#include <string>
#include <utility>

namespace lc1::audio {
namespace {

void clear_al_errors() noexcept
{
    while (alGetError() != AL_NO_ERROR) {
    }
}

void check_al(char const *operation)
{
    ALenum const error = alGetError();
    if (error == AL_NO_ERROR) {
        return;
    }

    ALchar const *const description = alGetString(error);
    throw std::runtime_error{std::string{operation} + " failed: " +
                             (description != nullptr ? description : "unknown OpenAL error")};
}

void require_finite(float value, char const *name)
{
    if (!std::isfinite(value)) {
        throw std::invalid_argument{std::string{name} + " must be finite"};
    }
}

struct PcmDeleter {
    void (*free)(void *) = nullptr;

    void operator()(void *pointer) const noexcept { free(pointer); }
};

using PcmBuffer = std::unique_ptr<void, PcmDeleter>;

struct DecodedAudio {
    PcmBuffer samples;
    std::uint32_t channels{};
    std::uint32_t sample_rate{};
    std::uint64_t frame_count{};
};

void free_wav_pcm(void *samples) noexcept
{
    drwav_free(samples, nullptr);
}

void free_mp3_pcm(void *samples) noexcept
{
    drmp3_free(samples, nullptr);
}

void free_ogg_pcm(void *samples) noexcept
{
    std::free(samples);
}

[[nodiscard]] DecodedAudio read_wav(std::filesystem::path const &path)
{
    auto const filename = path.string();
    unsigned int channels = 0;
    unsigned int sample_rate = 0;
    drwav_uint64 frame_count = 0;
    drwav_int16 *const samples = drwav_open_file_and_read_pcm_frames_s16(
        filename.c_str(), &channels, &sample_rate, &frame_count, nullptr);
    if (samples == nullptr) {
        throw std::runtime_error{"Failed to decode WAV file: " + filename};
    }

    return DecodedAudio{
        .samples = PcmBuffer{samples, PcmDeleter{&free_wav_pcm}},
        .channels = channels,
        .sample_rate = sample_rate,
        .frame_count = frame_count,
    };
}

[[nodiscard]] DecodedAudio read_mp3(std::filesystem::path const &path)
{
    auto const filename = path.string();
    drmp3_config config{};
    drmp3_uint64 frame_count = 0;
    drmp3_int16 *const samples =
        drmp3_open_file_and_read_pcm_frames_s16(filename.c_str(), &config, &frame_count, nullptr);
    if (samples == nullptr) {
        throw std::runtime_error{"Failed to decode MP3 file: " + filename};
    }

    return DecodedAudio{
        .samples = PcmBuffer{samples, PcmDeleter{&free_mp3_pcm}},
        .channels = config.channels,
        .sample_rate = config.sampleRate,
        .frame_count = frame_count,
    };
}

[[nodiscard]] DecodedAudio read_ogg(std::filesystem::path const &path)
{
    auto const filename = path.string();
    int channels = 0;
    int sample_rate = 0;
    short *samples = nullptr;
    int const frame_count =
        stb_vorbis_decode_filename(filename.c_str(), &channels, &sample_rate, &samples);
    if (frame_count < 0) {
        throw std::runtime_error{"Failed to decode OGG file: " + filename};
    }

    return DecodedAudio{
        .samples = PcmBuffer{samples, PcmDeleter{&free_ogg_pcm}},
        .channels = static_cast<std::uint32_t>(channels),
        .sample_rate = static_cast<std::uint32_t>(sample_rate),
        .frame_count = static_cast<std::uint64_t>(frame_count),
    };
}

[[nodiscard]] DecodedAudio read_audio(std::filesystem::path const &path)
{
    auto extension = path.extension().string();
    for (char &character : extension) {
        character = static_cast<char>(std::tolower(static_cast<unsigned char>(character)));
    }

    if (extension == ".wav") {
        return read_wav(path);
    }
    if (extension == ".mp3") {
        return read_mp3(path);
    }
    if (extension == ".ogg") {
        return read_ogg(path);
    }
    throw std::runtime_error{"Unsupported audio file format: " + path.string()};
}

} // namespace

namespace detail {

struct AudioContextState {
    ALCdevice *device{};
    ALCcontext *context{};

    AudioContextState()
    {
        device = alcOpenDevice(nullptr);
        if (device == nullptr) {
            throw std::runtime_error{"Failed to open the default OpenAL device"};
        }

        context = alcCreateContext(device, nullptr);
        if (context == nullptr) {
            alcCloseDevice(device);
            device = nullptr;
            throw std::runtime_error{"Failed to create an OpenAL context"};
        }

        if (alcMakeContextCurrent(context) != ALC_TRUE) {
            alcDestroyContext(context);
            context = nullptr;
            alcCloseDevice(device);
            device = nullptr;
            throw std::runtime_error{"Failed to make the OpenAL context current"};
        }
    }

    ~AudioContextState() noexcept
    {
        if (context != nullptr) {
            if (alcGetCurrentContext() == context) {
                (void)alcMakeContextCurrent(nullptr);
            }
            alcDestroyContext(context);
            context = nullptr;
        }

        if (device != nullptr) {
            (void)alcCloseDevice(device);
            device = nullptr;
        }
    }

    void make_current() const
    {
        if (alcGetCurrentContext() == context) {
            return;
        }

        if (alcMakeContextCurrent(context) != ALC_TRUE) {
            ALCenum const error = alcGetError(device);
            throw std::runtime_error{"Failed to make the OpenAL context current (ALC error " +
                                     std::to_string(error) + ")"};
        }
    }
};

struct SoundBuffer {
    explicit SoundBuffer(AudioContextState *owner) noexcept : context{owner} {}

    ~SoundBuffer() noexcept
    {
        if (id == 0 || context == nullptr) {
            return;
        }

        try {
            context->make_current();
            clear_al_errors();
            alDeleteBuffers(1, &id);
        }
        catch (...) {
            // Destructors must not throw. The borrowed AudioContext must still be alive.
        }
    }

    AudioContextState *context = nullptr; // Borrowed; the AudioContext outlives every Sound.
    ALuint id{};
};

} // namespace detail

AudioContext::AudioContext() : state_{std::make_unique<detail::AudioContextState>()}
{
}

AudioContext::~AudioContext() = default;

Listener::Listener(AudioContext const &context) noexcept : context_{context.state_.get()}
{
}

void Listener::set_gain(float gain)
{
    require_finite(gain, "gain");
    if (gain < 0.0F) {
        throw std::invalid_argument{"gain must be non-negative"};
    }

    context_->make_current();
    clear_al_errors();
    alListenerf(AL_GAIN, gain);
    check_al("alListenerf(AL_GAIN)");
}

void Listener::set_position(float x, float y, float z)
{
    require_finite(x, "x");
    require_finite(y, "y");
    require_finite(z, "z");

    context_->make_current();
    clear_al_errors();
    alListener3f(AL_POSITION, x, y, z);
    check_al("alListener3f(AL_POSITION)");
}

void Listener::set_orientation(float forward_x, float forward_y, float forward_z, float up_x,
                               float up_y, float up_z)
{
    require_finite(forward_x, "forward_x");
    require_finite(forward_y, "forward_y");
    require_finite(forward_z, "forward_z");
    require_finite(up_x, "up_x");
    require_finite(up_y, "up_y");
    require_finite(up_z, "up_z");

    std::array<ALfloat, 6> const orientation{forward_x, forward_y, forward_z, up_x, up_y, up_z};

    context_->make_current();
    clear_al_errors();
    alListenerfv(AL_ORIENTATION, orientation.data());
    check_al("alListenerfv(AL_ORIENTATION)");
}

Sound::Sound(std::shared_ptr<detail::SoundBuffer> buffer) noexcept : buffer_{std::move(buffer)}
{
}

Sound Sound::load_from_file(AudioContext const &context, std::filesystem::path const &path)
{
    if (context.state_ == nullptr) {
        throw std::logic_error{"AudioContext is not initialized"};
    }

    DecodedAudio decoded = read_audio(path);
    if (decoded.channels == 0 || decoded.channels > 2) {
        throw std::runtime_error{"Only mono and stereo audio files are supported: " +
                                 path.string()};
    }
    if (decoded.sample_rate > static_cast<std::uint32_t>(std::numeric_limits<ALsizei>::max())) {
        throw std::runtime_error{"Audio sample rate is too large for OpenAL: " + path.string()};
    }
    if (decoded.frame_count == 0) {
        throw std::runtime_error{"Audio file contains no PCM frames: " + path.string()};
    }

    constexpr std::uint64_t max_samples =
        static_cast<std::uint64_t>(std::numeric_limits<ALsizei>::max()) / sizeof(std::int16_t);
    if (decoded.frame_count > max_samples / decoded.channels) {
        throw std::runtime_error{"Audio is too large for a single OpenAL buffer: " + path.string()};
    }

    ALenum const format = decoded.channels == 1 ? AL_FORMAT_MONO16 : AL_FORMAT_STEREO16;
    ALsizei const byte_size =
        static_cast<ALsizei>(decoded.frame_count * decoded.channels * sizeof(std::int16_t));
    ALsizei const sample_rate = static_cast<ALsizei>(decoded.sample_rate);

    context.state_->make_current();
    auto buffer = std::make_shared<detail::SoundBuffer>(context.state_.get());

    clear_al_errors();
    alGenBuffers(1, &buffer->id);
    check_al("alGenBuffers");
    if (buffer->id == 0) {
        throw std::runtime_error{"OpenAL returned buffer ID 0"};
    }

    clear_al_errors();
    alBufferData(buffer->id, format, decoded.samples.get(), byte_size, sample_rate);
    check_al("alBufferData");

    return Sound{std::move(buffer)};
}

Source::Source(AudioContext const &context) : context_{context.state_.get()}
{
    if (context_ == nullptr) {
        throw std::logic_error{"AudioContext is not initialized"};
    }

    context_->make_current();
    clear_al_errors();
    alGenSources(1, &source_);
    try {
        check_al("alGenSources");
        if (source_ == 0) {
            throw std::runtime_error{"OpenAL returned source ID 0"};
        }
    }
    catch (...) {
        if (source_ != 0) {
            alDeleteSources(1, &source_);
            source_ = 0;
        }
        throw;
    }
}

Source::~Source() noexcept
{
    reset();
}

Source::Source(Source &&other) noexcept
    : context_{std::exchange(other.context_, nullptr)}, source_{std::exchange(other.source_, 0)},
      sound_{std::move(other.sound_)}
{
}

Source &Source::operator=(Source &&other) noexcept
{
    if (this != &other) {
        reset();
        context_ = std::exchange(other.context_, nullptr);
        source_ = std::exchange(other.source_, 0);
        sound_ = std::move(other.sound_);
    }
    return *this;
}

void Source::reset() noexcept
{
    if (source_ != 0 && context_ != nullptr) {
        try {
            context_->make_current();
            clear_al_errors();
            alSourceStop(source_);
            clear_al_errors();
            alSourcei(source_, AL_BUFFER, 0);
            clear_al_errors();
            alDeleteSources(1, &source_);
        }
        catch (...) {
            // Destructors must not throw. The borrowed AudioContext must still be alive.
        }
    }

    source_ = 0;
    sound_ = Sound{};
    context_ = nullptr;
}

void Source::require_valid() const
{
    if (context_ == nullptr || source_ == 0) {
        throw std::logic_error{"Operation on an empty or moved-from Source"};
    }
}

void Source::play(Sound const &sound)
{
    require_valid();
    if (sound.buffer_ == nullptr) {
        throw std::invalid_argument{"Cannot play an empty Sound"};
    }
    if (sound.buffer_->context != context_) {
        throw std::invalid_argument{"Sound and Source belong to different OpenAL contexts"};
    }

    context_->make_current();

    clear_al_errors();
    alSourceStop(source_);
    check_al("alSourceStop");

    // Detach the old buffer before releasing the Sound handle that keeps it alive.
    clear_al_errors();
    alSourcei(source_, AL_BUFFER, 0);
    check_al("alSourcei(AL_BUFFER, 0)");
    sound_ = Sound{};

    clear_al_errors();
    alSourcei(source_, AL_BUFFER, static_cast<ALint>(sound.buffer_->id));
    check_al("alSourcei(AL_BUFFER)");

    sound_ = sound;
    clear_al_errors();
    alSourcePlay(source_);
    check_al("alSourcePlay");
}

void Source::stop()
{
    require_valid();
    context_->make_current();
    clear_al_errors();
    alSourceStop(source_);
    check_al("alSourceStop");
}

void Source::pause()
{
    require_valid();
    context_->make_current();
    clear_al_errors();
    alSourcePause(source_);
    check_al("alSourcePause");
}

void Source::set_looping(bool looping)
{
    require_valid();
    context_->make_current();
    clear_al_errors();
    alSourcei(source_, AL_LOOPING, looping ? AL_TRUE : AL_FALSE);
    check_al("alSourcei(AL_LOOPING)");
}

void Source::set_gain(float gain)
{
    require_valid();
    require_finite(gain, "gain");
    if (gain < 0.0F) {
        throw std::invalid_argument{"gain must be non-negative"};
    }

    context_->make_current();
    clear_al_errors();
    alSourcef(source_, AL_GAIN, gain);
    check_al("alSourcef(AL_GAIN)");
}

void Source::set_position(float x, float y, float z)
{
    require_valid();
    require_finite(x, "x");
    require_finite(y, "y");
    require_finite(z, "z");

    context_->make_current();
    clear_al_errors();
    alSource3f(source_, AL_POSITION, x, y, z);
    check_al("alSource3f(AL_POSITION)");
}

bool Source::is_playing() const
{
    require_valid();
    context_->make_current();

    ALint state{};
    clear_al_errors();
    alGetSourcei(source_, AL_SOURCE_STATE, &state);
    check_al("alGetSourcei(AL_SOURCE_STATE)");
    return state == AL_PLAYING;
}

} // namespace lc1::audio

namespace lc1::audio {

[[nodiscard]] Sound::operator bool() const noexcept
{
    return static_cast<bool>(buffer_);
}

} // namespace lc1::audio
