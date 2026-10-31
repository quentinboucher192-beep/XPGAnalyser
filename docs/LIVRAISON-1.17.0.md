# API Studio et IHM Studio 1.17.0 : l'apprentissage de l'IA, visible, corrigeable, transportable

Livrée le 31/10/2026 en fin d'après-midi. Elle suit la 1.16.1 (`docs/LIVRAISON-1.16.1.md`), livrée l'après-midi même.

Tes demandes :
- « dans apprentissage l'onglet on comprend pas vraiment s'il progresse, y'a pas de graphique, rien ; améliore et trie par thème et ce qu'il sait, et qu'on puisse corriger à la main si nécessaire, avec options rechercher » (la maquette, vue et validée le matin) ;
- « dans l'installateur mettre des options pour directement lier des IA, et lier des apprentissages déjà faits qui pourraient provenir d'un autre PC et stockés sur un fichier, donc l'apprentissage doit être exportable/importable ».

En bref :
- **L'onglet Apprentissage devient un tableau de bord** : ce qu'elle sait, en chiffres et par thème, et **si elle progresse**, en deux graphiques par semaine.
- **Tout se corrige à la main** : un modèle métier (T_Pompe, T_Capteur…), ses membres et leur type, les mots qui l'appellent ; un mot de vocabulaire ; Rechercher, filtrer, trier.
- **« À lui apprendre »** : les demandes qu'elle n'a pas comprises, et le mot à lui apprendre pour chacune ; **« Essayer une phrase »** sans rien appliquer.
- **L'apprentissage dans un fichier `.xia`** : Exporter, Importer (avec ce qu'il ferait avant de confirmer).
- **L'installateur relie l'IA** (intégrée, Ollama, Claude, un service compatible) et **reprend un `.xia`** d'un autre PC.

**Ce qui reste à faire, et quand** : la **1.18.0** (demain vers 10 h) - les nouveaux outils de l'IA (types génériques `<T, K>`, symboles, vues modèles en-tête et pied de page, popups, équipements avec détection de ce qui répond sur chaque port, scripts, fonctions, variables système, ressources et fichiers joints, alarmes, dossiers, natives), les adresses, les variables liées aux équipements et les formules de lien dans les structures.

## 1. Ce qui change

### L'onglet Apprentissage : ce qu'elle sait, et qu'elle progresse

Fenêtre **Mémoire et apprentissage de l'IA** (paramètres de l'Assistant, ou Ctrl+K « mémoire »), onglet **Apprentissage** :
- **le bandeau**, toujours honnête : elle apprend sur ce PC (tes corrections, tes mots, tes modèles) ; l'IA intégrée s'en sert directement, les IA connectées les reçoivent avec chaque demande ; **le modèle distant n'est pas réentraîné**. À droite, **Importer…** et **Exporter l'apprentissage…** ;
- **quatre tuiles** : ce qu'elle sait (et ce qui s'est ajouté en 4 semaines, dont de ta main) ; les **demandes comprises** par l'IA intégrée sur 30 jours (et l'écart avec les 30 jours d'avant) ; les **propositions gardées** (appliquées plutôt que refusées) ; ce qu'il reste **à lui apprendre** ;
- **deux graphiques**, mesurés sur ce PC, par semaine (8 semaines, 3 mois ou 6 mois) : les demandes comprises et les propositions gardées (deux courbes, un axe en %) ; ce qu'elle a appris chaque semaine, par origine (tes corrections, l'Assistant, tes fichiers). Le survol d'un point ou d'une barre donne ses chiffres ;
- en bas, **les thèmes** (Modèles métier, Vocabulaire et synonymes, Genres d'objets, Propriétés et actions, Règles de création, Préférences, Contexte de projet, Connaissances métier, À lui apprendre) avec leur nombre et « +N » (ajoutés ces 4 semaines) ; au milieu, **la liste du thème** avec **Rechercher** (un nom, un mot, un membre, un type), l'état (actifs, désactivés, modifiés à la main) et le tri ; à droite, **le détail, modifiable** ;
- en pied, **Essayer une phrase**.

Les courbes partent de cette version : l'IA intégrée note chaque demande (comprise ou non) depuis la 1.16.1 ; il faut quelques jours d'utilisation pour qu'une courbe se dessine (avant, l'onglet le dit).

