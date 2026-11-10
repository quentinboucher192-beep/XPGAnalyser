// =============================================================================
//  license_admin/gui/GlobeStats.hpp - 1.47.0 : LE GLOBE DE STATISTIQUES, NOURRI
//  PAR LE SERVICE (planche 11 ; docs/STATISTIQUES.md, par. 8 et 13)
// -----------------------------------------------------------------------------
//  PageStatsGlobe.cpp. Les deux places que la page Statistiques et le tableau de
//  bord reservent (StatsKit.hpp) : statskit::makeStatsGlobeView (la vue << Le
//  globe >> : l'onglet Globe de Statistiques) et statskit::makeDashboardGlobe (la
//  petite carte du tableau de bord) ; et la sous-page << globe >> (Statistiques >
//  Le globe : le meme globe avec son titre - la petite carte y mene).
//
//  LA VUE (GlobeStatsView) :
//    - en haut : << Ou sont vos clients et leurs PC... - 54 clients, 342 PC, 21
//      villes, 9 pays >> et la periode (7 j, 30 j, 90 j, 12 mois : les commandes
//      de la periode) ;
//    - la carte du globe (GlobeBoard.hpp) : GET /admin/globe (Workspace::statsGlobe,
//      par runQuiet : jamais d'attente sur le fil de l'interface) - releve en
//      arrivant, a chaque periode, puis toutes les 5 minutes tant que la vue est
//      montree ;
//    - la carte d'une ville : << Voir les N clients >> ouvre Clients filtree sur
//      ses clients (la colonne Client), << Les N PC >> ouvre Activations filtree
//      sur leurs empreintes (les cles des PC que le service donne, retrouvees dans
//      l'archive de ce poste ; sinon les PC de ces clients qu'elle connait ; sinon
//      Clients filtree sur ces clients : les PC actives en ligne y sont, sous leur
//      licence) - la barre de filtres des listes (ui::FilterBar::setValues) ;
//    - EN DIRECT : GET /admin/globe/direct toutes les 6 secondes TANT QUE LA VUE EST
//      DESSINEE (une autre page, la fenetre reduite : rien ne part) ; chaque
//      nouvelle connexion fait pulser sa ville et s'ecrit au-dessus des villes ; le
//      globe est relu chaque minute (les PC actifs de chaque ville) ;
//    - COMPTE : les totaux de la periode ;
//    - plein ecran : la carte prend toute la vue ;
//    - un service d'avant la 1.47.0 : UN bandeau (stats::serviceTooOldText) et
//      << Deployer le service >> (le dialogue de la 1.45.0) ; pas regle, pas de cle
//      qui signe : le bandeau dit pourquoi, << Regler le service >> ; un refus :
//      sa phrase, << Reessayer >> ; aucune ville encore : la carte le dit au milieu
//      du globe (et combien de PC n'ont pas encore de lieu).
//  Les identifiants (les scripts) : globe.periode.<7j|30j|90j|12mois>, globe.deployer,
//  globe.regler, globe.reessayer, et ceux de la carte (GlobeBoard : globe.*).
//
//  LES CAPTURES SANS SERVICE : XPG_STATS_ESSAI=1 (comme les autres statistiques)
//  - les villes d'essai de la maquette, et une connexion d'essai toutes les 6 s en
//  direct. Jamais sans cette variable.
// =============================================================================
#pragma once

#include "GlobeBoard.hpp"
#include "Page.hpp"
#include "StatsKit.hpp"

#include "../Stats.hpp"

#include <functional>
#include <memory>
#include <set>
#include <string>
#include <vector>

namespace license_admin::gui::globestats {

// Les reponses d'essai (les essais) : chaque appel rendu par une fonction au lieu du service ; vide : le service.
struct Fetchers {
    std::function<OpResult(int periodDays, stats::GlobeReply&)> globe;
    std::function<OpResult(Seconds since, stats::LiveReply&)>   live;
};
void setFetchersForTests(Fetchers f);

// Une ville de la reponse -> une Place du globe (champ pour champ ; l'id << FR|Lyon >>).
[[nodiscard]] ui::globe::Place placeOf(const stats::GlobeCity& c);
[[nodiscard]] std::vector<ui::globe::Place> placesOf(const stats::GlobeReply& r);
// << Lyon, FR >> depuis l'id << FR|Lyon >> (une connexion d'une ville pas encore relue).
[[nodiscard]] std::string labelOfId(const std::string& id);
// Les villes d'essai de la maquette en reponse du service (XPG_STATS_ESSAI=1 ; les essais).
[[nodiscard]] stats::GlobeReply sampleReply(int periodDays, Seconds now);
[[nodiscard]] bool sampleMode();   // XPG_STATS_ESSAI=1

class GlobeStatsView final : public ui::Widget, public statskit::Reloadable {
public:
    // `title` : la sous-page (<< Le globe >> en grand, comme la planche) ; faux : l'onglet de Statistiques.
    GlobeStatsView(ShellHost host, bool title);
    ~GlobeStatsView() override;

    void reload() override;                          // relire tout de suite (la periode, le globe)
    void setPeriodDays(int days);                    // 7, 30, 90, 365
    [[nodiscard]] int periodDays() const noexcept { return days_; }
    [[nodiscard]] GlobeBoard& board() noexcept { return *board_; }
    [[nodiscard]] const stats::GlobeReply& reply() const noexcept { return reply_; }
    [[nodiscard]] bool hasReply() const noexcept { return hasReply_; }
    [[nodiscard]] const std::string& problem() const noexcept { return problem_; }   // vide : tout va bien
    [[nodiscard]] bool tooOld() const noexcept { return tooOld_; }
    [[nodiscard]] int livePolls() const noexcept { return livePolls_; }            // les essais : les releves en direct partis
    // Le temps qui passe (la peinture l'appelle avec l'heure de l'image ; les essais aussi) : les releves dus partent.
    void tick(double now);
    // Ce que font les boutons de la carte d'une ville (les essais) ; rend l'avis dit.
    std::string openClients(int place);
    std::string openPcs(int place);

    [[nodiscard]] ui::SizeHint sizeHint() const override;

protected:
    void onLayout() override;
    void onPaint(const ui::PaintContext& ctx) override;

private:
    void fetch();
    void pollLive();
    void adopt(const stats::GlobeReply& r);
    void adoptLive(const stats::LiveReply& r);
    void syncState();
    [[nodiscard]] std::string topLine() const;

    ShellHost                    host_;
    bool                         title_{false};
    GlobeBoard*                  board_{nullptr};
    statskit::SegGroup*          period_{nullptr};
    ui::Button *                 deploy_{nullptr}, *settings_{nullptr}, *retry_{nullptr};
    stats::GlobeReply            reply_;
    bool                         hasReply_{false}, fetching_{false}, liveFetching_{false}, tooOld_{false}, wantFetch_{true};
    std::string                  problem_;
    int                          days_{30};
    double                       now_{-1.0}, lastFetch_{-1e9}, nextLive_{0.0}, retryAt_{0.0};
    Seconds                      liveSince_{0};
    std::set<std::string>        seenPings_;
    int                          livePolls_{0}, samplePing_{0};
    gfx::Rect                    bannerRect_{};
    std::shared_ptr<bool>        alive_ = std::make_shared<bool>(true);
    core::ConnectionScope        links_;
};

} // namespace license_admin::gui::globestats
