// =============================================================================
//  project/GrafcetRewrite2.hpp - 1.10, chantier R2 : les reecritures qui manquaient
// -----------------------------------------------------------------------------
//  GrafcetRewrite.* insere et supprime des etapes, insere des transitions et des
//  actions. Il manquait : supprimer une transition, supprimer une action,
//  modifier une action (genre, retard, nom, etape), et l'apercu des lignes du ST
//  qui changent. Meme facon de faire : chirurgicale (seules les lignes qui
//  doivent bouger bougent, octet pour octet, \r compris), annulable (le texte
//  d'avant de chaque section touchee est garde), sans exception (un refus est un
//  core::Status avec un message en francais, tutoiement).
//
//  PLANIFIER, PUIS APPLIQUER. Chaque commande calcule d'abord un plan (le texte
//  nouveau de chaque section touchee) SANS toucher au projet ; execute()
//  applique le plan, preview() le compare au texte actuel. L'apercu montre donc
//  exactement ce que execute() ecrira, et le demander ne modifie rien.
//
//  LES AUTRES SECTIONS. Les tableaux Trans_<x> / Acts_<x> ne sont pas nommes
//  que dans les deux sections du grafcet : SFC_DEBUG de MAST.XPG lit
//  Acts_PompageA[i]... Supprimer A3 et renumeroter A4.. dans les deux sections
//  seulement ferait lire a SFC_DEBUG l'action d'a cote, sans que rien ne le
//  montre. Donc la renumerotation passe aussi dans les sections de la meme unite
//  de programme ; et une suppression est REFUSEE si l'element supprime est
//  employe ailleurs que dans ses propres lignes (une receptivite qui lit
//  FIN(A2), SFC_DEBUG...) : le message dit ou. On ne remplace rien a la place
//  de l'utilisateur : une receptivite reecrite sans lui est une receptivite
//  fausse decouverte sur la machine.
// =============================================================================
#pragma once

#include "../core/Command.hpp"
#include "../domain/ProjectModel.hpp"
#include "Grafcet.hpp"

#include <memory>
#include <string>
#include <utility>
#include <vector>

namespace project {

    using ProjectPtr = std::shared_ptr<domain::Project>;

    // Une ligne du programme qui change, pour l'apercu ("ce qui va changer dans
    // le programme"). Une ligne modifiee a les deux numeros ; une ligne retiree
    // n'a que lineBefore ; une ligne ajoutee n'a que lineAfter. Numeros a partir
    // de 1, comme dans l'editeur de ST. Textes sans le \r final.
    struct StLineChange {
        domain::Index section{ domain::kNoIndex };
        std::string   sectionName;        // "SFC_DetoxalA"
        std::size_t   lineBefore{ 0 };    // dans le texte actuel ; 0 = ligne ajoutee
        std::size_t   lineAfter{ 0 };     // dans le texte nouveau ; 0 = ligne retiree
        std::string   before, after;

        [[nodiscard]] bool added() const noexcept { return lineBefore == 0; }
        [[nodiscard]] bool removed() const noexcept { return lineAfter == 0; }
    };

    // Le plan d'une reecriture : le texte nouveau, complet, de chaque section
    // touchee. `error` (francais) est rempli quand la reecriture est refusee ;
    // alors `bodies` est vide.
    struct GrafcetPlan {
        std::string error;
        core::ErrorCode code{ core::ErrorCode::None };
        std::vector<std::pair<domain::Index, std::string>> bodies;

        [[nodiscard]] bool ok() const noexcept { return error.empty(); }
    };

    // Ce qu'une action devient. Tous les champs sont donnes (comme RenameStepCommand) :
    // l'interface part de l'action telle qu'elle est (grafcet::Action) et change ce
    // que l'utilisateur a change. Un champ egal a l'actuel ne touche aucune ligne.
    struct ActionChange {
        std::string         name;               // n= (8 caracteres au plus)
        grafcet::ActionKind kind{ grafcet::ActionKind::Continuous };  // k=
        std::string         delay;              // d:= (texte ST verbatim, ex. t#5s)
        int                 boundStep{ -1 };    // s= (l'etape de l'action)

        // Partir d'une action lue : rien ne change tant qu'on ne touche a rien.
        [[nodiscard]] static ActionChange from(const grafcet::Action& a) {
            return ActionChange{ a.name, a.kind, a.delay, a.boundStep };
        }
    };

    // ---- les plans (purs : ne modifient rien) -----------------------------------
    [[nodiscard]] GrafcetPlan planRemoveTransition(const domain::Project&, domain::Index section,
        int transitionId);
    [[nodiscard]] GrafcetPlan planRemoveAction(const domain::Project&, domain::Index section,
        int actionId);
    [[nodiscard]] GrafcetPlan planModifyAction(const domain::Project&, domain::Index section,
        int actionId, const ActionChange&);

    // Les lignes qui changent entre le texte actuel et le plan (dans l'ordre des
    // sections du plan, puis des lignes). Vide si le plan est refuse ou ne change rien.
    [[nodiscard]] std::vector<StLineChange> previewPlan(const domain::Project&, const GrafcetPlan&);

    // Les lignes qui changent entre deux textes d'une section (diff ligne a ligne,
    // plus longue sous-suite commune ; une ligne retiree suivie d'une ligne
    // ajoutee au meme endroit est donnee comme une ligne modifiee).
    [[nodiscard]] std::vector<StLineChange> diffSectionText(domain::Index section,
        const std::string& sectionName, const std::string& before, const std::string& after);

