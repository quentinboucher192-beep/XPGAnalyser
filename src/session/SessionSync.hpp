// =============================================================================
//  session/SessionSync.hpp - 1.43.0 : les changements qui passent d'un PC a l'autre
// -----------------------------------------------------------------------------
//  LE MIROIR (Mirror) garde l'etat PARTAGE : ce que ce PC a envoye ou recu en
//  dernier. Ce qui differe entre le projet et le miroir, c'est ce que ce PC a
//  change et pas encore envoye (collect) ; ce qui arrive d'un autre PC s'applique
//  au projet ET au miroir (applyView, applyProject) : les changements locaux pas
//  encore partis restent des changements locaux.
//
//  UNE VUE : objet par objet (ViewDelta) - ses objets changes ou neufs (leur texte,
//  celui du fichier de la vue), ceux supprimes, l'ordre de dessin s'il change, la vue
//  sans ses objets si ses proprietes changent. Deux equipiers sur deux objets de la
//  meme vue ne s'ecrasent pas.
//
//  LE RESTE DU PROJET (ProjectDelta) : ses fichiers hors des vues (ihm.txt, les
//  scripts, les ressources...), tels que l'enregistrement les ecrit. Un fichier de
//  texte passe en DIFFERENCES DE LIGNES avec leur contexte (diffLines / patchLines) :
//  deux equipiers qui ajoutent chacun une variable ne s'ecrasent pas ; une ressource
//  (une image) passe entiere. Une vue neuve passe entiere avec l'index qui la cite.
//
//  L'HOTE FAIT FOI : un ecart (un changement qui ne s'applique plus) se rattrape par
//  les empreintes (hashes) : l'equipier redemande l'etat de l'hote (fullView, fileOf).
// =============================================================================
#pragma once

#include "../core/Json.hpp"
#include "../hmi/HmiModel.hpp"

#include <map>
#include <memory>
#include <set>
#include <string>
#include <string_view>
#include <vector>

namespace session {

struct ViewDelta {
    hmi::Id view{hmi::kNoId};
    bool full{false};                  // la vue entiere (un rattrapage, une vue que ce PC n'a pas)
    bool shellChanged{false};          // ses proprietes (calques, scripts, guides...) : `shell`
    hmi::View shell;                   // sans ses objets
    std::vector<hmi::Object> objects;  // changes ou neufs (tous si `full`)
    std::vector<hmi::Id> removed;
    bool orderChanged{false};
    std::vector<hmi::Id> order;        // l'ordre de dessin apres (tous les objets)
    [[nodiscard]] bool empty() const noexcept;
    // Les objets que le changement touche (changes, neufs, supprimes).
    [[nodiscard]] std::vector<hmi::Id> touched() const;
};
// En trame : l'en-tete (op "VUE") et les donnees (le texte d'une vue avec les seuls objets du changement).
void encodeView(const ViewDelta&, core::json::Value& header, std::string& data);
bool decodeView(const core::json::Value& header, std::string_view data, ViewDelta&, std::string* why);

// ---- les differences de lignes ----
struct LineHunk {
    std::size_t at{0};                    // la ligne (0 : la premiere) ou commence `removed` dans le texte d'origine
    std::vector<std::string> before;      // le contexte avant (2 lignes au plus)
    std::vector<std::string> removed;
    std::vector<std::string> added;
    std::vector<std::string> after;       // le contexte apres
};
[[nodiscard]] std::vector<LineHunk> diffLines(std::string_view base, std::string_view now);
// Appliquer : chaque morceau la ou son contexte et ses lignes retirees se trouvent (au plus pres de `at`).
// Faux : un morceau ne trouve plus sa place (quelqu'un a change les memes lignes) ; `text` n'a pas change.
bool patchLines(std::string& text, const std::vector<LineHunk>&);

struct FileDelta {
    enum class Kind { Patch, Whole, Removed };
    std::string path;                     // relatif a ihm/ ("ihm.txt", "scripts/0031-Init.st"...)
    Kind kind{Kind::Whole};
    std::vector<LineHunk> hunks;          // Patch
    std::string whole;                    // Whole : le contenu (des octets)
};
struct ProjectDelta {
    std::vector<FileDelta> files;
    std::vector<std::pair<hmi::Id, std::string>> newViews;   // les vues neuves : leur texte entier
    [[nodiscard]] bool empty() const noexcept { return files.empty() && newViews.empty(); }
};
void encodeProject(const ProjectDelta&, core::json::Value& header, std::string& data);
bool decodeProject(const core::json::Value& header, std::string_view data, ProjectDelta&, std::string* why);

// Ce qu'applyProject a fait.
struct ProjectApplied {
    std::vector<hmi::Id> addedViews, removedViews;
    std::vector<hmi::View> removed;   // les vues retirees (l'hote les met dans la corbeille du projet)
    std::vector<std::string> conflicts;   // les fichiers dont un morceau n'a pas trouve sa place
};

class Mirror {
public:
    // Tout l'etat partage = le projet (au debut de la session, apres un rattrapage complet).
    void reset(hmi::Project&);
    // Les changements locaux depuis le dernier appel : les vues `touched` (presentes des deux cotes), et le
    // reste du projet si `projectTouched` (ou si des vues sont apparues / ont disparu). Le miroir les prend.
    void collect(hmi::Project&, const std::set<hmi::Id>& touched, bool projectTouched,
                 std::vector<ViewDelta>& views, ProjectDelta& project);
    // Un changement d'un autre PC : au projet et au miroir. Faux : `why` (la vue n'existe pas...).
    bool applyView(hmi::Project&, const ViewDelta&, std::string* why);
    bool applyProject(hmi::Project&, const ProjectDelta&, ProjectApplied& done, std::string* why);

