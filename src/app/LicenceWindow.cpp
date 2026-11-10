// =============================================================================
//  app/LicenceWindow.cpp - 1.30.0 : LA FENETRE LICENCE (le License Manager du
//  client, maquette << XPG License Manager (client) >>)
// -----------------------------------------------------------------------------
//  En tete : l'edition, le statut, le client, la licence ; les PC autorises et
//  lies, la fin, le registre ; la phrase de l'etat. Les onglets :
//    Activation       1.35.0 : activer avec un code (XPG-XXXX-XXXX-XXXX-XXXX,
//                     le nom de ce PC, Activer) ; la licence en ligne (le bail,
//                     la derniere synchronisation, Renouveler maintenant,
//                     Liberer ce poste) ; la licence flottante (le jeton, Rendre
//                     le poste) ; la Decouverte et ce qu'en garde le service
//                     (et la phrase sur l'empreinte de la machine) ;
//    Ma licence       ce que dit la licence signee de ce PC ;
//    Mes appareils    les PC lies (le nom donne par le client, l'empreinte de sa
//                     cle : rien de son materiel) ; << ce PC >> ;
//    Fonctionnalites  chacune (le registre), ouverte ou fermee, ses limites, et
//                     ou elle est dans le programme (licence::gates) ;
//    Editions         ce qu'ouvre chaque edition (le registre) ; la tienne ;
//    Historique       les imports, les demandes, les changements d'etat ;
//    Parametres       le dossier, l'empreinte de ce PC, les numeros du registre
//                     et de la liste de revocation, la cle de l'editeur ;
//    Demande a l'editeur  la demande sans code (1.34.0, par le service des
//                     licences depuis la 1.35.0) - le formulaire (Societe,
//                     Contact, Nom de ce PC, Licence existante, Message) et
//                     Envoyer a l'editeur ; la demande en cours (sa date, son
//                     etat), Annuler la demande, Verifier maintenant ; la derniere
//                     reponse.
//  Le reseau part sur le fil du service (app/LicenceService) : la fenetre ne
//  fait que le relever. Sans service dans ce programme : la phrase qui le dit,
//  et les boutons par fichier du bas, comme avant.
//  En bas : Activer par fichier de licence..., Demander une activation...,
//  Ouvrir le dossier, Fermer. L'en-tete dit la Decouverte (les jours qui restent)
//  et, pour une licence en ligne, la fin du bail.
//
//  1.46.0 : UN FIL D'ARIANE A LA PLACE DES ONZE ONGLETS (<< un paquet empile
//  incomprehensible >>, le client). L'ACCUEIL (kTabHome) : le resume de la licence
//  et les rubriques en quatre groupes (LicenceViews : LicenceHome) - Ma licence
//  (Ma licence, Mes appareils, Historique), Obtenir ou etendre (Demander ma
//  licence, Activation, Options, Mes achats), Decouvrir (Fonctionnalites,
//  Editions), Reglages (Parametres). Chaque rubrique : << Licence > sa rubrique v >>
//  en haut (Licence : l'accueil ; la rubrique : le menu de son groupe), Retour
//  arriere ou Alt+gauche : l'accueil, Echap : la fenetre se ferme. MON PROFIL :
//  sa petite fenetre a part (ProfileWindow ; << Modifier mon profil... >> dans la
//  demande). La capture en grand : on y change de fonctionnalite (les barrees
//  aussi, << pas dans ta licence >>). Les cartes des offres : rien de coupe.
// =============================================================================
#include "Licence.hpp"

#include "App.hpp"
#include "Dossiers.hpp"
#include "LicenceService.hpp"   // 1.35.0 : le service des licences
#include "LicenceViews.hpp"     // 1.45.0 : la capture qu'on agrandit, la capture en grand, les licences de ce PC
#include "Settings.hpp"
#include "hmi/HmiImages.hpp"    // 1.37.0 : la capture d'une fonctionnalite (onglet Editions)
#include "hmi/HmiPaneKit.hpp"
#include "Notices.hpp"                // 1.38.0 : les notifications generales
#include "Updates.hpp"                // 1.44.0 : les messages a ce PC, les mises a jour (leurs fenetres : UpdateWindows.cpp)
#include "ai/AiNotificationsView.hpp" // 1.38.0 : la liste (la meme que celle de l'Assistant)
#include "screens/Screens.hpp"

#include "../core/Edition.hpp"
#include "../core/Version.hpp"
#include "../deploy/Crypto.hpp"
#include "../licensing/Licensing.hpp"
#include "../licensing/Time.hpp"
#include "../menu/MenuManager.hpp"
#include "../ui/DialogFrame.hpp"
#include "../ui/widgets/Containers.hpp"
#include "../ui/widgets/Controls.hpp"
#include "../ui/widgets/DataViews.hpp"
#include "../ui/widgets/DatePicker.hpp"   // 1.42.0 : les dates d'une option
#include "../ui/Shapes.hpp"

#include <algorithm>
#include <array>      // 1.46.0 : les pages des rubriques
#include <cmath>
#include <ctime>
#include <filesystem>
#include <fstream>
#include <functional>
#include <initializer_list>
#include <memory>
#include <optional>
#include <utility>
#include <string>
#include <vector>

namespace app {

namespace {

constexpr gfx::FontId kTitle{17};
constexpr gfx::FontId kSmall{14};
constexpr gfx::FontId kValue{17};
using PG = ui::PropertyGrid;
using hmikit::Rows;

std::string fit(gfx::IRenderer& r, const std::string& s, gfx::FontId f, float w) {
    if (w <= 8.f) return {};
    if (r.measure(s, f).width <= w) return s;
    std::string t = s;
    while (!t.empty() && r.measure(t + "\xE2\x80\xA6", f).width > w) {
        while (!t.empty() && (static_cast<unsigned char>(t.back()) & 0xC0) == 0x80) t.pop_back();   // 1.44.0 : un caractere entier
        if (!t.empty()) t.pop_back();
    }
    return t + "\xE2\x80\xA6";
}

std::string typeLabel(licensing::LicenseType t) {
    switch (t) {
        case licensing::LicenseType::Perpetual: return "perp\xC3\xA9tuelle";
        case licensing::LicenseType::Temporary: return "temporaire";
        case licensing::LicenseType::Evaluation: return "d\xC3\xA9" "couverte";
        case licensing::LicenseType::Floating: return "flottante";
    }
    return {};
}

std::string dateOf(licensing::Seconds s) { return s ? licensing::isoDate(s) : std::string(); }

std::string whenOf(std::int64_t at) {
    if (at <= 0) return {};
    const std::string t = licensing::isoTime(static_cast<licensing::Seconds>(at));   // "2026-11-06T09:41:00Z"
    return t.size() >= 16 ? t.substr(0, 10) + " " + t.substr(11, 5) : t;
}

PG::Property prop(std::string name, std::string value, std::string description = {}) {
    PG::Property p;
    p.name = std::move(name);
    p.value = std::move(value);
    p.type = PG::ValueType::ReadOnly;
    p.description = std::move(description);
    return p;
}

menu::MenuTraits dialogTraits() {
    menu::MenuTraits t;
    t.kind = menu::MenuKind::Dialog;
    t.rendersBelow = true;
    t.updatesBelow = true;
    t.blocksInput = true;
    t.dimsBelow = true;
    return t;
}

class LicenceWindow;
class LicenceBody;

// Des lignes coupees a la largeur (les mots entiers).
std::vector<std::string> wrapText(gfx::IRenderer& r, const std::string& text, gfx::FontId f, float w) {
    std::vector<std::string> out;
    std::string line, word;
    const auto flush = [&] {
        if (word.empty()) return;
        const std::string tryLine = line.empty() ? word : line + " " + word;
        if (!line.empty() && r.measure(tryLine, f).width > w) {
            out.push_back(line);
            line = word;
        } else {
            line = tryLine;
        }
        word.clear();
    };
    for (char c : text) {
        if (c == ' ') flush();
        else if (c == '\n') {
            flush();
            out.push_back(line);
            line.clear();
        } else word += c;
    }
    flush();
    if (!line.empty()) out.push_back(line);
    return out;
}

// 1.38.0 : la reduction en cours d'une edition (son prix reduit et sa fin) ; vide : aucune, ou finie.
std::string promoOf(const licensing::RegistryEditionView& e) {
    if (e.promoPrice.empty() || e.promoUntil <= static_cast<std::int64_t>(std::time(nullptr))) return {};
    return e.promoPrice;
}
// "490 EUR -> 390 EUR jusqu'au 30/11/2026" (une ligne : le tableau des editions, le rappel de la demande).
std::string priceLine(const licensing::RegistryEditionView& e) {
    const std::string promo = promoOf(e);
    if (promo.empty()) return e.price;
    return (e.price.empty() ? std::string() : e.price + " \xE2\x86\x92 ") + promo + " jusqu'au " + licence::service::dayText(e.promoUntil);
}


// ---- 1.35.0 : l'onglet Activation (le code, la licence en ligne, la flottante, la Decouverte) ----
class ActivationPanel final : public ui::Widget {
public:
    ActivationPanel(LicenceBody& body, menu::MenuManager& menus);
    // L'etat, le releve, le travail en cours : relus (rien du reseau).
    void refresh();

protected:
    void onLayout() override;
    void onPaint(const ui::PaintContext& ctx) override;

private:
    // Une ligne de l'onglet : un titre de section, un texte (coupe), un libelle et sa valeur, un champ, des boutons.
    struct Item {
        enum Kind : unsigned char { Heading, Text, Pair, Field, Buttons } kind{Text};
        std::string label, value;
        int         tone{0};                  // 0 normal, 1 bon, 2 attention, 3 erreur, 5 discret
        std::vector<ui::Widget*> widgets;
        std::vector<std::string> lines;       // onLayout
        float       y{0};
    };
    void add(Item::Kind kind, std::string label, std::string value = {}, int tone = 0, std::vector<ui::Widget*> widgets = {});
    void doActivate();
    void askRelease();
    ui::Button* button(const char* id, const std::string& text, ui::Button::Style style = ui::Button::Style::Default);

    LicenceBody&       body_;
    menu::MenuManager& menus_;   // 1.45.0 : la pile de la fenetre (avant : l'App, pour ses menus seulement)
    ui::InputText*     code_{nullptr};
    ui::InputText*     pc_{nullptr};
    ui::Button*        activate_{nullptr};
    ui::Button*        renew_{nullptr};
    ui::Button*        release_{nullptr};
    ui::Button*        leaseReturn_{nullptr};
    ui::Button*        leaseTake_{nullptr};
    ui::Button*        trial_{nullptr};
    std::vector<Item>  items_;
    std::string        releaseId_;
    licence::service::Job busy_{licence::service::Job::None};
    float              bottom_{0};
    core::ConnectionScope links_;
};

// ---- 1.34.0 / 1.35.0 : l'onglet Demande a l'editeur (la demande sans code, par le service) ----
class FeatureTablePanel;   // 1.38.0 : l'offre choisie (la colonne de droite de la boutique)

class OnlinePanel final : public ui::Widget {
public:
    OnlinePanel(LicenceBody& body, menu::MenuManager& menus);
    // Le releve, la disponibilite, le travail en cours : relus (rien du reseau).
    void refresh();
    // 1.37.0 : Mon profil remplit les champs vides (la societe, le nom, le courriel).
    void prefill();

protected:
    void onLayout() override;
    void onPaint(const ui::PaintContext& ctx) override;
    ui::EventResult onEvent(const ui::InputEvent& ev) override;

private:
    void doSend();
    void askCancel();
    void doCheck();
    void askReleasePc(const licence::PcLicence& l);   // 1.45.0 : Liberer une licence de ce PC
    ui::InputText* field(const char* id, const std::string& placeholder, std::size_t maxLen);
    // 1.37.0 : la boutique - une carte par edition du registre (son prix, sa description, ce qu'elle donne).
    struct Card {
        std::string id, label, price, description, facts;
        std::string promo, promoEnd;   // 1.38.0 : la reduction en cours (le prix reduit, "jusqu'au 30/11/2026")
        gfx::Rect   box{};
        // 1.46.0 : RIEN DE COUPE (<< 50 EUR HT /pa... >>, << Reduction jusqu'au 22/12/20... >>, << 18/21 fonctio... >>) - les
        // lignes a la largeur de la carte (onLayout), la carte aussi haute qu'il le faut (la rangee : la plus haute).
        std::vector<std::string> titleL, priceL, promoL, endL, descL, factsL;
        bool        priceInline{false};   // l'ancien prix et le nouveau sur une ligne
    };
    void buildCards();
    // 1.46.0 : les lignes d'une carte a la largeur `w`, et sa hauteur.
    [[nodiscard]] float layoutCard(Card& k, float w) const;
    // Ce que contiendra la licence d'une edition (le rappel pendant l'attente) ; vide : inconnue.
    [[nodiscard]] std::string futureText(const std::string& editionId) const;
    // 1.38.0 : la colonne de droite - les fonctionnalites de l'offre choisie (ou demandee), et l'apercu qu'on agrandit.
    void buildOffer();

    LicenceBody&       body_;
    menu::MenuManager& menus_;
    licence::PcLicencesView* pcs_{nullptr};   // 1.45.0 : les licences de ce PC, en tete
    std::vector<Card>  cards_;
    std::string        chosen_;   // l'edition choisie ; vide : l'editeur choisit
    float              shopY_{0};
    ui::InputText*     customer_{nullptr};
    ui::InputText*     contact_{nullptr};
    ui::InputText*     email_{nullptr};
    ui::InputText*     pcName_{nullptr};
    ui::InputText*     licenseId_{nullptr};
    ui::InputText*     message_{nullptr};
    ui::Button*        send_{nullptr};
    ui::Button*        cancel_{nullptr};
    ui::Button*        check_{nullptr};
    ui::Button*        profile_{nullptr};   // 1.46.0 : << Modifier mon profil... >> (sa fenetre : Mon profil n'est plus une rubrique)
    licence::Profile   filled_;             // 1.46.0 : ce que le profil a mis dans les champs (un profil modifie les remplace)
    bool               available_{false};
    std::string        why_;
    licence::service::Record rec_;
    std::string        recProblem_;
    bool               pending_{false};
    licence::service::Job busy_{licence::service::Job::None};
    float              rowsY_{0}, infoY_{0}, footY_{0}, fieldsY_{0};
    // 1.42.0 : LA PARTIE GAUCHE DEFILE (une fenetre basse : le formulaire et ses boutons ne passent plus sous le bas de
    // l'onglet). scroll_ : le decalage ; contentH_ : la hauteur de ce qu'elle montre ; la molette, et une barre fine.
    float              scroll_{0}, contentH_{0};
    ui::ScrollArea     leftBar_;   // 1.46.0 : sa barre se tire, sa gouttiere se clique (avant : seulement dessinee)
    bool               relayout_{false};
    FeatureTablePanel* offer_{nullptr};          // 1.38.0
    std::shared_ptr<ui::ITableModel> offerModel_;
    std::string        offerShown_{"\x01"};     // l'offre deja montree (rien encore)
    std::string        offerTitle_;
    float              leftW_{0};                // la largeur de la partie gauche (0 : tout)
    gfx::Rect          offerBox_{};
    core::ConnectionScope links_;
};

// ---- 1.37.0 : l'onglet Mon profil (tout facultatif : il remplit la demande ; le courriel recoit le mail de l'editeur) ----
// 1.46.0 : plus un onglet - le corps de sa petite fenetre (ProfileWindow) ; `say` : sa phrase, `saved` : apres Enregistrer.
class ProfilePanel final : public ui::Widget {
public:
    ProfilePanel(std::function<void(std::string, int)> say, std::function<void()> saved);
    void refresh();   // relu du disque

protected:
    void onLayout() override;
    void onPaint(const ui::PaintContext& ctx) override;

private:
    void doSave();
    std::function<void(std::string, int)> say_;
    std::function<void()> saved_;
    ui::InputText*        email_{nullptr};
    ui::InputText*        name_{nullptr};
    ui::InputText*        company_{nullptr};
    ui::Button*           save_{nullptr};
    float                 rowsY_{0}, markX_{0};
    core::ConnectionScope links_;
};

// ---- 1.38.0 : Mes appareils - une ligne de resume (<< 3 PC sur 20 >>) au-dessus du tableau ----
class DevicesPanel final : public ui::Widget {
public:
    DevicesPanel() : ui::Widget("dialog.licence.appareils.panneau") {
        table_ = &static_cast<ui::TableView&>(addChild(std::make_unique<ui::TableView>("dialog.licence.appareils")));
    }
    [[nodiscard]] ui::TableView& table() noexcept { return *table_; }
    void setSummary(std::string text, std::string note) {
        if (text == summary_ && note == note_) return;
        summary_ = std::move(text);
        note_ = std::move(note);
        invalidateLayout();
        invalidate();
    }
    [[nodiscard]] const std::string& summary() const noexcept { return summary_; }

protected:
    void onLayout() override {
        const auto b = bounds();
        const float top = summary_.empty() ? 0.f : (note_.empty() ? 30.f : 50.f);
        table_->setBounds({b.x, b.y + top, b.w, std::max(40.f, b.h - top)});
    }
    void onPaint(const ui::PaintContext& ctx) override {
        if (summary_.empty()) return;
        auto& r = ctx.r;
        const auto& c = ctx.theme.color;
        const auto b = bounds();
        const std::string t = fit(r, summary_, kValue, b.w - 16.f);
        r.drawText({b.x + 8.f, b.y + 5.f}, t, kValue, c.text);
        r.drawText({b.x + 9.f, b.y + 5.f}, t, kValue, c.text);
        if (!note_.empty()) r.drawText({b.x + 8.f, b.y + 29.f}, fit(r, note_, kSmall, b.w - 16.f), kSmall, c.textMuted);
    }

private:
    ui::TableView* table_{nullptr};
    std::string    summary_, note_;
};

// ---- 1.42.0 : Options - ajouter une fonction a sa licence pour un temps (le catalogue de l'editeur, sa demande) ----
class OptionsPanel final : public ui::Widget {
public:
    OptionsPanel(LicenceBody& body, menu::MenuManager& menus);
    void refresh();
    // Choisir une option (une action fermee qui la propose, << Prolonger... >>).
    void choose(const std::string& optionId);
    [[nodiscard]] const std::string& chosen() const noexcept { return chosen_; }

protected:
    void onLayout() override;
    void onPaint(const ui::PaintContext& ctx) override;

private:
    struct Row {
        std::string id;
        bool        active{false};
        bool        inOffer{false};
    };
    void loadForm();
    void send();
    [[nodiscard]] std::int64_t now() const;
    [[nodiscard]] int days() const;   // -1 : des dates
    [[nodiscard]] std::string priceOf(int days) const;

    LicenceBody&     body_;
    menu::MenuManager& menus_;
    ui::TableView*   table_{nullptr};
    std::shared_ptr<ui::ITableModel> model_;
    ui::DropDown*    duration_{nullptr};
    ui::DropDown*    start_{nullptr};
    ui::DatePicker*  from_{nullptr};
    ui::DatePicker*  to_{nullptr};
    ui::InputText*   message_{nullptr};
    ui::Button*      send_{nullptr};
    ui::Button*      extend_{nullptr};
    std::vector<Row> rows_;
    std::string      chosen_, edition_, licenseId_, customer_;
    std::vector<std::string> activeLines_;   // << Exports avances - active, encore 6 j - jusqu'au dim. 15/11/2026 au soir >>
    std::string      activeId_;               // l'option en cours (Prolonger...)
    std::string      blocked_;                // vide : la demande peut partir ; sinon pourquoi pas
    std::string      pendingText_;            // une demande d'option en attente
    gfx::Rect        formBox_{}, activeBox_{};
    float            leadY_{0}, footY_{0};
    bool             loading_{false};
    core::ConnectionScope links_;
};

// ---- 1.42.0 : Mes achats - l'historique complet du client (offres, prolongations, options ; fini ou en cours) ----
class PurchasesPanel final : public ui::Widget {
public:
    explicit PurchasesPanel(LicenceBody& body);
    void refresh();
    // Les lignes montrees (le filtre) : les essais et le CSV.
    [[nodiscard]] std::size_t shownCount() const noexcept { return shown_.size(); }
    [[nodiscard]] std::string csv() const;

protected:
    void onLayout() override;
    void onPaint(const ui::PaintContext& ctx) override;

private:
    void rebuild();
    void exportCsv();
    [[nodiscard]] std::int64_t now() const;

    LicenceBody&                     body_;
    ui::TableView*                   table_{nullptr};
    std::shared_ptr<ui::ITableModel> model_;
    ui::Button*                      all_{nullptr};
    ui::Button*                      offers_{nullptr};
    ui::Button*                      options_{nullptr};
    ui::Button*                      export_{nullptr};
    std::vector<licensing::Purchase> all__;
    std::vector<licensing::Purchase> shown_;
    int                              filter_{0};   // 0 tout, 1 offres, 2 options
    std::string                      tile_[3][3];  // Ton offre, Option en cours, Depuis le : titre, valeur, detail
    gfx::Rect                        tiles_[3]{}, timeline_{};
    float                            leadY_{0};
    core::ConnectionScope            links_;
};

// ---- 1.38.0 : la fonctionnalite d'une ligne (Editions, Fonctionnalites, la boutique) : id vide, une ligne de resume ----
struct FeatureInfo {
    std::string id, label, product, editions;
    // 1.46.0 : barree dans son tableau (pas dans ta licence, pas dans l'offre choisie) - ses captures se montrent quand meme
    bool        locked{false};
    std::string lockedText;
};
// 1.45.0 : la capture qu'on agrandit et la capture en grand sont dans LicenceViews (les essais les construisent seules).
using ZoomImage = licence::CaptureView;
using licence::capturePng;
// La capture en grand (une fenetre au-dessus de la licence) : toutes les captures de la fonctionnalite, celle-ci d'abord
// (1.45.0 : ses fleches, sa legende, ses bulles). 1.46.0 : `choices` - les fonctionnalites du tableau d'ou elle est
// ouverte : on y change de fonctionnalite sans la fermer (les barrees comprises).
void showCapture(menu::MenuManager& menus, const FeatureInfo& f, const licence::FeaturePreview& preview, std::size_t shot,
                 const std::vector<hmi::Resource>& loaded = {}, std::vector<licence::CaptureChoice> choices = {});

// ---- 1.38.0 : l'apercu de la fonctionnalite choisie (sa capture qu'on agrandit, deux lignes, ses editions) ----
class FeaturePreviewPane final : public ui::Widget {
public:
    explicit FeaturePreviewPane(std::string id);
    void show(const FeatureInfo& info);
    void setEmptyText(std::string t) {
        waiting_ = std::move(t);
        invalidate();
    }
    // La capture en grand (une fenetre au-dessus) : la fonctionnalite, ses captures, celle montree (1.45.0 : toutes, pour
    // les fleches de la fenetre en grand ; et celles deja lues, pour ne pas les relire).
    std::function<void(const FeatureInfo&, const licence::FeaturePreview&, std::size_t, const std::vector<hmi::Resource>&)> onEnlarge;
    [[nodiscard]] const FeatureInfo& shown() const noexcept { return shown_; }
    [[nodiscard]] const licence::FeaturePreview& preview() const noexcept { return preview_; }
    [[nodiscard]] ZoomImage& image() noexcept { return *image_; }
    [[nodiscard]] std::size_t shot() const noexcept { return shot_; }

protected:
    void onLayout() override;
    void onPaint(const ui::PaintContext& ctx) override;

private:
    void applyShot();   // 1.44.0 : montrer la capture shot_
    ZoomImage*              image_{nullptr};
    ui::Button*             prev_{nullptr};   // 1.44.0 : plusieurs captures - la precedente, la suivante
    ui::Button*             next_{nullptr};
    std::size_t             shot_{0};
    float                   navY_{0.f};
    core::ConnectionScope   links_;
    FeatureInfo             shown_;
    licence::FeaturePreview preview_;
    std::vector<hmi::Resource> loaded_;   // 1.45.0 : les captures deja lues (la meme place que preview_.shots)
    std::string             waiting_{"Choisis une fonctionnalit\xC3\xA9 dans la liste : une capture et ce qu'elle fait s'affichent ici."};
    float                   textY_{0};
};

// ---- 1.37.0 : l'onglet Editions - le tableau, et l'apercu de la fonctionnalite choisie ; 1.38.0 : aussi l'onglet
// Fonctionnalites (le meme volet de droite, qu'on agrandit). ----
class FeatureTablePanel final : public ui::Widget {
public:
    FeatureTablePanel(std::string id, std::string tableId);
    [[nodiscard]] ui::TableView& table() noexcept { return *table_; }
    [[nodiscard]] FeaturePreviewPane& pane() noexcept { return *pane_; }
    using RowInfo = FeatureInfo;
    void setRows(std::vector<RowInfo> rows);
    // 1.38.0 : la liste au-dessus de l'apercu (une colonne etroite : la boutique).
    void setVertical(bool on) {
        vertical_ = on;
        invalidateLayout();
    }
    // Les essais : ce que montre l'apercu.
    [[nodiscard]] const std::string& shownFeature() const noexcept { return pane_->shown().id; }
    [[nodiscard]] const licence::FeaturePreview& preview() const noexcept { return pane_->preview(); }
    // 1.46.0 : les fonctionnalites du tableau (dans son ordre, les barrees comprises) : la liste de la capture en grand.
    [[nodiscard]] std::vector<licence::CaptureChoice> choices() const {
        std::vector<licence::CaptureChoice> out;
        for (const auto& r : rows_)
            if (!r.id.empty()) out.push_back({r.id, r.label, r.product, r.locked, r.lockedText});
        return out;
    }

protected:
    void onLayout() override;

private:
    void choose();
    ui::TableView*        table_{nullptr};
    FeaturePreviewPane*   pane_{nullptr};
    std::vector<RowInfo>  rows_;
    bool                  vertical_{false};
    core::ConnectionScope links_;
};
using EditionsPanel = FeatureTablePanel;

class LicenceBody final : public ui::Widget {
public:
    LicenceBody(LicenceWindow& owner, menu::MenuManager& menus, int tab);
    void reload();
    // 1.45.0 : montrer cet onglet (Regarder, une fenetre Licence deja ouverte qu'on redemande) ; kTabAuto : le plus utile.
    // 1.46.0 : une RUBRIQUE (son fil d'Ariane en haut) ; kTabHome : l'accueil ; kTabProfile : la fenetre Mon profil par-dessus.
    void showTab(int tab);
    [[nodiscard]] int autoTab() const;
    // 1.46.0 : la rubrique montree (kTabHome : l'accueil).
    [[nodiscard]] int section() const noexcept { return current_; }
    // 1.46.0 : revenir a l'accueil (<< Licence >> du fil, Retour arriere, Alt+gauche) ; faux : deja a l'accueil.
    bool back();
    [[nodiscard]] licence::LicenceCrumbs& crumbs() noexcept { return *crumbs_; }
    // 1.34.0 : chaque image (la fenetre) : un travail du service fini -> sa phrase, tout relu.
    void poll();
    void say(std::string text, int tone) {
        flash_ = std::move(text);
        flashTone_ = tone;
        invalidate();
    }
    // 1.37.0 : Mon profil enregistre - la demande reprend ce qui y manque.
    void profileSaved() {
        if (online_) online_->prefill();
    }

protected:
    void onLayout() override;
    void onPaint(const ui::PaintContext& ctx) override;
    ui::EventResult onEvent(const ui::InputEvent& ev) override;

private:
    void askImport();
    void doImport(const std::string& path);
    void askRequest();
    void refreshHome();   // 1.46.0 : ce que dit deja chaque rubrique, sur l'accueil
    ui::Widget* addPage(int tab, ui::WidgetPtr page);

