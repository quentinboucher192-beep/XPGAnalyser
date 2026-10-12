// =============================================================================
//  hmi/HmiVideoQueue.hpp - 1.12.12 (lot H) : LES IMAGES D'AVANCE D'UN DECODEUR
// -----------------------------------------------------------------------------
//  Media Foundation (HmiVideoMf.cpp, Windows) decode sur un fil a part ; le
//  dessin demande l'image d'une heure. Entre les deux, cette file : sans fil ni
//  verrou (MfSource les tient), elle se verifie partout - hmi_video_test la fait
//  tourner avec un faux decodeur.
//   - le fil decode tant qu'il y a de la place (kAhead images d'avance) ;
//   - le dessin prend les images dont l'heure est passee ; LA PREMIERE se montre
//     des qu'elle est la, meme si son heure n'est pas 0 (un MP4 a images B
//     commence souvent a 0,067 s : l'apercu de l'editeur, a 0 s, la montre) ;
//   - une demande en arriere (la boucle, un nouveau depart) vide la file et
//     demande au fil de repartir de la ; une image decodee pendant ce temps
//     (d'avant le saut) est jetee.
// =============================================================================
#pragma once

#include "HmiMedia.hpp"

#include <cstddef>
#include <cstdint>
#include <deque>
#include <utility>

namespace hmi::video {

class FrameQueue {
public:
    static constexpr std::size_t kAhead = 3;

    // Le dessin : l'image a montrer `seconds` apres le debut ; nulle : aucune encore. `serial` change avec elle.
    const Rgba* frameAt(double seconds, std::uint64_t& serial) {
        if (seconds + 0.25 < asked_) {
            seekTo_ = seconds < 0.05 ? 0.0 : seconds;
            queue_.clear();
        }
        asked_ = seconds;
        if (!shown_ && !queue_.empty()) take();   // la premiere image, quelle que soit son heure
        while (!queue_.empty() && queue_.front().time <= seconds + 1e-3) take();
        serial = serial_;
        return shown_ ? &current_ : nullptr;
    }
    // Le fil : a-t-il a faire (repartir d'ailleurs, ou decoder une image de plus) ? `ended` : le flux est fini.
    [[nodiscard]] bool wantsWork(bool ended) const noexcept { return seekTo_ >= 0 || (!ended && queue_.size() < kAhead); }
    // Le fil : d'ou repartir (secondes) ; negatif : nulle part. La demande est prise.
    double takeSeek() noexcept {
        const double s = seekTo_;
        seekTo_ = -1;
        return s;
    }
    // Le fil : une image decodee, a son heure ; jetee si un saut a ete demande depuis.
    void push(double time, Rgba image) {
        if (seekTo_ < 0) queue_.push_back({time, std::move(image)});
    }
    [[nodiscard]] std::size_t size() const noexcept { return queue_.size(); }

private:
    struct Frame {
        double time{0};
        Rgba   image;
    };
    void take() {
        current_ = std::move(queue_.front().image);
        queue_.pop_front();
        ++serial_;
        shown_ = true;
    }
    std::deque<Frame> queue_;
    Rgba              current_;
    std::uint64_t     serial_{0};
    bool              shown_{false};
    double            asked_{0};
    double            seekTo_{-1};
};

} // namespace hmi::video
