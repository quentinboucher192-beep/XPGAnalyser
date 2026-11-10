// =============================================================================
//  license_admin/gui/AdminsBoard.hpp - 1.47.0 : LES PAGES DES ADMINS (maquette
//  1.47.0, planches 01 a 05 ; la liste << stats-admins-liste >>, par. 1 a 4)
// -----------------------------------------------------------------------------
//  La page Administrateurs (PageAdmins.cpp) montre a l'admin+ :
//    - LA VUE D'ENSEMBLE (planche 01, PageAdminsOverview.cpp) : une periode pour
//      toute la page, comparee a celle d'avant ; les tuiles ; le classement trie
//      par colonne, sa barre de filtres, son clic droit, une ligne depliee (ses
//      chiffres, sa courbe sur la periode, ses 5 dernieres actions, ses droits) ;
//      les alertes, chacune avec son geste ;
//    - QUI FAIT QUOI (planche 05, PageAdminsWho.cpp) : la part de chacun par type
//      d'action (une barre a 100 %), cette periode et celle d'avant par admin,
//      ce que l'admin+ seul fait ;
//    - LA FICHE D'UN ADMIN (planches 02 a 04, PageAdminCard.cpp) : une longue
//      page a ancres (Vue d'ensemble, Activite, Licences et clients, Demandes,
//      Boutique, Securite, Droits). Un admin ne voit que la sienne.
//  LES CHIFFRES : [J] le journal des admins, calcule par l'Admin lui-meme
//  (license_admin/JournalStats) ; [S]/[N] le service (license_admin/Stats, par
//  gui/StatsKit : fetchSeries, fetchAdmins - ou les reponses d'essai). Les appels
//  au service sont lents : un travail sans bruit (runQuiet), jamais sur le fil
//  de l'interface ; un service d'avant la 1.47.0 : une phrase et << Deployer le
//  service >>, les chiffres du journal restent.
// =============================================================================
#pragma once

#include "AdminsKit.hpp"
#include "Page.hpp"
#include "StatsKit.hpp"

#include "../JournalStats.hpp"
#include "../Stats.hpp"
#include "../../ui/widgets/Charts.hpp"
#include "../../ui/widgets/ScrollBar.hpp"

#include <cmath>
#include <functional>
#include <limits>
#include <map>
#include <memory>
#include <string>
#include <vector>