### Corriger à la main

- **Un modèle métier** (ce que propose « un type pour une pompe ») : le nom du type, les mots qui l'appellent, ses **membres et leur type** (double-clic ; le type se choisit dans une liste ; + Membre, Retirer, monter, descendre). **Enregistrer** : l'IA intégrée s'en sert dès la demande suivante. Un modèle du logiciel corrigé garde **Revenir à l'original** ; **Nouveau modèle…** en ajoute un (T_Compresseur) ; Désactiver, Supprimer. Un type de membre illisible est refusé.
- **Un mot** : ce qu'il désigne - un genre d'objet (« surpresseur » = une pompe), un modèle métier, ou un mot à ignorer. Les mots du logiciel se lisent ; les tiens se changent, se désactivent, s'oublient.
- Les autres thèmes gardent la page des connaissances en texte (Consulter, Modifier, Activer, Désactiver, Supprimer, Ajouter une connaissance…), filtrée par le thème et par la recherche.

### À lui apprendre, essayer une phrase

- **À lui apprendre** : les demandes que l'IA intégrée n'a pas comprises ces 7 jours. Choisis le mot qu'elle ne connaît pas et ce qu'il désigne, puis **Lui apprendre** : la demande est réessayée aussitôt et la réponse dit si elle est maintenant comprise. **Ignorer** la retire.
- **Essayer une phrase** : l'IA intégrée la lit sur le projet et la vue ouverts et dit ce qu'elle proposerait (« Ajouter 3 pompes », « T_Capteur : Mesure (REAL), … ») - sans rien appliquer ni rien compter.

### L'apprentissage dans un fichier (.xia)

- **Exporter l'apprentissage…** écrit un `.xia` : tes connaissances, tes modèles corrigés, tes mots, l'état des connaissances du logiciel. **Jamais la clé API.**
- **Importer…** dit d'abord ce qu'il ferait (« Importer l'apprentissage de PC-ATELIER-B ? 3 connaissances ajoutées, 1 gardée (déjà là) »), puis importe après ta confirmation (le bouton n'est pas rouge : rien ne s'efface). Une connaissance que ce PC a déjà est gardée ; un fichier d'une version plus récente est refusé, avec la raison.
- En séance : `ia apprentissage exporter "chemin.xia"`, `ia apprentissage voir "chemin.xia"`, `ia apprentissage importer "chemin.xia"`.

### L'installateur relie l'IA

Deux pages (après la bibliothèque ; pas sur le poste d'exploitation) :
- **L'Assistant IA** : garder le réglage actuel, **l'IA intégrée**, **Ollama** sur ce PC (l'adresse se remplit), **Anthropic (Claude)**, ou **un service compatible OpenAI** (son adresse est demandée) ; la case **IA intégrée d'abord** ; l'adresse et le modèle. **Aucune clé API ne passe par l'installateur** : pour Claude, elle se saisit ensuite dans le logiciel (protégée par Windows).
- **Un apprentissage d'un autre PC** : un fichier `.xia` (vide : rien).

L'installateur dépose `installation.json` et la copie du `.xia` dans `ProgramData\XPGAnalyser\IA` ; IHM Studio les applique **au premier lancement de chaque compte Windows**, une fois par installation, et le dit dans l'Assistant. En silencieux (un déploiement) : `/IA=integree|ollama|anthropic|compatible|garder /IAADRESSE=... /IAMODELE=... /IADABORD=oui /APPRENTISSAGE="D:\appris.xia"`.

## 2. Les essais

(à compléter)

## 3. Les captures

(à compléter)

## 4. Installer

(à compléter)

## 5. Les fichiers modifiés

(à compléter)

## 6. Les limites

- Les courbes se remplissent avec l'usage : les mesures de l'IA intégrée datent de la 1.16.1 (aujourd'hui). Les propositions gardées comptent aussi celles des IA connectées (leur état, dans les conversations).
- « Apprendre » reste la mémoire locale : l'IA intégrée lit tes corrections ; une IA connectée les reçoit avec chaque demande ; **aucun modèle distant n'est réentraîné**.
- Les pages de l'installateur ont été essayées sous Wine (en silencieux et à l'écran) ; pas encore sur un vrai Windows.