    // L'etat d'une vue (un rattrapage) ; celui de quelques objets (un changement refuse : on remet le juste).
    [[nodiscard]] static ViewDelta fullView(const hmi::View&);
    [[nodiscard]] static ViewDelta objectsOf(const hmi::View&, const std::vector<hmi::Id>& ids);
    // Le contenu entier d'un fichier hors des vues (un rattrapage). Faux : il n'existe pas.
    bool fileOf(hmi::Project&, const std::string& path, FileDelta& out);

    // Les empreintes de l'etat partage : "v:<id>" pour une vue, "f:<chemin>" pour un fichier.
    [[nodiscard]] std::map<std::string, std::string> hashes(hmi::Project&);
    // Le miroir d'une vue ou d'un fichier remis a l'etat du projet (apres un rattrapage recu).
    void adoptView(const hmi::View&);
    void adoptFile(hmi::Project&, const std::string& path);

    [[nodiscard]] bool hasView(hmi::Id) const;

private:
    struct ViewSnap {
        hmi::View shell;
        std::vector<hmi::Id> order;
        std::map<hmi::Id, hmi::Object> objects;
    };
    std::map<hmi::Id, ViewSnap> views_;
    std::map<std::string, std::shared_ptr<const std::vector<std::uint8_t>>> files_;
    static ViewSnap snap(const hmi::View&);
};

// Le reste du projet passe de `from` a `to` (deux projets sans leurs vues : les instantanes d'une commande annulee ou
// retablie), reporte ligne par ligne sur `current` (hmi::Document::mergeShell en session). Faux : un morceau ne
// trouve plus sa place ; `current` n'a pas change.
bool mergeShell(hmi::Project& current, const hmi::Project& from, const hmi::Project& to);

// Les fichiers du projet hors des vues, tels que l'enregistrement les ecrit (les vues reduites a leur ligne de
// l'index), en-tete au format courant. Expose pour les essais.
[[nodiscard]] std::map<std::string, std::shared_ptr<const std::vector<std::uint8_t>>> shellFiles(hmi::Project&);

} // namespace session
