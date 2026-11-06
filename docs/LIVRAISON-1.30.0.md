# API Studio et IHM Studio 1.30.0 : les licences — la fenêtre Licence, l'activation par fichier, les entrées verrouillées, l'outil de l'éditeur

Faite à partir de tes demandes du 06/11 :
- « continue avec la 1.30 les licences » ;
- « dans l'exe de l'admin (moi) voir la liste de toutes les personnes qui ont installé le logiciel ».

Elle suit la maquette validée (onglets Admin, Client, « Dans API Studio / IHM Studio »), **sans service en ligne** : les licences sont signées par l'éditeur, vérifiées hors ligne et activées par fichier.

Elle se met à jour par-dessus la 1.29.0 et garde tes données.

## 0. À lire d'abord : la clé de l'éditeur, et pourquoi rien n'est verrouillé aujourd'hui

**Tant que ta clé d'éditeur n'est pas dans les programmes, les licences sont « non activées » : tout est ouvert.** C'est voulu :
- un programme qui contiendrait une clé d'essai pourrait recevoir de fausses licences ;
- un programme sans clé qui verrouillerait quand même bloquerait tout le monde, toi compris.

La clé d'essai ne sert qu'aux essais et aux captures. Le contrôle du paquet arrête la construction si elle se trouve dans un programme à livrer.

**Ce que tu as à faire, une fois, sur ton PC** (tout est expliqué pas à pas dans `LISEZ-MOI-EDITEUR.txt`, dans le zip de l'éditeur livré à part) :
1. `XPG-License-Admin.exe creer-cle --nom "…"` : ta clé, protégée par Windows pour ton compte. La partie secrète ne quitte jamais ton PC.
2. Une copie de secours chiffrée et hors ligne du fichier de la clé. Sans elle, plus aucune licence ne peut être signée.
3. `exporter-cle --sortie PublisherKey.hpp` : la clé **publique**.
4. `signer-registre --entree registre.json --sortie registre.xpgreg` : tes éditions et fonctionnalités, signées. Change d'abord le registre si tu veux.
5. Envoie-moi **seulement** `PublisherKey.hpp` et `registre.xpgreg`, jamais le fichier de la clé. La version suivante les embarquera : les licences y seront actives.

## 1. Ce qui change pour toi

### La fenêtre Licence (dans IHM Studio et API Studio)

**Aide › Licence…**, ou la pastille **Licence** de la barre d'état, en bas à droite. La pastille dit « Licence PROFESSIONAL · valide jusqu'au … », « Sans licence · édition FREE », etc. ; aujourd'hui, « Licences non activées ».

En tête : l'édition, le statut, le client, la licence, les PC autorisés et liés, la date de fin, le registre, et la phrase de l'état. Six onglets :
- **Ma licence** : ce que dit la licence signée (identifiant, type, durée, grâce, révocations) ;
- **Mes appareils** : les PC liés, sous le nom que chacun s'est donné et l'empreinte de sa clé ; « ce PC » est marqué ;
- **Fonctionnalités** : chacune, ouverte ou « pas dans ta licence », ses limites, et **où elle est dans le programme** ;
- **Éditions** : ce qu'ouvre chaque édition du registre ; la tienne est marquée ;
- **Historique** : les imports (acceptés ou refusés, avec la raison), les demandes, les changements d'état ;
- **Paramètres** : le dossier des licences de ce PC, son empreinte, le registre et la liste de révocation en vigueur, la clé de l'éditeur (présente ou non).

En bas : **Activer par fichier de licence…**, **Demander une activation…**, **Ouvrir le dossier des licences**.

### L'activation par fichier
1. **Demander une activation…** écrit un `.xpgreq`. Il porte la clé publique du PC (créée sur place, protégée par Windows), le nom qu'on lui donne et le logiciel. Rien du matériel n'y est.
2. Le client te l'envoie ; tu signes sa licence avec l'outil de l'éditeur.
3. **Activer par fichier de licence…** installe le `.xpglic`, mais seulement s'il vaut pour ce PC, maintenant. Sinon rien ne change, et la raison est dite.

### Les entrées « pas dans ta licence »

Un seul contrôle, placé aux points d'entrée des fonctionnalités. La liste est dans l'onglet Fonctionnalités, colonne « Dans le programme » :

| Fonctionnalité | Où |
|---|---|
| `ai_assistant` | le panneau de l'Assistant IA, l'envoi d'une demande |
| `ai_generation` | créer une mission IA |
| `ai_learning` | Mémoire et apprentissage de l'IA |
| `api_simulation` | Simuler l'automate, un cycle |
| `hmi_runtime` | Démarrer l'IHM, Essayer le poste |
| `api_export` | Exporter vers Control Expert |
| `api_debugger` | API › Tables d'animation |
| `api_library` | API › Bibliothèques (mise à jour) |
| `hmi_scripts` | Scripts généraux, Fonctions |
| `hmi_export` | Exporter les vues |
| `advanced_import` | Importer des vues |
| `advanced_export` | IHM › Rapports |
| `professional_tools` | Déploiement réseau, Outil Modbus |

Une entrée fermée garde sa place. Un clic ouvre **Pas dans ta licence** : ce qui manque, l'état de ta licence, et **Licence…** pour la voir.

**Jamais de perte de travail** :
- Ouvrir, Enregistrer, Enregistrer sous, Exporter le projet, Fermer et Quitter ne sont **jamais** verrouillés (un essai le vérifie) ;
- une licence qui prend fin pendant le travail ne ferme rien ;
- une demande déjà partie va à son terme ;
- le **poste d'exploitation** n'est jamais arrêté par la licence du PC.

Le choix des points d'entrée est le mien : les identifiants du registre viennent de la maquette. `api_editor`, `api_compiler`, `hmi_editor`, `hmi_widgets`, `hmi_animations`, `ai_advanced_models`, `floating_licenses` et `multi_user` n'ont pas encore d'entrée (l'onglet Fonctionnalités le dit). Dis-moi s'il faut en déplacer ou en ajouter.

