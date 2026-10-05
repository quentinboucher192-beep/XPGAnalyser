// =============================================================================
//  app/hmi/HmiSound.hpp — ecouter un son du projet IHM
// -----------------------------------------------------------------------------
//  UN SEUL SON A LA FOIS. En jouer un autre arrete le premier : c'est ce qu'on
//  attend d'un apercu, et d'une alarme sonore qui en remplace une autre.
//
//  LA POSITION SUIT L'HORLOGE DE L'APPLICATION (le `time` du dessin), pas la
//  carte son : le curseur de l'apercu avance au meme rythme que le reste de
//  l'ecran, et une session de script rejouee donne la meme image.
//
//  Sans SDL (les tests), il n'y a pas de sortie son : play() rend faux et le
//  dit, le reste de l'outil ne change pas.
// =============================================================================
#pragma once

#include "../../hmi/HmiModel.hpp"

#include <memory>
#include <string>

namespace app {

class HmiSoundPlayer {
public:
    static HmiSoundPlayer& instance();
    ~HmiSoundPlayer();

    // Faux (et `why`) si le son est illisible ou s'il n'y a pas de sortie audio.
    // `gain` : le volume, de 0 a 1 (lot 10 : Parametres systeme > Volume).
    bool play(const hmi::Resource& sound, std::string* why = nullptr, float gain = 1.f);
    void stop();
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
    const void* current_{nullptr};
    double      seconds_{0}, started_{-1};
    bool        pendingStart_{false};
};

} // namespace app
