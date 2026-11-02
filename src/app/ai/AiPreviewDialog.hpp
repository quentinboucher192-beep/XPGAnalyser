// =============================================================================
//  app/ai/AiPreviewDialog.hpp - l'Assistant IA : la fenetre de previsualisation
// -----------------------------------------------------------------------------
//  CE QUI CHANGERAIT, AVANT QUE RIEN NE CHANGE. Une fenetre du logiciel (elle se
//  deplace et s'agrandit), sur une proposition :
//
//    en haut     la vue visee, la demande ; les comptes - creer, modifier,
//                supprimer ;
//    a gauche    les objets concernes, leurs proprietes (avant -> apres), les
//                variables associees, les avertissements et les erreurs ; une
//                variable introuvable : la liste des variables existantes (les
//                plus proches d'abord) et "Creer ..." - c'est l'utilisateur qui
//                decide, jamais l'Assistant ;
//    a droite    L'APERCU : une COPIE de la vue, modifiee, dessinee par les
//                painters du logiciel ; les objets nouveaux encadres en vert, les
//                modifies en bleu, ceux a supprimer barres de rouge ;
//    en bas      1.22.7 : Retour (fermer sans rien faire), Modifier la demande (le
//                texte revient dans la saisie ; la proposition reste a decider),
//                Refuser... (une question d'abord), Appliquer (un seul Ctrl+Z ;
//                une suppression multiple se confirme). Echap ferme sans decider.
//
//  1.21.0 : LES SUPPRESSIONS (AiDelete) - une categorie en tete de la liste :
//  chaque suppression, sa strategie, le resume par genre (<< 4 variables
//  directement concernees >>), ce qui part, ce qui en depend (directe,
//  indirecte, potentielle, partagee), les consequences (references retirees ou
//  remplacees, orphelins gardes). Un clic sur une suppression la RETIRE du plan
//  (ou l'y remet) : le geste de la bande du plan (Assistant::setExcluded) - ce
//  qui en depend est ignore, la verification se refait.
//
//  1.22.1 : LE CLIC DROIT SUR UNE ERREUR (AiFixes.hpp) - le menu du clic droit
//  propose des corrections adaptees (une variable introuvable : utiliser une
//  proche, la choisir, la creer, retirer ce qui la lit ; une operation en erreur :
//  la retirer ; copier ; demander a l'IA). La proposition change (jamais le
//  projet), se revalide, l'erreur disparait ; la categorie CORRECTIONS dit ce qui
//  a ete fait, et le clic droit sur une correction la reprend (Revenir). Deux
//  erreurs identiques : une ligne << ... (2 operations) >>. Le choix du haut vaut
//  pour UNE variable introuvable (la premiere, ou celle qu'on a demandee) - les
//  autres gardent leur erreur jusqu'a leur propre choix.
//
//  1.22.4 : OU SONT LES ERREURS. Chaque objet de l'apercu vise par une erreur est
//  encadre de rouge, par un avertissement d'ambre (un cadre plus epais, une
//  pastille !) ; la legende le dit. A gauche, les lignes liees a une erreur ou a
//  un avertissement (les messages, les etapes, les objets, les variables, les
//  types) en ont la couleur et la pastille ; une RECHERCHE (le champ de l'appli :
//  Ctrl+F y met le clavier, Echap la vide) filtre les lignes de toutes les
//  categories, avec << 12 lignes >> ; deux PUCES, << Erreurs (n) >> et
//  << Avertissements (n) >>, ne montrent que ces lignes. CHOISIR UNE LIGNE ENCADRE
//  DANS L'APERCU CE QU'ELLE DESIGNE : un objet, les objets qui lisent une variable
//  ou un type, ceux d'une etape, ceux d'un message - un cadre de la couleur
//  d'accent qui pulse une seconde, l'apercu s'y recentre (et zoome si besoin) ; un
//  clic sur un objet de l'apercu choisit sa ligne. Ce que designe chaque ligne est
//  calcule une fois par proposition (Check::spots : les objets de chaque operation
//  et de chaque message), jamais a chaque image.
//
//  1.22.5 : L'AVIS DU LOGICIEL (AiBuild.hpp) - une categorie << GENERER ET
//  COMPILER >> apres les avertissements : chaque constat NOUVEAU de Generer et de
//  Compiler sur une copie du projet ou la proposition est appliquee (l'etape,
//  l'endroit, le message ; rouge ou ambre), sa ligne de titre dit le resume
//  (<< 2 erreurs, 1 avertissement nouveaux, 1,4 s >>) ou << en cours >>
//  tant que le verdict n'est pas la. Il arrive par setVerdict (la verification de
//  fond de l'Assistant) ou par la proposition elle-meme (Proposal::extra
//  << logiciel >>, relu a chaque image : un nouveau verdict s'y lit) ; il est verse
//  dans la verification (build::merge : ses erreurs comptent, ses objets
//  s'encadrent comme les autres) - la liste et ses reperes se refont, rien n'est
//  revalide. Une proposition corrigee depuis (une autre cle) : le verdict ne vaut
//  plus, << en cours >> jusqu'au suivant. Choisir une ligne encadre son objet ; le
//  clic droit : Copier, Retirer l'operation, Demander a l'IA de corriger (la
//  correction du logiciel dans le texte prepare, quand il en a une).
//
//  1.22.6 : TOUTES LES VUES DE LA PROPOSITION. Une proposition peut creer
//  plusieurs vues (une vue, une popup, un symbole) et toucher des objets d'autres
//  vues : chacune a son apercu (ai::previews - un passage sur une copie du projet,
//  ici ou sur un fil de fond pour une grosse proposition, comme la verification
//  de la carte). Au-dessus de l'apercu, LA BARRE DES VUES : << Vue 2/4 :
//  Popup_Vanne (neuve, popup) - 1 erreur >>, ses fleches (PageUp / PageDown,
//  Ctrl+gauche / droite), un clic sur le nom ouvre la liste des vues (ce que
//  chacune cree, modifie, supprime ; ses erreurs). A gauche, chaque ligne qui
//  designe des objets dit SUR QUELLE VUE ils sont (le nom en petit a droite :
//  marque d'une pastille d'accent pour la vue montree, grise pour une autre) ;
//  choisir une ligne d'une autre vue y fait passer l'apercu, puis l'encadre ; la
//  puce << Cette vue seulement >> ne garde que les lignes de la vue montree ; les
//  titres disent << (12, 3 sur cette vue) >>. Les cadres (nouveau, modifie,
//  supprime, erreur, avertissement, la ligne choisie) valent pour la vue montree.
//
//  1.22.7 : RETOUR ET CORRIGER RAPIDEMENT. En bas a gauche, << Retour >> (discret)
//  ferme la fenetre SANS RIEN FAIRE (comme Echap quand aucune recherche n'est en
//  cours) : ni refus, ni application, la saisie intacte ; la proposition reste << a
//  decider >>. << Refuser... >> (1.16.1) dit, lui, qu'il refuse. Le clic droit sur
//  CHAQUE erreur et CHAQUE avertissement (GENERER ET COMPILER compris, toutes les
//  vues) offre en tete << Corriger rapidement >> : la meilleure correction SURE
//  (AiFixes.hpp, quick : une variable nettement la plus proche, la correction du
//  logiciel, << references >> pour une suppression refusee, retirer l'operation...)
//  ou grisee avec la raison. La fenetre RESTE OUVERTE : le menu se ferme, la
//  verification et l'apercu se refont (la vue montree reste la meme), la ligne part
//  des erreurs, la categorie CORRECTIONS gagne sa ligne, mise en evidence un instant
//  (<< ... (corrige rapidement) >>), les objets touches s'encadrent (le cadre qui
//  pulse de la 1.22.4), et une BANDE en haut de la liste dit << Corrige : ... >> et
//  << Revenir >> (un clic la reprend). Les AUTRES corrections du menu (Utiliser,
//  Creer, Retirer, la correction du logiciel, Revenir) font de meme ; seule
//  << Demander a l'IA de corriger >> ferme (le texte va dans la saisie). << TOUT
//  CORRIGER RAPIDEMENT >> (le lien des titres ERREURS, AVERTISSEMENTS, GENERER ET
//  COMPILER ; leur clic droit) : toutes les corrections sures d'un coup, et le
//  compte (<< 6 erreurs corrigees, 2 restent : ... >>). Une grosse proposition
//  (checkInBackground) se reverifie sur un fil de fond : la liste d'avant reste
//  montree, la bande dit << Reverification... >>, la fenetre garde la main.
//
//  1.16.1 : L'APERCU SE ZOOME (la molette autour de la souris, les boutons -,
//  Ajuster, +, Ctrl+plus / moins / 0 ; zoome, il se deplace au glisser ; un
//  double-clic l'ajuste) ; une INFOBULLE sur chaque objet (son nom, son genre,
//  nouveau / modifie / supprime et ce qui change, sa place) ; la LISTE DE GAUCHE
//  se replie par groupe (le clic sur un titre) et se masque toute (le bouton
//  entre elle et l'apercu) pour laisser la largeur a l'apercu.
// =============================================================================
#pragma once