namespace license_admin::gui::adminsboard {

namespace js = license_admin::journalstats;
namespace ch = ui::charts;
using gfx::Color;
using gfx::FontId;
using gfx::Rect;
using statskit::Choice;

inline constexpr double kNaN = std::numeric_limits<double>::quiet_NaN();
[[nodiscard]] inline bool known(double v) noexcept { return !std::isnan(v); }

// ---- les couleurs de la maquette (extra.css) ----
inline constexpr Color kInk = Color::rgb(0x0F172A), kInk2 = Color::rgb(0x334155), kMuted = Color::rgb(0x64748B), kFaint = Color::rgb(0x94A3B8);
inline constexpr Color kLine = Color::rgb(0xE3E8EF), kLine2 = Color::rgb(0xF1F4F8), kSunken = Color::rgb(0xEEF2F7), kViolet = Color::rgb(0x6D4AFF);
inline constexpr Color kVioletInk = Color::rgb(0x5B3BE6), kVioletPale = Color::rgb(0xF3F0FF), kGreen = Color::rgb(0x15803D), kAmber = Color::rgb(0xB45309);
inline constexpr Color kRed = Color::rgb(0xDC2626), kAmberPale = Color::rgb(0xFFF8EB), kRedPale = Color::rgb(0xFEF2F2), kGreenPale = Color::rgb(0xF0FDF4);

// ============================================================================ les donnees
// Un admin tel que les pages le montrent : l'admin+, un admin de la liste (actif ou retire), une demande recue.
struct Person {
    enum class Kind : std::uint8_t { Plus, Admin, Pending };
    Kind                     kind{Kind::Admin};
    std::string              key;          // sa cle (64 hexa) ; une demande : la cle demandee
    std::string              id;           // son identifiant au service (xpg-...)
    std::string              name, pc, machine, profile, fingerprint, email, software, version;
    Seconds                  since{0}, retired{0}, received{0}, seen{0};
    std::vector<std::string> rights;
    std::string              addedBy;      // << toi >> (l'admin+), le nom de qui l'a ajoute
    int                      color{0};     // l'ordre fixe : 0 l'admin+, puis les admins de la liste (retires compris)
    int                      gaps{-1};     // ses ecarts a son profil (droits ajoutes ou retires pour lui) ; -1 : pas de profil
    bool                     me{false}, mismatch{false};
    [[nodiscard]] bool active() const noexcept { return kind != Kind::Pending && retired == 0; }
};
[[nodiscard]] Color personColor(const Person& p);   // Camille, Lea, Hugo, Nina... ; une demande : gris

// Ce que le service dit d'un admin sur la periode (NaN : inconnu).
struct AdminFigures {
    double licences{kNaN}, licencesBefore{kNaN};           // licences_nouvelles
    double requests{kNaN}, requestsBefore{kNaN};           // demandes_traitees
    double accepted{kNaN}, refused{kNaN};                  // demandes_acceptees, _refusees
    double meanDelay{kNaN}, meanDelayBefore{kNaN}, medianDelay{kNaN}, maxDelay{kNaN};   // secondes
    double amount{kNaN}, amountBefore{kNaN}, orders{kNaN}, ordersBefore{kNaN};          // centimes, commandes livrees
    bool   hasUsage{false};
    stats::AdminUsage usage;                               // ses jours, ses heures (UTC), sa version, sa ville
    std::string serviceId;                                 // son identifiant au service
};
// La fiche (planche 04) : ce qu'elle produit, et ses courbes sur 12 semaines (elle, les autres).
struct CardFigures {
    bool        loaded{false}, loading{false};
    std::string problem;
    double prolonged{kNaN}, options{kNaN}, active{kNaN}, expired{kNaN}, revoked{kNaN}, pcs{kNaN};
    double basket{kNaN}, basketBefore{kNaN}, refunds{kNaN}, refundsCount{kNaN};
    double clients{kNaN}, clientsBefore{kNaN};
    std::vector<std::pair<std::string, double>> byOffer, topClients;   // licences_nouvelles par offre, par client
    double topShare{kNaN};                                             // la part de ses 5 premiers (du montant)
    std::vector<stats::Bucket> weeks;                                  // 12 semaines
    std::vector<double> weekLicences, weekLicencesOthers, weekRequests, weekRequestsOthers, weekAmount, weekAmountOthers, weekDelay;
    double expiring{kNaN};                                             // ses licences qui finissent dans les 30 jours
};
// Les chiffres du service pour l'en-tete (planche 01).
struct Totals {
    double pending{kNaN}, oldest{kNaN}, meanDelay{kNaN}, meanDelayBefore{kNaN};
    double licences{kNaN}, licencesBefore{kNaN}, licencesPlus{kNaN};
    double amount{kNaN}, amountBefore{kNaN}, orders{kNaN};
    std::vector<double> amountPoints, licencePoints;
};

struct Data {
    Choice              choice;
    stats::Period       statsPeriod, statsBefore;   // la meme periode, en jours "AAAA-MM-JJ"
    js::Period          period, previous;
    Seconds             now{0};
    std::vector<Person> people;                     // l'admin+, puis les admins de la liste, puis les demandes
    js::Overview        ov;                         // les chiffres du journal
    std::vector<js::Line> lines;                    // le journal lu (les plus anciennes d'abord)
    bool                journal{false};             // le journal est lu (l'admin+)
    int                 journalLines{0};
    int                 profiles{0};
    // le service
    bool                serviceAsked{false}, serviceLoading{false}, serviceLoaded{false}, serviceTooOld{false};
    std::string         serviceNote;                // pourquoi pas de chiffres du service (vide : ils sont la)
    std::map<std::string, AdminFigures> figures;    // par cle
    std::map<std::string, CardFigures>  cards;      // par cle (la fiche ouverte)
    Totals              totals;
    std::string         latestAdmin;                // la derniere version de l'Admin (ce programme)

