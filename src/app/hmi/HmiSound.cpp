#include "HmiSound.hpp"

#include "../../hmi/HmiMedia.hpp"

#include <algorithm>
#include <cstdlib>
#include <map>

#if XPG_WITH_SDL
#  include <SDL3/SDL.h>
#endif

namespace app {

// ============================================================ les voix ===
int SoundVoices::start(const void* sound, double seconds, double now, std::vector<int>& silenced, int channel, float gain,
                       std::vector<float> peaks) {
    finished(now, silenced);
    for (std::size_t i = voices_.size(); i-- > 0;)
        if (voices_[i].sound == sound) {               // le meme son : il repart du debut
            silenced.push_back(voices_[i].id);
            voices_.erase(voices_.begin() + static_cast<long>(i));
        }
    while (voices_.size() >= kMax) {                   // trop de voix : la plus ancienne se tait
        silenced.push_back(voices_.front().id);
        voices_.erase(voices_.begin());
    }
    const int id = next_++;
    voices_.push_back({id, sound, now + std::max(0.0, seconds), channel, std::clamp(gain, 0.f, 1.f), now, std::move(peaks)});
    return id;
}

// 1.14.0 : le melangeur.
void SoundVoices::silenceChannel(int channel, std::vector<int>& silenced) {
    for (std::size_t i = voices_.size(); i-- > 0;)
        if (channel == 0 || voices_[i].channel == channel) {
            silenced.push_back(voices_[i].id);
            voices_.erase(voices_.begin() + static_cast<long>(i));
        }
}

void SoundVoices::setGain(int channel, float gain, std::vector<int>* changed) {
    for (auto& v : voices_)
        if (channel == 0 || v.channel == channel) {
            v.gain = std::clamp(gain, 0.f, 1.f);
            if (changed) changed->push_back(v.id);
        }
}

float SoundVoices::level(int channel, double now) const {
    float best = 0.f;
    for (const auto& v : voices_) {
        if ((channel != 0 && v.channel != channel) || now < v.started || now >= v.ends) continue;
        float peak = 0.7f;           // sans enveloppe : un niveau moyen tant qu'elle joue
        if (!v.peaks.empty()) {
            const auto k = static_cast<std::size_t>(std::max(0.0, (now - v.started) / kPeakStep));
            peak = k < v.peaks.size() ? v.peaks[k] : 0.f;
        }
        best = std::max(best, peak * v.gain);
    }
    return std::clamp(best, 0.f, 1.f);
}

std::vector<float> SoundVoices::peaksOf(const std::vector<std::int16_t>& samples, int channels, int rate, double step) {
    std::vector<float> out;
    if (channels <= 0 || rate <= 0 || step <= 0) return out;
    const auto frames = samples.size() / static_cast<std::size_t>(channels);
    const auto per = std::max<std::size_t>(1, static_cast<std::size_t>(rate * step));
    for (std::size_t f = 0; f < frames; f += per) {
        int peak = 0;
        const std::size_t end = std::min(frames, f + per) * static_cast<std::size_t>(channels);
        for (std::size_t i = f * static_cast<std::size_t>(channels); i < end; ++i) peak = std::max(peak, std::abs(static_cast<int>(samples[i])));
        out.push_back(static_cast<float>(peak) / 32767.f);
    }
    return out;
}

void SoundVoices::finished(double now, std::vector<int>& silenced) {
    for (std::size_t i = voices_.size(); i-- > 0;)
        if (now >= voices_[i].ends) {
            silenced.push_back(voices_[i].id);
            voices_.erase(voices_.begin() + static_cast<long>(i));
        }
}

void SoundVoices::forget(int id) {
    voices_.erase(std::remove_if(voices_.begin(), voices_.end(), [id](const Voice& v) { return v.id == id; }), voices_.end());
}

std::size_t SoundVoices::count(double now) const {
    return static_cast<std::size_t>(std::count_if(voices_.begin(), voices_.end(), [now](const Voice& v) { return now < v.ends; }));
}

// ========================================================== le lecteur ===
struct HmiSoundPlayer::Impl {
#if XPG_WITH_SDL
    SDL_AudioStream* stream{nullptr};
    // 1.12.3 : les voix melangees - une sortie, un flux par voix.
    SDL_AudioDeviceID                device{0};
    std::map<int, SDL_AudioStream*>  voices;
    void silence(const std::vector<int>& ids) {
        for (const int id : ids)
            if (const auto it = voices.find(id); it != voices.end()) {
                SDL_DestroyAudioStream(it->second);
                voices.erase(it);
            }
    }
#endif
};

namespace {
#if XPG_WITH_SDL
double audioClock() { return static_cast<double>(SDL_GetTicks()) / 1000.0; }
#endif
} // namespace

HmiSoundPlayer& HmiSoundPlayer::instance() {
    static HmiSoundPlayer player;
    return player;
}

HmiSoundPlayer::HmiSoundPlayer() : impl_(std::make_unique<Impl>()) {}
HmiSoundPlayer::~HmiSoundPlayer() {
#if XPG_WITH_SDL
    // Apres SDL_Quit (fin du programme), SDL a deja detruit ses flux : ne plus
    // toucher au notre.
    if (impl_ && !SDL_WasInit(SDL_INIT_AUDIO)) {
        impl_->stream = nullptr;
        impl_->voices.clear();
        impl_->device = 0;
    }
#endif
    stop();
    stopMix();
#if XPG_WITH_SDL
    if (impl_ && impl_->device) SDL_CloseAudioDevice(impl_->device);
#endif
}

bool HmiSoundPlayer::play(const hmi::Resource& sound, std::string* why, float gain) {
    stop();
    if (!sound.data || sound.kind() != hmi::MediaKind::Sound) {
        if (why) *why = "ce n'est pas un son";
        return false;
    }
    hmi::Pcm pcm;
    if (!hmi::decodePcm(*sound.data, sound.format, pcm)) {
        if (why) *why = "son illisible (" + sound.format + ")";
        return false;
    }
    hmi::applyGain(pcm, gain);
#if XPG_WITH_SDL
    if (!SDL_WasInit(SDL_INIT_AUDIO) && !SDL_InitSubSystem(SDL_INIT_AUDIO)) {
        if (why) *why = std::string("pas de sortie son : ") + SDL_GetError();
        return false;
    }
    SDL_AudioSpec spec;
    spec.format = SDL_AUDIO_S16;
    spec.channels = pcm.channels;
    spec.freq = pcm.rate;
    impl_->stream = SDL_OpenAudioDeviceStream(SDL_AUDIO_DEVICE_DEFAULT_PLAYBACK, &spec, nullptr, nullptr);
    if (!impl_->stream) {
        if (why) *why = std::string("pas de sortie son : ") + SDL_GetError();
        return false;
    }
    SDL_PutAudioStreamData(impl_->stream, pcm.samples.data(), static_cast<int>(pcm.samples.size() * sizeof(std::int16_t)));
    SDL_FlushAudioStream(impl_->stream);
    SDL_ResumeAudioStreamDevice(impl_->stream);
    current_ = sound.data.get();
    seconds_ = pcm.seconds();
    pendingStart_ = true;
    return true;
#else
    if (why) *why = "pas de sortie son dans cette version (construite sans SDL)";
    return false;
#endif
}

void HmiSoundPlayer::stop() {
#if XPG_WITH_SDL
    if (impl_ && impl_->stream) {
        SDL_DestroyAudioStream(impl_->stream);
        impl_->stream = nullptr;
    }
#endif
    current_ = nullptr;
    seconds_ = 0;
    started_ = -1;
    pendingStart_ = false;
}

bool HmiSoundPlayer::playing(double now) {
    if (!current_) return false;
    if (pendingStart_) { started_ = now; pendingStart_ = false; }
    if (now - started_ > seconds_) { stop(); return false; }
    return true;
}

bool HmiSoundPlayer::mix(const hmi::Resource& sound, std::string* why, float gain, int channel) {
    if (!sound.data || sound.kind() != hmi::MediaKind::Sound) {
        if (why) *why = "ce n'est pas un son";
        return false;
    }
    hmi::Pcm pcm;
    if (!hmi::decodePcm(*sound.data, sound.format, pcm)) {
        if (why) *why = "son illisible (" + sound.format + ")";
        return false;
    }
    // 1.14.0 : le gain va au flux (SDL_SetAudioStreamGain) - le melangeur le change pendant que la voix joue.
    gain = std::clamp(gain, 0.f, 1.f);
#if XPG_WITH_SDL
    if (!SDL_WasInit(SDL_INIT_AUDIO) && !SDL_InitSubSystem(SDL_INIT_AUDIO)) {
        if (why) *why = std::string("pas de sortie son : ") + SDL_GetError();
        return false;
    }
    // Une seule sortie pour toutes les voix : la carte son les melange.
    if (!impl_->device) impl_->device = SDL_OpenAudioDevice(SDL_AUDIO_DEVICE_DEFAULT_PLAYBACK, nullptr);
    if (!impl_->device) {
        if (why) *why = std::string("pas de sortie son : ") + SDL_GetError();
        return false;
    }
    std::vector<int> silenced;
    const int id = voices_.start(sound.data.get(), pcm.seconds(), audioClock(), silenced, channel, gain,
                                 SoundVoices::peaksOf(pcm.samples, pcm.channels, pcm.rate));
    impl_->silence(silenced);
    SDL_AudioSpec spec;
    spec.format = SDL_AUDIO_S16;
    spec.channels = pcm.channels;
    spec.freq = pcm.rate;
    SDL_AudioStream* stream = SDL_CreateAudioStream(&spec, nullptr);
    if (!stream || !SDL_BindAudioStream(impl_->device, stream)) {
        if (why) *why = std::string("pas de sortie son : ") + SDL_GetError();
        if (stream) SDL_DestroyAudioStream(stream);
        voices_.forget(id);
        return false;
    }
    SDL_SetAudioStreamGain(stream, gain);
    SDL_PutAudioStreamData(stream, pcm.samples.data(), static_cast<int>(pcm.samples.size() * sizeof(std::int16_t)));
    SDL_FlushAudioStream(stream);
    impl_->voices[id] = stream;
    return true;
#else
    (void)channel;
    if (why) *why = "pas de sortie son dans cette version (construite sans SDL)";
    return false;
#endif
}

// 1.14.0 : le melangeur.
void HmiSoundPlayer::stopChannel(int channel) {
    std::vector<int> silenced;
    voices_.silenceChannel(channel, silenced);
#if XPG_WITH_SDL
    if (impl_) impl_->silence(silenced);
#endif
}

void HmiSoundPlayer::setChannelGain(int channel, float gain) {
    std::vector<int> changed;
    voices_.setGain(channel, gain, &changed);
#if XPG_WITH_SDL
    if (impl_)
        for (const int id : changed)
            if (const auto it = impl_->voices.find(id); it != impl_->voices.end()) SDL_SetAudioStreamGain(it->second, std::clamp(gain, 0.f, 1.f));
#endif
}

float HmiSoundPlayer::level(int channel) {
#if XPG_WITH_SDL
    return voices_.level(channel, audioClock());
#else
    (void)channel;
    return 0.f;
#endif
}

void HmiSoundPlayer::stopMix() {
#if XPG_WITH_SDL
    if (impl_) {
        for (auto& [id, stream] : impl_->voices) SDL_DestroyAudioStream(stream);
        impl_->voices.clear();
    }
#endif
    voices_.clear();
}

std::size_t HmiSoundPlayer::mixing() {
#if XPG_WITH_SDL
    std::vector<int> done;
    voices_.finished(audioClock(), done);
    impl_->silence(done);
    return voices_.voices().size();
#else
    return 0;
#endif
}

double HmiSoundPlayer::position(double now) const {
    if (!current_ || started_ < 0) return 0;
    return std::min(seconds_, std::max(0.0, now - started_));
}

} // namespace app