#include "AiBuild.hpp"
#include "AiFixes.hpp"
#include "AiOperations.hpp"
#include "../../menu/IMenu.hpp"

#include <map>
#include <memory>
#include <set>
#include <string>
#include <utility>
#include <vector>

namespace ui { class Button; class DropDown; class PopupMenu; class SearchField; }

namespace app::ai {

class AiPanel;

class AiPreviewDialog final : public menu::WidgetMenu {
public:
    AiPreviewDialog(AiPanel& panel, int message);
    ~AiPreviewDialog() override;   // 1.22.6 : le calcul des vues en cours, attendu

    [[nodiscard]] menu::MenuTraits traits() const override;
    [[nodiscard]] std::string      title() const override { return "Pr\xC3\xA9visualisation des modifications"; }
    ui::EventResult                HandleEvent(const ui::InputEvent& ev) override;
    void                           Update(const menu::FrameContext& f) override;

    // Les gestes (essais, scripts).
    bool apply();
    void cancel();          // Refuser... : la proposition est refusee (une question d'abord)
    void editRequest();     // Modifier la demande
    void close();           // Echap, 1.22.7 : Retour - sans rien decider ni toucher
    [[nodiscard]] ui::Button* backButton() const noexcept { return back_; }     // 1.22.7 : Retour
    [[nodiscard]] ui::Button* cancelButton() const noexcept { return cancel_; } // Refuser...
    [[nodiscard]] bool waiting() const noexcept { return waiting_; }   // 1.20.1 : la verification de fond n'est pas finie
    bool chooseVariable(int index);   // dans la liste deroulante (le dernier : creer)
    // 1.21.0 : retirer (ou remettre) une suppression (l'operation `op`, une operation supprimer) - comme la bande du plan.
    bool excludeRemoval(std::size_t op, bool excluded);
    // Les lignes de la categorie Suppressions (leur texte), dans l'ordre (essais).
    [[nodiscard]] std::vector<std::string> removalRows() const;