    [[nodiscard]] const Person*          person(const std::string& key) const;
    [[nodiscard]] const js::AdminStats*  stats(const std::string& key) const;   // vide : pas dans le journal
    [[nodiscard]] const AdminFigures*    fig(const std::string& key) const;
    [[nodiscard]] int activeCount() const;
    [[nodiscard]] int retiredCount() const;
    [[nodiscard]] int pendingCount() const;
};

// Le calcul local (le fil de l'interface : le journal de ce poste, JournalStats) : `people` deja la.
void computeLocal(const Workspace& w, Data& d);
// La periode d'un choix (les jours de Paris, et leurs textes pour le service).
void setPeriod(Data& d, const Choice& c, Seconds now, const std::string& allFrom);

// LE SERVICE (LENT : sur le fil d'un travail). Le resultat est ensuite pose sur Data (applyService) sur le fil de
// l'interface. `keys` / `ids` / `names` : les admins a lire (une copie : rien du Workspace hors de ses appels).
struct ServiceAsk {
    stats::Period            period, before;
    std::vector<std::string> keys, ids, names;
};
struct ServiceReply {
    bool                                tooOld{false};
    std::string                         note;
    std::map<std::string, AdminFigures> figures;
    Totals                              totals;
};
OpResult fetchService(const Workspace& w, const ServiceAsk& ask, ServiceReply& out);
void     applyService(Data& d, ServiceReply r);
// La fiche d'un admin (planche 04) : ses courbes sur 12 semaines, ses clients, ses offres.
struct CardAsk {
    stats::Period period, before;
    std::string   key, serviceId;
    Seconds       now{0};
};
OpResult fetchCard(const Workspace& w, const CardAsk& ask, CardFigures& out);

// ============================================================================ une vue qui defile
//  Une vue de la page (la vue d'ensemble, qui fait quoi, la fiche) : son contenu est pose a partir de son haut moins
//  le decalage (place), dessine dans son cadre (draw) ; la molette et la barre la font defiler. Un enfant hors du cadre
//  est cache (Hidden) ; un enfant que la vue a replie (Collapsed) le reste.
class ScrolledView : public ui::Widget {
public:
    using ui::Widget::Widget;
    [[nodiscard]] float offset() const noexcept { return offset_; }
    [[nodiscard]] float contentHeight() const noexcept { return content_; }
    void scrollTo(float y);   // le contenu a cette hauteur (depuis son haut) en haut du cadre

protected:
    // Poser le contenu dans `area` (son haut : le haut du cadre moins le decalage) ; rendre sa hauteur.
    virtual float place(const Rect& area) = 0;
    virtual void  draw(const ui::PaintContext& ctx) = 0;
    void            onLayout() override;
    void            onPaint(const ui::PaintContext& ctx) override;
    void            onPaintOverlay(const ui::PaintContext& ctx) override;
    ui::EventResult onEvent(const ui::InputEvent& ev) override;
    [[nodiscard]] Rect viewport() const noexcept { return view_; }
    // Poser un enfant : `shown` faux -> replie ; hors du cadre -> cache (Hidden), sinon visible.
    void put(ui::Widget* w, const Rect& r, bool shown = true) const;

private:
    ui::ScrollArea scroll_;
    float          offset_{0.f}, content_{0.f};
    Rect           view_{};
};

// ============================================================================ dessiner
// Une carte blanche (le trait, l'ombre a peine), comme la maquette.
void card(gfx::IRenderer& r, const ui::Theme& th, const Rect& b, float radius = 12.f);
// L'avatar d'un admin : son initiale sur sa couleur (un carre arrondi).
void avatarOf(gfx::IRenderer& r, const Rect& box, const Person& p);
// Un texte raccourci a `w`, dessine ; rend sa largeur.
float text(gfx::IRenderer& r, float x, float y, const std::string& s, FontId f, Color c, float w = 1e9f);
// L'ecart : << +22 % >> (vert ou rouge selon le sens, gris s'il est nul) ; rend sa largeur ; vide : 0.
float delta(gfx::IRenderer& r, float x, float y, double cur, double prev, bool upIsGood = true, FontId f = FontId{12, 1}, bool count = false);
// La pastille J, S ou N (la source d'un chiffre).
float sourceTag(gfx::IRenderer& r, float x, float y, const std::string& tag);
// Le titre d'une carte (gras), son sous-titre (discret) ; rend la hauteur prise.
float cardTitle(gfx::IRenderer& r, float x, float y, float w, const std::string& title, const std::string& sub = {}, const std::string& tags = {});

// Un lien (<< Voir dans le journal \xE2\x80\xBA >>) : un bouton dessine en texte violet (les scripts le cliquent : --cliquer).
class LinkButton final : public ui::Button {
public:
    LinkButton(std::string text, std::string id, bool chevron = true) : ui::Button(std::move(text), std::move(id)), chevron_(chevron) {}
    [[nodiscard]] ui::SizeHint sizeHint() const override;
    [[nodiscard]] float naturalWidth() const;
    Color color{Color::rgb(0x5B3BE6)};

protected:
    void onPaint(const ui::PaintContext& ctx) override;

private:
    bool chevron_;
};
template <class T>
T& adoptIn(ui::Widget& parent, std::unique_ptr<T> w) {
    return static_cast<T&>(parent.addChild(std::move(w)));
}
ui::Button& makeButton(ui::Widget& parent, std::string text, std::string id, ui::Button::Style style = ui::Button::Style::Default,
                       ui::Icon icon = ui::Icon::None);
[[nodiscard]] float buttonW(const ui::Widget& b, float minimum = 0.f);

// ---- les droits, les versions ----
[[nodiscard]] std::string rightShort(const std::string& id);     // << Creer et signer >>, << Retirer un PC >>...
[[nodiscard]] bool olderVersion(const std::string& a, const std::string& b);   // a < b (<< 1.45.2 >> < << 1.47.0 >>)

// ---- les textes ----
[[nodiscard]] std::string num(double v);               // << 1 240 >> ; NaN : << \xE2\x80\x94 >>
[[nodiscard]] std::string euros(double cents);          // << 4 870 \xE2\x82\xAC >>
[[nodiscard]] std::string hoursText(double seconds);    // << 4 h 05 >>
[[nodiscard]] std::string signedCount(double cur, double prev);   // << +7 >>, << \xE2\x88\x923 >>, << = >>
[[nodiscard]] std::string pct(double cur, double prev);           // << +22 % >> ; vide : rien avant
[[nodiscard]] std::string ago(Seconds now, Seconds t);            // << il y a 12 min >>, << hier 18:20 >>, << 28/10 16:02 >>
[[nodiscard]] std::string dayMonth(Seconds t);                    // << 03/11 >> (heure de Paris)
[[nodiscard]] std::string clockOf(Seconds t);                     // << 10:48 >> (heure de Paris)
[[nodiscard]] std::string minuteText(int minutes);                // 520 -> << 8 h 40 >>
[[nodiscard]] std::string shortSummary(const js::Ref& r);         // << a sign\xC3\xA9 XPG-\xE2\x80\xA6412 >>
[[nodiscard]] std::string levelName(js::Level l);                 // << Info >>, << Moyenne >>, << Haute >>, << Critique >>
[[nodiscard]] Color levelColor(js::Level l);

// ============================================================================ les vues (leurs fichiers)
// Ce que les vues demandent a la page (la coquille, les dialogues).
struct Actions {
    std::function<void(const std::string& key)> openCard, journalOf, rightsOf, retire, verify, refuse, select;
    std::function<void(std::vector<std::uint64_t> lines, const std::string& key)> journalLines;   // ces lignes du journal
    std::function<void()>                        addAdmin, criticality, deploy, reload, versions;
    std::function<void(const std::string& key, std::vector<std::string> rights)> removeRights;   // << Retirer ces droits ? >>
    std::function<void(const std::string& pageId, const std::string& focus)> go;   // une autre page (les licences, les demandes)
    std::function<std::string(const std::string& key)> whyNotRetire;   // vide : il peut etre retire
    std::function<void(std::vector<std::string> keys)> exportRows;      // un CSV de ces admins
    std::function<bool()>                        busy;
};

class Overview;   // planche 01
class Who;        // planche 05
class AdminCard;  // planches 02-04
std::unique_ptr<ScrolledView> makeOverview(std::shared_ptr<Data> data, Actions actions, ShellHost host);
std::unique_ptr<ScrolledView> makeWho(std::shared_ptr<Data> data, Actions actions, ShellHost host);
std::unique_ptr<ScrolledView> makeCard(std::shared_ptr<Data> data, Actions actions, ShellHost host, std::string key, bool own);
// La vue d'ensemble : la ligne choisie (depliee), le tri ; les essais.
void        overviewSelect(ScrolledView& v, const std::string& key);
[[nodiscard]] std::string overviewSelected(const ScrolledView& v);
void        overviewSort(ScrolledView& v, int column);   // un clic sur un titre (le meme : l'ordre inverse)
[[nodiscard]] std::vector<std::string> overviewOrder(const ScrolledView& v);   // les cles, dans l'ordre montre (filtre compris)
// La fiche : aller a une ancre (0 Vue d'ensemble, 1 Activite, 2 Licences et clients, 3 Demandes, 4 Boutique, 5 Securite, 6 Droits).
void        cardAnchor(ScrolledView& v, int anchor);
// Les donnees ont change : refaire (les barres de filtres, les graphiques).
void        refreshView(ScrolledView& v);

} // namespace license_admin::gui::adminsboard
