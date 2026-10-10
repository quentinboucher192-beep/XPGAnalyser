#include "HmiSound.hpp"

#include "../../hmi/HmiMedia.hpp"

#include <algorithm>
#include <map>

#if XPG_WITH_SDL
#  include <SDL3/SDL.h>
#endif

namespace app {

// ============================================================ les voix ===
int SoundVoices::start(const void* sound, double seconds, double now, std::vector<int>& silenced) {
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
    voices_.push_back({id, sound, now + std::max(0.0, seconds)});
    return id;
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

bool HmiSoundPlayer::mix(const hmi::Resource& sound, std::string* why, float gain) {
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
    // Une seule sortie pour toutes les voix : la carte son les melange.
    if (!impl_->device) impl_->device = SDL_OpenAudioDevice(SDL_AUDIO_DEVICE_DEFAULT_PLAYBACK, nullptr);
    if (!impl_->device) {
        if (why) *why = std::string("pas de sortie son : ") + SDL_GetError();
        return false;
    }
    std::vector<int> silenced;
    const int id = voices_.start(sound.data.get(), pcm.seconds(), audioClock(), silenced);
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
    SDL_PutAudioStreamData(stream, pcm.samples.data(), static_cast<int>(pcm.samples.size() * sizeof(std::int16_t)));
    SDL_FlushAudioStream(stream);
    impl_->voices[id] = stream;
    return true;
#else
    if (why) *why = "pas de sortie son dans cette version (construite sans SDL)";
    return false;
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