    [[nodiscard]] int                message() const noexcept { return message_; }
    [[nodiscard]] const Check&       check() const noexcept { return check_; }
    [[nodiscard]] const Preview&     preview() const noexcept { return preview_; }
    [[nodiscard]] ui::Button*        applyButton() const noexcept { return apply_; }
    [[nodiscard]] ui::DropDown*      variableChoice() const noexcept { return variable_; }
    [[nodiscard]] std::vector<std::string> variableItems() const;

    // 1.16.1 : le zoom (1 : la vue entiere ; jusqu'a 10), les groupes de la liste ("OBJETS CONCERN\xC3\x89S", "ERREURS"...),
    // la liste masquee, l'infobulle sous un point de l'ecran ; les places (essais).
    void  setPreviewZoom(float z);
    [[nodiscard]] float previewZoom() const;
    bool  foldGroup(const std::string& heading, bool folded);
    [[nodiscard]] bool groupFolded(const std::string& heading) const;
    [[nodiscard]] std::size_t shownRows() const;              // les lignes de la liste montrees (groupes replies : leur titre)
    void  setListHidden(bool hidden);
    [[nodiscard]] bool listHidden() const;
    [[nodiscard]] std::string previewTooltip(gfx::Point at) const;
    [[nodiscard]] gfx::Rect  previewArea() const;
    [[nodiscard]] gfx::Rect  listArea() const;
    [[nodiscard]] gfx::Rect  groupRect(const std::string& heading) const;
    [[nodiscard]] gfx::Point objectPoint(const std::string& name) const;   // le centre d'un objet de l'apercu a l'ecran