### L'outil de l'éditeur (`XPG-License-Admin.exe`, livré à part, pour toi seul)
- Ses commandes :
  - `creer-cle`, `cle`, `exporter-cle` ;
  - `signer-registre`, `signer-licence` (un PC de plus : `--licence ancienne.xpglic`), `signer-revocations` ;
  - `verifier`.
- **Le registre des activations** (ta demande) : chaque licence signée y note une ligne par PC lié (client, licence, édition, nom du PC, empreinte, logiciel et version, dates). `enregistrer-demande` note une demande reçue pas encore signée ; les révocations y sont aussi.
- **`liste [--licence ID] [--client TEXTE] [--csv FICHIER]`** : tous les PC connus, avec leur état (demande, activé, révoqué, expiré) et les totaux. Le CSV s'ouvre dans Excel.
- **Honnêtement** : sans service en ligne, tu ne connais que les PC qui t'envoient une demande. Voir **chaque lancement** du logiciel, même sans activation, demandera le service de licences en ligne. Il faudra aussi une information claire des utilisateurs (RGPD) : à faire avec l'hébergement.

### Où sont les licences
- **Sur ton PC d'administration** :
  - ta clé (DPAPI, ton compte) ;
  - le registre des activations (`activations.jsonl`, dans le dossier de la clé) ;
  - les fichiers que tu signes, là où tu les ranges.
- **Sur chaque PC client** : `C:\ProgramData\XPG\Licences`, qui contient la licence, le registre, les révocations, la clé du PC et l'historique. L'installateur crée ce dossier pour tous les comptes ; la désinstallation le garde.
- **Sur GitHub** : rien pour l'instant. Un dépôt privé comme source centrale (prévu par la maquette ; licences, registre et révocations signés, coordonnées des clients chiffrées, jamais la clé privée) peut venir dans une prochaine version si tu le veux.

## 2. Les captures

Prises sous Wine, dans le vrai programme Windows, sur un projet d'exemple. Les captures 01 à 13 viennent d'une copie du programme construite **avec la clé d'essai** (jamais livrée), pour montrer les licences en marche ; les captures 14 et 15 viennent du **programme livré**.

