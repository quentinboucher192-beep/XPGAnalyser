// =============================================================================
//  project/MacroSession.hpp - un lancement de macro, du formulaire a l'Appliquer
//  (lot macros 1)
// -----------------------------------------------------------------------------
//  LE FORMULAIRE MONTRE CE QUE LA MACRO DEMANDE VRAIMENT, AVEC LES REPONSES DU
//  MOMENT. La macro est rejouee en apercu (elle ne laisse rien derriere elle) ;
//  chaque question qu'elle atteint devient un champ, avec la valeur qu'elle
//  propose tant qu'on n'en a pas tape une autre. Une question qui depend d'une
//  reponse apparait des que la reponse la rend atteignable ; celle d'une
//  branche ecartee disparait.
//
//  LES TOURS. sim::execute ne se suspend pas : une question sans reponse rend
//  sa valeur proposee et la macro continue. Un tour qui va au bout ainsi EST
//  l'apercu avec les valeurs proposees ; un tour arrete par AskNow en appelle
//  un autre, avec ces valeurs. ImporterClasseur : deux tours (le classeur,
//  puis tout le reste), 0,3 s chacun sur le classeur d'exemple.
//
//  LA CONFIRMATION N'EST PLUS UNE QUESTION. Confirm() recoit "O" : c'est le
//  bouton Appliquer qui confirme, et son texte vient de "#! appliquer" (ou du
//  Confirm de la macro).
// =============================================================================
#pragma once

#include "Macro.hpp"
#include "MacroSpec.hpp"
#include "SharedLibrary.hpp"

#include <map>
#include <memory>
#include <set>
#include <string>
#include <vector>

namespace project::macro {

class MacroSession {
public:
    using ProjectPtr = std::shared_ptr<domain::Project>;
    using Answers    = std::map<std::string, std::string>;

    MacroSession(ProjectPtr project, std::string libsRoot, std::string name, std::string source);
    ~MacroSession();

    [[nodiscard]] const std::string& name() const noexcept { return name_; }
    [[nodiscard]] const std::string& source() const noexcept { return source_; }
    [[nodiscard]] const MacroSpec&   spec() const noexcept { return spec_; }
    [[nodiscard]] SharedLibrary&     library() noexcept { return *library_; }

    // ---- les reponses ---------------------------------------------------------
    // Tapee : elle reste jusqu'a ce qu'on l'efface.
    void setAnswer(const std::string& key, std::string value);
    // Revenir a la valeur que la macro propose.
    void clearAnswer(const std::string& key);
    // Les reponses du dernier lancement ("comme la derniere fois").
    void preload(const Answers& remembered);
    [[nodiscard]] const Answers& answers() const noexcept { return answers_; }
    [[nodiscard]] bool isRemembered(const std::string& key) const { return remembered_.count(key) != 0; }
    // Ce que recevra la macro : tapees, retenues, proposees, et "confirm" = O.
    [[nodiscard]] Answers effectiveAnswers() const;

    // ---- les tableaux CSV ("#! tableau", OpenTable) -----------------------------
    // Rend vide, ou pourquoi le fichier n'a pas pu etre lu.
    std::string setTable(const std::string& table, const std::string& path);
    [[nodiscard]] const std::map<std::string, std::string>& tablePaths() const noexcept { return tablePaths_; }

    // ---- un apercu ------------------------------------------------------------
    struct Field {
        enum class Origin : std::uint8_t { Proposed, Typed, Remembered };
        MacroQuestion question;          // tel que la macro l'a pose, ce tour-ci
        FieldSpec     spec;              // declare ("#! champ"), ou deduit du Ask
        std::string   value;             // ce qui sera envoye
        Origin        origin{Origin::Proposed};
        std::string   label;             // motif resolu
        std::string   group;
        bool          advanced{false};
    };
    struct Outcome {
        std::vector<Field> fields;       // dans l'ordre ou la macro les atteint
        MacroReport        report;       // le dernier tour
        bool               complete{false};   // alle au bout, chaque question ayant sa valeur
        bool               stopped{false};    // arrete par AskNow sans rien de neuf a proposer
        std::string        failure;
        std::string        confirm;      // le texte du Confirm de la macro
        std::size_t        rounds{0};
        double             seconds{0.0};
    };
    // Rejoue en apercu jusqu'a ce que les questions ne bougent plus (8 tours au
    // plus). `exact` : un dernier tour ou chaque valeur est une reponse - le
    // bilan est alors complet, fichiers annonces compris (avant Appliquer).
    const Outcome& refresh(bool exact = false);
    [[nodiscard]] const Outcome& outcome() const noexcept { return outcome_; }
    [[nodiscard]] const Field* field(const std::string& key) const;

    // Applique : un tour en mode Apply, avec effectiveAnswers(). nullptr : rien
    // a changer, ou un echec - `report` dit lequel. Une seule commande : un
    // seul Ctrl+Z.
    [[nodiscard]] core::CommandPtr apply(MacroReport& report);

    // Le texte du bouton Appliquer.
    [[nodiscard]] std::string applyText() const;

private:
    [[nodiscard]] FieldSpec specFor(const MacroQuestion&) const;

    ProjectPtr                      project_;
    std::unique_ptr<SharedLibrary>  library_;
    std::string                     name_, source_;
    MacroSpec                       spec_;
    Answers                         answers_;
    std::set<std::string>           remembered_;
    Answers                         proposed_;      // les valeurs proposees du dernier apercu
    std::map<std::string, std::string> tables_;     // nom -> octets CSV
    std::map<std::string, std::string> tablePaths_;
    Outcome                         outcome_;
};

} // namespace project::macro