    // 1.22.1 : LE CLIC DROIT SUR UNE ERREUR (AiFixes.hpp) - les lignes telles qu'elles se lisent (les doublons regroupes), celles
    // des corrections faites ; le menu d'une ligne (au milieu de la liste), ses entrees (un sous-menu : "parent > enfant"),
    // en choisir une comme un clic ; la variable introuvable que vise le choix du haut (focusVariable : << Choisir une
    // variable... >>). Pour les essais et les scripts ; la souris passe par les memes gestes.
    [[nodiscard]] std::vector<std::string> errorRows() const;
    [[nodiscard]] std::vector<std::string> warningRows() const;
    [[nodiscard]] std::vector<std::string> correctionRows() const;
    bool openErrorMenu(std::size_t line);
    bool openWarningMenu(std::size_t line);
    bool openCorrectionMenu(std::size_t line);
    [[nodiscard]] bool menuOpen() const;
    [[nodiscard]] std::vector<std::string> menuItems() const;
    bool chooseMenuItem(const std::string& label);
    void focusVariable(const std::string& name, bool openList = false);   // openList : sa liste s'ouvre aussi
    [[nodiscard]] const std::string& variableShown() const noexcept { return missingName_; }

    // 1.22.4 : OU SONT LES ERREURS - le niveau d'un objet de l'apercu (0 : rien ; 1 : un avertissement ; 2 : une erreur) ;
    // l'objet de l'apercu de ce nom (kNoId : aucun) ; la recherche et les puces (Erreurs, Avertissements) ; les lignes
    // montrees (titres compris) et celles qui repondent au filtre (<< 12 lignes >> ; 0 sans filtre) ; choisir une ligne (celle
    // qui dit `part`, sinon la premiere qui le contient ; son groupe se deplie), ce qu'elle encadre dans l'apercu ; un clic sur
    // un objet de l'apercu (il choisit sa ligne) ; la place d'une puce. Pour les essais et les scripts ; la souris et le
    // clavier passent par les memes gestes.
    [[nodiscard]] int                      issueOf(hmi::Id id) const;
    [[nodiscard]] hmi::Id                  objectId(const std::string& name) const;
    void                                   setSearch(const std::string& text);
    [[nodiscard]] std::string              searchText() const;
    void                                   setIssueFilter(bool errors, bool warnings);
    [[nodiscard]] bool                     errorFilter() const;
    [[nodiscard]] bool                     warningFilter() const;
    [[nodiscard]] std::vector<std::string> visibleRows() const;
    [[nodiscard]] std::size_t              matchCount() const;
    bool                                   selectRow(const std::string& part);
    [[nodiscard]] std::string              selectedRow() const;
    [[nodiscard]] std::vector<hmi::Id>     highlighted() const;
    bool                                   clickObject(hmi::Id id);
    [[nodiscard]] gfx::Rect                chipRect(bool errors) const;
    [[nodiscard]] ui::SearchField*         searchField() const noexcept { return search_; }

