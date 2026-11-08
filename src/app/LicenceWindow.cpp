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
// =============================================================================
#include "Licence.hpp"

#include "App.hpp"
#include "Dossiers.hpp"
#include "LicenceService.hpp"   // 1.35.0 : le service des licences
#include "Settings.hpp"
#include "hmi/HmiImages.hpp"    // 1.37.0 : la capture d'une fonctionnalite (onglet Editions)
#include "hmi/HmiPaneKit.hpp"
#include "screens/Screens.hpp"

#include "../core/Edition.hpp"
#include "../core/Version.hpp"
#include "../deploy/Crypto.hpp"
#include "../licensing/Licensing.hpp"
#include "../menu/MenuManager.hpp"
#include "../ui/DialogFrame.hpp"
#include "../ui/widgets/Containers.hpp"
#include "../ui/widgets/Controls.hpp"
#include "../ui/widgets/DataViews.hpp"

#include <algorithm>
#include <cmath>
#include <filesystem>
#include <fstream>
#include <initializer_list>
#include <memory>
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
        t.pop_back();
        while (!t.empty() && (static_cast<unsigned char>(t.back()) & 0xC0) == 0x80) t.pop_back();
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

// ---- 1.35.0 : l'onglet Activation (le code, la licence en ligne, la flottante, la Decouverte) ----
class ActivationPanel final : public ui::Widget {
public:
    ActivationPanel(LicenceBody& body, App& app);
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
    App&               app_;
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
class OnlinePanel final : public ui::Widget {
public:
    OnlinePanel(LicenceBody& body, App& app);
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
    ui::InputText* field(const char* id, const std::string& placeholder, std::size_t maxLen);
    // 1.37.0 : la boutique - une carte par edition du registre (son prix, sa description, ce qu'elle donne).
    struct Card {
        std::string id, label, price, description, facts;
        gfx::Rect   box{};
    };
    void buildCards();
    // Ce que contiendra la licence d'une edition (le rappel pendant l'attente) ; vide : inconnue.
    [[nodiscard]] std::string futureText(const std::string& editionId) const;

    LicenceBody&       body_;
    App&               app_;
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
    bool               available_{false};
    std::string        why_;
    licence::service::Record rec_;
    std::string        recProblem_;
    bool               pending_{false};
    licence::service::Job busy_{licence::service::Job::None};
    float              rowsY_{0}, infoY_{0}, footY_{0}, fieldsY_{0};
    core::ConnectionScope links_;
};

// ---- 1.37.0 : l'onglet Mon profil (tout facultatif : il remplit la demande ; le courriel recoit le mail de l'editeur) ----
class ProfilePanel final : public ui::Widget {
public:
    explicit ProfilePanel(LicenceBody& body);
    void refresh();   // relu du disque

protected:
    void onLayout() override;
    void onPaint(const ui::PaintContext& ctx) override;

private:
    void doSave();
    LicenceBody&          body_;
    ui::InputText*        email_{nullptr};
    ui::InputText*        name_{nullptr};
    ui::InputText*        company_{nullptr};
    ui::Button*           save_{nullptr};
    float                 rowsY_{0};
    core::ConnectionScope links_;
};

// ---- 1.37.0 : l'onglet Editions - le tableau, et l'apercu de la fonctionnalite choisie (sa capture, deux lignes) ----
class EditionsPanel final : public ui::Widget {
public:
    EditionsPanel();
    [[nodiscard]] ui::TableView& table() noexcept { return *table_; }
    // Une ligne du tableau : la fonctionnalite (id vide : une ligne de resume), son nom, son logiciel, ses editions.
    struct RowInfo {
        std::string id, label, product, editions;
    };
    void setRows(std::vector<RowInfo> rows);
    // Les essais : ce que montre l'apercu.
    [[nodiscard]] const std::string& shownFeature() const noexcept { return shown_.id; }
    [[nodiscard]] const licence::FeaturePreview& preview() const noexcept { return preview_; }

protected:
    void onLayout() override;
    void onPaint(const ui::PaintContext& ctx) override;

private:
    void choose();
    ui::TableView*          table_{nullptr};
    std::vector<RowInfo>    rows_;
    RowInfo                 shown_;
    licence::FeaturePreview preview_;
    hmi::Resource           image_;
    gfx::Rect               pane_{};
    core::ConnectionScope   links_;
};

class LicenceBody final : public ui::Widget {
public:
    LicenceBody(LicenceWindow& owner, App& app, int tab);
    void reload();
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

