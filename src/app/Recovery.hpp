// =============================================================================
//  app/Recovery.hpp - la reprise apres un arret brutal (lot 15)
// -----------------------------------------------------------------------------
//  LE PC S'ETEINT, LE LOGICIEL PLANTE : au prochain lancement, une question -
//  "Voulez-vous recharger les donnees precedentes ?".
//
//  LE VERROU DE SESSION. Chaque lancement pose un fichier dans le dossier de
//  reprise (a cote des reglages) : son numero de processus, le projet ouvert,
//  le mode (conception, poste), et il le touche toutes les 10 s. Une sortie
//  normale l'efface. Un verrou qui reste, dont le processus n'existe plus, dit
//  un arret brutal.
//
//  CE QUI SE GARDE :
//    en conception    le projet, s'il a des modifications non enregistrees,
//                     toutes les 2 minutes - dans le dossier de reprise, JAMAIS
//                     par-dessus le projet (c'est l'utilisateur qui enregistre) ;
//    en poste         l'etat de la marche toutes les 10 s : la vue montree, les
//                     variables IHM (les compteurs), les alarmes en cours et
//                     leur acquittement.
//
//  AU LANCEMENT SUIVANT : en conception, la question, et "Recharger" rouvre le
//  projet avec les modifications gardees (a enregistrer) ; en poste lance sans
//  personne (demarrage automatique), la question avec un compte a rebours de
//  30 s, puis la reprise d'office.
//
//  Les fichiers s'ecrivent entiers puis se renomment : un arret au milieu
//  d'une ecriture laisse l'ancien, jamais un fichier coupe.
// =============================================================================
#pragma once

#include <functional>
#include <optional>
#include <string>

namespace app {

class Recovery {
public:
    // Ce qu'a laisse une session arretee brutalement.
    struct Found {
        std::string lockFile;
        std::string started;          // "2026-09-25 23:20:00"
        std::string lastBeat;         // la derniere fois qu'elle a dit qu'elle vivait
        std::string project;          // le dossier du projet ouvert
        std::string mode;             // "conception", "poste"
        std::string autosave;         // le dossier de la sauvegarde de reprise ("" : aucune)
        std::string autosavedAt;
        std::string stationState;     // le fichier de l'etat du poste ("" : aucun)
        std::string stationAt;
        [[nodiscard]] bool hasData() const noexcept { return !autosave.empty() || !stationState.empty(); }
    };

    Recovery() = default;
    ~Recovery();
    Recovery(const Recovery&) = delete;
    Recovery& operator=(const Recovery&) = delete;

    // Au lancement : le dossier de reprise (cree s'il le faut), le verrou de
    // cette session, les sessions arretees brutalement.
    void start(const std::string& folder);
    // A la sortie normale : le verrou et les donnees de cette session s'effacent.
    void stop();
    [[nodiscard]] const std::string& folder() const noexcept { return folder_; }

    // La plus recente des sessions arretees brutalement (avec des donnees).
    [[nodiscard]] const std::optional<Found>& crashed() const noexcept { return crashed_; }
    // La question est posee (et la reponse donnee) : ses donnees s'effacent.
    void forget();
    // Garder les donnees d'une session arretee pour les recharger (le poste :
    // au demarrage suivant) ; rend ce qu'elle avait.
    [[nodiscard]] std::optional<Found> take();

    // Chaque image : le battement (toutes les 10 s).
    void tick(double dt);
    void setProject(const std::string& folder, const std::string& mode);

    // En conception : sauver maintenant ? (des modifications, et 2 min depuis la derniere).
    [[nodiscard]] bool autosaveDue(bool modified) const;
    // `write` ecrit le projet dans ce dossier ; vrai : garde.
    bool autosave(const std::function<bool(const std::string& folder, std::string* why)>& write, std::string* why = nullptr);
    // Le projet vient d'etre enregistre : la sauvegarde de reprise ne sert plus.
    void clearAutosave();
    void setAutosavePeriod(double seconds) noexcept { period_ = seconds; }

    // En poste : l'etat de la marche (le texte de hmi::Runtime::stateSnapshot).
    bool saveStationState(const std::string& text);
    [[nodiscard]] static std::string readFile(const std::string& path);

    // Le processus de ce numero tourne-t-il ? (Windows : OpenProcess ; ailleurs : kill 0).
    [[nodiscard]] static bool processAlive(long pid);
    [[nodiscard]] static long currentProcess();

private:
    void writeLock();

    std::string          folder_;
    std::string          id_;                  // le nom de cette session
    std::string          lockFile_;
    std::string          started_;
    std::string          project_, mode_;
    std::string          autosave_, autosavedAt_;
    std::string          station_, stationAt_;
    double               clock_{0};
    double               nextBeat_{0};
    double               lastSave_{-1e9};
    double               period_{120};
    std::optional<Found> crashed_;
    bool                 running_{false};
};

} // namespace app
