# Maquette 1.40.0 : le nouveau design (proposition)

Pages HTML (1600 x 1000) rendues en PNG par `rendre.js` (Playwright) : `node rendre.js <ce dossier> 2`
(les images vont dans `hd/`, a 2x ; `1` : dans `v/`). Rien de client : des noms d'essai.

- `00-systeme.html` : le systeme commun (une couleur par logiciel : Admin violet, API Studio bleu, IHM Studio
  turquoise ; Inter et JetBrains Mono ; etats, surfaces, composants ; quatre principes).
- `01-admin-tableau.html`, `02-admin-licences.html` : l'Admin (le tableau de bord, la page Licences avec le
  PC libere vu seul).
- `03-ihm-studio.html`, `04-api-studio.html` : les Studios.
- `05-licence-client.html` : la fenetre Licence (le suivi de la demande dans sa propre bande ; son fond
  est la capture de `03`, rendue d'abord).
- `06-installateur-admin.html` : l'installateur de l'Admin.
