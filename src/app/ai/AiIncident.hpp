// =============================================================================
//  app/ai/AiIncident.hpp - 1.20.0 : LES DOSSIERS D'INCIDENT (sur le disque)
// -----------------------------------------------------------------------------
//  Un incident est un DOSSIER AUTONOME, dans les ressources du projet :
//
//    ihm/ressources/enregistrements/en attente/<date>-<alarme>-<id>/
//       incident.json        le contexte du defaut, COPIE (hmi::incident::Snapshot) :
//                            l'alarme, la chaine de dependances, les valeurs, la
//                            chronologie, les alarmes liees, les pistes, l'historique
//       capture.jpg          l'ecran au declenchement (une image : jamais dite video)
//       video.avi            les secondes avant et apres, en Motion-JPEG (une vraie
//       video.json           video, lue par l'objet Video de l'IHM) et ses instants
//       analyses/000-initiale.json    l'analyse du moment (jamais reecrite)
//       analyses/001-....json         les analyses suivantes, chacune un fichier
//       commentaires/001.json         les commentaires, un fichier chacun
//       validation.json      (valides/ seulement) quand, par qui, l'empreinte
//       empreintes.sha256    l'empreinte SHA-256 de chaque fichier, au format de
//                            sha256sum (verifiable hors de l'application)
//
//  RIEN N'EST RECALCULE depuis le projet du moment : un ajout est un fichier de
//  plus ; aucun fichier ecrit n'est reecrit (sauf la liste des empreintes, qui ne
//  fait que s'allonger - verifie). La creation passe par un dossier ".partiel"
//  renomme a la fin : un arret brutal laisse un ".partiel", repris au demarrage.
//  Valider = copier vers valides/ (un nom jamais ecrase), verifier chaque
//  empreinte de la copie, puis seulement retirer l'original.
//
//  Le stockage ne touche a aucun ecran : l'hote l'appelle depuis son fil
//  d'ecriture ; les essais, directement.
// =============================================================================
#pragma once

#include "AiJson.hpp"
#include "../../hmi/HmiIncident.hpp"

#include <cstdint>
#include <functional>
#include <optional>
#include <memory>
#include <string>
#include <string_view>
#include <vector>

namespace app::ai::incidents {

inline constexpr int              kFormat = 1;
inline constexpr std::string_view kPendingFolder = "en attente";
inline constexpr std::string_view kValidatedFolder = "valides";
inline constexpr std::string_view kManifest = "empreintes.sha256";

// ---- le contenu ----------------------------------------------------------------------------------------------------
// Ce que l'ecran a montre. `capture` : l'image du declenchement (JPEG) ; `frames` : la video (JPEG, avant et apres).
struct Capture {
    std::vector<std::uint8_t> jpeg;
    int                       width{0}, height{0};
    std::string               at;          // l'horodatage de l'image
    std::string               note;        // pourquoi il n'y en a pas ("aucune vue en marche n'etait affichee")
};
struct Video {
    // 1.21.0 (plantage 1.20.2, std::bad_alloc) : les images sont PARTAGEES avec le tampon de l'hote, jamais copiees - une image
    // repetee a chaque top de la cadence, plusieurs incidents declenches ensemble ne multiplient plus la memoire.
    using Frame = std::shared_ptr<const std::vector<std::uint8_t>>;
    std::vector<Frame>                     frames;   // JPEG
    std::vector<double>                    times;    // secondes depuis le declenchement (negatif : avant)
    int                                    width{0}, height{0};
    double                                 fps{0};
    double                                 before{0}, after{0};
    std::string                            note;
};

struct Analysis {
    int         number{0};          // 0 : l'initiale
    std::string kind;               // "initiale", "complementaire"
    std::string createdAt;
    std::string source;             // "IA int\xC3\xA9gr\xC3\xA9" "e (d\xC3\xA9terministe)", "OpenAI (gpt-...)"
    std::string state;              // "faite", "echec"
    std::string text;               // la fiche ou la reponse du modele
    std::string certainty;          // "fait", "indirect", "hypothese" ; vide : non dit
    std::string contextHash;        // l'empreinte du contexte envoye (le cache des analyses repetees)
    std::string error;              // echec : pourquoi
};
struct Comment {
    int         number{0};
    std::string createdAt, author, text;
};
struct Validation {
    std::string at, author, note, manifestHash, from;
};

struct Integrity {
    bool                     ok{false};
    std::size_t              checked{0};
    std::vector<std::string> missing, altered, extra;
    std::string              why;   // le manifeste illisible
};

enum class State : std::uint8_t { Pending, Validated };

// Un incident relu (la liste, la fiche).
struct Item {
    std::string                 folder;    // le chemin complet
    std::string                 name;      // le nom du dossier
    State                       state{State::Pending};
    bool                        readable{false};
    std::string                 why;       // illisible : pourquoi
    hmi::incident::Snapshot     snapshot;
    std::vector<Analysis>       analyses;
    std::vector<Comment>        comments;
    std::optional<Validation>   validation;
    bool                        hasCapture{false}, hasVideo{false};
    std::string                 capturePath, videoPath;
    int                         captureWidth{0}, captureHeight{0};
    std::string                 captureNote, videoNote;
    double                      videoSeconds{0};
    int                         videoFrames{0};
    double                      videoFps{0};
    // les raccourcis de la liste
    [[nodiscard]] std::string equipment() const;       // le premier equipement de la chaine
    [[nodiscard]] std::string synthesis() const;       // la premiere phrase de la derniere analyse faite (sinon le resume)
    [[nodiscard]] std::string certainty() const;       // la certitude de la derniere analyse faite ("hypoth\xC3\xA8se"...)
    [[nodiscard]] std::string analysisState() const;   // "faite", "en attente", "\xC3\xA9" "chec"
    [[nodiscard]] const Analysis* latest() const;      // la derniere analyse faite (nulle : aucune)
};

// ---- le JSON (le format du dossier) ------------------------------------------------------------------------------
[[nodiscard]] json::Value toJson(const hmi::incident::Snapshot&);
bool                      fromJson(const json::Value&, hmi::incident::Snapshot&, std::string* why = nullptr);
[[nodiscard]] json::Value toJson(const Analysis&);
bool                      fromJson(const json::Value&, Analysis&);

// ---- l'analyse -----------------------------------------------------------------------------------------------------
// L'analyse initiale, faite ici (sans reseau) : la fiche de diagnostic.
[[nodiscard]] Analysis initialAnalysis(const hmi::incident::Snapshot&, std::string_view createdAt);
// Ce qu'on envoie a un modele : la consigne (jamais inventer, faits / indirect / hypotheses, l'ordre n'est pas la cause,
// dire ce qui manque) et le contexte CIBLE de l'incident (pas tout le projet), borne.
[[nodiscard]] std::string analysisSystemPrompt();
[[nodiscard]] std::string analysisRequest(const hmi::incident::Snapshot&, std::size_t maxBytes = 12000,
                                          const std::vector<Analysis>& earlier = {});
// La certitude dite par le modele ("Certitude : relation indirecte") ; vide : non dite.
[[nodiscard]] std::string certaintyOf(std::string_view text);
// L'empreinte d'un contexte (le cache : la meme demande au meme modele n'est pas renvoyee).
[[nodiscard]] std::string contextHash(std::string_view system, std::string_view request, std::string_view model);

// Un identifiant unique (32 chiffres hexadecimaux).
[[nodiscard]] std::string newId();
// "2026-10-31 17:40:12.350" -> l'heure locale de cette machine.
[[nodiscard]] std::string nowStamp();

// ---- le stockage ---------------------------------------------------------------------------------------------------
class Store {
public:
    // `root` : le dossier des enregistrements (hmi::rec::folderOf) ; les incidents dans root/en attente et root/valides.
    explicit Store(std::string root);
    [[nodiscard]] const std::string& root() const noexcept { return root_; }
    [[nodiscard]] std::string pendingFolder() const;
    [[nodiscard]] std::string validatedFolder() const;