| Capture | Ce qu'elle montre |
|---|---|
| `1300_01_pastille_sans_licence` | la pastille de la barre d'état : « Sans licence · édition FREE » |
| `1300_02_licence_sans_licence` | la fenêtre Licence sans licence : statut Absente, l'édition FREE du registre |
| `1300_03_pas_dans_ta_licence` | l'Assistant IA en FREE : la fenêtre « Pas dans ta licence » ; rien n'est fermé |
| `1300_04_demande_activation` | Demander une activation… : le nom du PC, la licence, le fichier `.xpgreq` |
| `1300_05_activer_par_fichier` | Activer par fichier de licence… : le `.xpglic` signé par l'outil de l'éditeur |
| `1300_06_licence_importee` | la licence installée : PROFESSIONAL, Valide |
| `1300_07_ma_licence` | Ma licence : client, identifiant, durée, PC 1 sur 5, signature vérifiée |
| `1300_08_mes_appareils` | Mes appareils : ce PC, sous le nom donné, et l'empreinte de sa clé |
| `1300_09_fonctionnalites` | Fonctionnalités : ouvertes ou « pas dans ta licence », et où elles sont dans le programme |
| `1300_10_editions` | Éditions : FREE, PERSONAL, PROFESSIONAL, ENTERPRISE, EVALUATION, CUSTOM |
| `1300_11_historique` | Historique : la demande, l'import, le changement d'état |
| `1300_12_parametres` | Paramètres : le dossier des licences, l'empreinte du PC, le registre, la clé de l'éditeur |
| `1300_13_pastille_licence` | la pastille : « Licence PROFESSIONAL · valide jusqu'au 2027-11-06 » |
| `1300_14_livre_non_activees` | **le programme livré** : « Licences non activées » (pas encore de clé d'éditeur) |
| `1300_15_livre_fonctionnalites_ouvertes` | **le programme livré** : toutes les fonctionnalités ouvertes |

Les noms (« Entreprise ABC », « XPG-2026-00042 ») sont inventés pour l'essai.

## 3. Les essais

- **Linux** : 144 suites sur 144 réussies (`ctest`). Deux nouvelles :
  - `licensing` (239 contrôles) : la signature, le registre, les licences, la révocation, la grâce, l'horloge reculée, un PC de trop, un fichier abîmé, le registre des activations et `liste` ;
  - `licenceui` (29 contrôles) : la table des points d'entrée (jamais Ouvrir, Enregistrer, Exporter le projet, Fermer, Quitter), « Non activées » sans clé, puis une vraie chaîne avec une clé d'essai (licence PERSONAL signée et importée, ce qu'elle ouvre et ferme, un fichier abîmé refusé sans rien changer, Mes appareils, l'Historique).
- **Windows (sous Wine)** :
  - les programmes construits avec MinGW ;
  - **aucune trace de la clé d'essai** dans les programmes livrés (contrôlé octet par octet) ;
  - la fenêtre Licence ouverte et parcourue onglet par onglet par un script : demande d'activation écrite, licence signée par l'outil de l'éditeur, importée, « Valide / PROFESSIONAL » relu par `licence-etat`.
- **Un défaut trouvé par ces captures, et corrigé** : « Demander une activation… » plantait sous Windows. Le chemin proposé se construisait sur deux copies temporaires d'une même chaîne. C'est réparé, et la capture 04 le montre.
- **La mise à jour par-dessus la 1.29.0 (sous Wine)** :
  - l'installation passe, ta clé d'API est gardée ;
  - les programmes installés sont ceux construits ; versions 1.30.0, agent 1.1.0 ;
  - `--cli`, le bac à sable, les associations de fichiers et les sauvegardes marchent ;
  - `C:\ProgramData\XPG\Licences` est créé ;
  - rien de l'outil de l'éditeur n'est installé.

## 4. Installer

Comme d'habitude : lance `API-IHM-Studio-Setup-1.30.0.exe` (dans le zip) par-dessus la 1.29.0. Tes projets, tes réglages et ta clé d'API restent.

| Fichier | SHA-256 | Taille |
|---|---|---|
| `API-IHM-Studio-Setup-1.30.0.exe` | `0377EC4E61DFD5F52CFA38621D6F92F0E93DC3BB9E0325E549ADFAD7A2075309` | 54 708 615 octets |
| `API-Studio-1.30.0-portable.zip` | `CB707783FB6A7C1AC8017F8867FA040CCE762AD0615D06536E25BEB5FA2172D1` | 43 642 047 octets (190 fichiers) |
| `IHM-Studio-1.30.0-portable.zip` | `3B588BCCD7AB6B38DA50A6D37E35675D58D7D66C03D4383A2983BDA8EAE0974A` | 45 082 076 octets (191 fichiers) |

**L'outil de l'éditeur** (`XPG-License-Admin-1.30.0-EDITEUR.zip`) est livré **à part** : il est pour toi seul, ne le donne à personne et ne le mets pas avec l'installateur. Il contient `XPG-License-Admin.exe`, `LISEZ-MOI-EDITEUR.txt`, le registre d'exemple (`registre.json`) et un exemple de paramètres de licence.

## 5. Limites

- **Rien n'a été essayé sur un vrai Windows.** Ni le droit d'écriture du dossier des licences pour un second compte, ni un vrai pare-feu, ni DPAPI hors de Wine. Tout a été vérifié sous Linux, et sous Wine pour les exécutables Windows.
- **Les licences ne sont pas actives** dans ces programmes tant que ta clé n'y est pas (§ 0).
- **`outils/lib/package.ps1`** (la construction sous Windows) a reçu le même contrôle du paquet, mais il n'a pas pu être exécuté ici : il n'y a pas de PowerShell.
- **La sauvegarde de ta clé** est liée à ton compte Windows (DPAPI). Une sauvegarde protégée par mot de passe n'existe pas encore : garde une image complète de ton PC.
- **Pas encore faits** : l'interface graphique de l'outil de l'éditeur (la maquette) ; l'activation en ligne, les licences flottantes, la désactivation et le transfert en direct, voir chaque lancement (le service en ligne) ; la synchronisation GitHub.