    LicenceWindow&    owner_;
    menu::MenuManager& menus_;
    // 1.46.0 : plus de bande d'onglets - l'accueil, le fil d'Ariane, une page par rubrique (son numero : licence::kTab*)
    licence::LicenceHome*   home_{nullptr};
    licence::LicenceCrumbs* crumbs_{nullptr};
    std::array<ui::Widget*, static_cast<std::size_t>(licence::kTabPurchases) + 1> pages_{};
    int               current_{licence::kTabHome};
    ui::PropertyGrid* mine_{nullptr};
    ui::TableView*    devices_{nullptr};
    DevicesPanel*     devicesPanel_{nullptr};    // 1.38.0 : << 3 PC sur 20 >>
    ui::TableView*    features_{nullptr};
    FeatureTablePanel* featuresPanel_{nullptr};  // 1.38.0 : avec l'apercu qu'on agrandit
    ui::TableView*    editions_{nullptr};
    EditionsPanel*    editionsPanel_{nullptr};   // 1.37.0 : avec l'apercu
    ui::TableView*    history_{nullptr};
    ui::PropertyGrid* params_{nullptr};
    OnlinePanel*      online_{nullptr};      // 1.34.0
    ActivationPanel*  activation_{nullptr};  // 1.35.0
    OptionsPanel*     options_{nullptr};     // 1.42.0
    PurchasesPanel*   purchases_{nullptr};   // 1.42.0
    unsigned long long seenRevision_{0};     // 1.34.0 : le dernier travail du service releve
    bool              wasBusy_{false};
    licensing::Seconds devicesCheckedAt_{0};  // 1.38.0 : la derniere fois que Mes appareils a regarde l'age de sa liste
    ui::Button*       import_{nullptr};
    ui::Button*       request_{nullptr};
    ui::Button*       folder_{nullptr};
    ui::Button*       close_{nullptr};
    std::shared_ptr<ui::ITableModel> devicesModel_, featuresModel_, editionsModel_, historyModel_;
    licensing::LicenseState state_;
    std::string       flash_;
    int               flashTone_{0};
    gfx::Rect         panel_{}, closeBox_{};
    float             headerY_{0}, messageY_{0}, pageTop_{0};
    core::ConnectionScope links_;
};

// 1.45.0 : LA fenetre Licence ouverte (une seule a la fois : showLicenceWindow) - fil de l'interface.
LicenceWindow* g_licenceWindow = nullptr;

class LicenceWindow final : public menu::WidgetMenu {
public:
    LicenceWindow(menu::MenuManager& menus, int tab) : menu::WidgetMenu("dialog.licence"), menus_(menus), tab_(tab) { g_licenceWindow = this; }
    ~LicenceWindow() override {
        if (g_licenceWindow == this) g_licenceWindow = nullptr;
    }
    [[nodiscard]] menu::MenuTraits traits() const override { return dialogTraits(); }
    [[nodiscard]] std::string title() const override { return "Licence"; }
    ui::EventResult HandleEvent(const ui::InputEvent& ev) override {
        if (const auto* k = std::get_if<ui::KeyDown>(&ev)) {
            if (k->key == ui::Key::Escape) {
                if (body_ && body_->crumbs().siblingsOpen()) {   // le menu du fil ouvert : il se ferme d'abord
                    body_->crumbs().siblings().close();
                    return ui::EventResult::Consumed;
                }
                fermer();
                return ui::EventResult::Consumed;
            }
            // 1.46.0 : Alt+gauche - l'accueil (comme un navigateur) ; Retour arriere aussi, hors d'un champ (il y efface)
            if (body_ && k->key == ui::Key::Left && k->mods.alt && !k->mods.ctrl && !k->mods.shift && body_->back()) return ui::EventResult::Consumed;
            if (body_ && k->key == ui::Key::Backspace && k->mods.none()) {
                if (menu::WidgetMenu::HandleEvent(ev) == ui::EventResult::Consumed) return ui::EventResult::Consumed;
                (void)body_->back();
                return ui::EventResult::Consumed;
            }
        }
        return menu::WidgetMenu::HandleEvent(ev);
    }
    void Update(const menu::FrameContext& f) override {   // 1.34.0 : le fil du service des licences
        menu::WidgetMenu::Update(f);
        if (body_) body_->poll();
    }
    void fermer() {
        if (done_) return;
        done_ = true;
        if (g_licenceWindow == this) g_licenceWindow = nullptr;   // 1.45.0 : elle part - une nouvelle peut s'ouvrir
        manager().CloseDialog(menu::DialogResult{menu::DialogResult::Button::Cancel, {}});
    }
    [[nodiscard]] bool closing() const noexcept { return done_; }
    // 1.45.0 : redemandee alors qu'elle est ouverte - relue, sur l'onglet demande (kTabAuto : celui qu'elle montre).
    // 1.46.0 : kTabProfile - la fenetre Mon profil, par-dessus (ce n'est plus une rubrique).
    void goTo(int tab) {
        if (tab == licence::kTabProfile) {
            showProfileWindow(menus_);
            return;
        }
        if (!body_) {   // pas encore construite (demandee deux fois dans la meme image)
            if (tab != licence::kTabAuto) tab_ = tab;
            return;
        }
        body_->reload();
        if (tab != licence::kTabAuto) body_->showTab(tab);
    }
    // 1.46.0 : la rubrique montree (kTabHome : l'accueil ; pas encore construite : celle demandee).
    [[nodiscard]] int section() const noexcept { return body_ ? body_->section() : tab_; }
    // 1.46.0 : Mon profil enregistre (sa fenetre) : la demande reprend ce qui y manque.
    void profileSaved() {
        if (body_) body_->profileSaved();
    }

protected:
    core::Status buildUi() override {
        auto body = std::make_unique<LicenceBody>(*this, menus_, tab_);
        body_ = body.get();
        setRoot(std::move(body));
        return core::ok();
    }

private:
    menu::MenuManager& menus_;
    int          tab_{0};
    bool         done_{false};
    LicenceBody* body_{nullptr};
};

LicenceBody::LicenceBody(LicenceWindow& owner, menu::MenuManager& menus, int tab) : ui::Widget("dialog.licence.body"), owner_(owner), menus_(menus) {
    // 1.46.0 : l'accueil (les rubriques en groupes) et le fil d'Ariane, puis une page par rubrique (une seule montree)
    home_ = &static_cast<licence::LicenceHome&>(addChild(std::make_unique<licence::LicenceHome>("licence.accueil")));
    home_->onOpen = [this](int t) { showTab(t); };
    crumbs_ = &static_cast<licence::LicenceCrumbs&>(addChild(std::make_unique<licence::LicenceCrumbs>("dialog.licence.fil")));
    crumbs_->onHome = [this] { showTab(licence::kTabHome); };
    crumbs_->onSection = [this](int t) { showTab(t); };
    auto mine = std::make_unique<ui::PropertyGrid>("dialog.licence.ma-licence");
    mine_ = mine.get();
    auto devices = std::make_unique<DevicesPanel>();   // 1.38.0 : le resume au-dessus du tableau
    devices->table().setColumns({{"PC", 260.f}, {"Empreinte de sa cl\xC3\xA9", 260.f}, {"Activ\xC3\xA9 le", 150.f}, {"", 90.f}});
    devicesPanel_ = devices.get();
    devices_ = &devices->table();
    // 1.38.0 : le tableau des fonctionnalites et, a droite, l'apercu de celle choisie (comme l'onglet Editions).
    auto features = std::make_unique<FeatureTablePanel>("dialog.licence.fonctionnalites.panneau", "dialog.licence.fonctionnalites");
    features->table().setColumns({{"Fonctionnalit\xC3\xA9", 200.f}, {"Identifiant", 150.f}, {"Logiciel", 100.f}, {"\xC3\x89tat", 150.f},
                                  {"Limites", 140.f}, {"Dans le programme", 320.f}});
    featuresPanel_ = features.get();
    features_ = &features->table();
    auto editions = std::make_unique<EditionsPanel>("dialog.licence.editions.panneau", "dialog.licence.editions");   // 1.37.0 : avec l'apercu
    editionsPanel_ = editions.get();
    editions_ = &editions->table();
    // 1.46.0 : en grand, la liste du tableau a cote (changer de fonctionnalite sans fermer, les barrees comprises)
    featuresPanel_->pane().onEnlarge = [this](const FeatureInfo& f, const licence::FeaturePreview& p, std::size_t shot,
                                              const std::vector<hmi::Resource>& loaded) {
        showCapture(menus_, f, p, shot, loaded, featuresPanel_->choices());
    };
    editionsPanel_->pane().onEnlarge = [this](const FeatureInfo& f, const licence::FeaturePreview& p, std::size_t shot,
                                              const std::vector<hmi::Resource>& loaded) {
        showCapture(menus_, f, p, shot, loaded, editionsPanel_->choices());
    };
    auto history = std::make_unique<ui::TableView>("dialog.licence.historique");
    history->setColumns({{"Quand (UTC)", 150.f}, {"Quoi", 120.f}, {"R\xC3\xA9sultat", 90.f}, {"D\xC3\xA9tail", 620.f}});
    history_ = history.get();
    auto params = std::make_unique<ui::PropertyGrid>("dialog.licence.parametres");
    params_ = params.get();
    auto online = std::make_unique<OnlinePanel>(*this, menus);   // 1.34.0
    online_ = online.get();
    auto activation = std::make_unique<ActivationPanel>(*this, menus);   // 1.35.0
    activation_ = activation.get();
    auto options = std::make_unique<OptionsPanel>(*this, menus);   // 1.42.0
    options_ = options.get();
    auto purchases = std::make_unique<PurchasesPanel>(*this);    // 1.42.0
    purchases_ = purchases.get();
    (void)addPage(licence::kTabActivation, std::move(activation));   // 1.35.0
    (void)addPage(licence::kTabMine, std::move(mine));
    (void)addPage(licence::kTabDevices, std::move(devices));
    (void)addPage(licence::kTabFeatures, std::move(features));
    (void)addPage(licence::kTabEditions, std::move(editions));
    (void)addPage(licence::kTabHistory, std::move(history));
    (void)addPage(licence::kTabSettings, std::move(params));
    (void)addPage(licence::kTabRequest, std::move(online));   // 1.34.0 ; 1.36.0 : Demander ma licence
    (void)addPage(licence::kTabOptions, std::move(options));   // 1.42.0
    (void)addPage(licence::kTabPurchases, std::move(purchases));
    // (1.37.0 : Mon profil etait l'onglet 8 ; 1.46.0 : sa fenetre a part, showProfileWindow)
    import_ = &static_cast<ui::Button&>(addChild(std::make_unique<ui::Button>("Activer par fichier de licence\xE2\x80\xA6", "dialog.licence.importer")));
    import_->setStyle(ui::Button::Style::Primary);
    request_ = &static_cast<ui::Button&>(addChild(std::make_unique<ui::Button>("Demander une activation\xE2\x80\xA6", "dialog.licence.demander")));
    // 1.36.0 : avec le service des licences, la demande en ligne est le chemin principal (Demander ma licence) ;
    // les fichiers restent pour un PC sans Internet - en second.
    if (licence::onlineRequests()) {
        import_->setText("Fichier de licence\xE2\x80\xA6");
        import_->setStyle(ui::Button::Style::Default);
        import_->setTooltip("Un fichier .xpglic re\xC3\xA7u de l'\xC3\xA9" "diteur (un PC sans Internet). Avec Internet, demande plut\xC3\xB4t ta licence "
                            "(Licence \xE2\x80\xBA Demander ma licence) : elle s'installe d'elle-m\xC3\xAAme.");
        request_->setText("Demande par fichier (sans Internet)\xE2\x80\xA6");
        request_->setTooltip("\xC3\x89" "crit une demande (.xpgreq) \xC3\xA0 envoyer \xC3\xA0 l'\xC3\xA9" "diteur toi-m\xC3\xAAme : seulement pour un PC sans Internet.");
    }
    folder_ = &static_cast<ui::Button&>(addChild(std::make_unique<ui::Button>("Ouvrir le dossier des licences", "dialog.licence.dossier")));
    close_ = &static_cast<ui::Button&>(addChild(std::make_unique<ui::Button>("Fermer", "dialog.licence.fermer")));
    links_ += import_->clicked->connect([this] { askImport(); });
    links_ += request_->clicked->connect([this] { askRequest(); });
    links_ += folder_->clicked->connect([this] {
        const std::string f = licensing::getLicenseFolder();
        std::error_code ec;
        std::filesystem::create_directories(std::filesystem::path(std::u8string(f.begin(), f.end())), ec);
        if (auto why = dossiers::ouvrirDansLeSysteme(f); !why.empty()) say("Le dossier ne s'ouvre pas : " + why, 3);
    });
    links_ += close_->clicked->connect([this] { owner_.fermer(); });
    seenRevision_ = licence::service::revision();
    wasBusy_ = licence::service::busy();
    reload();
    showTab(tab == licence::kTabProfile ? licence::kTabAuto : tab);   // Mon profil : sa fenetre (showLicenceWindow l'ouvre)
}

ui::Widget* LicenceBody::addPage(int tab, ui::WidgetPtr page) {
    ui::Widget* w = &addChild(std::move(page));
    w->setVisibility(ui::Visibility::Collapsed);
    if (tab >= 0 && static_cast<std::size_t>(tab) < pages_.size()) pages_[static_cast<std::size_t>(tab)] = w;
    return w;
}

int LicenceBody::autoTab() const {
    // 1.46.0 : kTabAuto - l'accueil (le resume de la licence, toutes les rubriques), sauf la ou une rubrique est plus utile :
    // sans licence valable, la demande en ligne (ou l'activation sans service) ; un bail fini, un poste flottant a prendre :
    // l'activation. (1.35.0 : Ma licence pour une licence par fichier valable, sinon Activation.)
    using S = licensing::LicenseStatus;
    const bool valid = state_.status == S::Valid || state_.status == S::Grace;
    if (valid || state_.status == S::NotConfigured) return licence::kTabHome;
    if (state_.status == S::LeaseExpired || state_.status == S::NoFloatingSeat) return licence::kTabActivation;
    return licence::onlineRequests() ? licence::kTabRequest : licence::kTabActivation;   // 1.36.0 : la demande en ligne
}

void LicenceBody::showTab(int tab) {
    if (tab == licence::kTabAuto) tab = autoTab();
    if (tab == licence::kTabProfile) {   // 1.46.0 : plus une rubrique - sa fenetre, par-dessus celle-ci
        showProfileWindow(menus_);
        return;
    }
    if (tab != licence::kTabHome && (tab < 0 || static_cast<std::size_t>(tab) >= pages_.size() || !pages_[static_cast<std::size_t>(tab)]))
        tab = licence::kTabHome;
    current_ = tab;
    home_->setVisibility(tab == licence::kTabHome ? ui::Visibility::Visible : ui::Visibility::Collapsed);
    for (std::size_t i = 0; i < pages_.size(); ++i)
        if (pages_[i]) pages_[i]->setVisibility(static_cast<int>(i) == tab ? ui::Visibility::Visible : ui::Visibility::Collapsed);
    crumbs_->setSection(tab);
    if (tab == licence::kTabHome) refreshHome();
    invalidateLayout();
    invalidate();
}

bool LicenceBody::back() {
    if (current_ == licence::kTabHome) return false;
    showTab(licence::kTabHome);
    return true;
}

void LicenceBody::refreshHome() {
    namespace L = licence;
    const auto& st = state_;
    using S = licensing::LicenseStatus;
    const bool hasLicence = !st.licenseId.empty();
    const auto reg = licensing::getRegistry();
    const std::int64_t now = static_cast<std::int64_t>(std::time(nullptr));
    // Ma licence : son edition et son etat ; Mes appareils : ses PC
    {
        const std::string edition = !st.licensedEdition.empty() ? st.licensedEdition : st.edition;
        const bool good = st.status == S::Valid;
        const int tone = good ? 1 : (st.status == S::Grace || st.status == S::Evaluation || st.status == S::Absent) ? 2 : st.status == S::NotConfigured ? 0 : 3;
        home_->setHint(L::kTabMine, (hasLicence && !edition.empty() ? edition + " \xC2\xB7 " : std::string()) + licensing::statusLabel(st.status), tone);
        home_->setHint(L::kTabDevices, hasLicence && st.maxDevices > 0 ? std::to_string(std::max(1, st.boundDevices)) + " PC sur " + std::to_string(st.maxDevices)
                                                                       : std::string(), 0);
        const std::size_t events = licensing::getHistory(200).size();
        home_->setHint(L::kTabHistory, events ? std::to_string(events) + (events > 1 ? " \xC3\xA9v\xC3\xA9nements" : " \xC3\xA9v\xC3\xA9nement") : std::string(), 0);
    }
    // Demander ma licence : la demande qui attend, ou les offres en vente ; Activation : le bail, la Decouverte
    {
        const auto pr = L::pendingRequest();
        std::size_t onSale = 0;
        if (reg)
            for (const auto& e : reg->editions)
                if (!e.retired && e.id != reg->unlicensedEdition && e.onSale(now)) ++onSale;
        if (pr.pending) home_->setHint(L::kTabRequest, "demande n\xC2\xB0 " + std::to_string(pr.number) + " en attente", 2);
        else if (L::onlineRequests() && onSale) home_->setHint(L::kTabRequest, std::to_string(onSale) + (onSale > 1 ? " offres" : " offre"), 0);
        else home_->setHint(L::kTabRequest, {}, 0);
        if (st.online && st.activationUntil)
            home_->setHint(L::kTabActivation, "bail jusqu'au " + L::dayOf(st.activationUntil), st.activationWarning ? 2 : 0);
        else if (st.status == S::LeaseExpired) home_->setHint(L::kTabActivation, "bail expir\xC3\xA9", 3);
        else if (st.status == S::Evaluation)
            home_->setHint(L::kTabActivation, "D\xC3\xA9" "couverte : " + std::to_string(std::max(0, st.evaluationDaysLeft)) + " j", st.evaluationDaysLeft <= 7 ? 2 : 0);
        else home_->setHint(L::kTabActivation, {}, 0);
    }
    // Options : celles en cours ; Mes achats : combien
    {
        int active = 0;
        for (const auto& o : st.options)
            if (o.activeAt(st.trustedNow ? st.trustedNow : static_cast<licensing::Seconds>(now), st.expiresAt)) ++active;
        home_->setHint(L::kTabOptions, active ? std::to_string(active) + (active > 1 ? " actives" : " active") : std::string(), active ? 1 : 0);
        const std::size_t bought = !st.purchases.empty() ? st.purchases.size() : (hasLicence ? 1u + st.options.size() : 0u);
        home_->setHint(L::kTabPurchases, bought ? std::to_string(bought) + (bought > 1 ? " achats" : " achat") : std::string(), 0);
    }
    // Fonctionnalites : celles ouvertes ; Editions : les offres ; Parametres : le registre
    {
        std::size_t all = 0, open = 0, editions = 0;
        if (reg) {
            for (const auto& f : reg->features)
                if (!f.retired) {
                    ++all;
                    if (L::allows(f.id)) ++open;
                }
            for (const auto& e : reg->editions)
                if (!e.retired && e.id != reg->unlicensedEdition && e.onSale(now)) ++editions;
        }
        home_->setHint(L::kTabFeatures, all ? std::to_string(open) + "/" + std::to_string(all) + (open > 1 ? " ouvertes" : " ouverte") : std::string(), 0);
        home_->setHint(L::kTabEditions, editions ? std::to_string(editions) + (editions > 1 ? " offres" : " offre") : std::string(), 0);
        home_->setHint(L::kTabSettings, st.registryNumber ? "registre n\xC2\xB0 " + std::to_string(st.registryNumber) : std::string("aucun registre"), 0);
    }
}

void LicenceBody::poll() {
    namespace lg = licence::service;
    lg::poll();
    if (const auto rev = lg::revision(); rev != seenRevision_) {
        seenRevision_ = rev;
        wasBusy_ = lg::busy();
        const auto r = lg::lastResult();
        if (!r.message.empty()) say(r.message, r.licenseInstalled || (r.ok && !r.refused) ? 1 : (r.refused || r.ok) ? 2 : 3);
        reload();
        return;
    }
    if (const bool b = lg::busy(); b != wasBusy_) {
        wasBusy_ = b;
        online_->refresh();
        activation_->refresh();
        options_->refresh();   // 1.42.0 : Envoyer la demande (un travail du service)
    }
    // 1.38.0 : Mes appareils ouvert sur une licence en ligne - les PC de la licence relus au service s'ils ont plus de
    // 10 minutes (un renouvellement du bail, au plus toutes les 10 minutes) : << N PC sur 20 >> suit les autres PC
    // sans attendre le renouvellement automatique (7 jours avant la fin du bail).
    if (current_ == licence::kTabDevices && state_.online && !state_.licenseId.empty() && !lg::busy()) {
        constexpr licensing::Seconds kDevicesMaxAge = 600;
        const licensing::Seconds now = licensing::systemNow();
        if (now - devicesCheckedAt_ >= kDevicesMaxAge) {
            devicesCheckedAt_ = now;
            const auto known = lg::onlineDevices(state_.licenseId);
            if (!known || now - known->receivedAt >= kDevicesMaxAge) (void)lg::startRenew();
        }
    }
}

void LicenceBody::reload() {
    licence::refresh();
    state_ = licensing::getLicenseState();
    const auto& st = state_;
    const bool configured = licensing::isConfigured();
    const auto reg = licensing::getRegistry();

    // ---- Ma licence ----
    {
        std::vector<PG::Category> cats;
        PG::Category c;
        c.name = "Ma licence";
        c.properties.push_back(prop("\xC3\x89tat", licensing::statusLabel(st.status), licence::stateText(st)));
        if (!st.licensedEdition.empty() || !st.edition.empty())
            c.properties.push_back(prop("\xC3\x89" "dition", st.licensedEdition.empty() ? st.edition : st.licensedEdition,
                                        "L'\xC3\xA9" "dition de la licence ; sans licence valable, aucun projet ne s'ouvre ni ne se cr\xC3\xA9" "e (1.35.1)."));
        if (!st.customer.empty()) c.properties.push_back(prop("Propri\xC3\xA9taire", st.customer));
        if (!st.licenseId.empty()) c.properties.push_back(prop("Identifiant", st.licenseId));
        if (!st.licenseId.empty()) c.properties.push_back(prop("Type", typeLabel(st.type)));
        if (st.issuedAt)
            c.properties.push_back(prop("Valable", "du " + dateOf(st.issuedAt) + (st.expiresAt ? " au " + dateOf(st.expiresAt) : std::string(" (sans fin)"))));
        if (st.graceEndsAt) c.properties.push_back(prop("Gr\xC3\xA2" "ce jusqu'au", dateOf(st.graceEndsAt),
                                                        "Apr\xC3\xA8s la fin, les fonctionnalit\xC3\xA9s restent ouvertes ces jours-l\xC3\xA0, le temps d'importer le renouvellement."));
        if (!st.licenseId.empty())
            c.properties.push_back(prop("PC", std::to_string(st.boundDevices) + " li\xC3\xA9(s) sur " + std::to_string(st.maxDevices) + " autoris\xC3\xA9(s)"));
        c.properties.push_back(prop("Signature", configured ? std::string("v\xC3\xA9rifi\xC3\xA9" "e hors ligne avec la cl\xC3\xA9 de l'\xC3\xA9" "diteur compil\xC3\xA9" "e dans ce programme")
                                                            : std::string("ce programme n'a pas encore la cl\xC3\xA9 de l'\xC3\xA9" "diteur")));
        c.properties.push_back(prop("R\xC3\xA9vocations", st.revocationNumber ? "liste n\xC2\xB0 " + std::to_string(st.revocationNumber) : std::string("aucune liste")));
        if (st.status == licensing::LicenseStatus::Evaluation)   // 1.34.0 ; 1.35.0 : la Decouverte
            c.properties.push_back(prop("D\xC3\xA9" "couverte", licence::discoveryText(st),
                                        "La D\xC3\xA9" "couverte de ce PC (30 jours), depuis son premier lancement : les fonctionnalit\xC3\xA9s de "
                                        "l'\xC3\xA9" "dition d'\xC3\xA9valuation du registre. Ensuite, sans licence, aucun projet ne s'ouvre ni ne se cr\xC3\xA9" "e."));
        if (st.online) {   // 1.35.0 : la licence en ligne
            c.properties.push_back(prop("Activation", "en ligne, par le service des licences",
                                        "Ce PC a son activation sign\xC3\xA9" "e par le service des licences de l'\xC3\xA9" "diteur ; elle vaut le temps d'un bail, "
                                        "renouvel\xC3\xA9 \xC3\xA0 chaque connexion."));
            c.properties.push_back(prop("Bail jusqu'au", st.activationUntil ? licence::service::dateText(st.activationUntil) + " (" +
                                                                                 std::to_string(std::max(0, st.activationDaysLeft)) + " j)"
                                                                           : std::string("aucun bail valable"),
                                        "Sans connexion avant cette date, les fonctions de la licence se mettent en pause sur ce PC, jusqu'\xC3\xA0 la "
                                        "prochaine connexion. Reculer l'horloge ne prolonge pas le bail."));
            if (st.lastServiceSync)
                c.properties.push_back(prop("Derni\xC3\xA8re synchronisation", licence::service::dateText(st.lastServiceSync),
                                            "La derni\xC3\xA8re heure sign\xC3\xA9" "e re\xC3\xA7ue du service."));
        }
        if (st.floating)
            c.properties.push_back(prop("Poste flottant", st.leaseUntil ? "pris jusqu'\xC3\xA0 " + licence::service::dateText(st.leaseUntil)
                                                                        : std::string("aucun poste pris"),
                                        "Une licence flottante : un poste du nombre achet\xC3\xA9 est pris tant que le programme tourne, et rendu \xC3\xA0 sa fermeture."));
        if (!st.detail.empty()) c.properties.push_back(prop("D\xC3\xA9tail", st.detail));
        cats.push_back(std::move(c));
        PG::Category a;
        a.name = "Activer une licence";
        {
            std::string why;   // 1.35.0 : le code, par le service des licences
            if (licence::service::available(&why))
                a.properties.push_back(prop("Avec un code", "Licence \xE2\x80\xBA Activation (le code XPG-XXXX-XXXX-XXXX-XXXX re\xC3\xA7u de l'\xC3\xA9" "diteur)",
                                            "Le service des licences v\xC3\xA9rifie le code et active la licence sur ce PC, tout de suite, sans fichier."));
            else a.properties.push_back(prop("Avec un code", "pas dans ce programme", why));
        }
        a.properties.push_back(prop("Par fichier", "Activer par fichier de licence\xE2\x80\xA6 (le .xpglic re\xC3\xA7u de l'\xC3\xA9" "diteur)",
                                    "Le fichier contient la licence sign\xC3\xA9" "e, le registre et, s'il y en a une, la liste de r\xC3\xA9vocation. Elle "
                                    "n'est install\xC3\xA9" "e que si elle vaut pour ce PC, maintenant."));
        a.properties.push_back(prop("La demande", "Demander une activation\xE2\x80\xA6 (un .xpgreq \xC3\xA0 envoyer \xC3\xA0 l'\xC3\xA9" "diteur)",
                                    "Elle porte la cl\xC3\xA9 publique de ce PC et le nom que tu lui donnes ; rien de son mat\xC3\xA9riel."));
        {
            std::string why;   // 1.34.0 ; 1.35.0 : par le service des licences
            if (licence::service::available(&why))
                a.properties.push_back(prop("Sans code", "Licence \xE2\x80\xBA Demander ma licence (par le service des licences)",
                                            "La demande part chez l'\xC3\xA9" "diteur, scell\xC3\xA9" "e pour lui ; sa r\xC3\xA9ponse revient ici : une licence "
                                            "accept\xC3\xA9" "e s'installe d'elle-m\xC3\xAA" "me, sans fichier."));
            else a.properties.push_back(prop("Sans code", "pas dans ce programme", why));
        }
        cats.push_back(std::move(a));
        mine_->setCategories(std::move(cats));
    }

    // ---- Mes appareils ----
    // 1.38.0 : une licence en ligne - les PC que le service a donnes au dernier renouvellement (ce PC d'abord, et il
    // compte : << 3 PC sur 20 >>) ; une licence par fichier - les PC lies dans la licence.
    {
        namespace ls = licence::service;
        const auto online = st.online && !st.licenseId.empty() ? ls::onlineDevices(st.licenseId) : std::nullopt;
        if (online) {
            std::vector<std::vector<std::string>> rows;
            std::vector<int> tone;   // 0 neutre, 1 ce PC, 2 une place gardee
            for (const auto& d : online->list) {
                std::string state = d.state == "active" ? std::string("actif") : d.state == "liberee" ? "lib\xC3\xA9r\xC3\xA9 (garde sa place)" : d.state;
                if (online->type == "floating") state += d.token ? std::string(" \xC2\xB7 poste pris") : std::string(" \xC2\xB7 sans poste");
                rows.push_back({(d.name.empty() ? std::string("(sans nom)") : d.name) + (d.thisPc ? " \xE2\x80\x94 ce PC" : ""), ls::shortKey(d.key),
                                ls::dayText(d.activatedAt), ls::dateText(d.seenAt), ls::dayText(d.until), state});
                tone.push_back(d.thisPc ? 1 : d.state == "liberee" ? 2 : 0);
            }
            devices_->setColumns({{"PC", 240.f}, {"Cl\xC3\xA9", 150.f}, {"Activ\xC3\xA9 le", 110.f}, {"Vu le", 150.f}, {"Jusqu'au", 110.f}, {"\xC3\x89tat", 220.f}});
            devicesModel_ = std::make_shared<Rows>(std::vector<std::string>{"PC", "Cl\xC3\xA9", "Activ\xC3\xA9 le", "Vu le", "Jusqu'au", "\xC3\x89tat"}, std::move(rows),
                                                   [tone](ui::RowIndex r, std::size_t c) {
                                                       ui::CellStyle s;
                                                       if (c == 1) s.monospace = true;
                                                       if (r < tone.size() && tone[r] == 1) s.bold = true;
                                                       if (r < tone.size() && tone[r] == 2 && c == 5) s.fgTone = ui::Tone::Warning;
                                                       return s;
                                                   });
            devices_->setModel(devicesModel_);
            const bool floating = online->type == "floating";
            std::string sum = floating ? std::to_string(online->used) + " poste" + (online->used > 1 ? "s" : "") + " pris sur " + std::to_string(online->max) +
                                             " (licence flottante, " + std::to_string(online->total) + " PC activ\xC3\xA9" + (online->total > 1 ? "s" : "") + ")"
                                       : std::to_string(online->used) + " PC sur " + std::to_string(online->max) + " utilis\xC3\xA9" + (online->used > 1 ? "s" : "") +
                                             " \xE2\x80\x94 ce PC compris";
            sum += " \xC2\xB7 licence " + st.licenseId;
            std::string note = "Relev\xC3\xA9 au service le " + ls::dateText(online->receivedAt) +
                               " (\xC3\xA0 l'ouverture de cette rubrique, toutes les 10 minutes au plus), sans les PC lib\xC3\xA9r\xC3\xA9s ni retir\xC3\xA9s.";
            if (online->total > static_cast<int>(online->list.size()))
                note += " Les " + std::to_string(online->list.size()) + " plus r\xC3\xA9" "cents sont montr\xC3\xA9s.";
            devicesPanel_->setSummary(std::move(sum), std::move(note));
        } else {
            std::vector<std::vector<std::string>> rows;
            for (const auto& d : licensing::getLicensedDevices())
                rows.push_back({d.name.empty() ? std::string("(sans nom)") : d.name, d.fingerprint, d.activatedAt.size() >= 10 ? d.activatedAt.substr(0, 10) : d.activatedAt,
                                d.thisPc ? "ce PC" : ""});
            std::string sum, note;
            if (st.online) {   // une licence en ligne pas encore renouvelee par cette version : ce PC, au moins
                ls::Record rec;
                std::string w;
                (void)ls::loadRecord(rec, &w);
                rows.insert(rows.begin(), {(rec.activatedPcName.empty() ? std::string("(sans nom)") : rec.activatedPcName) + " \xE2\x80\x94 ce PC",
                                           st.deviceFingerprint, ls::dayText(rec.activatedAt), "ce PC"});
                sum = "Ce PC \xC2\xB7 licence " + st.licenseId + " (" + std::to_string(st.maxDevices) + " PC au plus)";
                note = "La liste des autres PC de la licence arrive du service des licences d\xC3\xA8s qu'il r\xC3\xA9pond (une connexion \xC3\xA0 Internet).";
            } else if (!rows.empty() && st.maxDevices > 0)
                sum = std::to_string(rows.size()) + " PC sur " + std::to_string(st.maxDevices) + " li\xC3\xA9" + (rows.size() > 1 ? "s" : "") + " \xC3\xA0 la licence";
            devices_->setColumns({{"PC", 260.f}, {"Empreinte de sa cl\xC3\xA9", 260.f}, {"Activ\xC3\xA9 le", 150.f}, {"", 90.f}});
            devicesModel_ = std::make_shared<Rows>(std::vector<std::string>{"PC", "Empreinte de sa cl\xC3\xA9", "Activ\xC3\xA9 le", ""}, std::move(rows));
            devices_->setModel(devicesModel_);
            devicesPanel_->setSummary(std::move(sum), std::move(note));
        }
    }

    // ---- Fonctionnalites ----
    {
        std::vector<std::vector<std::string>> rows;
        std::vector<bool> open;
        std::vector<FeatureInfo> infos;   // 1.38.0 : l'apercu de chaque ligne
        if (reg)
            for (const auto& f : reg->features) {
                if (f.retired) continue;
                const bool on = licence::allows(f.id);
                {
                    std::string in;
                    for (const auto& e : reg->editions)
                        if (std::find(e.features.begin(), e.features.end(), f.id) != e.features.end()) in += (in.empty() ? "" : ", ") + (e.label.empty() ? e.id : e.label);
                    // 1.46.0 : barree (pas dans ta licence) - sa capture se montre quand meme, en grand aussi
                    infos.push_back({f.id, f.label.empty() ? f.id : f.label, f.product, in, !on, on ? std::string() : std::string("pas dans ta licence")});
                }
                std::string limits;
                for (const auto& [lid, llabel] : f.limits)
                    if (const auto v = licensing::getFeatureLimit(f.id, lid))
                        limits += (limits.empty() ? "" : ", ") + (llabel.empty() ? lid : llabel) + " : " + std::to_string(*v);
                std::string where;
                for (const auto& g : licence::gates())
                    if (f.id == g.feature) where += (where.empty() ? "" : " ; ") + std::string(g.label);
                rows.push_back({f.label.empty() ? f.id : f.label, f.id, f.product,
                                on ? std::string("ouverte") : std::string("pas dans ta licence"), limits,
                                where.empty() ? std::string("(pas encore reli\xC3\xA9" "e \xC3\xA0 une entr\xC3\xA9" "e)") : where});
                open.push_back(on);
            }
        featuresModel_ = std::make_shared<Rows>(
            std::vector<std::string>{"Fonctionnalit\xC3\xA9", "Identifiant", "Logiciel", "\xC3\x89tat", "Limites", "Dans le programme"}, std::move(rows),
            [open](ui::RowIndex r, std::size_t c) {
                ui::CellStyle s;
                if (c == 1) s.monospace = true;
                if (c == 0 && r < open.size() && !open[r]) s.locked = true;   // 1.44.0 : pas dans ta licence - barree
                if (c == 3 && r < open.size()) {
                    s.icon = open[r] ? ui::Icon::Ok : ui::Icon::Lock;
                    s.iconTone = open[r] ? ui::Tone::Ok : ui::Tone::Warning;
                    s.fgTone = open[r] ? ui::Tone::Ok : ui::Tone::Warning;
                }
                return s;
            });
        features_->setModel(featuresModel_);
        featuresPanel_->setRows(std::move(infos));
    }

    // ---- Editions (ce qu'ouvre chacune) ----
    {
        std::vector<std::string> headers{"Fonctionnalit\xC3\xA9"};
        std::vector<ui::TableView::Column> cols{{"Fonctionnalit\xC3\xA9", 200.f}};
        std::vector<std::vector<std::string>> rows;
        std::vector<EditionsPanel::RowInfo> infos;   // 1.37.0 : la fonctionnalite de chaque ligne (l'apercu)
        std::size_t mineCol = 0, summary = 0;
        if (reg) {
            // 1.42.0 : les offres EN VENTE (une offre temporaire hors de sa periode, une offre retiree : plus montrees), et la tienne
            std::vector<licensing::RegistryEditionView> eds;
            const std::int64_t now = static_cast<std::int64_t>(std::time(nullptr));
            for (const auto& e : reg->editions) {   // 1.46.0 : celles du registre (avant : la liste vide elle-meme - aucune colonne)
                const bool isMine = !st.licensedEdition.empty() ? e.id == st.licensedEdition : e.id == st.edition;
                if (isMine || e.id == reg->unlicensedEdition || e.onSale(now)) eds.push_back(e);
            }
            for (const auto& e : eds) {
                const bool isMine = !st.licensedEdition.empty() ? e.id == st.licensedEdition : e.id == st.edition;
                std::string h = (e.label.empty() ? e.id : e.label) + (isMine ? " (la tienne)" : "");
                headers.push_back(h);
                cols.push_back({h, 150.f});
                if (isMine) mineCol = headers.size() - 1;
            }
            {
                std::vector<std::string> line{"PC \xC2\xB7 dur\xC3\xA9" "e"};
                for (const auto& e : eds)
                    line.push_back(std::to_string(e.maxDevices) + " PC \xC2\xB7 " + (e.durationDays > 0 ? std::to_string(e.durationDays) + " j" : std::string("sans fin")));
                rows.push_back(std::move(line));
                infos.push_back({});
            }
            // 1.37.0 : le prix (la boutique), si l'editeur en a donne un.
            if (std::any_of(eds.begin(), eds.end(), [](const auto& e) { return !e.price.empty() || !promoOf(e).empty(); })) {
                std::vector<std::string> line{"Prix"};
                for (const auto& e : eds) line.push_back(priceLine(e).empty() ? std::string("\xE2\x80\x94") : priceLine(e));   // 1.38.0 : la reduction
                rows.push_back(std::move(line));
                infos.push_back({});
            }
            summary = rows.size();
            for (const auto& f : reg->features) {
                if (f.retired) continue;
                std::vector<std::string> line{f.label.empty() ? f.id : f.label};
                std::string in;
                for (const auto& e : eds) {
                    const bool has = std::find(e.features.begin(), e.features.end(), f.id) != e.features.end();
                    line.push_back(has ? "\xE2\x9C\x93" : "\xE2\x80\x94");
                    if (has) in += (in.empty() ? "" : ", ") + (e.label.empty() ? e.id : e.label);
                }
                rows.push_back(std::move(line));
                const bool on = licence::allows(f.id);
                infos.push_back({f.id, f.label.empty() ? f.id : f.label, f.product, in, !on, on ? std::string() : std::string("pas dans ta licence")});
            }
        }
        std::vector<bool> lockedRows;   // 1.46.0 : pas dans ta licence - barree, comme dans Fonctionnalites
        for (const auto& i : infos) lockedRows.push_back(i.locked);
        editions_->setColumns(cols);
        editionsModel_ = std::make_shared<Rows>(std::move(headers), std::move(rows), [mineCol, summary, lockedRows](ui::RowIndex r, std::size_t c) {
            ui::CellStyle s;
            if (r < summary) s.bold = true;
            if (c > 0 && c == mineCol) s.bold = true;
            if (c == 0 && r < lockedRows.size() && lockedRows[r]) s.locked = true;
            return s;
        });
        editions_->setModel(editionsModel_);
        editionsPanel_->setRows(std::move(infos));
    }

    // ---- Historique ----
    {
        std::vector<std::vector<std::string>> rows;
        // 1.34.0 : les genres en francais ; 1.35.0 : la Decouverte, le service.
        const auto kindLabel = [](const std::string& k) -> std::string {
            if (k == "etat") return "\xC3\xA9tat";
            if (k == "evaluation") return "D\xC3\xA9" "couverte";
            if (k == "passerelle") return "demande (1.34)";
            return k;
        };
        for (const auto& h : licensing::getHistory(200))
            rows.push_back({whenOf(h.at), kindLabel(h.kind), h.ok ? "fait" : "refus\xC3\xA9", h.text});
        historyModel_ = std::make_shared<Rows>(std::vector<std::string>{"Quand (UTC)", "Quoi", "R\xC3\xA9sultat", "D\xC3\xA9tail"}, std::move(rows));
        history_->setModel(historyModel_);
    }

    // ---- Parametres ----
    {
        std::vector<PG::Category> cats;
        PG::Category c;
        c.name = "Ce PC";
        c.properties.push_back(prop("Dossier des licences", licensing::getLicenseFolder(),
                                    "La cl\xC3\xA9 de ce PC (prot\xC3\xA9g\xC3\xA9" "e par Windows), la licence, le registre, la liste de r\xC3\xA9vocation, "
                                    "l'historique. Copi\xC3\xA9 sur un autre PC, il ne sert \xC3\xA0 rien."));
        const std::string fp = licensing::thisDeviceFingerprint();
        c.properties.push_back(prop("Empreinte de ce PC", fp.empty() ? std::string("pas encore cr\xC3\xA9\xC3\xA9" "e (elle l'est \xC3\xA0 la premi\xC3\xA8re demande)") : fp,
                                    "L'empreinte de la cl\xC3\xA9 publique de ce PC : jamais son nom r\xC3\xA9seau, son adresse ou son num\xC3\xA9ro de s\xC3\xA9rie."));
        cats.push_back(std::move(c));
        PG::Category d;
        d.name = "Les documents sign\xC3\xA9s";
        d.properties.push_back(prop("Registre", reg ? "n\xC2\xB0 " + std::to_string(reg->number) + (reg->issuedAt.empty() ? std::string{} : " du " + reg->issuedAt.substr(0, 10))
                                                    : std::string("aucun"),
                                    "Les \xC3\xA9" "ditions et les fonctionnalit\xC3\xA9s, sign\xC3\xA9s par l'\xC3\xA9" "diteur. Un registre plus ancien que le "
                                    "dernier vu est refus\xC3\xA9."));
        d.properties.push_back(prop("Sans licence", reg && !reg->unlicensedEdition.empty() ? "l'\xC3\xA9" "dition " + reg->unlicensedEdition : std::string("-")));
        d.properties.push_back(prop("R\xC3\xA9vocations", st.revocationNumber ? "liste n\xC2\xB0 " + std::to_string(st.revocationNumber) : std::string("aucune")));
        d.properties.push_back(prop("Cl\xC3\xA9 de l'\xC3\xA9" "diteur", configured ? std::string("pr\xC3\xA9sente dans ce programme") : std::string("absente : licences non activ\xC3\xA9" "es, tout est ouvert")));
        if (!st.registryProblem.empty()) d.properties.push_back(prop("Registre : probl\xC3\xA8me", st.registryProblem));
        cats.push_back(std::move(d));
        PG::Category p;
        p.name = "Ce programme";
        p.properties.push_back(prop("Programme", core::productName() + " " + XPG_ANALYZER_VERSION));
        p.properties.push_back(prop("V\xC3\xA9rification", "hors ligne, \xC3\xA0 chaque lancement puis une fois par heure",
                                    "L'heure de ce PC est contr\xC3\xB4l\xC3\xA9" "e : un retour en arri\xC3\xA8re de plus de 24 h est d\xC3\xA9tect\xC3\xA9."));
        cats.push_back(std::move(p));
        params_->setCategories(std::move(cats));
    }
    online_->refresh();       // 1.34.0
    activation_->refresh();   // 1.35.0
    options_->refresh();      // 1.42.0
    purchases_->refresh();
    refreshHome();            // 1.46.0 : ce que dit deja chaque rubrique
    import_->setEnabled(configured);
    import_->setTooltip(configured ? std::string("Le .xpglic re\xC3\xA7u de l'\xC3\xA9" "diteur")
                                   : std::string("Ce programme ne peut pas encore v\xC3\xA9rifier une licence : il n'a pas la cl\xC3\xA9 de l'\xC3\xA9" "diteur"));
    invalidate();
}

void LicenceBody::askImport() {
    std::vector<FormDialog::Field> fields;
    fields.push_back({"Fichier", "", "C:\\Users\\moi\\Downloads\\XPG-2026-00042.xpglic", false, {}});
    menus_.ShowDialog(
        FormDialog::withBrowse(std::make_unique<FormDialog>("dialog.licenceImport", "Activer par fichier de licence",
                                                            "Le fichier .xpglic re\xC3\xA7u de l'\xC3\xA9" "diteur (pi\xC3\xA8" "ce jointe, tel quel). La licence n'est "
                                                            "install\xC3\xA9" "e que si elle vaut pour ce PC, maintenant ; sinon rien ne change et la raison est dite.",
                                                            std::move(fields), "Activer"),
                               0, ui::openFile("Licences XPG (.xpglic)|*.xpglic|Tous les fichiers|*.*", {}, "Activer par fichier de licence")),
        [this](const menu::DialogResult& r) {
            if (!r.accepted()) return;
            const auto v = FormDialog::split(r.payload);
            if (!v.empty() && !v[0].empty()) doImport(v[0]);
        });
}

void LicenceBody::doImport(const std::string& path) {
    const auto report = licensing::importLicenseFile(path);
    licence::refresh();
    std::string text;
    for (const auto& l : report.lines) text += "\xE2\x80\xA2 " + l + "\n";
    if (!report.ok) text += (text.empty() ? "" : "\n") + report.why;
    else text += (text.empty() ? "" : "\n") + report.state.message;
    say(report.ok ? (report.licenseInstalled ? "Licence install\xC3\xA9" "e : " + report.state.message : std::string("Import fait : ") + report.state.message)
                  : "Refus\xC3\xA9 : " + report.why,
        report.ok ? 1 : 3);
    reload();
    menus_.ShowDialog(std::make_unique<MessageDialog>(report.ok ? "Licence import\xC3\xA9" "e" : "Licence refus\xC3\xA9" "e", std::move(text),
                                                            report.ok ? MessageDialog::Icon::Info : MessageDialog::Icon::Warning),
                            nullptr);
}

void LicenceBody::askRequest() {
    const std::string sharedFolder = Settings::sharedFolder();   // une seule chaine (begin et end de la meme)
    const auto shared = std::filesystem::path(std::u8string(sharedFolder.begin(), sharedFolder.end())) / "Licences";
    const auto u8 = (shared / "demande-activation.xpgreq").u8string();
    std::vector<FormDialog::Field> fields;
    fields.push_back({"Nom de ce PC", "", "Poste bureau d'\xC3\xA9tudes (le nom que tu lui donnes ; facultatif)", false, {}});
    fields.push_back({"Licence demand\xC3\xA9" "e", state_.licenseId, "XPG-2026-00042 (facultatif)", false, {}});
    fields.push_back({"Enregistrer sous", std::string(u8.begin(), u8.end()), "", false, {}});
    menus_.ShowDialog(
        FormDialog::withBrowse(std::make_unique<FormDialog>("dialog.licenceRequest", "Demander une activation",
                                                            "La demande porte la cl\xC3\xA9 publique de ce PC (cr\xC3\xA9\xC3\xA9" "e ici la premi\xC3\xA8re fois, "
                                                            "prot\xC3\xA9g\xC3\xA9" "e par Windows), le nom que tu lui donnes et le logiciel ; elle est sign\xC3\xA9" "e par ce "
                                                            "PC. Rien de son mat\xC3\xA9riel. Envoie le fichier \xC3\xA0 l'\xC3\xA9" "diteur : il te renvoie ta licence.",
                                                            std::move(fields), "Cr\xC3\xA9" "er la demande"),
                               2, ui::saveFile("Demandes d'activation (.xpgreq)|*.xpgreq", std::string(u8.begin(), u8.end()), "Demander une activation")),
        [this](const menu::DialogResult& r) {
            if (!r.accepted()) return;
            const auto v = FormDialog::split(r.payload);
            if (v.size() < 3 || v[2].empty()) return;
            std::error_code ec;
            std::filesystem::create_directories(std::filesystem::path(std::u8string(v[2].begin(), v[2].end())).parent_path(), ec);
            std::string why;
            const std::string software = core::productName() + " " + XPG_ANALYZER_VERSION;
            if (licensing::writeActivationRequest(v[2], v[0], v[1], software, &why)) {
                say("Demande \xC3\xA9" "crite : " + v[2] + " - envoie-la \xC3\xA0 l'\xC3\xA9" "diteur", 1);
                reload();
            } else {
                say("La demande n'a pas pu \xC3\xAAtre \xC3\xA9" "crite : " + why, 3);
            }
        });
}

void LicenceBody::onLayout() {
    const auto r = bounds();
    // 1.37.0 : 1320 au plus (9 onglets avec Mon profil : leurs noms tiennent ; plus etroit, des icones).
    const float w = std::min(1320.f, std::max(640.f, r.w - 40.f));
    const float h = std::min(820.f, std::max(480.f, r.h - 40.f));
    panel_ = ui::dialogframe::place(*this, {std::floor(r.x + (r.w - w) * 0.5f), std::floor(r.y + (r.h - h) * 0.5f), w, h});
    closeBox_ = {panel_.right() - 36.f, panel_.y + 9.f, 24.f, 24.f};
    ui::dialogframe::keep(*this, closeBox_);
    const float x = panel_.x + 16.f, W = panel_.w - 32.f;
    // 1.46.0 : le fil d'Ariane sous le titre ; l'accueil : le resume (deux lignes de quatre cases) puis la phrase de l'etat ;
    // une rubrique : la phrase de l'etat (les gestes s'y disent) puis sa page, plus haute qu'un onglet d'avant.
    const float crumbsY = panel_.y + 50.f;
    crumbs_->setBounds({x, crumbsY, W, 30.f});
    if (current_ == licence::kTabHome) {
        headerY_ = crumbsY + 48.f;
        messageY_ = headerY_ + 2.f * 44.f + 4.f;
    } else {
        headerY_ = 0.f;
        messageY_ = crumbsY + 38.f;
    }
    pageTop_ = messageY_ + 36.f;
    const float bottom = panel_.bottom() - 52.f;
    const gfx::Rect page{x, pageTop_, W, std::max(60.f, bottom - pageTop_ - 6.f)};
    home_->setBounds(page);
    for (auto* p : pages_)
        if (p) p->setBounds(page);
    float bx = x;
    for (auto* b : {import_, request_, folder_}) {
        const float bw = std::ceil(std::max(110.f, b->sizeHint().preferred.w));   // 1.42.0 : des pixels entiers (des bords nets)
        b->setBounds({bx, panel_.bottom() - 44.f, bw, 32.f});
        bx += bw + 8.f;
    }
    const float cw = std::max(110.f, close_->sizeHint().preferred.w);
    close_->setBounds({panel_.right() - 16.f - cw, panel_.bottom() - 44.f, cw, 32.f});
}

void LicenceBody::onPaint(const ui::PaintContext& ctx) {
    auto& r = ctx.r;
    const auto& c = ctx.theme.color;
    r.fillRect(panel_, c.panelBg);
    r.strokeRect(panel_, c.borderStrong, 1.f);
    r.fillRect({panel_.x, panel_.y, panel_.w, 42.f}, c.headerBg);
    r.drawText({panel_.x + 16.f, panel_.y + (42.f - r.lineHeight(kTitle)) * 0.5f},
               fit(r, "Licence \xE2\x80\x94 " + core::productName(), kTitle, panel_.w - 70.f), kTitle, c.text);
    r.drawText({closeBox_.x + 7.f, closeBox_.y + 2.f}, "\xC3\x97", kTitle, c.textMuted);
    r.fillRect({panel_.x, panel_.y + 42.f, panel_.w, 1.f}, c.border);

    // L'en-tete : deux lignes de quatre cases.
    const auto& st = state_;
    const bool hasLicence = !st.licenseId.empty();
    const bool evaluation = st.status == licensing::LicenseStatus::Evaluation;   // 1.34.0 ; 1.35.0 : la Decouverte
    const std::string evalEnd = st.evaluationEndsAt ? licensing::isoDate(st.evaluationEndsAt) : std::string();
    const std::string edition = !st.licensedEdition.empty() ? st.licensedEdition : (st.edition.empty() ? std::string("\xE2\x80\x94") : st.edition);
    struct Cell { const char* label; std::string value; bool status; };
    const Cell cells[8] = {
        {"\xC3\x89" "dition", edition, false},
        {"Statut", licensing::statusLabel(st.status), true},
        {"Client", hasLicence && !st.customer.empty() ? st.customer : std::string("\xE2\x80\x94"), false},
        {"Licence", hasLicence ? st.licenseId
                               : evaluation ? "D\xC3\xA9" "couverte : " + std::to_string(std::max(0, st.evaluationDaysLeft)) + " j restant" +
                                                  (st.evaluationDaysLeft > 1 ? "s" : "")
                                            : std::string("\xE2\x80\x94"), evaluation && !hasLicence},
        {"PC autoris\xC3\xA9s", hasLicence ? std::to_string(st.maxDevices) : std::string("\xE2\x80\x94"), false},
        {st.online ? "Bail" : "PC li\xC3\xA9s",
         st.online ? (st.activationUntil ? "jusqu'au " + licence::dayOf(st.activationUntil) : std::string("fini"))
                   : hasLicence ? std::to_string(st.boundDevices) + " (" + std::to_string(std::max(0, st.maxDevices - st.boundDevices)) + " libres)"
                                : std::string("\xE2\x80\x94"), st.online},
        {"Expire le", hasLicence ? (st.expiresAt ? st.expirationDate() : std::string("jamais"))
                                 : (evaluation && !evalEnd.empty()) ? evalEnd + " (D\xC3\xA9" "couverte)" : std::string("\xE2\x80\x94"), false},
        {"Registre", st.registryNumber ? "n\xC2\xB0 " + std::to_string(st.registryNumber) : std::string("aucun"), false},
    };
    const float x = panel_.x + 16.f, W = panel_.w - 32.f, cw = W / 4.f;
    using S = licensing::LicenseStatus;
    const gfx::Color good = ctx.theme.tone(ui::Tone::Ok, c.text), warn = ctx.theme.tone(ui::Tone::Warning, c.text),
                     bad = ctx.theme.tone(ui::Tone::Error, c.text);
    const gfx::Color statusColor = st.status == S::Valid ? (st.online && st.activationWarning ? warn : good)
                                 : (st.status == S::Grace || st.status == S::Absent || st.status == S::Evaluation || st.status == S::NoFloatingSeat) ? warn
                                 : st.status == S::NotConfigured ? c.textMuted : bad;
    // 1.46.0 : le resume sur l'accueil seulement (une rubrique a son fil et la phrase de l'etat : plus de place pour elle)
    if (current_ == licence::kTabHome) {
        const gfx::Rect box{x, headerY_ - 8.f, W, 2.f * 44.f + 6.f};
        r.fillRoundedRect(box, c.windowBg, 8.f);
        ui::shapes::strokeRoundedRect(r, box, c.border, 8.f, 1.f);
        const float iw = (W - 28.f) / 4.f;
        for (int i = 0; i < 8; ++i) {
            const float cx = x + 14.f + iw * static_cast<float>(i % 4);
            const float cy = headerY_ + 44.f * static_cast<float>(i / 4);
            r.drawText({cx, cy}, cells[i].label, kSmall, c.textMuted);
            r.drawText({cx, cy + r.lineHeight(kSmall) + 1.f}, fit(r, cells[i].value, kValue, iw - 12.f), kValue, cells[i].status ? statusColor : c.text);
        }
    }
    (void)cw;
    // La phrase de l'etat (ou le dernier geste).
    const std::string line = flash_.empty() ? licence::stateText(st) : flash_;
    const int tone = flash_.empty() ? (st.status == S::Valid ? 1 : st.status == S::NotConfigured ? 0
                                       : (st.status == S::Grace || st.status == S::Absent || st.status == S::Evaluation || st.status == S::NoFloatingSeat) ? 2 : 3)
                                    : flashTone_;
    const gfx::Color lineColor = tone == 1 ? good : tone == 2 ? warn : tone == 3 ? bad : c.text;
    const gfx::Rect band{x, messageY_, W, 28.f};
    r.fillRect(band, c.headerBg);
    r.fillRect({band.x, band.y, 3.f, band.h}, lineColor);
    r.drawText({band.x + 10.f, band.y + (band.h - r.lineHeight(kSmall)) * 0.5f}, fit(r, line, kSmall, band.w - 16.f), kSmall, c.text);
}

ui::EventResult LicenceBody::onEvent(const ui::InputEvent& ev) {
    if (const auto* d = std::get_if<ui::MouseDown>(&ev); d && closeBox_.contains(d->pos)) {
        owner_.fermer();
        return ui::EventResult::Consumed;
    }
    return ui::EventResult::Ignored;
}

// ============================================================ 1.42.0 : Options et Mes achats ==
namespace {

// << 1 semaine >>, << 1 mois >>, << Jusqu'a la fin de ta licence >> ; -1 : des dates.
std::string optionDurationText(int days) {
    if (days < 0) return "D'autres dates\xE2\x80\xA6";
    if (days == 0) return "Jusqu'\xC3\xA0 la fin de ta licence";
    if (days == 7) return "1 semaine";
    if (days == 14) return "2 semaines";
    if (days == 30) return "1 mois";
    if (days == 365) return "1 an";
    return std::to_string(days) + " jours";
}
// << la semaine >>, << le mois >>, << l'an >> (apres un prix).
std::string optionPer(int days) {
    if (days == 0) return "jusqu'\xC3\xA0 la fin";
    if (days == 7) return "la semaine";
    if (days == 30) return "le mois";
    if (days == 365) return "l'an";
    return std::to_string(days) + " j";
}
// Un prix lisible (<< 490 EUR HT >>, << 1 028 EUR >>) : son montant ; << offerte >> : 0 ; illisible : rien.
std::optional<double> priceAmount(const std::string& p) {
    if (p == "offerte" || p == "offert") return 0.0;
    double v = 0;
    bool any = false, frac = false;
    double scale = 0.1;
    for (std::size_t i = 0; i < p.size(); ++i) {
        const unsigned char c = static_cast<unsigned char>(p[i]);
        if (c >= '0' && c <= '9') {
            any = true;
            if (frac) {
                v += (c - '0') * scale;
                scale *= 0.1;
            } else {
                v = v * 10 + (c - '0');
            }
        } else if (c == ' ' && any && !frac) {
            continue;
        } else if (c == 0xC2 && i + 1 < p.size() && (static_cast<unsigned char>(p[i + 1]) == 0xA0)) {   // une espace insecable
            ++i;
        } else if (c == 0xE2 && i + 2 < p.size() && static_cast<unsigned char>(p[i + 1]) == 0x80 && static_cast<unsigned char>(p[i + 2]) == 0xAF) {
            i += 2;   // l'espace fine insecable
        } else if ((c == ',' || c == '.') && any && !frac) {
            frac = true;
        } else if (any) {
            break;
        }
    }
    if (!any) return std::nullopt;
    return v;
}
std::string euros(double v) {
    const long long n = static_cast<long long>(std::llround(v));
    std::string d = std::to_string(n), out;
    for (std::size_t i = 0; i < d.size(); ++i) {
        if (i && (d.size() - i) % 3 == 0) out += "\xE2\x80\xAF";
        out += d[i];
    }
    return out + " \xE2\x82\xAC";
}
std::string featureNameOf(const licensing::RegistryView& reg, const std::string& id) {
    for (const auto& f : reg.features)
        if (f.id == id) return f.label.empty() ? f.id : f.label;
    return id;
}
// Le dernier jour d'une periode qui finit (exclue) a `until` : << dim. 15/11/2026 >>.
std::string lastDayOf(std::int64_t until) { return until ? ui::DatePicker::frenchDay(until - 1) : std::string(); }
int daysLeft(std::int64_t now, std::int64_t until) { return until > now ? static_cast<int>((until - now + 86399) / 86400) : 0; }

} // namespace

// ------------------------------------------------------------------ Options
OptionsPanel::OptionsPanel(LicenceBody& body, menu::MenuManager& menus) : ui::Widget("licence.options"), body_(body), menus_(menus) {
    table_ = &static_cast<ui::TableView&>(addChild(std::make_unique<ui::TableView>("licence.options.catalogue")));
    table_->setColumns({{"Option", 170.f}, {"Ce qu'elle ouvre", 180.f}, {"Prix (HT)", 330.f}, {"", 120.f}});
    table_->setSelectionMode(ui::SelectionMode::Single);
    duration_ = &static_cast<ui::DropDown&>(addChild(std::make_unique<ui::DropDown>("licence.options.duree")));
    start_ = &static_cast<ui::DropDown&>(addChild(std::make_unique<ui::DropDown>("licence.options.debut")));
    start_->setItems({{"D\xC3\xA8s qu'elle est accept\xC3\xA9" "e", "maintenant"}, {"Le\xE2\x80\xA6", "date"}});
    start_->setSelectedIndex(0);
    from_ = &static_cast<ui::DatePicker&>(addChild(std::make_unique<ui::DatePicker>("licence.options.debut-date")));
    from_->setShortcuts({{"Demain", 1, 0, false}, {"Dans 1 semaine", 7, 0, false}});
    to_ = &static_cast<ui::DatePicker&>(addChild(std::make_unique<ui::DatePicker>("licence.options.fin")));
    to_->setShortcuts({{"1 semaine", 7, 0, false}, {"2 semaines", 14, 0, false}, {"1 mois", 0, 1, false}});
    message_ = &static_cast<ui::InputText&>(addChild(std::make_unique<ui::InputText>("licence.options.mot")));
    message_->setPlaceholder("Pour la mise en service de la station (facultatif)");
    message_->setMaxLength(licence::service::kMaxMessage);
    send_ = &static_cast<ui::Button&>(addChild(std::make_unique<ui::Button>("Envoyer la demande", "licence.options.envoyer")));
    send_->setStyle(ui::Button::Style::Primary);
    extend_ = &static_cast<ui::Button&>(addChild(std::make_unique<ui::Button>("Prolonger\xE2\x80\xA6", "licence.options.prolonger")));
    links_ += table_->selectionChanged->connect([this](const std::vector<ui::RowIndex>& r) {
        if (loading_ || r.empty() || r.front() >= rows_.size()) return;
        chosen_ = rows_[r.front()].id;
        loadForm();
    });
    links_ += duration_->selectionChanged->connect([this](int) {
        if (loading_) return;
        if (days() < 0 && !to_->date()) {
            const std::int64_t start = from_->date() ? from_->date() : ui::DatePicker::dayOf(now());
            to_->setRangeStart(start);
            to_->setDate(start + 6 * 86400);
        }
        invalidateLayout();
        invalidate();
    });
    links_ += start_->selectionChanged->connect([this](int i) {
        if (loading_) return;
        if (i == 1 && !from_->date()) from_->setDate(ui::DatePicker::dayOf(now()) + 86400);
        invalidateLayout();
        invalidate();
    });
    links_ += from_->changed->connect([this](std::int64_t d) {
        to_->setRangeStart(d);
        invalidate();
    });
    links_ += to_->changed->connect([this](std::int64_t) { invalidate(); });
    links_ += send_->clicked->connect([this] { send(); });
    links_ += extend_->clicked->connect([this] {
        if (!activeId_.empty()) choose(activeId_);
    });
    chosen_ = licence::takeWantedOption();   // une action fermee qui l'a proposee
}

std::int64_t OptionsPanel::now() const {
    const auto st = licensing::getLicenseState();
    return st.trustedNow ? static_cast<std::int64_t>(st.trustedNow) : static_cast<std::int64_t>(std::time(nullptr));
}

int OptionsPanel::days() const {
    const auto* it = duration_->selectedItem();
    if (!it || it->value == "dates") return -1;
    return std::atoi(it->value.c_str());
}

std::string OptionsPanel::priceOf(int d) const {
    const auto reg = licensing::getRegistry();
    const auto* o = reg ? reg->option(chosen_) : nullptr;
    if (!o) return {};
    for (const auto& x : o->durations)
        if (x.first == d) return x.second;
    return {};
}

void OptionsPanel::choose(const std::string& optionId) {
    chosen_ = optionId;
    refresh();
}

void OptionsPanel::refresh() {
    namespace ls = licence::service;
    loading_ = true;
    const auto st = licensing::getLicenseState();
    const auto reg = licensing::getRegistry();
    const std::int64_t t = now();
    edition_ = st.licensedEdition;
    licenseId_ = st.licenseId;
    customer_ = st.customer;
    rows_.clear();
    activeLines_.clear();
    activeId_.clear();
    std::vector<std::vector<std::string>> cells;
    std::vector<int> tones;
    if (reg) {
        const licensing::RegistryEditionView* ed = nullptr;
        for (const auto& e : reg->editions)
            if (e.id == edition_) ed = &e;
        for (const auto& o : reg->options) {
            bool active = false;
            std::int64_t until = 0;
            for (const auto& lo : st.options)
                if (lo.id == o.id && lo.activeAt(st.trustedNow ? st.trustedNow : t, st.expiresAt)) {
                    active = true;
                    until = static_cast<std::int64_t>(lo.until ? lo.until : st.expiresAt);
                }
            if (!active && (!o.shop || !o.offeredTo(edition_))) continue;
            bool inOffer = ed && !o.features.empty();
            if (inOffer)
                for (const auto& f : o.features)
                    if (std::find(ed->features.begin(), ed->features.end(), f) == ed->features.end()) inOffer = false;
            Row row;
            row.id = o.id;
            row.active = active;
            row.inOffer = inOffer;
            rows_.push_back(row);
            std::string opens;
            for (const auto& f : o.features) opens += (opens.empty() ? "" : ", ") + featureNameOf(*reg, f);
            std::string prices;
            for (const auto& d : o.durations)
                prices += (prices.empty() ? "" : " \xC2\xB7 ") + (d.second.empty() ? std::string("sur demande") : d.second) + " " + optionPer(d.first);
            std::string state;
            int tone = 0;
            if (active) {
                state = until ? "active \xC2\xB7 encore " + std::to_string(daysLeft(t, until)) + " j" : std::string("active");
                tone = 1;
            } else if (inOffer) {
                state = "dans ton offre";
                tone = 3;
            }
            cells.push_back({o.label.empty() ? o.id : o.label, opens, prices, state});
            tones.push_back(tone);
            if (active) {
                if (activeId_.empty()) activeId_ = o.id;
                std::string line = (o.label.empty() ? o.id : o.label) + " \xE2\x80\x94 active";
                if (until) line += ", encore " + std::to_string(daysLeft(t, until)) + " j \xE2\x80\x94 jusqu'au " + lastDayOf(until) + " au soir";
                else line += " jusqu'\xC3\xA0 la fin de ta licence";
                activeLines_.push_back(line);
            }
        }
    }
    model_ = std::make_shared<Rows>(std::vector<std::string>{"Option", "Ce qu'elle ouvre", "Prix (HT)", ""}, std::move(cells),
                                    [tones](ui::RowIndex r, std::size_t c) {
                                        ui::CellStyle s;
                                        if (c == 0) s.bold = true;
                                        if (c == 3 && r < tones.size()) {
                                            s.fgTone = tones[r] == 1 ? ui::Tone::Ok : tones[r] == 3 ? ui::Tone::Info : ui::Tone::None;
                                            s.bold = tones[r] == 1;
                                        }
                                        return s;
                                    });
    table_->setModel(model_);
    // l'option choisie : celle demandee, sinon la premiere qui n'est pas deja dans l'offre
    if (std::none_of(rows_.begin(), rows_.end(), [&](const Row& r) { return r.id == chosen_; })) {
        chosen_.clear();
        for (const auto& r : rows_)
            if (!r.inOffer) {
                chosen_ = r.id;
                break;
            }
    }
    for (std::size_t i = 0; i < rows_.size(); ++i)
        if (rows_[i].id == chosen_) table_->selectModelRows({static_cast<ui::RowIndex>(i)}, false);
    // peut-elle partir ?
    blocked_.clear();
    pendingText_.clear();
    using S = licensing::LicenseStatus;
    std::string why;
    ls::Record rec;
    std::string recProblem;
    if (!ls::loadRecord(rec, &recProblem)) rec = ls::Record{};
    if (!licensing::isConfigured()) blocked_ = "Ce programme n'a pas de licence \xC3\xA0 g\xC3\xA9rer : tout y est ouvert.";
    else if (licenseId_.empty() || (st.status != S::Valid && st.status != S::Grace))
        blocked_ = "Une option s'ajoute \xC3\xA0 une licence en cours : active d'abord ta licence (Licence \xE2\x80\xBA Activation ou Demander ma licence).";
    else if (rows_.empty())
        blocked_ = "L'\xC3\xA9" "diteur ne propose pas encore d'option pour ton offre" + (edition_.empty() ? std::string(".") : " " + edition_ + ".");
    else if (!ls::available(&why))
        blocked_ = "Le service des licences n'est pas joignable d'ici" + (why.empty() ? std::string() : " (" + why + ")") +
                   " : demande l'option \xC3\xA0 l'\xC3\xA9" "diteur directement.";
    else if (ls::pending(rec)) {
        pendingText_ = "Ta demande n\xC2\xB0 " + std::to_string(rec.number) + (rec.option.empty() ? std::string() : " (option " + (rec.optionText.empty() ? rec.option : rec.optionText) + ")") +
                       " attend la r\xC3\xA9ponse de l'\xC3\xA9" "diteur : une seule demande \xC3\xA0 la fois (Demander ma licence \xE2\x80\xBA Annuler la demande).";
        blocked_ = pendingText_;
    }
    loading_ = false;
    loadForm();
}

void OptionsPanel::loadForm() {
    loading_ = true;
    const auto reg = licensing::getRegistry();
    const auto* o = reg ? reg->option(chosen_) : nullptr;
    std::vector<ui::DropDown::Item> items;
    if (o)
        for (const auto& d : o->durations) {
            ui::DropDown::Item it{optionDurationText(d.first), std::to_string(d.first)};
            it.note = d.second.empty() ? std::string("sur demande") : d.second;
            items.push_back(std::move(it));
        }
    items.push_back({optionDurationText(-1), "dates"});
    duration_->setItems(items);
    duration_->setSelectedIndex(0);
    const std::int64_t today = ui::DatePicker::dayOf(now());
    from_->setToday(today);
    to_->setToday(today);
    from_->setMinDate(today);
    to_->setMinDate(today);
    const bool canSend = blocked_.empty() && o != nullptr;
    for (ui::Widget* w : std::initializer_list<ui::Widget*>{duration_, start_, from_, to_, message_}) w->setEnabled(canSend);
    send_->setEnabled(canSend && licence::service::busyJob() == licence::service::Job::None);
    send_->setText(licence::service::busyJob() == licence::service::Job::Request ? std::string("Envoi en cours\xE2\x80\xA6") : std::string("Envoyer la demande"));
    extend_->setVisibility(activeId_.empty() ? ui::Visibility::Collapsed : ui::Visibility::Visible);
    loading_ = false;
    invalidateLayout();
    invalidate();
}

void OptionsPanel::send() {
    namespace ls = licence::service;
    if (!blocked_.empty()) return body_.say(blocked_, 2);
    const auto reg = licensing::getRegistry();
    const auto* o = reg ? reg->option(chosen_) : nullptr;
    if (!o) return body_.say("Choisis d'abord une option dans la liste.", 2);
    const std::int64_t today = ui::DatePicker::dayOf(now());
    ls::RequestForm f;
    f.customer = customer_;
    if (f.customer.empty()) {
        const auto p = licence::loadProfile();
        f.customer = !p.company.empty() ? p.company : p.name;
    }
    if (f.customer.empty()) return body_.say("Ta licence ne porte pas de nom de client : indique ta soci\xC3\xA9t\xC3\xA9 dans Mon profil, puis recommence.", 2);
    f.email = licence::loadProfile().email;
    f.licenseId = licenseId_;
    f.option = o->id;
    f.optionDays = days();
    const bool atDate = start_->selectedIndex() == 1 || f.optionDays < 0;
    f.optionFrom = atDate && from_->date() > today ? from_->date() : 0;
    if (f.optionDays < 0) {
        f.optionLastDay = to_->date();
        const std::int64_t start = f.optionFrom ? f.optionFrom : today;
        if (!f.optionLastDay || f.optionLastDay < start) return body_.say("Choisis la fin de l'option (Au) : un jour apr\xC3\xA8s son d\xC3\xA9" "but.", 2);
    }
    std::string text = (o->label.empty() ? o->id : o->label) + ", " + optionDurationText(f.optionDays);
    if (f.optionDays < 0) text = (o->label.empty() ? o->id : o->label) + ", du " + licence::service::dayText(f.optionFrom ? f.optionFrom : today) + " au " +
                                 licence::service::dayText(f.optionLastDay);
    f.optionText = text;
    f.message = message_->text();
    if (!ls::startRequest(f)) return body_.say("Un travail du service des licences est d\xC3\xA9j\xC3\xA0 en cours : attends sa fin.", 2);
    body_.say("Envoi de ta demande d'option \xC3\xA0 l'\xC3\xA9" "diteur\xE2\x80\xA6", 0);
    loadForm();
}

void OptionsPanel::onLayout() {
    const auto b = bounds();
    const float x = b.x + 12.f, w = std::max(200.f, b.w - 24.f);
    float y = b.y + 10.f;
    y += 26.f;   // le titre
    leadY_ = y;
    y += 22.f;
    if (!activeLines_.empty()) {
        const float h = 18.f + 22.f * static_cast<float>(activeLines_.size());
        activeBox_ = {x, y, w, h};
        const float ew = std::max(110.f, extend_->sizeHint().preferred.w + 8.f);
        extend_->setBounds({x + w - ew - 10.f, y + (h - 30.f) * 0.5f, ew, 30.f});
        y += h + 10.f;
    } else {
        activeBox_ = {};
        extend_->setBounds({});
    }
    const float footH = 24.f;
    const float formW = std::clamp(w * 0.40f, 330.f, 440.f);
    const float listH = std::max(80.f, b.bottom() - 8.f - footH - y);
    table_->setBounds({x, y, std::max(160.f, w - formW - 12.f), listH});
    formBox_ = {x + w - formW, y, formW, listH};
    footY_ = y + listH + 6.f;
    // le formulaire
    const float fx = formBox_.x + 14.f, fw = formBox_.w - 28.f;
    float fy = formBox_.y + 62.f;
    duration_->setBounds({fx, fy + 18.f, fw, 30.f});
    fy += 56.f;
    const bool dates = days() < 0;
    const bool atDate = start_->selectedIndex() == 1;
    if (dates) {
        start_->setVisibility(ui::Visibility::Collapsed);
        from_->setVisibility(ui::Visibility::Visible);
        to_->setVisibility(ui::Visibility::Visible);
        const float hw = std::floor((fw - 10.f) * 0.5f);
        from_->setBounds({fx, fy + 18.f, hw, 30.f});
        to_->setBounds({fx + hw + 10.f, fy + 18.f, fw - hw - 10.f, 30.f});
    } else {
        start_->setVisibility(ui::Visibility::Visible);
        to_->setVisibility(ui::Visibility::Collapsed);
        from_->setVisibility(atDate ? ui::Visibility::Visible : ui::Visibility::Collapsed);
        const float sw = atDate ? std::floor((fw - 10.f) * 0.55f) : fw;
        start_->setBounds({fx, fy + 18.f, sw, 30.f});
        from_->setBounds({fx + sw + 10.f, fy + 18.f, fw - sw - 10.f, 30.f});
    }
    fy += 56.f;
    message_->setBounds({fx, fy + 18.f, fw, 30.f});
    const float bw = std::max(170.f, send_->sizeHint().preferred.w + 12.f);
    send_->setBounds({formBox_.right() - 14.f - bw, formBox_.bottom() - 14.f - 32.f, bw, 32.f});
}

void OptionsPanel::onPaint(const ui::PaintContext& ctx) {
    auto& r = ctx.r;
    const auto& c = ctx.theme.color;
    const auto b = bounds();
    const float x = b.x + 12.f, w = std::max(200.f, b.w - 24.f);
    r.drawText({x, b.y + 10.f}, "Options", gfx::semibold(kTitle), c.text);
    r.drawText({x, leadY_}, fit(r, "Ajoute une fonction \xC3\xA0 ta licence pour un temps, sans changer d'offre. Elle arrive toute seule, puis s'arr\xC3\xAAte \xC3\xA0 sa date.",
                                   kSmall, w),
               kSmall, c.textMuted);
    if (activeBox_.w > 0) {
        r.fillRoundedRect(activeBox_, ctx.theme.tone(ui::Tone::Ok, c.panelBg).withAlpha(30), 8.f);
        for (std::size_t i = 0; i < activeLines_.size(); ++i)
            r.drawText({activeBox_.x + 14.f, activeBox_.y + 9.f + 22.f * static_cast<float>(i)}, fit(r, activeLines_[i], kSmall, activeBox_.w - 160.f), kSmall,
                       ctx.theme.tone(ui::Tone::Ok, c.text));
    }
    // Ta demande
    r.fillRoundedRect(formBox_, c.windowBg, 8.f);
    ui::shapes::strokeRoundedRect(r, formBox_, c.border, 8.f, 1.f);
    const float fx = formBox_.x + 14.f, fw = formBox_.w - 28.f;
    r.drawText({fx, formBox_.y + 12.f}, "Ta demande", kSmall, c.textMuted);
    const auto reg = licensing::getRegistry();
    const auto* o = reg ? reg->option(chosen_) : nullptr;
    r.drawText({fx, formBox_.y + 32.f}, fit(r, o ? (o->label.empty() ? o->id : o->label) : std::string("Choisis une option"), kValue, fw), kValue, c.text);
    float fy = formBox_.y + 62.f;
    r.drawText({fx, fy}, "Pour combien de temps", kSmall, c.textMuted);
    fy += 56.f;
    r.drawText({fx, fy}, days() < 0 ? "Du \xE2\x80\xA6 au (compris)" : "\xC3\x80 partir de", kSmall, c.textMuted);
    fy += 56.f;
    r.drawText({fx, fy}, "Un mot \xC2\xB7 facultatif", kSmall, c.textMuted);
    fy += 56.f;
    // le total et ce qui se passe ensuite
    const std::string price = days() >= 0 ? priceOf(days()) : std::string();
    if (!price.empty()) {
        r.drawText({fx, fy}, "Total HT", kSmall, c.textMuted);
        const float pw = r.measure(price, kValue).width;
        r.drawText({fx + fw - pw, fy - 2.f}, price, kValue, c.text);
        fy += 26.f;
    }
    const std::string note = !blocked_.empty() ? blocked_
                                               : "Aucun paiement ici : l'\xC3\xA9" "diteur re\xC3\xA7oit ta demande et l'accepte. L'option arrive toute seule sur tes PC.";
    for (const auto& l : wrapText(r, note, kSmall, fw)) {
        if (fy > send_->bounds().y - 20.f) break;
        r.drawText({fx, fy}, l, kSmall, !blocked_.empty() ? ctx.theme.tone(ui::Tone::Warning, c.text) : c.textMuted);
        fy += 18.f;
    }
    r.drawText({x, footY_}, "Tes options pass\xC3\xA9" "es sont dans Mes achats.", kSmall, c.textMuted);
}

// ------------------------------------------------------------------ Mes achats
PurchasesPanel::PurchasesPanel(LicenceBody& body) : ui::Widget("licence.achats"), body_(body) {
    table_ = &static_cast<ui::TableView&>(addChild(std::make_unique<ui::TableView>("licence.achats.table")));
    table_->setColumns({{"Date", 110.f}, {"Achat", 330.f}, {"P\xC3\xA9riode", 220.f}, {"Prix HT", 110.f}, {"\xC3\x89tat", 150.f}});
    table_->setSelectionMode(ui::SelectionMode::Single);
    all_ = &static_cast<ui::Button&>(addChild(std::make_unique<ui::Button>("Tout", "licence.achats.tout")));
    offers_ = &static_cast<ui::Button&>(addChild(std::make_unique<ui::Button>("Offres", "licence.achats.offres")));
    options_ = &static_cast<ui::Button&>(addChild(std::make_unique<ui::Button>("Options", "licence.achats.options")));
    export_ = &static_cast<ui::Button&>(addChild(std::make_unique<ui::Button>("Exporter (CSV)", "licence.achats.exporter")));
    export_->setTooltip("Un fichier CSV (Excel) de ton historique, dans le dossier partag\xC3\xA9 (Licences).");
    links_ += all_->clicked->connect([this] {
        filter_ = 0;
        rebuild();
    });
    links_ += offers_->clicked->connect([this] {
        filter_ = 1;
        rebuild();
    });
    links_ += options_->clicked->connect([this] {
        filter_ = 2;
        rebuild();
    });
    links_ += export_->clicked->connect([this] { exportCsv(); });
}

std::int64_t PurchasesPanel::now() const {
    const auto st = licensing::getLicenseState();
    return st.trustedNow ? static_cast<std::int64_t>(st.trustedNow) : static_cast<std::int64_t>(std::time(nullptr));
}

void PurchasesPanel::refresh() {
    const auto st = licensing::getLicenseState();
    const auto reg = licensing::getRegistry();
    const std::int64_t t = now();
    all__ = st.purchases;
    // une licence d'avant la 1.42 (sans historique) : ce que dit la licence installee
    if (all__.empty() && !st.licenseId.empty()) {
        licensing::Purchase p;
        p.at = st.issuedAt;
        p.kind = "offer";
        p.label = st.editionLabel.empty() ? st.licensedEdition : st.editionLabel;
        p.licenseId = st.licenseId;
        p.from = st.issuedAt;
        p.until = st.expiresAt;
        if (reg)
            for (const auto& e : reg->editions)
                if (e.id == st.licensedEdition) p.price = e.price;
        all__.push_back(p);
        for (const auto& o : st.options) {
            licensing::Purchase q;
            q.at = o.from;
            q.kind = "option";
            q.label = o.label.empty() ? o.id : o.label;
            q.licenseId = st.licenseId;
            q.from = o.from;
            q.until = o.until ? o.until : st.expiresAt;
            q.price = o.gift ? (o.price.empty() ? std::string("offerte") : o.price) : o.price;
            all__.push_back(q);
        }
    }
    std::stable_sort(all__.begin(), all__.end(), [](const licensing::Purchase& a, const licensing::Purchase& b) { return a.at > b.at; });
    // les trois tuiles
    tile_[0][0] = "Ton offre";
    tile_[0][1] = st.licenseId.empty() ? std::string("Aucune licence") : (st.editionLabel.empty() ? st.licensedEdition : st.editionLabel);
    tile_[0][2] = st.licenseId.empty() ? std::string() : (st.expiresAt ? "jusqu'au " + licence::service::dayText(st.expiresAt) : std::string("sans fin")) + " \xC2\xB7 " +
                                                            std::to_string(std::max(1, st.maxDevices)) + " PC";
    tile_[1][0] = "Option en cours";
    tile_[1][1] = "Aucune";
    tile_[1][2] = "Ajoute-en une dans Options";
    for (const auto& o : st.options)
        if (o.activeAt(st.trustedNow ? st.trustedNow : t, st.expiresAt)) {
            const std::int64_t until = static_cast<std::int64_t>(o.until ? o.until : st.expiresAt);
            tile_[1][1] = o.label.empty() ? o.id : o.label;
            tile_[1][2] = until ? "encore " + std::to_string(daysLeft(t, until)) + " j \xC2\xB7 jusqu'au " + lastDayOf(until) + " au soir" : std::string("jusqu'\xC3\xA0 la fin de ta licence");
            break;
        }
    std::int64_t first = 0;
    double total = 0;
    bool priced = !all__.empty();
    for (const auto& p : all__) {
        if (!first || (p.at && p.at < first)) first = p.at;
        const auto v = priceAmount(p.price);
        if (v) total += *v;
        else priced = false;
    }
    tile_[2][0] = first ? "Depuis le " + licence::service::dayText(first) : std::string("Historique");
    tile_[2][1] = std::to_string(all__.size()) + (all__.size() > 1 ? " achats" : " achat");
    tile_[2][2] = priced ? euros(total) + " HT en tout" : std::string();
    rebuild();
}

void PurchasesPanel::rebuild() {
    shown_.clear();
    std::size_t nOffers = 0, nOptions = 0;
    for (const auto& p : all__) {
        const bool opt = p.kind == "option";
        (opt ? nOptions : nOffers) += 1;
        if (filter_ == 0 || (filter_ == 1 && !opt) || (filter_ == 2 && opt)) shown_.push_back(p);
    }
    all_->setText("Tout (" + std::to_string(all__.size()) + ")");
    offers_->setText("Offres (" + std::to_string(nOffers) + ")");
    options_->setText("Options (" + std::to_string(nOptions) + ")");
    all_->setStyle(filter_ == 0 ? ui::Button::Style::Primary : ui::Button::Style::Default);
    offers_->setStyle(filter_ == 1 ? ui::Button::Style::Primary : ui::Button::Style::Default);
    options_->setStyle(filter_ == 2 ? ui::Button::Style::Primary : ui::Button::Style::Default);
    const std::int64_t t = now();
    std::vector<std::vector<std::string>> cells;
    std::vector<int> tones;
    for (const auto& p : shown_) {
        std::string state;
        int tone = 0;
        if (p.from > t) {
            state = "\xC3\x80 venir";
            tone = 3;
        } else if (p.until && t >= p.until) {
            state = "Finie";
        } else {
            state = p.until ? "En cours \xC2\xB7 " + std::to_string(daysLeft(t, p.until)) + " j" : std::string("En cours");
            tone = 1;
        }
        std::string kind = p.kind == "option" ? " \xC2\xB7 option" : p.kind == "extension" ? " \xC2\xB7 prolongation" : std::string();
        const std::string period = licence::service::dayText(p.from) + " \xE2\x86\x92 " + (p.until ? licence::service::dayText(p.until - (p.kind == "option" ? 1 : 0)) : std::string("sans fin"));
        cells.push_back({licence::service::dayText(p.at), p.label + kind, period, p.price.empty() ? std::string("\xE2\x80\x94") : p.price, state});
        tones.push_back(tone);
    }
    model_ = std::make_shared<Rows>(std::vector<std::string>{"Date", "Achat", "P\xC3\xA9riode", "Prix HT", "\xC3\x89tat"}, std::move(cells),
                                    [tones](ui::RowIndex r, std::size_t c) {
                                        ui::CellStyle s;
                                        if (c == 4 && r < tones.size()) {
                                            s.fgTone = tones[r] == 1 ? ui::Tone::Ok : tones[r] == 3 ? ui::Tone::Info : ui::Tone::None;
                                            s.bold = tones[r] == 1;
                                        }
                                        return s;
                                    });
    table_->setModel(model_);
    export_->setEnabled(!all__.empty());
    invalidateLayout();
    invalidate();
}

std::string PurchasesPanel::csv() const {
    const auto cellOf = [](std::string v) {
        if (!v.empty() && (v[0] == '=' || v[0] == '+' || v[0] == '-' || v[0] == '@' || v[0] == '\t' || v[0] == '\r')) v = "'" + v;   // pas de formule
        std::string out = "\"";
        for (char ch : v) out += ch == '"' ? std::string("\"\"") : std::string(1, ch);
        return out + "\"";
    };
    std::string out = "\xEF\xBB\xBF" "Date;Achat;Type;Du;Au;Prix HT;Licence\r\n";
    for (const auto& p : all__) {
        const std::string kind = p.kind == "option" ? "option" : p.kind == "extension" ? "prolongation" : "offre";
        out += cellOf(licence::service::dayText(p.at)) + ";" + cellOf(p.label) + ";" + cellOf(kind) + ";" + cellOf(licence::service::dayText(p.from)) + ";" +
               cellOf(p.until ? licence::service::dayText(p.until - (p.kind == "option" ? 1 : 0)) : std::string("sans fin")) + ";" + cellOf(p.price) + ";" +
               cellOf(p.licenseId) + "\r\n";
    }
    return out;
}

void PurchasesPanel::exportCsv() {
    const std::string sharedFolder = Settings::sharedFolder();
    const auto dir = std::filesystem::path(std::u8string(sharedFolder.begin(), sharedFolder.end())) / "Licences";
    std::error_code ec;
    std::filesystem::create_directories(dir, ec);
    const auto file = dir / "mes-achats.csv";
    std::ofstream out(file, std::ios::binary | std::ios::trunc);
    const std::string text = csv();
    out.write(text.data(), static_cast<std::streamsize>(text.size()));
    out.close();
    const auto u8 = file.u8string();
    if (!out) return body_.say("Le fichier n'a pas pu \xC3\xAAtre \xC3\xA9" "crit : " + std::string(u8.begin(), u8.end()), 3);
    body_.say("Historique export\xC3\xA9 : " + std::string(u8.begin(), u8.end()) + " (Ouvrir le dossier des licences).", 1);
}

void PurchasesPanel::onLayout() {
    const auto b = bounds();
    const float x = b.x + 12.f, w = std::max(200.f, b.w - 24.f);
    float y = b.y + 10.f + 26.f;
    leadY_ = y;
    y += 24.f;
    const float tw = std::floor((w - 20.f) / 3.f);
    for (int i = 0; i < 3; ++i) tiles_[i] = {x + static_cast<float>(i) * (tw + 10.f), y, tw, 70.f};
    y += 80.f;
    timeline_ = {x, y, w, 44.f};
    y += 54.f;
    float bx = x;
    for (ui::Button* btn : {all_, offers_, options_}) {
        const float bw = btn->sizeHint().preferred.w + 10.f;
        btn->setBounds({bx, y, bw, 28.f});
        bx += bw + 6.f;
    }
    const float ew = export_->sizeHint().preferred.w + 12.f;
    export_->setBounds({x + w - ew, y, ew, 28.f});
    y += 36.f;
    table_->setBounds({x, y, w, std::max(60.f, b.bottom() - 8.f - y)});
}

void PurchasesPanel::onPaint(const ui::PaintContext& ctx) {
    auto& r = ctx.r;
    const auto& c = ctx.theme.color;
    const auto b = bounds();
    const float x = b.x + 12.f, w = std::max(200.f, b.w - 24.f);
    r.drawText({x, b.y + 10.f}, "Mes achats", gfx::semibold(kTitle), c.text);
    r.drawText({x, leadY_}, fit(r, "Tout ce que tu as pris depuis ta premi\xC3\xA8re licence : les offres, les prolongations, les options. M\xC3\xAAme ce qui est fini.", kSmall, w),
               kSmall, c.textMuted);
    for (int i = 0; i < 3; ++i) {
        const auto& t = tiles_[i];
        r.fillRoundedRect(t, c.windowBg, 8.f);
        ui::shapes::strokeRoundedRect(r, t, c.border, 8.f, 1.f);
        r.drawText({t.x + 12.f, t.y + 8.f}, fit(r, tile_[i][0], kSmall, t.w - 24.f), kSmall, c.textMuted);
        r.drawText({t.x + 12.f, t.y + 26.f}, fit(r, tile_[i][1], kValue, t.w - 24.f), kValue, i == 1 && tile_[i][1] != "Aucune" ? ctx.theme.tone(ui::Tone::Warning, c.text) : c.text);
        r.drawText({t.x + 12.f, t.y + 48.f}, fit(r, tile_[i][2], kSmall, t.w - 24.f), kSmall, c.textMuted);
    }
    // la ligne de temps : les offres (pleines), les options (au-dessus), aujourd'hui
    if (!all__.empty()) {
        std::int64_t lo = 0, hi = 0;
        const std::int64_t t = now();
        for (const auto& p : all__) {
            if (!lo || (p.from && p.from < lo)) lo = p.from;
            hi = std::max<std::int64_t>({hi, p.until, p.from});
        }
        hi = std::max(hi, t);
        if (hi <= lo) hi = lo + 86400;
        const auto& tl = timeline_;
        const float bx = tl.x + 4.f, bw = tl.w - 8.f;
        const auto px = [&](std::int64_t s) { return bx + bw * static_cast<float>(static_cast<double>(s - lo) / static_cast<double>(hi - lo)); };
        r.fillRoundedRect({bx, tl.y + 20.f, bw, 10.f}, c.border.withAlpha(128), 5.f);
        for (const auto& p : all__) {
            const float a = px(p.from), e = px(p.until ? p.until : hi);
            if (p.kind == "option") r.fillRoundedRect({a, tl.y + 8.f, std::max(4.f, e - a), 8.f}, ctx.theme.tone(ui::Tone::Warning, c.accent), 4.f);
            else r.fillRoundedRect({a, tl.y + 20.f, std::max(4.f, e - a), 10.f}, c.accent, 5.f);
        }
        const float tx = px(t);
        r.fillRect({tx - 1.f, tl.y + 2.f, 2.f, 34.f}, c.text);
        r.drawText({bx, tl.y + 33.f}, licence::service::dayText(lo), kSmall, c.textMuted);
        const std::string end = licence::service::dayText(hi);
        r.drawText({bx + bw - r.measure(end, kSmall).width, tl.y + 33.f}, end, kSmall, c.textMuted);
    }
}

// ============================================================ 1.35.0 : Activation ==
ActivationPanel::ActivationPanel(LicenceBody& body, menu::MenuManager& menus) : ui::Widget("licence.activation"), body_(body), menus_(menus) {
    auto code = std::make_unique<ui::InputText>("licence.code.champ");
    code->setPlaceholder("XPG-XXXX-XXXX-XXXX-XXXX");
    code->setMaxLength(48);
    code->setMonospace(true);
    code_ = &static_cast<ui::InputText&>(addChild(std::move(code)));
    auto pc = std::make_unique<ui::InputText>("licence.code.pc");
    pc->setPlaceholder("Poste bureau d'\xC3\xA9tudes (facultatif : le nom que tu donnes \xC3\xA0 ce PC)");
    pc->setMaxLength(licence::service::kMaxPcName);
    pc_ = &static_cast<ui::InputText&>(addChild(std::move(pc)));
    activate_ = button("licence.code.activer", "Activer", ui::Button::Style::Primary);
    activate_->setTooltip("Le service des licences v\xC3\xA9rifie le code et active la licence sur ce PC (le code ne va que l\xC3\xA0, il n'est \xC3\xA9" "crit nulle part)");
    renew_ = button("licence.renouveler", "Renouveler maintenant");
    renew_->setTooltip("Renouvelle le bail de ce PC tout de suite (il l'est aussi \xC3\xA0 chaque lancement, puis chaque heure quand il finit bient\xC3\xB4t)");
    release_ = button("licence.liberer", "Lib\xC3\xA9rer ce poste\xE2\x80\xA6", ui::Button::Style::Danger);
    release_->setTooltip("Rend le poste de ce PC : un autre PC peut prendre sa place ; les fonctions de la licence se ferment ici");
    leaseReturn_ = button("licence.flottante.rendre", "Rendre le poste");
    leaseReturn_->setTooltip("Rend le poste flottant tout de suite (un coll\xC3\xA8gue peut le prendre) ; il n'est plus repris tout seul");
    leaseTake_ = button("licence.flottante.prendre", "Prendre un poste", ui::Button::Style::Primary);
    trial_ = button("licence.decouverte", "D\xC3\xA9" "clarer maintenant");
    trial_->setTooltip("D\xC3\xA9" "clare la D\xC3\xA9" "couverte de cette machine au service des licences (elle l'est aussi \xC3\xA0 chaque lancement)");
    links_ += activate_->clicked->connect([this] { doActivate(); });
    links_ += renew_->clicked->connect([this] {
        if (!licence::service::startRenew()) body_.say("Un travail du service des licences est d\xC3\xA9j\xC3\xA0 en cours : attends sa fin.", 2);
        else body_.say("Renouvellement du bail\xE2\x80\xA6", 0);
        refresh();
    });
    links_ += release_->clicked->connect([this] { askRelease(); });
    links_ += leaseReturn_->clicked->connect([this] {
        if (!licence::service::startReturnLease()) body_.say("Un travail du service des licences est d\xC3\xA9j\xC3\xA0 en cours : attends sa fin.", 2);
        else body_.say("Retour du poste flottant\xE2\x80\xA6", 0);
        refresh();
    });
    links_ += leaseTake_->clicked->connect([this] {
        if (!licence::service::startTakeLease()) body_.say("Un travail du service des licences est d\xC3\xA9j\xC3\xA0 en cours : attends sa fin.", 2);
        else body_.say("Prise d'un poste flottant\xE2\x80\xA6", 0);
        refresh();
    });
    links_ += trial_->clicked->connect([this] {
        if (!licence::service::startTrial()) body_.say("Un travail du service des licences est d\xC3\xA9j\xC3\xA0 en cours : attends sa fin.", 2);
        else body_.say("D\xC3\xA9" "claration de la D\xC3\xA9" "couverte\xE2\x80\xA6", 0);
        refresh();
    });
    refresh();
}

ui::Button* ActivationPanel::button(const char* id, const std::string& text, ui::Button::Style style) {
    auto b = std::make_unique<ui::Button>(text, id);
    b->setStyle(style);
    return &static_cast<ui::Button&>(addChild(std::move(b)));
}

void ActivationPanel::add(Item::Kind kind, std::string label, std::string value, int tone, std::vector<ui::Widget*> widgets) {
    Item it;
    it.kind = kind;
    it.label = std::move(label);
    it.value = std::move(value);
    it.tone = tone;
    it.widgets = std::move(widgets);
    for (auto* w : it.widgets) w->setVisibility(ui::Visibility::Visible);
    items_.push_back(std::move(it));
}

void ActivationPanel::refresh() {
    namespace ls = licence::service;
    const auto st = licensing::getLicenseState();
    std::string why;
    const bool on = ls::available(&why);
    ls::Record rec;
    std::string recProblem;
    if (!ls::loadRecord(rec, &recProblem)) rec = ls::Record{};
    busy_ = ls::busyJob();
    const bool idle = busy_ == ls::Job::None;
    items_.clear();
    for (auto* w : std::initializer_list<ui::Widget*>{code_, pc_, activate_, renew_, release_, leaseReturn_, leaseTake_, trial_}) {
        w->setVisibility(ui::Visibility::Collapsed);
        w->setEnabled(idle && on);
    }
    activate_->setText(busy_ == ls::Job::Activate ? std::string("Activation en cours\xE2\x80\xA6") : std::string("Activer"));
    renew_->setText(busy_ == ls::Job::Renew || busy_ == ls::Job::Sync ? std::string("Renouvellement en cours\xE2\x80\xA6") : std::string("Renouveler maintenant"));

    // ---- activer avec un code ----
    add(Item::Heading, "Activer avec un code");
    if (!on) {
        add(Item::Text, {}, why + " Utilise les boutons du bas : Demander une activation\xE2\x80\xA6 (un fichier \xC3\xA0 envoyer \xC3\xA0 l'\xC3\xA9" "diteur), puis Activer par "
                               "fichier de licence\xE2\x80\xA6 avec sa r\xC3\xA9ponse.", 5);
    } else {
        add(Item::Text, {}, "Le code re\xC3\xA7u de l'\xC3\xA9" "diteur (XPG-XXXX-XXXX-XXXX-XXXX) : le service des licences le v\xC3\xA9rifie et active la licence sur ce "
                            "PC, tout de suite. Le code n'est \xC3\xA9" "crit nulle part sur ce PC.");
        add(Item::Field, "Code d'activation", {}, 0, {code_});
        add(Item::Field, "Nom de ce PC", {}, 0, {pc_});
        add(Item::Buttons, {}, {}, 0, {activate_});
        if (pc_->text().empty() && !rec.activatedPcName.empty()) pc_->setText(rec.activatedPcName);
    }

    // ---- la licence en ligne ----
    releaseId_ = st.online ? st.licenseId : rec.activatedLicense;
    if (st.online || !rec.activatedLicense.empty()) {
        add(Item::Heading, "Ta licence en ligne");
        const std::string edition = !st.licensedEdition.empty() ? st.licensedEdition : st.edition;
        add(Item::Pair, "Licence", releaseId_ + (edition.empty() ? std::string() : " \xC2\xB7 " + edition) +
                                       (rec.activatedPcName.empty() ? std::string() : " \xC2\xB7 PC \xC2\xAB " + rec.activatedPcName + " \xC2\xBB"));
        if (st.status == licensing::LicenseStatus::LeaseExpired)
            add(Item::Pair, "Bail", "expir\xC3\xA9 : connecte ce PC \xC3\xA0 Internet, il se renouvelle tout seul", 3);
        else if (st.activationUntil)
            add(Item::Pair, "Bail", "jusqu'au " + ls::dateText(st.activationUntil) + " (" + std::to_string(std::max(0, st.activationDaysLeft)) + " j)" +
                                        (st.activationWarning ? " \xE2\x80\x94 connecte ce PC \xC3\xA0 Internet avant cette date" : std::string()),
                st.activationWarning ? 2 : 1);
        const licensing::Seconds sync = st.lastServiceSync ? st.lastServiceSync : rec.lastSync;
        add(Item::Pair, "Derni\xC3\xA8re synchronisation", sync ? ls::dateText(sync) : std::string("jamais"), sync ? 0 : 2);
        add(Item::Text, {}, "Le bail se renouvelle tout seul \xC3\xA0 chaque connexion. Lib\xC3\xA9rer ce poste rend sa place \xC3\xA0 un autre PC : tout de suite "
                            "deux fois par mois (ou autant que de postes), sinon \xC3\xA0 la fin de son bail. Les fonctions de la licence se ferment ici.", 5);
        add(Item::Buttons, {}, {}, 0, {renew_, release_});
    }

    // ---- la licence flottante ----
    if (st.floating) {
        add(Item::Heading, "Licence flottante");
        const licensing::Seconds now = licensing::systemNow();
        if (st.leaseUntil > 0 && st.status != licensing::LicenseStatus::NoFloatingSeat)
            add(Item::Pair, "Poste", "pris jusqu'\xC3\xA0 " + ls::dateText(st.leaseUntil).substr(11) + " (renouvel\xC3\xA9 toutes les 5 minutes tant que le programme tourne)", 1);
        else if (ls::leaseHeldBack())
            add(Item::Pair, "Poste", "rendu : il n'est plus repris tout seul (Prendre un poste le reprend)", 2);
        else if (const licensing::Seconds at = ls::seatFreeAt(); at > now)
            add(Item::Pair, "Poste", "aucun poste libre \xE2\x80\x94 libre dans " + std::to_string((at - now + 59) / 60) + " min ; nouvel essai tout seul", 2);
        else
            add(Item::Pair, "Poste", "aucun poste pris pour l'instant ; nouvel essai tout seul", 2);
        add(Item::Buttons, {}, {}, 0, {st.leaseUntil > 0 && !ls::leaseHeldBack() ? leaseReturn_ : leaseTake_});
    }

    // ---- la Decouverte ----
    if (st.status == licensing::LicenseStatus::Evaluation || st.evaluationStartedAt) {
        add(Item::Heading, "D\xC3\xA9" "couverte");
        if (st.status == licensing::LicenseStatus::Evaluation) add(Item::Pair, "D\xC3\xA9" "couverte", licence::discoveryText(st), st.evaluationDaysLeft <= 7 ? 2 : 0);
        else if (st.evaluationOver) add(Item::Pair, "D\xC3\xA9" "couverte", "finie" + (st.evaluationEndsAt ? " le " + licence::dayOf(st.evaluationEndsAt) : std::string()), 5);
        if (rec.trialRecordedAt)
            add(Item::Pair, "Chez le service", "enregistr\xC3\xA9" "e par le service le " + ls::dateText(rec.trialRecordedAt) +
                                                    (rec.trialStart ? " (d\xC3\xA9" "but connu pour cette machine : " + licence::dayOf(rec.trialStart) + ")" : std::string()), 0);
        else
            add(Item::Pair, "Chez le service", on ? std::string("pas encore enregistr\xC3\xA9" "e : elle le sera \xC3\xA0 la prochaine connexion") : std::string("\xE2\x80\x94"), 5);
        add(Item::Text, {}, "Pour que la D\xC3\xA9" "couverte ne recommence pas en r\xC3\xA9installant Windows ou en effa\xC3\xA7" "ant ses fichiers, le service des licences garde "
                            "une empreinte de cette machine (tir\xC3\xA9" "e de l'identifiant de l'installation de Windows) : elle ne dit rien de ton mat\xC3\xA9riel, "
                            "mais elle reconna\xC3\xAEt la machine.", 5);
        if (on && st.status == licensing::LicenseStatus::Evaluation && !rec.trialRecordedAt) add(Item::Buttons, {}, {}, 0, {trial_});
    }
    if (!idle) add(Item::Text, {}, ls::jobText(busy_), 5);
    if (!recProblem.empty()) add(Item::Text, {}, recProblem, 3);
    invalidateLayout();
    invalidate();
}

void ActivationPanel::doActivate() {
    std::string code = code_->text();
    if (code.find_first_not_of(" \t") == std::string::npos) {
        body_.say("Tape d'abord le code re\xC3\xA7u de l'\xC3\xA9" "diteur (XPG-XXXX-XXXX-XXXX-XXXX).", 2);
        return;
    }
    std::string normalized;
    if (!licensing::service::normalizeCode(code, normalized)) {
        body_.say("Ce n'est pas un code d'activation : il a la forme XPG-XXXX-XXXX-XXXX-XXXX (16 lettres et chiffres).", 2);
        return;
    }
    deploy::crypto::wipe(normalized.data(), normalized.size());
    // Le reseau part sur le fil du service ; la fenetre relevera le resultat (LicenceBody::poll). Le champ est vide :
    // le code ne reste pas a l'ecran.
    if (!licence::service::startActivate(code, pc_->text())) {
        body_.say("Un travail du service des licences est d\xC3\xA9j\xC3\xA0 en cours : attends sa fin, puis r\xC3\xA9" "essaie.", 2);
    } else {
        code_->setText({});
        body_.say("Activation en cours\xE2\x80\xA6", 0);
    }
    deploy::crypto::wipe(code.data(), code.size());
    refresh();
}

void ActivationPanel::askRelease() {
    if (releaseId_.empty()) return;
    auto dialog = std::make_unique<MessageDialog>(
        "Lib\xC3\xA9rer ce poste",
        "Ce PC rend son poste de la licence " + releaseId_ + " : un autre PC pourra prendre sa place (tout de suite, ou \xC3\xA0 la fin du bail de ce "
        "PC si la licence a d\xC3\xA9j\xC3\xA0 chang\xC3\xA9 de PC deux fois ce mois-ci, ou autant que de postes), et les fonctions de la licence se "
        "fermeront ici (ton travail reste ouvert ; Enregistrer et Exporter restent permis). Pour la retrouver plus tard sur ce PC : son code, s'il "
        "reste un poste libre.",
        MessageDialog::Icon::Question, "Lib\xC3\xA9rer ce poste");
    dialog->setCancelLabel("Garder la licence ici");
    const std::string id = releaseId_;
    menus_.ShowDialog(std::move(dialog), [this, id](const menu::DialogResult& r) {
        if (!r.accepted()) return;
        if (!licence::service::startRelease(id)) body_.say("Un travail du service des licences est d\xC3\xA9j\xC3\xA0 en cours : attends sa fin.", 2);
        else body_.say("Lib\xC3\xA9ration de ce poste\xE2\x80\xA6", 0);
        refresh();
    });
}

void ActivationPanel::onLayout() {
    const auto b = bounds();
    const float x = b.x + 16.f, W = std::max(200.f, b.w - 32.f);
    const float labelW = 210.f, valueX = x + labelW, valueW = std::max(120.f, W - labelW);
    const gfx::FontId small{14}, head{15};
    const float lh = ui::lineHeight(small) + 4.f;
    const auto wrap = [](const std::string& text, gfx::FontId f, float w) {
        std::vector<std::string> out;
        std::string line, word;
        const auto flush = [&] {
            if (word.empty()) return;
            const std::string t = line.empty() ? word : line + " " + word;
            if (!line.empty() && ui::measureWidth(t, f) > w) {
                out.push_back(line);
                line = word;
            } else {
                line = t;
            }
            word.clear();
        };
        for (const char c : text) {
            if (c == ' ') flush();
            else word += c;
        }
        flush();
        if (!line.empty()) out.push_back(line);
        return out;
    };
    float y = b.y + 12.f;
    for (auto& it : items_) {
        it.lines.clear();
        switch (it.kind) {
            case Item::Heading:
                if (&it != &items_.front()) y += 10.f;
                it.y = y;
                y += ui::lineHeight(head) + 8.f;
                break;
            case Item::Text:
                it.lines = wrap(it.value, small, W);
                if (it.lines.size() > 4) it.lines.resize(4);
                it.y = y;
                y += lh * static_cast<float>(it.lines.size()) + 4.f;
                break;
            case Item::Pair:
                it.lines = wrap(it.value, small, valueW);
                if (it.lines.size() > 3) it.lines.resize(3);
                if (it.lines.empty()) it.lines.push_back({});
                it.y = y;
                y += lh * static_cast<float>(it.lines.size());
                break;
            case Item::Field:
                it.y = y;
                for (auto* w : it.widgets) w->setBounds({valueX, y, std::min(520.f, valueW), 28.f});
                y += 36.f;
                break;
            case Item::Buttons: {
                it.y = y + 2.f;
                float bx = valueX;
                for (auto* w : it.widgets) {
                    const float bw = std::max(150.f, w->sizeHint().preferred.w);
                    w->setBounds({bx, it.y, bw, 32.f});
                    bx += bw + 8.f;
                }
                y += 44.f;
                break;
            }
        }
    }
    bottom_ = y;
}

void ActivationPanel::onPaint(const ui::PaintContext& ctx) {
    auto& r = ctx.r;
    const auto& c = ctx.theme.color;
    const auto b = bounds();
    const float x = b.x + 16.f, W = std::max(200.f, b.w - 32.f), labelW = 210.f;
    const gfx::FontId small{14}, head{15};
    const float lh = r.lineHeight(small) + 4.f;
    const gfx::Color good = ctx.theme.tone(ui::Tone::Ok, c.text), warn = ctx.theme.tone(ui::Tone::Warning, c.text),
                     bad = ctx.theme.tone(ui::Tone::Error, c.text);
    const auto color = [&](int tone) { return tone == 1 ? good : tone == 2 ? warn : tone == 3 ? bad : tone == 5 ? c.textMuted : c.text; };
    r.pushClip(b);
    for (const auto& it : items_) {
        switch (it.kind) {
            case Item::Heading:
                r.drawText({x, it.y}, it.label, gfx::semibold(head), c.text);   // 1.41.0 : la demi-grasse
                r.fillRect({x, it.y + r.lineHeight(head) + 3.f, W, 1.f}, c.border);
                break;
            case Item::Text: {
                float y = it.y;
                for (const auto& l : it.lines) {
                    r.drawText({x, y}, fit(r, l, small, W), small, color(it.tone));
                    y += lh;
                }
                break;
            }
            case Item::Pair: {
                r.drawText({x, it.y}, fit(r, it.label, small, labelW - 10.f), small, c.textMuted);
                float y = it.y;
                for (const auto& l : it.lines) {
                    r.drawText({x + labelW, y}, fit(r, l, small, W - labelW), small, color(it.tone));
                    y += lh;
                }
                break;
            }
            case Item::Field:
                r.drawText({x, it.y + (28.f - r.lineHeight(small)) * 0.5f}, it.label, small, c.text);
                break;
            case Item::Buttons:
                break;
        }
    }
    r.popClip();
}

// ============================================================ 1.34.0 / 1.35.0 : Demande a l'editeur ==
// 1.37.0 : ce que donne une edition, en une ligne : "1 PC \xC2\xB7 1 an \xC2\xB7 18 fonctions".
std::string durationText(const licensing::RegistryEditionView& e) {
    if (e.type == "perpetual" || e.durationDays <= 0) return "sans fin";
    if (e.durationDays % 365 == 0) return e.durationDays == 365 ? std::string("1 an") : std::to_string(e.durationDays / 365) + " ans";
    return std::to_string(e.durationDays) + " jours";
}
std::string editionFacts(const licensing::RegistryEditionView& e) {
    std::string t = std::to_string(std::max(1, e.maxDevices)) + (e.type == "floating" ? " postes en m\xC3\xAAme temps" : e.maxDevices > 1 ? " PC" : " PC");
    t += " \xC2\xB7 " + durationText(e);
    // 1.44.0 : << 18/21 fonctions >> - sur combien le registre en a (les retirees ne comptent pas)
    std::size_t all = 0;
    if (const auto reg = licensing::getRegistry())
        for (const auto& f : reg->features)
            if (!f.retired) ++all;
    t += " \xC2\xB7 " + std::to_string(e.features.size()) + (all > 0 ? "/" + std::to_string(all) : std::string()) + (e.features.size() > 1 || all > 1 ? " fonctions" : " fonction");
    return t;
}

OnlinePanel::OnlinePanel(LicenceBody& body, menu::MenuManager& menus) : ui::Widget("licence.demande"), body_(body), menus_(menus) {
    customer_ = field("licence.demande.client", "le nom de ta soci\xC3\xA9t\xC3\xA9 (obligatoire)", licence::service::kMaxCustomer);
    contact_ = field("licence.demande.contact", "ton nom, ou un t\xC3\xA9l\xC3\xA9phone (facultatif)", licence::service::kMaxContact);
    email_ = field("licence.demande.courriel", "nom@societe.fr (facultatif : le mail de l'\xC3\xA9" "diteur quand il accepte)", licence::service::kMaxEmail);
    pcName_ = field("licence.demande.pc", "Poste bureau d'\xC3\xA9tudes (facultatif)", licence::service::kMaxPcName);
    licenseId_ = field("licence.demande.licence", "XPG-2026-00042 (facultatif : pour ajouter ce PC \xC3\xA0 une licence de ta soci\xC3\xA9t\xC3\xA9)",
                       licence::service::kMaxLicenseId);
    message_ = field("licence.demande.message", "un mot pour l'\xC3\xA9" "diteur (facultatif)", licence::service::kMaxMessage);
    send_ = &static_cast<ui::Button&>(addChild(std::make_unique<ui::Button>("Envoyer \xC3\xA0 l'\xC3\xA9" "diteur", "licence.demande.envoyer")));
    send_->setStyle(ui::Button::Style::Primary);
    cancel_ = &static_cast<ui::Button&>(addChild(std::make_unique<ui::Button>("Annuler la demande", "licence.demande.annuler")));
    cancel_->setStyle(ui::Button::Style::Danger);
    check_ = &static_cast<ui::Button&>(addChild(std::make_unique<ui::Button>("V\xC3\xA9rifier maintenant", "licence.demande.verifier")));
    check_->setTooltip("Demande au service des licences ce que l'\xC3\xA9" "diteur a r\xC3\xA9pondu (le Studio le fait aussi tout seul : \xC3\xA0 chaque lancement, "
                       "puis toutes les minutes tant qu'une demande attend - toutes les 5 minutes apr\xC3\xA8s deux heures).");
    // 1.46.0 : Mon profil n'est plus une rubrique - sa petite fenetre, d'ici (il remplit ces champs)
    profile_ = &static_cast<ui::Button&>(addChild(std::make_unique<ui::Button>("Modifier mon profil\xE2\x80\xA6", "licence.demande.profil")));
    profile_->setStyle(ui::Button::Style::Flat);
    profile_->setIcon(ui::Icon::User);
    profile_->setTooltip("Ton courriel, ton nom, ta soci\xC3\xA9t\xC3\xA9 (facultatifs) : ils remplissent cette demande ; le courriel re\xC3\xA7oit le mail "
                         "de l'\xC3\xA9" "diteur quand il accepte.");
    links_ += send_->clicked->connect([this] { doSend(); });
    links_ += cancel_->clicked->connect([this] { askCancel(); });
    links_ += check_->clicked->connect([this] { doCheck(); });
    links_ += profile_->clicked->connect([this] { showProfileWindow(menus_); });
    // 1.38.0 : l'offre choisie - ses fonctionnalites et la capture de celle qu'on clique (zoom, en grand).
    offer_ = &static_cast<FeatureTablePanel&>(addChild(std::make_unique<FeatureTablePanel>("licence.demande.offre", "licence.demande.offre.liste")));
    offer_->setVertical(true);
    offer_->table().setColumns({{"Fonctionnalit\xC3\xA9", 230.f}, {"Logiciel", 110.f}});
    offer_->pane().setEmptyText("Clique une fonctionnalit\xC3\xA9 : sa capture et ce qu'elle fait s'affichent ici (molette : zoom, clic : en grand).");
    offer_->pane().onEnlarge = [this](const FeatureInfo& f, const licence::FeaturePreview& p, std::size_t shot, const std::vector<hmi::Resource>& loaded) {
        showCapture(menus_, f, p, shot, loaded, offer_->choices());   // 1.46.0 : l'offre entiere a cote (ce qu'elle n'a pas : barre)
    };
    // 1.45.0 : LES LICENCES DE CE PC, en tete - Regarder (l'onglet qui la montre), Liberer (rendre la place de ce PC).
    pcs_ = &static_cast<licence::PcLicencesView&>(addChild(std::make_unique<licence::PcLicencesView>("licence.demande.ce-pc")));
    pcs_->onLook = [this](const licence::PcLicence& l) { body_.showTab(l.tab); };
    pcs_->onRelease = [this](const licence::PcLicence& l) { askReleasePc(l); };
    prefill();   // 1.37.0 : Mon profil
    refresh();
}

ui::InputText* OnlinePanel::field(const char* id, const std::string& placeholder, std::size_t maxLen) {
    auto f = std::make_unique<ui::InputText>(id);
    f->setPlaceholder(placeholder);
    f->setMaxLength(maxLen);
    return &static_cast<ui::InputText&>(addChild(std::move(f)));
}

void OnlinePanel::prefill() {
    // 1.46.0 : un champ vide, ou encore tel que le profil l'avait rempli, suit le profil (modifie dans sa fenetre) ; ce que
    // tu y as tape toi-meme reste.
    const auto p = licence::loadProfile();
    const auto follow = [](ui::InputText* f, const std::string& before, const std::string& now) {
        if (f->text().empty() || f->text() == before) f->setText(now);
    };
    const auto customerOf = [](const licence::Profile& q) { return !q.company.empty() ? q.company : q.name; };
    const auto contactOf = [](const licence::Profile& q) { return !q.company.empty() ? q.name : std::string(); };
    follow(customer_, customerOf(filled_), customerOf(p));
    follow(contact_, contactOf(filled_), contactOf(p));
    follow(email_, filled_.email, p.email);
    filled_ = p;
}

void OnlinePanel::buildCards() {
    cards_.clear();
    const auto reg = licensing::getRegistry();
    if (!reg) return;
    const std::int64_t now = static_cast<std::int64_t>(std::time(nullptr));
    for (const auto& e : reg->editions) {
        if (e.retired || e.id == reg->unlicensedEdition || !e.onSale(now)) continue;   // 1.42.0 : hors de sa periode de vente : pas montree
        Card k;
        k.id = e.id;
        k.label = e.label.empty() ? e.id : e.label;
        k.price = e.price;
        k.promo = promoOf(e);   // 1.38.0
        if (!k.promo.empty()) k.promoEnd = "jusqu'au " + licence::service::dayText(e.promoUntil);
        k.description = e.description;
        k.facts = editionFacts(e);
        if (e.saleUntil) k.facts += (k.facts.empty() ? "" : " \xC2\xB7 ") + std::string("en vente jusqu'au ") + licence::service::dayText(e.saleUntil - 1);   // 1.42.0
        cards_.push_back(std::move(k));
        if (cards_.size() >= 8) break;   // une boutique lisible
    }
    if (std::none_of(cards_.begin(), cards_.end(), [this](const Card& k) { return k.id == chosen_; })) chosen_.clear();
}

std::string OnlinePanel::futureText(const std::string& id) const {
    const auto reg = licensing::getRegistry();
    if (!reg || id.empty()) return {};
    for (const auto& e : reg->editions) {
        if (e.id != id) continue;
        std::string names;
        std::size_t n = 0;
        for (const auto& f : reg->features)
            if (std::find(e.features.begin(), e.features.end(), f.id) != e.features.end()) {
                names += (n++ ? ", " : "") + (f.label.empty() ? f.id : f.label);
            }
        std::string t = "\xC3\x80 l'acceptation, ta licence " + (e.label.empty() ? e.id : e.label) + " contiendra : " + editionFacts(e);
        if (!names.empty()) t += " \xE2\x80\x94 " + names;
        t += ". L'\xC3\xA9" "diteur peut l'ajuster en l'acceptant.";
        return t;
    }
    return {};
}

void OnlinePanel::refresh() {
    namespace ls = licence::service;
    available_ = ls::available(&why_);
    if (std::string w = licence::takeWantedOffer(); !w.empty()) chosen_ = w;   // 1.44.0 : l'offre a montrer (l'essai)
    recProblem_.clear();
    if (!ls::loadRecord(rec_, &recProblem_)) rec_ = ls::Record{};
    pending_ = ls::pending(rec_);
    busy_ = ls::busyJob();
    {   // 1.45.0 : les licences de ce PC (l'etat, le releve qu'on vient de lire)
        const auto st = licensing::getLicenseState();
        pcs_->setRows(licence::pcLicences(st, rec_, ls::leaseHeldBack(), licensing::systemNow()), licence::pcLicencesEmptyText(st));
        for (std::size_t i = 0; i < pcs_->rows().size(); ++i)
            if (auto* b = pcs_->releaseButton(i)) b->setEnabled(busy_ == ls::Job::None);
    }
    buildCards();
    const bool form = available_ && !pending_;
    for (auto* f : {customer_, contact_, email_, pcName_, licenseId_, message_}) {
        f->setVisibility(form ? ui::Visibility::Visible : ui::Visibility::Collapsed);
        f->setEnabled(busy_ == ls::Job::None);
    }
    if (form && licenseId_->text().empty()) {
        const auto st = licensing::getLicenseState();
        if (!st.licenseId.empty()) licenseId_->setText(st.licenseId);
    }
    send_->setVisibility(form ? ui::Visibility::Visible : ui::Visibility::Collapsed);
    profile_->setVisibility(form ? ui::Visibility::Visible : ui::Visibility::Collapsed);   // 1.46.0
    cancel_->setVisibility(available_ && pending_ ? ui::Visibility::Visible : ui::Visibility::Collapsed);
    check_->setVisibility(available_ ? ui::Visibility::Visible : ui::Visibility::Collapsed);
    for (auto* b : {send_, cancel_, check_}) b->setEnabled(busy_ == ls::Job::None);
    buildOffer();
    send_->setText(busy_ == ls::Job::Request ? std::string("Envoi en cours\xE2\x80\xA6") : std::string("Envoyer \xC3\xA0 l'\xC3\xA9" "diteur"));
    cancel_->setText(busy_ == ls::Job::CancelRequest ? std::string("Annulation en cours\xE2\x80\xA6") : std::string("Annuler la demande"));
    check_->setText(busy_ == ls::Job::Device ? std::string("V\xC3\xA9rification en cours\xE2\x80\xA6") : std::string("V\xC3\xA9rifier maintenant"));
    invalidateLayout();
    invalidate();
}

void OnlinePanel::doSend() {
    namespace ls = licence::service;
    ls::RequestForm f;
    f.customer = customer_->text();
    f.contact = contact_->text();
    f.email = email_->text();   // 1.37.0
    f.edition = chosen_;
    f.pcName = pcName_->text();
    f.licenseId = licenseId_->text();
    f.message = message_->text();
    if (f.customer.find_first_not_of(" \t") == std::string::npos) {
        body_.say("Indique d'abord le nom de ta soci\xC3\xA9t\xC3\xA9 (Soci\xC3\xA9t\xC3\xA9 / client).", 2);
        return;
    }
    // Le reseau part sur le fil du service ; la fenetre relevera le resultat (LicenceBody::poll).
    if (!ls::startRequest(f)) body_.say("Un travail du service des licences est d\xC3\xA9j\xC3\xA0 en cours : attends sa fin.", 2);
    else body_.say("Envoi de la demande \xC3\xA0 l'\xC3\xA9" "diteur\xE2\x80\xA6", 0);
    refresh();
}

void OnlinePanel::askCancel() {
    auto dialog = std::make_unique<MessageDialog>(
        "Annuler la demande",
        "La demande n\xC2\xB0 " + std::to_string(rec_.number) + " (envoy\xC3\xA9" "e le " + licence::service::dateText(rec_.sentAt) +
            ") est retir\xC3\xA9" "e : l'\xC3\xA9" "diteur ne la verra plus en attente. Tu pourras en envoyer une nouvelle.",
        MessageDialog::Icon::Question, "Annuler la demande");
    dialog->setCancelLabel("Garder la demande");
    menus_.ShowDialog(std::move(dialog), [this](const menu::DialogResult& r) {
        if (!r.accepted()) return;
        if (!licence::service::startCancelRequest()) body_.say("Un travail du service des licences est d\xC3\xA9j\xC3\xA0 en cours : attends sa fin.", 2);
        else body_.say("Annulation de la demande\xE2\x80\xA6", 0);
        refresh();
    });
}

// 1.45.0 : Liberer une licence de ce PC - la question (ce qui se passe, la Decouverte ou plus de licence apres), puis le
// travail du service (comme licence-liberer ; le poste flottant : rendu) ; une licence par fichier, une option : la phrase.
void OnlinePanel::askReleasePc(const licence::PcLicence& l) {
    namespace ls = licence::service;
    const auto plan = licence::releasePlan(l, licensing::getLicenseState(), licensing::systemNow());
    if (plan.action == licence::ReleasePlan::Action::None) {
        menus_.ShowDialog(std::make_unique<MessageDialog>(plan.title, plan.text, MessageDialog::Icon::Info), nullptr);
        return;
    }
    if (!available_) {
        body_.say(why_.empty() ? std::string("Le service des licences n'est pas dans ce programme : rien ne se lib\xC3\xA8re d'ici.") : why_, 2);
        return;
    }
    auto dialog = std::make_unique<MessageDialog>(plan.title, plan.text, MessageDialog::Icon::Question, plan.confirm);
    dialog->setCancelLabel(plan.cancel);
    dialog->setParagraphWidth(640.f);
    const auto action = plan.action;
    const std::string id = l.id;
    menus_.ShowDialog(std::move(dialog), [this, action, id](const menu::DialogResult& r) {
        if (!r.accepted()) return;
        const bool seat = action == licence::ReleasePlan::Action::ReturnSeat;
        if (!(seat ? ls::startReturnLease() : ls::startRelease(id)))
            body_.say("Un travail du service des licences est d\xC3\xA9j\xC3\xA0 en cours : attends sa fin.", 2);
        else
            body_.say(seat ? std::string("Retour du poste flottant\xE2\x80\xA6") : "Lib\xC3\xA9ration de la licence " + id + "\xE2\x80\xA6", 0);
        refresh();
    });
}

void OnlinePanel::doCheck() {
    if (!licence::service::startCheckDevice()) {
        body_.say(available_ ? std::string("Un travail du service des licences est d\xC3\xA9j\xC3\xA0 en cours : attends sa fin.") : why_, 2);
        return;
    }
    body_.say("V\xC3\xA9rification aupr\xC3\xA8s du service des licences\xE2\x80\xA6", 0);
    refresh();
}

void OnlinePanel::buildOffer() {
    const std::string id = pending_ ? rec_.edition : chosen_;
    const auto reg = licensing::getRegistry();
    const bool visible = available_ && reg && !reg->features.empty();
    offer_->setVisibility(visible ? ui::Visibility::Visible : ui::Visibility::Collapsed);
    const std::string key = id + "|" + std::to_string(reg ? reg->number : 0);   // le registre change : relu
    if (!visible || key == offerShown_) return;
    offerShown_ = key;
    const licensing::RegistryEditionView* ed = nullptr;
    for (const auto& e : reg->editions)
        if (!id.empty() && e.id == id) ed = &e;
    std::vector<std::vector<std::string>> rows;
    std::vector<FeatureInfo> infos;
    // 1.44.0 : une offre choisie montre AUSSI ce qu'elle n'a pas, barre (apres ce qu'elle a) : << 18/21 >>
    std::vector<std::vector<std::string>> outRows;
    std::vector<FeatureInfo> outInfos;
    std::size_t all = 0;
    for (const auto& f : reg->features) {
        if (f.retired) continue;
        ++all;
        const bool inOffer = !ed || std::find(ed->features.begin(), ed->features.end(), f.id) != ed->features.end();
        std::string in;
        for (const auto& e : reg->editions)
            if (!e.retired && e.id != reg->unlicensedEdition && std::find(e.features.begin(), e.features.end(), f.id) != e.features.end())
                in += (in.empty() ? "" : ", ") + (e.label.empty() ? e.id : e.label);
        (inOffer ? rows : outRows).push_back({f.label.empty() ? f.id : f.label, f.product});
        // 1.46.0 : pas dans cette offre - barree, ses captures quand meme (l'apercu, et en grand avec toute l'offre a cote)
        const std::string offerName = ed ? (ed->label.empty() ? ed->id : ed->label) : std::string();
        (inOffer ? infos : outInfos).push_back({f.id, f.label.empty() ? f.id : f.label, f.product, in, !inOffer, inOffer ? std::string() : "pas dans " + offerName});
    }
    const std::size_t included = rows.size();
    for (auto& r : outRows) rows.push_back(std::move(r));
    for (auto& i : outInfos) infos.push_back(std::move(i));
    const std::string name = ed ? (ed->label.empty() ? ed->id : ed->label) : std::string();
    offerTitle_ = ed ? (pending_ ? "Ce que contient ta demande : " : "Ce que contient ") + name + " (" + std::to_string(included) + "/" + std::to_string(all) + ")"
                     : "Les fonctionnalit\xC3\xA9s des offres (" + std::to_string(rows.size()) + ") - choisis une offre";
    offerModel_ = std::make_shared<Rows>(std::vector<std::string>{"Fonctionnalit\xC3\xA9", "Logiciel"}, std::move(rows),
                                         [included](ui::RowIndex r, std::size_t) {
                                             ui::CellStyle s;
                                             s.locked = r >= included;   // pas dans cette offre : barree, un cadenas
                                             return s;
                                         });
    offer_->table().setModel(offerModel_);
    offer_->setRows(std::move(infos));
    // L'offre choisie : la premiere fonctionnalite tout de suite (une capture a regarder).
    if (ed && offerModel_->rowCount() > 0) offer_->table().selectModelRows({0});
    invalidateLayout();
    invalidate();
}

ui::EventResult OnlinePanel::onEvent(const ui::InputEvent& ev) {
    // 1.42.0 : la molette fait defiler la partie gauche (pas la colonne de l'offre : elle a sa liste). 1.46.0 : sa barre se
    // tire (la souris tenue) et un clic dans sa gouttiere avance d'une page (ui::ScrollArea, placee par onLayout).
    if (bool moved = false; leftBar_.handle(*this, ev, scroll_, &moved)) {
        if (moved) invalidateLayout();
        return ui::EventResult::Consumed;
    }
    // 1.37.0 : la boutique - un clic choisit une edition, un second clic la rend (l'editeur choisira).
    if (const auto* d = std::get_if<ui::MouseDown>(&ev); d && available_ && !pending_)
        for (const auto& k : cards_)
            if (k.box.contains(d->pos)) {
                chosen_ = chosen_ == k.id ? std::string() : k.id;
                buildOffer();   // 1.38.0 : la colonne de droite suit l'offre choisie
                body_.say(chosen_.empty() ? std::string("Aucune \xC3\xA9" "dition choisie : l'\xC3\xA9" "diteur la choisira avec toi.")
                                          : "\xC3\x89" "dition choisie : " + k.label
                                                + (!k.promo.empty() ? " (" + k.promo + ", " + k.promoEnd + ")" : k.price.empty() ? std::string() : " (" + k.price + ")") + ".",
                          0);
                invalidate();
                return ui::EventResult::Consumed;
            }
    return ui::EventResult::Ignored;
}

namespace {
constexpr float kCardGap = 10.f, kIntroLines = 3.f;   // 1.46.0 : plus de hauteur fixe (kCardH 116) - la carte suit ses lignes

// 1.46.0 : des lignes coupees a la largeur, a la mesure de l'interface (la mise en page n'a pas de renderer) ; les mots entiers.
std::vector<std::string> wrapMeasured(const std::string& text, gfx::FontId f, float w) {
    std::vector<std::string> out;
    std::string line, word;
    const auto flush = [&] {
        if (word.empty()) return;
        const std::string t = line.empty() ? word : line + " " + word;
        if (!line.empty() && ui::measureWidth(t, f) > w) {
            out.push_back(line);
            line = word;
        } else {
            line = t;
        }
        word.clear();
    };
    for (const char ch : text) {
        if (ch == ' ') flush();
        else word += ch;
    }
    flush();
    if (!line.empty()) out.push_back(line);
    return out;
}
// << 1 PC . 40 jours . 18/21 fonctions >> : coupee entre ses morceaux (<< . >>) plutot qu'au milieu de l'un d'eux.
std::vector<std::string> wrapDotted(const std::string& text, gfx::FontId f, float w) {
    const std::string dot = " \xC2\xB7 ";
    std::vector<std::string> parts, out;
    for (std::size_t from = 0;;) {
        const auto at = text.find(dot, from);
        parts.push_back(text.substr(from, at == std::string::npos ? std::string::npos : at - from));
        if (at == std::string::npos) break;
        from = at + dot.size();
    }
    std::string line;
    for (const auto& part : parts) {
        const std::string t = line.empty() ? part : line + dot + part;
        if (line.empty() || ui::measureWidth(t, f) <= w) {
            line = t;
            continue;
        }
        out.push_back(line);
        line = part;
    }
    if (!line.empty()) out.push_back(line);
    // un morceau plus large que la carte a lui seul : coupe a ses mots
    std::vector<std::string> fitted;
    for (const auto& l : out) {
        if (ui::measureWidth(l, f) <= w) fitted.push_back(l);
        else
            for (auto& x : wrapMeasured(l, f, w)) fitted.push_back(std::move(x));
    }
    return fitted;
}
} // namespace

float OnlinePanel::layoutCard(Card& k, float w) const {
    const float iw = std::max(40.f, w - 20.f);
    const bool on = k.id == chosen_;
    k.titleL = wrapMeasured(k.label, kValue, iw - (on ? 22.f : 0.f));
    if (k.titleL.size() > 2) k.titleL.resize(2);
    k.priceL.clear();
    k.promoL.clear();
    k.endL.clear();
    k.priceInline = false;
    if (!k.promo.empty()) {
        // une reduction : l'ancien prix barre et le nouveau sur une ligne s'ils tiennent, sinon l'un sous l'autre ; sa fin, entiere
        if (!k.price.empty() && ui::measureWidth(k.price, kSmall) + 8.f + ui::measureWidth(k.promo, gfx::semibold(kSmall)) <= iw) {
            k.priceInline = true;
            k.priceL = {k.price};
            k.promoL = {k.promo};
        } else {
            if (!k.price.empty()) k.priceL = wrapMeasured(k.price, kSmall, iw);
            k.promoL = wrapMeasured(k.promo, gfx::semibold(kSmall), iw);
        }
        k.endL = wrapMeasured("R\xC3\xA9" "duction " + k.promoEnd, kSmall, iw);
    } else {
        k.priceL = wrapMeasured(k.price.empty() ? std::string("prix sur demande") : k.price, kSmall, iw);
    }
    k.descL = wrapMeasured(k.description, kSmall, iw);
    if (k.descL.size() > 3) {   // la phrase : trois lignes au plus (la suite : dans Editions)
        k.descL.resize(3);
        k.descL.back() += " \xE2\x80\xA6";
    }
    k.factsL = wrapDotted(k.facts, kSmall, iw);
    const float lhTitle = ui::lineHeight(kValue) + 2.f;
    const std::size_t priceLines = k.priceInline ? 1 : k.priceL.size() + k.promoL.size();
    return 8.f + static_cast<float>(k.titleL.size()) * lhTitle + static_cast<float>(priceLines) * 19.f + static_cast<float>(k.endL.size()) * 18.f +
           static_cast<float>(k.descL.size()) * 18.f + 4.f + static_cast<float>(k.factsL.size()) * 18.f + 8.f;
}

void OnlinePanel::onLayout() {
    const auto b = bounds();
    const float x = b.x + 16.f, fullW = std::max(200.f, b.w - 32.f);
    scroll_ = std::clamp(scroll_, 0.f, std::max(0.f, contentH_ - b.h));
    const float top = b.y - scroll_;   // 1.42.0 : la partie gauche defile ; la colonne de droite (l'offre) reste
    const float fixedRowsY = b.y + 16.f + kIntroLines * 20.f + 10.f;
    rowsY_ = top + 16.f + kIntroLines * 20.f + 10.f;   // sous l'introduction (trois lignes au plus)
    // 1.38.0 : a droite, l'offre choisie (une fenetre assez large) ; sinon tout a gauche, comme avant.
    float W = fullW;
    offerBox_ = {};
    if (offer_->visibility() == ui::Visibility::Visible) {
        if (fullW >= 900.f) {
            const float cw = std::clamp(std::floor(fullW * 0.36f), 320.f, 480.f);
            W = fullW - cw - 20.f;
            offerBox_ = {x + W + 20.f, fixedRowsY, cw, std::max(120.f, b.bottom() - 8.f - fixedRowsY)};
            offer_->setBounds({offerBox_.x, offerBox_.y + 24.f, offerBox_.w, std::max(80.f, offerBox_.h - 24.f)});
        } else
            offer_->setBounds({b.x, b.y, 0.f, 0.f});
    }
    leftW_ = W;
    {   // 1.45.0 : les licences de ce PC, en tete (sous l'introduction ; la boutique et la demande en dessous)
        const float ph = pcs_->wantedHeight();
        pcs_->setBounds({x, rowsY_, W, ph});
        rowsY_ += ph + 14.f;
    }
    const float labelW = 210.f, fieldX = x + labelW, fieldW = std::max(160.f, std::min(620.f, W - labelW));
    float y = rowsY_;
    if (available_ && !pending_) {
        // 1.37.0 : la boutique - les cartes des editions, sur une ou deux rangees.
        shopY_ = y;
        if (!cards_.empty()) {
            y += 24.f;
            const std::size_t n = cards_.size();
            // 1.46.0 : des cartes de 210 px au moins (avant 180 : leurs lignes coupees), aussi hautes que leurs lignes ; une rangee
            // a la hauteur de sa plus haute carte
            const std::size_t perRow = std::max<std::size_t>(1, std::min<std::size_t>(n, static_cast<std::size_t>((W + kCardGap) / (210.f + kCardGap))));
            const float cw = std::floor(std::min(300.f, (W - kCardGap * static_cast<float>(perRow - 1)) / static_cast<float>(perRow)));
            for (std::size_t row = 0; row * perRow < n; ++row) {
                float rowH = 0.f;
                for (std::size_t i = row * perRow; i < std::min(n, (row + 1) * perRow); ++i) rowH = std::max(rowH, layoutCard(cards_[i], cw));
                rowH = std::ceil(rowH);
                for (std::size_t i = row * perRow; i < std::min(n, (row + 1) * perRow); ++i)
                    cards_[i].box = {x + static_cast<float>(i % perRow) * (cw + kCardGap), y, cw, rowH};
                y += rowH + kCardGap;
            }
            y += 4.f;
        }
        fieldsY_ = y;
        for (auto* f : {customer_, contact_, email_, licenseId_, pcName_, message_}) {
            f->setBounds({fieldX, y, fieldW, 28.f});
            y += 34.f;
        }
        y += 4.f;
        float bx = fieldX;
        for (auto* btn : {send_, check_}) {
            const float bw = std::max(150.f, btn->sizeHint().preferred.w);
            btn->setBounds({bx, y, bw, 32.f});
            bx += bw + 8.f;
        }
        {   // 1.46.0 : << Modifier mon profil... >> a la suite (ou dessous, si la place manque)
            const float pw = std::ceil(profile_->sizeHint().preferred.w + 8.f);
            if (bx + pw <= x + W) {
                profile_->setBounds({bx + 4.f, y, pw, 32.f});
            } else {
                y += 38.f;
                profile_->setBounds({fieldX, y, pw, 32.f});
            }
        }
        y += 44.f;
    } else if (available_) {
        y += 7.f * 22.f + 3.f * 20.f + 10.f;   // la demande en cours (sept lignes) et ce que contiendra sa licence
        float bx = fieldX;
        for (auto* btn : {cancel_, check_}) {
            const float bw = std::max(150.f, btn->sizeHint().preferred.w);
            btn->setBounds({bx, y, bw, 32.f});
            bx += bw + 8.f;
        }
        y += 44.f;
    }
    infoY_ = rowsY_;
    footY_ = y;
    // 1.42.0 : la hauteur de la partie gauche (avec les lignes du bas : la derniere reponse, la verification) ; plus
    // basse que l'onglet, elle defile.
    const float before = contentH_;
    contentH_ = (footY_ + 4.f * 20.f + 8.f) - top;
    if (std::abs(contentH_ - before) > 0.5f && !relayout_) {
        relayout_ = true;   // le decalage borne a la nouvelle hauteur : une seconde passe
        onLayout();
        relayout_ = false;
    }
    // 1.46.0 : la barre de la partie gauche - sous l'introduction, entre la partie gauche et l'offre ; la molette sur la
    // partie gauche seulement (l'offre a sa liste).
    leftBar_.place({b.x, b.y, (offerBox_.w > 0.f ? offerBox_.x : b.right()) - b.x, b.h}, contentH_, b.h,
                   {x + leftW_ + 5.f, fixedRowsY, ui::kScrollBarThickness, std::max(40.f, b.bottom() - 4.f - fixedRowsY)});
}

void OnlinePanel::onPaint(const ui::PaintContext& ctx) {
    namespace ls = licence::service;
    auto& r = ctx.r;
    const auto& c = ctx.theme.color;
    const auto b = bounds();
    const float x = b.x + 16.f, lh = r.lineHeight(kSmall) + 4.f;
    float W = std::max(200.f, b.w - 32.f);
    const float labelW = 210.f, fieldX = x + labelW;
    // L'introduction.
    // 1.36.0 : la demande en ligne, le chemin principal : ce qui va se passer, dit simplement.
    const std::string intro = !available_ ? std::string()
        : pending_
        ? std::string("Ta demande attend l'\xC3\xA9" "diteur. Le Studio regarde tout seul, toutes les minutes, s'il a r\xC3\xA9pondu : d\xC3\xA8s qu'il "
                      "l'accepte, ta licence s'installe d'elle-m\xC3\xAAme et une fen\xC3\xAAtre te le confirme (ce qu'elle ouvre, jusqu'\xC3\xA0 quand). "
                      "Tu peux fermer cette fen\xC3\xAAtre, et m\xC3\xAAme le Studio : la r\xC3\xA9ponse t'attendra au prochain lancement.")
        : std::string("Choisis ta licence et demande-la \xC3\xA0 l'\xC3\xA9" "diteur : ta soci\xC3\xA9t\xC3\xA9 suffit, le reste est facultatif (ton profil remplit "
                      "les champs : Modifier mon profil\xE2\x80\xA6). Ta demande part scell\xC3\xA9" "e : lui seul la lit. D\xC3\xA8s qu'il l'accepte, ta licence s'installe d'elle-m\xC3\xAAme et "
                      "une fen\xC3\xAAtre te le confirme - aucun fichier, aucun code \xC3\xA0 taper. Une seule demande \xC3\xA0 la fois pour ce PC.");
    const std::string introOff = why_ + " Utilise les boutons du bas : Demander une activation\xE2\x80\xA6 (un fichier \xC3\xA0 envoyer \xC3\xA0 l'\xC3\xA9" "diteur), "
                                 "puis Activer par fichier de licence\xE2\x80\xA6 avec sa r\xC3\xA9ponse.";
    float y = b.y + 16.f - scroll_;   // 1.42.0 : la partie gauche defile
    // La barre de defilement de la partie gauche quand elle est plus haute que l'onglet (1.44.0 : SOUS l'introduction, qui
    // prend toute la largeur ; 1.46.0 : ui::ScrollArea - elle se tire et se clique, comme celle d'une liste).
    leftBar_.paint(ctx, scroll_);
    int n = 0;
    for (const auto& l : wrapText(r, available_ ? intro : introOff, kSmall, W)) {
        if (++n > 3) break;
        r.drawText({x, y}, fit(r, l, kSmall, W), kSmall, available_ ? c.text : c.textMuted);
        y += 20.f;
    }
    if (!available_) return;
    const gfx::Color good = ctx.theme.tone(ui::Tone::Ok, c.text), warn = ctx.theme.tone(ui::Tone::Warning, c.text),
                     bad = ctx.theme.tone(ui::Tone::Error, c.text);
    // 1.38.0 : la colonne de droite (l'offre) ; la partie gauche garde sa largeur.
    if (offerBox_.w > 0.f) {
        const std::string t = fit(r, offerTitle_, kSmall, offerBox_.w);
        r.drawText({offerBox_.x, offerBox_.y}, t, kSmall, c.text);
        r.drawText({offerBox_.x + 1.f, offerBox_.y}, t, kSmall, c.text);
    }
    if (leftW_ > 0.f) W = std::min(leftW_, W);
    if (!pending_) {
        // 1.37.0 : la boutique.
        if (!cards_.empty()) {
            r.drawText({x, shopY_}, "Choisis ta licence (facultatif : sinon l'\xC3\xA9" "diteur la choisit avec toi) - aucun paiement ici, l'\xC3\xA9" "diteur te contacte.",
                       kSmall, c.textMuted);
            for (const auto& k : cards_) {
                const bool on = k.id == chosen_;
                r.fillRect(k.box, on ? c.headerBg : c.inputBg);
                r.strokeRect(k.box, on ? c.accent : c.border, on ? 2.f : 1.f);
                // 1.46.0 : les lignes de la mise en page (layoutCard), entieres - plus rien de coupe par << ... >>
                const float ix = k.box.x + 10.f;
                float ty = k.box.y + 8.f;
                if (on) r.drawText({k.box.right() - 26.f, ty}, "\xE2\x9C\x93", kValue, c.accent);
                for (const auto& l : k.titleL) {
                    r.drawText({ix, ty}, l, kValue, c.text);
                    r.drawText({ix + 1.f, ty}, l, kValue, c.text);   // gras
                    ty += r.lineHeight(kValue) + 2.f;
                }
                const auto struck = [&](const std::string& l, float px, float py) {
                    const float ow = r.measure(l, kSmall).width;
                    r.drawText({px, py}, l, kSmall, c.textMuted);
                    r.fillRect({px, std::round(py + r.lineHeight(kSmall) * 0.5f), ow, 1.f}, c.textMuted);
                    return ow;
                };
                if (!k.promo.empty()) {
                    // 1.38.0 : une reduction - l'ancien prix barre, le nouveau, et sa fin.
                    const gfx::Color hot = ctx.theme.tone(ui::Tone::Ok, c.accent);
                    const gfx::FontId bold = gfx::semibold(kSmall);
                    if (k.priceInline) {
                        const float ow = struck(k.priceL.front(), ix, ty);
                        r.drawText({ix + ow + 8.f, ty}, k.promoL.front(), bold, hot);
                        ty += 19.f;
                    } else {
                        for (const auto& l : k.priceL) {
                            (void)struck(l, ix, ty);
                            ty += 19.f;
                        }
                        for (const auto& l : k.promoL) {
                            r.drawText({ix, ty}, l, bold, hot);
                            ty += 19.f;
                        }
                    }
                    for (const auto& l : k.endL) {   // sa fin, entiere
                        r.drawText({ix, ty}, l, kSmall, hot);
                        ty += 18.f;
                    }
                } else {
                    for (const auto& l : k.priceL) {
                        r.drawText({ix, ty}, l, kSmall, k.price.empty() ? c.textMuted : c.accent);
                        ty += 19.f;
                    }
                }
                for (const auto& l : k.descL) {
                    r.drawText({ix, ty}, l, kSmall, c.textMuted);
                    ty += 18.f;
                }
                ty += 4.f;
                for (const auto& l : k.factsL) {
                    r.drawText({ix, ty}, l, kSmall, c.text);
                    ty += 18.f;
                }
            }
        }
        const char* labels[6] = {"Soci\xC3\xA9t\xC3\xA9 / client *", "Ton nom", "Courriel", "Licence existante", "Nom de ce PC", "Message"};
        float fy = fieldsY_;
        for (const char* l : labels) {
            r.drawText({x, fy + (28.f - r.lineHeight(kSmall)) * 0.5f}, l, kSmall, c.text);
            fy += 34.f;
        }
    } else {
        // La demande en cours.
        const std::string state = ls::stateText(rec_);
        std::string wanted = "\xE2\x80\x94 (l'\xC3\xA9" "diteur la choisit)";
        if (!rec_.edition.empty()) {
            wanted = rec_.edition;
            if (const auto reg = licensing::getRegistry())
                for (const auto& e : reg->editions)
                    if (e.id == rec_.edition) wanted = (e.label.empty() ? e.id : e.label) + (priceLine(e).empty() ? std::string() : " \xC2\xB7 " + priceLine(e));
        }
        struct Line { const char* label; std::string value; bool strong; };
        const Line lines[7] = {
            {"Demande", "n\xC2\xB0 " + std::to_string(rec_.number), false},
            {"Envoy\xC3\xA9" "e le", ls::dateText(rec_.sentAt), false},
            {"\xC3\x89tat", state, true},
            {"Licence demand\xC3\xA9" "e", wanted, false},   // 1.37.0 : la boutique
            {"Soci\xC3\xA9t\xC3\xA9 / client", rec_.customer.empty() ? std::string("\xE2\x80\x94") : rec_.customer, false},
            {"Nom de ce PC", rec_.pcName.empty() ? std::string("\xE2\x80\x94") : rec_.pcName, false},
            {"Licence existante", rec_.licenseId.empty() ? std::string("\xE2\x80\x94 (une nouvelle licence)") : rec_.licenseId, false},
        };
        float ly = infoY_;
        for (const auto& l : lines) {
            r.drawText({x, ly}, l.label, kSmall, c.textMuted);
            r.drawText({fieldX, ly}, fit(r, l.value, kSmall, W - labelW), kSmall, l.strong ? warn : c.text);
            ly += 22.f;
        }
        // 1.37.0 : le rappel de ce que contiendra la licence acceptee.
        const std::string future = futureText(rec_.edition);
        int k = 0;
        for (const auto& l : wrapText(r, future.empty() ? std::string("\xC3\x80 l'acceptation, l'\xC3\xA9" "diteur choisit l'\xC3\xA9" "dition, la dur\xC3\xA9" "e et le nombre de PC ; "
                                                                      "la fen\xC3\xAAtre de confirmation te dira ce qu'elle ouvre.")
                                                        : future,
                                      kSmall, W)) {
            if (++k > 3) break;
            r.drawText({x, ly + 4.f}, fit(r, l, kSmall, W), kSmall, c.text);
            ly += 20.f;
        }
    }
    // La derniere reponse, la derniere verification. 1.37.0 : jamais sous le bas de l'onglet (une fenetre basse :
    // les lignes du bas ne chevauchent pas les boutons de la fenetre).
    float fy = footY_;
    const float limit = b.bottom() - 2.f;
    if (busy_ != ls::Job::None && fy + lh <= limit) {
        r.drawText({x, fy}, ls::jobText(busy_), kSmall, c.textMuted);
        fy += lh;
    }
    if (!rec_.local.empty() && !pending_ && fy + lh <= limit) {
        const std::string last = "Derni\xC3\xA8re demande (n\xC2\xB0 " + std::to_string(rec_.number) + ", " + ls::dateText(rec_.sentAt) + ") : " + ls::stateText(rec_);
        const gfx::Color col = rec_.outcome == "acceptee" ? good : rec_.outcome == "refusee" ? bad : c.text;
        r.drawText({x, fy}, fit(r, last, kSmall, W), kSmall, col);
        fy += lh;
    }
    if (!rec_.lastMessage.empty()) {
        int k = 0;
        for (const auto& l : wrapText(r, "Derni\xC3\xA8re r\xC3\xA9ponse : " + rec_.lastMessage, kSmall, W)) {
            if (++k > 3 || fy + lh > limit) break;
            r.drawText({x, fy}, fit(r, l, kSmall, W), kSmall, rec_.outcome == "refusee" ? bad : c.text);
            fy += lh;
        }
    }
    if (rec_.lastCheck && fy + lh <= limit) {
        r.drawText({x, fy}, "Derni\xC3\xA8re v\xC3\xA9rification : " + ls::dateText(rec_.lastCheck), kSmall, c.textMuted);
        fy += lh;
    }
    if (!recProblem_.empty() && fy + lh <= limit) r.drawText({x, fy}, fit(r, recProblem_, kSmall, W), kSmall, bad);
}

// ============================================================ 1.37.0 : Mon profil ==
ProfilePanel::ProfilePanel(std::function<void(std::string, int)> say, std::function<void()> saved)
    : ui::Widget("licence.profil"), say_(std::move(say)), saved_(std::move(saved)) {
    const auto mk = [this](const char* id, const char* placeholder) {
        auto f = std::make_unique<ui::InputText>(id);
        f->setPlaceholder(placeholder);
        f->setMaxLength(licence::kMaxProfileField);
        return &static_cast<ui::InputText&>(addChild(std::move(f)));
    };
    email_ = mk("licence.profil.courriel", "nom@societe.fr");
    name_ = mk("licence.profil.nom", "Pr\xC3\xA9nom Nom");
    company_ = mk("licence.profil.societe", "Ta soci\xC3\xA9t\xC3\xA9");
    save_ = &static_cast<ui::Button&>(addChild(std::make_unique<ui::Button>("Enregistrer mon profil", "licence.profil.enregistrer")));
    save_->setStyle(ui::Button::Style::Primary);
    links_ += save_->clicked->connect([this] { doSave(); });
    links_ += email_->textChanged->connect([this](const std::string&) { invalidate(); });   // 1.38.0 : la coche en direct
    refresh();
}

void ProfilePanel::refresh() {
    const auto p = licence::loadProfile();
    email_->setText(p.email);
    name_->setText(p.name);
    company_->setText(p.company);
    invalidate();
}

void ProfilePanel::doSave() {
    licence::Profile p;
    p.email = email_->text();
    p.name = name_->text();
    p.company = company_->text();
    p.verified = licence::loadProfile().verified;   // 1.38.0 : gardee si le courriel ne change pas
    std::string why;
    if (!licence::saveProfile(p, &why)) {
        if (say_) say_("Profil non enregistr\xC3\xA9 : " + why + ".", 3);
        return;
    }
    refresh();
    if (say_) say_("Profil enregistr\xC3\xA9 : il remplit ta demande de licence ; ton courriel recevra le mail de l'\xC3\xA9" "diteur.", 1);
    if (saved_) saved_();
}

void ProfilePanel::onLayout() {
    const auto b = bounds();
    const float x = b.x + 16.f, W = std::max(200.f, b.w - 32.f);
    const float labelW = 210.f, fieldX = x + labelW, fieldW = std::max(160.f, std::min(520.f, W - labelW));
    rowsY_ = b.y + 16.f + 3.f * 20.f + 10.f;
    float y = rowsY_;
    for (auto* f : {email_, name_, company_}) {
        f->setBounds({fieldX, y, fieldW, 28.f});
        y += 36.f;
    }
    markX_ = fieldX + fieldW + 12.f;   // 1.38.0 : la coche du courriel
    save_->setBounds({fieldX, y + 4.f, std::max(180.f, save_->sizeHint().preferred.w), 32.f});
}

void ProfilePanel::onPaint(const ui::PaintContext& ctx) {
    auto& r = ctx.r;
    const auto& c = ctx.theme.color;
    const auto b = bounds();
    const float x = b.x + 16.f, W = std::max(200.f, b.w - 32.f);
    const std::string intro =
        "Tout est facultatif. Ton profil remplit ta demande de licence (Licence\xE2\x80\xA6 \xE2\x80\xBA Demander ma licence) ; ton courriel re\xC3\xA7oit le mail de l'\xC3\xA9" "diteur "
        "quand il accepte ta demande (avec le code d'une licence en ligne). Il reste sur ce PC, pour API Studio et IHM Studio ; rien ne part tant que "
        "tu n'envoies pas une demande.";
    float y = b.y + 16.f;
    int n = 0;
    for (const auto& l : wrapText(r, intro, kSmall, W)) {
        if (++n > 3) break;
        r.drawText({x, y}, fit(r, l, kSmall, W), kSmall, c.text);
        y += 20.f;
    }
    // 1.38.0 : la coche du courriel - sa forme, et s'il a deja recu un mail de l'editeur.
    {
        const std::string mail = email_->text();
        const auto saved = licence::loadProfile();
        const float my = rowsY_ + (28.f - r.lineHeight(kSmall)) * 0.5f, mw = std::max(40.f, b.right() - 16.f - markX_);
        if (mail.find_first_not_of(" \t") == std::string::npos) {
            r.drawText({markX_, my}, fit(r, "(facultatif)", kSmall, mw), kSmall, c.textMuted);
        } else if (!licence::plausibleEmail(mail)) {
            r.drawText({markX_, my}, fit(r, "\xE2\x9C\x97 pas une adresse (nom@domaine.fr)", kSmall, mw), kSmall, ctx.theme.tone(ui::Tone::Error, c.text));
        } else {
            const auto b0 = mail.find_first_not_of(" \t"), b1 = mail.find_last_not_of(" \t");
            const bool verified = saved.emailVerified() && saved.email == mail.substr(b0, b1 - b0 + 1);
            r.drawText({markX_, my}, fit(r, verified ? "\xE2\x9C\x93 v\xC3\xA9rifi\xC3\xA9" "e : un mail de l'\xC3\xA9" "diteur y est parti"
                                                     : "\xE2\x9C\x93 adresse valide",
                                         kSmall, mw),
                       kSmall, ctx.theme.tone(ui::Tone::Ok, c.text));
        }
    }
    const char* labels[3] = {"Courriel", "Nom", "Soci\xC3\xA9t\xC3\xA9"};
    float fy = rowsY_;
    for (const char* l : labels) {
        r.drawText({x, fy + (28.f - r.lineHeight(kSmall)) * 0.5f}, l, kSmall, c.text);
        fy += 36.f;
    }
    r.drawText({x, fy + 48.f}, fit(r, "Enregistr\xC3\xA9 dans : " + licence::profilePath(), kSmall, W), kSmall, c.textMuted);
}

// ============================================================ 1.46.0 : la fenetre Mon profil ==
// Mon profil n'est plus une rubrique de la fenetre Licence (<< mets-le ailleurs >>) : sa petite fenetre, ouverte de l'accueil
// du Studio, d'Aide > Mon profil..., de << Modifier mon profil... >> (la demande) et de showLicenceWindow(kTabProfile).
class ProfileWindow;
ProfileWindow* g_profileWindow = nullptr;

class ProfileBody final : public ui::Widget {
public:
    explicit ProfileBody(std::function<void()> close) : ui::Widget("dialog.profil.corps"), close_(std::move(close)) {
        panel_ = &static_cast<ProfilePanel&>(addChild(std::make_unique<ProfilePanel>(
            [this](std::string t, int tone) {
                flash_ = std::move(t);
                tone_ = tone;
                invalidate();
            },
            [] {
                // la demande de licence ouverte reprend ce qui y manque
                if (g_licenceWindow) g_licenceWindow->profileSaved();
            })));
        closeButton_ = &static_cast<ui::Button&>(addChild(std::make_unique<ui::Button>("Fermer", "dialog.profil.fermer")));
        links_ += closeButton_->clicked->connect([this] {
            if (close_) close_();
        });
    }
    [[nodiscard]] const std::string& flash() const noexcept { return flash_; }

protected:
    void onLayout() override {
        const auto r = bounds();
        const float w = std::min(780.f, std::max(520.f, r.w - 40.f)), h = std::min(440.f, std::max(360.f, r.h - 40.f));
        frame_ = ui::dialogframe::place(*this, {std::floor(r.x + (r.w - w) * 0.5f), std::floor(r.y + (r.h - h) * 0.5f), w, h});
        closeBox_ = {frame_.right() - 36.f, frame_.y + 9.f, 24.f, 24.f};
        ui::dialogframe::keep(*this, closeBox_);
        panel_->setBounds({frame_.x, frame_.y + 46.f, frame_.w, std::max(200.f, frame_.h - 46.f - 92.f)});
        const float cw = std::max(110.f, closeButton_->sizeHint().preferred.w);
        closeButton_->setBounds({frame_.right() - 16.f - cw, frame_.bottom() - 44.f, cw, 32.f});
    }
    void onPaint(const ui::PaintContext& ctx) override {
        auto& r = ctx.r;
        const auto& c = ctx.theme.color;
        r.fillRect(frame_, c.panelBg);
        r.strokeRect(frame_, c.borderStrong, 1.f);
        r.fillRect({frame_.x, frame_.y, frame_.w, 42.f}, c.headerBg);
        ui::drawIcon(r, ui::Icon::User, {frame_.x + 14.f, frame_.y + 13.f, 16.f, 16.f}, c.text);
        r.drawText({frame_.x + 38.f, frame_.y + (42.f - r.lineHeight(kTitle)) * 0.5f}, "Mon profil", kTitle, c.text);
        r.drawText({closeBox_.x + 7.f, closeBox_.y + 2.f}, "\xC3\x97", kTitle, c.textMuted);
        r.fillRect({frame_.x, frame_.y + 42.f, frame_.w, 1.f}, c.border);
        if (!flash_.empty()) {
            const gfx::Color ink = tone_ == 1 ? ctx.theme.tone(ui::Tone::Ok, c.text) : tone_ == 3 ? ctx.theme.tone(ui::Tone::Error, c.text) : c.text;
            const gfx::Rect band{frame_.x + 16.f, frame_.bottom() - 86.f, frame_.w - 32.f, 28.f};
            r.fillRect(band, c.headerBg);
            r.fillRect({band.x, band.y, 3.f, band.h}, ink);
            r.drawText({band.x + 10.f, band.y + (band.h - r.lineHeight(kSmall)) * 0.5f}, fit(r, flash_, kSmall, band.w - 16.f), kSmall, c.text);
        }
    }
    ui::EventResult onEvent(const ui::InputEvent& ev) override {
        if (const auto* d = std::get_if<ui::MouseDown>(&ev); d && closeBox_.contains(d->pos)) {
            if (close_) close_();
            return ui::EventResult::Consumed;
        }
        return ui::EventResult::Ignored;
    }

private:
    std::function<void()> close_;
    ProfilePanel*         panel_{nullptr};
    ui::Button*           closeButton_{nullptr};
    std::string           flash_;
    int                   tone_{0};
    gfx::Rect             frame_{}, closeBox_{};
    core::ConnectionScope links_;
};

class ProfileWindow final : public menu::WidgetMenu {
public:
    ProfileWindow() : menu::WidgetMenu("dialog.profil") { g_profileWindow = this; }
    ~ProfileWindow() override {
        if (g_profileWindow == this) g_profileWindow = nullptr;
    }
    [[nodiscard]] menu::MenuTraits traits() const override { return dialogTraits(); }
    [[nodiscard]] std::string title() const override { return "Mon profil"; }
    ui::EventResult HandleEvent(const ui::InputEvent& ev) override {
        if (const auto* k = std::get_if<ui::KeyDown>(&ev); k && k->key == ui::Key::Escape) {
            fermer();
            return ui::EventResult::Consumed;
        }
        return menu::WidgetMenu::HandleEvent(ev);
    }
    void fermer() {
        if (done_) return;
        done_ = true;
        if (g_profileWindow == this) g_profileWindow = nullptr;
        manager().CloseDialog(menu::DialogResult{menu::DialogResult::Button::Cancel, {}});
    }
    [[nodiscard]] bool closing() const noexcept { return done_; }

protected:
    core::Status buildUi() override {
        setRoot(std::make_unique<ProfileBody>([this] { fermer(); }));
        return core::ok();
    }

private:
    bool done_{false};
};

// 1.38.0 : la capture qu'on agrandit (capturePng, ZoomImage) : 1.45.0, dans LicenceViews.cpp (licence::CaptureView).

// ============================================================ 1.38.0 : l'apercu d'une fonctionnalite ==
FeaturePreviewPane::FeaturePreviewPane(std::string id) : ui::Widget(std::move(id)) {
    image_ = &static_cast<ZoomImage&>(addChild(std::make_unique<ZoomImage>(this->id() + ".capture")));
    image_->onOpen = [this] {
        if (!onEnlarge || !image_->hasImage()) return;
        // 1.45.0 : toutes les captures (les fleches de la fenetre en grand), celle montree d'abord ; celles deja lues
        onEnlarge(shown_, preview_, shot_, loaded_);
    };
    image_->setVisibility(ui::Visibility::Collapsed);
    // 1.44.0 : plusieurs captures - des fleches de part et d'autre de << Capture 2/3 : sa legende >>
    prev_ = &static_cast<ui::Button&>(addChild(std::make_unique<ui::Button>("\xE2\x80\xB9", this->id() + ".precedente")));
    next_ = &static_cast<ui::Button&>(addChild(std::make_unique<ui::Button>("\xE2\x80\xBA", this->id() + ".suivante")));
    links_ += prev_->clicked->connect([this] {
        if (preview_.shots.size() < 2) return;
        shot_ = (shot_ + preview_.shots.size() - 1) % preview_.shots.size();
        applyShot();
    });
    links_ += next_->clicked->connect([this] {
        if (preview_.shots.size() < 2) return;
        shot_ = (shot_ + 1) % preview_.shots.size();
        applyShot();
    });
    prev_->setVisibility(ui::Visibility::Collapsed);
    next_->setVisibility(ui::Visibility::Collapsed);
}

void FeaturePreviewPane::show(const FeatureInfo& info) {
    if (info.id == shown_.id && info.editions == shown_.editions && info.label == shown_.label) {
        if (info.locked != shown_.locked || info.lockedText != shown_.lockedText) {   // 1.46.0 : une autre offre choisie
            shown_.locked = info.locked;
            shown_.lockedText = info.lockedText;
            invalidateLayout();
            invalidate();
        }
        return;
    }
    shown_ = info;
    preview_ = shown_.id.empty() ? licence::FeaturePreview{} : licence::featurePreview(shown_.id);
    loaded_.assign(preview_.shots.size(), hmi::Resource{});
    shot_ = 0;
    applyShot();
    image_->setVisibility(shown_.id.empty() ? ui::Visibility::Collapsed : ui::Visibility::Visible);
}

void FeaturePreviewPane::applyShot() {
    if (shot_ < preview_.shots.size()) {
        const auto& s = preview_.shots[shot_];
        // 1.45.0 : lue une fois (les fleches qui reviennent : rien a relire) ; ses bulles dans leur repere
        if (shot_ >= loaded_.size()) loaded_.resize(preview_.shots.size());
        if (!loaded_[shot_].data) loaded_[shot_] = capturePng(s.imagePath, licence::captureName(shown_.id, shot_));
        image_->setImage(loaded_[shot_]);
        image_->setSpots(s.spots, s.refW, s.refH);
    } else {
        image_->setImage(capturePng(preview_.imagePath, shown_.id + ".png"));
        image_->setSpots({});
    }
    const auto vis = preview_.shots.size() > 1 ? ui::Visibility::Visible : ui::Visibility::Collapsed;
    prev_->setVisibility(vis);
    next_->setVisibility(vis);
    invalidateLayout();
    invalidate();
}

void FeaturePreviewPane::onLayout() {
    const auto b = bounds();
    const float x = b.x + 12.f, W = std::max(60.f, b.w - 24.f);
    float y = b.y + 12.f + 22.f + (shown_.product.empty() && !shown_.locked ? 0.f : 20.f) + 4.f;
    // La capture (960 x 600) a la largeur du volet ; la moitie de la hauteur au plus.
    const bool several = preview_.shots.size() > 1;
    const float ih = std::min(std::floor(W * 0.625f), std::max(80.f, b.h * 0.55f - (several ? 32.f : 0.f)));
    image_->setBounds({x, y, W, ih});
    navY_ = y + ih + 4.f;
    if (several) {
        prev_->setBounds({x, navY_, 30.f, 26.f});
        next_->setBounds({x + W - 30.f, navY_, 30.f, 26.f});
    }
    textY_ = y + ih + 10.f + (several ? 30.f : 0.f);
}

void FeaturePreviewPane::onPaint(const ui::PaintContext& ctx) {
    auto& r = ctx.r;
    const auto& c = ctx.theme.color;
    const auto b = bounds();
    r.fillRect(b, c.headerBg);
    r.strokeRect(b, c.border, 1.f);
    const float x = b.x + 12.f, W = b.w - 24.f;
    float y = b.y + 12.f;
    if (shown_.id.empty()) {
        for (const auto& l : wrapText(r, waiting_, kSmall, W)) {
            r.drawText({x, y}, fit(r, l, kSmall, W), kSmall, c.textMuted);
            y += 20.f;
        }
        return;
    }
    const std::string title = fit(r, shown_.label, kValue, W);
    r.drawText({x, y}, title, kValue, c.text);
    r.drawText({x + 1.f, y}, title, kValue, c.text);
    y += r.lineHeight(kValue) + 2.f;
    // 1.46.0 : une ligne barree - sa capture se montre, et l'apercu dit pourquoi elle est barree (apres le logiciel)
    float px = x;
    if (!shown_.product.empty()) {
        const std::string p = fit(r, shown_.product, kSmall, W);
        r.drawText({x, y}, p, kSmall, c.textMuted);
        px += r.measure(p, kSmall).width + 12.f;
    }
    if (shown_.locked && px + 60.f < x + W) {
        ui::drawIcon(r, ui::Icon::Lock, {px, y + (r.lineHeight(kSmall) - 12.f) * 0.5f, 12.f, 12.f}, c.warning);
        const std::string t = shown_.lockedText.empty() ? std::string("pas dans ta licence") : shown_.lockedText;
        r.drawText({px + 16.f, y}, fit(r, t, kSmall, x + W - px - 16.f), kSmall, ctx.theme.tone(ui::Tone::Warning, c.text));
    }
    // 1.44.0 : << Capture 2/3 : sa legende >>, entre les fleches
    if (preview_.shots.size() > 1) {
        const std::string caption = preview_.shots[shot_].caption;
        const std::string nav = "Capture " + std::to_string(shot_ + 1) + "/" + std::to_string(preview_.shots.size()) + (caption.empty() ? std::string() : " : " + caption);
        const float nw = W - 2.f * 38.f;
        r.drawText({x + 38.f, navY_ + (26.f - r.lineHeight(kSmall)) * 0.5f}, fit(r, nav, kSmall, nw), kSmall, c.text);
    }
    y = textY_;
    int n = 0;
    for (const auto& l : wrapText(r, preview_.description.empty() ? std::string("(pas encore de description)") : preview_.description, kSmall, W)) {
        if (++n > 4 || y + 20.f > b.bottom()) break;
        r.drawText({x, y}, fit(r, l, kSmall, W), kSmall, preview_.description.empty() ? c.textMuted : c.text);
        y += 20.f;
    }
    y += 6.f;
    if (!shown_.editions.empty())
        for (const auto& l : wrapText(r, "Dans : " + shown_.editions, kSmall, W)) {
            if (y + 20.f > b.bottom()) break;
            r.drawText({x, y}, fit(r, l, kSmall, W), kSmall, c.textMuted);
            y += 20.f;
        }
}

// ============================================================ 1.37.0 / 1.38.0 : un tableau et son apercu ==
FeatureTablePanel::FeatureTablePanel(std::string id, std::string tableId) : ui::Widget(std::move(id)) {
    table_ = &static_cast<ui::TableView&>(addChild(std::make_unique<ui::TableView>(std::move(tableId))));
    table_->setSelectionMode(ui::SelectionMode::Single);
    pane_ = &static_cast<FeaturePreviewPane&>(addChild(std::make_unique<FeaturePreviewPane>(this->id() + ".apercu")));
    links_ += table_->selectionChanged->connect([this](const std::vector<ui::RowIndex>&) { choose(); });
}

void FeatureTablePanel::setRows(std::vector<RowInfo> rows) {
    rows_ = std::move(rows);
    choose();
}

void FeatureTablePanel::choose() {
    const auto sel = table_->selectedModelRows();
    RowInfo info;
    if (!sel.empty() && sel.front() < rows_.size()) info = rows_[sel.front()];
    pane_->show(info);
}

void FeatureTablePanel::onLayout() {
    const auto b = bounds();
    if (vertical_) {
        const float th = std::clamp(std::floor(b.h * 0.34f), 96.f, 220.f);
        table_->setBounds({b.x, b.y, b.w, th});
        pane_->setBounds({b.x, b.y + th + 8.f, b.w, std::max(60.f, b.h - th - 8.f)});
        return;
    }
    const float pw = std::clamp(std::floor(b.w * 0.42f), 280.f, 520.f);
    table_->setBounds({b.x, b.y, std::max(200.f, b.w - pw - 12.f), b.h});
    pane_->setBounds({b.right() - pw, b.y, pw, b.h});
}

// ============================================================ 1.38.0 : la capture en grand ==
// 1.45.0 : le corps est licence::CaptureZoomBody (LicenceViews) - les fleches, << Capture 2/3 : sa legende >>, les bulles.
class ZoomWindow final : public menu::WidgetMenu {
public:
    ZoomWindow(licence::CaptureSet set, std::size_t shot)
        : menu::WidgetMenu("dialog.capture"), title_(set.title), set_(std::move(set)), shot_(shot) {}
    [[nodiscard]] menu::MenuTraits traits() const override { return dialogTraits(); }
    [[nodiscard]] std::string title() const override { return body_ ? body_->title() : title_; }   // 1.46.0 : la fonctionnalite montree
    ui::EventResult HandleEvent(const ui::InputEvent& ev) override {
        if (const auto* k = std::get_if<ui::KeyDown>(&ev)) {
            // la liste des fonctionnalites ouverte : ses touches d'abord (Echap la ferme, les fleches s'y deplacent)
            if (body_ && body_->featureList() && body_->featureList()->isOpen()) return menu::WidgetMenu::HandleEvent(ev);
            if (k->key == ui::Key::Escape) {
                fermer();
                return ui::EventResult::Consumed;
            }
            // 1.45.0 : les fleches du clavier passent d'une capture a l'autre
            if (body_ && (k->key == ui::Key::Left || k->key == ui::Key::Right) && !k->mods.alt && body_->shotCount() > 1) {
                body_->step(k->key == ui::Key::Left ? -1 : 1);
                return ui::EventResult::Consumed;
            }
            // 1.46.0 : Page precedente / Page suivante : une autre fonctionnalite (celles de la liste, les barrees comprises)
            if (body_ && (k->key == ui::Key::PageUp || k->key == ui::Key::PageDown) && body_->featureCount() > 1) {
                body_->stepFeature(k->key == ui::Key::PageUp ? -1 : 1);
                return ui::EventResult::Consumed;
            }
        }
        return menu::WidgetMenu::HandleEvent(ev);
    }
    void fermer() {
        if (done_) return;
        done_ = true;
        manager().CloseDialog(menu::DialogResult{menu::DialogResult::Button::Cancel, {}});
    }

protected:
    core::Status buildUi() override {
        auto body = std::make_unique<licence::CaptureZoomBody>(std::move(set_), shot_, [this] { fermer(); });
        body_ = body.get();
        if (!choices_.empty()) body_->setChoices(std::move(choices_));   // 1.46.0 : changer de fonctionnalite
        setRoot(std::move(body));
        return core::ok();
    }

public:
    std::vector<licence::CaptureChoice> choices_;   // 1.46.0 : les fonctionnalites qu'on peut y choisir

private:
    std::string               title_;
    licence::CaptureSet       set_;
    std::size_t               shot_{0};
    licence::CaptureZoomBody* body_{nullptr};
    bool                      done_{false};
};

void showCapture(menu::MenuManager& menus, const FeatureInfo& f, const licence::FeaturePreview& preview, std::size_t shot,
                 const std::vector<hmi::Resource>& loaded, std::vector<licence::CaptureChoice> choices) {
    licence::CaptureSet set;
    set.feature = f.id;
    set.title = f.label + (f.product.empty() ? std::string() : " \xE2\x80\x94 " + f.product);
    set.description = preview.description;
    set.shots = preview.shots;
    if (set.shots.empty() && !preview.imagePath.empty()) set.shots.push_back({preview.imagePath, {}, {}});
    set.images = loaded;
    set.locked = f.locked;   // 1.46.0 : une ligne barree - ses captures, et la fenetre le dit
    set.lockedText = f.lockedText;
    auto window = std::make_unique<ZoomWindow>(std::move(set), shot);
    window->choices_ = std::move(choices);
    menus.ShowDialog(std::move(window), [](const menu::DialogResult&) {});
}

// ============================================================ 1.39.0 : une action pas dans la licence ==
// La fenetre du controle (licence::gate) : ce que l'action demande, pourquoi ta licence ne le permet pas, la capture
// de la fonctionnalite (la molette zoome), les offres qui la contiennent ; Voir les offres... ouvre la boutique.
class LockedBody final : public ui::Widget {
public:
    LockedBody(std::string action, std::string why, std::string offers, std::string description, hmi::Resource img, bool hasImage,
               std::function<void()> offersFn, std::function<void()> close, std::string optionLine = {}, std::function<void()> optionFn = {})
        : ui::Widget("dialog.licence-bloquee.corps"), action_(std::move(action)), why_(std::move(why)), offers_(std::move(offers)),
          description_(std::move(description)), optionLine_(std::move(optionLine)), hasImage_(hasImage), offersFn_(std::move(offersFn)),
          close_(std::move(close)), optionFn_(std::move(optionFn)) {
        if (hasImage_) {
            image_ = &static_cast<ZoomImage&>(addChild(std::make_unique<ZoomImage>("dialog.licence-bloquee.capture")));
            image_->setImage(std::move(img));
            image_->setHint(true);
        }
        offersButton_ = &static_cast<ui::Button&>(addChild(std::make_unique<ui::Button>("Voir les offres\xE2\x80\xA6", "dialog.licence-bloquee.offres")));
        offersButton_->setStyle(ui::Button::Style::Primary);
        closeButton_ = &static_cast<ui::Button&>(addChild(std::make_unique<ui::Button>("Fermer", "dialog.licence-bloquee.fermer")));
        links_ += offersButton_->clicked->connect([this] {
            if (offersFn_) offersFn_();
        });
        // 1.42.0 : une option du catalogue l'ouvre - << Voir l'option >> (le chemin le plus court), les offres en second
        if (!optionLine_.empty()) {
            optionButton_ = &static_cast<ui::Button&>(addChild(std::make_unique<ui::Button>("Voir l'option", "dialog.licence-bloquee.option")));
            optionButton_->setStyle(ui::Button::Style::Primary);
            offersButton_->setStyle(ui::Button::Style::Default);
            links_ += optionButton_->clicked->connect([this] {
                if (optionFn_) optionFn_();
            });
        }
        links_ += closeButton_->clicked->connect([this] {
            if (close_) close_();
        });
    }