    LicenceWindow&    owner_;
    App&              app_;
    ui::TabControl*   tabs_{nullptr};
    ui::PropertyGrid* mine_{nullptr};
    ui::TableView*    devices_{nullptr};
    ui::TableView*    features_{nullptr};
    ui::TableView*    editions_{nullptr};
    EditionsPanel*    editionsPanel_{nullptr};   // 1.37.0 : avec l'apercu
    ui::TableView*    history_{nullptr};
    ui::PropertyGrid* params_{nullptr};
    OnlinePanel*      online_{nullptr};      // 1.34.0
    ActivationPanel*  activation_{nullptr};  // 1.35.0
    ProfilePanel*     profile_{nullptr};     // 1.37.0
    unsigned long long seenRevision_{0};     // 1.34.0 : le dernier travail du service releve
    bool              wasBusy_{false};
    ui::Button*       import_{nullptr};
    ui::Button*       request_{nullptr};
    ui::Button*       folder_{nullptr};
    ui::Button*       close_{nullptr};
    std::shared_ptr<ui::ITableModel> devicesModel_, featuresModel_, editionsModel_, historyModel_;
    licensing::LicenseState state_;
    std::string       flash_;
    int               flashTone_{0};
    gfx::Rect         panel_{}, closeBox_{};
    float             headerY_{0}, messageY_{0};
    core::ConnectionScope links_;
};

class LicenceWindow final : public menu::WidgetMenu {
public:
    LicenceWindow(App& app, int tab) : menu::WidgetMenu("dialog.licence"), app_(app), tab_(tab) {}
    [[nodiscard]] menu::MenuTraits traits() const override { return dialogTraits(); }
    [[nodiscard]] std::string title() const override { return "Licence"; }
    ui::EventResult HandleEvent(const ui::InputEvent& ev) override {
        if (const auto* k = std::get_if<ui::KeyDown>(&ev); k && k->key == ui::Key::Escape) {
            fermer();
            return ui::EventResult::Consumed;
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
        manager().CloseDialog(menu::DialogResult{menu::DialogResult::Button::Cancel, {}});
    }

protected:
    core::Status buildUi() override {
        auto body = std::make_unique<LicenceBody>(*this, app_, tab_);
        body_ = body.get();
        setRoot(std::move(body));
        return core::ok();
    }

private:
    App&         app_;
    int          tab_{0};
    bool         done_{false};
    LicenceBody* body_{nullptr};
};

LicenceBody::LicenceBody(LicenceWindow& owner, App& app, int tab) : ui::Widget("dialog.licence.body"), owner_(owner), app_(app) {
    auto tabs = std::make_unique<ui::TabControl>("dialog.licence.onglets");
    auto mine = std::make_unique<ui::PropertyGrid>("dialog.licence.ma-licence");
    mine_ = mine.get();
    auto devices = std::make_unique<ui::TableView>("dialog.licence.appareils");
    devices->setColumns({{"PC", 260.f}, {"Empreinte de sa cl\xC3\xA9", 260.f}, {"Activ\xC3\xA9 le", 150.f}, {"", 90.f}});
    devices_ = devices.get();
    auto features = std::make_unique<ui::TableView>("dialog.licence.fonctionnalites");
    features->setColumns({{"Fonctionnalit\xC3\xA9", 220.f}, {"Identifiant", 170.f}, {"Logiciel", 110.f}, {"\xC3\x89tat", 150.f},
                          {"Limites", 160.f}, {"Dans le programme", 380.f}});
    features_ = features.get();
    auto editions = std::make_unique<EditionsPanel>();   // 1.37.0 : le tableau et l'apercu de la fonctionnalite choisie
    editionsPanel_ = editions.get();
    editions_ = &editions->table();
    auto history = std::make_unique<ui::TableView>("dialog.licence.historique");
    history->setColumns({{"Quand (UTC)", 150.f}, {"Quoi", 120.f}, {"R\xC3\xA9sultat", 90.f}, {"D\xC3\xA9tail", 620.f}});
    history_ = history.get();
    auto params = std::make_unique<ui::PropertyGrid>("dialog.licence.parametres");
    params_ = params.get();
    auto online = std::make_unique<OnlinePanel>(*this, app);   // 1.34.0
    online_ = online.get();
    auto activation = std::make_unique<ActivationPanel>(*this, app);   // 1.35.0
    activation_ = activation.get();
    auto profile = std::make_unique<ProfilePanel>(*this);   // 1.37.0
    profile_ = profile.get();
    tabs->addTab({"Activation", ui::Icon::Network}, std::move(activation));   // 1.35.0 : licence::kTabActivation
    tabs->addTab({"Ma licence", ui::Icon::Lock}, std::move(mine));
    tabs->addTab({"Mes appareils", ui::Icon::Screen}, std::move(devices));
    tabs->addTab({"Fonctionnalit\xC3\xA9s", ui::Icon::Ok}, std::move(features));
    tabs->addTab({"\xC3\x89" "ditions", ui::Icon::Layers}, std::move(editions));
    tabs->addTab({"Historique", ui::Icon::History}, std::move(history));
    tabs->addTab({"Param\xC3\xA8tres", ui::Icon::Settings}, std::move(params));
    tabs->addTab({"Demander ma licence", ui::Icon::Open}, std::move(online));   // 1.34.0 ; licence::kTabRequest (1.36.0 : son nom)
    tabs->addTab({"Mon profil", ui::Icon::User}, std::move(profile));            // 1.37.0 : licence::kTabProfile
    tabs_ = &static_cast<ui::TabControl&>(addChild(std::move(tabs)));
    import_ = &static_cast<ui::Button&>(addChild(std::make_unique<ui::Button>("Activer par fichier de licence\xE2\x80\xA6", "dialog.licence.importer")));
    import_->setStyle(ui::Button::Style::Primary);
    request_ = &static_cast<ui::Button&>(addChild(std::make_unique<ui::Button>("Demander une activation\xE2\x80\xA6", "dialog.licence.demander")));
    // 1.36.0 : avec le service des licences, la demande en ligne est le chemin principal (onglet Demander ma licence) ;
    // les fichiers restent pour un PC sans Internet - en second.
    if (licence::onlineRequests()) {
        import_->setText("Fichier de licence\xE2\x80\xA6");
        import_->setStyle(ui::Button::Style::Default);
        import_->setTooltip("Un fichier .xpglic re\xC3\xA7u de l'\xC3\xA9" "diteur (un PC sans Internet). Avec Internet, demande plut\xC3\xB4t ta licence "
                            "(onglet Demander ma licence) : elle s'installe d'elle-m\xC3\xAAme.");
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
    if (tab < 0) {   // licence::kTabAuto : Ma licence pour une licence par fichier valable, sinon Activation
        using S = licensing::LicenseStatus;
        const bool valid = state_.status == S::Valid || state_.status == S::Grace;
        const bool fileLicence = !state_.online && valid;
        tab = fileLicence ? licence::kTabMine : licence::kTabActivation;
        // 1.36.0 : sans licence valable (ni bail a renouveler, ni poste flottant a attendre), la demande en ligne
        if (!valid && state_.status != S::LeaseExpired && state_.status != S::NoFloatingSeat && state_.status != S::NotConfigured &&
            licence::onlineRequests())
            tab = licence::kTabRequest;
    }
    tabs_->setCurrentIndex(static_cast<std::size_t>(std::clamp(tab, 0, licence::kTabProfile)));
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
                a.properties.push_back(prop("Avec un code", "l'onglet Activation (le code XPG-XXXX-XXXX-XXXX-XXXX re\xC3\xA7u de l'\xC3\xA9" "diteur)",
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
                a.properties.push_back(prop("Sans code", "l'onglet Demande \xC3\xA0 l'\xC3\xA9" "diteur (par le service des licences)",
                                            "La demande part chez l'\xC3\xA9" "diteur, scell\xC3\xA9" "e pour lui ; sa r\xC3\xA9ponse revient ici : une licence "
                                            "accept\xC3\xA9" "e s'installe d'elle-m\xC3\xAA" "me, sans fichier."));
            else a.properties.push_back(prop("Sans code", "pas dans ce programme", why));
        }
        cats.push_back(std::move(a));
        mine_->setCategories(std::move(cats));
    }

    // ---- Mes appareils ----
    {
        std::vector<std::vector<std::string>> rows;
        for (const auto& d : licensing::getLicensedDevices())
            rows.push_back({d.name.empty() ? std::string("(sans nom)") : d.name, d.fingerprint, d.activatedAt.size() >= 10 ? d.activatedAt.substr(0, 10) : d.activatedAt,
                            d.thisPc ? "ce PC" : ""});
        devicesModel_ = std::make_shared<Rows>(std::vector<std::string>{"PC", "Empreinte de sa cl\xC3\xA9", "Activ\xC3\xA9 le", ""}, std::move(rows));
        devices_->setModel(devicesModel_);
    }

    // ---- Fonctionnalites ----
    {
        std::vector<std::vector<std::string>> rows;
        std::vector<bool> open;
        if (reg)
            for (const auto& f : reg->features) {
                if (f.retired) continue;
                const bool on = licence::allows(f.id);
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
                if (c == 3 && r < open.size()) {
                    s.icon = open[r] ? ui::Icon::Ok : ui::Icon::Lock;
                    s.iconTone = open[r] ? ui::Tone::Ok : ui::Tone::Warning;
                    s.fgTone = open[r] ? ui::Tone::Ok : ui::Tone::Warning;
                }
                return s;
            });
        features_->setModel(featuresModel_);
    }

    // ---- Editions (ce qu'ouvre chacune) ----
    {
        std::vector<std::string> headers{"Fonctionnalit\xC3\xA9"};
        std::vector<ui::TableView::Column> cols{{"Fonctionnalit\xC3\xA9", 200.f}};
        std::vector<std::vector<std::string>> rows;
        std::vector<EditionsPanel::RowInfo> infos;   // 1.37.0 : la fonctionnalite de chaque ligne (l'apercu)
        std::size_t mineCol = 0, summary = 0;
        if (reg) {
            for (const auto& e : reg->editions) {
                const bool isMine = !st.licensedEdition.empty() ? e.id == st.licensedEdition : e.id == st.edition;
                std::string h = (e.label.empty() ? e.id : e.label) + (isMine ? " (la tienne)" : "");
                headers.push_back(h);
                cols.push_back({h, 150.f});
                if (isMine) mineCol = headers.size() - 1;
            }
            {
                std::vector<std::string> line{"PC \xC2\xB7 dur\xC3\xA9" "e"};
                for (const auto& e : reg->editions)
                    line.push_back(std::to_string(e.maxDevices) + " PC \xC2\xB7 " + (e.durationDays > 0 ? std::to_string(e.durationDays) + " j" : std::string("sans fin")));
                rows.push_back(std::move(line));
                infos.push_back({});
            }
            // 1.37.0 : le prix (la boutique), si l'editeur en a donne un.
            if (std::any_of(reg->editions.begin(), reg->editions.end(), [](const auto& e) { return !e.price.empty(); })) {
                std::vector<std::string> line{"Prix"};
                for (const auto& e : reg->editions) line.push_back(e.price.empty() ? std::string("\xE2\x80\x94") : e.price);
                rows.push_back(std::move(line));
                infos.push_back({});
            }
            summary = rows.size();
            for (const auto& f : reg->features) {
                if (f.retired) continue;
                std::vector<std::string> line{f.label.empty() ? f.id : f.label};
                std::string in;
                for (const auto& e : reg->editions) {
                    const bool has = std::find(e.features.begin(), e.features.end(), f.id) != e.features.end();
                    line.push_back(has ? "\xE2\x9C\x93" : "\xE2\x80\x94");
                    if (has) in += (in.empty() ? "" : ", ") + (e.label.empty() ? e.id : e.label);
                }
                rows.push_back(std::move(line));
                infos.push_back({f.id, f.label.empty() ? f.id : f.label, f.product, in});
            }
        }
        editions_->setColumns(cols);
        editionsModel_ = std::make_shared<Rows>(std::move(headers), std::move(rows), [mineCol, summary](ui::RowIndex r, std::size_t c) {
            ui::CellStyle s;
            if (r < summary) s.bold = true;
            if (c > 0 && c == mineCol) s.bold = true;
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
    import_->setEnabled(configured);
    import_->setTooltip(configured ? std::string("Le .xpglic re\xC3\xA7u de l'\xC3\xA9" "diteur")
                                   : std::string("Ce programme ne peut pas encore v\xC3\xA9rifier une licence : il n'a pas la cl\xC3\xA9 de l'\xC3\xA9" "diteur"));
    invalidate();
}

void LicenceBody::askImport() {
    std::vector<FormDialog::Field> fields;
    fields.push_back({"Fichier", "", "C:\\Users\\moi\\Downloads\\XPG-2026-00042.xpglic", false, {}});
    app_.menus().ShowDialog(
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
    app_.menus().ShowDialog(std::make_unique<MessageDialog>(report.ok ? "Licence import\xC3\xA9" "e" : "Licence refus\xC3\xA9" "e", std::move(text),
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
    app_.menus().ShowDialog(
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
    headerY_ = panel_.y + 52.f;
    messageY_ = headerY_ + 2.f * 44.f + 6.f;
    const float tabsY = messageY_ + 34.f;
    const float bottom = panel_.bottom() - 52.f;
    tabs_->setBounds({x, tabsY, W, std::max(60.f, bottom - tabsY - 6.f)});
    float bx = x;
    for (auto* b : {import_, request_, folder_}) {
        const float bw = std::max(110.f, b->sizeHint().preferred.w);
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
    for (int i = 0; i < 8; ++i) {
        const float cx = x + cw * static_cast<float>(i % 4);
        const float cy = headerY_ + 44.f * static_cast<float>(i / 4);
        r.drawText({cx, cy}, cells[i].label, kSmall, c.textMuted);
        r.drawText({cx, cy + r.lineHeight(kSmall) + 1.f}, fit(r, cells[i].value, kValue, cw - 12.f), kValue, cells[i].status ? statusColor : c.text);
    }
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

// ============================================================ 1.35.0 : Activation ==
ActivationPanel::ActivationPanel(LicenceBody& body, App& app) : ui::Widget("licence.activation"), body_(body), app_(app) {
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
    app_.menus().ShowDialog(std::move(dialog), [this, id](const menu::DialogResult& r) {
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
                r.drawText({x, it.y}, it.label, head, c.text);
                r.drawText({x + 0.6f, it.y}, it.label, head, c.text);
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
    t += " \xC2\xB7 " + std::to_string(e.features.size()) + (e.features.size() > 1 ? " fonctions" : " fonction");
    return t;
}

OnlinePanel::OnlinePanel(LicenceBody& body, App& app) : ui::Widget("licence.demande"), body_(body), app_(app) {
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
    links_ += send_->clicked->connect([this] { doSend(); });
    links_ += cancel_->clicked->connect([this] { askCancel(); });
    links_ += check_->clicked->connect([this] { doCheck(); });
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
    const auto p = licence::loadProfile();
    if (customer_->text().empty()) customer_->setText(!p.company.empty() ? p.company : p.name);
    if (contact_->text().empty() && !p.company.empty()) contact_->setText(p.name);
    if (email_->text().empty()) email_->setText(p.email);
}

void OnlinePanel::buildCards() {
    cards_.clear();
    const auto reg = licensing::getRegistry();
    if (!reg) return;
    for (const auto& e : reg->editions) {
        if (e.retired || e.id == reg->unlicensedEdition) continue;
        Card k;
        k.id = e.id;
        k.label = e.label.empty() ? e.id : e.label;
        k.price = e.price;
        k.description = e.description;
        k.facts = editionFacts(e);
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
    recProblem_.clear();
    if (!ls::loadRecord(rec_, &recProblem_)) rec_ = ls::Record{};
    pending_ = ls::pending(rec_);
    busy_ = ls::busyJob();
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
    cancel_->setVisibility(available_ && pending_ ? ui::Visibility::Visible : ui::Visibility::Collapsed);
    check_->setVisibility(available_ ? ui::Visibility::Visible : ui::Visibility::Collapsed);
    for (auto* b : {send_, cancel_, check_}) b->setEnabled(busy_ == ls::Job::None);
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
    app_.menus().ShowDialog(std::move(dialog), [this](const menu::DialogResult& r) {
        if (!r.accepted()) return;
        if (!licence::service::startCancelRequest()) body_.say("Un travail du service des licences est d\xC3\xA9j\xC3\xA0 en cours : attends sa fin.", 2);
        else body_.say("Annulation de la demande\xE2\x80\xA6", 0);
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

ui::EventResult OnlinePanel::onEvent(const ui::InputEvent& ev) {
    // 1.37.0 : la boutique - un clic choisit une edition, un second clic la rend (l'editeur choisira).
    if (const auto* d = std::get_if<ui::MouseDown>(&ev); d && available_ && !pending_)
        for (const auto& k : cards_)
            if (k.box.contains(d->pos)) {
                chosen_ = chosen_ == k.id ? std::string() : k.id;
                body_.say(chosen_.empty() ? std::string("Aucune \xC3\xA9" "dition choisie : l'\xC3\xA9" "diteur la choisira avec toi.")
                                          : "\xC3\x89" "dition choisie : " + k.label + (k.price.empty() ? std::string() : " (" + k.price + ")") + ".",
                          0);
                invalidate();
                return ui::EventResult::Consumed;
            }
    return ui::EventResult::Ignored;
}

namespace {
constexpr float kCardH = 98.f, kCardGap = 10.f, kIntroLines = 3.f;
} // namespace

void OnlinePanel::onLayout() {
    const auto b = bounds();
    const float x = b.x + 16.f, W = std::max(200.f, b.w - 32.f);
    const float labelW = 210.f, fieldX = x + labelW, fieldW = std::max(160.f, std::min(620.f, W - labelW));
    rowsY_ = b.y + 16.f + kIntroLines * 20.f + 10.f;   // sous l'introduction (trois lignes au plus)
    float y = rowsY_;
    if (available_ && !pending_) {
        // 1.37.0 : la boutique - les cartes des editions, sur une ou deux rangees.
        shopY_ = y;
        if (!cards_.empty()) {
            y += 24.f;
            const std::size_t n = cards_.size();
            const std::size_t perRow = std::max<std::size_t>(1, std::min<std::size_t>(n, static_cast<std::size_t>((W + kCardGap) / (180.f + kCardGap))));
            const float cw = std::min(260.f, (W - kCardGap * static_cast<float>(perRow - 1)) / static_cast<float>(perRow));
            for (std::size_t i = 0; i < n; ++i) {
                const float cx = x + static_cast<float>(i % perRow) * (cw + kCardGap);
                const float cy = y + static_cast<float>(i / perRow) * (kCardH + kCardGap);
                cards_[i].box = {cx, cy, cw, kCardH};
            }
            y += static_cast<float>((n + perRow - 1) / perRow) * (kCardH + kCardGap) + 4.f;
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
}

void OnlinePanel::onPaint(const ui::PaintContext& ctx) {
    namespace ls = licence::service;
    auto& r = ctx.r;
    const auto& c = ctx.theme.color;
    const auto b = bounds();
    const float x = b.x + 16.f, W = std::max(200.f, b.w - 32.f), lh = r.lineHeight(kSmall) + 4.f;
    const float labelW = 210.f, fieldX = x + labelW;
    // L'introduction.
    // 1.36.0 : la demande en ligne, le chemin principal : ce qui va se passer, dit simplement.
    const std::string intro = !available_ ? std::string()
        : pending_
        ? std::string("Ta demande attend l'\xC3\xA9" "diteur. Le Studio regarde tout seul, toutes les minutes, s'il a r\xC3\xA9pondu : d\xC3\xA8s qu'il "
                      "l'accepte, ta licence s'installe d'elle-m\xC3\xAAme et une fen\xC3\xAAtre te le confirme (ce qu'elle ouvre, jusqu'\xC3\xA0 quand). "
                      "Tu peux fermer cette fen\xC3\xAAtre, et m\xC3\xAAme le Studio : la r\xC3\xA9ponse t'attendra au prochain lancement.")
        : std::string("Choisis ta licence et demande-la \xC3\xA0 l'\xC3\xA9" "diteur : ta soci\xC3\xA9t\xC3\xA9 suffit, le reste est facultatif (Mon profil remplit "
                      "les champs). Ta demande part scell\xC3\xA9" "e : lui seul la lit. D\xC3\xA8s qu'il l'accepte, ta licence s'installe d'elle-m\xC3\xAAme et "
                      "une fen\xC3\xAAtre te le confirme - aucun fichier, aucun code \xC3\xA0 taper. Une seule demande \xC3\xA0 la fois pour ce PC.");
    const std::string introOff = why_ + " Utilise les boutons du bas : Demander une activation\xE2\x80\xA6 (un fichier \xC3\xA0 envoyer \xC3\xA0 l'\xC3\xA9" "diteur), "
                                 "puis Activer par fichier de licence\xE2\x80\xA6 avec sa r\xC3\xA9ponse.";
    float y = b.y + 16.f;
    int n = 0;
    for (const auto& l : wrapText(r, available_ ? intro : introOff, kSmall, W)) {
        if (++n > 3) break;
        r.drawText({x, y}, fit(r, l, kSmall, W), kSmall, available_ ? c.text : c.textMuted);
        y += 20.f;
    }
    if (!available_) return;
    const gfx::Color good = ctx.theme.tone(ui::Tone::Ok, c.text), warn = ctx.theme.tone(ui::Tone::Warning, c.text),
                     bad = ctx.theme.tone(ui::Tone::Error, c.text);
    if (!pending_) {
        // 1.37.0 : la boutique.
        if (!cards_.empty()) {
            r.drawText({x, shopY_}, "Choisis ta licence (facultatif : sinon l'\xC3\xA9" "diteur la choisit avec toi) - aucun paiement ici, l'\xC3\xA9" "diteur te contacte.",
                       kSmall, c.textMuted);
            for (const auto& k : cards_) {
                const bool on = k.id == chosen_;
                r.fillRect(k.box, on ? c.headerBg : c.inputBg);
                r.strokeRect(k.box, on ? c.accent : c.border, on ? 2.f : 1.f);
                const float ix = k.box.x + 10.f, iw = k.box.w - 20.f;
                float ty = k.box.y + 8.f;
                const std::string title = fit(r, k.label, kValue, iw - (on ? 22.f : 0.f));
                r.drawText({ix, ty}, title, kValue, c.text);
                r.drawText({ix + 1.f, ty}, title, kValue, c.text);   // gras
                if (on) r.drawText({k.box.right() - 26.f, ty}, "\xE2\x9C\x93", kValue, c.accent);
                ty += r.lineHeight(kValue) + 2.f;
                r.drawText({ix, ty}, fit(r, k.price.empty() ? std::string("prix sur demande") : k.price, kSmall, iw), kSmall,
                           k.price.empty() ? c.textMuted : c.accent);
                ty += 19.f;
                int dl = 0;
                for (const auto& l : wrapText(r, k.description, kSmall, iw)) {
                    if (++dl > 1) break;
                    r.drawText({ix, ty}, fit(r, l, kSmall, iw), kSmall, c.textMuted);
                }
                ty += 19.f;
                r.drawText({ix, ty}, fit(r, k.facts, kSmall, iw), kSmall, c.text);
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
                    if (e.id == rec_.edition) wanted = (e.label.empty() ? e.id : e.label) + (e.price.empty() ? std::string() : " \xC2\xB7 " + e.price);
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
ProfilePanel::ProfilePanel(LicenceBody& body) : ui::Widget("licence.profil"), body_(body) {
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
    std::string why;
    if (!licence::saveProfile(p, &why)) {
        body_.say("Profil non enregistr\xC3\xA9 : " + why + ".", 3);
        return;
    }
    refresh();
    body_.say("Profil enregistr\xC3\xA9 : il remplit ta demande de licence ; ton courriel recevra le mail de l'\xC3\xA9" "diteur.", 1);
    body_.profileSaved();
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
    save_->setBounds({fieldX, y + 4.f, std::max(180.f, save_->sizeHint().preferred.w), 32.f});
}

void ProfilePanel::onPaint(const ui::PaintContext& ctx) {
    auto& r = ctx.r;
    const auto& c = ctx.theme.color;
    const auto b = bounds();
    const float x = b.x + 16.f, W = std::max(200.f, b.w - 32.f);
    const std::string intro =
        "Tout est facultatif. Ton profil remplit ta demande de licence (onglet Demander ma licence) ; ton courriel re\xC3\xA7oit le mail de l'\xC3\xA9" "diteur "
        "quand il accepte ta demande (avec le code d'une licence en ligne). Il reste sur ce PC, pour API Studio et IHM Studio ; rien ne part tant que "
        "tu n'envoies pas une demande.";
    float y = b.y + 16.f;
    int n = 0;
    for (const auto& l : wrapText(r, intro, kSmall, W)) {
        if (++n > 3) break;
        r.drawText({x, y}, fit(r, l, kSmall, W), kSmall, c.text);
        y += 20.f;
    }
    const char* labels[3] = {"Courriel", "Nom", "Soci\xC3\xA9t\xC3\xA9"};
    float fy = rowsY_;
    for (const char* l : labels) {
        r.drawText({x, fy + (28.f - r.lineHeight(kSmall)) * 0.5f}, l, kSmall, c.text);
        fy += 36.f;
    }
    r.drawText({x, fy + 48.f}, fit(r, "Enregistr\xC3\xA9 dans : " + licence::profilePath(), kSmall, W), kSmall, c.textMuted);
}

// ============================================================ 1.37.0 : Editions et l'apercu ==
EditionsPanel::EditionsPanel() : ui::Widget("dialog.licence.editions.panneau") {
    table_ = &static_cast<ui::TableView&>(addChild(std::make_unique<ui::TableView>("dialog.licence.editions")));
    table_->setSelectionMode(ui::SelectionMode::Single);
    links_ += table_->selectionChanged->connect([this](const std::vector<ui::RowIndex>&) { choose(); });
}

void EditionsPanel::setRows(std::vector<RowInfo> rows) {
    rows_ = std::move(rows);
    shown_ = RowInfo{};
    choose();
}

void EditionsPanel::choose() {
    const auto sel = table_->selectedModelRows();
    RowInfo info;
    if (!sel.empty() && sel.front() < rows_.size()) info = rows_[sel.front()];
    if (info.id == shown_.id && info.editions == shown_.editions && info.label == shown_.label) return;
    shown_ = std::move(info);
    preview_ = shown_.id.empty() ? licence::FeaturePreview{} : licence::featurePreview(shown_.id);
    image_ = hmi::Resource{};
    if (!preview_.imagePath.empty()) {
        std::error_code ec;
        const auto path = std::filesystem::path(std::u8string(preview_.imagePath.begin(), preview_.imagePath.end()));
        const auto size = std::filesystem::file_size(path, ec);
        if (!ec && size > 0 && size < 8u * 1024u * 1024u) {
            std::ifstream in(path, std::ios::binary);
            hmi::Bytes bytes(static_cast<std::size_t>(size));
            if (in.read(reinterpret_cast<char*>(bytes.data()), static_cast<std::streamsize>(size))) {
                image_.name = shown_.id + ".png";
                image_.format = "PNG";
                image_.data = std::make_shared<const hmi::Bytes>(std::move(bytes));
            }
        }
    }
    invalidate();
}

void EditionsPanel::onLayout() {
    const auto b = bounds();
    const float pw = std::clamp(std::floor(b.w * 0.42f), 280.f, 470.f);
    table_->setBounds({b.x, b.y, std::max(200.f, b.w - pw - 12.f), b.h});
    pane_ = {b.right() - pw, b.y, pw, b.h};
}

void EditionsPanel::onPaint(const ui::PaintContext& ctx) {
    auto& r = ctx.r;
    const auto& c = ctx.theme.color;
    r.fillRect(pane_, c.headerBg);
    r.strokeRect(pane_, c.border, 1.f);
    const float x = pane_.x + 12.f, W = pane_.w - 24.f;
    float y = pane_.y + 12.f;
    if (shown_.id.empty()) {
        for (const auto& l : wrapText(r, "Choisis une fonctionnalit\xC3\xA9 dans la liste : une capture et ce qu'elle fait s'affichent ici.", kSmall, W)) {
            r.drawText({x, y}, fit(r, l, kSmall, W), kSmall, c.textMuted);
            y += 20.f;
        }
        return;
    }
    const std::string title = fit(r, shown_.label, kValue, W);
    r.drawText({x, y}, title, kValue, c.text);
    r.drawText({x + 1.f, y}, title, kValue, c.text);
    y += r.lineHeight(kValue) + 2.f;
    if (!shown_.product.empty()) {
        r.drawText({x, y}, fit(r, shown_.product, kSmall, W), kSmall, c.textMuted);
        y += 20.f;
    }
    y += 4.f;
    // La capture (960 x 600), a la largeur du volet.
    const float ih = std::min(std::floor(W * 0.625f), std::max(80.f, pane_.h * 0.5f));
    const gfx::Rect box{x, y, W, ih};
    r.fillRect(box, c.inputBg);
    bool drawn = false;
    if (image_.data) {
        const auto img = HmiImageCache::instance().resource(r, image_);
        if (img.valid()) {
            const float k = std::min(box.w / static_cast<float>(img.width), box.h / static_cast<float>(img.height));
            const float w = static_cast<float>(img.width) * k, h = static_cast<float>(img.height) * k;
            r.drawImage(img.tex, {box.x + (box.w - w) * 0.5f, box.y + (box.h - h) * 0.5f, w, h});
            drawn = true;
        }
    }
    if (!drawn)
        r.drawText({box.x + 12.f, box.y + (box.h - r.lineHeight(kSmall)) * 0.5f}, fit(r, "Pas encore de capture pour cette fonctionnalit\xC3\xA9.", kSmall, W - 24.f),
                   kSmall, c.textMuted);
    r.strokeRect(box, c.border, 1.f);
    y += ih + 10.f;
    int n = 0;
    for (const auto& l : wrapText(r, preview_.description.empty() ? std::string("(pas encore de description)") : preview_.description, kSmall, W)) {
        if (++n > 4) break;
        r.drawText({x, y}, fit(r, l, kSmall, W), kSmall, preview_.description.empty() ? c.textMuted : c.text);
        y += 20.f;
    }
    y += 6.f;
    for (const auto& l : wrapText(r, "Dans : " + (shown_.editions.empty() ? std::string("aucune \xC3\xA9" "dition") : shown_.editions), kSmall, W)) {
        r.drawText({x, y}, fit(r, l, kSmall, W), kSmall, c.textMuted);
        y += 20.f;
    }
}

} // namespace

void showLicenceWindow(App& app, int tab) {
    app.menus().ShowDialog(std::make_unique<LicenceWindow>(app, tab), nullptr);
}

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

void showAnswer(App& app, const Answer& a) {
    const auto v = answerView(a, licensing::getLicenseState());
    auto dlg = std::make_unique<MessageDialog>(v.title, v.text, v.accepted ? MessageDialog::Icon::Info : MessageDialog::Icon::Warning, v.confirm);
    dlg->setCancelLabel(v.accepted ? "Fermer" : "Plus tard");
    dlg->setParagraphWidth(720.f);   // ce qu'elle ouvre, jusqu'a quand : un paragraphe, pas un editeur
    const int tab = v.tab;
    app.menus().ShowDialog(std::move(dlg), [&app, tab](const menu::DialogResult& r) {
        if (r.accepted()) showLicenceWindow(app, tab);
    });
}

} // namespace licence

} // namespace app
