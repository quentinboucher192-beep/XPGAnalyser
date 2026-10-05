# L'import depuis le classeur d'affaire

Ce dossier genere le programme Control Expert depuis le classeur
(`automation.xlsm`) : cartes, voies, equipements, cablage, reports, alarmes,
reglages, controle, Modbus, grafcets, diagnostic et simulation. Ce fichier dit quelle
macro fait quoi, dans quel ordre, et ce qui reste a faire a la main.

Il est en ASCII, comme tout `libs/` : Control Expert lit ces fichiers.

---

## 1. L'affaire entiere : `ImporterClasseur`

Une execution, deux ecrans de questions (le classeur, puis tout le reste),
une seule annulation.

    1  VerifierClasseur     ce qui va casser - ne modifie rien
    2  ImporterProgramme    cartes, voies, equipements, cablage, reports,
                            alarmes, horloge
    3  ImporterReglages     si l'onglet Reglages est la
    4  ImporterControle     si Paliers, Rotations, Modes ou Interverrouillages
    5  ImporterModbus       si l'onglet Modbus est la
    6  ImporterGrafcet      si l'onglet Grafcet Etapes est la
    7  GenererDiagnostic    si on le demande
    8  GenererSimulation    si on le demande

Chaque etape est une macro de ce dossier, lancee par `RunMacro` : elle reste
lancable seule. Les macros appelees partagent les reponses (le classeur, la
tache, le prefixe ne se donnent qu'une fois) et la commande de l'appelante :
**Ctrl+Z reprend l'affaire entiere**.

Ce que Ctrl+Z ne reprend pas : les imports de bibliotheque (`LibImport`,
`LibUpdate`) et les fichiers ecrits a cote du classeur (`alarmes.csv`). Le
compte rendu le dit a chaque fois.

**L'apercu d'abord.** Il montre tout ce qui serait fait, avertissements de
`VerifierClasseur` compris. C'est le moment de s'arreter si le classeur n'est
pas pret.

---

## 2. Les onglets et ce qu'ils deviennent

| Onglet | Macro | Ce qui en sort |
|---|---|---|
| Config | toutes | periode de tache, zones MW / MX, dossier libs |
| Cartes API | ImporterProgramme | `CarteXX_RrSm` (tableau de voies, taille du bloc), `IO_XX_RrSm` (DFB_IO_*) |
| ES | ImporterProgramme | les reglages de chaque voie, `Mapping_Entrees` / `Mapping_Sorties` |
| Equipements | ImporterProgramme | `Pompes`, `Vannes`... (ARRAY[0..15]), `EQ_Pompes`... (DFB_EQ_*), `Equipements` |
| Cablage | ImporterProgramme | `Cablage_Entrees` / `Cablage_Sorties` |
| Liens | ImporterProgramme | `Reports_TC`, `Reports_TM_TA`, alarmes d'equipement |
| Reglages | ImporterReglages | `Reglages_Init` (gardee) |
| Paliers | ImporterControle | `PAL_<groupe>` (DFB_CTL_LEVEL) |
| Rotations | ImporterControle | `ROT_<groupe>` (DFB_SEQ_ROTATE) |
| Modes | ImporterControle | `GestionModes`, `Mode_Courant`, le CASE des sous-routines |
| Interverrouillages | ImporterControle | `ITL_<groupe>` (DFB_ITL_MATRIX) |
| Modbus | ImporterModbus | `MB_<liaison>` (DFB_COM_MODBUS), `Modbus_Entrees` / `Modbus_Sorties` |
| Grafcet Etapes | ImporterGrafcet | `Etapes_<g>`, `GC_<g>` (DFB_GRAFCETENGINE), `Ctrl_<g>`, la sous-routine `Grafcet_<g>` |
| Grafcet Transitions | ImporterGrafcet | les transitions et leurs receptivites, dans `Grafcet_<g>` |
| Grafcet Actions | ImporterGrafcet | les actions et ce qu'elles commandent, dans `Grafcet_<g>` |
| Grafcets | ImporterGrafcet | quand chaque grafcet tourne (son Enable) - facultatif |

Les six onglets de controle et les quatre des grafcets sont FACULTATIFS. Leur
forme est celle de `onglets-a-ajouter.xlsx` : a copier dans le classeur
d'affaire, une regle par onglet dans sa page LISEZ-MOI.

**Les tableaux d'equipements ont la taille du bloc** (ARRAY[0..15]), pas celle
de l'onglet : le bloc les prend en InOut, et Control Expert refuse un tableau
d'une autre taille. `Count` dit combien sont traites. Plus de 16 equipements
dans un tableau : deux tableaux (colonne Variable).

---

## 3. L'ordre de la tache : `RangerSections`

Chaque import la lance a la fin. Elle ne deplace que les sections generees qui
sont dans le mauvais sens ; les tiennes ne bougent pas.

    Horloge            DFB_SYS_CLOCK : le temps reel du cycle
    Alarmes_Init       \
    Reglages_Init       |  GARDEES : elles ne font quelque chose qu'au premier
    Controle_Init       |  cycle, et apres un import qui a change leur contenu
    Modbus_Init        /
    Simulation         les retours simules, AVANT les blocs d'entree
    Mapping_Entrees    %I / %IW  ->  voies
    Modbus_Entrees     ce qui arrive des equipements distants
    Cablage_Entrees    voies  ->  equipements
    Reports_TC         l'IHM commande
    Modes              le gestionnaire, puis les sous-routines du mode actif
    Grafcets           le temps des grafcets, puis Grafcet_<g>() pour chacun
    Controle           paliers, rotations, puis interverrouillages EN DERNIER
    Equipements        les DFB_EQ_*
    Alarmes_Cycle      etats d'alarme, puis gestionnaires
    Reports_TM_TA      vers l'IHM
    Modbus_Sorties     ce qui repart vers les equipements distants
    Cablage_Sorties    equipements  ->  voies
    Mapping_Sorties    voies  ->  %Q / %QW
    Diagnostic         bits systeme, modules en erreur

**Les sections gardees** s'ecrivent `IF X_Sig <> 1234 THEN ... X_Sig := 1234;
END_IF;`. La signature est calculee sur le contenu : un import qui ne change
rien laisse la section dormir, un import qui change la liste la fait rejouer
une fois. Pas de `%S13` : une modification en ligne ne passe pas par un
redemarrage.

**Les grafcets passent apres les modes et avant le controle** : un grafcet
peut s'arreter sur le mode (colonne Marche de l'onglet Grafcets), et les
interverrouillages ont le dernier mot sur ce qu'il commande.

---

## 4. Les alarmes

- **Un registre de 64** (DFB_ALM_MANAGER) tant qu'elles y tiennent ; au-dela,
  des registres de 256 (DFB_ALM_MANAGER256 : `AlmDefs`, `AlmDefs_2`...) qui
  partagent le meme `AlmCmd`. Aucune alarme n'est perdue.
- **Les alarmes de voie** : une entree TOR avec AlarmEn a O, chaque seuil
  S0..S3 actif d'une mesure. **Les alarmes d'equipement** : les lignes TA de
  l'onglet Liens (gravite dans la colonne Sev, sinon la question `sevEq`).
- **L'UUID porte la provenance** : genre, rack, module, voie, seuil. Un
  identifiant lu dans un journal dans cinq ans dit encore d'ou il vient.
- **La liste est ecrite en CSV** a cote du classeur (`alarmes.csv` :
  registre, emplacement, UUID, libelle, lien, gravite, origine). C'est ce
  qu'on donne a l'integrateur IHM.
- Les gestionnaires sont dates par `Horloge.EpochS`.

---

## 5. Les cartes mixtes

DDM 16022, DDM 16025, DDM 3202K : les sorties sont les voies 16 et suivantes.
AMM 0600 : les sorties sont les voies 4 et 5. Le classeur declare une carte
mixte en DEUX lignes de Cartes API (une par sens, meme emplacement) et numerote
ses voies depuis 0 dans les deux sens : l'import decale les sorties a leur
vraie voie et le dit. `GenererDiagnostic` ne surveille l'emplacement qu'une
fois.

---

## 6. La simulation : `GenererSimulation`

Une section `Simulation`, placee avant les blocs d'entree, qui renvoie a chaque
voie ce que la machine repondrait : retour de marche = commande, fin de course
= commande de sa position, thermique et arret d'urgence sains, mesures entre
leurs seuils, variateurs Modbus emules (CiA402). Les voies sans equipement qui
portent une alarme sont simulees a l'etat sain.

**Tout est sous `SimActive`.** A FALSE, la section rend la main aux cartes et
ne fait rien d'autre : elle peut rester dans la tache. `SimActive` se met a
TRUE depuis une table d'animation, JAMAIS sur site.

Sans automate : `tools/mac_run` fait tourner la macro puis le programme genere
dans le simulateur de l'outil :

    mac_run ImporterClasseur.mac affaire.xlsm libs --court --cycles 1000
            --poser "%SW0=20,SimActive=TRUE,%MW1001=512" --lire "Mode_Courant,Pompes[0].State"

(`%SW0=20` : le simulateur ne tient pas les mots systeme ; `%MW1001=512` : le
bit Auto de l'exemple.)

---

## 7. Les grafcets : `ImporterGrafcet`

Quatre onglets, une colonne `Grafcet` qui dit a quel grafcet appartient chaque
ligne. Le detail du moteur (DFB_GRAFCETENGINE 1.49) et les bugs corriges sont
dans `libs/Client/LISEZ-MOI-GRAFCET.md`.

    Grafcet Etapes        Grafcet, Etape (0..27), Nom (4 car.), Initiale, Finale
    Grafcet Transitions   Grafcet, Transition, De (2 ou 2,3), Vers (4 ou 4,5),
                          Receptivite, Tempo (s), Texte (8 car.)
    Grafcet Actions       Grafcet, Action, Etape, Nom (8 car.), Qualificatif
                          (0..11 ou son nom), Retard (s), Condition, Cible
    Grafcets              Grafcet, Marche (facultatif)

**Les raccourcis**, dans une receptivite ou une condition d'action :
`FIN(A2)` l'action 2 a fini, `ACTIF(X3)`, `DUREE(X3) >= T#5s`,
`FINI(Autre)` le grafcet Autre est fini (et pas en train d'etre relance).

**La cible d'une action**, trois formes :

- une variable BOOL (`Moteurs[0].RunReq`) : le grafcet la POSSEDE, elle vaut le
  OU des actions qui la citent, a chaque cycle ;
- une instruction (`Vannes[1].PosReq := 1;`) : executee quand l'action sort,
  rien le reste du temps ;
- `INIT(Autre)` : relance le grafcet Autre.

**Ce qui est genere, par grafcet** : les tableaux `Etapes_<g>`, `Trans_<g>`,
`Acts_<g>`, le moteur `GC_<g>`, `Ctrl_<g>` (ST_GC_CTRL, pour l'IHM) et la
sous-routine `Grafcet_<g>` : la construction (une fois, gardee par
`GC_<g>_Sig`), les receptivites AVANT l'appel du moteur, l'appel, l'etat, les
sorties, la remise a zero des commandes de l'IHM APRES l'appel.
`PreparerGrafcets` importe la bibliotheque, cree `GC_Builder`, `GC_Temps` et
la section `Grafcets` qui fait avancer le temps et appelle chaque grafcet.

**Sans classeur** : `GenererGrafcet nom=Remplissage etapes=ATT,REMP,VID`
ecrit le squelette (etapes en sequence, receptivites a FALSE, moteur appele),
a completer a la main. Elle ne reecrit jamais un grafcet existant.

**Relancer l'import** reecrit les sous-routines `Grafcet_<g>` ; les appels de
la section `Grafcets` sont gardes (ceux de GenererGrafcet compris).

---

## 8. Les autres macros

**Une etape a la fois** (supposent que les precedentes ont tourne) :
ImporterCartes, ImporterVoies, ImporterEquipements, ImporterCablage,
ImporterReports, ImporterAlarmes. `ImporterProgramme` fait les six ensemble, et
mieux (registre dimensionne, alarmes d'equipement, horloge).

**Sans classeur** :

| Macro | Pour |
|---|---|
| CreerEquipement | un equipement de plus : type et bloc importes, tableau et instance, appel ecrit ou son Count releve |
| CreerSR / AppelerSR | une sous-routine et ses variables convenues `SR_In_x` / `SR_Out_x` ; l'appel `SR();` entre ses entrees et ses sorties |
| GenererModes | la section des modes, a la main (ImporterControle le fait depuis l'onglet Modes) |
| GenererGrafcet | le squelette d'un grafcet, a completer a la main |
| PreparerGrafcets | la bibliotheque GRAFCET, `GC_Builder`, `GC_Temps` et la section `Grafcets` (lancee par les deux precedentes) |
| CreerVariable, CreerInstanceDDT, CreerInstanceDFB | une variable, une instance, avec import du type au besoin |
| EnsureLibraryItem, EnsureSection | s'assurer qu'un element ou une section existe |

**La bibliotheque** :

| Macro | Pour |
|---|---|
| VerifierBibliotheque | ce que le projet a en retard sur `libs/` - ne modifie rien |
| MettreAJourBibliotheque | met a jour, les types d'abord, les blocs ensuite ; pas d'annulation |

---

## 9. Verifier

- **Avant** : `VerifierClasseur` (lancee par ImporterClasseur). Elle controle
  chaque champ cite contre sa famille, les adresses contre les zones de
  Config, les tailles de carte, les indices d'equipement, les onglets de
  controle, Modbus et grafcets (numeros, etapes citees, qualificatifs, INIT).
  `ImporterGrafcet` ajoute dans son apercu la structure (etape sans sortie,
  etape jamais atteinte) et les raccourcis vers ce qui n'existe pas.
- **Hors de l'outil** : `tools/mac_run` (la macro, le ST produit, le controle
  des appels de blocs, la simulation) et `tools/mac_check` (chaque macro parse,
  chaque question a son aide).
- **Sur site** : le cahier de recette, `tools/generer_recette.py classeur.xlsm`
  - une ligne par voie, par alarme, par report et par essai d'equipement.

---

## 10. A valider sur site

- **Modbus et Altivar** : adresses ADDM, premier registre et mots de chaque
  equipement (ETA 3201, CMD 8501... selon le modele). Tout tourne en
  simulation, rien n'a ete essaye contre un vrai variateur.
- **Les sous-routines de mode** creees vides (SR_Auto...) sont a remplir.
- **Les expressions de l'onglet** (Demande, Mesure, Condition, Receptivite,
  Marche) sont recopiees telles quelles : elles compilent si elles sont justes.
- **Les grafcets** : chaque sequence se deroule en simulation (`mac_run
  --cycles`), mais les receptivites et les temps sont ceux du classeur - a
  derouler une fois sur site, etape par etape (`Ctrl_<g>.Cmd.ModeReq := 2`,
  puis `StepReq`).
