// =============================================================================
//  license_admin/gui/StatsKit.hpp - 1.47.0 : CE QUE LES PAGES DES STATISTIQUES
//  PARTAGENT (la page Statistiques, ses Mises a jour, la fiche d'un client, la
//  rangee de courbes du tableau de bord)
// -----------------------------------------------------------------------------
//  La maquette validee : docs/maquettes/1.47.0, planches 08 (la vue generale),
//  09 (les mises a jour), 10 (la fiche d'un client), 11 (le globe : la place que
//  le globe prend, PageStatsGlobe.cpp). Le contrat des chiffres :
//  docs/STATISTIQUES.md (par. 13 : Stats.hpp ; par. 14 : ce que lit chaque
//  graphique). Les graphiques : ui/widgets/Charts.hpp.
//
//  ICI :
//    - LES APPELS (lents : par runQuiet / runSlow) : par le Workspace, ou par
//      des reponses d'essai (setFetchersForTests : les essais et les captures
//      donnent des reponses JSON lues par les vrais lecteurs de Stats.hpp) ;
//    - LA PERIODE CHOISIE (Choice) : 7 j, 30 j, 90 j, 12 mois, tout, deux dates ;
//      la comparaison (la periode d'avant, l'an dernier, rien) ; le pas (jour,
//      semaine, mois) - communs a tous les graphiques d'une page ;
//    - LES AXES (une categorie par periode du pas : << 17/08 >>, << Semaine du
//      26/10 au 01/11 >>, la periode en cours en pale) et les valeurs (les
//      centimes en euros, les secondes en heures) ;
//    - OUVRIR UNE LISTE FILTREE (un clic sur un point : << Commandes >> filtree
//      sur cette semaine) : la page de la liste, sa barre de filtres
//      (ui::FilterBar, celle de toutes les listes) ;
//    - les morceaux dessines communs : la barre des choix (SegGroup), les
//      onglets (TabButton), le bandeau d'un service trop ancien.
//  Les heures du service sont en UTC : on montre l'heure du PC (la France).
// =============================================================================
#pragma once

#include "Page.hpp"

#include "../Stats.hpp"
#include "../../ui/Icons.hpp"
#include "../../ui/widgets/Charts.hpp"
#include "../../ui/widgets/Controls.hpp"

#include <functional>
#include <map>
#include <memory>
#include <string>
#include <utility>
#include <vector>

