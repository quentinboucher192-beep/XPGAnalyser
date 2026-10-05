// =============================================================================
//  app/TutorialsLot8.cpp - Lot API 8 : didacticiels et aide (voir TutorialsLot8.hpp)
// -----------------------------------------------------------------------------
//  Les resumes reprennent les mots de la maquette validee (maquette-lot8.html,
//  ses visites guidees) : ce que l'utilisateur y a lu, il le retrouve ici.
// =============================================================================
#include "TutorialsLot8.hpp"

#include <utility>

namespace app::lot8 {

namespace {

const std::vector<NewTrail> kTrails = {
    {"api-simuler", "Simuler et suivre la simulation", "interactif", 7, 3, ui::Icon::Play,
     "Le nouveau dossier Simulation (au m\xC3\xAAme niveau qu'API, IHM et Versions) : la Vue d'ensemble, la phrase qui dit tout, "
     "Simuler (F5), Pause, Un cycle, Arr\xC3\xAAter (Maj+F5), ce qui m\xC3\xA9rite ton attention, les for\xC3\xA7" "ages, les courbes, le journal.",
     "sim-ensemble"},
    {"api-deboguer", "D\xC3\xA9" "boguer pas \xC3\xA0 pas", "interactif", 7, 3, ui::Icon::StepOnce,
     "Un point d'arr\xC3\xAAt dans la marge du code, sa condition, la ligne d'arr\xC3\xAAt, Section suivante (F10), la trace "
     "du cycle, les espions et \xC2\xAB qui a \xC3\xA9" "crit \xC2\xBB.",
     "sim-debogage"},
    {"api-pause", "Modifier pendant une pause", "interactif", 4, 2, ui::Icon::Pause,
     "En pause, tu modifies le projet ; au prochain Continuer (F5), la simulation reprend au m\xC3\xAAme cycle avec ta "
     "modification (avant, elle repartait du cycle 0). Le journal le note.",
     "sim-pause"},
    {"api-glisser", "Glisser un fichier dans le projet", "visite", 5, 2, ui::Icon::Open,
     "Glisse n'importe quel fichier sur la fen\xC3\xAAtre : d'un classeur, tout ce qu'on peut en faire, \xC3\xA0 cocher ; d'un autre "
     "fichier, Ressources et / ou Fichiers externes.",
     "glisser"},
    {"api-theme", "Cr\xC3\xA9" "er ou modifier un th\xC3\xA8me", "visite", 6, 3, ui::Icon::Layers,
     "Affichage \xE2\x80\xBA Th\xC3\xA8me\xE2\x80\xA6 : 43 th\xC3\xA8mes par famille, un clic applique ; Nouveau (\xC3\xA0 partir d'un th\xC3\xA8me), les "
     "contrastes v\xC3\xA9rifi\xC3\xA9s, Enregistrer, Exporter et Importer un .xpgtheme.",
     "themes"},
    {"api-renommer", "Renommer partout", "visite", 6, 3, ui::Icon::Variable,
     "La case Nom, F2 ou le double-clic ouvrent le dialogue Renommer : le verdict du nom, o\xC3\xB9 \xC3\xA7" "a change (API, IHM, "
     "Tables), avant / apr\xC3\xA8s ; les expressions des objets suivent ; un champ de DDT aussi.",
     "renommer"},
    {"api-expressions", "Expressions : Compiler et G\xC3\xA9n\xC3\xA9rer", "visite", 6, 3, ui::Icon::Code,
     "Le symbole fx d'une propri\xC3\xA9t\xC3\xA9 li\xC3\xA9" "e \xC3\xA0 une expression, le badge fx de la liste, une expression impossible en rouge ; "
     "Compiler la trouve (Aller \xC3\xA0, Remplacer), G\xC3\xA9n\xC3\xA9rer refuse tant qu'il en reste.",
     "compiler"},
    {"api-filtres", "Chercher et retrouver ses filtres", "visite", 5, 2, ui::Icon::Filter,
     "Un champ de recherche dans chaque volet (Ctrl+F y met le curseur), \xC2\xAB 3 sur 18 \xC2\xBB, les mots surlign\xC3\xA9s ; tes "
     "recherches et tes filtres sont retenus d'une s\xC3\xA9" "ance \xC3\xA0 l'autre ; Effacer quand tu n'en veux plus.",
     "filtres"},
};

std::string& mailbox() {
    static std::string anchor;
    return anchor;
}

} // namespace

const std::vector<NewTrail>& trails() { return kTrails; }

const NewTrail* find(std::string_view key) {
    for (const auto& t : kTrails)
        if (key == t.key) return &t;
    return nullptr;
}

std::string helpAnchorFor(std::string_view screen) {
    static const std::pair<const char*, const char*> kScreens[] = {
        {"ensemble", "sim-ensemble"}, {"automate", "sim-ensemble"}, {"ihm", "sim-ensemble"},
        {"equipements", "sim-ensemble"}, {"debogage", "sim-debogage"}, {"forcages", "sim-forcages"},
        {"courbes", "sim-courbes"}, {"journal", "sim-journal"}, {"glisser", "glisser"},
        {"themes", "themes"}, {"renommer", "renommer"}, {"compiler", "compiler"},
    };
    for (const auto& [k, a] : kScreens)
        if (screen == k) return a;
    return {};
}

std::vector<std::string> allAnchors() {
    std::vector<std::string> out{kNewsAnchor, kShortcutsAnchor, "sim-pause", "filtres", "exports-lot8"};
    for (const char* k : {"ensemble", "debogage", "forcages", "courbes", "journal", "glisser", "themes", "renommer", "compiler"})
        out.push_back(helpAnchorFor(k));
    for (const auto& t : kTrails) out.emplace_back(t.anchor);
    return out;
}

void setPendingHelpAnchor(std::string anchor) { mailbox() = std::move(anchor); }

std::string takePendingHelpAnchor() {
    std::string a = std::move(mailbox());
    mailbox().clear();
    return a;
}

} // namespace app::lot8
