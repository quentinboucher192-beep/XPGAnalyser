// =============================================================================
//  app/hmi/HmiSound.hpp — ecouter un son du projet IHM
// -----------------------------------------------------------------------------
//  L'APERCU : UN SEUL SON A LA FOIS (play, stop). En ecouter un autre arrete le
//  premier : c'est ce qu'on attend d'un apercu.
//
//  1.12.3 : L'IHM EN MARCHE MELANGE SES SONS (mix). Deux alarmes qui sonnent
//  ensemble s'entendent ensemble : une voix par son, que la sortie son melange.
//  Le meme son relance repart du debut ; Silence et l'arret les coupent toutes.
//
//  LA POSITION SUIT L'HORLOGE DE L'APPLICATION (le `time` du dessin), pas la
//  carte son : le curseur de l'apercu avance au meme rythme que le reste de
//  l'ecran, et une session de script rejouee donne la meme image.
//
//  Sans SDL (les tests), il n'y a pas de sortie son : play() rend faux et le
//  dit, le reste de l'outil ne change pas.
//
//  1.14.0 : CHAQUE VOIX A SON CANAL (hmi::SoundChannel : Alarmes, Scripts,
//  Actions, Videos, Relecture) ET SON GAIN, que le melangeur change pendant
//  qu'elle joue (le gain du flux, pas des echantillons) ; couper un canal fait
//  taire ses voix. Le niveau d'un canal (le vumetre) : l'enveloppe de chaque
//  voix (sa crete par 50 ms), au point ou elle en est, fois son gain.
// =============================================================================
#pragma once

#include "../../hmi/HmiModel.hpp"

#include <cstddef>
#include <cstdint>
#include <memory>
#include <string>
#include <vector>

namespace app {

// 1.12.3 : LES VOIX - qui joue, jusqu'a quand ; ce qui doit se taire pour qu'un
// son parte : les voix finies, le meme son encore en cours (il repart du debut,
// sans echo), la plus ancienne au-dela de kMax. Sans carte son : essayable.
class SoundVoices {
public:
    static constexpr std::size_t kMax = 8;
    static constexpr double kPeakStep = 0.05;           // 1.14.0 : une crete par 50 ms (le vumetre)
    struct Voice {
        int         id{0};
        const void* sound{nullptr};      // la ressource, reconnue a ses octets
        double      ends{0};             // l'heure ou elle finit
        // 1.14.0 : son canal (rang de hmi::kSoundChannels), son gain (0..1), son depart, ses cretes (0..1).
        int                channel{0};
        float              gain{1.f};
        double             started{0};
        std::vector<float> peaks;
    };
    // Une voix pour `sound` (de `seconds`) a `now` ; `silenced` recoit les voix a arreter.
    // 1.14.0 : sur `channel`, a `gain`, avec ses cretes (vide : un niveau moyen tant qu'elle joue).
    int start(const void* sound, double seconds, double now, std::vector<int>& silenced, int channel = 0, float gain = 1.f,
              std::vector<float> peaks = {});
    void finished(double now, std::vector<int>& silenced);    // les voix finies a `now`
    void forget(int id);
    void clear() noexcept { voices_.clear(); }
    [[nodiscard]] std::size_t count(double now) const;
    [[nodiscard]] const std::vector<Voice>& voices() const noexcept { return voices_; }
    // 1.14.0 : les voix d'un canal (0 : toutes) se taisent ; `silenced` les recoit.
    void silenceChannel(int channel, std::vector<int>& silenced);
    // ... prennent ce gain (les ids changes dans `changed`).
    void setGain(int channel, float gain, std::vector<int>* changed = nullptr);
    // Le niveau d'un canal a `now` (0 : tous) : la plus forte de ses voix, sa crete fois son gain.
    [[nodiscard]] float level(int channel, double now) const;
    // Les cretes d'un son (16 bits entrelaces) par pas de `step` secondes.
    [[nodiscard]] static std::vector<float> peaksOf(const std::vector<std::int16_t>& samples, int channels, int rate,
                                                    double step = kPeakStep);

private:
    std::vector<Voice> voices_;
    int                next_{1};
};

class HmiSoundPlayer {
public:
    static HmiSoundPlayer& instance();
    ~HmiSoundPlayer();

    // Faux (et `why`) si le son est illisible ou s'il n'y a pas de sortie audio.
    // `gain` : le volume, de 0 a 1 (lot 10 : Parametres systeme > Volume).
    bool play(const hmi::Resource& sound, std::string* why = nullptr, float gain = 1.f);
    void stop();
    // 1.12.3 : un son de l'IHM en marche, mele a ceux qui jouent deja (une voix chacun).
    // 1.14.0 : `channel` - son canal (rang de hmi::kSoundChannels) ; `gain` : celui du canal.
    bool mix(const hmi::Resource& sound, std::string* why = nullptr, float gain = 1.f, int channel = 0);
    void stopMix();                                    // Silence, l'arret : toutes les voix
    [[nodiscard]] std::size_t mixing();                // les voix qui jouent encore
    // 1.14.0 : le melangeur - les voix d'un canal (0 : toutes) se taisent, ou prennent ce gain ;
    // le niveau d'un canal (le vumetre, 0 a 1).
    void stopChannel(int channel);
    void setChannelGain(int channel, float gain);
    [[nodiscard]] float level(int channel);
    // `now` : l'heure de l'application. Le premier appel apres play() fixe le
    // depart ; faux quand le son est fini (il s'arrete alors).
    [[nodiscard]] bool playing(double now);
    [[nodiscard]] double position(double now) const;
    // Le contenu en cours (la ressource est reconnue a ses octets).
    [[nodiscard]] const void* current() const noexcept { return current_; }
    [[nodiscard]] double duration() const noexcept { return seconds_; }

private:
    HmiSoundPlayer();
    struct Impl;
    std::unique_ptr<Impl> impl_;
    SoundVoices voices_;                               // 1.12.3
    const void* current_{nullptr};
    double      seconds_{0}, started_{-1};
    bool        pendingStart_{false};
};

} // namespace app
