// =============================================================================
//  hmi/HmiVideo.hpp - 1.12.12 (lot H) : LA VIDEO DECODEE
// -----------------------------------------------------------------------------
//  Avant la 1.12.12, une video n'etait pas decodee : l'objet montrait son image
//  d'attente et une barre qui bouclait sur la duree du fichier. Maintenant, deux
//  decodeurs derriere une meme interface (FrameSource) :
//   - LE MOTION-JPEG EN AVI, lu ici, sur toutes les plateformes (Linux, Wine,
//     Windows) : le conteneur RIFF AVI (avih et strh : la cadence ; strf : la
//     taille ; la liste movi : les morceaux 00dc / 00db, chacun une image JPEG
//     que stb_image decode, une a la fois, quand elle se montre). Les tables de
//     Huffman qu'un AVI de camera omet (celles de la norme JPEG) sont remises ;
//   - SOUS WINDOWS SEULEMENT, Media Foundation (HmiVideoMf.cpp, IMFSourceReader
//     en RGB32) : MP4 / H.264, WMV, MOV, un AVI d'un autre codec - decode sur un
//     fil a part, image par image, sans tout charger.
//  Ailleurs, un MP4 ou un WEBM ne se decode pas : l'objet garde son image
//  d'attente et le dit ; Generer le dit avant (« MP4 : decode sous Windows
//  seulement ; ailleurs, convertir en Motion-JPEG AVI »).
//
//  LA LECTURE (Playback) est une machine a etats sans horloge : le moteur lui
//  donne l'heure a chaque cycle (les essais, une horloge factice). Depart a
//  l'affichage ou sur condition (elle joue tant que la condition est vraie et
//  revient au debut quand elle tombe), Lecture (vrai : elle avance ; faux :
//  pause), en boucle ou une fois (elle reste alors sur sa derniere image).
// =============================================================================
#pragma once

#include "HmiMedia.hpp"

#include <cstddef>
#include <cstdint>
#include <memory>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

namespace hmi::video {

// ---- le Motion-JPEG en AVI ----------------------------------------------------------------
struct AviInfo {
    int         width{0}, height{0};
    double      fps{0};               // images par seconde (strh : rate / scale ; sinon avih)
    std::string codec;                // le code du flux video : "MJPG", "H264", "XVID"...
    std::vector<std::pair<std::size_t, std::size_t>> frames;   // (debut, taille) de chaque image dans le fichier
    [[nodiscard]] double seconds() const noexcept { return fps > 0 ? static_cast<double>(frames.size()) / fps : 0.0; }
    [[nodiscard]] bool   mjpeg() const noexcept;   // un codec Motion-JPEG (MJPG, AVRn, dmb1, JPEG...)
};
[[nodiscard]] bool isAvi(const Bytes& data) noexcept;
// Lire le conteneur (l'en-tete, le flux video, ses images) ; faux et `error` : pas un AVI lisible.
bool parseAvi(const Bytes& data, AviInfo& out, std::string* error = nullptr);
// L'image `index` d'un AVI Motion-JPEG, en RGBA (une image vide du fichier : la precedente).
bool decodeAviFrame(const Bytes& data, const AviInfo& info, std::size_t index, Rgba& out, std::string* error = nullptr);
// Une image JPEG, ses tables de Huffman remises si elle n'en a pas (les AVI des cameras s'en passent).
bool decodeMjpegFrame(const std::uint8_t* data, std::size_t size, Rgba& out, std::string* error = nullptr);
// Ecrire un AVI Motion-JPEG (les essais, les exemples) : des images JPEG deja codees, leur cadence.
[[nodiscard]] Bytes writeMjpegAvi(int width, int height, double fps, const std::vector<Bytes>& jpegFrames);

// ---- ce que la plateforme decode -----------------------------------------------------------
// Le conseil de Generer (une information) : vide pour un AVI Motion-JPEG (decode partout) ; sinon
// « MP4 : decode sous Windows seulement ; ailleurs, convertir en Motion-JPEG AVI ».
[[nodiscard]] std::string decodeAdvice(std::string_view format, std::string_view codec);
// Ce format se decode-t-il ICI (le Motion-JPEG partout ; le reste sous Windows, par Media Foundation) ?
[[nodiscard]] bool decodedHere(std::string_view format, std::string_view codec);

// ---- une source d'images -------------------------------------------------------------------
class FrameSource {
public:
    virtual ~FrameSource() = default;
    // L'image a montrer `seconds` apres le debut ; nulle : aucune n'est encore prete (le fil la decode)
    // ou la source a echoue. `serial` change quand l'image change (une texture a refaire).
    virtual const Rgba* frameAt(double seconds, std::uint64_t& serial) = 0;
    // Vrai : la source ne donnera rien (format inconnu de la plateforme, fichier illisible) ; `error` dit pourquoi.
    [[nodiscard]] virtual bool failed(std::string* error = nullptr) const = 0;
    [[nodiscard]] virtual double duration() const = 0;   // en secondes ; 0 : inconnue
};
// La source d'une ressource video : le Motion-JPEG ici, le reste par Media Foundation sous Windows. Nulle
// (et `why`) : ce format ne se decode pas sur cette plateforme.
[[nodiscard]] std::unique_ptr<FrameSource> openFrameSource(const BlobPtr& data, std::string_view format, std::string* why = nullptr);
// HmiVideoMf.cpp : Media Foundation (Windows) ; ailleurs, nulle.
[[nodiscard]] std::unique_ptr<FrameSource> openMediaFoundation(const BlobPtr& data, std::string_view format, std::string* why);
// Media Foundation est-il la (charge a la demande : un Windows N sans le Media Feature Pack n'en a pas) ?
// Ailleurs que sous Windows : non. Ce qui manque, dit a l'utilisateur ; vide : rien ne manque (ou pas Windows).
[[nodiscard]] bool mediaFoundationAvailable();
[[nodiscard]] std::string mediaFoundationMissing();

// ---- la lecture ----------------------------------------------------------------------------
struct PlaybackSettings {
    bool onCondition{false};    // Depart : sur condition (sinon a l'affichage)
    bool loop{true};            // en boucle (sinon une fois)
};
struct Playback {
    bool   started{false};      // partie : on montre ses images (sinon : l'image d'attente)
    bool   playing{false};      // elle avance (partie, pas en pause, pas finie)
    bool   ended{false};        // une fois : arrivee au bout (sa derniere image reste)
    double position{0};         // secondes depuis le debut de la video
    double lastNow{-1};         // l'heure du dernier pas
    bool   conditionWas{false};
    long long loops{0};         // les tours faits (en boucle)
    double written{-1};         // la position deja ecrite dans la variable de position (le moteur)
};
// La vue s'ouvre : tout repart ; « a l'affichage », la lecture part.
void openView(Playback& pb, const PlaybackSettings& s, double now);
// Un pas du moteur : `now` son heure (s), `play` la Lecture (faux : pause), `condition` le depart sur
// condition, `duration` la duree de la video (0 : inconnue : elle avance sans fin).
void step(Playback& pb, const PlaybackSettings& s, double now, bool play, bool condition, double duration);
// « en boucle », « une fois » ; « a l'affichage », « sur condition » (sans casse ; vide : les defauts).
[[nodiscard]] PlaybackSettings settingsOf(std::string_view start, std::string_view repeat);

} // namespace hmi::video
