// =============================================================================
//  app/LicenceViews.hpp - 1.45.0 : DES VUES DE LA FENETRE LICENCE, A PART
//  (les essais les construisent seules, sans l'application)
// -----------------------------------------------------------------------------
//  CaptureView      une capture qu'on agrandit (la << ZoomImage >> de la 1.38.0) :
//                   la molette zoome autour de la souris, glisser la deplace,
//                   double clic : ajustee ; un clic simple (ajustee) : onOpen (la
//                   voir en grand). SES BULLES (1.44.0) : des zones de la capture
//                   qui disent ce qu'on y voit au survol - a leur place quels que
//                   soient le zoom, le deplacement et la taille du fichier (une
//                   capture refaite en 2x : son repere, CaptureShot::refW/refH).
//                   L'image est dessinee lissee, a la copie reduite la plus proche
//                   (HmiImageCache::smooth) : nette en petit comme en grand.
//  CaptureZoomBody  la capture EN GRAND (la fenetre au-dessus) : les fleches et
//                   << Capture 2/3 : sa legende >>, les bulles, la phrase.
//  PcLicencesView   << Les licences de ce PC >> (en tete de Demander ma licence) :
//                   une ligne par licence que ce PC utilise, Regarder, Liberer.
//
//  Le modele des licences de ce PC (pcLicences, releasePlan) : sans reseau, sans
//  fenetre - ce que l'etat de la licence et le releve du service disent.
// =============================================================================
#pragma once

#include "Licence.hpp"

#include "../core/Signal.hpp"
#include "../hmi/HmiModel.hpp"
#include "../ui/Widget.hpp"

#include <array>
#include <cstddef>
#include <functional>
#include <optional>
#include <string>
#include <vector>

namespace ui {
class Button;
}