    // 1.45.0 : les bulles de sa capture (la premiere), dans leur repere
    void setSpots(const licence::CaptureShot& shot) {
        if (image_) image_->setSpots(shot.spots, shot.refW, shot.refH);
    }
    // 1.46.0 : un clic sur la capture : en grand (toutes ses captures ; les autres fonctionnalites a cote)
    void setZoom(std::function<void()> zoom) {
        if (image_ && zoom) image_->onOpen = std::move(zoom);
    }

protected:
    void onLayout() override {
        const auto r = bounds();
        const float w = std::min(820.f, std::max(460.f, r.w - 40.f));
        const float h = std::min(hasImage_ ? 640.f : 360.f, std::max(300.f, r.h - 40.f));
        panel_ = ui::dialogframe::place(*this, {std::floor(r.x + (r.w - w) * 0.5f), std::floor(r.y + (r.h - h) * 0.5f), w, h});
        closeBox_ = {panel_.right() - 36.f, panel_.y + 9.f, 24.f, 24.f};
        ui::dialogframe::keep(*this, closeBox_);
        const float bw = std::max(150.f, offersButton_->sizeHint().preferred.w), cw = std::max(100.f, closeButton_->sizeHint().preferred.w);
        float right = panel_.right() - 16.f;
        if (optionButton_) {
            const float ow = std::max(130.f, optionButton_->sizeHint().preferred.w);
            optionButton_->setBounds({right - ow, panel_.bottom() - 46.f, ow, 32.f});
            right -= ow + 8.f;
        }
        offersButton_->setBounds({right - bw, panel_.bottom() - 46.f, bw, 32.f});
        closeButton_->setBounds({offersButton_->bounds().x - 8.f - cw, panel_.bottom() - 46.f, cw, 32.f});
        // le texte en haut (l'action, pourquoi : 4 lignes au plus), la capture au milieu, les offres et la note en bas
        textTop_ = panel_.y + 56.f;
        imageTop_ = textTop_ + 26.f + 4.f * 19.f + 8.f;
        bottomText_ = panel_.bottom() - 58.f - 3.f * 19.f - (description_.empty() || !hasImage_ ? 0.f : 19.f) - (optionLine_.empty() ? 0.f : 19.f);
        if (image_) image_->setBounds({panel_.x + 16.f, imageTop_, panel_.w - 32.f, std::max(60.f, bottomText_ - 8.f - imageTop_)});
    }
    void onPaint(const ui::PaintContext& ctx) override {
        auto& r = ctx.r;
        const auto& c = ctx.theme.color;
        r.fillRect(panel_, c.panelBg);
        r.strokeRect(panel_, c.borderStrong, 1.f);
        r.fillRect({panel_.x, panel_.y, panel_.w, 42.f}, c.headerBg);
        ui::drawIcon(r, ui::Icon::Lock, {panel_.x + 14.f, panel_.y + 13.f, 16.f, 16.f}, c.warning);
        r.drawText({panel_.x + 38.f, panel_.y + (42.f - r.lineHeight(kTitle)) * 0.5f}, fit(r, "Ta licence ne permet pas cette action", kTitle, panel_.w - 90.f),
                   kTitle, c.text);
        r.drawText({closeBox_.x + 7.f, closeBox_.y + 2.f}, "\xC3\x97", kTitle, c.textMuted);
        r.fillRect({panel_.x, panel_.y + 42.f, panel_.w, 1.f}, c.border);
        r.fillRect({panel_.x, panel_.y + 42.f, 4.f, panel_.h - 42.f}, c.warning);
        const float W = panel_.w - 36.f, x = panel_.x + 20.f;
        const std::string head = "\xC2\xAB " + action_ + " \xC2\xBB";
        r.drawText({x, textTop_}, fit(r, head, kValue, W), kValue, c.text);
        r.drawText({x + 1.f, textTop_}, fit(r, head, kValue, W), kValue, c.text);
        float y = textTop_ + 26.f;
        int n = 0;
        for (const auto& l : wrapText(r, why_, kSmall, W)) {
            if (++n > 4) break;
            r.drawText({x, y}, fit(r, l, kSmall, W), kSmall, c.text);
            y += 19.f;
        }
        y = bottomText_;
        if (hasImage_ && !description_.empty()) {
            r.drawText({x, y}, fit(r, description_, kSmall, W), kSmall, c.textMuted);
            y += 19.f;
        }
        if (!offers_.empty()) {
            const std::string lead = "Elle est dans : ";
            r.drawText({x, y}, lead, kSmall, c.text);
            r.drawText({x + 1.f, y}, lead, kSmall, c.text);
            r.drawText({x + r.measure(lead, kSmall).width + 2.f, y}, fit(r, offers_, kSmall, W - r.measure(lead, kSmall).width - 2.f), kSmall, c.text);
            y += 19.f;
        }
        if (!optionLine_.empty()) {   // 1.42.0
            r.drawText({x, y}, fit(r, optionLine_, gfx::semibold(kSmall), W), gfx::semibold(kSmall), ctx.theme.tone(ui::Tone::Accent, c.text));
            y += 19.f;
        }
        for (const auto& l : wrapText(r, "Rien n'est ferm\xC3\xA9 : ton projet reste ouvert ; Enregistrer et Exporter restent permis.", kSmall, W)) {
            r.drawText({x, y}, fit(r, l, kSmall, W), kSmall, c.textMuted);
            y += 19.f;
            break;
        }
    }
    ui::EventResult onEvent(const ui::InputEvent& ev) override {
        if (const auto* d = std::get_if<ui::MouseDown>(&ev); d && closeBox_.contains(d->pos)) {
            if (close_) close_();
            return ui::EventResult::Consumed;
        }
        return ui::EventResult::Ignored;
    }

private:
    std::string           action_, why_, offers_, description_, optionLine_;
    bool                  hasImage_{false};
    std::function<void()> offersFn_, close_, optionFn_;
    ZoomImage*            image_{nullptr};
    ui::Button*           optionButton_{nullptr};   // 1.42.0
    ui::Button*           offersButton_{nullptr};
    ui::Button*           closeButton_{nullptr};
    gfx::Rect             panel_{}, closeBox_{};
    float                 textTop_{0}, imageTop_{0}, bottomText_{0};
    core::ConnectionScope links_;
};

class LockedWindow final : public menu::WidgetMenu {
public:
    LockedWindow(App& app, std::string action, std::string why, std::string offers, std::string description, hmi::Resource img, bool hasImage,
                 std::string optionLine = {})
        : menu::WidgetMenu("dialog.licence-bloquee"), app_(app), action_(std::move(action)), why_(std::move(why)), offers_(std::move(offers)),
          description_(std::move(description)), optionLine_(std::move(optionLine)), img_(std::move(img)), hasImage_(hasImage) {}
    [[nodiscard]] menu::MenuTraits traits() const override { return dialogTraits(); }
    [[nodiscard]] std::string title() const override { return "Ta licence ne permet pas cette action"; }
    ui::EventResult HandleEvent(const ui::InputEvent& ev) override {
        if (const auto* k = std::get_if<ui::KeyDown>(&ev); k && k->key == ui::Key::Escape) {
            fermer(false);
            return ui::EventResult::Consumed;
        }
        return menu::WidgetMenu::HandleEvent(ev);
    }
    void fermer(bool offers, bool option = false) {
        if (done_) return;
        done_ = true;
        manager().CloseDialog(menu::DialogResult{offers || option ? menu::DialogResult::Button::Ok : menu::DialogResult::Button::Cancel,
                                                 option ? std::string("option") : std::string()});
    }

protected:
    core::Status buildUi() override {
        auto body = std::make_unique<LockedBody>(action_, why_, offers_, description_, std::move(img_), hasImage_, [this] { fermer(true); },
                                                 [this] { fermer(false); }, optionLine_, [this] { fermer(false, true); });
        body->setSpots(shot_);   // 1.45.0
        if (!feature_.empty())   // 1.46.0 : la capture en grand, par-dessus
            body->setZoom([this] { (void)licence::showFeatureCapture(app_, feature_, 0); });
        setRoot(std::move(body));
        return core::ok();
    }

public:
    licence::CaptureShot shot_;   // 1.45.0 : la capture montree (ses bulles)
    std::string          feature_;   // 1.46.0 : sa fonctionnalite (la capture en grand)

private:
    App&          app_;
    std::string   action_, why_, offers_, description_, optionLine_;
    hmi::Resource img_;
    bool          hasImage_{false};
    bool          done_{false};
};

} // namespace

namespace licence {
namespace {
std::string g_wantedOption;   // 1.42.0 : l'option a montrer a l'ouverture de l'onglet Options (fil de l'interface)
std::string g_wantedOffer;    // 1.44.0 : l'offre a choisir a l'ouverture de Demander ma licence
} // namespace
void setWantedOption(std::string optionId) { g_wantedOption = std::move(optionId); }
std::string takeWantedOption() { return std::exchange(g_wantedOption, std::string()); }
void setWantedOffer(std::string edition) { g_wantedOffer = std::move(edition); }
bool captureSpotCenter(const ui::Widget& w, std::size_t n, float& x, float& y) {
    const auto* z = dynamic_cast<const ZoomImage*>(&w);
    const auto p = z ? z->spotCenter(n) : std::nullopt;
    if (!p) return false;
    x = p->x;
    y = p->y;
    return true;
}
std::string takeWantedOffer() { return std::exchange(g_wantedOffer, std::string()); }

void showLocked(App& app, std::string_view feature, std::string actionLabel) {
    const std::string id(feature);
    const std::string name = featureLabel(id);
    std::string product;
    std::string offers;
    if (const auto reg = licensing::getRegistry()) {
        for (const auto& f : reg->features)
            if (f.id == id) product = f.product;
        for (const auto& e : reg->editions) {
            if (e.retired || e.id == reg->unlicensedEdition || std::find(e.features.begin(), e.features.end(), id) == e.features.end()) continue;
            const std::string price = priceLine(e);
            offers += (offers.empty() ? "" : "   \xC2\xB7   ") + (e.label.empty() ? e.id : e.label) + (price.empty() ? std::string() : " (" + price + ")");
        }
    }
    const auto st = licensing::getLicenseState();
    const std::string edition = !st.licensedEdition.empty() ? st.licensedEdition : st.editionLabel.empty() ? st.edition : st.editionLabel;
    std::string why = "Elle demande la fonctionnalit\xC3\xA9 \xC2\xAB " + name + " \xC2\xBB" + (product.empty() ? std::string() : " (" + product + ")");
    if (st.status == licensing::LicenseStatus::Evaluation)
        why += " : la D\xC3\xA9" "couverte ne la contient pas. Demande une licence qui la contient.";
    else if (licensing::licenseFeaturesApply(st.status) && !edition.empty())
        why += " : ta licence " + edition + (st.licenseId.empty() ? std::string() : " (" + st.licenseId + ")") + " ne la contient pas.";
    else
        why += " : ce PC n'a pas de licence valable" + (st.message.empty() ? std::string(".") : std::string(" (") + st.message + ").");
    // 1.42.0 : une option du catalogue qui l'ouvre, proposee a l'offre de cette licence (en cours)
    std::string optionLine, optionId;
    if (const auto reg = licensing::getRegistry();
        reg && !st.licenseId.empty() && (st.status == licensing::LicenseStatus::Valid || st.status == licensing::LicenseStatus::Grace))
        for (const auto& o : reg->options) {
            if (!o.shop || !o.offeredTo(st.licensedEdition) || std::find(o.features.begin(), o.features.end(), id) == o.features.end()) continue;
            optionId = o.id;
            optionLine = "Option " + (o.label.empty() ? o.id : o.label);
            for (const auto& d : o.durations)
                if (!d.second.empty()) {
                    optionLine += " \xC2\xB7 d\xC3\xA8s " + d.second + " " + optionPer(d.first);
                    break;
                }
            optionLine += " : ajoute-la \xC3\xA0 ta licence pour un temps.";
            break;
        }
    const auto pv = featurePreview(id);
    const bool hasImage = !pv.imagePath.empty();
    hmi::Resource img = hasImage ? capturePng(pv.imagePath, id + ".png") : hmi::Resource{};
    auto window = std::make_unique<LockedWindow>(app, actionLabel.empty() ? name : std::move(actionLabel), std::move(why), std::move(offers),
                                                 pv.description, std::move(img), hasImage, optionLine);
    if (!pv.shots.empty()) window->shot_ = pv.shots.front();   // 1.45.0 : ses bulles
    if (!pv.shots.empty()) window->feature_ = id;               // 1.46.0 : un clic, en grand
    app.menus().ShowDialog(std::move(window),
                           [&app, optionId](const menu::DialogResult& r) {
                               if (!r.accepted()) return;
                               if (r.payload == "option") {
                                   setWantedOption(optionId);
                                   showLicenceWindow(app, kTabOptions);
                               } else {
                                   showLicenceWindow(app, kTabRequest);
                               }
                           });
}
} // namespace licence

namespace {

// ============================================================ 1.39.0 : l'offre modifiee, avant / apres ==
// La fenetre << Voir le detail >> d'une notification << Offre modifiee >> : toute l'offre, fonctionnalite par
// fonctionnalite (avant, apres ; les changements d'abord, en couleur), le prix, les PC, la duree qui changent ; un clic
// sur une ligne montre sa capture (la molette zoome, un clic l'ouvre en grand). Les changements seuls, ou toute l'offre.
class OfferChangeBody final : public ui::Widget {
public:
    OfferChangeBody(App& app, licence::watch::OfferChange ch, std::function<void()> compare, std::function<void()> close)
        : ui::Widget("dialog.offre-modifiee.corps"), app_(app), ch_(std::move(ch)), compare_(std::move(compare)), close_(std::move(close)) {
        for (const auto& r : ch_.rows) changes_ += r.before != r.after ? 1 : 0;
        onlyChanges_ = changes_ > 0;
        changesButton_ = &static_cast<ui::Button&>(addChild(std::make_unique<ui::Button>(
            "Les changements (" + std::to_string(changes_) + ")", "dialog.offre-modifiee.changements")));
        allButton_ = &static_cast<ui::Button&>(addChild(std::make_unique<ui::Button>(
            "Toute l'offre (" + std::to_string(ch_.rows.size()) + ")", "dialog.offre-modifiee.tout")));
        panel_ = &static_cast<FeatureTablePanel&>(addChild(std::make_unique<FeatureTablePanel>("dialog.offre-modifiee.panneau", "dialog.offre-modifiee.table")));
        panel_->pane().onEnlarge = [this](const FeatureInfo& f, const licence::FeaturePreview& p, std::size_t shot,
                                          const std::vector<hmi::Resource>& loaded) {
            showCapture(app_.menus(), f, p, shot, loaded, panel_->choices());   // 1.46.0 : les autres lignes a cote
        };
        compareButton_ = &static_cast<ui::Button&>(addChild(std::make_unique<ui::Button>("Comparer toutes les offres\xE2\x80\xA6", "dialog.offre-modifiee.comparer")));
        closeButton_ = &static_cast<ui::Button&>(addChild(std::make_unique<ui::Button>("Fermer", "dialog.offre-modifiee.fermer")));
        closeButton_->setStyle(ui::Button::Style::Primary);
        links_ += changesButton_->clicked->connect([this] { setFilter(true); });
        links_ += allButton_->clicked->connect([this] { setFilter(false); });
        links_ += compareButton_->clicked->connect([this] {
            if (compare_) compare_();
        });
        links_ += closeButton_->clicked->connect([this] {
            if (close_) close_();
        });
        setFilter(onlyChanges_);
    }
    // Les essais : ce que montre la liste.
    [[nodiscard]] std::size_t shownRows() const noexcept { return model_ ? model_->rowCount() : 0; }
    [[nodiscard]] FeatureTablePanel& panel() noexcept { return *panel_; }

