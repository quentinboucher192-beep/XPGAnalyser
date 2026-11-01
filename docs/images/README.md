# Captures d'écran de la documentation

**Ce dossier ne contient encore aucune capture.** Le dépôt ne contient pas de capture d'écran du logiciel (vérifié pour la 1.21.3) :

- les images de `installateur/images/` sont l'habillage de l'installateur (fabriqué par `outils/installateur_images.py`) ;
- les fichiers de `docs/maquettes/` sont des maquettes HTML de conception, pas des captures du logiciel ;
- les noms `PNG_…` cités par l'aide F1 (`@capture` dans `tools/guide-ihm/guide-ihm.txt`) désignent des captures produites hors du dépôt pour le guide Word/PDF ;
- seuls les logos des deux applications sont présents (`resources/logo/`).

Retour au [README](../../README.md#captures).

## Comment produire les captures

- **F12** dans l'application : capture de la fenêtre dans le dossier des captures (`src/help/Shortcuts.cpp`, `src/app/Capture.cpp`).
- **Séances rejouées** : `--script <séance> --captures <dossier> --size 1920x1080` rejoue une séance (`src/app/ScriptRunner.hpp`) et écrit ses captures ; c'est ainsi que sont faites les captures des livraisons (`src/main.cpp`). Les séances existantes sont dans `tools/sessions/`.
- Une capture doit montrer le logiciel réel, avec des données de démonstration (par exemple le projet modèle `bac/Armoire_Gaz` ou un projet créé pour l'occasion) et aucune donnée de client, clé API ou mot de passe.

## Captures à ajouter

| Fichier prévu | Contenu attendu | Document qui l'utilisera |
|---|---|---|
| `docs/images/api-accueil.png` | Accueil d'API Studio : projets récents, Nouveau projet, Ouvrir | README, [API-STUDIO.md](../API-STUDIO.md) |
| `docs/images/api-ecran-projet.png` | Écran du projet : arbre API, onglet d'une section ST, barre du haut | [API-STUDIO.md](../API-STUDIO.md#ecran) |
| `docs/images/api-tableau-de-bord.png` | Tableau de bord de l'API et sa liste « À regarder » | [API-STUDIO.md](../API-STUDIO.md#analyse) |
| `docs/images/api-variables.png` | Onglet Variables, une variable dépliée | [API-STUDIO.md](../API-STUDIO.md#variables) |
| `docs/images/api-grafcet.png` | Éditeur de grafcet et volet Code | [API-STUDIO.md](../API-STUDIO.md#grafcet) |
| `docs/images/api-simulation-debogage.png` | Simulation › Débogage en pause sur un point d'arrêt | [API-STUDIO.md](../API-STUDIO.md#simulation) |
| `docs/images/ihm-editeur.png` | Éditeur de vues : bibliothèque d'objets, vue, inspecteur | README, [IHM-STUDIO.md](../IHM-STUDIO.md#editeur) |
| `docs/images/ihm-equipements.png` | Configuration › Équipements, un équipement Modbus TCP et ses variables liées | [IHM-STUDIO.md](../IHM-STUDIO.md#communication) |
| `docs/images/ihm-compiler.png` | Panneau du bas après Compiler (F7), un constat localisé | [IHM-STUDIO.md](../IHM-STUDIO.md#compiler) |
| `docs/images/ihm-simulation.png` | L'IHM en simulation avec un équipement simulé | [IHM-STUDIO.md](../IHM-STUDIO.md#simulation) |
| `docs/images/ihm-poste.png` | Poste d'exploitation en plein écran | [IHM-STUDIO.md](../IHM-STUDIO.md#poste) |
| `docs/images/ia-panneau.png` | Panneau Assistant IA : une conversation et une carte « Opérations proposées » | README, [ASSISTANT-IA.md](../ASSISTANT-IA.md#application) |
| `docs/images/ia-previsualisation.png` | Fenêtre « Prévisualisation des modifications » | [ASSISTANT-IA.md](../ASSISTANT-IA.md#application) |
| `docs/images/ia-proposer-la-suite.png` | Carte appliquée avec « Suite prévue » et le bouton « Proposer la suite » | [ASSISTANT-IA.md](../ASSISTANT-IA.md#suite) |
| `docs/images/ia-parametres.png` | Paramètres de l'Assistant IA (fournisseur, modèle, clés masquées) | [ASSISTANT-IA.md](../ASSISTANT-IA.md#fournisseurs) |
| `docs/images/ia-notifications.png` | Onglet Notifications du panneau | [ASSISTANT-IA.md](../ASSISTANT-IA.md#notifications) |
| `docs/images/ia-memoire.png` | Fenêtre « Mémoire et apprentissage de l'IA », onglet Vue générale | [ASSISTANT-IA.md](../ASSISTANT-IA.md#memoire) |
| `docs/images/installateur-profil.png` | Page « Que veux-tu installer ? » de l'installateur | [GUIDE-INSTALLATION.md](../GUIDE-INSTALLATION.md) |

Quand une capture est ajoutée, la référencer depuis le document indiqué avec un chemin relatif et un texte alternatif qui décrit ce qu'elle montre.
