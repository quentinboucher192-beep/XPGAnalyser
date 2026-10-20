#include "Commands.hpp"

#include "KeyMap.hpp"

namespace ui::cmd {

namespace {

// L'ordre suit l'enum : def() y lit directement.
const std::vector<Def>& table() {
    static const std::vector<Def> t = {
        { Id::Couper,            "Couper",                                  Icon::Cut,        "Ctrl+X" },
        { Id::Copier,            "Copier",                                  Icon::Copy,       "Ctrl+C" },
        { Id::Coller,            "Coller",                                  Icon::Paste,      "Ctrl+V" },
        { Id::CollerIci,         "Coller ici",                              Icon::Paste,      "Ctrl+V" },
        { Id::Dupliquer,         "Dupliquer",                               Icon::Duplicate,  "Ctrl+D" },
        { Id::DupliquerFenetre,  "Dupliquer\xE2\x80\xA6",                   Icon::Duplicate,  "Ctrl+Maj+D" },
        { Id::ToutChoisir,       "Tout choisir",                            Icon::None,       "Ctrl+A" },
        { Id::Renommer,          "Renommer\xE2\x80\xA6",                    Icon::Rename,     "F2" },
        { Id::RenommerPartout,   "Renommer partout\xE2\x80\xA6",            Icon::Rename,     "F2" },
        { Id::Supprimer,         "Supprimer",                               Icon::Delete,     "Suppr" },
        { Id::Retirer,           "Retirer",                                 Icon::Delete,     "Suppr" },
        { Id::Ajouter,           "Ajouter",                                 Icon::Plus,       "Inser" },
        { Id::Monter,            "Monter",                                  Icon::ArrowUp,    "Alt+\xE2\x86\x91" },
        { Id::Descendre,         "Descendre",                               Icon::ArrowDown,  "Alt+\xE2\x86\x93" },
        { Id::Ouvrir,            "Ouvrir",                                  Icon::Open,       "Entr\xC3\xA9" "e" },
        { Id::OuvrirFenetre,     "Ouvrir dans une fen\xC3\xAAtre",          Icon::Window,     "" },
        { Id::AfficherArbre,     "Afficher dans l'arbre",                   Icon::GoTo,       "" },
        { Id::Epingler,          "\xC3\x89pingler",                         Icon::Pin,        "" },
        { Id::Desepingler,       "D\xC3\xA9s\xC3\xA9pingler",               Icon::Pin,        "" },
        { Id::Aide,              "Aide",                                    Icon::Help,       "F1" },
        { Id::AllerDefinition,   "Aller \xC3\xA0 la d\xC3\xA9" "finition",  Icon::GoTo,       "F12" },
        { Id::VoirUsages,        "Voir les usages",                         Icon::Search,     "Maj+F12" },
        { Id::ChoisirFonction,   "Choisir une fonction\xE2\x80\xA6",        Icon::Function,   "Ctrl+I" },
        { Id::Corriger,          "Corriger",                                Icon::Wrench,     "Ctrl+." },
        { Id::CopierNom,         "Copier le nom",                           Icon::Copy,       "" },
        { Id::CopierValeur,      "Copier la valeur",                        Icon::Copy,       "Ctrl+C" },
        { Id::Forcer,            "Forcer\xE2\x80\xA6",                      Icon::Force,      "Entr\xC3\xA9" "e" },
        { Id::Relacher,          "Rel\xC3\xA2" "cher",                      Icon::Release,    "Suppr" },
        { Id::ToutRelacher,      "Tout rel\xC3\xA2" "cher",                 Icon::Release,    "" },
        { Id::SuivreCourbe,      "Suivre sur la courbe",                    Icon::Chart,      "" },
        { Id::Simuler,           "Simuler",                                 Icon::Play,       "F5" },
        { Id::Arreter,           "Arr\xC3\xAAter",                          Icon::Stop,       "Maj+F5" },
        { Id::Compiler,          "Compiler",                                Icon::Code,       "F7" },
        { Id::CreerVersion,      "Cr\xC3\xA9" "er une version\xE2\x80\xA6", Icon::History,    "Ctrl+Alt+S" },
        { Id::ImporterXhw,       "Importer la configuration (.XHW)\xE2\x80\xA6", Icon::Import, "Ctrl+Maj+O" },
        { Id::ReanalyserFichiers,"R\xC3\xA9" "analyser les fichiers",       Icon::Refresh,    "" },
        { Id::ExporterZip,       "Exporter (.zip)\xE2\x80\xA6",             Icon::Export,     "" },
        { Id::Grouper,           "Grouper",                                 Icon::Group,      "Ctrl+G" },
        { Id::Degrouper,         "D\xC3\xA9grouper",                        Icon::Group,      "Ctrl+Maj+G" },
        { Id::CopierStyle,       "Copier le style",                         Icon::Copy,       "Ctrl+Alt+C" },
        { Id::CollerStyle,       "Coller le style",                         Icon::Paste,      "Ctrl+Alt+V" },
        { Id::Verrouiller,       "Verrouiller",                             Icon::Lock,       "" },
        { Id::Deverrouiller,     "D\xC3\xA9verrouiller",                    Icon::Unlock,     "" },
        { Id::Deplier,           "D\xC3\xA9plier",                          Icon::Expand,     "\xE2\x86\x92" },
        { Id::Replier,           "Replier",                                 Icon::Collapse,   "\xE2\x86\x90" },
        { Id::ToutDeplier,       "Tout d\xC3\xA9plier",                     Icon::Expand,     "" },
        { Id::ToutReplier,       "Tout replier",                            Icon::Collapse,   "" },
        { Id::Fermer,            "Fermer",                                  Icon::Close,      "Ctrl+W" },
        { Id::RouvrirOnglet,     "Rouvrir le dernier ferm\xC3\xA9",         Icon::Undo,       "Ctrl+Maj+T" },
        { Id::Actualiser,        "Actualiser",                              Icon::Refresh,    "F5" },
        { Id::DossierParent,     "Dossier parent",                          Icon::ArrowUp,    "Retour" },
    };
    return t;
}

}   // namespace

const std::vector<Def>& all() { return table(); }

const Def& def(Id id) noexcept {
    const auto& t = table();
    const auto i = static_cast<std::size_t>(id);
    return i < t.size() ? t[i] : t.front();
}

PopupMenu::Item item(Id id, int itemId, bool enabled, std::string reason, std::string label) {
    const Def& d = def(id);
    PopupMenu::Item it;
    it.label = label.empty() ? std::string(d.label) : std::move(label);
    it.shortcut = std::string(d.shortcut);
    it.disabledReason = enabled ? std::string{} : std::move(reason);
    it.icon = d.icon;
    it.enabled = enabled;
    it.id = itemId;
    return it;
}

PopupMenu::Item separator() {
    PopupMenu::Item it;
    it.separator = true;
    return it;
}

PopupMenu::Item heading(std::string label, std::string right) {
    PopupMenu::Item it;
    it.label = std::move(label);
    it.shortcut = std::move(right);
    it.heading = true;
    it.enabled = false;
    return it;
}

bool looksLikeShortcut(std::string_view text) {
    if (text.empty()) return true;
    // Une ou deux touches (« Ctrl+K, Ctrl+C ») : chacune se lit comme un appui.
    std::size_t start = 0;
    while (start <= text.size()) {
        const auto comma = text.find(", ", start);
        const auto part = text.substr(start, comma == std::string_view::npos ? std::string_view::npos : comma - start);
        if (!keymap::parseStroke(part)) {
            // Les noms que la table des touches ne lit pas mais que l'appli affiche.
            static const char* kExtra[] = { "Ctrl+molette", "Retour", "Ctrl+Retour", "=" };   // 1.12.18 : "=" ouvre une expression
            bool extra = false;
            for (const char* e : kExtra) if (part == e) extra = true;
            if (!extra) return false;
        }
        if (comma == std::string_view::npos) break;
        start = comma + 2;
    }
    return true;
}

std::vector<std::string> badShortcuts(const std::vector<PopupMenu::Item>& items) {
    std::vector<std::string> out;
    for (const auto& it : items) {
        if (it.separator) continue;
        if (!it.heading && !looksLikeShortcut(it.shortcut)) out.push_back(it.label + " : " + it.shortcut);
        const auto sub = badShortcuts(it.children);
        out.insert(out.end(), sub.begin(), sub.end());
    }
    return out;
}

}   // namespace ui::cmd
