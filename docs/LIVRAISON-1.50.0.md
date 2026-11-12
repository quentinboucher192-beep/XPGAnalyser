# 1.50.0 : les méthodes des types IHM comme les fonctions, « Étirer », POINTER TO, « Dupliquer » pour 500 marques, les tableaux partout, les scripts C/C++, répondre dans le chat

Fait à partir de ta liste (12/11) et des quatre maquettes que tu as validées (tes réponses : « Elle s'écrira »
FUNCTION ; « Étirer » : une solution efficace ; POINTER TO, un vrai pointeur ; « Dupliquer » : i = la 1re profondeur,
j = la 2e).

## 0. En bref (dans les Studios)

**Les méthodes des types IHM = les fonctions des symboles** (maquette 01)

- « Ajouter une méthode… » ouvre **la même fenêtre** que « Nouvelle fonction du symbole » : Nom, Génériques, Retour,
  Unité, Portée (publique / privée), Virtuelle, Description, et « Elle s'écrira `FUNCTION PUBLIC VIRTUAL Demarrer : BOOL` ».
- L'éditeur d'une méthode a **les mêmes onglets** : Code, Paramètres, Locales, Constantes, Tests. **SELF** est la
  première ligne des Paramètres (grisée, verrouillée).
- **Les tests d'une méthode** partent d'une instance d'essai du type (« SELF avant » règle ses membres) ; « Lancer les
  tests », la colonne Tests de la liste.
- **Tes projets se convertissent seuls** à l'ouverture : les blocs VAR écrits dans le corps d'une méthode passent dans
  Locales / Constantes ; rien n'est perdu, le format du fichier ne change pas.
- L'aide à la saisie propose les méthodes après `Pompe1.` ; une méthode privée y est grisée.

**« Étirer »** (maquette 02)

- Clic droit d'un objet de la vue › **Étirer ›** (juste après Aligner), aussi dans la barre d'outils de la vue (« Étirer ▾ »)
  et le clic droit de l'explorateur : jusqu'au bord gauche / droit / haut / bas de la vue, toute la largeur, toute la
  hauteur, toute la vue ; jusqu'à l'objet voisin, au bord de la sélection, à un repère, à une colonne de la grille ;
  « Garder les marges » ; la fenêtre « Étirer… » pour régler finement.
- **L'aperçu au survol** (en pointillés turquoise, avec « + 396 px · largeur 412 → 808 ») ; rien ne bouge avant le clic ;
  **un seul Ctrl+Z** ; la barre d'état dit ce qui a changé.
- **Ctrl+Alt+Maj+flèche** : jusqu'au bord gauche / droit / haut / bas (ils étaient libres).
- Un groupe s'étire en entier ; un objet tourné, par sa boîte ; une ligne, dans son sens ; un objet verrouillé : grisé
  avec la raison. La marge gardée : celle de la grille de colonnes quand elle est affichée, sinon le pas d'aimantation
  (10 px) — la vue n'a pas de « marge » à elle.

@@POINTER@@

@@DUPLIQUER@@

@@TABLEAUX@@

**Le chat de la session** (l'onglet Session et la fenêtre de la session)

- **Répondre** à un message (le bouton au survol, le clic droit) : « En réponse à Léa : « … » » au-dessus de la saisie
  (✕ ou Échap pour annuler) ; la réponse montre l'extrait cité ; un clic dessus ramène au message d'origine.
- **Réduire / déballer** : au-delà de 5 lignes, les 4 premières et « … voir tout » ; « Réduire » ; « Tout réduire ».
- **Copier** (Ctrl+C, le clic droit) ; « Copier avec l'auteur et l'heure » (« 14:32 Léa Essai : … »).
- Un Studio plus ancien dans la même session reçoit la citation en première ligne du message.

## 0 bis. Dans l'Admin

- **L'installateur XPG Admin** : la page « Code d'invitation » ne se chevauche plus (la consigne entière, l'étiquette
  au-dessus du champ, le message d'erreur sous le champ) ; le Récapitulatif des deux installateurs revient à la ligne au
  lieu de couper les lignes longues.
- « Repartir de zéro » est proposé au premier lancement d'un Admin+ 1.49 **ou plus récent** (si tu passes directement à
  la 1.50, tu le vois quand même, une seule fois).

## 1. Ce qu'il te reste à faire

1. Installer **l'Admin+ 1.50.0** (par-dessus la 1.49.0), puis les **Studios 1.50.0**.
2. **Service en ligne › Déployer le service** (la 1.50.0 ; rien de nouveau côté service, il suit le numéro).
3. **Versions** : publier la 1.50.0 avec ses trois fichiers (Studios, Admin+, XPG Admin).
4. Ouvrir un projet qui a des types IHM avec des méthodes : la conversion se fait seule (tu peux vérifier les onglets).

## 2. Les essais

@@ESSAIS@@

## 3. Installer

@@EMPREINTES@@

**XPG Admin** (pour tes admins, à publier dans Versions) : `XPG-Admin-Setup-1.50.0.exe` (dans le zip).

| Fichier | SHA-256 | Taille |
|---|---|---|
@@XPGADMIN@@

## 4. Limites

@@LIMITES@@
- **Rien sur un vrai Windows** : sous Wine seulement.