    // 1.22.5 : L'AVIS DU LOGICIEL - le verdict de fond de la proposition (Generer et Compiler sur une copie) : la categorie
    // GENERER ET COMPILER se refait, ses constats sont verses dans la verification (les cadres, les puces, Appliquer) ; sans
    // tout recalculer (ni validation, ni apercu). Avant lui : << en cours >>. Les lignes de la categorie (les constats, dans
    // l'ordre), le menu d'une ligne (essais et scripts).
    void setVerdict(const build::Verdict& verdict);
    [[nodiscard]] bool                     verdictKnown() const noexcept { return verdictKnown_; }
    [[nodiscard]] const build::Verdict&    verdict() const noexcept { return verdict_; }
    [[nodiscard]] std::vector<std::string> buildRows() const;
    bool                                   openBuildMenu(std::size_t line);

    // 1.22.6 : TOUTES LES VUES - le nombre de vues de la proposition (0 : aucune a dessiner), celle qui est montree (son
    // rang), le calcul encore en cours (une grosse proposition : sur un fil de fond) ; montrer une vue (le zoom revient a
    // Ajuster), la suivante (+1) ou la precedente (-1) - faux : hors de la liste, au bout ; le titre d'une vue (<< Vue 2/4 :
    // Popup_Vanne (neuve, popup) - 1 erreur >>), celui de la barre ; la liste des vues (un menu : << titre | ce qu'elle
    // change >>) ; la vue que dit une ligne de la liste (son nom, << 2 vues >>, vide : aucune) et si c'est la vue montree ;
    // la puce << Cette vue seulement >> ; les places (la barre : -1 la fleche precedente, 0 le nom, 1 la suivante). Pour les
    // essais et les scripts ; la souris et le clavier (PageUp, PageDown, Ctrl+gauche / droite) passent par les memes gestes.
    [[nodiscard]] std::size_t              viewCount() const noexcept;
    [[nodiscard]] std::size_t              shownView() const noexcept { return shown_; }
    [[nodiscard]] bool                     viewsPending() const noexcept { return viewJob_ != nullptr; }
    bool                                   showView(std::size_t rank);
    bool                                   stepView(int delta);
    [[nodiscard]] std::string              viewTitle(std::size_t rank) const;
    [[nodiscard]] std::string              viewBarText() const;
    bool                                   openViewMenu();
    [[nodiscard]] bool                     viewMenuOpen() const;
    [[nodiscard]] std::vector<std::string> viewMenuItems() const;
    [[nodiscard]] std::string              rowView(const std::string& text) const;
    [[nodiscard]] bool                     rowOnShownView(const std::string& text) const;
    void                                   setViewFilter(bool on);
    [[nodiscard]] bool                     viewFilter() const;
    [[nodiscard]] gfx::Rect                viewBarRect(int part) const;
    [[nodiscard]] gfx::Rect                viewChipRect() const;