    // LA CREATION : le dossier .partiel, ses fichiers, les empreintes, puis le nom final. `folder` : le chemin final.
    bool create(const hmi::incident::Snapshot&, const Capture&, const Analysis& initial, std::string& folder,
                std::string* why = nullptr);
    // La video (apres le declenchement) : video.avi et video.json ; jamais si elle existe deja.
    bool addVideo(const std::string& folder, const Video&, std::string* why = nullptr);
    // Une analyse de plus (numerotee a la suite ; jamais une ecrasee). `a.number` est rempli.
    bool addAnalysis(const std::string& folder, Analysis& a, std::string* why = nullptr);
    // Un commentaire de plus.
    bool addComment(const std::string& folder, Comment& c, std::string* why = nullptr);
    // VALIDER : la copie vers valides/, verifiee, puis l'original retire. `validated` : le chemin dans valides/.
    bool validate(const std::string& folder, const std::string& author, const std::string& note, std::string& validated,
                  std::string* why = nullptr);
    // Supprimer un incident EN ATTENTE (un valide ne se supprime pas d'ici).
    bool remove(const std::string& folder, std::string* why = nullptr);

    // La lecture.
    [[nodiscard]] std::vector<Item> list(State) const;
    [[nodiscard]] Item read(const std::string& folder) const;
    [[nodiscard]] Integrity verify(const std::string& folder) const;
    // Les incidents de cette alarme (en attente et valides), du plus recent au plus ancien, `except` : un id a ignorer.
    [[nodiscard]] std::vector<hmi::incident::Snapshot::Previous> history(std::string_view alarm, std::string_view except = {},
                                                                         std::size_t max = 10) const;
    // Le nombre d'incidents en attente (sans les relire).
    [[nodiscard]] std::size_t pendingCount() const;

    // Au demarrage : les ".partiel" (termines s'ils sont lisibles, sinon gardes en ".incomplet"), les copies de
    // validation interrompues, l'original reste apres une validation finie. Les lignes disent ce qui a ete fait.
    std::vector<std::string> recover();

    // Les essais : une ecriture qui echoue (le disque plein) ; vrai : ce chemin echoue.
    void failWritesForTests(std::function<bool(const std::string& path)> f) { fail_ = std::move(f); }

private:
    bool write(const std::string& path, std::string_view content, std::string* why) const;
    bool appendManifest(const std::string& folder, const std::vector<std::string>& relPaths, std::string* why) const;
    std::string                                   root_;
    std::function<bool(const std::string&)>       fail_;
};

} // namespace app::ai::incidents
