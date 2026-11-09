# Maquette 1.42.0 : offres, options, achats, Clients en arbre, sessions (proposition)

Pages HTML (1600 x 1000). Les sources sont les `src-NN-*.html` : `python3 prep.py` en fait les `NN-*.html`
(il ajoute la barre laterale de l'Admin `<!--SIDE:cle-->`, le menu de la fenetre Licence `<!--LICNAV:cle-->` et les
mois du mini calendrier `<!--CAL:AAAA-MM:debut:fin:aujourdhui-->`). Rendu en PNG : `node rendre.js <dossier> 1`
(dans `v/`) ; les fonds floutes des dialogues sont `v/fond-ihm.png` (la planche IHM Studio de la 1.40, a 1600 x 1000)
et `v/fond-00-admin-offres.png` (`node rendre-fond.js <dossier> 00-admin-offres.html`). Rien de client : des noms d'essai.

- `00` les offres de l'Admin : l'offre temporaire (en vente du ... au ...), la duree de la licence, le mini calendrier ;
- `01` supprimer une offre (plus utilisee / encore utilisee) et un client ;
- `02` les options (des fonctionnalites vendues a part, pour un temps) ; `03` ajouter une option a une licence ;
- `04` la page Clients : Actualiser, le deballage en arbre (client > licences > PC, options, achats, demandes) ;
- `05` Mes achats (l'historique complet du client) ; `06` les options cote client ;
- `07` les sessions : le principe ; `08` demarrer ; `09` en session (IHM Studio) ; `10` rejoindre ; `11` en session (API Studio).

`maquette-1.42.0.html` : toutes les planches dans une seule page autonome (`python3 assembler.py <dossier> maquette-1.42.0.html`).