    void setFilter(bool onlyChanges) {
        onlyChanges_ = onlyChanges && changes_ > 0;
        changesButton_->setStyle(onlyChanges_ ? ui::Button::Style::Primary : ui::Button::Style::Default);
        allButton_->setStyle(onlyChanges_ ? ui::Button::Style::Default : ui::Button::Style::Primary);
        changesButton_->setEnabled(changes_ > 0);
        std::vector<std::vector<std::string>> rows;
        std::vector<FeatureInfo> infos;
        std::vector<int> tone;   // -1 retiree, 1 ajoutee, 0 inchangee
        const std::string yes = "\xE2\x9C\x93", no = "\xE2\x80\x94";
        for (const auto& r : ch_.rows) {
            const bool changed = r.before != r.after;
            if (onlyChanges_ && !changed) continue;
            const std::string what = !changed ? std::string("inchang\xC3\xA9" "e") : r.after ? std::string("\xE2\x9C\x93 ajout\xC3\xA9" "e") : std::string("\xE2\x9C\x97 retir\xC3\xA9" "e");
            rows.push_back({r.label, r.product, r.before ? yes : no, r.after ? yes : no, what});
            // 1.46.0 : retiree de l'offre - barree en grand aussi, ses captures quand meme
            infos.push_back({r.id, r.label, r.product, {}, !r.after, r.after ? std::string() : std::string("retir\xC3\xA9" "e de l'offre")});
            tone.push_back(!changed ? 0 : r.after ? 1 : -1);
        }
        model_ = std::make_shared<Rows>(std::vector<std::string>{"Fonctionnalit\xC3\xA9", "Logiciel", "Avant", "Apr\xC3\xA8s", "Changement"}, std::move(rows),
                                        [tone](ui::RowIndex r, std::size_t c) {
                                            ui::CellStyle s;
                                            if (r < tone.size() && tone[r] != 0) {
                                                if (c == 0 || c == 4) s.bold = true;
                                                if (c >= 3) s.fgTone = tone[r] > 0 ? ui::Tone::Ok : ui::Tone::Error;
                                            }
                                            return s;
                                        });
        panel_->table().setColumns({{"Fonctionnalit\xC3\xA9", 210.f}, {"Logiciel", 106.f}, {"Avant", 82.f}, {"Apr\xC3\xA8s", 82.f}, {"Changement", 116.f}});
        panel_->table().setModel(model_);
        panel_->setRows(std::move(infos));
        if (model_->rowCount() > 0) panel_->table().selectModelRows({0});
        invalidate();
    }

protected:
    void onLayout() override {
        const auto r = bounds();
        const float w = std::min(1180.f, std::max(560.f, r.w - 40.f)), h = std::min(760.f, std::max(400.f, r.h - 40.f));
        frame_ = ui::dialogframe::place(*this, {std::floor(r.x + (r.w - w) * 0.5f), std::floor(r.y + (r.h - h) * 0.5f), w, h});
        closeBox_ = {frame_.right() - 36.f, frame_.y + 9.f, 24.f, 24.f};
        ui::dialogframe::keep(*this, closeBox_);
        const float lines = 1.f + (ch_.values.empty() ? 0.f : 1.f);
        const float chipsY = frame_.y + 52.f + lines * 20.f + 6.f;
        const float c1 = std::max(150.f, changesButton_->sizeHint().preferred.w), c2 = std::max(130.f, allButton_->sizeHint().preferred.w);
        changesButton_->setBounds({frame_.x + 16.f, chipsY, c1, 28.f});
        allButton_->setBounds({frame_.x + 16.f + c1 + 6.f, chipsY, c2, 28.f});
        const float top = chipsY + 36.f;
        panel_->setBounds({frame_.x + 16.f, top, frame_.w - 32.f, std::max(120.f, frame_.bottom() - 56.f - top)});
        const float cw = std::max(110.f, closeButton_->sizeHint().preferred.w), pw = std::max(200.f, compareButton_->sizeHint().preferred.w);
        closeButton_->setBounds({frame_.right() - 16.f - cw, frame_.bottom() - 44.f, cw, 32.f});
        compareButton_->setBounds({frame_.x + 16.f, frame_.bottom() - 44.f, pw, 32.f});
    }
    void onPaint(const ui::PaintContext& ctx) override {
        auto& r = ctx.r;
        const auto& c = ctx.theme.color;
        r.fillRect(frame_, c.panelBg);
        r.strokeRect(frame_, c.borderStrong, 1.f);
        r.fillRect({frame_.x, frame_.y, frame_.w, 42.f}, c.headerBg);
        const std::string title = "Offre \xC2\xAB " + ch_.label + " \xC2\xBB : avant / apr\xC3\xA8s" + (ch_.mine ? " (ta licence)" : "");
        r.drawText({frame_.x + 16.f, frame_.y + (42.f - r.lineHeight(kTitle)) * 0.5f}, fit(r, title, kTitle, frame_.w - 70.f), kTitle, c.text);
        r.drawText({closeBox_.x + 7.f, closeBox_.y + 2.f}, "\xC3\x97", kTitle, c.textMuted);
        r.fillRect({frame_.x, frame_.y + 42.f, frame_.w, 1.f}, c.border);
        int added = 0, removed = 0;
        for (const auto& x : ch_.rows) {
            if (!x.before && x.after) ++added;
            if (x.before && !x.after) --removed;
        }
        std::string sum = std::to_string(-removed) + (removed < -1 ? " fonctionnalit\xC3\xA9s retir\xC3\xA9" "es" : " fonctionnalit\xC3\xA9 retir\xC3\xA9" "e") + ", " +
                          std::to_string(added) + (added > 1 ? " ajout\xC3\xA9" "es" : " ajout\xC3\xA9" "e");
        if (ch_.registry) sum += " \xC2\xB7 registre n\xC2\xB0 " + std::to_string(ch_.registry);
        if (ch_.mine) sum += " \xC2\xB7 ta licence garde ce qu'elle contient jusqu'\xC3\xA0 son renouvellement";
        float y = frame_.y + 52.f;
        r.drawText({frame_.x + 16.f, y}, fit(r, sum, kSmall, frame_.w - 32.f), kSmall, c.text);
        y += 20.f;
        if (!ch_.values.empty()) {
            std::string v;
            for (const auto& x : ch_.values) v += (v.empty() ? "" : "   \xC2\xB7   ") + x.label + " : " + x.before + " \xE2\x86\x92 " + x.after;
            r.drawText({frame_.x + 16.f, y}, fit(r, v, kSmall, frame_.w - 32.f), kSmall, c.text);
            r.drawText({frame_.x + 17.f, y}, fit(r, v, kSmall, frame_.w - 32.f), kSmall, c.text);
        }
    }
    ui::EventResult onEvent(const ui::InputEvent& ev) override {
        if (const auto* d = std::get_if<ui::MouseDown>(&ev); d && closeBox_.contains(d->pos)) {
            if (close_) close_();
            return ui::EventResult::Consumed;
        }
        return ui::EventResult::Ignored;
    }

private:
    App&                  app_;
    licence::watch::OfferChange  ch_;
    std::function<void()> compare_, close_;
    int                   changes_{0};
    bool                  onlyChanges_{true};
    ui::Button*           changesButton_{nullptr};
    ui::Button*           allButton_{nullptr};
    FeatureTablePanel*    panel_{nullptr};
    ui::Button*           compareButton_{nullptr};
    ui::Button*           closeButton_{nullptr};
    std::shared_ptr<Rows> model_;
    gfx::Rect             frame_{}, closeBox_{};
    core::ConnectionScope links_;
};

class OfferChangeWindow final : public menu::WidgetMenu {
public:
    OfferChangeWindow(App& app, licence::watch::OfferChange ch) : menu::WidgetMenu("dialog.offre-modifiee"), app_(app), ch_(std::move(ch)) {}
    [[nodiscard]] menu::MenuTraits traits() const override { return dialogTraits(); }
    [[nodiscard]] std::string title() const override { return "Offre " + ch_.label + " : avant / apr\xC3\xA8s"; }
    ui::EventResult HandleEvent(const ui::InputEvent& ev) override {
        if (const auto* k = std::get_if<ui::KeyDown>(&ev); k && k->key == ui::Key::Escape) {
            fermer(false);
            return ui::EventResult::Consumed;
        }
        return menu::WidgetMenu::HandleEvent(ev);
    }
    void fermer(bool compare) {
        if (done_) return;
        done_ = true;
        manager().CloseDialog(menu::DialogResult{compare ? menu::DialogResult::Button::Ok : menu::DialogResult::Button::Cancel, {}});
    }

protected:
    core::Status buildUi() override {
        setRoot(std::make_unique<OfferChangeBody>(app_, ch_, [this] { fermer(true); }, [this] { fermer(false); }));
        return core::ok();
    }

private:
    App&                 app_;
    licence::watch::OfferChange ch_;
    bool                 done_{false};
};

} // namespace

namespace licence {
bool showOfferChange(App& app, const std::string& key) {
    for (const auto& n : notices::list()) {
        if (n.key != key) continue;
        auto ch = watch::offerChangeOf(n.data);
        if (!ch) return false;
        (void)notices::markRead(n.id);
        app.menus().ShowDialog(std::make_unique<OfferChangeWindow>(app, std::move(*ch)), [&app](const menu::DialogResult& r) {
            if (r.accepted()) showLicenceWindow(app, kTabEditions);
        });
        return true;
    }
    return false;
}
} // namespace licence

namespace {

// ============================================================ 1.38.0 : la fenetre Notifications ==
class NoticesBody final : public ui::Widget {
public:
    NoticesBody(std::function<void(int)> openLicence, std::function<bool(const std::string&)> openDetail, std::function<void()> close)
        : ui::Widget("dialog.notifications.corps"), openLicence_(std::move(openLicence)), openDetail_(std::move(openDetail)), close_(std::move(close)) {
        ai::AiNotificationsView::Callbacks cb;
        cb.follow = [this](const std::string& action) {
            const auto t = notices::parseAction(action);
            if (t.kind == "licence" && openLicence_) {
                openLicence_(t.tab);
                return true;
            }
            if (t.kind == "detail" && openDetail_) return openDetail_(t.key);   // 1.39.0 : l'offre modifiee, avant / apres
            // 1.44.0 : un message a ce PC (ses fichiers), une mise a jour - leur fenetre, par-dessus celle-ci
            if ((action.rfind("fichiers:", 0) == 0 || action.rfind("maj:", 0) == 0) && openDetail_) return openDetail_(action);
            return false;
        };
        list_ = &static_cast<ai::AiNotificationsView&>(addChild(std::make_unique<ai::AiNotificationsView>("dialog.notifications.liste", cb)));
        ai::AiNotificationsView::Source src{&notices::revision, &notices::list, &notices::markRead, &notices::remove, &notices::markAllRead,
                                            &notices::clearRead, &updates::filesLine};   // 1.44.0 : << Avec fichiers >>
        list_->setSource(src);
        closeButton_ = &static_cast<ui::Button&>(addChild(std::make_unique<ui::Button>("Fermer", "dialog.notifications.fermer")));
        closeButton_->setStyle(ui::Button::Style::Primary);
        links_ += closeButton_->clicked->connect([this] {
            if (close_) close_();
        });
    }
    [[nodiscard]] ai::AiNotificationsView& list() noexcept { return *list_; }
    void poll() { (void)list_->poll(); }

protected:
    void onLayout() override {
        const auto r = bounds();
        const float w = std::min(820.f, std::max(420.f, r.w - 40.f)), h = std::min(720.f, std::max(320.f, r.h - 40.f));
        panel_ = ui::dialogframe::place(*this, {std::floor(r.x + (r.w - w) * 0.5f), std::floor(r.y + (r.h - h) * 0.5f), w, h});
        closeBox_ = {panel_.right() - 36.f, panel_.y + 9.f, 24.f, 24.f};
        ui::dialogframe::keep(*this, closeBox_);
        list_->setBounds({panel_.x + 12.f, panel_.y + 50.f + 22.f, panel_.w - 24.f, std::max(80.f, panel_.h - 50.f - 22.f - 56.f)});
        const float cw = std::max(110.f, closeButton_->sizeHint().preferred.w);
        closeButton_->setBounds({panel_.right() - 16.f - cw, panel_.bottom() - 44.f, cw, 32.f});
    }
    void onPaint(const ui::PaintContext& ctx) override {
        auto& r = ctx.r;
        const auto& c = ctx.theme.color;
        r.fillRect(panel_, c.panelBg);
        r.strokeRect(panel_, c.borderStrong, 1.f);
        r.fillRect({panel_.x, panel_.y, panel_.w, 42.f}, c.headerBg);
        ai::AiNotificationsView::drawBell(r, {panel_.x + 14.f, panel_.y + 11.f, 20.f, 20.f}, c.text);
        r.drawText({panel_.x + 42.f, panel_.y + (42.f - r.lineHeight(kTitle)) * 0.5f}, "Notifications", kTitle, c.text);
        r.drawText({closeBox_.x + 7.f, closeBox_.y + 2.f}, "\xC3\x97", kTitle, c.textMuted);
        r.fillRect({panel_.x, panel_.y + 42.f, panel_.w, 1.f}, c.border);
        r.drawText({panel_.x + 14.f, panel_.y + 50.f},
                   fit(r, "Ta licence, les offres de l'\xC3\xA9" "diteur, ses annonces, ses messages \xC3\xA0 ce PC et les mises \xC3\xA0 jour - ici seulement, jamais par mail.",
                       kSmall, panel_.w - 28.f), kSmall,
                   c.textMuted);
    }
    ui::EventResult onEvent(const ui::InputEvent& ev) override {
        if (const auto* d = std::get_if<ui::MouseDown>(&ev); d && closeBox_.contains(d->pos)) {
            if (close_) close_();
            return ui::EventResult::Consumed;
        }
        return ui::EventResult::Ignored;
    }

private:
    std::function<void(int)>   openLicence_;
    std::function<bool(const std::string&)> openDetail_;   // 1.39.0
    std::function<void()>      close_;
    ai::AiNotificationsView*   list_{nullptr};
    ui::Button*                closeButton_{nullptr};
    gfx::Rect                  panel_{}, closeBox_{};
    core::ConnectionScope      links_;
};

class NoticesWindow final : public menu::WidgetMenu {
public:
    NoticesWindow(App& app, std::shared_ptr<int> then) : menu::WidgetMenu("dialog.notifications"), app_(app), then_(std::move(then)) {}
    [[nodiscard]] menu::MenuTraits traits() const override { return dialogTraits(); }
    [[nodiscard]] std::string title() const override { return "Notifications"; }
    ui::EventResult HandleEvent(const ui::InputEvent& ev) override {
        if (const auto* k = std::get_if<ui::KeyDown>(&ev); k && k->key == ui::Key::Escape) {
            fermer();
            return ui::EventResult::Consumed;
        }
        return menu::WidgetMenu::HandleEvent(ev);
    }
    void Update(const menu::FrameContext& f) override {
        menu::WidgetMenu::Update(f);
        if (body_) body_->poll();   // une notification arrivee pendant qu'on regarde
    }
    void fermer() {
        if (done_) return;
        done_ = true;
        manager().CloseDialog(menu::DialogResult{menu::DialogResult::Button::Cancel, {}});
    }

protected:
    core::Status buildUi() override {
        auto body = std::make_unique<NoticesBody>(
            [this](int tab) {
                *then_ = tab < 0 ? licence::kTabAuto : tab;   // la fenetre Licence, une fois celle-ci fermee
                fermer();
            },
            [this](const std::string& key) {
                // 1.44.0 : un message (ses fichiers), une mise a jour
                if (key.rfind("fichiers:", 0) == 0 || key.rfind("maj:", 0) == 0) return licence::openNoticeAction(app_, key);
                return licence::showOfferChange(app_, key);   // 1.39.0 : par-dessus celle-ci
            },
            [this] { fermer(); });
        body_ = body.get();
        if (withFiles_) body_->list().setFilter(ai::AiNotificationsView::Filter::WithFiles);   // 1.44.0
        setRoot(std::move(body));
        return core::ok();
    }

public:
    bool withFiles_{false};

private:
    App&                 app_;
    std::shared_ptr<int> then_;
    NoticesBody*         body_{nullptr};
    bool                 done_{false};
};

} // namespace

void showNoticesWindow(App& app) { showNoticesWindow(app, false); }

void showNoticesWindow(App& app, bool withFiles) {
    auto then = std::make_shared<int>(-1000);
    auto w = std::make_unique<NoticesWindow>(app, then);
    w->withFiles_ = withFiles;   // 1.44.0 : ouverte sur << Avec fichiers >>
    app.menus().ShowDialog(std::move(w), [&app, then](const menu::DialogResult&) {
        if (*then != -1000) showLicenceWindow(app, *then);
    });
}

void showLicenceWindow(App& app, int tab) { showLicenceWindow(app.menus(), tab); }

void showLicenceWindow(menu::MenuManager& menus, int tab) {
    // 1.46.0 : Mon profil n'est plus une rubrique - sa fenetre (les appels d'avant : l'accueil, Aide, << Verifier mon adresse >>)
    if (tab == licence::kTabProfile) {
        showProfileWindow(menus);
        return;
    }
    // 1.45.0 : UNE SEULE FENETRE LICENCE. Deja ouverte (sous la reponse de l'editeur, << Voir ma licence >>) : elle passe a
    // l'onglet demande ; avant, une seconde s'empilait sur la premiere - deux clics sur la croix pour en sortir.
    if (g_licenceWindow && !g_licenceWindow->closing()) {
        g_licenceWindow->goTo(tab);
        return;
    }
    menus.ShowDialog(std::make_unique<LicenceWindow>(menus, tab), nullptr);
}

bool licenceWindowOpen() noexcept { return g_licenceWindow && !g_licenceWindow->closing(); }

int licenceWindowSection() noexcept { return licenceWindowOpen() ? g_licenceWindow->section() : licence::kTabAuto; }

void showProfileWindow(App& app) { showProfileWindow(app.menus()); }

void showProfileWindow(menu::MenuManager& menus) {
    if (g_profileWindow && !g_profileWindow->closing()) return;   // deja ouverte : une seule
    menus.ShowDialog(std::make_unique<ProfileWindow>(), nullptr);
}

bool profileWindowOpen() noexcept { return g_profileWindow && !g_profileWindow->closing(); }

namespace licence {

bool gateProjects(App& app) {
    std::string why;
    if (projectsAllowed(&why)) return true;
    showNotices();   // 1.35.1 : le poste d'exploitation refuse revient a l'accueil : l'encart y dit << Sans licence >>
    // 1.36.0 : la demande en ligne d'abord (ou celle qui attend) ; sans service : le code, ou le fichier.
    std::string how = "Active ta licence avec le code re\xC3\xA7u de l'\xC3\xA9" "diteur (ou demande-la) : Activer ma licence\xE2\x80\xA6";
    std::string button = "Activer ma licence\xE2\x80\xA6";
    int tab = kTabActivation;
    if (onlineRequests()) {
        tab = kTabRequest;
        if (const auto pr = pendingRequest(); pr.pending) {
            how = "Ta demande n\xC2\xB0 " + std::to_string(pr.number) + " (envoy\xC3\xA9" "e le " + service::dateText(pr.sentAt) +
                  ") attend l'\xC3\xA9" "diteur. D\xC3\xA8s qu'il l'accepte, ta licence s'installe d'elle-m\xC3\xAAme et une fen\xC3\xAAtre te le confirme.";
            button = "Voir ma demande";
        } else {
            how = "Demande ta licence \xC3\xA0 l'\xC3\xA9" "diteur : ta soci\xC3\xA9t\xC3\xA9, un contact, et c'est parti. D\xC3\xA8s qu'il l'accepte, ta licence "
                  "s'installe d'elle-m\xC3\xAAme et une fen\xC3\xAAtre te le confirme. Aucun fichier, aucun code \xC3\xA0 taper.";
            button = "Demander ma licence\xE2\x80\xA6";
        }
    }
    auto dlg = std::make_unique<MessageDialog>(
        "Licence n\xC3\xA9" "cessaire",
        "Sans licence, " + core::productName() + " ne s'utilise pas : aucun projet ne s'ouvre ni ne se cr\xC3\xA9" "e.\n\n" + why + "\n\n" + how,
        MessageDialog::Icon::Warning, button);
    dlg->setCancelLabel("Fermer");
    app.menus().ShowDialog(std::move(dlg), [&app, tab](const menu::DialogResult& r) {
        if (r.accepted()) showLicenceWindow(app, tab);
    });
    return false;
}

void showAnswer(App& app, const Answer& a) { showAnswer(app.menus(), a); }

void showAnswer(menu::MenuManager& menus, const Answer& a) {
    const auto v = answerView(a, licensing::getLicenseState());
    auto dlg = std::make_unique<MessageDialog>(v.title, v.text, v.accepted ? MessageDialog::Icon::Info : MessageDialog::Icon::Warning, v.confirm);
    dlg->setCancelLabel(v.accepted ? "Fermer" : "Plus tard");
    dlg->setParagraphWidth(720.f);   // ce qu'elle ouvre, jusqu'a quand : un paragraphe, pas un editeur
    // 1.38.0 : le code montre se copie (le presse-papiers, sur ce clic seulement).
    if (!a.code.empty())
        dlg->setExtraButton("Copier le code", [code = a.code] { ui::setClipboardText(code); }, "Code copi\xC3\xA9 \xE2\x9C\x93");
    const int tab = v.tab;
    // 1.45.0 : une fenetre de reponse a la fois (la suivante attend sa fermeture : takeAnswer) ; << Voir ma licence >>
    // ramene la fenetre Licence deja ouverte sur l'onglet (showLicenceWindow), sans en empiler une seconde.
    setAnswerOpen(true);
    menus.ShowDialog(std::move(dlg), [&menus, tab](const menu::DialogResult& r) {
        setAnswerOpen(false);
        if (r.accepted()) showLicenceWindow(menus, tab);
    });
}

bool showFeatureCapture(App& app, const std::string& feature, std::size_t shot) {
    const auto pv = featurePreview(feature);
    if (pv.shots.empty()) return false;
    std::string product;
    if (const auto reg = licensing::getRegistry())
        for (const auto& f : reg->features)
            if (f.id == feature) product = f.product;
    // 1.46.0 : toutes les fonctionnalites du registre dans sa liste (celles que la licence n'ouvre pas : barrees, avec leurs captures)
    const bool locked = !allows(feature);
    showCapture(app.menus(), FeatureInfo{feature, featureLabel(feature), product, {}, locked, locked ? "pas dans ta licence" : ""}, pv,
                std::min(shot, pv.shots.size() - 1), {}, registryChoices());
    return true;
}

} // namespace licence

} // namespace app