    // 1.22.7 : CORRIGER RAPIDEMENT - `list` 1 : une erreur, 2 : un avertissement, 4 : un constat du logiciel (GENERER ET
    // COMPILER) ; `line` : son rang (errorRows, warningRows, buildRows). Ce que ferait << Corriger rapidement >> (grisee : `why`) ;
    // le faire (comme le menu : la fenetre reste ouverte) ; << Tout corriger rapidement >> d'une categorie (le nombre de lignes
    // corrigees) et combien elle en offre ; le clic droit d'un titre ; pourquoi une entree du menu ouvert est grisee (vide :
    // choisissable, ou absente). LA BANDE (<< Corrige : ... >> ; vide : aucune) et son << Revenir >> ; les lignes de
    // CORRECTIONS mises en evidence par la derniere correction ; la reverification de fond en cours (une grosse proposition).
    // Pour les essais et les scripts ; la souris passe par les memes gestes.
    [[nodiscard]] fixes::Action            quickFor(int list, std::size_t line) const;
    bool                                   quickFix(int list, std::size_t line);
    std::size_t                            quickFixAll(int list);
    [[nodiscard]] std::size_t              quickCount(int list) const;
    bool                                   openHeadingMenu(int list);
    [[nodiscard]] std::string              menuWhy(const std::string& label) const;
    [[nodiscard]] std::string              bandText() const;
    bool                                   undoBand();
    [[nodiscard]] std::vector<std::string> flashedRows() const;
    [[nodiscard]] bool                     rechecking() const noexcept { return rechecking_; }
    [[nodiscard]] gfx::Rect                bandUndoRect() const;
    [[nodiscard]] gfx::Rect                quickAllRect(int list) const;

protected:
    core::Status buildUi() override;

private:
    friend class AiPreviewBody;
    void refresh();
    void finish(bool ok);
    // 1.22.1 : le menu d'une ligne (`list` 1 : une erreur, 2 : un avertissement, 3 : une correction), au point `at`.
    bool openFixMenu(int list, std::size_t line, gfx::Point at);
    bool openGroupMenu(int list, gfx::Point at);   // 1.22.7 : le clic droit d'un titre (1, 2, 4) - Tout corriger rapidement
    void runFix(int id);
    // 1.22.7 : la ligne `line` de la categorie `list` (1, 2, 4) telle que le menu la lit ; faire des corrections SANS FERMER (la
    // proposition change, la verification et l'apercu se refont, ce qui a change se montre : la bande, la ligne de CORRECTIONS,
    // les objets encadres) - `quick` : des corrections rapides ; `headline` : ce que la bande dit (vide : la correction elle-
    // meme) ; la mise en evidence des lignes de CORRECTIONS de ces cles (apres la verification).
    [[nodiscard]] bool lineOf(int list, std::size_t line, fixes::Line& out) const;
    void corrected(const std::map<std::string, std::string>& before, bool quick, const std::string& headline);
    [[nodiscard]] std::map<std::string, std::string> correctionState() const;
    void flash();

