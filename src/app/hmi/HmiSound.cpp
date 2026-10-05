#include "HmiSound.hpp"

#include "../../hmi/HmiMedia.hpp"

#include <algorithm>

#if XPG_WITH_SDL
#  include <SDL3/SDL.h>
#endif

namespace app {

struct HmiSoundPlayer::Impl {
#if XPG_WITH_SDL
    SDL_AudioStream* stream{nullptr};
#endif
};

HmiSoundPlayer& HmiSoundPlayer::instance() {
    static HmiSoundPlayer player;
    return player;
}

HmiSoundPlayer::HmiSoundPlayer() : impl_(std::make_unique<Impl>()) {}
HmiSoundPlayer::~HmiSoundPlayer() {
#if XPG_WITH_SDL
    // Apres SDL_Quit (fin du programme), SDL a deja detruit ses flux : ne plus
    // toucher au notre.
    if (impl_ && impl_->stream && !SDL_WasInit(SDL_INIT_AUDIO)) impl_->stream = nullptr;
#endif
    stop();
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

double HmiSoundPlayer::position(double now) const {
    if (!current_ || started_ < 0) return 0;
    return std::min(seconds_, std::max(0.0, now - started_));
}

} // namespace app