    // L'apercu d'une commande QUELCONQUE (y compris celles de GrafcetRewrite.hpp :
    // InsertStepCommand...) : elle est executee, comparee, puis defaite, et le
    // texte de chaque section est remis octet pour octet. Modifie donc le projet le
    // temps de l'appel : a appeler depuis le fil de l'interface, pas pendant une
    // simulation qui lirait le texte. Rend false (et `error`) si la commande refuse.
    [[nodiscard]] bool previewCommand(domain::Project&, core::ICommand&,
        std::vector<StLineChange>& out, std::string& error);

    // ---------------------------------------------------------------------------
    //  Supprimer une transition.
    //
    //  Ses deux appels Builder(T:=2) (le descripteur et le retard), sa ligne
    //  Trans_<x>[i].Condition := ...; puis les transitions au-dessus descendent
    //  d'un cran : id= de leur descripteur, et Trans_<x>[k] partout dans l'unite.
    //  Les etapes ne bougent pas (s= et d= sont des etapes). Refusee si
    //  Trans_<x>[i] est employee ailleurs que dans ses propres lignes.
    // ---------------------------------------------------------------------------
    class RemoveTransitionCommand final : public core::ICommand {
    public:
        RemoveTransitionCommand(ProjectPtr project, domain::Index section, int transitionId);

        core::Status execute() override;
        core::Status undo() override;
        [[nodiscard]] std::string label() const override;

        // Les lignes qui changeront ; vide (et `error`) si la suppression est refusee.
        [[nodiscard]] std::vector<StLineChange> preview(std::string* error = nullptr) const;

        // Ce que la suppression laisse au grafcet, pour la boite de confirmation (en
        // francais) : "X3 n'aura plus de transition de sortie.", "X5 ne sera plus
        // atteinte." Les etapes visees gardent leur numero : seules les transitions
        // descendent.
        [[nodiscard]] std::vector<std::string> consequences() const;

    private:
        ProjectPtr    project_;
        domain::Index section_;
        int           transitionId_;
        std::vector<std::pair<domain::Index, std::string>> previous_;
        bool          applied_{ false };
    };

    // ---------------------------------------------------------------------------
    //  Supprimer une action.
    //
    //  Ses appels Builder(T:=3), sa ligne Acts_<x>[i].EnableCond := ...;, son bloc
    //  IF Acts_<x>[i].Out THEN ... END_IF; dans SFC_<x>_Actions (avec les deux
    //  lignes de commentaire qu'InsertActionCommand ecrit au-dessus, s'il les a
    //  ecrites ; les en-tetes d'etape du projet restent), puis la renumerotation des
    //  actions au-dessus (id=, Acts_<x>[k] partout dans l'unite, receptivites
    //  comprises). REFUSEE si une receptivite lit l'action (FIN(A2) =
    //  Acts_<x>[2].Finished, .Started...) ou si une autre section l'emploie :
    //  le message dit ou, et c'est a l'utilisateur de changer ces receptivites.
    // ---------------------------------------------------------------------------
    class RemoveActionCommand final : public core::ICommand {
    public:
        RemoveActionCommand(ProjectPtr project, domain::Index section, int actionId);

        core::Status execute() override;
        core::Status undo() override;
        [[nodiscard]] std::string label() const override;

        [[nodiscard]] std::vector<StLineChange> preview(std::string* error = nullptr) const;

        // Ou l'action est employee (pour la boite de confirmation) : "T2", "T3",
        // "SFC_DEBUG, ligne 120". Vide si rien ne l'emploie.
        [[nodiscard]] std::vector<std::string> usedBy() const;

    private:
        ProjectPtr    project_;
        domain::Index section_;
        int           actionId_;
        std::string   name_;   // pour l'etiquette, lu a la construction
        std::vector<std::pair<domain::Index, std::string>> previous_;
        bool          applied_{ false };
    };

    // ---------------------------------------------------------------------------
    //  Modifier une action : son genre (k=), son retard (d:=), son nom (n=), son
    //  etape (s=). Seuls les champs qui changent sont reecrits, champ par champ :
    //  ceux que cette version ne connait pas restent. Le retard est remplace dans
    //  son appel Builder(T:=3,d:=...) ; s'il n'y en a pas et qu'il en faut un, la
    //  ligne est ajoutee juste sous le descripteur, comme InsertActionCommand
    //  l'ecrit. Les deux commentaires qu'InsertActionCommand met au-dessus du
    //  bloc (etape, genre) suivent s'ils sont la ; ceux du projet ne sont pas
    //  touches. Le bloc IF ... END_IF ne bouge pas.
    // ---------------------------------------------------------------------------
    class ModifyActionCommand final : public core::ICommand {
    public:
        ModifyActionCommand(ProjectPtr project, domain::Index section, int actionId,
            ActionChange change);

        core::Status execute() override;
        core::Status undo() override;
        [[nodiscard]] std::string label() const override;

        [[nodiscard]] std::vector<StLineChange> preview(std::string* error = nullptr) const;

    private:
        ProjectPtr    project_;
        domain::Index section_;
        int           actionId_;
        ActionChange  change_;
        std::vector<std::pair<domain::Index, std::string>> previous_;
        bool          applied_{ false };
    };

} // namespace project
