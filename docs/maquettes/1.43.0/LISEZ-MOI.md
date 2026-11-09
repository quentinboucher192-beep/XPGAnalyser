# Maquette 1.43.0 : plusieurs admins, le journal, les fichiers dans les notifications, les mises à jour (proposition)

Pages HTML (1600 x 1000). Les sources sont les `src-NN-*.html` : `python3 prep.py` en fait les `NN-*.html`
(il ajoute la barre latérale de l'Admin `<!--SIDE:cle-->`, avec les entrées « Administrateurs », « Journal des admins »
et « Versions » dans Sécurité, et la version 1.43.0 en bas ; le menu de la fenêtre Licence `<!--LICNAV:cle-->` et les mois
du mini calendrier `<!--CAL:...-->` comme en 1.42). Rendu en PNG : `node rendre.js <dossier> 1` (dans `v/`).
Les fonds floutés : `v/fond-ihm.png` (la planche IHM Studio de la 1.40, à 1600 x 1000, reprise de la 1.42) et
`v/fond-01-admin-admins.png` (`node rendre-fond.js <dossier> 01-admin-admins.html`, à refaire avant la 02).
Rien de client : des prénoms et des sociétés d'essai.

- `00` plusieurs admins, le principe : la clé de l'éditeur reste chez l'admin+, chaque admin a sa clé certifiée,
  les Studios vérifient seuls, retirer = révoquer le certificat, le journal signé ; les pouvoirs de chacun ;
- `01` la page Administrateurs (vue de l'admin+), et la même page vue par un admin (lecture seule) ;
- `02` ajouter un admin : son nom, il installe l'Admin (reçu dans ses notifications), l'empreinte lue au téléphone ;
- `03` le journal des admins : un par admin et le global, la criticité, le tri par colonne, une ligne dépliée ;
- `04` la criticité des actions (par type, « Me prévenir tout de suite »), le clic droit sur une ligne, l'alerte en direct ;
- `05` dans un Studio : une notification qui porte un fichier (l'installateur de l'Admin), signé et vérifié ;
- `06` la mise à jour : la notification sur tous les PC, les 2 installateurs sur le PC d'un admin, le toast, l'Admin ;
- `07` la page Versions : publier une version (les 2 installateurs, la version lue dans les fichiers), qui est à jour.

`maquette-1.43.0.html` : toutes les planches dans une seule page autonome (`python3 assembler.py <dossier> maquette-1.43.0.html`).