    AiPanel&      panel_;
    int           message_;
    Check         check_;
    Preview       preview_;
    std::string   key_;
    ui::Button*   back_{nullptr};   // 1.22.7 : Retour
    ui::Button*   edit_{nullptr};
    ui::Button*   cancel_{nullptr};
    ui::Button*   apply_{nullptr};
    ui::DropDown* variable_{nullptr};
    std::vector<std::string> variableNames_;   // les noms de la liste ("" : creer)
    std::string   missingName_;                // la variable introuvable (gardee apres un choix)
    bool          done_{false};
    bool          waiting_{false};    // 1.20.1 : la verification de fond de la carte n'est pas finie
    bool          planReady_{false};
    double        lastWait_{-1.0};
    bool          syncing_{false};
    bool          planFolded_{false};   // 1.19.0 : les categories du plan repliees a la premiere ouverture
    // 1.22.1 : le clic droit (le menu, ses actions par id), les lignes d'erreur, d'avertissement et de correction montrees ;
    // le choix du haut demande pour une variable (pinned_) et sa liste a ouvrir au prochain Update.
    ui::PopupMenu*                  fixMenu_{nullptr};
    std::vector<fixes::Action>      fixActions_;
    std::vector<fixes::Line>        errorLines_, warningLines_;
    std::vector<fixes::Correction>  corrections_;
    std::string                     createType_{"BOOL"};   // le type de << Creer ... >> dans la liste du haut
    std::size_t                     otherMissing_{0};      // les autres variables introuvables (sans choix)
    bool                            pinned_{false};
    bool                            openChoice_{false};
    // 1.22.7 : CORRIGER RAPIDEMENT - ce que ferait << Corriger rapidement >> pour chaque ligne (calcule une fois par
    // verification : les erreurs, les avertissements, les constats du logiciel) ; les corrections faites ainsi (leurs cles,
    // << (corrige rapidement) >>) ; la bande (son texte, ses Revenir, les cles qu'elle reprend) ; les lignes a mettre en evidence
    // (au prochain rafraichissement complet) ; une liste deja montree (une reverification la garde) ; la reverification en cours.
    std::vector<fixes::Action>      quickErrors_, quickWarnings_, quickBuild_;
    std::set<std::string>           quickKeys_;
    std::string                     bandText_;
    std::vector<fixes::Action>      bandUndo_;
    std::vector<std::string>        bandKeys_;
    std::vector<std::string>        flashKeys_;
    bool                            flashPending_{false};
    bool                            shownOnce_{false};
    bool                            rechecking_{false};
    [[nodiscard]] const std::vector<fixes::Action>& quickOf(int list) const;
    // 1.22.4 : la recherche de la liste ; calcules une fois par proposition (refresh) - le rang de chaque objet de l'apercu,
    // le niveau de chaque objet en cause et ses messages (son infobulle). 1.22.6 : ceux de la vue montree (useShownView).
    ui::SearchField*                         search_{nullptr};
    std::map<hmi::Id, std::size_t>           objectIndex_;
    std::map<hmi::Id, int>                   issues_;
    std::map<hmi::Id, std::vector<std::string>> issueTexts_;
    // 1.22.5 : l'avis du logiciel - le verdict (connu ou non), le dernier lu dans la proposition (Proposal::extra << logiciel >>),
    // les constats montres (la categorie, le clic droit : sa liste 4).
    build::Verdict                           verdict_;
    bool                                     verdictKnown_{false};
    json::Value                              verdictSeen_;
    std::vector<build::Finding>              buildLines_;
    void                                     adoptStoredVerdict(const Proposal& p, bool forget);
    // 1.22.6 : TOUTES LES VUES - l'apercu de la vue principale (celui de la carte) ; celui de chaque vue (calcule une fois par
    // proposition et par etat du projet, viewsKey_ : ici, ou sur un fil de fond, viewJob_) ; la vue montree (son rang ; son
    // nom, choisi a la main, gardee d'un calcul a l'autre) ; les erreurs et les avertissements par vue et par objet (le
    // niveau, les messages), leur compte par vue ; la liste des vues (un menu). `preview_` est l'apercu de la vue montree.
    struct ViewJob;
    struct ViewStats {
        std::size_t errors{0}, warnings{0};
    };
    Preview                                  main_;
    std::vector<ViewPreview>                 views_;
    std::size_t                              shown_{0};
    std::string                              shownName_;
    std::string                              viewsKey_;
    std::shared_ptr<ViewJob>                 viewJob_;
    std::vector<std::shared_ptr<ViewJob>>    retiring_;
    std::map<std::pair<std::size_t, hmi::Id>, int>                      viewIssues_;
    std::map<std::pair<std::size_t, hmi::Id>, std::vector<std::string>> viewIssueTexts_;
    std::vector<ViewStats>                   viewStats_;
    ui::PopupMenu*                           viewMenu_{nullptr};
    void                                     ensureViews(const Proposal& p);
    void                                     pollViews();
    void                                     useShownView();   // preview_, objectIndex_, issues_ : ceux de la vue montree
    [[nodiscard]] std::string                viewName(std::size_t rank) const;
    // 1.22.8 : la vie de la fenetre (<< Creer l'unite... >> s'ouvre par-dessus : son retour ne touche qu'une fenetre vivante).
    std::shared_ptr<int>                     life_{std::make_shared<int>(0)};
    core::ConnectionScope links_;
};

} // namespace app::ai