namespace app::licence {

namespace service {
struct Record;
}

// ---- les captures ----------------------------------------------------------------------------------------------------
// Lire une capture (PNG) en ressource d'image ; vide : rien a montrer (absente, vide, 32 Mo ou plus).
[[nodiscard]] hmi::Resource capturePng(const std::string& path, const std::string& name);
// Le nom de la ressource de la capture n (0 : << id.png >>, 1 : << id-2.png >>...).
[[nodiscard]] std::string captureName(const std::string& feature, std::size_t n);

class CaptureView final : public ui::Widget {
public:
    explicit CaptureView(std::string id);
    void setImage(hmi::Resource res);
    void fitAgain();
    [[nodiscard]] bool hasImage() const noexcept { return image_.data != nullptr; }
    [[nodiscard]] float zoom() const noexcept { return zoom_; }
    [[nodiscard]] const hmi::Resource& image() const noexcept { return image_; }
    void setEmptyText(std::string t) { empty_ = std::move(t); }
    void setHint(bool on) { hint_ = on; }
    // Les bulles (en pixels du repere `refW` x `refH` ; 0 : les pixels de l'image elle-meme).
    void setSpots(std::vector<CaptureSpot> s, float refW = 0.f, float refH = 0.f);
    [[nodiscard]] std::size_t spotCount() const noexcept { return spots_.size(); }
    // La zone de la bulle n a l'ecran (le zoom, le deplacement, le repere compris) ; vide : pas de bulle n, pas d'image.
    [[nodiscard]] std::optional<gfx::Rect> spotRect(std::size_t n) const;
    [[nodiscard]] std::optional<gfx::Point> spotCenter(std::size_t n) const;
    // La bulle sous ce point (-1 : aucune) ; celle que la souris survole (-1 : aucune).
    [[nodiscard]] int spotAt(gfx::Point p) const;
    [[nodiscard]] int hoveredSpot() const noexcept { return hoverSpot_; }
    // La taille de l'image elle-meme (0 : pas encore lue) ; ou elle est dessinee (le zoom et le deplacement compris).
    [[nodiscard]] float imageWidth() const noexcept { return iw_; }
    [[nodiscard]] float imageHeight() const noexcept { return ih_; }
    [[nodiscard]] gfx::Rect imageRect() const noexcept;
    std::function<void()> onOpen;
    // Les essais : un pas de molette en (x, y).
    void wheelAt(gfx::Point at, float dy) { wheel(at, dy); }

protected:
    void onPaint(const ui::PaintContext& ctx) override;
    ui::EventResult onEvent(const ui::InputEvent& ev) override;

private:
    void wheel(gfx::Point at, float dy);
    void clampOffset();
    hmi::Resource            image_;
    std::vector<CaptureSpot> spots_;
    float                    refW_{0.f}, refH_{0.f};
    int                      hoverSpot_{-1};
    std::string              empty_;
    float                    zoom_{1.f}, ox_{0.f}, oy_{0.f};
    float                    iw_{0.f}, ih_{0.f};
    bool                     dragging_{false}, moved_{false}, hint_{true};
    gfx::Point               last_{};
};

// Une fonctionnalite et toutes ses captures (la fenetre en grand).
struct CaptureSet {
    std::string              feature;       // son identifiant (le nom des ressources)
    std::string              title;         // << Editeur de vues - IHM Studio >>
    std::string              description;   // sa phrase
    std::vector<CaptureShot> shots;
    std::vector<hmi::Resource> images;      // celles deja lues (la meme place que shots ; les autres : lues a la demande)
};

class CaptureZoomBody final : public ui::Widget {
public:
    CaptureZoomBody(CaptureSet set, std::size_t shot, std::function<void()> close);
    [[nodiscard]] CaptureView& image() noexcept { return *image_; }
    [[nodiscard]] std::size_t shot() const noexcept { return shot_; }
    [[nodiscard]] std::size_t shotCount() const noexcept { return set_.shots.size(); }
    // La precedente (-1), la suivante (+1), en boucle.
    void step(int delta);
    void showShot(std::size_t n);
    // << Capture 2/3 : sa legende >> ; vide : une seule capture.
    [[nodiscard]] std::string navText() const;
    // La phrase sous la capture : sa legende, puis celle de la fonctionnalite.
    [[nodiscard]] std::string text() const;

protected:
    void onLayout() override;
    void onPaint(const ui::PaintContext& ctx) override;
    ui::EventResult onEvent(const ui::InputEvent& ev) override;

private:
    CaptureSet            set_;
    std::size_t           shot_{0};
    std::function<void()> close_;
    CaptureView*          image_{nullptr};
    ui::Button*           prev_{nullptr};
    ui::Button*           next_{nullptr};
    ui::Button*           closeButton_{nullptr};
    gfx::Rect             panel_{}, closeBox_{};
    float                 navY_{0.f}, textY_{0.f};
    core::ConnectionScope links_;
};

// ---- les licences de ce PC -------------------------------------------------------------------------------------------
struct PcLicence {
    enum class Kind : unsigned char {
        File,       // liee a ce PC dans le fichier signe par l'editeur
        Online,     // en ligne : activee par le service, avec son bail
        Renew,      // en ligne, le bail fini (ou la licence plus sur ce PC) : sa place est encore prise
        Floating,   // le poste pris d'une licence flottante
        Option,     // une option de la licence (en cours ou a venir)
    };
    Kind        kind{Kind::File};
    std::string id;          // la licence (une option, un poste : celle qui les porte)
    std::string title;       // << XPG-2026-00042 >>, << Poste flottant >>, le nom de l'option
    std::string edition, customer;
    std::string state;       // << En ligne . Valide . bail jusqu'au 08/12/2026 10:42 (29 j) >>
    std::string until;       // << jusqu'au 07/11/2027 >>, << sans fin >>
    int         tone{0};     // 1 bon, 2 attention, 3 erreur
    int         tab{kTabMine};   // Regarder : l'onglet qui la montre
};
// Ce que ce PC utilise maintenant (l'etat de la licence, le releve du service, le poste flottant rendu ou pas).
[[nodiscard]] std::vector<PcLicence> pcLicences();
[[nodiscard]] std::vector<PcLicence> pcLicences(const licensing::LicenseState& st, const service::Record& rec, bool leaseHeldBack,
                                                licensing::Seconds now);
// Liberer une ligne : ce qui se passe (la question, ou la phrase qui dit pourquoi pas d'ici).
struct ReleasePlan {
    enum class Action : unsigned char {
        None,         // rien a faire d'ici (une licence par fichier, une option) : la phrase seule
        Release,      // la place de ce PC rendue au service (comme licence-liberer)
        ReturnSeat,   // le poste flottant rendu
    };
    Action      action{Action::None};
    std::string title, text, confirm, cancel;
};
[[nodiscard]] ReleasePlan releasePlan(const PcLicence& l, const licensing::LicenseState& st, licensing::Seconds now);
// Le texte quand ce PC n'a aucune licence (la Decouverte, s'il y en a une).
[[nodiscard]] std::string pcLicencesEmptyText(const licensing::LicenseState& st);

class PcLicencesView final : public ui::Widget {
public:
    static constexpr std::size_t kMaxRows = 6;
    static constexpr float kHeader = 26.f, kRow = 56.f, kGap = 6.f, kEmpty = 26.f;
    explicit PcLicencesView(std::string id);
    void setRows(std::vector<PcLicence> rows, std::string emptyText);
    [[nodiscard]] const std::vector<PcLicence>& rows() const noexcept { return rows_; }
    [[nodiscard]] const std::string& emptyText() const noexcept { return empty_; }
    // La hauteur qu'il lui faut (le titre, les lignes, ou la phrase vide).
    [[nodiscard]] float wantedHeight() const noexcept;
    [[nodiscard]] ui::Button* lookButton(std::size_t i) const noexcept { return i < kMaxRows ? look_[i] : nullptr; }
    [[nodiscard]] ui::Button* releaseButton(std::size_t i) const noexcept { return i < kMaxRows ? release_[i] : nullptr; }
    std::function<void(const PcLicence&)> onLook, onRelease;

protected:
    void onLayout() override;
    void onPaint(const ui::PaintContext& ctx) override;

private:
    std::vector<PcLicence>               rows_;
    std::string                          empty_;
    std::array<ui::Button*, kMaxRows>    look_{}, release_{};
    std::vector<gfx::Rect>               boxes_;
    core::ConnectionScope                links_;
};

} // namespace app::licence
