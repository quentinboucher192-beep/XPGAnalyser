#!/bin/bash
# Rejoue les sessions de capture 1 a 58 (lots 1 a 21, lot macros 1, lots API 2 a 8 ; la 58 en cinq parties)
# dans un dossier neuf, avec les
# fichiers de donnees poses entre les sessions. Linux (SDL hors ecran).
#
#   XPG_ANALYZER=build/xpg_analyzer MAST=chemin/MAST.XPG ECHANTILLONS_SRC=tests/fixtures/ihm \
#     scripts/rejouer-toutes-les-sessions.sh [dossier]          (defaut : ./demo-ihm)
#
# Les captures arrivent dans <dossier>/captures, les journaux dans <dossier>/session*.log.
# Les sessions 37 a 39 (lot 15) tournent dans le reseau de laboratoire : root, et
# tools/labo/banc-modbus compile (voir tools/labo/reseau-labo.sh) ; sinon sautees.
L=$(cd "$(dirname "$0")" && pwd)
BIN=$(readlink -f "${XPG_ANALYZER:-build/xpg_analyzer}")
R=$(mkdir -p "${1:-demo-ihm}" && cd "${1:-demo-ihm}" && pwd)
export SDL_VIDEO_DRIVER=offscreen SDL_AUDIODRIVER=dummy
export XDG_CONFIG_HOME=$R/config PROJETS=$R/Projets XPG=$R/MAST.XPG
export ECHANTILLONS=$R/echantillons DONNEES=$R/Projets/Armoire_Gaz/donnees
# Lot API 4 : la configuration materielle des essais (CONFIG.XHW), importee dans le projet ouvert.
export XHW=$R/CONFIG.XHW
run() { timeout 1200 "$BIN" --script "$L/$1" --captures "$R/captures" --size 1920x1080 > "$R/$2.log" 2>&1; echo "$2 rc=$?"; }
rm -rf "$R"; mkdir -p "$R/config" "$R/Projets" "$R/captures"
cp "${MAST:?MAST : le chemin du MAST.XPG}" "$R/MAST.XPG"
cp "${XHW_SRC:-$(dirname "$MAST")/CONFIG.XHW}" "$R/CONFIG.XHW" 2>/dev/null || echo "CONFIG.XHW absent (XHW_SRC) : la session 54 le demande"
cp -r "${ECHANTILLONS_SRC:-tests/fixtures/ihm}" "$R/echantillons"
# Lot macros 1 : le classeur d'epreuve (tests/fixtures/macros) et la bibliotheque complete (libs/).
cp "${MACROS_SRC:-tests/fixtures/macros}"/* "$R/echantillons/" 2>/dev/null
LIBS=$(readlink -f "${LIBS_SRC:-libs}")
cd "$R"
run session-1-creation.txt session1
run session-2-reouverture.txt session2
mkdir -p "$DONNEES"
for f in consignes.csv journal.txt recettes_gaz.json equipements.xml historique.db parametres.xlsx; do cp "$ECHANTILLONS/$f" "$DONNEES/"; done
run session-3-ressources-fichiers.txt session3
sleep 1
printf 'Temporisation purge;25;s;B\n' >> "$DONNEES/consignes.csv"
rm -f "$DONNEES/journal.txt"
run session-4-reouverture-lot2.txt session4
run session-5-programmation.txt session5
run session-6-reouverture-lot3.txt session6
printf 'Jeu;Seuil poids A (kg);Seuil poids B (kg);Message\nArgon;9;9.5;%s\nMelange;10;75;%s\n' "'Argon Ar (import)'" "'Melange N2/Ar'" > "$DONNEES/reglages_import.csv"
run session-7-supervision.txt session7
run session-8-simulation-echange.txt session8
run session-9-reouverture-lot4.txt session9
run session-10-lot5.txt session10
run session-11-modeles.txt session11
run session-12-objets.txt session12
run session-13-ressources.txt session13
run session-14-fonctions.txt session14
run session-15-aide.txt session15
# Lot 8 : popups, utilisateurs, aide des objets (exemples animes).
run session-16-popups.txt session16
run session-17-utilisateurs.txt session17
run session-18-aide-objets.txt session18
# Lot 9 : les commandes, les variables systeme et d'instances, les afficheurs
# (et l'exemple fige de chacun des objets du lot).
run session-19-commandes.txt session19
run session-20-variables.txt session20
run session-21-afficheurs.txt session21
# Lot 10 : l'arbre API / IHM et le menu Parametres systeme, les 24 symboles de
# synoptique, le SVG net et recolorie, les symboles reutilisables (Carte_Armoire).
run session-22-api-parametres.txt session22
run session-23-synoptique.txt session23
run session-24-svg-symboles.txt session24
# Lot 11 : les graphiques, les objets des alarmes (consigne, sons, mise de cote),
# la production (TRS, tableau de variables, editeur de recette, exports CSV /
# Excel / PDF), la vanne reglante et les seuils des contenants.
run session-25-graphiques.txt session25
run session-26-alarmes.txt session26
run session-27-production.txt session27
# Lot 12 : la navigation et la structure (onglets, cadres, panneaux, plan a
# zones), le menu natif de connexion et le logo, concevoir plus vite (variables
# glissees, popups d'equipement, styles nommes, rechercher / remplacer, modeles
# de vues, ecart egal, vignettes).
run session-28-navigation.txt session28
run session-29-connexion.txt session29
run session-30-conception.txt session30
# Lot 13 : la securite (politique des mots de passe, verrouillage, signature
# electronique, avertissement de deconnexion, badge, journal d'audit), la qualite
# (Generer et Compiler en couleur, essais de reception, performances, dossier de
# l'IHM - ses images tirees des PDF), le multilingue et le confort (langues, unites
# et formats, taille du texte, daltonien, symboles, theme jour).
run session-31-securite.txt session31
run session-32-qualite.txt session32
bash "$L/pdf32.sh" "$R/Projets/Armoire_Gaz" "$R/captures"
run session-33-multilingue.txt session33
# Lot 14 : la communication reelle (Modbus TCP vers le serveur de demonstration),
# le poste d'exploitation (plein ecran, kiosque, ecrans secondaires), les
# notifications (la boite d'essai), un rapport periodique, l'acces par navigateur.
run session-34-communication.txt session34
run session-35-poste.txt session35
# La 36 : pendant la marche, une "tablette" (Chromium, par Playwright pour Python)
# se connecte a l'acces web et capture la page - s'il est la ; sinon la capture
# PNG_389 manque, rien d'autre. Puis la premiere page du rapport PDF en image.
if python3 -c "import playwright" 2>/dev/null; then
  python3 "$L/navigateur36.py" "$R/captures/PNG_389_navigateur.png" 8089 > "$R/navigateur36.log" 2>&1 &
  NAV=$!
fi
run session-36-notifications-rapports-web.txt session36
[ -n "$NAV" ] && { wait $NAV; echo "navigateur36 rc=$?"; }
PDF=$(ls -t "$R/Projets/Armoire_Gaz/exports"/rapport_*.pdf 2>/dev/null | head -1)
if [ -n "$PDF" ] && command -v pdftoppm > /dev/null; then
  pdftoppm -png -r 110 -f 1 -l 1 "$PDF" "$R/captures/rapport_page"
  mv "$R/captures"/rapport_page-*1.png "$R/captures/PNG_387_rapport_pdf.png"
fi
# Lot 15 : les equipements du reseau, le reseau du PC, le scanner IP (37),
# l'outil Modbus et l'espion (38) ; le poste d'exploitation et la reprise (39 A a
# D : trois arrets brutaux - kill -9 - puis un demarrage sans personne, --ihm).
# Dans le reseau de laboratoire : un "PC" a quatre ports (l'espace de noms
# "poste", nomme POSTE-ATELIER-01), des equipements Modbus qui vivent (banc-modbus).
LABO=${LABO:-$L/../tools/labo}
if [ "$(id -u)" = 0 ] && [ -x "$LABO/banc-modbus" ] && command -v ip > /dev/null; then
  bash "$LABO/reseau-labo.sh" start "$LABO/banc-modbus" > "$R/labo.log" 2>&1
  runlab() {  # runlab <script> <journal> [arguments...]
    local s=$1 l=$2; shift 2
    timeout 1500 ip netns exec poste unshare -u sh -c 'hostname POSTE-ATELIER-01; exec "$0" "$@"' \
      "$BIN" --script "$L/$s" --captures "$R/captures" --size 1920x1080 "$@" > "$R/$l.log" 2>&1
    echo "$l rc=$?"
  }
  crash_after() {  # crash_after <script> <journal> <capture qui dit "pret"> : l'arret brutal
    runlab "$1" "$2" &
    local pid=$! n=0
    while [ ! -f "$R/captures/$3" ] && [ $n -lt 900 ]; do sleep 1; n=$((n+1)); done
    sleep 3
    pkill -9 -f "^$BIN --script $L/$1"; wait $pid 2>/dev/null
    echo "$2 : arret brutal (kill -9) apres $n s"
  }
  runlab session-37-equipements.txt session37
  runlab session-38-outil-modbus.txt session38
  # La sauvegarde de reprise toutes les 10 s (2 min d'ordinaire) : la session 39 B
  # ne dure pas deux minutes.
  echo "recovery.autosaveSeconds = 10" >> "$XDG_CONFIG_HOME/xpg-analyzer/settings.txt"
  crash_after session-39a-poste-equipements.txt session39a PNG_415_poste_avant_coupure.png
  crash_after session-39b-reprise-poste.txt session39b PNG_418_conception_modifiee.png
  crash_after session-39c-reprise-conception.txt session39c PNG_425_poste_avant_coupure2.png
  runlab session-39d-poste-sans-personne.txt session39d --ihm "$R/Projets/Armoire_Gaz"
  bash "$LABO/reseau-labo.sh" stop
else
  echo "sessions 37 a 39 sautees : il faut le reseau de laboratoire (root, $LABO/banc-modbus compile)"
fi
# Lot 16 : les types IHM (structures), les tableaux et leurs proprietes, les
# dossiers des variables, la liaison dans la case (40) ; les scripts sur les
# tableaux et le plan d'adressage range par liaison (41) ; le GIF anime (42) ; le
# tutoriel de chaque objet et le glisser d'une variable dans un dossier (43).
run session-40-variables-types.txt session40
run session-41-scripts-plan.txt session41
run session-42-gif-anime.txt session42
run session-43-tutoriels.txt session43
# Lot 17 : les equipements et leurs jumeaux, les zones memoire, supprimer, glisser
# sur un port, le reseau simule (44) ; la carte memoire, le scanner, les
# chevauchements (45) ; les jumeaux en marche : comportement, panne simulee,
# l'outil Modbus et l'espion (46). De nouveau dans le reseau de laboratoire (la
# centrale et l'analyseur y ont des zones, comme de vrais appareils).
if [ "$(id -u)" = 0 ] && [ -x "$LABO/banc-modbus" ] && command -v ip > /dev/null; then
  bash "$LABO/reseau-labo.sh" start "$LABO/banc-modbus" > "$R/labo17.log" 2>&1
  runlab session-44-equipements-jumeaux.txt session44
  runlab session-45-carte-memoire.txt session45
  runlab session-46-jumeaux-en-marche.txt session46
  # Lot 18 : les valeurs simulees - animer, la zone de mouvement, forcer ; les
  # ecritures refusees (carte, outil Modbus) ; la simulation (onglet Jumeaux).
  runlab session-47-valeurs-simulees.txt session47
  # Lot 19 : l'historique - Ctrl+Z / Ctrl+Y partout, le tiroir (Ctrl+H), revenir
  # a un etat ; les sous-onglets qui ne tiennent pas ; Ctrl+S, le garde-fou.
  runlab session-48-historique-onglets.txt session48
  # Lot 20 : coller depuis Excel (une liste d'E/S dans les variables IHM, des noms
  # dans une vue), les modeles de vues et leur galerie, exporter / importer des
  # vues (le paquet Armoires_Sud.xpgvues des echantillons), Aller a (Ctrl+K).
  runlab session-49-excel-modeles.txt session49
  # Lot 21 : les versions du projet (creer, comparer, restaurer), le didacticiel en
  # parcours (la visite, « Creer un symbole de projet » pas a pas), les icones de
  # l'aide, les dossiers des listes de l'IHM et le glisser-deposer.
  runlab session-50-versions-didacticiel-dossiers.txt session50
  bash "$LABO/reseau-labo.sh" stop
else
  echo "sessions 44 a 50 sautees : il faut le reseau de laboratoire (root, $LABO/banc-modbus compile)"
fi
# Lot macros 1 : l'onglet Macros (dossiers, fiche), le formulaire (le bouton ..., Ctrl+V,
# glisser, l'apercu, Appliquer, Ctrl+Z), ranger (modele, dossier, glisser, renommer,
# corbeille), les quatre onglets de l'aide. Hors reseau de laboratoire.
rm -rf "$R/libs" && cp -r "$LIBS" "$R/libs"
run session-51-macros.txt session51
# Lot API 2 : la barre du haut et ses menus, l'arbre de l'API en francais, le tableau
# de bord, les onglets de l'API (Configuration, Taches, Sous-routines), les pastilles
# et la legende des macros, la simulation dans la barre.
run session-52-api-barre.txt session52
# Lot API 3 : les tables d'animation (creer, renommer, les variables de l'IHM et de
# l'automate, glisser depuis l'arbre, la simulation, ecrire, forcer, l'historique).
run session-53-api-tables-animation.txt session53
# Lot API 4 : Configuration (importer le .XHW, les racks, voies et adresses, reseau,
# plan memoire, remplacer un module), Taches, Ordre d'execution.
run session-54-api-configuration-taches-ordre.txt session54
# Lot API 5 : Types derives (renommer, + Champ, le refus d'une suppression), Blocs DFB
# (le bloc dessine, ses instances, creer une instance), Unites (renommer, les tables
# suivent), Variables (lues par l'IHM, ajouter a une table, renommer, CSV), Sous-routines,
# le plan memoire en trois zones (%M, %MW, %KW : taille, bornes, utilisation).
run session-55-api-types-dfb-unites-variables.txt session55
# Lot API 6 : l'etat et les versions (la version en cours dans la barre, Terminer,
# Livrer, deverrouiller ; la frise qui defile), coller depuis Excel (Variables,
# Unites et leurs variables, Tables d'animation et leurs structures depliees), les
# neuf themes, l'icone du projet, le mode Modifier des macros.
run session-56-api-macros-versions-themes-icone.txt session56
# Lot API 7 : l'accueil refait ; la simulation et les statistiques dans l'API (F9, Ctrl+5) ;
# le menu des onglets, les groupes cote a cote, la mosaique, une fenetre detachee ; deballer
# a toute profondeur ; renommer (API et IHM) ; importer un .XPG ; glisser un export ;
# chercher (commentaires, filtres par colonne, Aller a partout) ; le clic droit de l'arbre ;
# le didacticiel de l'API ; Ctrl+W.
run session-57-api-lot7.txt session57
# Lot API 8 : le dossier Simulation (Vue d'ensemble, Debogage, points d'arret et leur condition, modifier
# en pause et garder le cycle, Forcages, Journal) ; renommer cote IHM (les expressions suivent), Compiler et
# les expressions impossibles, le fx ; glisser un classeur, un PDF ; les themes (familles, editeur) ; les
# filtres retenus ; Generer maintenant (ou ?) ; l'explorateur de fichiers de l'appli ; Aide > Nouveautes,
# les parcours du lot ; les bandeaux haut et bas ; l'arbre du projet (filtre, rail, epingles, sante).
run session-58a-simulation.txt session58a
run session-58b-ihm-fichiers-themes.txt session58b
run session-58c-explorateur.txt session58c
run session-58d-aide-didacticiel.txt session58d
run session-58e-bandeaux-arbre.txt session58e
[ -f "$L/planche-lot15.py" ] && python3 "$L/planche-lot15.py" "$R/captures" > /dev/null 2>&1
[ -f "$L/planche-lot16.py" ] && python3 "$L/planche-lot16.py" "$R/captures" > /dev/null 2>&1
[ -f "$L/planche-lot17.py" ] && python3 "$L/planche-lot17.py" "$R/captures" > /dev/null 2>&1
[ -f "$L/planche-lot18.py" ] && python3 "$L/planche-lot18.py" "$R/captures" > /dev/null 2>&1
[ -f "$L/planche-lot19.py" ] && python3 "$L/planche-lot19.py" "$R/captures" > /dev/null 2>&1
[ -f "$L/planche-lot20.py" ] && python3 "$L/planche-lot20.py" "$R/captures" > /dev/null 2>&1
[ -f "$L/planche-lot21.py" ] && python3 "$L/planche-lot21.py" "$R/captures" > /dev/null 2>&1
[ -f "$L/planche-macros-lot1.py" ] && python3 "$L/planche-macros-lot1.py" "$R/captures" > /dev/null 2>&1
[ -f "$L/planche-api-lot2.py" ] && python3 "$L/planche-api-lot2.py" "$R/captures" > /dev/null 2>&1
[ -f "$L/planche-api-lot34.py" ] && python3 "$L/planche-api-lot34.py" "$R/captures" > /dev/null 2>&1
[ -f "$L/planche-api-lot5.py" ] && python3 "$L/planche-api-lot5.py" "$R/captures" > /dev/null 2>&1
[ -f "$L/planche-api-lot6.py" ] && python3 "$L/planche-api-lot6.py" "$R/captures" > /dev/null 2>&1
[ -f "$L/planche-api-lot7.py" ] && python3 "$L/planche-api-lot7.py" "$R/captures" > /dev/null 2>&1
[ -f "$L/planche-api-lot8.py" ] && python3 "$L/planche-api-lot8.py" "$R/captures" > /dev/null 2>&1
echo "--- echecs du script :"
grep -hE "^\[script\] ligne [0-9]+ :" "$R"/session*.log | head -30
echo "--- captures : $(ls "$R/captures" | wc -l)"
