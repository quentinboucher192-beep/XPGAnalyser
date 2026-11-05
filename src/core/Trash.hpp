// =============================================================================
//  core/Trash.hpp - 1.25.1 : la corbeille d'un magasin de fichiers
// -----------------------------------------------------------------------------
//  Le client (05/11) : << corrige les limites restantes >> - ce qui partait du
//  disque << definitivement >> (les incidents, les listes de l'Assistant IA, les
//  versions) revient maintenant par UN Ctrl+Z.
//
//  UNE CORBEILLE, C'EST UN DOSSIER DU MAGASIN : supprimer y deplace le fichier
//  (ou le dossier) sous le nom "<date>~<son nom>" (la date en temps universel :
//  20261105-173012). Le magasin ne la montre jamais (son nom commence par un
//  point, ou elle est hors des dossiers qu'il lit). Ctrl+Z l'en ressort, a sa
//  place et sous son nom ; un element du meme nom revenu entretemps : Ctrl+Z le
//  dit et laisse la corbeille.
//
//  LA PURGE : au demarrage suivant (ou a l'ouverture du magasin), ce qui y est
//  depuis plus de kKeepDays jours part pour de bon. Rien d'autre n'y touche :
//  une corbeille n'est pas une sauvegarde, c'est le temps d'un Ctrl+Z.
//
//  Les chemins sont en UTF-8 (comme partout dans l'application).
// =============================================================================
#pragma once

#include "Command.hpp"

#include <cstdint>
#include <functional>
#include <string>
#include <vector>

namespace core::trash {

inline constexpr int kKeepDays = 7;

// "20261105-173012" : l'heure universelle de `seconds` (0 : maintenant).
[[nodiscard]] std::string stamp(std::int64_t seconds = 0);
// Le nom d'origine d'un element de la corbeille ("20261105-173012~Pompe.json" -> "Pompe.json") ; tel quel sinon.
// (Deux du meme nom a la meme seconde : le second porte " (2)", que ce nom garde.)
[[nodiscard]] std::string originalName(const std::string& trashedName);
// Depuis quand (secondes depuis 1970, temps universel) ; -1 : pas un nom de corbeille.
[[nodiscard]] std::int64_t trashedAt(const std::string& trashedName);

// Deplacer un fichier ou un dossier (un autre volume : copier puis effacer). Faux et `why` sinon.
bool move(const std::string& fromUtf8, const std::string& toUtf8, std::string* why = nullptr);
// Mettre `itemUtf8` dans la corbeille `dirUtf8` (creee au besoin). `trashed` : ou il est. Faux et `why` sinon.
bool put(const std::string& itemUtf8, const std::string& dirUtf8, std::string& trashed, std::string* why = nullptr);
// Le rendre a `originalUtf8` ; refuse si quelque chose y est deja (`why` le dit).
bool takeBack(const std::string& trashedUtf8, const std::string& originalUtf8, std::string* why = nullptr);
// Ce qui est dans la corbeille depuis plus de `days` jours part pour de bon. Rend combien. `now` : 0, l'horloge.
std::size_t purge(const std::string& dirUtf8, int days = kKeepDays, std::int64_t now = 0);
// Les elements de la corbeille (leurs chemins), du plus recent au plus ancien.
[[nodiscard]] std::vector<std::string> list(const std::string& dirUtf8);

// Ce qu'un geste a mis dans une corbeille : d'ou il vient, ou il est.
struct Moved {
    std::string original, trashed;
};
// Tout rendre (Ctrl+Z) : ce qui ne revient pas est dit dans `why` (les autres reviennent quand meme). Faux : un au moins.
bool takeBackAll(const std::vector<Moved>& moved, std::string* why = nullptr);
// Tout remettre dans la corbeille (Ctrl+Y) : chacun dans celle d'ou il venait (`dirUtf8` s'il n'en venait d'aucune) ;
// `moved` suit (les nouveaux `trashed`).
bool putAll(std::vector<Moved>& moved, const std::string& dirUtf8, std::string* why = nullptr);

// ---- une commande faite de deux gestes (un Ctrl+Z pour ce qui n'est pas dans le projet) ----
//  `doIt` la premiere fois et a chaque Retablir, `undoIt` a chaque Annuler ; `after` apres l'un ou l'autre (rafraichir
//  le volet). Un refus de `doIt` la premiere fois : la pile ne la garde pas. `alreadyDone` : le geste est deja fait
//  (le volet a supprime avant d'empiler) - la premiere execution ne le refait pas.
class LambdaCommand final : public ICommand {
public:
    LambdaCommand(std::string label, std::function<Status()> doIt, std::function<Status()> undoIt,
                  std::function<void()> after = {}, bool alreadyDone = false)
        : label_(std::move(label)), do_(std::move(doIt)), undo_(std::move(undoIt)), after_(std::move(after)),
          skip_(alreadyDone) {}
    Status execute() override {
        if (skip_) {
            skip_ = false;
            return ok();
        }
        auto st = do_ ? do_() : ok();
        if (after_) after_();
        return st;
    }
    Status undo() override {
        auto st = undo_ ? undo_() : ok();
        if (after_) after_();
        return st;
    }
    [[nodiscard]] std::string label() const override { return label_; }

private:
    std::string             label_;
    std::function<Status()> do_, undo_;
    std::function<void()>   after_;
    bool                    skip_{false};
};

// Pousser une commande sur la pile de l'application (CommandGroupScope::stack) ; sans pile (un essai qui n'en a pas) :
// l'executer seulement. Faux et `why` : elle a refuse.
bool apply(CommandPtr cmd, std::string* why = nullptr);

} // namespace core::trash