namespace license_admin::gui::statskit {

namespace ch = ui::charts;

// ============================================================================ les appels
// Les reponses d'essai (les essais, les captures) : chaque appel rendu par une fonction au lieu du service. Une fonction
// vide : le Workspace (le reseau). Installe : les controles << le service est regle >> sont sautes (le bandeau du
// service trop ancien reste : il se lit dans l'etat du service).
struct Fetchers {
    std::function<OpResult(const stats::SeriesQuery&, stats::SeriesReply&)>  series;
    std::function<OpResult(const stats::UpdatesQuery&, stats::UpdatesReply&)> updates;
    std::function<OpResult(const stats::Period&, stats::AdminsReply&)>       admins;
    std::function<OpResult(int periodDays, stats::GlobeReply&)>              globe;
    std::function<OpResult(stats::SnapshotReply&)>                           snapshot;
};
void setFetchersForTests(Fetchers f);
[[nodiscard]] bool hooked();
// LES REPONSES D'ESSAI TOUTES FAITES (StatsSamples.cpp) : des chiffres FICTIFS et realistes (Camille, Lea, Hugo, Nina
// Essai ; Atelier Exemple, Societe d'essai...), ecrits en JSON comme le service 1.47.0 les rend puis lus par les vrais
// lecteurs (stats::parseSeries...) - pour les essais et les captures de l'Admin. Jamais montrees sans qu'on le demande :
// la variable d'environnement XPG_STATS_ESSAI=1 (les captures sous Wine) les installe a l'ouverture d'une page des
// statistiques (installSamplesFromEnvironment). `now` : l'heure de reference (vide : celle du Workspace a chaque appel).
[[nodiscard]] Fetchers sampleFetchers(std::function<Seconds()> now);
void installSamplesFromEnvironment(std::function<Seconds()> now);
// Le JSON d'essai lui-meme (les essais le modifient : un refus, des points vides).
[[nodiscard]] std::string sampleSeriesJson(const stats::SeriesQuery& q, Seconds now);
[[nodiscard]] std::string sampleUpdatesJson(const stats::UpdatesQuery& q, Seconds now);
[[nodiscard]] std::string sampleAdminsJson(const stats::Period& p, Seconds now);
// Les appels eux-memes (LENTS : sur le fil d'un travail). `w` n'est lu que pour ses appels au service.
OpResult fetchSeries(const Workspace& w, const stats::SeriesQuery& q, stats::SeriesReply& out, std::string* raw = nullptr);
OpResult fetchUpdates(const Workspace& w, const stats::UpdatesQuery& q, stats::UpdatesReply& out);
OpResult fetchAdmins(const Workspace& w, const stats::Period& p, stats::AdminsReply& out);
OpResult fetchGlobe(const Workspace& w, int periodDays, stats::GlobeReply& out);
OpResult fetchSnapshot(const Workspace& w, stats::SnapshotReply& out);

// Peut-on demander les statistiques ? Vide : oui ; sinon la phrase (le service a deployer, pas regle, pas de cle).
// `tooOld` : vrai quand c'est un service d'avant la 1.47.0 (le bandeau et << Deployer le service >>).
[[nodiscard]] std::string blocked(const Workspace& w, bool* tooOld = nullptr);
// Un refus du service qui dit qu'il est trop ancien (une route inconnue : la meme phrase).
[[nodiscard]] bool saysTooOld(const std::string& message);

// Un travail sans bruit (runQuiet ; les essais : runSlow).
bool runQuietly(const ShellHost& h, const std::string& label, std::function<OpResult()> work, std::function<void(const OpResult&)> done);

// LE RELEVE DES CHIFFRES A PART : les pages des statistiques lisent le service sur LEUR fil (un Background a elles), pas
// dans la file de la coquille - sinon elles attendraient derriere le regard, la version, les demandes (10 a 20 s au
// lancement). Des LECTURES seulement (GET /admin/stats/..., /admin/globe) : rien n'est ecrit dans le Workspace sur ce
// fil. Un a la fois par vue ; `done` est rendu sur le fil de l'interface par poll() (la vue l'appelle en se dessinant).
// Les reponses d'essai (hooked) : sur place, tout de suite.
class StatsJob {
public:
    StatsJob();
    ~StatsJob();
    StatsJob(const StatsJob&) = delete;
    StatsJob& operator=(const StatsJob&) = delete;
    bool start(const std::string& label, std::function<OpResult()> work, std::function<void(const OpResult&)> done);
    bool poll();   // vrai : un resultat a ete rendu
    [[nodiscard]] bool running() const noexcept { return bg_.running() || pending_; }
    void wait();

private:
    Background                           bg_;
    std::function<void(const OpResult&)> done_;
    bool                                 pending_{false};
};
// Les essais : attendre la fin de tous les releves en cours et rendre leurs resultats.
void finishJobsForTests();

// ============================================================================ la periode
enum class Range : std::uint8_t { Days7, Days30, Days90, Months12, All, Dates };
struct Choice {
    Range          range{Range::Days90};
    stats::Compare compare{stats::Compare::LastYear};
    stats::Step    step{stats::Step::Week};
    std::string    from, to;   // Dates : "AAAA-MM-JJ"
};
// La periode de ce choix ; `allFrom` : le premier jour de << Tout >> (le debut de l'archive).
[[nodiscard]] stats::Period periodOf(const Choice& c, Seconds now, const std::string& allFrom);
// Le premier jour de << Tout >> : la plus ancienne licence de l'archive ou commande (sinon deux ans).
[[nodiscard]] std::string allFromOf(const Workspace& w);
// Le nombre de jours d'une periode (bornes comprises) ; 0 : illisible.
[[nodiscard]] int daysOf(const stats::Period& p);
// Le pas, monte s'il donnerait plus de 400 points (le service les refuse) : jour -> semaine -> mois.
[[nodiscard]] stats::Step fitStep(const stats::Period& p, stats::Step want);
// << 13/08 -> 10/11/2026 >>.
[[nodiscard]] std::string periodText(const stats::Period& p);
[[nodiscard]] const char* rangeKey(Range r) noexcept;    // "7j", "30j", "90j", "12mois", "tout", "dates"
[[nodiscard]] const char* rangeLabel(Range r) noexcept;  // "7 j", "30 j", "90 j", "12 mois", "Tout", "Deux dates\xE2\x80\xA6"
[[nodiscard]] const char* compareLabel(stats::Compare c) noexcept;   // "la p\xC3\xA9riode d'avant", "l'an dernier", "rien"
[[nodiscard]] const char* stepLabel(stats::Step s) noexcept;          // "jour", "semaine", "mois"
[[nodiscard]] std::string stepSmall(stats::Step s);                   // "par semaine"

// ============================================================================ les jours, les axes, les valeurs
[[nodiscard]] ch::fr::Day dayNumber(const std::string& day);   // "AAAA-MM-JJ" -> des jours depuis 1970 (-1 : illisible)
[[nodiscard]] std::string ddmm(const std::string& day);         // "03/11"
[[nodiscard]] std::string ddmmyyyy(const std::string& day);     // "03/11/2026"
// Une categorie par periode du pas ; `level` : << Fin de la semaine du ... >> (un niveau : la valeur a la fin).
[[nodiscard]] std::vector<ch::Category> axisOf(const std::vector<stats::Bucket>& buckets, stats::Step step, bool level = false);
// Quatre libelles repartis (le premier, deux au milieu, le dernier).
[[nodiscard]] std::vector<std::pair<int, int>> ticksOf(std::size_t n);
// L'indice de la derniere periode si elle est en cours (aujourd'hui dedans, pas finie) ; -1 sinon.
[[nodiscard]] int partialFrom(const std::vector<stats::Bucket>& buckets, stats::Step step, const std::string& today);
// Les points d'une mesure (NaN : un trou) multiplies par `scale` (centimes -> euros : 0.01 ; secondes -> heures).
[[nodiscard]] std::vector<double> valuesOf(const std::vector<double>& points, double scale = 1.0);
[[nodiscard]] double sumOf(const std::vector<double>& v);
[[nodiscard]] double lastOf(const std::vector<double>& v);   // la derniere valeur connue (NaN : aucune)
// Le nom de la serie de la periode et de sa comparaison (<< 2026 >> / << l'an dernier >>, << cette p\xC3\xA9riode >> / << la
// p\xC3\xA9riode d'avant >>).
[[nodiscard]] std::string mainName(stats::Compare c, const stats::Period& p);
[[nodiscard]] std::string compareName(stats::Compare c, bool longForm = false, const stats::Period& p = {});
// << contre 23 000 \xE2\x82\xAC l'an dernier >>.
[[nodiscard]] std::string againstText(stats::Compare c, const std::string& value, bool withWhen = true);
// Le libelle d'une offre ou d'une cle d'une division (<< options >> -> << Options >>, << \xE2\x80\xA6 >> -> << les autres >>, "" -> << sans >>).
[[nodiscard]] std::string partLabel(const std::string& split, const std::string& key);
// La cle de couleur d'une offre (<< offre:PROFESSIONAL >>).
[[nodiscard]] std::string offerColorKey(const std::string& key);
// L'heure UTC (0-23) a l'heure du PC (la France : +1 ou +2) pour ce jour.
[[nodiscard]] int localHour(int utcHour, Seconds day);
// << auj. 11:40 >>, << hier >>, << il y a 41 j >> (l'heure du PC).
[[nodiscard]] std::string seenText(Seconds now, Seconds at);

// ============================================================================ ouvrir une liste filtree
// Un clic sur un point : la page d'une liste (<< commandes >>, << licences >>, << activations >>, << demandes >>), sa
// barre de filtres (ui::FilterBar) posee : des valeurs sur des colonnes (par leur titre), une periode (le titre d'une
// colonne de dates, du jour `from` au jour `to`), une recherche. La barre ne sait qu'une periode qui finit aujourd'hui
// (les N derniers jours) ou un seul jour : une periode plus ancienne de plusieurs jours devient << depuis ce jour >> (c'est
// dit dans l'avis). Rend l'avis montre (vide : rien a dire).
struct ListFilter {
    std::string                                      page;
    std::vector<std::pair<std::string, std::string>> values;    // (titre de colonne, valeur)
    std::string                                      periodField, from, to;
    std::string                                      search;
    std::string                                      what;      // << les commandes de la semaine du 26/10 au 01/11 >>
};
std::string openList(const ShellHost& h, ui::Widget& from, const ListFilter& f, Seconds now);
// Ce que la derniere ouverture a pose (les essais).
[[nodiscard]] const ListFilter& lastOpened();

// ============================================================================ les morceaux dessines
// Une barre de choix (<< 7 j | 30 j | 90 j | ... >>) : une piste grise, le choix en blanc. Ses boutons : <id>.<cle>.
class SegGroup final : public ui::Widget {
public:
    struct Item {
        std::string key, label;
        ui::Icon    icon{ui::Icon::None};
    };
    SegGroup(std::string id, std::vector<Item> items);
    void setCurrent(const std::string& key);
    [[nodiscard]] const std::string& current() const noexcept { return current_; }
    [[nodiscard]] float preferredWidth() const;
    [[nodiscard]] ui::Button* button(const std::string& key) const;
    // Changer le libelle d'un choix (un nombre : << Tous 119 >>).
    void setLabel(const std::string& key, std::string label);
    std::function<void(const std::string&)> onPick;

protected:
    void onLayout() override;
    void onPaint(const ui::PaintContext& ctx) override;

private:
    class Seg;
    std::vector<Item>        items_;
    std::vector<ui::Button*> buttons_;
    std::string              current_;
    core::ConnectionScope    links_;
};

// Un onglet (une icone, un libelle, un nombre discret ; le choisi souligne).
class TabButton final : public ui::Button {
public:
    TabButton(std::string label, std::string id, ui::Icon icon, std::string count = {});
    void setSelected(bool on) {
        selected_ = on;
        invalidate();
    }
    void setCount(std::string c) {
        count_ = std::move(c);
        invalidateLayout();
    }
    [[nodiscard]] bool selected() const noexcept { return selected_; }
    [[nodiscard]] float preferredWidth() const;

protected:
    void onPaint(const ui::PaintContext& ctx) override;

private:
    std::string label_, count_;
    ui::Icon    icon_;
    bool        selected_{false};
};

// LA CARTE D'UN GRAPHIQUE DONT LES LIGNES TIENNENT (jamais d'ellipse, << le texte sort du cadre >>) : la valeur, l'ecart, puis
// la premiere des comparaisons proposees qui tient sur la ligne (la plus longue d'abord) ; sinon sans l'ecart ; sinon sans
// comparaison. La precision du titre : la premiere qui tient, sinon aucune. Choisi a chaque mise en page (sa largeur).
class FitCard : public ch::ChartCard {
public:
    FitCard(std::string id, std::string title, std::string small = {});
    void setText(std::string value, ch::Delta delta = {}, std::vector<std::string> compares = {});
    void setText(std::string value, ch::Delta delta, std::string compare) {
        setText(std::move(value), delta, std::vector<std::string>{std::move(compare)});
    }
    void setSmalls(std::string title, std::vector<std::string> smalls);
    void setSmalls(std::string title, std::string small) { setSmalls(std::move(title), std::vector<std::string>{std::move(small)}); }
    // Ce qui est montre (les essais).
    [[nodiscard]] const std::string& shownCompare() const noexcept { return shownCompare_; }
    [[nodiscard]] bool deltaShown() const noexcept { return deltaShown_; }

protected:
    void onLayout() override;

private:
    void apply();
    std::string              text_, heading_;
    ch::Delta                delta_{};
    std::vector<std::string> compares_, smalls_;
    std::string              applied_, shownCompare_;
    bool                     deltaShown_{false};
};

// UNE LISTE DE BARRES DONT LES LIBELLES NE SONT JAMAIS COUPES : la place manque pour le libelle, la valeur et la colonne
// discrete (<< 21 cmd >>, << 14 % >>) - la colonne discrete s'efface (de toutes les lignes) ; le reste comme BarList.
class FitBarList : public ch::BarList {
public:
    using ch::BarList::BarList;
    void setData(ch::BarListData data);   // cache celui de BarList : les donnees entieres
    [[nodiscard]] bool extrasShown() const noexcept { return extras_ == 1; }

protected:
    void onLayout() override;

private:
    void            apply();
    ch::BarListData full_{};
    int             extras_{-1};   // -1 pas encore pose, 1 montrees, 0 effacees
};

// Une carte simple (le fond blanc, le trait, l'ombre de la maquette).
void paintCard(gfx::IRenderer& r, const ui::Theme& th, const gfx::Rect& b);
// Un bandeau (avertissement : ambre ; info : bleu) avec son icone et ses lignes ; rend sa hauteur.
float paintBanner(gfx::IRenderer& r, const ui::Theme& th, const gfx::Rect& b, const std::string& text, bool warning, float reserveRight = 0.f);
[[nodiscard]] float bannerHeight(const std::string& text, float width, float reserveRight = 0.f);

// ============================================================================ les vues (d'autres fichiers)
// Les mises a jour (planche 09) : PageStatsUpdates.cpp. La vue lit sa periode chez son parent (getChoice), et un clic sur
// << Voir les 119 dans Activations >> ouvre la liste.
class UpdatesView;
std::unique_ptr<ui::Widget> makeUpdatesView(ShellHost host, std::function<Choice()> getChoice, std::function<std::string()> getSoftware);
// Le globe de Statistiques (planche 11) et la petite carte du tableau de bord : PageStatsGlobe.cpp (l'agent du globe les
// remplit ; une place reservee en attendant).
std::unique_ptr<ui::Widget> makeStatsGlobeView(ShellHost host);
std::unique_ptr<ui::Widget> makeDashboardGlobe(ShellHost host);
// La rangee de courbes du tableau de bord (PageStatsStrip.cpp) : 4 petites cartes (le montant encaisse, les licences
// actives, les PC actifs, les demandes) sur 30 jours ; un clic : la page Statistiques. `reload()` : relire.
struct Reloadable {
    virtual ~Reloadable() = default;
    virtual void reload() = 0;
};
class DashboardStrip : public ui::Widget, public Reloadable {
public:
    using ui::Widget::Widget;
    [[nodiscard]] virtual bool shown() const = 0;   // faux : rien a montrer (pas de service, pas de cle)
};
std::unique_ptr<DashboardStrip> makeDashboardStrip(ShellHost host);
// Recharger une vue (les essais : apres avoir change les reponses d'essai).
void reloadView(ui::Widget& view);

} // namespace license_admin::gui::statskit
