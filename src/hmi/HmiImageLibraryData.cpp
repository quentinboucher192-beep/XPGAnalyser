// =============================================================================
//  hmi/HmiImageLibraryData.cpp - les images de la bibliotheque d'images (1.14.1, lot N6)
// -----------------------------------------------------------------------------
//  GENERE par tools/images/generer_images.py depuis tools/images/images.txt :
//  NE PAS MODIFIER A LA MAIN. Changer la description, puis :
//      python3 tools/images/generer_images.py --cpp src/hmi/HmiImageLibraryData.cpp
//  320 images, dessinees pour l'application (aucune image ni police d'icones d'ailleurs).
// =============================================================================
#include "HmiAssets.hpp"

namespace hmi::imagelib {

const std::vector<BuiltInImage>& builtInData() {
    static const std::vector<BuiltInImage> all = {
        // ---- fleche-haut
        {"fleche-haut", "Fl\xC3\xA8" "che haut", "Fl\xC3\xA8" "ches et navigation",
         "haut, monter, up, fl\xC3\xA8" "che",
         true, true, 64, 64,
         "<svg xmlns=\"http://www.w3.org/2000/svg\" width=\"64\" height=\"64\" viewBox=\"0 0 24 24\">"
         "<g stroke-linecap=\"round\" stroke-linejoin=\"round\">"
         "<path d=\"M12 20L12 4.5M16.178 9.479L12 4.5L7.822 9.479\" fill=\"none\" stroke=\"#E6EAF0\" stroke-width=\"2\"/>"
         "</g></svg>"},
        // ---- fleche-bas
        {"fleche-bas", "Fl\xC3\xA8" "che bas", "Fl\xC3\xA8" "ches et navigation",
         "bas, descendre, down, fl\xC3\xA8" "che",
         true, true, 64, 64,
         "<svg xmlns=\"http://www.w3.org/2000/svg\" width=\"64\" height=\"64\" viewBox=\"0 0 24 24\">"
         "<g stroke-linecap=\"round\" stroke-linejoin=\"round\">"
         "<path d=\"M12 4L12 19.5M7.822 14.521L12 19.5L16.178 14.521\" fill=\"none\" stroke=\"#E6EAF0\" stroke-width=\"2\"/>"
         "</g></svg>"},
        // ---- fleche-gauche
        {"fleche-gauche", "Fl\xC3\xA8" "che gauche", "Fl\xC3\xA8" "ches et navigation",
         "gauche, retour, left, fl\xC3\xA8" "che",
         true, true, 64, 64,
         "<svg xmlns=\"http://www.w3.org/2000/svg\" width=\"64\" height=\"64\" viewBox=\"0 0 24 24\">"
         "<g stroke-linecap=\"round\" stroke-linejoin=\"round\">"
         "<path d=\"M20 12L4.5 12M9.479 7.822L4.5 12L9.479 16.178\" fill=\"none\" stroke=\"#E6EAF0\" stroke-width=\"2\"/>"
         "</g></svg>"},
        // ---- fleche-droite
        {"fleche-droite", "Fl\xC3\xA8" "che droite", "Fl\xC3\xA8" "ches et navigation",
         "droite, avancer, right, fl\xC3\xA8" "che",
         true, true, 64, 64,
         "<svg xmlns=\"http://www.w3.org/2000/svg\" width=\"64\" height=\"64\" viewBox=\"0 0 24 24\">"
         "<g stroke-linecap=\"round\" stroke-linejoin=\"round\">"
         "<path d=\"M4 12L19.5 12M14.521 16.178L19.5 12L14.521 7.822\" fill=\"none\" stroke=\"#E6EAF0\" stroke-width=\"2\"/>"
         "</g></svg>"},
        // ---- fleche-haut-droite
        {"fleche-haut-droite", "Fl\xC3\xA8" "che diagonale haut", "Fl\xC3\xA8" "ches et navigation",
         "diagonale, haut, droite, fl\xC3\xA8" "che, ouvrir",
         true, true, 64, 64,
         "<svg xmlns=\"http://www.w3.org/2000/svg\" width=\"64\" height=\"64\" viewBox=\"0 0 24 24\">"
         "<g stroke-linecap=\"round\" stroke-linejoin=\"round\">"
         "<path d=\"M5.5 18.5L18.5 5.5M17.933 11.975L18.5 5.5L12.025 6.067\" fill=\"none\" stroke=\"#E6EAF0\" stroke-width=\"2\"/>"
         "</g></svg>"},
        // ---- fleche-bas-droite
        {"fleche-bas-droite", "Fl\xC3\xA8" "che diagonale bas", "Fl\xC3\xA8" "ches et navigation",
         "diagonale, bas, droite, fl\xC3\xA8" "che",
         true, true, 64, 64,
         "<svg xmlns=\"http://www.w3.org/2000/svg\" width=\"64\" height=\"64\" viewBox=\"0 0 24 24\">"
         "<g stroke-linecap=\"round\" stroke-linejoin=\"round\">"
         "<path d=\"M5.5 5.5L18.5 18.5M12.025 17.933L18.5 18.5L17.933 12.025\" fill=\"none\" stroke=\"#E6EAF0\" stroke-width=\"2\"/>"
         "</g></svg>"},
        // ---- chevron-haut
        {"chevron-haut", "Chevron haut", "Fl\xC3\xA8" "ches et navigation",
         "haut, monter, replier, chevron",
         true, true, 64, 64,
         "<svg xmlns=\"http://www.w3.org/2000/svg\" width=\"64\" height=\"64\" viewBox=\"0 0 24 24\">"
         "<g stroke-linecap=\"round\" stroke-linejoin=\"round\">"
         "<path d=\"M5 15.5l7-7 7 7\" fill=\"none\" stroke=\"#E6EAF0\" stroke-width=\"2\"/></g></svg>"},
        // ---- chevron-bas
        {"chevron-bas", "Chevron bas", "Fl\xC3\xA8" "ches et navigation",
         "bas, descendre, d\xC3\xA9plier, chevron",
         true, true, 64, 64,
         "<svg xmlns=\"http://www.w3.org/2000/svg\" width=\"64\" height=\"64\" viewBox=\"0 0 24 24\">"
         "<g stroke-linecap=\"round\" stroke-linejoin=\"round\">"
         "<path d=\"M5 8.5l7 7 7-7\" fill=\"none\" stroke=\"#E6EAF0\" stroke-width=\"2\"/></g></svg>"},
        // ---- precedent
        {"precedent", "Pr\xC3\xA9" "c\xC3\xA9" "dent", "Fl\xC3\xA8" "ches et navigation",
         "pr\xC3\xA9" "c\xC3\xA9" "dent, chevron, gauche, arri\xC3\xA8re, page",
         true, true, 64, 64,
         "<svg xmlns=\"http://www.w3.org/2000/svg\" width=\"64\" height=\"64\" viewBox=\"0 0 24 24\">"
         "<g stroke-linecap=\"round\" stroke-linejoin=\"round\">"
         "<path d=\"M15.5 5l-7 7 7 7\" fill=\"none\" stroke=\"#E6EAF0\" stroke-width=\"2\"/></g></svg>"},
        // ---- suivant
        {"suivant", "Suivant", "Fl\xC3\xA8" "ches et navigation",
         "suivant, chevron, droite, avant, page",
         true, true, 64, 64,
         "<svg xmlns=\"http://www.w3.org/2000/svg\" width=\"64\" height=\"64\" viewBox=\"0 0 24 24\">"
         "<g stroke-linecap=\"round\" stroke-linejoin=\"round\">"
         "<path d=\"M8.5 5l7 7-7 7\" fill=\"none\" stroke=\"#E6EAF0\" stroke-width=\"2\"/></g></svg>"},
        // ---- double-gauche
        {"double-gauche", "Retour rapide", "Fl\xC3\xA8" "ches et navigation",
         "double, gauche, reculer, chevrons",
         true, true, 64, 64,
         "<svg xmlns=\"http://www.w3.org/2000/svg\" width=\"64\" height=\"64\" viewBox=\"0 0 24 24\">"
         "<g stroke-linecap=\"round\" stroke-linejoin=\"round\">"
         "<path d=\"M18.5 6l-6 6 6 6\" fill=\"none\" stroke=\"#E6EAF0\" stroke-width=\"2\"/>"
         "<path d=\"M11.5 6l-6 6 6 6\" fill=\"none\" stroke=\"#E6EAF0\" stroke-width=\"2\"/></g></svg>"},
        // ---- double-droite
        {"double-droite", "Avance rapide", "Fl\xC3\xA8" "ches et navigation",
         "double, droite, avancer, chevrons",
         true, true, 64, 64,
         "<svg xmlns=\"http://www.w3.org/2000/svg\" width=\"64\" height=\"64\" viewBox=\"0 0 24 24\">"
         "<g stroke-linecap=\"round\" stroke-linejoin=\"round\">"
         "<path d=\"M5.5 6l6 6-6 6\" fill=\"none\" stroke=\"#E6EAF0\" stroke-width=\"2\"/>"
         "<path d=\"M12.5 6l6 6-6 6\" fill=\"none\" stroke=\"#E6EAF0\" stroke-width=\"2\"/></g></svg>"},
        // ---- premier
        {"premier", "Premier", "Fl\xC3\xA8" "ches et navigation",
         "premier, d\xC3\xA9" "but, gauche, origine",
         true, true, 64, 64,
         "<svg xmlns=\"http://www.w3.org/2000/svg\" width=\"64\" height=\"64\" viewBox=\"0 0 24 24\">"
         "<g stroke-linecap=\"round\" stroke-linejoin=\"round\">"
         "<path d=\"M6 5v14\" fill=\"none\" stroke=\"#E6EAF0\" stroke-width=\"2\"/>"
         "<path d=\"M18 5.5l-7 6.5 7 6.5\" fill=\"none\" stroke=\"#E6EAF0\" stroke-width=\"2\"/></g></svg>"},
        // ---- dernier
        {"dernier", "Dernier", "Fl\xC3\xA8" "ches et navigation",
         "dernier, fin, droite",
         true, true, 64, 64,
         "<svg xmlns=\"http://www.w3.org/2000/svg\" width=\"64\" height=\"64\" viewBox=\"0 0 24 24\">"
         "<g stroke-linecap=\"round\" stroke-linejoin=\"round\">"
         "<path d=\"M18 5v14\" fill=\"none\" stroke=\"#E6EAF0\" stroke-width=\"2\"/>"
         "<path d=\"M6 5.5l7 6.5-7 6.5\" fill=\"none\" stroke=\"#E6EAF0\" stroke-width=\"2\"/></g></svg>"},
        // ---- annuler
        {"annuler", "Annuler", "Fl\xC3\xA8" "ches et navigation",
         "annuler, retour, d\xC3\xA9" "faire, undo",
         true, true, 64, 64,
         "<svg xmlns=\"http://www.w3.org/2000/svg\" width=\"64\" height=\"64\" viewBox=\"0 0 24 24\">"
         "<g stroke-linecap=\"round\" stroke-linejoin=\"round\">"
         "<path d=\"M4.5 10h9.5a5.5 5.5 0 0 1 0 11H10\" fill=\"none\" stroke=\"#E6EAF0\" stroke-width=\"2\"/>"
         "<path d=\"M8.5 6L4.5 10l4 4\" fill=\"none\" stroke=\"#E6EAF0\" stroke-width=\"2\"/></g></svg>"},
        // ---- retablir
        {"retablir", "R\xC3\xA9tablir", "Fl\xC3\xA8" "ches et navigation",
         "r\xC3\xA9tablir, refaire, redo",
         true, true, 64, 64,
         "<svg xmlns=\"http://www.w3.org/2000/svg\" width=\"64\" height=\"64\" viewBox=\"0 0 24 24\">"
         "<g stroke-linecap=\"round\" stroke-linejoin=\"round\">"
         "<path d=\"M19.5 10H10a5.5 5.5 0 0 0 0 11h4\" fill=\"none\" stroke=\"#E6EAF0\" stroke-width=\"2\"/>"
         "<path d=\"M15.5 6l4 4-4 4\" fill=\"none\" stroke=\"#E6EAF0\" stroke-width=\"2\"/></g></svg>"},
        // ---- accueil
        {"accueil", "Accueil", "Fl\xC3\xA8" "ches et navigation",
         "accueil, maison, home, menu principal, d\xC3\xA9part",
         true, true, 64, 64,
         "<svg xmlns=\"http://www.w3.org/2000/svg\" width=\"64\" height=\"64\" viewBox=\"0 0 24 24\">"
         "<g stroke-linecap=\"round\" stroke-linejoin=\"round\">"
         "<path d=\"M3 11.5L12 4l9 7.5\" fill=\"none\" stroke=\"#E6EAF0\" stroke-width=\"2\"/>"
         "<path d=\"M5.5 9.5v11h4.5v-6h4v6h4.5v-11\" fill=\"none\" stroke=\"#E6EAF0\" stroke-width=\"2\"/></g></svg>"},
        // ---- rafraichir
        {"rafraichir", "Rafra\xC3\xAE" "chir", "Fl\xC3\xA8" "ches et navigation",
         "rafra\xC3\xAE" "chir, actualiser, recharger, refresh, cycle",
         true, true, 64, 64,
         "<svg xmlns=\"http://www.w3.org/2000/svg\" width=\"64\" height=\"64\" viewBox=\"0 0 24 24\">"
         "<g stroke-linecap=\"round\" stroke-linejoin=\"round\">"
         "<path d=\"M18.495 16.25A7.5 7.5 0 1 1 17.303 7.197M12.322 6.761L17.303 7.197L16.868 2.216\" fill=\"none\" stroke=\"#E6EAF0\" stroke-width=\"2\"/>"
         "</g></svg>"},
        // ---- rotation-horaire
        {"rotation-horaire", "Rotation horaire", "Fl\xC3\xA8" "ches et navigation",
         "rotation, tourner, horaire, sens",
         true, true, 64, 64,
         "<svg xmlns=\"http://www.w3.org/2000/svg\" width=\"64\" height=\"64\" viewBox=\"0 0 24 24\">"
         "<g stroke-linecap=\"round\" stroke-linejoin=\"round\">"
         "<path d=\"M4.952 10.435A7.5 7.5 0 1 1 12 20.5M15.83 17.286L12 20.5L15.83 23.714\" fill=\"none\" stroke=\"#E6EAF0\" stroke-width=\"2\"/>"
         "</g></svg>"},
        // ---- rotation-antihoraire
        {"rotation-antihoraire", "Rotation antihoraire", "Fl\xC3\xA8" "ches et navigation",
         "rotation, tourner, antihoraire, sens",
         true, true, 64, 64,
         "<svg xmlns=\"http://www.w3.org/2000/svg\" width=\"64\" height=\"64\" viewBox=\"0 0 24 24\">"
         "<g stroke-linecap=\"round\" stroke-linejoin=\"round\">"
         "<path d=\"M19.048 10.435A7.5 7.5 0 1 0 12 20.5M8.17 23.714L12 20.5L8.17 17.286\" fill=\"none\" stroke=\"#E6EAF0\" stroke-width=\"2\"/>"
         "</g></svg>"},
        // ---- menu
        {"menu", "Menu", "Fl\xC3\xA8" "ches et navigation",
         "menu, liste, hamburger, options",
         true, true, 64, 64,
         "<svg xmlns=\"http://www.w3.org/2000/svg\" width=\"64\" height=\"64\" viewBox=\"0 0 24 24\">"
         "<g stroke-linecap=\"round\" stroke-linejoin=\"round\">"
         "<path d=\"M4 6.5h16M4 12h16M4 17.5h16\" fill=\"none\" stroke=\"#E6EAF0\" stroke-width=\"2\"/></g></svg>"},
        // ---- plein-ecran
        {"plein-ecran", "Plein \xC3\xA9" "cran", "Fl\xC3\xA8" "ches et navigation",
         "plein \xC3\xA9" "cran, agrandir, maximiser",
         true, true, 64, 64,
         "<svg xmlns=\"http://www.w3.org/2000/svg\" width=\"64\" height=\"64\" viewBox=\"0 0 24 24\">"
         "<g stroke-linecap=\"round\" stroke-linejoin=\"round\">"
         "<path d=\"M4 9V4h5M15 4h5v5M20 15v5h-5M9 20H4v-5\" fill=\"none\" stroke=\"#E6EAF0\" stroke-width=\"2\"/>"
         "</g></svg>"},
        // ---- reduire
        {"reduire", "R\xC3\xA9" "duire", "Fl\xC3\xA8" "ches et navigation",
         "r\xC3\xA9" "duire, quitter le plein \xC3\xA9" "cran, minimiser",
         true, true, 64, 64,
         "<svg xmlns=\"http://www.w3.org/2000/svg\" width=\"64\" height=\"64\" viewBox=\"0 0 24 24\">"
         "<g stroke-linecap=\"round\" stroke-linejoin=\"round\">"
         "<path d=\"M9 4v5H4M20 9h-5V4M15 20v-5h5M4 15h5v5\" fill=\"none\" stroke=\"#E6EAF0\" stroke-width=\"2\"/>"
         "</g></svg>"},
        // ---- deplacer
        {"deplacer", "D\xC3\xA9placer", "Fl\xC3\xA8" "ches et navigation",
         "d\xC3\xA9placer, bouger, quatre directions, pan",
         true, true, 64, 64,
         "<svg xmlns=\"http://www.w3.org/2000/svg\" width=\"64\" height=\"64\" viewBox=\"0 0 24 24\">"
         "<g stroke-linecap=\"round\" stroke-linejoin=\"round\">"
         "<path d=\"M12 3v18M3 12h18\" fill=\"none\" stroke=\"#E6EAF0\" stroke-width=\"2\"/>"
         "<path d=\"M9.5 5.5L12 3l2.5 2.5M9.5 18.5L12 21l2.5-2.5M5.5 9.5L3 12l2.5 2.5M18.5 9.5L21 12l-2.5 2.5\" fill=\"none\" stroke=\"#E6EAF0\" stroke-width=\"2\"/>"
         "</g></svg>"},
        // ---- trier
        {"trier", "Trier", "Fl\xC3\xA8" "ches et navigation",
         "trier, haut, bas, ordre, tri",
         true, true, 64, 64,
         "<svg xmlns=\"http://www.w3.org/2000/svg\" width=\"64\" height=\"64\" viewBox=\"0 0 24 24\">"
         "<g stroke-linecap=\"round\" stroke-linejoin=\"round\">"
         "<path d=\"M8 20L8 4M10.893 7.447L8 4L5.107 7.447\" fill=\"none\" stroke=\"#E6EAF0\" stroke-width=\"2\"/>"
         "<path d=\"M16 4L16 20M13.107 16.553L16 20L18.893 16.553\" fill=\"none\" stroke=\"#E6EAF0\" stroke-width=\"2\"/>"
         "</g></svg>"},
        // ---- echanger
        {"echanger", "\xC3\x89" "changer", "Fl\xC3\xA8" "ches et navigation",
         "\xC3\xA9" "changer, permuter, gauche, droite, swap",
         true, true, 64, 64,
         "<svg xmlns=\"http://www.w3.org/2000/svg\" width=\"64\" height=\"64\" viewBox=\"0 0 24 24\">"
         "<g stroke-linecap=\"round\" stroke-linejoin=\"round\">"
         "<path d=\"M4 8L20 8M16.553 10.893L20 8L16.553 5.107\" fill=\"none\" stroke=\"#E6EAF0\" stroke-width=\"2\"/>"
         "<path d=\"M20 16L4 16M7.447 13.107L4 16L7.447 18.893\" fill=\"none\" stroke=\"#E6EAF0\" stroke-width=\"2\"/>"
         "</g></svg>"},
        // ---- sortie
        {"sortie", "Sortie", "Fl\xC3\xA8" "ches et navigation",
         "sortie, d\xC3\xA9" "connexion, quitter, logout",
         true, true, 64, 64,
         "<svg xmlns=\"http://www.w3.org/2000/svg\" width=\"64\" height=\"64\" viewBox=\"0 0 24 24\">"
         "<g stroke-linecap=\"round\" stroke-linejoin=\"round\">"
         "<path d=\"M10 4H5.5a1 1 0 0 0-1 1v14a1 1 0 0 0 1 1H10\" fill=\"none\" stroke=\"#E6EAF0\" stroke-width=\"2\"/>"
         "<path d=\"M9.5 12L20.5 12M17.053 14.893L20.5 12L17.053 9.107\" fill=\"none\" stroke=\"#E6EAF0\" stroke-width=\"2\"/>"
         "</g></svg>"},
        // ---- entree
        {"entree", "Entr\xC3\xA9" "e", "Fl\xC3\xA8" "ches et navigation",
         "entr\xC3\xA9" "e, connexion, login, entrer",
         true, true, 64, 64,
         "<svg xmlns=\"http://www.w3.org/2000/svg\" width=\"64\" height=\"64\" viewBox=\"0 0 24 24\">"
         "<g stroke-linecap=\"round\" stroke-linejoin=\"round\">"
         "<path d=\"M14 4h4.5a1 1 0 0 1 1 1v14a1 1 0 0 1-1 1H14\" fill=\"none\" stroke=\"#E6EAF0\" stroke-width=\"2\"/>"
         "<path d=\"M3.5 12L14.5 12M11.053 14.893L14.5 12L11.053 9.107\" fill=\"none\" stroke=\"#E6EAF0\" stroke-width=\"2\"/>"
         "</g></svg>"},
        // ---- lien-externe
        {"lien-externe", "Ouvrir ailleurs", "Fl\xC3\xA8" "ches et navigation",
         "ouvrir, lien, externe, fen\xC3\xAAtre",
         true, true, 64, 64,
         "<svg xmlns=\"http://www.w3.org/2000/svg\" width=\"64\" height=\"64\" viewBox=\"0 0 24 24\">"
         "<g stroke-linecap=\"round\" stroke-linejoin=\"round\">"
         "<path d=\"M18.5 13.5v5a1.5 1.5 0 0 1-1.5 1.5H5.5A1.5 1.5 0 0 1 4 18.5V7a1.5 1.5 0 0 1 1.5-1.5h5\" fill=\"none\" stroke=\"#E6EAF0\" stroke-width=\"2\"/>"
         "<path d=\"M11 13L20 4M19.564 8.981L20 4L15.019 4.436\" fill=\"none\" stroke=\"#E6EAF0\" stroke-width=\"2\"/>"
         "</g></svg>"},
        // ---- reglages
        {"reglages", "R\xC3\xA9glages", "Interface",
         "r\xC3\xA9glages, param\xC3\xA8tres, engrenage, options, configuration",
         true, true, 64, 64,
         "<svg xmlns=\"http://www.w3.org/2000/svg\" width=\"64\" height=\"64\" viewBox=\"0 0 24 24\">"
         "<g stroke-linecap=\"round\" stroke-linejoin=\"round\">"
         "<path d=\"M10.315 4.979 L10.661 2.595 L13.339 2.595 L13.685 4.979 L15.772 5.844 L17.704 4.403 L19.597 6.296 L18.156 8.228 L19.021 10.315 L21.405 10.661 L21.405 13.339 L19.021 13.685 L18.156 15.772 L19.597 17.704 L17.704 19.597 L15.772 18.156 L13.685 19.021 L13.339 21.405 L10.661 21.405 L10.315 19.021 L8.228 18.156 L6.296 19.597 L4.403 17.704 L5.844 15.772 L4.979 13.685 L2.595 13.339 L2.595 10.661 L4.979 10.315 L5.844 8.228 L4.403 6.296 L6.296 4.403 L8.228 5.844Z\" fill=\"none\" stroke=\"#E6EAF0\" stroke-width=\"2\"/>"
         "<circle cx=\"12\" cy=\"12\" r=\"3.2\" fill=\"none\" stroke=\"#E6EAF0\" stroke-width=\"2\"/></g></svg>"},
        // ---- curseurs
        {"curseurs", "R\xC3\xA9glages fins", "Interface",
         "curseurs, r\xC3\xA9glages, mixage, niveaux",
         true, true, 64, 64,
         "<svg xmlns=\"http://www.w3.org/2000/svg\" width=\"64\" height=\"64\" viewBox=\"0 0 24 24\">"
         "<g stroke-linecap=\"round\" stroke-linejoin=\"round\">"
         "<path d=\"M4 6.5h4.2M11.8 6.5H20M4 12h10.6M18.2 12H20M4 17.5h1.8M9.4 17.5H20\" fill=\"none\" stroke=\"#E6EAF0\" stroke-width=\"2\"/>"
         "<circle cx=\"10\" cy=\"6.5\" r=\"1.8\" fill=\"none\" stroke=\"#E6EAF0\" stroke-width=\"2\"/>"
         "<circle cx=\"16.4\" cy=\"12\" r=\"1.8\" fill=\"none\" stroke=\"#E6EAF0\" stroke-width=\"2\"/>"
         "<circle cx=\"7.6\" cy=\"17.5\" r=\"1.8\" fill=\"none\" stroke=\"#E6EAF0\" stroke-width=\"2\"/></g></svg>"},
        // ---- utilisateur
        {"utilisateur", "Utilisateur", "Interface",
         "utilisateur, personne, compte, profil",
         true, true, 64, 64,
         "<svg xmlns=\"http://www.w3.org/2000/svg\" width=\"64\" height=\"64\" viewBox=\"0 0 24 24\">"
         "<g stroke-linecap=\"round\" stroke-linejoin=\"round\">"
         "<circle cx=\"12\" cy=\"8\" r=\"4\" fill=\"none\" stroke=\"#E6EAF0\" stroke-width=\"2\"/>"
         "<path d=\"M4.5 20.5a7.5 7.5 0 0 1 15 0\" fill=\"none\" stroke=\"#E6EAF0\" stroke-width=\"2\"/></g></svg>"},
        // ---- cadenas-ferme
        {"cadenas-ferme", "Cadenas ferm\xC3\xA9", "Interface",
         "cadenas, verrouill\xC3\xA9, ferm\xC3\xA9, s\xC3\xA9" "curit\xC3\xA9, prot\xC3\xA9g\xC3\xA9",
         true, true, 64, 64,
         "<svg xmlns=\"http://www.w3.org/2000/svg\" width=\"64\" height=\"64\" viewBox=\"0 0 24 24\">"
         "<g stroke-linecap=\"round\" stroke-linejoin=\"round\">"
         "<rect x=\"5\" y=\"10.5\" width=\"14\" height=\"10.5\" rx=\"2\" fill=\"none\" stroke=\"#E6EAF0\" stroke-width=\"2\"/>"
         "<path d=\"M8 10.5V7.5a4 4 0 0 1 8 0v3\" fill=\"none\" stroke=\"#E6EAF0\" stroke-width=\"2\"/>"
         "<path d=\"M12 14.5v2.5\" fill=\"none\" stroke=\"#E6EAF0\" stroke-width=\"2\"/></g></svg>"},
        // ---- cadenas-ouvert
        {"cadenas-ouvert", "Cadenas ouvert", "Interface",
         "cadenas, d\xC3\xA9verrouill\xC3\xA9, ouvert, libre",
         true, true, 64, 64,
         "<svg xmlns=\"http://www.w3.org/2000/svg\" width=\"64\" height=\"64\" viewBox=\"0 0 24 24\">"
         "<g stroke-linecap=\"round\" stroke-linejoin=\"round\">"
         "<rect x=\"5\" y=\"10.5\" width=\"14\" height=\"10.5\" rx=\"2\" fill=\"none\" stroke=\"#E6EAF0\" stroke-width=\"2\"/>"
         "<path d=\"M8 10.5V7.5a4 4 0 0 1 7.7-1.6\" fill=\"none\" stroke=\"#E6EAF0\" stroke-width=\"2\"/>"
         "<path d=\"M12 14.5v2.5\" fill=\"none\" stroke=\"#E6EAF0\" stroke-width=\"2\"/></g></svg>"},
        // ---- cle
        {"cle", "Cl\xC3\xA9", "Interface",
         "cl\xC3\xA9, acc\xC3\xA8s, mot de passe, autorisation",
         true, true, 64, 64,
         "<svg xmlns=\"http://www.w3.org/2000/svg\" width=\"64\" height=\"64\" viewBox=\"0 0 24 24\">"
         "<g stroke-linecap=\"round\" stroke-linejoin=\"round\">"
         "<circle cx=\"8\" cy=\"16\" r=\"4.5\" fill=\"none\" stroke=\"#E6EAF0\" stroke-width=\"2\"/>"
         "<path d=\"M11.2 12.8L20 4\" fill=\"none\" stroke=\"#E6EAF0\" stroke-width=\"2\"/>"
         "<path d=\"M16.8 7.2l2.6 2.6M14.3 9.7l2 2\" fill=\"none\" stroke=\"#E6EAF0\" stroke-width=\"2\"/></g></svg>"},
        // ---- cloche
        {"cloche", "Cloche d'alarme", "Interface",
         "cloche, alarme, alerte, notification, sonnerie",
         true, true, 64, 64,
         "<svg xmlns=\"http://www.w3.org/2000/svg\" width=\"64\" height=\"64\" viewBox=\"0 0 24 24\">"
         "<g stroke-linecap=\"round\" stroke-linejoin=\"round\">"
         "<path d=\"M6 16.5V11a6 6 0 0 1 12 0v5.5l1.5 2h-15z\" fill=\"none\" stroke=\"#E6EAF0\" stroke-width=\"2\"/>"
         "<path d=\"M10 21h4M12 3v2\" fill=\"none\" stroke=\"#E6EAF0\" stroke-width=\"2\"/></g></svg>"},
        // ---- cloche-barree
        {"cloche-barree", "Alarme coup\xC3\xA9" "e", "Interface",
         "cloche, alarme, silence, muet, acquitter",
         true, true, 64, 64,
         "<svg xmlns=\"http://www.w3.org/2000/svg\" width=\"64\" height=\"64\" viewBox=\"0 0 24 24\">"
         "<g stroke-linecap=\"round\" stroke-linejoin=\"round\"><g transform=\"translate(0 0) scale(1)\">"
         "<path d=\"M6 16.5V11a6 6 0 0 1 12 0v5.5l1.5 2h-15z\" fill=\"none\" stroke=\"#E6EAF0\" stroke-width=\"2\"/>"
         "<path d=\"M10 21h4M12 3v2\" fill=\"none\" stroke=\"#E6EAF0\" stroke-width=\"2\"/></g>"
         "<path d=\"M3.5 3.5l17 17\" fill=\"none\" stroke=\"#E6EAF0\" stroke-width=\"2\"/></g></svg>"},
        // ---- info
        {"info", "Information", "Interface",
         "information, info, renseignement",
         true, true, 64, 64,
         "<svg xmlns=\"http://www.w3.org/2000/svg\" width=\"64\" height=\"64\" viewBox=\"0 0 24 24\">"
         "<g stroke-linecap=\"round\" stroke-linejoin=\"round\">"
         "<circle cx=\"12\" cy=\"12\" r=\"9.5\" fill=\"none\" stroke=\"#E6EAF0\" stroke-width=\"2\"/>"
         "<path d=\"M12 11v6\" fill=\"none\" stroke=\"#E6EAF0\" stroke-width=\"2\"/>"
         "<circle cx=\"12\" cy=\"7.6\" r=\"1.3\" fill=\"#E6EAF0\"/></g></svg>"},
        // ---- aide
        {"aide", "Aide", "Interface",
         "aide, question, help, comment",
         true, true, 64, 64,
         "<svg xmlns=\"http://www.w3.org/2000/svg\" width=\"64\" height=\"64\" viewBox=\"0 0 24 24\">"
         "<g stroke-linecap=\"round\" stroke-linejoin=\"round\">"
         "<circle cx=\"12\" cy=\"12\" r=\"9.5\" fill=\"none\" stroke=\"#E6EAF0\" stroke-width=\"2\"/>"
         "<path d=\"M9.3 9.4a2.8 2.8 0 1 1 3.9 2.6c-.8.4-1.2 1-1.2 1.8v.6\" fill=\"none\" stroke=\"#E6EAF0\" stroke-width=\"2\"/>"
         "<circle cx=\"12\" cy=\"17.2\" r=\"1.3\" fill=\"#E6EAF0\"/></g></svg>"},
        // ---- attention
        {"attention", "Attention", "Interface",
         "attention, avertissement, warning, d\xC3\xA9" "faut",
         true, true, 64, 64,
         "<svg xmlns=\"http://www.w3.org/2000/svg\" width=\"64\" height=\"64\" viewBox=\"0 0 24 24\">"
         "<g stroke-linecap=\"round\" stroke-linejoin=\"round\">"
         "<path d=\"M12 3.5L21.5 20h-19z\" fill=\"none\" stroke=\"#E6EAF0\" stroke-width=\"2\"/>"
         "<path d=\"M12 9.5v5\" fill=\"none\" stroke=\"#E6EAF0\" stroke-width=\"2\"/>"
         "<circle cx=\"12\" cy=\"17.3\" r=\"1.3\" fill=\"#E6EAF0\"/></g></svg>"},
        // ---- ok
        {"ok", "Valider", "Interface",
         "ok, valider, coche, correct, bon",
         true, true, 64, 64,
         "<svg xmlns=\"http://www.w3.org/2000/svg\" width=\"64\" height=\"64\" viewBox=\"0 0 24 24\">"
         "<g stroke-linecap=\"round\" stroke-linejoin=\"round\">"
         "<path d=\"M4 12.5l5 5L20 6.5\" fill=\"none\" stroke=\"#E6EAF0\" stroke-width=\"2\"/></g></svg>"},
        // ---- ok-cercle
        {"ok-cercle", "Valid\xC3\xA9", "Interface",
         "ok, valid\xC3\xA9, coche, conforme, termin\xC3\xA9",
         true, true, 64, 64,
         "<svg xmlns=\"http://www.w3.org/2000/svg\" width=\"64\" height=\"64\" viewBox=\"0 0 24 24\">"
         "<g stroke-linecap=\"round\" stroke-linejoin=\"round\">"
         "<circle cx=\"12\" cy=\"12\" r=\"9.5\" fill=\"none\" stroke=\"#E6EAF0\" stroke-width=\"2\"/>"
         "<path d=\"M7.5 12.3l3 3 6-6.3\" fill=\"none\" stroke=\"#E6EAF0\" stroke-width=\"2\"/></g></svg>"},
        // ---- croix
        {"croix", "Fermer", "Interface",
         "croix, fermer, annuler, supprimer, non",
         true, true, 64, 64,
         "<svg xmlns=\"http://www.w3.org/2000/svg\" width=\"64\" height=\"64\" viewBox=\"0 0 24 24\">"
         "<g stroke-linecap=\"round\" stroke-linejoin=\"round\">"
         "<path d=\"M6 6l12 12M18 6L6 18\" fill=\"none\" stroke=\"#E6EAF0\" stroke-width=\"2\"/></g></svg>"},
        // ---- croix-cercle
        {"croix-cercle", "Refus\xC3\xA9", "Interface",
         "croix, refus\xC3\xA9, erreur, non conforme",
         true, true, 64, 64,
         "<svg xmlns=\"http://www.w3.org/2000/svg\" width=\"64\" height=\"64\" viewBox=\"0 0 24 24\">"
         "<g stroke-linecap=\"round\" stroke-linejoin=\"round\">"
         "<circle cx=\"12\" cy=\"12\" r=\"9.5\" fill=\"none\" stroke=\"#E6EAF0\" stroke-width=\"2\"/>"
         "<path d=\"M8.5 8.5l7 7M15.5 8.5l-7 7\" fill=\"none\" stroke=\"#E6EAF0\" stroke-width=\"2\"/></g></svg>"},
        // ---- plus
        {"plus", "Plus", "Interface",
         "plus, ajouter, augmenter, nouveau",
         true, true, 64, 64,
         "<svg xmlns=\"http://www.w3.org/2000/svg\" width=\"64\" height=\"64\" viewBox=\"0 0 24 24\">"
         "<g stroke-linecap=\"round\" stroke-linejoin=\"round\">"
         "<path d=\"M12 5v14M5 12h14\" fill=\"none\" stroke=\"#E6EAF0\" stroke-width=\"2\"/></g></svg>"},
        // ---- moins
        {"moins", "Moins", "Interface",
         "moins, retirer, diminuer",
         true, true, 64, 64,
         "<svg xmlns=\"http://www.w3.org/2000/svg\" width=\"64\" height=\"64\" viewBox=\"0 0 24 24\">"
         "<g stroke-linecap=\"round\" stroke-linejoin=\"round\">"
         "<path d=\"M5 12h14\" fill=\"none\" stroke=\"#E6EAF0\" stroke-width=\"2\"/></g></svg>"},
        // ---- recherche
        {"recherche", "Rechercher", "Interface",
         "recherche, loupe, chercher, trouver",
         true, true, 64, 64,
         "<svg xmlns=\"http://www.w3.org/2000/svg\" width=\"64\" height=\"64\" viewBox=\"0 0 24 24\">"
         "<g stroke-linecap=\"round\" stroke-linejoin=\"round\">"
         "<circle cx=\"10.5\" cy=\"10.5\" r=\"6.5\" fill=\"none\" stroke=\"#E6EAF0\" stroke-width=\"2\"/>"
         "<path d=\"M15.3 15.3L20.5 20.5\" fill=\"none\" stroke=\"#E6EAF0\" stroke-width=\"2\"/></g></svg>"},
        // ---- zoom-plus
        {"zoom-plus", "Zoom avant", "Interface",
         "zoom, agrandir, loupe, plus",
         true, true, 64, 64,
         "<svg xmlns=\"http://www.w3.org/2000/svg\" width=\"64\" height=\"64\" viewBox=\"0 0 24 24\">"
         "<g stroke-linecap=\"round\" stroke-linejoin=\"round\"><g transform=\"translate(0 0) scale(1)\">"
         "<circle cx=\"10.5\" cy=\"10.5\" r=\"6.5\" fill=\"none\" stroke=\"#E6EAF0\" stroke-width=\"2\"/>"
         "<path d=\"M15.3 15.3L20.5 20.5\" fill=\"none\" stroke=\"#E6EAF0\" stroke-width=\"2\"/></g>"
         "<path d=\"M10.5 7.8v5.4M7.8 10.5h5.4\" fill=\"none\" stroke=\"#E6EAF0\" stroke-width=\"2\"/></g></svg>"},
        // ---- zoom-moins
        {"zoom-moins", "Zoom arri\xC3\xA8re", "Interface",
         "zoom, r\xC3\xA9" "duire, loupe, moins",
         true, true, 64, 64,
         "<svg xmlns=\"http://www.w3.org/2000/svg\" width=\"64\" height=\"64\" viewBox=\"0 0 24 24\">"
         "<g stroke-linecap=\"round\" stroke-linejoin=\"round\"><g transform=\"translate(0 0) scale(1)\">"
         "<circle cx=\"10.5\" cy=\"10.5\" r=\"6.5\" fill=\"none\" stroke=\"#E6EAF0\" stroke-width=\"2\"/>"
         "<path d=\"M15.3 15.3L20.5 20.5\" fill=\"none\" stroke=\"#E6EAF0\" stroke-width=\"2\"/></g>"
         "<path d=\"M7.8 10.5h5.4\" fill=\"none\" stroke=\"#E6EAF0\" stroke-width=\"2\"/></g></svg>"},
        // ---- filtre
        {"filtre", "Filtre", "Interface",
         "filtre, entonnoir, trier, s\xC3\xA9lection",
         true, true, 64, 64,
         "<svg xmlns=\"http://www.w3.org/2000/svg\" width=\"64\" height=\"64\" viewBox=\"0 0 24 24\">"
         "<g stroke-linecap=\"round\" stroke-linejoin=\"round\">"
         "<path d=\"M3.5 4.5h17l-6.5 8v6.5l-4 2v-8.5z\" fill=\"none\" stroke=\"#E6EAF0\" stroke-width=\"2\"/></g>"
         "</svg>"},
        // ---- enregistrer
        {"enregistrer", "Enregistrer", "Interface",
         "enregistrer, sauvegarder, disquette, save",
         true, true, 64, 64,
         "<svg xmlns=\"http://www.w3.org/2000/svg\" width=\"64\" height=\"64\" viewBox=\"0 0 24 24\">"
         "<g stroke-linecap=\"round\" stroke-linejoin=\"round\">"
         "<path d=\"M5 4h11l3.5 3.5V19a1 1 0 0 1-1 1H5a1 1 0 0 1-1-1V5a1 1 0 0 1 1-1z\" fill=\"none\" stroke=\"#E6EAF0\" stroke-width=\"2\"/>"
         "<path d=\"M8 4v4.5h7V4\" fill=\"none\" stroke=\"#E6EAF0\" stroke-width=\"2\"/>"
         "<rect x=\"7.5\" y=\"13\" width=\"9\" height=\"7\" fill=\"none\" stroke=\"#E6EAF0\" stroke-width=\"2\"/></g>"
         "</svg>"},
        // ---- imprimer
        {"imprimer", "Imprimer", "Interface",
         "imprimer, imprimante, papier, rapport",
         true, true, 64, 64,
         "<svg xmlns=\"http://www.w3.org/2000/svg\" width=\"64\" height=\"64\" viewBox=\"0 0 24 24\">"
         "<g stroke-linecap=\"round\" stroke-linejoin=\"round\">"
         "<path d=\"M7 8.5V3.5h10v5\" fill=\"none\" stroke=\"#E6EAF0\" stroke-width=\"2\"/>"
         "<path d=\"M7 17H5a1.5 1.5 0 0 1-1.5-1.5V10A1.5 1.5 0 0 1 5 8.5h14a1.5 1.5 0 0 1 1.5 1.5v5.5A1.5 1.5 0 0 1 19 17h-2\" fill=\"none\" stroke=\"#E6EAF0\" stroke-width=\"2\"/>"
         "<rect x=\"7\" y=\"13.5\" width=\"10\" height=\"7\" fill=\"none\" stroke=\"#E6EAF0\" stroke-width=\"2\"/>"
         "</g></svg>"},
        // ---- exporter
        {"exporter", "Exporter", "Interface",
         "exporter, sortir, envoyer, export",
         true, true, 64, 64,
         "<svg xmlns=\"http://www.w3.org/2000/svg\" width=\"64\" height=\"64\" viewBox=\"0 0 24 24\">"
         "<g stroke-linecap=\"round\" stroke-linejoin=\"round\">"
         "<path d=\"M12 14L12 3.5M14.893 6.947L12 3.5L9.107 6.947\" fill=\"none\" stroke=\"#E6EAF0\" stroke-width=\"2\"/>"
         "<path d=\"M5 12v6.5A1.5 1.5 0 0 0 6.5 20h11a1.5 1.5 0 0 0 1.5-1.5V12\" fill=\"none\" stroke=\"#E6EAF0\" stroke-width=\"2\"/>"
         "</g></svg>"},
        // ---- importer
        {"importer", "Importer", "Interface",
         "importer, entrer, recevoir, import",
         true, true, 64, 64,
         "<svg xmlns=\"http://www.w3.org/2000/svg\" width=\"64\" height=\"64\" viewBox=\"0 0 24 24\">"
         "<g stroke-linecap=\"round\" stroke-linejoin=\"round\">"
         "<path d=\"M12 3.5L12 14M9.107 10.553L12 14L14.893 10.553\" fill=\"none\" stroke=\"#E6EAF0\" stroke-width=\"2\"/>"
         "<path d=\"M5 12v6.5A1.5 1.5 0 0 0 6.5 20h11a1.5 1.5 0 0 0 1.5-1.5V12\" fill=\"none\" stroke=\"#E6EAF0\" stroke-width=\"2\"/>"
         "</g></svg>"},
        // ---- telecharger
        {"telecharger", "T\xC3\xA9l\xC3\xA9" "charger", "Interface",
         "t\xC3\xA9l\xC3\xA9" "charger, descendre, download",
         true, true, 64, 64,
         "<svg xmlns=\"http://www.w3.org/2000/svg\" width=\"64\" height=\"64\" viewBox=\"0 0 24 24\">"
         "<g stroke-linecap=\"round\" stroke-linejoin=\"round\">"
         "<path d=\"M12 3.5L12 15M8.786 11.17L12 15L15.214 11.17\" fill=\"none\" stroke=\"#E6EAF0\" stroke-width=\"2\"/>"
         "<path d=\"M5 20h14\" fill=\"none\" stroke=\"#E6EAF0\" stroke-width=\"2\"/></g></svg>"},
        // ---- televerser
        {"televerser", "T\xC3\xA9l\xC3\xA9verser", "Interface",
         "t\xC3\xA9l\xC3\xA9verser, envoyer, upload",
         true, true, 64, 64,
         "<svg xmlns=\"http://www.w3.org/2000/svg\" width=\"64\" height=\"64\" viewBox=\"0 0 24 24\">"
         "<g stroke-linecap=\"round\" stroke-linejoin=\"round\">"
         "<path d=\"M12 15L12 3.5M15.214 7.33L12 3.5L8.786 7.33\" fill=\"none\" stroke=\"#E6EAF0\" stroke-width=\"2\"/>"
         "<path d=\"M5 20h14\" fill=\"none\" stroke=\"#E6EAF0\" stroke-width=\"2\"/></g></svg>"},
        // ---- lecture
        {"lecture", "Lecture", "Interface",
         "lecture, d\xC3\xA9marrer, play, marche, lancer",
         true, true, 64, 64,
         "<svg xmlns=\"http://www.w3.org/2000/svg\" width=\"64\" height=\"64\" viewBox=\"0 0 24 24\">"
         "<g stroke-linecap=\"round\" stroke-linejoin=\"round\">"
         "<path d=\"M7.5 4.5v15a.8.8 0 0 0 1.2.7l11.5-7.5a.8.8 0 0 0 0-1.4L8.7 3.8a.8.8 0 0 0-1.2.7z\" fill=\"#E6EAF0\"/>"
         "</g></svg>"},
        // ---- pause
        {"pause", "Pause", "Interface",
         "pause, attente, suspendre",
         true, true, 64, 64,
         "<svg xmlns=\"http://www.w3.org/2000/svg\" width=\"64\" height=\"64\" viewBox=\"0 0 24 24\">"
         "<g stroke-linecap=\"round\" stroke-linejoin=\"round\">"
         "<rect x=\"6\" y=\"4.5\" width=\"4\" height=\"15\" rx=\"1\" fill=\"#E6EAF0\"/>"
         "<rect x=\"14\" y=\"4.5\" width=\"4\" height=\"15\" rx=\"1\" fill=\"#E6EAF0\"/></g></svg>"},
        // ---- stop
        {"stop", "Stop", "Interface",
         "stop, arr\xC3\xAAt, arr\xC3\xAAter, carr\xC3\xA9",
         true, true, 64, 64,
         "<svg xmlns=\"http://www.w3.org/2000/svg\" width=\"64\" height=\"64\" viewBox=\"0 0 24 24\">"
         "<g stroke-linecap=\"round\" stroke-linejoin=\"round\">"
         "<rect x=\"5\" y=\"5\" width=\"14\" height=\"14\" rx=\"2\" fill=\"#E6EAF0\"/></g></svg>"},
        // ---- enregistrement
        {"enregistrement", "Enregistrement", "Interface",
         "enregistrement, enregistrer, rec, point",
         true, true, 64, 64,
         "<svg xmlns=\"http://www.w3.org/2000/svg\" width=\"64\" height=\"64\" viewBox=\"0 0 24 24\">"
         "<g stroke-linecap=\"round\" stroke-linejoin=\"round\">"
         "<circle cx=\"12\" cy=\"12\" r=\"7.5\" fill=\"#E6EAF0\"/></g></svg>"},
        // ---- avance-rapide
        {"avance-rapide", "Avance rapide (lecture)", "Interface",
         "avance, rapide, suivant, forward",
         true, true, 64, 64,
         "<svg xmlns=\"http://www.w3.org/2000/svg\" width=\"64\" height=\"64\" viewBox=\"0 0 24 24\">"
         "<g stroke-linecap=\"round\" stroke-linejoin=\"round\">"
         "<polygon points=\"3 6 3 18 11.5 12\" fill=\"#E6EAF0\"/>"
         "<polygon points=\"12 6 12 18 20.5 12\" fill=\"#E6EAF0\"/></g></svg>"},
        // ---- retour-arriere
        {"retour-arriere", "Retour rapide (lecture)", "Interface",
         "retour, rapide, pr\xC3\xA9" "c\xC3\xA9" "dent, rewind",
         true, true, 64, 64,
         "<svg xmlns=\"http://www.w3.org/2000/svg\" width=\"64\" height=\"64\" viewBox=\"0 0 24 24\">"
         "<g stroke-linecap=\"round\" stroke-linejoin=\"round\">"
         "<polygon points=\"21 6 21 18 12.5 12\" fill=\"#E6EAF0\"/>"
         "<polygon points=\"12 6 12 18 3.5 12\" fill=\"#E6EAF0\"/></g></svg>"},
        // ---- horloge
        {"horloge", "Horloge", "Interface",
         "horloge, heure, temps, dur\xC3\xA9" "e, montre",
         true, true, 64, 64,
         "<svg xmlns=\"http://www.w3.org/2000/svg\" width=\"64\" height=\"64\" viewBox=\"0 0 24 24\">"
         "<g stroke-linecap=\"round\" stroke-linejoin=\"round\">"
         "<circle cx=\"12\" cy=\"12\" r=\"9.5\" fill=\"none\" stroke=\"#E6EAF0\" stroke-width=\"2\"/>"
         "<path d=\"M12 6.5V12l4 2.5\" fill=\"none\" stroke=\"#E6EAF0\" stroke-width=\"2\"/></g></svg>"},
        // ---- calendrier
        {"calendrier", "Calendrier", "Interface",
         "calendrier, date, jour, planning, agenda",
         true, true, 64, 64,
         "<svg xmlns=\"http://www.w3.org/2000/svg\" width=\"64\" height=\"64\" viewBox=\"0 0 24 24\">"
         "<g stroke-linecap=\"round\" stroke-linejoin=\"round\">"
         "<rect x=\"3.5\" y=\"5\" width=\"17\" height=\"15.5\" rx=\"2\" fill=\"none\" stroke=\"#E6EAF0\" stroke-width=\"2\"/>"
         "<path d=\"M3.5 10h17M8 3v4M16 3v4\" fill=\"none\" stroke=\"#E6EAF0\" stroke-width=\"2\"/>"
         "<rect x=\"7\" y=\"13\" width=\"3\" height=\"3\" rx=\"0.5\" fill=\"#E6EAF0\"/></g></svg>"},
        // ---- sablier
        {"sablier", "Sablier", "Interface",
         "sablier, attente, minuterie, temporisation",
         true, true, 64, 64,
         "<svg xmlns=\"http://www.w3.org/2000/svg\" width=\"64\" height=\"64\" viewBox=\"0 0 24 24\">"
         "<g stroke-linecap=\"round\" stroke-linejoin=\"round\">"
         "<path d=\"M6.5 3.5h11M6.5 20.5h11\" fill=\"none\" stroke=\"#E6EAF0\" stroke-width=\"2\"/>"
         "<path d=\"M8 3.5c0 4.5 8 4.8 8 8.5s-8 4-8 8.5M16 3.5c0 4.5-8 4.8-8 8.5s8 4 8 8.5\" fill=\"none\" stroke=\"#E6EAF0\" stroke-width=\"2\"/>"
         "</g></svg>"},
        // ---- poubelle
        {"poubelle", "Corbeille", "Interface",
         "poubelle, corbeille, supprimer, effacer",
         true, true, 64, 64,
         "<svg xmlns=\"http://www.w3.org/2000/svg\" width=\"64\" height=\"64\" viewBox=\"0 0 24 24\">"
         "<g stroke-linecap=\"round\" stroke-linejoin=\"round\">"
         "<path d=\"M4 6.5h16M9.5 6.5V4h5v2.5\" fill=\"none\" stroke=\"#E6EAF0\" stroke-width=\"2\"/>"
         "<path d=\"M6 6.5l1 13.5a1 1 0 0 0 1 1h8a1 1 0 0 0 1-1l1-13.5\" fill=\"none\" stroke=\"#E6EAF0\" stroke-width=\"2\"/>"
         "<path d=\"M10 10.5v6.5M14 10.5v6.5\" fill=\"none\" stroke=\"#E6EAF0\" stroke-width=\"2\"/></g></svg>"},
        // ---- crayon
        {"crayon", "Modifier", "Interface",
         "crayon, modifier, \xC3\xA9" "diter, \xC3\xA9" "crire",
         true, true, 64, 64,
         "<svg xmlns=\"http://www.w3.org/2000/svg\" width=\"64\" height=\"64\" viewBox=\"0 0 24 24\">"
         "<g stroke-linecap=\"round\" stroke-linejoin=\"round\">"
         "<path d=\"M4 20l1-4.5L16 4.5a2.1 2.1 0 0 1 3 0l.5.5a2.1 2.1 0 0 1 0 3L8.5 19z\" fill=\"none\" stroke=\"#E6EAF0\" stroke-width=\"2\"/>"
         "<path d=\"M14 6.5l3.5 3.5\" fill=\"none\" stroke=\"#E6EAF0\" stroke-width=\"2\"/></g></svg>"},
        // ---- copier
        {"copier", "Copier", "Interface",
         "copier, dupliquer, copie",
         true, true, 64, 64,
         "<svg xmlns=\"http://www.w3.org/2000/svg\" width=\"64\" height=\"64\" viewBox=\"0 0 24 24\">"
         "<g stroke-linecap=\"round\" stroke-linejoin=\"round\">"
         "<rect x=\"8.5\" y=\"8.5\" width=\"12\" height=\"12\" rx=\"1.5\" fill=\"none\" stroke=\"#E6EAF0\" stroke-width=\"2\"/>"
         "<path d=\"M15.5 8.5V5A1.5 1.5 0 0 0 14 3.5H5A1.5 1.5 0 0 0 3.5 5v9A1.5 1.5 0 0 0 5 15.5h3.5\" fill=\"none\" stroke=\"#E6EAF0\" stroke-width=\"2\"/>"
         "</g></svg>"},
        // ---- dossier
        {"dossier", "Dossier", "Interface",
         "dossier, r\xC3\xA9pertoire, classer, ouvrir",
         true, true, 64, 64,
         "<svg xmlns=\"http://www.w3.org/2000/svg\" width=\"64\" height=\"64\" viewBox=\"0 0 24 24\">"
         "<g stroke-linecap=\"round\" stroke-linejoin=\"round\">"
         "<path d=\"M3.5 6.5A1.5 1.5 0 0 1 5 5h4.5l2 2.5H19a1.5 1.5 0 0 1 1.5 1.5v9.5A1.5 1.5 0 0 1 19 20H5a1.5 1.5 0 0 1-1.5-1.5z\" fill=\"none\" stroke=\"#E6EAF0\" stroke-width=\"2\"/>"
         "</g></svg>"},
        // ---- document
        {"document", "Document", "Interface",
         "document, fichier, page, rapport, notice",
         true, true, 64, 64,
         "<svg xmlns=\"http://www.w3.org/2000/svg\" width=\"64\" height=\"64\" viewBox=\"0 0 24 24\">"
         "<g stroke-linecap=\"round\" stroke-linejoin=\"round\">"
         "<path d=\"M6 3.5h8l4.5 4.5v12.5H6z\" fill=\"none\" stroke=\"#E6EAF0\" stroke-width=\"2\"/>"
         "<path d=\"M14 3.5V8h4.5\" fill=\"none\" stroke=\"#E6EAF0\" stroke-width=\"2\"/>"
         "<path d=\"M9 12.5h6M9 16h6\" fill=\"none\" stroke=\"#E6EAF0\" stroke-width=\"2\"/></g></svg>"},
        // ---- liste
        {"liste", "Liste", "Interface",
         "liste, \xC3\xA9l\xC3\xA9ments, lignes, menu",
         true, true, 64, 64,
         "<svg xmlns=\"http://www.w3.org/2000/svg\" width=\"64\" height=\"64\" viewBox=\"0 0 24 24\">"
         "<g stroke-linecap=\"round\" stroke-linejoin=\"round\">"
         "<path d=\"M9.5 6h10.5M9.5 12h10.5M9.5 18h10.5\" fill=\"none\" stroke=\"#E6EAF0\" stroke-width=\"2\"/>"
         "<circle cx=\"5\" cy=\"6\" r=\"1.5\" fill=\"#E6EAF0\"/>"
         "<circle cx=\"5\" cy=\"12\" r=\"1.5\" fill=\"#E6EAF0\"/>"
         "<circle cx=\"5\" cy=\"18\" r=\"1.5\" fill=\"#E6EAF0\"/></g></svg>"},
        // ---- grille
        {"grille", "Grille", "Interface",
         "grille, tuiles, vignettes, mosa\xC3\xAFque",
         true, true, 64, 64,
         "<svg xmlns=\"http://www.w3.org/2000/svg\" width=\"64\" height=\"64\" viewBox=\"0 0 24 24\">"
         "<g stroke-linecap=\"round\" stroke-linejoin=\"round\">"
         "<rect x=\"4\" y=\"4\" width=\"6.5\" height=\"6.5\" rx=\"1\" fill=\"none\" stroke=\"#E6EAF0\" stroke-width=\"2\"/>"
         "<rect x=\"13.5\" y=\"4\" width=\"6.5\" height=\"6.5\" rx=\"1\" fill=\"none\" stroke=\"#E6EAF0\" stroke-width=\"2\"/>"
         "<rect x=\"4\" y=\"13.5\" width=\"6.5\" height=\"6.5\" rx=\"1\" fill=\"none\" stroke=\"#E6EAF0\" stroke-width=\"2\"/>"
         "<rect x=\"13.5\" y=\"13.5\" width=\"6.5\" height=\"6.5\" rx=\"1\" fill=\"none\" stroke=\"#E6EAF0\" stroke-width=\"2\"/>"
         "</g></svg>"},
        // ---- favori
        {"favori", "Favori", "Interface",
         "favori, \xC3\xA9toile, pr\xC3\xA9" "f\xC3\xA9r\xC3\xA9",
         true, true, 64, 64,
         "<svg xmlns=\"http://www.w3.org/2000/svg\" width=\"64\" height=\"64\" viewBox=\"0 0 24 24\">"
         "<g stroke-linecap=\"round\" stroke-linejoin=\"round\">"
         "<polygon points=\"12 3 14.469 9.402 21.32 9.772 15.994 14.098 17.76 20.728 12 17 6.24 20.728 8.006 14.098 2.68 9.772 9.531 9.402\" fill=\"none\" stroke=\"#E6EAF0\" stroke-width=\"2\"/>"
         "</g></svg>"},
        // ---- oeil
        {"oeil", "Voir", "Interface",
         "voir, \xC5\x93il, visible, afficher",
         true, true, 64, 64,
         "<svg xmlns=\"http://www.w3.org/2000/svg\" width=\"64\" height=\"64\" viewBox=\"0 0 24 24\">"
         "<g stroke-linecap=\"round\" stroke-linejoin=\"round\">"
         "<path d=\"M2.5 12c2.2-4.2 5.6-6.5 9.5-6.5s7.3 2.3 9.5 6.5c-2.2 4.2-5.6 6.5-9.5 6.5S4.7 16.2 2.5 12z\" fill=\"none\" stroke=\"#E6EAF0\" stroke-width=\"2\"/>"
         "<circle cx=\"12\" cy=\"12\" r=\"3\" fill=\"none\" stroke=\"#E6EAF0\" stroke-width=\"2\"/></g></svg>"},
        // ---- oeil-barre
        {"oeil-barre", "Masquer", "Interface",
         "masquer, cach\xC3\xA9, invisible, \xC5\x93il",
         true, true, 64, 64,
         "<svg xmlns=\"http://www.w3.org/2000/svg\" width=\"64\" height=\"64\" viewBox=\"0 0 24 24\">"
         "<g stroke-linecap=\"round\" stroke-linejoin=\"round\"><g transform=\"translate(0 0) scale(1)\">"
         "<path d=\"M2.5 12c2.2-4.2 5.6-6.5 9.5-6.5s7.3 2.3 9.5 6.5c-2.2 4.2-5.6 6.5-9.5 6.5S4.7 16.2 2.5 12z\" fill=\"none\" stroke=\"#E6EAF0\" stroke-width=\"2\"/>"
         "<circle cx=\"12\" cy=\"12\" r=\"3\" fill=\"none\" stroke=\"#E6EAF0\" stroke-width=\"2\"/></g>"
         "<path d=\"M4 4l16 16\" fill=\"none\" stroke=\"#E6EAF0\" stroke-width=\"2\"/></g></svg>"},
        // ---- lien
        {"lien", "Lien", "Interface",
         "lien, cha\xC3\xAEne, relier, attacher",
         true, true, 64, 64,
         "<svg xmlns=\"http://www.w3.org/2000/svg\" width=\"64\" height=\"64\" viewBox=\"0 0 24 24\">"
         "<g stroke-linecap=\"round\" stroke-linejoin=\"round\">"
         "<path d=\"M10 13.5a4 4 0 0 0 5.7.4l3-3a4 4 0 0 0-5.7-5.7l-1.2 1.2\" fill=\"none\" stroke=\"#E6EAF0\" stroke-width=\"2\"/>"
         "<path d=\"M14 10.5a4 4 0 0 0-5.7-.4l-3 3a4 4 0 0 0 5.7 5.7l1.2-1.2\" fill=\"none\" stroke=\"#E6EAF0\" stroke-width=\"2\"/>"
         "</g></svg>"},
        // ---- enveloppe
        {"enveloppe", "Message", "Interface",
         "message, courriel, mail, enveloppe, e-mail",
         true, true, 64, 64,
         "<svg xmlns=\"http://www.w3.org/2000/svg\" width=\"64\" height=\"64\" viewBox=\"0 0 24 24\">"
         "<g stroke-linecap=\"round\" stroke-linejoin=\"round\">"
         "<rect x=\"3\" y=\"5.5\" width=\"18\" height=\"13\" rx=\"2\" fill=\"none\" stroke=\"#E6EAF0\" stroke-width=\"2\"/>"
         "<path d=\"M3.5 6.5L12 13l8.5-6.5\" fill=\"none\" stroke=\"#E6EAF0\" stroke-width=\"2\"/></g></svg>"},
        // ---- telephone
        {"telephone", "T\xC3\xA9l\xC3\xA9phone", "Interface",
         "t\xC3\xA9l\xC3\xA9phone, appel, combin\xC3\xA9, astreinte",
         true, true, 64, 64,
         "<svg xmlns=\"http://www.w3.org/2000/svg\" width=\"64\" height=\"64\" viewBox=\"0 0 24 24\">"
         "<g stroke-linecap=\"round\" stroke-linejoin=\"round\">"
         "<path d=\"M6.5 3.5h3l1.5 4.5-2 1.5a10.5 10.5 0 0 0 5.5 5.5l1.5-2 4.5 1.5v3a2 2 0 0 1-2 2A15.5 15.5 0 0 1 4.5 5.5a2 2 0 0 1 2-2z\" fill=\"none\" stroke=\"#E6EAF0\" stroke-width=\"2\"/>"
         "</g></svg>"},
        // ---- haut-parleur
        {"haut-parleur", "Son", "Interface",
         "son, haut-parleur, volume, audio",
         true, true, 64, 64,
         "<svg xmlns=\"http://www.w3.org/2000/svg\" width=\"64\" height=\"64\" viewBox=\"0 0 24 24\">"
         "<g stroke-linecap=\"round\" stroke-linejoin=\"round\">"
         "<path d=\"M4 9.5h3.5L12 5.5v13l-4.5-4H4z\" fill=\"none\" stroke=\"#E6EAF0\" stroke-width=\"2\"/>"
         "<path d=\"M15.5 9a4 4 0 0 1 0 6M18.5 6.5a8 8 0 0 1 0 11\" fill=\"none\" stroke=\"#E6EAF0\" stroke-width=\"2\"/>"
         "</g></svg>"},
        // ---- haut-parleur-coupe
        {"haut-parleur-coupe", "Son coup\xC3\xA9", "Interface",
         "son coup\xC3\xA9, muet, silence, mute",
         true, true, 64, 64,
         "<svg xmlns=\"http://www.w3.org/2000/svg\" width=\"64\" height=\"64\" viewBox=\"0 0 24 24\">"
         "<g stroke-linecap=\"round\" stroke-linejoin=\"round\">"
         "<path d=\"M4 9.5h3.5L12 5.5v13l-4.5-4H4z\" fill=\"none\" stroke=\"#E6EAF0\" stroke-width=\"2\"/>"
         "<path d=\"M15.5 9.5l5 5M20.5 9.5l-5 5\" fill=\"none\" stroke=\"#E6EAF0\" stroke-width=\"2\"/></g></svg>"},
        // ---- appareil-photo
        {"appareil-photo", "Capture", "Interface",
         "capture, photo, appareil, image",
         true, true, 64, 64,
         "<svg xmlns=\"http://www.w3.org/2000/svg\" width=\"64\" height=\"64\" viewBox=\"0 0 24 24\">"
         "<g stroke-linecap=\"round\" stroke-linejoin=\"round\">"
         "<path d=\"M3.5 8.5A1.5 1.5 0 0 1 5 7h2.5L9 4.5h6L16.5 7H19a1.5 1.5 0 0 1 1.5 1.5V18A1.5 1.5 0 0 1 19 19.5H5A1.5 1.5 0 0 1 3.5 18z\" fill=\"none\" stroke=\"#E6EAF0\" stroke-width=\"2\"/>"
         "<circle cx=\"12\" cy=\"13\" r=\"3.7\" fill=\"none\" stroke=\"#E6EAF0\" stroke-width=\"2\"/></g></svg>"},
        // ---- courbe
        {"courbe", "Courbe", "Interface",
         "courbe, graphique, tendance, historique",
         true, true, 64, 64,
         "<svg xmlns=\"http://www.w3.org/2000/svg\" width=\"64\" height=\"64\" viewBox=\"0 0 24 24\">"
         "<g stroke-linecap=\"round\" stroke-linejoin=\"round\">"
         "<path d=\"M3.5 3.5v17h17\" fill=\"none\" stroke=\"#E6EAF0\" stroke-width=\"2\"/>"
         "<path d=\"M7 15l4-5 3.5 3.5L20 6.5\" fill=\"none\" stroke=\"#E6EAF0\" stroke-width=\"2\"/></g></svg>"},
        // ---- barres
        {"barres", "Graphique en barres", "Interface",
         "barres, graphique, statistiques, histogramme",
         true, true, 64, 64,
         "<svg xmlns=\"http://www.w3.org/2000/svg\" width=\"64\" height=\"64\" viewBox=\"0 0 24 24\">"
         "<g stroke-linecap=\"round\" stroke-linejoin=\"round\">"
         "<path d=\"M3.5 3.5v17h17\" fill=\"none\" stroke=\"#E6EAF0\" stroke-width=\"2\"/>"
         "<rect x=\"7\" y=\"12\" width=\"3\" height=\"6\" rx=\"0.5\" fill=\"#E6EAF0\"/>"
         "<rect x=\"12\" y=\"8.5\" width=\"3\" height=\"9.5\" rx=\"0.5\" fill=\"#E6EAF0\"/>"
         "<rect x=\"17\" y=\"5\" width=\"3\" height=\"13\" rx=\"0.5\" fill=\"#E6EAF0\"/></g></svg>"},
        // ---- jauge
        {"jauge", "Tableau de bord", "Interface",
         "jauge, compteur, vitesse, tableau de bord",
         true, true, 64, 64,
         "<svg xmlns=\"http://www.w3.org/2000/svg\" width=\"64\" height=\"64\" viewBox=\"0 0 24 24\">"
         "<g stroke-linecap=\"round\" stroke-linejoin=\"round\">"
         "<path d=\"M3.5 16.5a8.5 8.5 0 1 1 17 0\" fill=\"none\" stroke=\"#E6EAF0\" stroke-width=\"2\"/>"
         "<path d=\"M12 16.5l4.2-4.8\" fill=\"none\" stroke=\"#E6EAF0\" stroke-width=\"2\"/>"
         "<circle cx=\"12\" cy=\"16.5\" r=\"1.8\" fill=\"#E6EAF0\"/></g></svg>"},
        // ---- alimentation
        {"alimentation", "Marche / arr\xC3\xAAt", "Interface",
         "alimentation, marche, arr\xC3\xAAt, power, interrupteur",
         true, true, 64, 64,
         "<svg xmlns=\"http://www.w3.org/2000/svg\" width=\"64\" height=\"64\" viewBox=\"0 0 24 24\">"
         "<g stroke-linecap=\"round\" stroke-linejoin=\"round\">"
         "<path d=\"M12 3.5V12\" fill=\"none\" stroke=\"#E6EAF0\" stroke-width=\"2\"/>"
         "<path d=\"M7.2 6.3a7.5 7.5 0 1 0 9.6 0\" fill=\"none\" stroke=\"#E6EAF0\" stroke-width=\"2\"/></g></svg>"},
        // ---- repere
        {"repere", "Rep\xC3\xA8re", "Interface",
         "rep\xC3\xA8re, drapeau, marque, signaler",
         true, true, 64, 64,
         "<svg xmlns=\"http://www.w3.org/2000/svg\" width=\"64\" height=\"64\" viewBox=\"0 0 24 24\">"
         "<g stroke-linecap=\"round\" stroke-linejoin=\"round\">"
         "<path d=\"M5.5 21V3.5\" fill=\"none\" stroke=\"#E6EAF0\" stroke-width=\"2\"/>"
         "<path d=\"M5.5 4.5h12.5l-2.5 4 2.5 4H5.5\" fill=\"none\" stroke=\"#E6EAF0\" stroke-width=\"2\"/></g></svg>"},
        // ---- position
        {"position", "Position", "Interface",
         "position, lieu, emplacement, carte, site",
         true, true, 64, 64,
         "<svg xmlns=\"http://www.w3.org/2000/svg\" width=\"64\" height=\"64\" viewBox=\"0 0 24 24\">"
         "<g stroke-linecap=\"round\" stroke-linejoin=\"round\">"
         "<path d=\"M12 21s-6.5-6.2-6.5-11.5a6.5 6.5 0 0 1 13 0C18.5 14.8 12 21 12 21z\" fill=\"none\" stroke=\"#E6EAF0\" stroke-width=\"2\"/>"
         "<circle cx=\"12\" cy=\"9.5\" r=\"2.5\" fill=\"none\" stroke=\"#E6EAF0\" stroke-width=\"2\"/></g></svg>"},
        // ---- danger-general
        {"danger-general", "Danger g\xC3\xA9n\xC3\xA9ral", "S\xC3\xA9" "curit\xC3\xA9 et signalisation",
         "danger, attention, avertissement, triangle",
         false, false, 64, 64,
         "<svg xmlns=\"http://www.w3.org/2000/svg\" width=\"64\" height=\"64\" viewBox=\"0 0 24 24\">"
         "<g stroke-linecap=\"round\" stroke-linejoin=\"round\">"
         "<path d=\"M12 2.4L22.6 20.9H1.4Z\" fill=\"#F5C400\" stroke=\"#1B1B1B\" stroke-width=\"1.7\"/>"
         "<path d=\"M12 8.6v6.2\" fill=\"none\" stroke=\"#1B1B1B\" stroke-width=\"2.4\"/>"
         "<circle cx=\"12\" cy=\"17.9\" r=\"1.35\" fill=\"#1B1B1B\"/></g></svg>"},
        // ---- danger-electrique
        {"danger-electrique", "Danger \xC3\xA9lectrique", "S\xC3\xA9" "curit\xC3\xA9 et signalisation",
         "danger, \xC3\xA9lectricit\xC3\xA9, tension, \xC3\xA9lectrique, foudre",
         false, false, 64, 64,
         "<svg xmlns=\"http://www.w3.org/2000/svg\" width=\"64\" height=\"64\" viewBox=\"0 0 24 24\">"
         "<g stroke-linecap=\"round\" stroke-linejoin=\"round\">"
         "<path d=\"M12 2.4L22.6 20.9H1.4Z\" fill=\"#F5C400\" stroke=\"#1B1B1B\" stroke-width=\"1.7\"/>"
         "<g transform=\"translate(7.2 9.8) scale(0.4)\">"
         "<path d=\"M13.5 2.5L5 13.5h6.5l-1.5 8 9-11.5h-6.5z\" fill=\"#1B1B1B\"/></g></g></svg>"},
        // ---- danger-chaud
        {"danger-chaud", "Surface chaude", "S\xC3\xA9" "curit\xC3\xA9 et signalisation",
         "danger, chaud, chaleur, br\xC3\xBBlure, temp\xC3\xA9rature",
         false, false, 64, 64,
         "<svg xmlns=\"http://www.w3.org/2000/svg\" width=\"64\" height=\"64\" viewBox=\"0 0 24 24\">"
         "<g stroke-linecap=\"round\" stroke-linejoin=\"round\">"
         "<path d=\"M12 2.4L22.6 20.9H1.4Z\" fill=\"#F5C400\" stroke=\"#1B1B1B\" stroke-width=\"1.7\"/>"
         "<g transform=\"translate(7.2 9.8) scale(0.4)\">"
         "<path d=\"M3.5 20h17\" fill=\"none\" stroke=\"#1B1B1B\" stroke-width=\"2.9\"/>"
         "<path d=\"M8 16.5c-1.5-1.8 1.5-3.2 0-5s1.5-3.2 0-5\" fill=\"none\" stroke=\"#1B1B1B\" stroke-width=\"2.9\"/>"
         "<path d=\"M12 16.5c-1.5-1.8 1.5-3.2 0-5s1.5-3.2 0-5\" fill=\"none\" stroke=\"#1B1B1B\" stroke-width=\"2.9\"/>"
         "<path d=\"M16 16.5c-1.5-1.8 1.5-3.2 0-5s1.5-3.2 0-5\" fill=\"none\" stroke=\"#1B1B1B\" stroke-width=\"2.9\"/>"
         "</g></g></svg>"},
        // ---- danger-toxique
        {"danger-toxique", "Mati\xC3\xA8res toxiques", "S\xC3\xA9" "curit\xC3\xA9 et signalisation",
         "danger, toxique, poison, chimique",
         false, false, 64, 64,
         "<svg xmlns=\"http://www.w3.org/2000/svg\" width=\"64\" height=\"64\" viewBox=\"0 0 24 24\">"
         "<g stroke-linecap=\"round\" stroke-linejoin=\"round\">"
         "<path d=\"M12 2.4L22.6 20.9H1.4Z\" fill=\"#F5C400\" stroke=\"#1B1B1B\" stroke-width=\"1.7\"/>"
         "<g transform=\"translate(7.2 9.8) scale(0.4)\">"
         "<path d=\"M12 3a6.5 6.5 0 0 0-6.5 6.5c0 2.4 1.2 4 2.5 4.8V17h8v-2.7c1.3-.8 2.5-2.4 2.5-4.8A6.5 6.5 0 0 0 12 3zM9.3 8a1.8 1.8 0 1 1 0 3.6 1.8 1.8 0 0 1 0-3.6zM14.7 8a1.8 1.8 0 1 1 0 3.6 1.8 1.8 0 0 1 0-3.6z\" fill=\"#1B1B1B\" fill-rule=\"evenodd\"/>"
         "<path d=\"M4.5 18.5l15 3.5M4.5 22l15-3.5\" fill=\"none\" stroke=\"#1B1B1B\" stroke-width=\"2.9\"/></g></g>"
         "</svg>"},
        // ---- danger-inflammable
        {"danger-inflammable", "Mati\xC3\xA8res inflammables", "S\xC3\xA9" "curit\xC3\xA9 et signalisation",
         "danger, inflammable, feu, incendie",
         false, false, 64, 64,
         "<svg xmlns=\"http://www.w3.org/2000/svg\" width=\"64\" height=\"64\" viewBox=\"0 0 24 24\">"
         "<g stroke-linecap=\"round\" stroke-linejoin=\"round\">"
         "<path d=\"M12 2.4L22.6 20.9H1.4Z\" fill=\"#F5C400\" stroke=\"#1B1B1B\" stroke-width=\"1.7\"/>"
         "<g transform=\"translate(7.2 9.8) scale(0.4)\">"
         "<path d=\"M12 21.5c-3.9 0-6.5-2.6-6.5-6 0-3.2 2.2-4.8 3.6-7 .4 1.8 1.3 2.8 2.4 3.3.1-3.1 1.6-5.8 4.2-7.8.2 3.3 2.8 5.1 2.8 10 0 4-2.6 7.5-6.5 7.5z\" fill=\"#1B1B1B\"/>"
         "</g></g></svg>"},
        // ---- danger-explosion
        {"danger-explosion", "Risque d'explosion", "S\xC3\xA9" "curit\xC3\xA9 et signalisation",
         "danger, explosion, explosif, atex",
         false, false, 64, 64,
         "<svg xmlns=\"http://www.w3.org/2000/svg\" width=\"64\" height=\"64\" viewBox=\"0 0 24 24\">"
         "<g stroke-linecap=\"round\" stroke-linejoin=\"round\">"
         "<path d=\"M12 2.4L22.6 20.9H1.4Z\" fill=\"#F5C400\" stroke=\"#1B1B1B\" stroke-width=\"1.7\"/>"
         "<g transform=\"translate(7.2 9.8) scale(0.4)\">"
         "<polygon points=\"13.063 4.137 13.757 9.458 18.872 7.838 15.6 12.091 20.363 14.563 15.042 15.257 16.662 20.372 12.409 17.1 9.937 21.863 9.243 16.542 4.128 18.162 7.4 13.909 2.637 11.437 7.958 10.743 6.338 5.628 10.591 8.9\" fill=\"#1B1B1B\"/>"
         "<rect x=\"18.5\" y=\"2.5\" width=\"2.5\" height=\"2.5\" rx=\"0.3\" fill=\"#1B1B1B\"/>"
         "<rect x=\"2.5\" y=\"3.5\" width=\"2\" height=\"2\" rx=\"0.3\" fill=\"#1B1B1B\"/>"
         "<rect x=\"19.5\" y=\"17\" width=\"2\" height=\"2\" rx=\"0.3\" fill=\"#1B1B1B\"/></g></g></svg>"},
        // ---- danger-radiation
        {"danger-radiation", "Rayonnement ionisant", "S\xC3\xA9" "curit\xC3\xA9 et signalisation",
         "danger, radiation, radioactif, nucl\xC3\xA9" "aire",
         false, false, 64, 64,
         "<svg xmlns=\"http://www.w3.org/2000/svg\" width=\"64\" height=\"64\" viewBox=\"0 0 24 24\">"
         "<g stroke-linecap=\"round\" stroke-linejoin=\"round\">"
         "<path d=\"M12 2.4L22.6 20.9H1.4Z\" fill=\"#F5C400\" stroke=\"#1B1B1B\" stroke-width=\"1.7\"/>"
         "<g transform=\"translate(7.2 9.8) scale(0.4)\">"
         "<path d=\"M7.25 3.773 A9.5 9.5 0 0 1 16.75 3.773L13.4 9.575 A2.8 2.8 0 0 0 10.6 9.575Z\" fill=\"#1B1B1B\"/>"
         "<path d=\"M21.5 12 A9.5 9.5 0 0 1 16.75 20.227L13.4 14.425 A2.8 2.8 0 0 0 14.8 12Z\" fill=\"#1B1B1B\"/>"
         "<path d=\"M7.25 20.227 A9.5 9.5 0 0 1 2.5 12L9.2 12 A2.8 2.8 0 0 0 10.6 14.425Z\" fill=\"#1B1B1B\"/>"
         "<circle cx=\"12\" cy=\"12\" r=\"1.736\" fill=\"#1B1B1B\"/></g></g></svg>"},
        // ---- danger-laser
        {"danger-laser", "Faisceau laser", "S\xC3\xA9" "curit\xC3\xA9 et signalisation",
         "danger, laser, rayon, faisceau",
         false, false, 64, 64,
         "<svg xmlns=\"http://www.w3.org/2000/svg\" width=\"64\" height=\"64\" viewBox=\"0 0 24 24\">"
         "<g stroke-linecap=\"round\" stroke-linejoin=\"round\">"
         "<path d=\"M12 2.4L22.6 20.9H1.4Z\" fill=\"#F5C400\" stroke=\"#1B1B1B\" stroke-width=\"1.7\"/>"
         "<g transform=\"translate(7.2 9.8) scale(0.4)\">"
         "<path d=\"M2.5 12h8\" fill=\"none\" stroke=\"#1B1B1B\" stroke-width=\"2.9\"/>"
         "<circle cx=\"15\" cy=\"12\" r=\"2\" fill=\"#1B1B1B\"/>"
         "<path d=\"M15 5.5v2.5M15 16v2.5M21.5 12H19M10.4 7.4l1.7 1.7M19.6 7.4l-1.7 1.7M10.4 16.6l1.7-1.7M19.6 16.6l-1.7-1.7\" fill=\"none\" stroke=\"#1B1B1B\" stroke-width=\"2.9\"/>"
         "</g></g></svg>"},
        // ---- danger-magnetique
        {"danger-magnetique", "Champ magn\xC3\xA9tique", "S\xC3\xA9" "curit\xC3\xA9 et signalisation",
         "danger, magn\xC3\xA9tique, aimant, champ",
         false, false, 64, 64,
         "<svg xmlns=\"http://www.w3.org/2000/svg\" width=\"64\" height=\"64\" viewBox=\"0 0 24 24\">"
         "<g stroke-linecap=\"round\" stroke-linejoin=\"round\">"
         "<path d=\"M12 2.4L22.6 20.9H1.4Z\" fill=\"#F5C400\" stroke=\"#1B1B1B\" stroke-width=\"1.7\"/>"
         "<g transform=\"translate(7.2 9.8) scale(0.4)\">"
         "<path d=\"M7 3.5v8a5 5 0 0 0 10 0v-8\" fill=\"none\" stroke=\"#1B1B1B\" stroke-width=\"4.93\" stroke-linecap=\"butt\"/>"
         "<path d=\"M5 7h4M15 7h4\" fill=\"none\" stroke=\"#1B1B1B\" stroke-width=\"2.03\"/>"
         "<path d=\"M3 18.5c2.5 2.6 15.5 2.6 18 0\" fill=\"none\" stroke=\"#1B1B1B\" stroke-width=\"2.03\"/></g></g>"
         "</svg>"},
        // ---- danger-corrosif
        {"danger-corrosif", "Mati\xC3\xA8res corrosives", "S\xC3\xA9" "curit\xC3\xA9 et signalisation",
         "danger, corrosif, acide, chimique",
         false, false, 64, 64,
         "<svg xmlns=\"http://www.w3.org/2000/svg\" width=\"64\" height=\"64\" viewBox=\"0 0 24 24\">"
         "<g stroke-linecap=\"round\" stroke-linejoin=\"round\">"
         "<path d=\"M12 2.4L22.6 20.9H1.4Z\" fill=\"#F5C400\" stroke=\"#1B1B1B\" stroke-width=\"1.7\"/>"
         "<g transform=\"translate(7.2 9.8) scale(0.4)\">"
         "<path d=\"M4 4.5l7.5 3.8-1.8 3.6-7.5-3.8z\" fill=\"none\" stroke=\"#1B1B1B\" stroke-width=\"2.9\"/>"
         "<circle cx=\"11.5\" cy=\"14\" r=\"1\" fill=\"#1B1B1B\"/>"
         "<circle cx=\"9.5\" cy=\"16.5\" r=\"1\" fill=\"#1B1B1B\"/>"
         "<path d=\"M3.5 20.5h17\" fill=\"none\" stroke=\"#1B1B1B\" stroke-width=\"2.9\"/>"
         "<path d=\"M14 20.5l1.5-2.5 1.5 2.5\" fill=\"none\" stroke=\"#1B1B1B\" stroke-width=\"2.9\"/></g></g></svg>"},
        // ---- danger-froid
        {"danger-froid", "Basse temp\xC3\xA9rature", "S\xC3\xA9" "curit\xC3\xA9 et signalisation",
         "danger, froid, gel, basse temp\xC3\xA9rature",
         false, false, 64, 64,
         "<svg xmlns=\"http://www.w3.org/2000/svg\" width=\"64\" height=\"64\" viewBox=\"0 0 24 24\">"
         "<g stroke-linecap=\"round\" stroke-linejoin=\"round\">"
         "<path d=\"M12 2.4L22.6 20.9H1.4Z\" fill=\"#F5C400\" stroke=\"#1B1B1B\" stroke-width=\"1.7\"/>"
         "<g transform=\"translate(7.2 9.8) scale(0.4)\"><g transform=\"rotate(0 12 12)\">"
         "<path d=\"M12 2.5v19\" fill=\"none\" stroke=\"#1B1B1B\" stroke-width=\"2.9\"/></g>"
         "<g transform=\"rotate(60 12 12)\">"
         "<path d=\"M12 2.5v19\" fill=\"none\" stroke=\"#1B1B1B\" stroke-width=\"2.9\"/></g>"
         "<g transform=\"rotate(120 12 12)\">"
         "<path d=\"M12 2.5v19\" fill=\"none\" stroke=\"#1B1B1B\" stroke-width=\"2.9\"/></g>"
         "<g transform=\"rotate(0 12 12)\">"
         "<path d=\"M9.5 4l2.5 2.5L14.5 4M9.5 20l2.5-2.5 2.5 2.5\" fill=\"none\" stroke=\"#1B1B1B\" stroke-width=\"2.9\"/>"
         "</g><g transform=\"rotate(60 12 12)\">"
         "<path d=\"M9.5 4l2.5 2.5L14.5 4M9.5 20l2.5-2.5 2.5 2.5\" fill=\"none\" stroke=\"#1B1B1B\" stroke-width=\"2.9\"/>"
         "</g><g transform=\"rotate(120 12 12)\">"
         "<path d=\"M9.5 4l2.5 2.5L14.5 4M9.5 20l2.5-2.5 2.5 2.5\" fill=\"none\" stroke=\"#1B1B1B\" stroke-width=\"2.9\"/>"
         "</g></g></g></svg>"},
        // ---- danger-chariots
        {"danger-chariots", "Chariots \xC3\xA9l\xC3\xA9vateurs", "S\xC3\xA9" "curit\xC3\xA9 et signalisation",
         "danger, chariot, \xC3\xA9l\xC3\xA9vateur, circulation",
         false, false, 64, 64,
         "<svg xmlns=\"http://www.w3.org/2000/svg\" width=\"64\" height=\"64\" viewBox=\"0 0 24 24\">"
         "<g stroke-linecap=\"round\" stroke-linejoin=\"round\">"
         "<path d=\"M12 2.4L22.6 20.9H1.4Z\" fill=\"#F5C400\" stroke=\"#1B1B1B\" stroke-width=\"1.7\"/>"
         "<g transform=\"translate(7.2 9.8) scale(0.4)\">"
         "<path d=\"M2.5 16.5V12a1 1 0 0 1 1-1h8.8l1.7 2.6v2.9z\" fill=\"#1B1B1B\"/>"
         "<path d=\"M5.2 11V5.2h5l2.3 5.8\" fill=\"none\" stroke=\"#1B1B1B\" stroke-width=\"2.9\"/>"
         "<path d=\"M15.6 3v15.8H22\" fill=\"none\" stroke=\"#1B1B1B\" stroke-width=\"2.9\"/>"
         "<circle cx=\"6\" cy=\"18.6\" r=\"2.4\" fill=\"#1B1B1B\"/>"
         "<circle cx=\"12.4\" cy=\"18.6\" r=\"2.4\" fill=\"#1B1B1B\"/></g></g></svg>"},
        // ---- danger-ondes
        {"danger-ondes", "Ondes radio", "S\xC3\xA9" "curit\xC3\xA9 et signalisation",
         "danger, ondes, rayonnement, antenne",
         false, false, 64, 64,
         "<svg xmlns=\"http://www.w3.org/2000/svg\" width=\"64\" height=\"64\" viewBox=\"0 0 24 24\">"
         "<g stroke-linecap=\"round\" stroke-linejoin=\"round\">"
         "<path d=\"M12 2.4L22.6 20.9H1.4Z\" fill=\"#F5C400\" stroke=\"#1B1B1B\" stroke-width=\"1.7\"/>"
         "<g transform=\"translate(7.2 9.8) scale(0.4)\">"
         "<path d=\"M12 10.5V21M8.5 21h7\" fill=\"none\" stroke=\"#1B1B1B\" stroke-width=\"2.9\"/>"
         "<circle cx=\"12\" cy=\"9\" r=\"1.7\" fill=\"#1B1B1B\"/>"
         "<path d=\"M8.6 5.6a4.8 4.8 0 0 0 0 6.8M15.4 5.6a4.8 4.8 0 0 1 0 6.8\" fill=\"none\" stroke=\"#1B1B1B\" stroke-width=\"2.9\"/>"
         "<path d=\"M5.6 2.6a9 9 0 0 0 0 12.8M18.4 2.6a9 9 0 0 1 0 12.8\" fill=\"none\" stroke=\"#1B1B1B\" stroke-width=\"2.9\"/>"
         "</g></g></svg>"},
        // ---- interdit
        {"interdit", "Interdiction", "S\xC3\xA9" "curit\xC3\xA9 et signalisation",
         "interdit, interdiction, d\xC3\xA9" "fense",
         false, false, 64, 64,
         "<svg xmlns=\"http://www.w3.org/2000/svg\" width=\"64\" height=\"64\" viewBox=\"0 0 24 24\">"
         "<g stroke-linecap=\"round\" stroke-linejoin=\"round\">"
         "<circle cx=\"12\" cy=\"12\" r=\"10.3\" fill=\"#FFFFFF\"/>"
         "<circle cx=\"12\" cy=\"12\" r=\"9.2\" fill=\"none\" stroke=\"#D93A2B\" stroke-width=\"2.6\"/>"
         "<path d=\"M5.5 5.5L18.5 18.5\" fill=\"none\" stroke=\"#D93A2B\" stroke-width=\"2.6\" stroke-linecap=\"butt\"/>"
         "</g></svg>"},
        // ---- interdit-fumer
        {"interdit-fumer", "D\xC3\xA9" "fense de fumer", "S\xC3\xA9" "curit\xC3\xA9 et signalisation",
         "interdit, fumer, cigarette, tabac",
         false, false, 64, 64,
         "<svg xmlns=\"http://www.w3.org/2000/svg\" width=\"64\" height=\"64\" viewBox=\"0 0 24 24\">"
         "<g stroke-linecap=\"round\" stroke-linejoin=\"round\">"
         "<circle cx=\"12\" cy=\"12\" r=\"10.3\" fill=\"#FFFFFF\"/>"
         "<g transform=\"translate(5.75 5.75) scale(0.521)\">"
         "<rect x=\"2.5\" y=\"13\" width=\"14\" height=\"3.5\" rx=\"0.5\" fill=\"#1B1B1B\"/>"
         "<rect x=\"17.5\" y=\"13\" width=\"4\" height=\"3.5\" rx=\"0.5\" fill=\"#1B1B1B\"/>"
         "<path d=\"M18 10.5c-1-1.5 1-2.5 0-4M20.5 10.5c-1-1.5 1-2.5 0-4\" fill=\"none\" stroke=\"#1B1B1B\" stroke-width=\"2.08\"/>"
         "</g><circle cx=\"12\" cy=\"12\" r=\"9.2\" fill=\"none\" stroke=\"#D93A2B\" stroke-width=\"2.6\"/>"
         "<path d=\"M5.5 5.5L18.5 18.5\" fill=\"none\" stroke=\"#D93A2B\" stroke-width=\"2.6\" stroke-linecap=\"butt\"/>"
         "</g></svg>"},
        // ---- interdit-flamme
        {"interdit-flamme", "Flamme nue interdite", "S\xC3\xA9" "curit\xC3\xA9 et signalisation",
         "interdit, flamme, feu, allumer",
         false, false, 64, 64,
         "<svg xmlns=\"http://www.w3.org/2000/svg\" width=\"64\" height=\"64\" viewBox=\"0 0 24 24\">"
         "<g stroke-linecap=\"round\" stroke-linejoin=\"round\">"
         "<circle cx=\"12\" cy=\"12\" r=\"10.3\" fill=\"#FFFFFF\"/>"
         "<g transform=\"translate(5.75 5.75) scale(0.521)\">"
         "<path d=\"M12 21.5c-3.9 0-6.5-2.6-6.5-6 0-3.2 2.2-4.8 3.6-7 .4 1.8 1.3 2.8 2.4 3.3.1-3.1 1.6-5.8 4.2-7.8.2 3.3 2.8 5.1 2.8 10 0 4-2.6 7.5-6.5 7.5z\" fill=\"#1B1B1B\"/>"
         "</g><circle cx=\"12\" cy=\"12\" r=\"9.2\" fill=\"none\" stroke=\"#D93A2B\" stroke-width=\"2.6\"/>"
         "<path d=\"M5.5 5.5L18.5 18.5\" fill=\"none\" stroke=\"#D93A2B\" stroke-width=\"2.6\" stroke-linecap=\"butt\"/>"
         "</g></svg>"},
        // ---- interdit-pietons
        {"interdit-pietons", "Interdit aux pi\xC3\xA9tons", "S\xC3\xA9" "curit\xC3\xA9 et signalisation",
         "interdit, pi\xC3\xA9ton, passage, personne",
         false, false, 64, 64,
         "<svg xmlns=\"http://www.w3.org/2000/svg\" width=\"64\" height=\"64\" viewBox=\"0 0 24 24\">"
         "<g stroke-linecap=\"round\" stroke-linejoin=\"round\">"
         "<circle cx=\"12\" cy=\"12\" r=\"10.3\" fill=\"#FFFFFF\"/>"
         "<g transform=\"translate(5.75 5.75) scale(0.521)\">"
         "<circle cx=\"13.2\" cy=\"4.2\" r=\"2.1\" fill=\"#1B1B1B\"/>"
         "<path d=\"M12.6 7.6L11 13.6\" fill=\"none\" stroke=\"#1B1B1B\" stroke-width=\"2.6\"/>"
         "<path d=\"M11 13.6l3 3 1.2 4.6\" fill=\"none\" stroke=\"#1B1B1B\" stroke-width=\"2.6\"/>"
         "<path d=\"M11 13.6l-2 3.6-2.6 3.3\" fill=\"none\" stroke=\"#1B1B1B\" stroke-width=\"2.6\"/>"
         "<path d=\"M12.4 8.6l3.2 2.8 2.4-.6\" fill=\"none\" stroke=\"#1B1B1B\" stroke-width=\"2.6\"/>"
         "<path d=\"M12.4 8.6l-3.2 2-1 3.2\" fill=\"none\" stroke=\"#1B1B1B\" stroke-width=\"2.6\"/></g>"
         "<circle cx=\"12\" cy=\"12\" r=\"9.2\" fill=\"none\" stroke=\"#D93A2B\" stroke-width=\"2.6\"/>"
         "<path d=\"M5.5 5.5L18.5 18.5\" fill=\"none\" stroke=\"#D93A2B\" stroke-width=\"2.6\" stroke-linecap=\"butt\"/>"
         "</g></svg>"},
        // ---- interdit-toucher
        {"interdit-toucher", "Ne pas toucher", "S\xC3\xA9" "curit\xC3\xA9 et signalisation",
         "interdit, toucher, main, contact",
         false, false, 64, 64,
         "<svg xmlns=\"http://www.w3.org/2000/svg\" width=\"64\" height=\"64\" viewBox=\"0 0 24 24\">"
         "<g stroke-linecap=\"round\" stroke-linejoin=\"round\">"
         "<circle cx=\"12\" cy=\"12\" r=\"10.3\" fill=\"#FFFFFF\"/>"
         "<g transform=\"translate(5.75 5.75) scale(0.521)\">"
         "<rect x=\"7\" y=\"10.5\" width=\"10.5\" height=\"9\" rx=\"3\" fill=\"#1B1B1B\"/>"
         "<path d=\"M8.2 11V5.5M11.1 10.5V3.6M14 10.5V4.2M16.6 11.5V6.6\" fill=\"none\" stroke=\"#1B1B1B\" stroke-width=\"3.12\"/>"
         "<path d=\"M7.6 15.6L4.3 12.4\" fill=\"none\" stroke=\"#1B1B1B\" stroke-width=\"3.12\"/>"
         "<rect x=\"8.5\" y=\"18.5\" width=\"7.5\" height=\"3.5\" rx=\"1\" fill=\"#1B1B1B\"/></g>"
         "<circle cx=\"12\" cy=\"12\" r=\"9.2\" fill=\"none\" stroke=\"#D93A2B\" stroke-width=\"2.6\"/>"
         "<path d=\"M5.5 5.5L18.5 18.5\" fill=\"none\" stroke=\"#D93A2B\" stroke-width=\"2.6\" stroke-linecap=\"butt\"/>"
         "</g></svg>"},
        // ---- interdit-telephone
        {"interdit-telephone", "T\xC3\xA9l\xC3\xA9phone interdit", "S\xC3\xA9" "curit\xC3\xA9 et signalisation",
         "interdit, t\xC3\xA9l\xC3\xA9phone, portable, mobile",
         false, false, 64, 64,
         "<svg xmlns=\"http://www.w3.org/2000/svg\" width=\"64\" height=\"64\" viewBox=\"0 0 24 24\">"
         "<g stroke-linecap=\"round\" stroke-linejoin=\"round\">"
         "<circle cx=\"12\" cy=\"12\" r=\"10.3\" fill=\"#FFFFFF\"/>"
         "<g transform=\"translate(5.75 5.75) scale(0.521)\">"
         "<rect x=\"6\" y=\"2.5\" width=\"12\" height=\"19\" rx=\"2.5\" fill=\"none\" stroke=\"#1B1B1B\" stroke-width=\"2.6\"/>"
         "<path d=\"M10.5 18.5h3\" fill=\"none\" stroke=\"#1B1B1B\" stroke-width=\"2.6\"/></g>"
         "<circle cx=\"12\" cy=\"12\" r=\"9.2\" fill=\"none\" stroke=\"#D93A2B\" stroke-width=\"2.6\"/>"
         "<path d=\"M5.5 5.5L18.5 18.5\" fill=\"none\" stroke=\"#D93A2B\" stroke-width=\"2.6\" stroke-linecap=\"butt\"/>"
         "</g></svg>"},
        // ---- interdit-eau
        {"interdit-eau", "Ne pas utiliser d'eau", "S\xC3\xA9" "curit\xC3\xA9 et signalisation",
         "interdit, eau, \xC3\xA9teindre, arroser",
         false, false, 64, 64,
         "<svg xmlns=\"http://www.w3.org/2000/svg\" width=\"64\" height=\"64\" viewBox=\"0 0 24 24\">"
         "<g stroke-linecap=\"round\" stroke-linejoin=\"round\">"
         "<circle cx=\"12\" cy=\"12\" r=\"10.3\" fill=\"#FFFFFF\"/>"
         "<g transform=\"translate(5.75 5.75) scale(0.521)\">"
         "<path d=\"M12 3s-6.5 7.2-6.5 11.5a6.5 6.5 0 0 0 13 0C18.5 10.2 12 3 12 3z\" fill=\"none\" stroke=\"#1B1B1B\" stroke-width=\"2.6\"/>"
         "</g><circle cx=\"12\" cy=\"12\" r=\"9.2\" fill=\"none\" stroke=\"#D93A2B\" stroke-width=\"2.6\"/>"
         "<path d=\"M5.5 5.5L18.5 18.5\" fill=\"none\" stroke=\"#D93A2B\" stroke-width=\"2.6\" stroke-linecap=\"butt\"/>"
         "</g></svg>"},
        // ---- interdit-chariots
        {"interdit-chariots", "Interdit aux chariots", "S\xC3\xA9" "curit\xC3\xA9 et signalisation",
         "interdit, chariot, \xC3\xA9l\xC3\xA9vateur, engin",
         false, false, 64, 64,
         "<svg xmlns=\"http://www.w3.org/2000/svg\" width=\"64\" height=\"64\" viewBox=\"0 0 24 24\">"
         "<g stroke-linecap=\"round\" stroke-linejoin=\"round\">"
         "<circle cx=\"12\" cy=\"12\" r=\"10.3\" fill=\"#FFFFFF\"/>"
         "<g transform=\"translate(5.75 5.75) scale(0.521)\">"
         "<path d=\"M2.5 16.5V12a1 1 0 0 1 1-1h8.8l1.7 2.6v2.9z\" fill=\"#1B1B1B\"/>"
         "<path d=\"M5.2 11V5.2h5l2.3 5.8\" fill=\"none\" stroke=\"#1B1B1B\" stroke-width=\"2.6\"/>"
         "<path d=\"M15.6 3v15.8H22\" fill=\"none\" stroke=\"#1B1B1B\" stroke-width=\"2.6\"/>"
         "<circle cx=\"6\" cy=\"18.6\" r=\"2.4\" fill=\"#1B1B1B\"/>"
         "<circle cx=\"12.4\" cy=\"18.6\" r=\"2.4\" fill=\"#1B1B1B\"/></g>"
         "<circle cx=\"12\" cy=\"12\" r=\"9.2\" fill=\"none\" stroke=\"#D93A2B\" stroke-width=\"2.6\"/>"
         "<path d=\"M5.5 5.5L18.5 18.5\" fill=\"none\" stroke=\"#D93A2B\" stroke-width=\"2.6\" stroke-linecap=\"butt\"/>"
         "</g></svg>"},
        // ---- obligation
        {"obligation", "Obligation", "S\xC3\xA9" "curit\xC3\xA9 et signalisation",
         "obligation, obligatoire, consigne",
         false, false, 64, 64,
         "<svg xmlns=\"http://www.w3.org/2000/svg\" width=\"64\" height=\"64\" viewBox=\"0 0 24 24\">"
         "<g stroke-linecap=\"round\" stroke-linejoin=\"round\">"
         "<circle cx=\"12\" cy=\"12\" r=\"10.5\" fill=\"#1F64B5\"/>"
         "<path d=\"M12 6.5v7.5\" fill=\"none\" stroke=\"#FFFFFF\" stroke-width=\"2.6\"/>"
         "<circle cx=\"12\" cy=\"17.3\" r=\"1.5\" fill=\"#FFFFFF\"/></g></svg>"},
        // ---- obligation-casque
        {"obligation-casque", "Casque obligatoire", "S\xC3\xA9" "curit\xC3\xA9 et signalisation",
         "obligation, casque, t\xC3\xAAte, protection",
         false, false, 64, 64,
         "<svg xmlns=\"http://www.w3.org/2000/svg\" width=\"64\" height=\"64\" viewBox=\"0 0 24 24\">"
         "<g stroke-linecap=\"round\" stroke-linejoin=\"round\">"
         "<circle cx=\"12\" cy=\"12\" r=\"10.5\" fill=\"#1F64B5\"/>"
         "<g transform=\"translate(4.75 4.75) scale(0.604)\">"
         "<path d=\"M4.5 16a7.5 7.5 0 0 1 15 0z\" fill=\"#FFFFFF\"/>"
         "<rect x=\"2.5\" y=\"16\" width=\"19\" height=\"2.6\" rx=\"1.3\" fill=\"#FFFFFF\"/></g></g></svg>"},
        // ---- obligation-lunettes
        {"obligation-lunettes", "Lunettes obligatoires", "S\xC3\xA9" "curit\xC3\xA9 et signalisation",
         "obligation, lunettes, yeux, protection",
         false, false, 64, 64,
         "<svg xmlns=\"http://www.w3.org/2000/svg\" width=\"64\" height=\"64\" viewBox=\"0 0 24 24\">"
         "<g stroke-linecap=\"round\" stroke-linejoin=\"round\">"
         "<circle cx=\"12\" cy=\"12\" r=\"10.5\" fill=\"#1F64B5\"/>"
         "<g transform=\"translate(4.75 4.75) scale(0.604)\">"
         "<rect x=\"2.5\" y=\"9.5\" width=\"8\" height=\"6\" rx=\"3\" fill=\"none\" stroke=\"#FFFFFF\" stroke-width=\"2.5\"/>"
         "<rect x=\"13.5\" y=\"9.5\" width=\"8\" height=\"6\" rx=\"3\" fill=\"none\" stroke=\"#FFFFFF\" stroke-width=\"2.5\"/>"
         "<path d=\"M10.5 11.5c1-.8 2-.8 3 0\" fill=\"none\" stroke=\"#FFFFFF\" stroke-width=\"2.5\"/></g></g></svg>"},
        // ---- obligation-gants
        {"obligation-gants", "Gants obligatoires", "S\xC3\xA9" "curit\xC3\xA9 et signalisation",
         "obligation, gants, mains, protection",
         false, false, 64, 64,
         "<svg xmlns=\"http://www.w3.org/2000/svg\" width=\"64\" height=\"64\" viewBox=\"0 0 24 24\">"
         "<g stroke-linecap=\"round\" stroke-linejoin=\"round\">"
         "<circle cx=\"12\" cy=\"12\" r=\"10.5\" fill=\"#1F64B5\"/>"
         "<g transform=\"translate(4.75 4.75) scale(0.604)\">"
         "<path d=\"M7 20v-3.5L4.3 13.8a1.4 1.4 0 0 1 2.2-1.7L8.5 14V5.8a1.3 1.3 0 0 1 2.6 0V11 4.6a1.3 1.3 0 0 1 2.6 0V11 5.4a1.3 1.3 0 0 1 2.6 0v6.1V7.6a1.3 1.3 0 0 1 2.6 0V15c0 2.5-1 4-2.5 5z\" fill=\"#FFFFFF\"/>"
         "<rect x=\"6.5\" y=\"20.5\" width=\"10.5\" height=\"2.5\" rx=\"0.5\" fill=\"#FFFFFF\"/></g></g></svg>"},
        // ---- obligation-chaussures
        {"obligation-chaussures", "Chaussures de s\xC3\xA9" "curit\xC3\xA9", "S\xC3\xA9" "curit\xC3\xA9 et signalisation",
         "obligation, chaussures, pieds, protection",
         false, false, 64, 64,
         "<svg xmlns=\"http://www.w3.org/2000/svg\" width=\"64\" height=\"64\" viewBox=\"0 0 24 24\">"
         "<g stroke-linecap=\"round\" stroke-linejoin=\"round\">"
         "<circle cx=\"12\" cy=\"12\" r=\"10.5\" fill=\"#1F64B5\"/>"
         "<g transform=\"translate(4.75 4.75) scale(0.604)\">"
         "<path d=\"M6 3.5h5.5v9l6 2.2c1.6.6 2.5 1.9 2.5 3.5V20H4.5V5a1.5 1.5 0 0 1 1.5-1.5z\" fill=\"#FFFFFF\"/>"
         "<rect x=\"4\" y=\"20.5\" width=\"16.5\" height=\"2\" rx=\"0.8\" fill=\"#FFFFFF\"/></g></g></svg>"},
        // ---- obligation-auditive
        {"obligation-auditive", "Protection auditive", "S\xC3\xA9" "curit\xC3\xA9 et signalisation",
         "obligation, casque antibruit, oreilles, bruit, protection",
         false, false, 64, 64,
         "<svg xmlns=\"http://www.w3.org/2000/svg\" width=\"64\" height=\"64\" viewBox=\"0 0 24 24\">"
         "<g stroke-linecap=\"round\" stroke-linejoin=\"round\">"
         "<circle cx=\"12\" cy=\"12\" r=\"10.5\" fill=\"#1F64B5\"/>"
         "<g transform=\"translate(4.75 4.75) scale(0.604)\">"
         "<path d=\"M5.5 13v-2a6.5 6.5 0 0 1 13 0v2\" fill=\"none\" stroke=\"#FFFFFF\" stroke-width=\"2.5\"/>"
         "<rect x=\"3\" y=\"12\" width=\"5\" height=\"8\" rx=\"2\" fill=\"#FFFFFF\"/>"
         "<rect x=\"16\" y=\"12\" width=\"5\" height=\"8\" rx=\"2\" fill=\"#FFFFFF\"/></g></g></svg>"},
        // ---- obligation-masque
        {"obligation-masque", "Masque obligatoire", "S\xC3\xA9" "curit\xC3\xA9 et signalisation",
         "obligation, masque, respiratoire, poussi\xC3\xA8re, protection",
         false, false, 64, 64,
         "<svg xmlns=\"http://www.w3.org/2000/svg\" width=\"64\" height=\"64\" viewBox=\"0 0 24 24\">"
         "<g stroke-linecap=\"round\" stroke-linejoin=\"round\">"
         "<circle cx=\"12\" cy=\"12\" r=\"10.5\" fill=\"#1F64B5\"/>"
         "<g transform=\"translate(4.75 4.75) scale(0.604)\">"
         "<circle cx=\"12\" cy=\"11\" r=\"8\" fill=\"none\" stroke=\"#FFFFFF\" stroke-width=\"2.5\"/>"
         "<circle cx=\"9.2\" cy=\"8.8\" r=\"1.1\" fill=\"#FFFFFF\"/>"
         "<circle cx=\"14.8\" cy=\"8.8\" r=\"1.1\" fill=\"#FFFFFF\"/>"
         "<path d=\"M6.5 12.2h11c0 3.8-2.4 6.6-5.5 6.6s-5.5-2.8-5.5-6.6z\" fill=\"#FFFFFF\"/>"
         "<path d=\"M4.2 12.8l2.6-.4M19.8 12.8l-2.6-.4\" fill=\"none\" stroke=\"#FFFFFF\" stroke-width=\"2.5\"/></g>"
         "</g></svg>"},
        // ---- obligation-gilet
        {"obligation-gilet", "Gilet obligatoire", "S\xC3\xA9" "curit\xC3\xA9 et signalisation",
         "obligation, gilet, haute visibilit\xC3\xA9, protection",
         false, false, 64, 64,
         "<svg xmlns=\"http://www.w3.org/2000/svg\" width=\"64\" height=\"64\" viewBox=\"0 0 24 24\">"
         "<g stroke-linecap=\"round\" stroke-linejoin=\"round\">"
         "<circle cx=\"12\" cy=\"12\" r=\"10.5\" fill=\"#1F64B5\"/>"
         "<g transform=\"translate(4.75 4.75) scale(0.604)\">"
         "<path d=\"M8 3.5L4 6v14.5h6.5V12M16 3.5L20 6v14.5h-6.5V12M8 3.5c0 2.5 1.6 4.5 4 4.5s4-2 4-4.5\" fill=\"none\" stroke=\"#FFFFFF\" stroke-width=\"2.5\"/>"
         "<path d=\"M4 15h6.5M13.5 15H20\" fill=\"none\" stroke=\"#FFFFFF\" stroke-width=\"2.5\"/></g></g></svg>"},
        // ---- obligation-notice
        {"obligation-notice", "Consulter la notice", "S\xC3\xA9" "curit\xC3\xA9 et signalisation",
         "obligation, notice, manuel, lire, instructions",
         false, false, 64, 64,
         "<svg xmlns=\"http://www.w3.org/2000/svg\" width=\"64\" height=\"64\" viewBox=\"0 0 24 24\">"
         "<g stroke-linecap=\"round\" stroke-linejoin=\"round\">"
         "<circle cx=\"12\" cy=\"12\" r=\"10.5\" fill=\"#1F64B5\"/>"
         "<g transform=\"translate(4.75 4.75) scale(0.604)\">"
         "<path d=\"M12 6.5C10 5 7 4.5 3.5 5v13c3.5-.5 6.5 0 8.5 1.5 2-1.5 5-2 8.5-1.5V5c-3.5-.5-6.5 0-8.5 1.5zM12 6.5v13\" fill=\"none\" stroke=\"#FFFFFF\" stroke-width=\"2.5\"/>"
         "</g></g></svg>"},
        // ---- premiers-secours
        {"premiers-secours", "Premiers secours", "S\xC3\xA9" "curit\xC3\xA9 et signalisation",
         "secours, premiers secours, infirmerie, croix",
         false, false, 64, 64,
         "<svg xmlns=\"http://www.w3.org/2000/svg\" width=\"64\" height=\"64\" viewBox=\"0 0 24 24\">"
         "<g stroke-linecap=\"round\" stroke-linejoin=\"round\">"
         "<rect x=\"1.5\" y=\"1.5\" width=\"21\" height=\"21\" rx=\"2.2\" fill=\"#1E9A4A\"/>"
         "<g transform=\"translate(4 4) scale(0.667)\">"
         "<path d=\"M9.5 4h5v5.5H20v5h-5.5V20h-5v-5.5H4v-5h5.5z\" fill=\"#FFFFFF\"/></g></g></svg>"},
        // ---- sortie-secours
        {"sortie-secours", "Sortie de secours", "S\xC3\xA9" "curit\xC3\xA9 et signalisation",
         "secours, sortie, \xC3\xA9vacuation, issue",
         false, false, 64, 64,
         "<svg xmlns=\"http://www.w3.org/2000/svg\" width=\"64\" height=\"64\" viewBox=\"0 0 24 24\">"
         "<g stroke-linecap=\"round\" stroke-linejoin=\"round\">"
         "<rect x=\"1.5\" y=\"1.5\" width=\"21\" height=\"21\" rx=\"2.2\" fill=\"#1E9A4A\"/>"
         "<g transform=\"translate(4 4) scale(0.667)\">"
         "<path d=\"M15 3.5h5.5v17H15\" fill=\"none\" stroke=\"#FFFFFF\" stroke-width=\"2.4\"/>"
         "<circle cx=\"9\" cy=\"5\" r=\"1.9\" fill=\"#FFFFFF\"/>"
         "<path d=\"M8.5 8.3l-1.5 5 3 2.5.6 4.7\" fill=\"none\" stroke=\"#FFFFFF\" stroke-width=\"2.4\"/>"
         "<path d=\"M7 13.3l-1.3 3.4-2.7 2.6\" fill=\"none\" stroke=\"#FFFFFF\" stroke-width=\"2.4\"/>"
         "<path d=\"M8.6 9l3.3 2.4 2.6-.4\" fill=\"none\" stroke=\"#FFFFFF\" stroke-width=\"2.4\"/>"
         "<path d=\"M8.6 9l-3 1.6-1.4 2.6\" fill=\"none\" stroke=\"#FFFFFF\" stroke-width=\"2.4\"/></g></g></svg>"},
        // ---- douche-securite
        {"douche-securite", "Douche de s\xC3\xA9" "curit\xC3\xA9", "S\xC3\xA9" "curit\xC3\xA9 et signalisation",
         "secours, douche, rin\xC3\xA7" "age, chimique",
         false, false, 64, 64,
         "<svg xmlns=\"http://www.w3.org/2000/svg\" width=\"64\" height=\"64\" viewBox=\"0 0 24 24\">"
         "<g stroke-linecap=\"round\" stroke-linejoin=\"round\">"
         "<rect x=\"1.5\" y=\"1.5\" width=\"21\" height=\"21\" rx=\"2.2\" fill=\"#1E9A4A\"/>"
         "<g transform=\"translate(4 4) scale(0.667)\">"
         "<path d=\"M4 3.5h8.5v3\" fill=\"none\" stroke=\"#FFFFFF\" stroke-width=\"2.4\"/>"
         "<path d=\"M7.5 10.5a5 4 0 0 1 10 0z\" fill=\"#FFFFFF\"/>"
         "<path d=\"M9 13.5l-1 3M12.5 13.5v3.5M16 13.5l1 3M10.5 18.5l-.6 2M14.5 18.5l.6 2\" fill=\"none\" stroke=\"#FFFFFF\" stroke-width=\"2.4\"/>"
         "</g></g></svg>"},
        // ---- rince-oeil
        {"rince-oeil", "Rince-\xC5\x93il", "S\xC3\xA9" "curit\xC3\xA9 et signalisation",
         "secours, rince-\xC5\x93il, yeux, lavage",
         false, false, 64, 64,
         "<svg xmlns=\"http://www.w3.org/2000/svg\" width=\"64\" height=\"64\" viewBox=\"0 0 24 24\">"
         "<g stroke-linecap=\"round\" stroke-linejoin=\"round\">"
         "<rect x=\"1.5\" y=\"1.5\" width=\"21\" height=\"21\" rx=\"2.2\" fill=\"#1E9A4A\"/>"
         "<g transform=\"translate(4 4) scale(0.667)\">"
         "<path d=\"M3 16c2.5-3.5 5.5-5 9-5s6.5 1.5 9 5c-2.5 3.5-5.5 5-9 5s-6.5-1.5-9-5z\" fill=\"none\" stroke=\"#FFFFFF\" stroke-width=\"2.4\"/>"
         "<circle cx=\"12\" cy=\"16\" r=\"2.2\" fill=\"#FFFFFF\"/>"
         "<path d=\"M8 9c0-2 1-3.5 2-4.5M16 9c0-2-1-3.5-2-4.5\" fill=\"none\" stroke=\"#FFFFFF\" stroke-width=\"2.4\"/>"
         "</g></g></svg>"},
        // ---- defibrillateur
        {"defibrillateur", "D\xC3\xA9" "fibrillateur", "S\xC3\xA9" "curit\xC3\xA9 et signalisation",
         "secours, d\xC3\xA9" "fibrillateur, c\xC5\x93ur, DAE",
         false, false, 64, 64,
         "<svg xmlns=\"http://www.w3.org/2000/svg\" width=\"64\" height=\"64\" viewBox=\"0 0 24 24\">"
         "<g stroke-linecap=\"round\" stroke-linejoin=\"round\">"
         "<rect x=\"1.5\" y=\"1.5\" width=\"21\" height=\"21\" rx=\"2.2\" fill=\"#1E9A4A\"/>"
         "<g transform=\"translate(4 4) scale(0.667)\">"
         "<path d=\"M12 20.5s-8.5-5.2-8.5-11.2a4.5 4.5 0 0 1 8.5-2 4.5 4.5 0 0 1 8.5 2c0 6-8.5 11.2-8.5 11.2zM12.8 8.5l-3 5h2.6l-1 4.5 3.6-6h-2.7l.8-3.5z\" fill=\"#FFFFFF\" fill-rule=\"evenodd\"/>"
         "</g></g></svg>"},
        // ---- telephone-urgence
        {"telephone-urgence", "T\xC3\xA9l\xC3\xA9phone d'urgence", "S\xC3\xA9" "curit\xC3\xA9 et signalisation",
         "secours, t\xC3\xA9l\xC3\xA9phone, urgence, appel",
         false, false, 64, 64,
         "<svg xmlns=\"http://www.w3.org/2000/svg\" width=\"64\" height=\"64\" viewBox=\"0 0 24 24\">"
         "<g stroke-linecap=\"round\" stroke-linejoin=\"round\">"
         "<rect x=\"1.5\" y=\"1.5\" width=\"21\" height=\"21\" rx=\"2.2\" fill=\"#1E9A4A\"/>"
         "<g transform=\"translate(4 4) scale(0.667)\">"
         "<path d=\"M6.5 3.5h3l1.5 4.5-2 1.5a10.5 10.5 0 0 0 5.5 5.5l1.5-2 4.5 1.5v3a2 2 0 0 1-2 2A15.5 15.5 0 0 1 4.5 5.5a2 2 0 0 1 2-2z\" fill=\"none\" stroke=\"#FFFFFF\" stroke-width=\"2.4\"/>"
         "</g></g></svg>"},
        // ---- extincteur
        {"extincteur", "Extincteur", "S\xC3\xA9" "curit\xC3\xA9 et signalisation",
         "incendie, extincteur, feu",
         false, false, 64, 64,
         "<svg xmlns=\"http://www.w3.org/2000/svg\" width=\"64\" height=\"64\" viewBox=\"0 0 24 24\">"
         "<g stroke-linecap=\"round\" stroke-linejoin=\"round\">"
         "<rect x=\"1.5\" y=\"1.5\" width=\"21\" height=\"21\" rx=\"2.2\" fill=\"#D93A2B\"/>"
         "<g transform=\"translate(4 4) scale(0.667)\"><path d=\"M9 9a3 3 0 0 1 6 0v12H9z\" fill=\"#FFFFFF\"/>"
         "<path d=\"M12 6V3.5M10 3.5h4.5\" fill=\"none\" stroke=\"#FFFFFF\" stroke-width=\"2.4\"/>"
         "<path d=\"M14.5 4.5c2.5 0 4 1.2 4.5 3.5l.5 3\" fill=\"none\" stroke=\"#FFFFFF\" stroke-width=\"2.4\"/></g>"
         "</g></svg>"},
        // ---- alarme-incendie
        {"alarme-incendie", "Alarme incendie", "S\xC3\xA9" "curit\xC3\xA9 et signalisation",
         "incendie, alarme, cloche, \xC3\xA9vacuation",
         false, false, 64, 64,
         "<svg xmlns=\"http://www.w3.org/2000/svg\" width=\"64\" height=\"64\" viewBox=\"0 0 24 24\">"
         "<g stroke-linecap=\"round\" stroke-linejoin=\"round\">"
         "<rect x=\"1.5\" y=\"1.5\" width=\"21\" height=\"21\" rx=\"2.2\" fill=\"#D93A2B\"/>"
         "<g transform=\"translate(4 4) scale(0.667)\">"
         "<path d=\"M6 16.5V11a6 6 0 0 1 12 0v5.5l1.5 2h-15z\" fill=\"none\" stroke=\"#FFFFFF\" stroke-width=\"2.4\"/>"
         "<path d=\"M10 21h4M12 3v2\" fill=\"none\" stroke=\"#FFFFFF\" stroke-width=\"2.4\"/></g></g></svg>"},
        // ---- robinet-incendie
        {"robinet-incendie", "Robinet d'incendie arm\xC3\xA9", "S\xC3\xA9" "curit\xC3\xA9 et signalisation",
         "incendie, RIA, robinet, lance, tuyau",
         false, false, 64, 64,
         "<svg xmlns=\"http://www.w3.org/2000/svg\" width=\"64\" height=\"64\" viewBox=\"0 0 24 24\">"
         "<g stroke-linecap=\"round\" stroke-linejoin=\"round\">"
         "<rect x=\"1.5\" y=\"1.5\" width=\"21\" height=\"21\" rx=\"2.2\" fill=\"#D93A2B\"/>"
         "<g transform=\"translate(4 4) scale(0.667)\">"
         "<circle cx=\"10\" cy=\"12\" r=\"7\" fill=\"none\" stroke=\"#FFFFFF\" stroke-width=\"2.4\"/>"
         "<circle cx=\"10\" cy=\"12\" r=\"3.8\" fill=\"none\" stroke=\"#FFFFFF\" stroke-width=\"2.4\"/>"
         "<circle cx=\"10\" cy=\"12\" r=\"1.5\" fill=\"#FFFFFF\"/>"
         "<path d=\"M17 12h4v6.5\" fill=\"none\" stroke=\"#FFFFFF\" stroke-width=\"2.4\"/></g></g></svg>"},
        // ---- arret-urgence
        {"arret-urgence", "Arr\xC3\xAAt d'urgence", "S\xC3\xA9" "curit\xC3\xA9 et signalisation",
         "arr\xC3\xAAt d'urgence, AU, coup de poing, urgence, bouton",
         false, false, 64, 64,
         "<svg xmlns=\"http://www.w3.org/2000/svg\" width=\"64\" height=\"64\" viewBox=\"0 0 24 24\">"
         "<g stroke-linecap=\"round\" stroke-linejoin=\"round\">"
         "<circle cx=\"12\" cy=\"12\" r=\"10.5\" fill=\"#F5C400\"/>"
         "<circle cx=\"12\" cy=\"12\" r=\"10.5\" fill=\"none\" stroke=\"#1B1B1B\" stroke-width=\"1\"/>"
         "<circle cx=\"12\" cy=\"12.6\" r=\"6.9\" fill=\"#9E241A\"/>"
         "<circle cx=\"12\" cy=\"11.6\" r=\"6.4\" fill=\"#D93A2B\"/>"
         "<ellipse cx=\"10\" cy=\"9.4\" rx=\"2.6\" ry=\"1.5\" fill=\"#FFFFFF\" opacity=\"0.35\"/></g></svg>"},
        // ---- feu-tricolore
        {"feu-tricolore", "Feu tricolore", "S\xC3\xA9" "curit\xC3\xA9 et signalisation",
         "feu, tricolore, signalisation, rouge, vert, orange",
         false, false, 64, 64,
         "<svg xmlns=\"http://www.w3.org/2000/svg\" width=\"64\" height=\"64\" viewBox=\"0 0 24 24\">"
         "<g stroke-linecap=\"round\" stroke-linejoin=\"round\">"
         "<rect x=\"7\" y=\"2\" width=\"10\" height=\"20\" rx=\"3\" fill=\"#5B6576\"/>"
         "<circle cx=\"12\" cy=\"6.6\" r=\"2.4\" fill=\"#D93A2B\"/>"
         "<circle cx=\"12\" cy=\"12\" r=\"2.4\" fill=\"#EE8A1F\"/>"
         "<circle cx=\"12\" cy=\"17.4\" r=\"2.4\" fill=\"#1E9A4A\"/></g></svg>"},
        // ---- gyrophare
        {"gyrophare", "Gyrophare", "S\xC3\xA9" "curit\xC3\xA9 et signalisation",
         "gyrophare, balise, alarme, lumi\xC3\xA8re, flash",
         false, false, 64, 64,
         "<svg xmlns=\"http://www.w3.org/2000/svg\" width=\"64\" height=\"64\" viewBox=\"0 0 24 24\">"
         "<g stroke-linecap=\"round\" stroke-linejoin=\"round\">"
         "<rect x=\"6\" y=\"18\" width=\"12\" height=\"3\" rx=\"0.8\" fill=\"#5B6576\"/>"
         "<path d=\"M7 18v-5a5 5 0 0 1 10 0v5z\" fill=\"#EE8A1F\"/>"
         "<ellipse cx=\"10.3\" cy=\"12.6\" rx=\"1.2\" ry=\"2.2\" fill=\"#FFFFFF\" opacity=\"0.45\"/>"
         "<path d=\"M12 2.5v2.5M4.6 6.4l1.6 1.4M19.4 6.4l-1.6 1.4M2.8 13h2.2M19 13h2.2\" fill=\"none\" stroke=\"#EE8A1F\" stroke-width=\"1.6\"/>"
         "</g></svg>"},
        // ---- cone-chantier
        {"cone-chantier", "C\xC3\xB4ne de chantier", "S\xC3\xA9" "curit\xC3\xA9 et signalisation",
         "c\xC3\xB4ne, chantier, travaux, balisage",
         false, false, 64, 64,
         "<svg xmlns=\"http://www.w3.org/2000/svg\" width=\"64\" height=\"64\" viewBox=\"0 0 24 24\">"
         "<g stroke-linecap=\"round\" stroke-linejoin=\"round\"><path d=\"M10 4h4l4.5 15h-13z\" fill=\"#EE8A1F\"/>"
         "<path d=\"M8.35 9.5h7.3l.78 2.6H7.57z\" fill=\"#FFFFFF\"/>"
         "<path d=\"M6.85 14.5h10.3l.78 2.6H6.07z\" fill=\"#FFFFFF\"/>"
         "<rect x=\"3.5\" y=\"19\" width=\"17\" height=\"2.5\" rx=\"0.8\" fill=\"#5B6576\"/></g></svg>"},
        // ---- usine
        {"usine", "Usine", "Industrie et process",
         "usine, site, atelier, production, b\xC3\xA2timent",
         true, true, 64, 64,
         "<svg xmlns=\"http://www.w3.org/2000/svg\" width=\"64\" height=\"64\" viewBox=\"0 0 24 24\">"
         "<g stroke-linecap=\"round\" stroke-linejoin=\"round\">"
         "<path d=\"M2.5 20.5h19\" fill=\"none\" stroke=\"#E6EAF0\" stroke-width=\"2\"/>"
         "<path d=\"M4 20.5V10.5l5 3v-3l5 3v-3l3 2V4h3v16.5\" fill=\"none\" stroke=\"#E6EAF0\" stroke-width=\"2\"/>"
         "<rect x=\"6.3\" y=\"15.5\" width=\"2\" height=\"2\" rx=\"0.3\" fill=\"#E6EAF0\"/>"
         "<rect x=\"10.8\" y=\"15.5\" width=\"2\" height=\"2\" rx=\"0.3\" fill=\"#E6EAF0\"/>"
         "<rect x=\"15.3\" y=\"15.5\" width=\"2\" height=\"2\" rx=\"0.3\" fill=\"#E6EAF0\"/></g></svg>"},
        // ---- moteur
        {"moteur", "Moteur", "Industrie et process",
         "moteur, \xC3\xA9lectrique, entra\xC3\xAEnement",
         true, true, 64, 64,
         "<svg xmlns=\"http://www.w3.org/2000/svg\" width=\"64\" height=\"64\" viewBox=\"0 0 24 24\">"
         "<g stroke-linecap=\"round\" stroke-linejoin=\"round\">"
         "<rect x=\"2.5\" y=\"6\" width=\"14\" height=\"11.5\" rx=\"2.5\" fill=\"none\" stroke=\"#E6EAF0\" stroke-width=\"2\"/>"
         "<path d=\"M6 9.5h7M6 12h7M6 14.5h7\" fill=\"none\" stroke=\"#E6EAF0\" stroke-width=\"1.4\"/>"
         "<path d=\"M16.5 9.5h1.5v5h-1.5M18 12h3.5\" fill=\"none\" stroke=\"#E6EAF0\" stroke-width=\"2\"/>"
         "<path d=\"M5 17.5l-1 3h11l-1-3\" fill=\"none\" stroke=\"#E6EAF0\" stroke-width=\"2\"/></g></svg>"},
        // ---- moteur-symbole
        {"moteur-symbole", "Moteur (symbole)", "Industrie et process",
         "moteur, M, symbole, sch\xC3\xA9ma",
         true, true, 64, 64,
         "<svg xmlns=\"http://www.w3.org/2000/svg\" width=\"64\" height=\"64\" viewBox=\"0 0 24 24\">"
         "<g stroke-linecap=\"round\" stroke-linejoin=\"round\">"
         "<circle cx=\"12\" cy=\"12\" r=\"9.5\" fill=\"none\" stroke=\"#E6EAF0\" stroke-width=\"2\"/>"
         "<polyline points=\"7.8 16 7.8 8.2 12 13 16.2 8.2 16.2 16\" fill=\"none\" stroke=\"#E6EAF0\" stroke-width=\"2\"/>"
         "</g></svg>"},
        // ---- pompe
        {"pompe", "Pompe", "Industrie et process",
         "pompe, refoulement, centrifuge, fluide",
         true, true, 64, 64,
         "<svg xmlns=\"http://www.w3.org/2000/svg\" width=\"64\" height=\"64\" viewBox=\"0 0 24 24\">"
         "<g stroke-linecap=\"round\" stroke-linejoin=\"round\">"
         "<circle cx=\"11\" cy=\"12\" r=\"7.5\" fill=\"none\" stroke=\"#E6EAF0\" stroke-width=\"2\"/>"
         "<path d=\"M11 4.5h10.5M21.5 2.5v4\" fill=\"none\" stroke=\"#E6EAF0\" stroke-width=\"2\"/>"
         "<polygon points=\"8 8.5 8 15.5 17 12\" fill=\"none\" stroke=\"#E6EAF0\" stroke-width=\"2\"/>"
         "<path d=\"M6.5 21.5h9M8 18.8l-1.2 2.7M14 18.8l1.2 2.7\" fill=\"none\" stroke=\"#E6EAF0\" stroke-width=\"2\"/>"
         "</g></svg>"},
        // ---- ventilateur
        {"ventilateur", "Ventilateur", "Industrie et process",
         "ventilateur, h\xC3\xA9lice, air, extraction",
         true, true, 64, 64,
         "<svg xmlns=\"http://www.w3.org/2000/svg\" width=\"64\" height=\"64\" viewBox=\"0 0 24 24\">"
         "<g stroke-linecap=\"round\" stroke-linejoin=\"round\">"
         "<circle cx=\"12\" cy=\"12\" r=\"9.5\" fill=\"none\" stroke=\"#E6EAF0\" stroke-width=\"2\"/>"
         "<g transform=\"rotate(0 12 12)\">"
         "<path d=\"M12 12c-1-3.6-.4-6.6 3-7.4 2.1 1.2 1.5 4.5-3 7.4z\" fill=\"#E6EAF0\"/></g>"
         "<g transform=\"rotate(120 12 12)\">"
         "<path d=\"M12 12c-1-3.6-.4-6.6 3-7.4 2.1 1.2 1.5 4.5-3 7.4z\" fill=\"#E6EAF0\"/></g>"
         "<g transform=\"rotate(240 12 12)\">"
         "<path d=\"M12 12c-1-3.6-.4-6.6 3-7.4 2.1 1.2 1.5 4.5-3 7.4z\" fill=\"#E6EAF0\"/></g>"
         "<circle cx=\"12\" cy=\"12\" r=\"1.8\" fill=\"#E6EAF0\"/></g></svg>"},
        // ---- compresseur
        {"compresseur", "Compresseur", "Industrie et process",
         "compresseur, air, pression",
         true, true, 64, 64,
         "<svg xmlns=\"http://www.w3.org/2000/svg\" width=\"64\" height=\"64\" viewBox=\"0 0 24 24\">"
         "<g stroke-linecap=\"round\" stroke-linejoin=\"round\">"
         "<circle cx=\"12\" cy=\"12\" r=\"9.5\" fill=\"none\" stroke=\"#E6EAF0\" stroke-width=\"2\"/>"
         "<path d=\"M5 7.2l14 3M5 16.8l14-3\" fill=\"none\" stroke=\"#E6EAF0\" stroke-width=\"2\"/></g></svg>"},
        // ---- convoyeur
        {"convoyeur", "Convoyeur", "Industrie et process",
         "convoyeur, tapis, transport, bande",
         true, true, 64, 64,
         "<svg xmlns=\"http://www.w3.org/2000/svg\" width=\"64\" height=\"64\" viewBox=\"0 0 24 24\">"
         "<g stroke-linecap=\"round\" stroke-linejoin=\"round\">"
         "<rect x=\"2.5\" y=\"14\" width=\"19\" height=\"6\" rx=\"3\" fill=\"none\" stroke=\"#E6EAF0\" stroke-width=\"2\"/>"
         "<circle cx=\"5.5\" cy=\"17\" r=\"1\" fill=\"#E6EAF0\"/>"
         "<circle cx=\"12\" cy=\"17\" r=\"1\" fill=\"#E6EAF0\"/>"
         "<circle cx=\"18.5\" cy=\"17\" r=\"1\" fill=\"#E6EAF0\"/>"
         "<rect x=\"4.5\" y=\"6\" width=\"6.5\" height=\"6.5\" rx=\"0.8\" fill=\"#E6EAF0\"/>"
         "<rect x=\"13\" y=\"8.5\" width=\"5\" height=\"4\" rx=\"0.8\" fill=\"#E6EAF0\"/></g></svg>"},
        // ---- robot
        {"robot", "Robot", "Industrie et process",
         "robot, bras, manipulateur, automatisation",
         true, true, 64, 64,
         "<svg xmlns=\"http://www.w3.org/2000/svg\" width=\"64\" height=\"64\" viewBox=\"0 0 24 24\">"
         "<g stroke-linecap=\"round\" stroke-linejoin=\"round\">"
         "<path d=\"M4.5 20.5h9\" fill=\"none\" stroke=\"#E6EAF0\" stroke-width=\"2\"/>"
         "<path d=\"M6 20.5l1-3h4l1 3\" fill=\"none\" stroke=\"#E6EAF0\" stroke-width=\"2\"/>"
         "<path d=\"M9 15.5l3.5-7\" fill=\"none\" stroke=\"#E6EAF0\" stroke-width=\"2\"/>"
         "<path d=\"M12.5 8.5l5.5 2.5\" fill=\"none\" stroke=\"#E6EAF0\" stroke-width=\"2\"/>"
         "<path d=\"M18 11l2.5-2.5M18 11l1.3 3.3\" fill=\"none\" stroke=\"#E6EAF0\" stroke-width=\"2\"/>"
         "<circle cx=\"9\" cy=\"15.5\" r=\"1.8\" fill=\"#E6EAF0\"/>"
         "<circle cx=\"12.5\" cy=\"8.5\" r=\"1.8\" fill=\"#E6EAF0\"/></g></svg>"},
        // ---- engrenage
        {"engrenage", "Engrenage", "Industrie et process",
         "engrenage, m\xC3\xA9" "canique, roue dent\xC3\xA9" "e, r\xC3\xA9glage",
         true, true, 64, 64,
         "<svg xmlns=\"http://www.w3.org/2000/svg\" width=\"64\" height=\"64\" viewBox=\"0 0 24 24\">"
         "<g stroke-linecap=\"round\" stroke-linejoin=\"round\">"
         "<path d=\"M10.647 4.908 L10.928 2.561 L13.072 2.561 L13.353 4.908 L15.074 5.467 L16.681 3.733 L18.416 4.994 L17.263 7.058 L18.327 8.522 L20.646 8.063 L21.309 10.103 L19.163 11.095 L19.163 12.905 L21.309 13.897 L20.646 15.937 L18.327 15.478 L17.263 16.942 L18.416 19.006 L16.681 20.267 L15.074 18.533 L13.353 19.092 L13.072 21.439 L10.928 21.439 L10.647 19.092 L8.926 18.533 L7.319 20.267 L5.584 19.006 L6.737 16.942 L5.673 15.478 L3.354 15.937 L2.691 13.897 L4.837 12.905 L4.837 11.095 L2.691 10.103 L3.354 8.063 L5.673 8.522 L6.737 7.058 L5.584 4.994 L7.319 3.733 L8.926 5.467Z\" fill=\"none\" stroke=\"#E6EAF0\" stroke-width=\"2\"/>"
         "<circle cx=\"12\" cy=\"12\" r=\"3\" fill=\"none\" stroke=\"#E6EAF0\" stroke-width=\"2\"/></g></svg>"},
        // ---- engrenages
        {"engrenages", "Engrenages", "Industrie et process",
         "engrenages, m\xC3\xA9" "canique, transmission, maintenance",
         true, true, 64, 64,
         "<svg xmlns=\"http://www.w3.org/2000/svg\" width=\"64\" height=\"64\" viewBox=\"0 0 24 24\">"
         "<g stroke-linecap=\"round\" stroke-linejoin=\"round\">"
         "<path d=\"M7.847 4.196 L8.084 2.565 L9.916 2.565 L10.153 4.196 L11.581 4.788 L12.903 3.802 L14.198 5.097 L13.212 6.419 L13.804 7.847 L15.435 8.084 L15.435 9.916 L13.804 10.153 L13.212 11.581 L14.198 12.903 L12.903 14.198 L11.581 13.212 L10.153 13.804 L9.916 15.435 L8.084 15.435 L7.847 13.804 L6.419 13.212 L5.097 14.198 L3.802 12.903 L4.788 11.581 L4.196 10.153 L2.565 9.916 L2.565 8.084 L4.196 7.847 L4.788 6.419 L3.802 5.097 L5.097 3.802 L6.419 4.788Z\" fill=\"none\" stroke=\"#E6EAF0\" stroke-width=\"2\"/>"
         "<circle cx=\"9\" cy=\"9\" r=\"2\" fill=\"none\" stroke=\"#E6EAF0\" stroke-width=\"2\"/>"
         "<path d=\"M15.589 12.937 L15.796 11.665 L17.404 11.665 L17.611 12.937 L18.834 13.526 L19.957 12.894 L20.96 14.152 L20.094 15.107 L20.396 16.43 L21.59 16.914 L21.232 18.482 L19.946 18.401 L19.1 19.462 L19.466 20.697 L18.016 21.395 L17.279 20.339 L15.921 20.339 L15.184 21.395 L13.734 20.697 L14.1 19.462 L13.254 18.401 L11.968 18.482 L11.61 16.914 L12.804 16.43 L13.106 15.107 L12.24 14.152 L13.243 12.894 L14.366 13.526Z\" fill=\"none\" stroke=\"#E6EAF0\" stroke-width=\"2\"/>"
         "<circle cx=\"16.6\" cy=\"16.6\" r=\"1.6\" fill=\"none\" stroke=\"#E6EAF0\" stroke-width=\"2\"/></g></svg>"},
        // ---- marteau
        {"marteau", "Outil", "Industrie et process",
         "outil, marteau, maintenance, r\xC3\xA9paration",
         true, true, 64, 64,
         "<svg xmlns=\"http://www.w3.org/2000/svg\" width=\"64\" height=\"64\" viewBox=\"0 0 24 24\">"
         "<g stroke-linecap=\"round\" stroke-linejoin=\"round\">"
         "<path d=\"M13.5 4.5l6 6-2.5 2.5-6-6z\" fill=\"none\" stroke=\"#E6EAF0\" stroke-width=\"2\"/>"
         "<path d=\"M12.5 9.5L4.5 17.5a1.6 1.6 0 0 0 2.3 2.3l8-8\" fill=\"none\" stroke=\"#E6EAF0\" stroke-width=\"2\"/>"
         "</g></svg>"},
        // ---- cle-plate
        {"cle-plate", "Cl\xC3\xA9 plate", "Industrie et process",
         "cl\xC3\xA9, outil, maintenance, r\xC3\xA9glage, r\xC3\xA9paration",
         true, true, 64, 64,
         "<svg xmlns=\"http://www.w3.org/2000/svg\" width=\"64\" height=\"64\" viewBox=\"0 0 24 24\">"
         "<g stroke-linecap=\"round\" stroke-linejoin=\"round\">"
         "<path d=\"M15.5 3.7a5 5 0 0 0-5.6 6.7L3.8 16.5a1.9 1.9 0 0 0 2.7 2.7l6.1-6.1a5 5 0 0 0 6.7-5.6l-2.9 2.9-2.6-.4-.4-2.6z\" fill=\"none\" stroke=\"#E6EAF0\" stroke-width=\"2\"/>"
         "</g></svg>"},
        // ---- tournevis
        {"tournevis", "Tournevis", "Industrie et process",
         "tournevis, outil, maintenance",
         true, true, 64, 64,
         "<svg xmlns=\"http://www.w3.org/2000/svg\" width=\"64\" height=\"64\" viewBox=\"0 0 24 24\">"
         "<g stroke-linecap=\"round\" stroke-linejoin=\"round\"><g transform=\"rotate(45 12 12)\">"
         "<rect x=\"9.6\" y=\"1.5\" width=\"4.8\" height=\"9\" rx=\"2\" fill=\"#E6EAF0\"/></g>"
         "<g transform=\"rotate(45 12 12)\">"
         "<path d=\"M12 10.5v8.5\" fill=\"none\" stroke=\"#E6EAF0\" stroke-width=\"2.2\"/></g>"
         "<g transform=\"rotate(45 12 12)\">"
         "<path d=\"M10.8 20.2h2.4\" fill=\"none\" stroke=\"#E6EAF0\" stroke-width=\"2\"/></g></g></svg>"},
        // ---- vanne
        {"vanne", "Vanne", "Industrie et process",
         "vanne, robinet, fluide, ouvrir, fermer",
         true, true, 64, 64,
         "<svg xmlns=\"http://www.w3.org/2000/svg\" width=\"64\" height=\"64\" viewBox=\"0 0 24 24\">"
         "<g stroke-linecap=\"round\" stroke-linejoin=\"round\">"
         "<path d=\"M3 8v8l18-8v8z\" fill=\"none\" stroke=\"#E6EAF0\" stroke-width=\"2\"/>"
         "<path d=\"M12 12V5\" fill=\"none\" stroke=\"#E6EAF0\" stroke-width=\"2\"/>"
         "<path d=\"M8.5 5h7\" fill=\"none\" stroke=\"#E6EAF0\" stroke-width=\"2\"/></g></svg>"},
        // ---- vanne-manuelle
        {"vanne-manuelle", "Vanne manuelle", "Industrie et process",
         "vanne, manuelle, volant, robinet",
         true, true, 64, 64,
         "<svg xmlns=\"http://www.w3.org/2000/svg\" width=\"64\" height=\"64\" viewBox=\"0 0 24 24\">"
         "<g stroke-linecap=\"round\" stroke-linejoin=\"round\">"
         "<path d=\"M3 10v8l18-8v8z\" fill=\"none\" stroke=\"#E6EAF0\" stroke-width=\"2\"/>"
         "<path d=\"M12 14V6.5\" fill=\"none\" stroke=\"#E6EAF0\" stroke-width=\"2\"/>"
         "<ellipse cx=\"12\" cy=\"5\" rx=\"5\" ry=\"1.8\" fill=\"none\" stroke=\"#E6EAF0\" stroke-width=\"2\"/></g>"
         "</svg>"},
        // ---- vanne-3-voies
        {"vanne-3-voies", "Vanne 3 voies", "Industrie et process",
         "vanne, 3 voies, m\xC3\xA9lange, d\xC3\xA9rivation",
         true, true, 64, 64,
         "<svg xmlns=\"http://www.w3.org/2000/svg\" width=\"64\" height=\"64\" viewBox=\"0 0 24 24\">"
         "<g stroke-linecap=\"round\" stroke-linejoin=\"round\">"
         "<polygon points=\"3 6 3 14 12 10\" fill=\"none\" stroke=\"#E6EAF0\" stroke-width=\"2\"/>"
         "<polygon points=\"21 6 21 14 12 10\" fill=\"none\" stroke=\"#E6EAF0\" stroke-width=\"2\"/>"
         "<polygon points=\"8 19.5 16 19.5 12 10\" fill=\"none\" stroke=\"#E6EAF0\" stroke-width=\"2\"/>"
         "<path d=\"M12 10V3.5M9 3.5h6\" fill=\"none\" stroke=\"#E6EAF0\" stroke-width=\"2\"/></g></svg>"},
        // ---- cuve
        {"cuve", "Cuve", "Industrie et process",
         "cuve, r\xC3\xA9servoir, stockage, citerne",
         true, true, 64, 64,
         "<svg xmlns=\"http://www.w3.org/2000/svg\" width=\"64\" height=\"64\" viewBox=\"0 0 24 24\">"
         "<g stroke-linecap=\"round\" stroke-linejoin=\"round\">"
         "<path d=\"M5 5.5c0-1.6 14-1.6 14 0v13c0 1.6-14 1.6-14 0z\" fill=\"none\" stroke=\"#E6EAF0\" stroke-width=\"2\"/>"
         "<path d=\"M5 5.5c0 1.6 14 1.6 14 0\" fill=\"none\" stroke=\"#E6EAF0\" stroke-width=\"2\"/>"
         "<path d=\"M5 13c0 1.4 14 1.4 14 0\" fill=\"none\" stroke=\"#E6EAF0\" stroke-width=\"1.5\" opacity=\"0.6\"/>"
         "</g></svg>"},
        // ---- silo
        {"silo", "Silo", "Industrie et process",
         "silo, stockage, vrac, tr\xC3\xA9mie",
         true, true, 64, 64,
         "<svg xmlns=\"http://www.w3.org/2000/svg\" width=\"64\" height=\"64\" viewBox=\"0 0 24 24\">"
         "<g stroke-linecap=\"round\" stroke-linejoin=\"round\">"
         "<path d=\"M6 3.5h12v10.5l-4 4.5v3h-4v-3l-4-4.5z\" fill=\"none\" stroke=\"#E6EAF0\" stroke-width=\"2\"/>"
         "<path d=\"M6 9h12\" fill=\"none\" stroke=\"#E6EAF0\" stroke-width=\"1.5\" opacity=\"0.6\"/>"
         "<path d=\"M7 14.5v6.5M17 14.5v6.5\" fill=\"none\" stroke=\"#E6EAF0\" stroke-width=\"2\"/></g></svg>"},
        // ---- chaudiere
        {"chaudiere", "Chaudi\xC3\xA8re", "Industrie et process",
         "chaudi\xC3\xA8re, chauffage, br\xC3\xBBleur, flamme",
         true, true, 64, 64,
         "<svg xmlns=\"http://www.w3.org/2000/svg\" width=\"64\" height=\"64\" viewBox=\"0 0 24 24\">"
         "<g stroke-linecap=\"round\" stroke-linejoin=\"round\">"
         "<rect x=\"4.5\" y=\"3\" width=\"15\" height=\"18\" rx=\"2\" fill=\"none\" stroke=\"#E6EAF0\" stroke-width=\"2\"/>"
         "<path d=\"M4.5 8h15\" fill=\"none\" stroke=\"#E6EAF0\" stroke-width=\"2\"/>"
         "<path d=\"M12 18.5c-2 0-3.3-1.3-3.3-3 0-1.6 1.2-2.6 2-3.4.2 1 .8 1.6 1.4 1.8.2-1.6 1-3 2.2-3.9.2 1.5 1 2.6 1 4.5 0 2.4-1.4 4-3.3 4z\" fill=\"#E6EAF0\"/>"
         "<circle cx=\"8\" cy=\"5.5\" r=\"0.9\" fill=\"#E6EAF0\"/>"
         "<circle cx=\"11\" cy=\"5.5\" r=\"0.9\" fill=\"#E6EAF0\"/></g></svg>"},
        // ---- echangeur
        {"echangeur", "\xC3\x89" "changeur", "Industrie et process",
         "\xC3\xA9" "changeur, chaleur, thermique, serpentin",
         true, true, 64, 64,
         "<svg xmlns=\"http://www.w3.org/2000/svg\" width=\"64\" height=\"64\" viewBox=\"0 0 24 24\">"
         "<g stroke-linecap=\"round\" stroke-linejoin=\"round\">"
         "<circle cx=\"12\" cy=\"12\" r=\"9.5\" fill=\"none\" stroke=\"#E6EAF0\" stroke-width=\"2\"/>"
         "<polyline points=\"2.5 12 6.5 12 8.5 8 11 16 13.5 8 15.5 16 17.5 12 21.5 12\" fill=\"none\" stroke=\"#E6EAF0\" stroke-width=\"2\"/>"
         "</g></svg>"},
        // ---- filtre-process
        {"filtre-process", "Filtre", "Industrie et process",
         "filtre, filtration, process, tamis",
         true, true, 64, 64,
         "<svg xmlns=\"http://www.w3.org/2000/svg\" width=\"64\" height=\"64\" viewBox=\"0 0 24 24\">"
         "<g stroke-linecap=\"round\" stroke-linejoin=\"round\">"
         "<path d=\"M12 2.5l9.5 9.5-9.5 9.5L2.5 12z\" fill=\"none\" stroke=\"#E6EAF0\" stroke-width=\"2\"/>"
         "<path d=\"M6 12h12\" fill=\"none\" stroke=\"#E6EAF0\" stroke-width=\"2\" stroke-dasharray=\"2 2\" stroke-linecap=\"butt\"/>"
         "</g></svg>"},
        // ---- capteur
        {"capteur", "Capteur", "Industrie et process",
         "capteur, d\xC3\xA9tecteur, proximit\xC3\xA9, sonde",
         true, true, 64, 64,
         "<svg xmlns=\"http://www.w3.org/2000/svg\" width=\"64\" height=\"64\" viewBox=\"0 0 24 24\">"
         "<g stroke-linecap=\"round\" stroke-linejoin=\"round\">"
         "<rect x=\"2.5\" y=\"9\" width=\"10\" height=\"6\" rx=\"1.5\" fill=\"none\" stroke=\"#E6EAF0\" stroke-width=\"2\"/>"
         "<path d=\"M12.5 12h2\" fill=\"none\" stroke=\"#E6EAF0\" stroke-width=\"2\"/>"
         "<path d=\"M16.5 9a4 4 0 0 1 0 6M19.5 7a7 7 0 0 1 0 10\" fill=\"none\" stroke=\"#E6EAF0\" stroke-width=\"2\"/>"
         "</g></svg>"},
        // ---- thermometre
        {"thermometre", "Thermom\xC3\xA8tre", "Industrie et process",
         "thermom\xC3\xA8tre, temp\xC3\xA9rature, sonde",
         true, true, 64, 64,
         "<svg xmlns=\"http://www.w3.org/2000/svg\" width=\"64\" height=\"64\" viewBox=\"0 0 24 24\">"
         "<g stroke-linecap=\"round\" stroke-linejoin=\"round\">"
         "<path d=\"M10 14.3V5a2 2 0 0 1 4 0v9.3a4 4 0 1 1-4 0z\" fill=\"none\" stroke=\"#E6EAF0\" stroke-width=\"2\"/>"
         "<circle cx=\"12\" cy=\"17.5\" r=\"1.9\" fill=\"#E6EAF0\"/>"
         "<path d=\"M12 17.5V9.5\" fill=\"none\" stroke=\"#E6EAF0\" stroke-width=\"1.6\"/></g></svg>"},
        // ---- manometre
        {"manometre", "Manom\xC3\xA8tre", "Industrie et process",
         "manom\xC3\xA8tre, pression, mesure, cadran",
         true, true, 64, 64,
         "<svg xmlns=\"http://www.w3.org/2000/svg\" width=\"64\" height=\"64\" viewBox=\"0 0 24 24\">"
         "<g stroke-linecap=\"round\" stroke-linejoin=\"round\">"
         "<circle cx=\"12\" cy=\"10.5\" r=\"7.5\" fill=\"none\" stroke=\"#E6EAF0\" stroke-width=\"2\"/>"
         "<path d=\"M12 10.5l3.6-3.2\" fill=\"none\" stroke=\"#E6EAF0\" stroke-width=\"2\"/>"
         "<circle cx=\"12\" cy=\"10.5\" r=\"1.4\" fill=\"#E6EAF0\"/>"
         "<path d=\"M12 18v3.5M9 21.5h6\" fill=\"none\" stroke=\"#E6EAF0\" stroke-width=\"2\"/>"
         "<path d=\"M7.5 13.6l1-.6M16.5 13.6l-1-.6M12 4.6v1.2\" fill=\"none\" stroke=\"#E6EAF0\" stroke-width=\"1.5\"/>"
         "</g></svg>"},
        // ---- debitmetre
        {"debitmetre", "D\xC3\xA9" "bitm\xC3\xA8tre", "Industrie et process",
         "d\xC3\xA9" "bitm\xC3\xA8tre, d\xC3\xA9" "bit, compteur, mesure",
         true, true, 64, 64,
         "<svg xmlns=\"http://www.w3.org/2000/svg\" width=\"64\" height=\"64\" viewBox=\"0 0 24 24\">"
         "<g stroke-linecap=\"round\" stroke-linejoin=\"round\">"
         "<path d=\"M2 14h4.5M17.5 14H22\" fill=\"none\" stroke=\"#E6EAF0\" stroke-width=\"2\"/>"
         "<path d=\"M6.5 10.5v7M17.5 10.5v7\" fill=\"none\" stroke=\"#E6EAF0\" stroke-width=\"2\"/>"
         "<circle cx=\"12\" cy=\"14\" r=\"5.5\" fill=\"none\" stroke=\"#E6EAF0\" stroke-width=\"2\"/>"
         "<path d=\"M9.3 14L14.8 14M12.808 15.671L14.8 14L12.808 12.329\" fill=\"none\" stroke=\"#E6EAF0\" stroke-width=\"1.6\"/>"
         "<rect x=\"9.5\" y=\"2.5\" width=\"5\" height=\"3.5\" rx=\"0.8\" fill=\"none\" stroke=\"#E6EAF0\" stroke-width=\"2\"/>"
         "<path d=\"M12 6v2.5\" fill=\"none\" stroke=\"#E6EAF0\" stroke-width=\"2\"/></g></svg>"},
        // ---- niveau
        {"niveau", "Niveau", "Industrie et process",
         "niveau, cuve, remplissage, hauteur",
         true, true, 64, 64,
         "<svg xmlns=\"http://www.w3.org/2000/svg\" width=\"64\" height=\"64\" viewBox=\"0 0 24 24\">"
         "<g stroke-linecap=\"round\" stroke-linejoin=\"round\">"
         "<rect x=\"4\" y=\"3.5\" width=\"11\" height=\"17\" rx=\"2\" fill=\"none\" stroke=\"#E6EAF0\" stroke-width=\"2\"/>"
         "<rect x=\"4\" y=\"12.5\" width=\"11\" height=\"8\" rx=\"2\" fill=\"#E6EAF0\" opacity=\"0.4\"/>"
         "<path d=\"M4 12.5h11\" fill=\"none\" stroke=\"#E6EAF0\" stroke-width=\"2\"/>"
         "<path d=\"M19 12L19 4M20.928 6.298L19 4L17.072 6.298\" fill=\"none\" stroke=\"#E6EAF0\" stroke-width=\"2\"/>"
         "<path d=\"M19 12L19 20M17.072 17.702L19 20L20.928 17.702\" fill=\"none\" stroke=\"#E6EAF0\" stroke-width=\"2\"/>"
         "</g></svg>"},
        // ---- agitateur
        {"agitateur", "Agitateur", "Industrie et process",
         "agitateur, m\xC3\xA9langeur, malaxeur, cuve",
         true, true, 64, 64,
         "<svg xmlns=\"http://www.w3.org/2000/svg\" width=\"64\" height=\"64\" viewBox=\"0 0 24 24\">"
         "<g stroke-linecap=\"round\" stroke-linejoin=\"round\">"
         "<path d=\"M5 7v11.5a2 2 0 0 0 2 2h10a2 2 0 0 0 2-2V7\" fill=\"none\" stroke=\"#E6EAF0\" stroke-width=\"2\"/>"
         "<path d=\"M12 5v10.5\" fill=\"none\" stroke=\"#E6EAF0\" stroke-width=\"2\"/>"
         "<path d=\"M7.5 15.5c1.5 1.2 3 1.2 4.5 0s3-1.2 4.5 0\" fill=\"none\" stroke=\"#E6EAF0\" stroke-width=\"2\"/>"
         "<rect x=\"9.5\" y=\"2\" width=\"5\" height=\"3\" rx=\"0.6\" fill=\"none\" stroke=\"#E6EAF0\" stroke-width=\"2\"/>"
         "</g></svg>"},
        // ---- verin
        {"verin", "V\xC3\xA9rin", "Industrie et process",
         "v\xC3\xA9rin, piston, pneumatique, hydraulique",
         true, true, 64, 64,
         "<svg xmlns=\"http://www.w3.org/2000/svg\" width=\"64\" height=\"64\" viewBox=\"0 0 24 24\">"
         "<g stroke-linecap=\"round\" stroke-linejoin=\"round\">"
         "<rect x=\"2.5\" y=\"7.5\" width=\"12\" height=\"9\" rx=\"1.5\" fill=\"none\" stroke=\"#E6EAF0\" stroke-width=\"2\"/>"
         "<path d=\"M9 7.5v9\" fill=\"none\" stroke=\"#E6EAF0\" stroke-width=\"2\"/>"
         "<path d=\"M9 12h12.5\" fill=\"none\" stroke=\"#E6EAF0\" stroke-width=\"2\"/>"
         "<path d=\"M21.5 9.5v5\" fill=\"none\" stroke=\"#E6EAF0\" stroke-width=\"2\"/></g></svg>"},
        // ---- pesage
        {"pesage", "Pesage", "Industrie et process",
         "pesage, balance, poids, masse",
         true, true, 64, 64,
         "<svg xmlns=\"http://www.w3.org/2000/svg\" width=\"64\" height=\"64\" viewBox=\"0 0 24 24\">"
         "<g stroke-linecap=\"round\" stroke-linejoin=\"round\">"
         "<path d=\"M7.2 9.5h9.6l3 11.5H4.2z\" fill=\"#E6EAF0\"/>"
         "<circle cx=\"12\" cy=\"6\" r=\"2.6\" fill=\"none\" stroke=\"#E6EAF0\" stroke-width=\"2\"/></g></svg>"},
        // ---- four
        {"four", "Four", "Industrie et process",
         "four, cuisson, \xC3\xA9tuve, chaleur",
         true, true, 64, 64,
         "<svg xmlns=\"http://www.w3.org/2000/svg\" width=\"64\" height=\"64\" viewBox=\"0 0 24 24\">"
         "<g stroke-linecap=\"round\" stroke-linejoin=\"round\">"
         "<rect x=\"3\" y=\"4\" width=\"18\" height=\"16.5\" rx=\"2\" fill=\"none\" stroke=\"#E6EAF0\" stroke-width=\"2\"/>"
         "<rect x=\"6\" y=\"9\" width=\"12\" height=\"8.5\" rx=\"1\" fill=\"none\" stroke=\"#E6EAF0\" stroke-width=\"2\"/>"
         "<circle cx=\"6\" cy=\"6.3\" r=\"0.8\" fill=\"#E6EAF0\"/>"
         "<circle cx=\"9\" cy=\"6.3\" r=\"0.8\" fill=\"#E6EAF0\"/>"
         "<path d=\"M9.5 15.5c-.8-1 .8-1.8 0-2.8M12 15.5c-.8-1 .8-1.8 0-2.8M14.5 15.5c-.8-1 .8-1.8 0-2.8\" fill=\"none\" stroke=\"#E6EAF0\" stroke-width=\"1.4\"/>"
         "</g></svg>"},
        // ---- compteur
        {"compteur", "Compteur", "Industrie et process",
         "compteur, totalisateur, comptage, nombre",
         true, true, 64, 64,
         "<svg xmlns=\"http://www.w3.org/2000/svg\" width=\"64\" height=\"64\" viewBox=\"0 0 24 24\">"
         "<g stroke-linecap=\"round\" stroke-linejoin=\"round\">"
         "<rect x=\"2.5\" y=\"6.5\" width=\"19\" height=\"11\" rx=\"2\" fill=\"none\" stroke=\"#E6EAF0\" stroke-width=\"2\"/>"
         "<path d=\"M8.8 6.5v11M15.2 6.5v11\" fill=\"none\" stroke=\"#E6EAF0\" stroke-width=\"1.4\"/>"
         "<g transform=\"translate(3.85 9) scale(0.6)\">"
         "<path d=\"M3 0a3 3 0 0 0-3 3v4a3 3 0 0 0 6 0V3a3 3 0 0 0-3-3z\" fill=\"none\" stroke=\"#E6EAF0\" stroke-width=\"2.5\"/>"
         "</g><g transform=\"translate(10.2 9) scale(0.6)\">"
         "<path d=\"M4.6 10V0L0 7.1h6\" fill=\"none\" stroke=\"#E6EAF0\" stroke-width=\"2.5\"/></g>"
         "<g transform=\"translate(16.55 9) scale(0.6)\">"
         "<path d=\"M0 0h6L2.2 10\" fill=\"none\" stroke=\"#E6EAF0\" stroke-width=\"2.5\"/></g></g></svg>"},
        // ---- armoire-electrique
        {"armoire-electrique", "Armoire \xC3\xA9lectrique", "Industrie et process",
         "armoire, coffret, \xC3\xA9lectrique, tableau",
         true, true, 64, 64,
         "<svg xmlns=\"http://www.w3.org/2000/svg\" width=\"64\" height=\"64\" viewBox=\"0 0 24 24\">"
         "<g stroke-linecap=\"round\" stroke-linejoin=\"round\">"
         "<rect x=\"4.5\" y=\"2.5\" width=\"15\" height=\"19\" rx=\"1.5\" fill=\"none\" stroke=\"#E6EAF0\" stroke-width=\"2\"/>"
         "<path d=\"M12 2.5v19\" fill=\"none\" stroke=\"#E6EAF0\" stroke-width=\"2\"/>"
         "<path d=\"M9.8 10.5v3M14.2 10.5v3\" fill=\"none\" stroke=\"#E6EAF0\" stroke-width=\"2\"/>"
         "<path d=\"M8.6 4.5l-2.3 3.2h1.7l-.6 2.6 2.4-3.3H8.1z\" fill=\"#E6EAF0\"/></g></svg>"},
        // ---- presse
        {"presse", "Presse", "Industrie et process",
         "presse, emboutissage, hydraulique, machine",
         true, true, 64, 64,
         "<svg xmlns=\"http://www.w3.org/2000/svg\" width=\"64\" height=\"64\" viewBox=\"0 0 24 24\">"
         "<g stroke-linecap=\"round\" stroke-linejoin=\"round\">"
         "<path d=\"M4 21V3.5h16V21\" fill=\"none\" stroke=\"#E6EAF0\" stroke-width=\"2\"/>"
         "<path d=\"M2.5 21h19\" fill=\"none\" stroke=\"#E6EAF0\" stroke-width=\"2\"/>"
         "<rect x=\"8\" y=\"6\" width=\"8\" height=\"3.5\" rx=\"0.8\" fill=\"none\" stroke=\"#E6EAF0\" stroke-width=\"2\"/>"
         "<path d=\"M12 9.5V13\" fill=\"none\" stroke=\"#E6EAF0\" stroke-width=\"2\"/>"
         "<rect x=\"7.5\" y=\"13\" width=\"9\" height=\"2.5\" rx=\"0.6\" fill=\"#E6EAF0\"/>"
         "<rect x=\"6.5\" y=\"17.5\" width=\"11\" height=\"2\" rx=\"0.5\" fill=\"#E6EAF0\"/></g></svg>"},
        // ---- tuyauterie
        {"tuyauterie", "Tuyauterie", "Industrie et process",
         "tuyau, coude, canalisation, r\xC3\xA9seau",
         true, true, 64, 64,
         "<svg xmlns=\"http://www.w3.org/2000/svg\" width=\"64\" height=\"64\" viewBox=\"0 0 24 24\">"
         "<g stroke-linecap=\"round\" stroke-linejoin=\"round\">"
         "<path d=\"M2.5 6.5H11a6.5 6.5 0 0 1 6.5 6.5v8.5\" fill=\"none\" stroke=\"#E6EAF0\" stroke-width=\"2\"/>"
         "<path d=\"M2.5 11.5H11a1.5 1.5 0 0 1 1.5 1.5v8.5\" fill=\"none\" stroke=\"#E6EAF0\" stroke-width=\"2\"/>"
         "<path d=\"M2.5 4.5v9M10.5 21.5h9\" fill=\"none\" stroke=\"#E6EAF0\" stroke-width=\"2\"/></g></svg>"},
        // ---- eclair
        {"eclair", "\xC3\x89" "clair", "\xC3\x89nergie et fluides",
         "\xC3\xA9" "clair, \xC3\xA9lectricit\xC3\xA9, \xC3\xA9nergie, courant, foudre",
         true, true, 64, 64,
         "<svg xmlns=\"http://www.w3.org/2000/svg\" width=\"64\" height=\"64\" viewBox=\"0 0 24 24\">"
         "<g stroke-linecap=\"round\" stroke-linejoin=\"round\">"
         "<path d=\"M13.5 2.5L5 13.5h6.5l-1.5 8 9-11.5h-6.5z\" fill=\"#E6EAF0\"/></g></svg>"},
        // ---- prise
        {"prise", "Prise", "\xC3\x89nergie et fluides",
         "prise, courant, secteur, \xC3\xA9lectricit\xC3\xA9",
         true, true, 64, 64,
         "<svg xmlns=\"http://www.w3.org/2000/svg\" width=\"64\" height=\"64\" viewBox=\"0 0 24 24\">"
         "<g stroke-linecap=\"round\" stroke-linejoin=\"round\">"
         "<circle cx=\"12\" cy=\"12\" r=\"9.5\" fill=\"none\" stroke=\"#E6EAF0\" stroke-width=\"2\"/>"
         "<circle cx=\"8.8\" cy=\"12.5\" r=\"1.5\" fill=\"#E6EAF0\"/>"
         "<circle cx=\"15.2\" cy=\"12.5\" r=\"1.5\" fill=\"#E6EAF0\"/>"
         "<path d=\"M12 4.5v2.5\" fill=\"none\" stroke=\"#E6EAF0\" stroke-width=\"2\"/></g></svg>"},
        // ---- fiche
        {"fiche", "Fiche", "\xC3\x89nergie et fluides",
         "fiche, branchement, c\xC3\xA2" "ble, \xC3\xA9lectricit\xC3\xA9",
         true, true, 64, 64,
         "<svg xmlns=\"http://www.w3.org/2000/svg\" width=\"64\" height=\"64\" viewBox=\"0 0 24 24\">"
         "<g stroke-linecap=\"round\" stroke-linejoin=\"round\">"
         "<path d=\"M9 3v5M15 3v5\" fill=\"none\" stroke=\"#E6EAF0\" stroke-width=\"2\"/>"
         "<path d=\"M6 8h12v2.5a6 6 0 0 1-12 0z\" fill=\"none\" stroke=\"#E6EAF0\" stroke-width=\"2\"/>"
         "<path d=\"M12 16.5V21\" fill=\"none\" stroke=\"#E6EAF0\" stroke-width=\"2\"/></g></svg>"},
        // ---- batterie
        {"batterie", "Batterie pleine", "\xC3\x89nergie et fluides",
         "batterie, pleine, charge, accumulateur",
         true, true, 64, 64,
         "<svg xmlns=\"http://www.w3.org/2000/svg\" width=\"64\" height=\"64\" viewBox=\"0 0 24 24\">"
         "<g stroke-linecap=\"round\" stroke-linejoin=\"round\">"
         "<rect x=\"2.5\" y=\"7\" width=\"17\" height=\"10\" rx=\"2\" fill=\"none\" stroke=\"#E6EAF0\" stroke-width=\"2\"/>"
         "<rect x=\"20\" y=\"10\" width=\"2\" height=\"4\" rx=\"0.6\" fill=\"#E6EAF0\"/>"
         "<rect x=\"5\" y=\"9.5\" width=\"12\" height=\"5\" rx=\"0.6\" fill=\"#E6EAF0\"/></g></svg>"},
        // ---- batterie-moitie
        {"batterie-moitie", "Batterie \xC3\xA0 moiti\xC3\xA9", "\xC3\x89nergie et fluides",
         "batterie, moiti\xC3\xA9, charge",
         true, true, 64, 64,
         "<svg xmlns=\"http://www.w3.org/2000/svg\" width=\"64\" height=\"64\" viewBox=\"0 0 24 24\">"
         "<g stroke-linecap=\"round\" stroke-linejoin=\"round\">"
         "<rect x=\"2.5\" y=\"7\" width=\"17\" height=\"10\" rx=\"2\" fill=\"none\" stroke=\"#E6EAF0\" stroke-width=\"2\"/>"
         "<rect x=\"20\" y=\"10\" width=\"2\" height=\"4\" rx=\"0.6\" fill=\"#E6EAF0\"/>"
         "<rect x=\"5\" y=\"9.5\" width=\"6\" height=\"5\" rx=\"0.6\" fill=\"#E6EAF0\"/></g></svg>"},
        // ---- batterie-faible
        {"batterie-faible", "Batterie faible", "\xC3\x89nergie et fluides",
         "batterie, faible, vide, d\xC3\xA9" "charge",
         true, true, 64, 64,
         "<svg xmlns=\"http://www.w3.org/2000/svg\" width=\"64\" height=\"64\" viewBox=\"0 0 24 24\">"
         "<g stroke-linecap=\"round\" stroke-linejoin=\"round\">"
         "<rect x=\"2.5\" y=\"7\" width=\"17\" height=\"10\" rx=\"2\" fill=\"none\" stroke=\"#E6EAF0\" stroke-width=\"2\"/>"
         "<rect x=\"20\" y=\"10\" width=\"2\" height=\"4\" rx=\"0.6\" fill=\"#E6EAF0\"/>"
         "<rect x=\"5\" y=\"9.5\" width=\"2.5\" height=\"5\" rx=\"0.6\" fill=\"#E6EAF0\"/></g></svg>"},
        // ---- batterie-charge
        {"batterie-charge", "Batterie en charge", "\xC3\x89nergie et fluides",
         "batterie, charge, recharge, chargeur",
         true, true, 64, 64,
         "<svg xmlns=\"http://www.w3.org/2000/svg\" width=\"64\" height=\"64\" viewBox=\"0 0 24 24\">"
         "<g stroke-linecap=\"round\" stroke-linejoin=\"round\">"
         "<rect x=\"2.5\" y=\"7\" width=\"17\" height=\"10\" rx=\"2\" fill=\"none\" stroke=\"#E6EAF0\" stroke-width=\"2\"/>"
         "<rect x=\"20\" y=\"10\" width=\"2\" height=\"4\" rx=\"0.6\" fill=\"#E6EAF0\"/>"
         "<path d=\"M12 8.5l-4 4.5h3l-1 3.5 4-4.5h-3z\" fill=\"#E6EAF0\"/></g></svg>"},
        // ---- panneau-solaire
        {"panneau-solaire", "Panneau solaire", "\xC3\x89nergie et fluides",
         "solaire, panneau, photovolta\xC3\xAFque, \xC3\xA9nergie",
         true, true, 64, 64,
         "<svg xmlns=\"http://www.w3.org/2000/svg\" width=\"64\" height=\"64\" viewBox=\"0 0 24 24\">"
         "<g stroke-linecap=\"round\" stroke-linejoin=\"round\">"
         "<path d=\"M5 4h14l3 9.5H2z\" fill=\"none\" stroke=\"#E6EAF0\" stroke-width=\"2\"/>"
         "<path d=\"M3.5 8.7h17M10 4l-1 9.5M14 4l1 9.5\" fill=\"none\" stroke=\"#E6EAF0\" stroke-width=\"1.5\"/>"
         "<path d=\"M12 13.5v5M7.5 20.5h9\" fill=\"none\" stroke=\"#E6EAF0\" stroke-width=\"2\"/></g></svg>"},
        // ---- eolienne
        {"eolienne", "\xC3\x89olienne", "\xC3\x89nergie et fluides",
         "\xC3\xA9olienne, vent, \xC3\xA9nergie, renouvelable",
         true, true, 64, 64,
         "<svg xmlns=\"http://www.w3.org/2000/svg\" width=\"64\" height=\"64\" viewBox=\"0 0 24 24\">"
         "<g stroke-linecap=\"round\" stroke-linejoin=\"round\">"
         "<path d=\"M11 21.5l.5-12h1l.5 12z\" fill=\"#E6EAF0\"/>"
         "<path d=\"M8 21.5h8\" fill=\"none\" stroke=\"#E6EAF0\" stroke-width=\"2\"/>"
         "<g transform=\"rotate(0 12 9.5)\">"
         "<path d=\"M12 9.5c-1.6-2.6-1.4-5.3 0-7.5 1.4 2.2 1.6 4.9 0 7.5z\" fill=\"#E6EAF0\"/></g>"
         "<g transform=\"rotate(120 12 9.5)\">"
         "<path d=\"M12 9.5c-1.6-2.6-1.4-5.3 0-7.5 1.4 2.2 1.6 4.9 0 7.5z\" fill=\"#E6EAF0\"/></g>"
         "<g transform=\"rotate(240 12 9.5)\">"
         "<path d=\"M12 9.5c-1.6-2.6-1.4-5.3 0-7.5 1.4 2.2 1.6 4.9 0 7.5z\" fill=\"#E6EAF0\"/></g>"
         "<circle cx=\"12\" cy=\"9.5\" r=\"1.6\" fill=\"#E6EAF0\"/></g></svg>"},
        // ---- goutte
        {"goutte", "Goutte d'eau", "\xC3\x89nergie et fluides",
         "eau, goutte, liquide, fluide",
         true, true, 64, 64,
         "<svg xmlns=\"http://www.w3.org/2000/svg\" width=\"64\" height=\"64\" viewBox=\"0 0 24 24\">"
         "<g stroke-linecap=\"round\" stroke-linejoin=\"round\">"
         "<path d=\"M12 3s-6.5 7.2-6.5 11.5a6.5 6.5 0 0 0 13 0C18.5 10.2 12 3 12 3z\" fill=\"none\" stroke=\"#E6EAF0\" stroke-width=\"2\"/>"
         "</g></svg>"},
        // ---- robinet
        {"robinet", "Robinet", "\xC3\x89nergie et fluides",
         "robinet, eau, distribution",
         true, true, 64, 64,
         "<svg xmlns=\"http://www.w3.org/2000/svg\" width=\"64\" height=\"64\" viewBox=\"0 0 24 24\">"
         "<g stroke-linecap=\"round\" stroke-linejoin=\"round\">"
         "<path d=\"M3 8h10a3.5 3.5 0 0 1 3.5 3.5V13\" fill=\"none\" stroke=\"#E6EAF0\" stroke-width=\"2\"/>"
         "<path d=\"M8 8V4.5M5.5 4.5h5\" fill=\"none\" stroke=\"#E6EAF0\" stroke-width=\"2\"/>"
         "<path d=\"M16.5 16.5s-1.6 1.8-1.6 2.9a1.6 1.6 0 0 0 3.2 0c0-1.1-1.6-2.9-1.6-2.9z\" fill=\"#E6EAF0\"/></g>"
         "</svg>"},
        // ---- flamme
        {"flamme", "Flamme", "\xC3\x89nergie et fluides",
         "flamme, feu, gaz, chaleur, combustion",
         true, true, 64, 64,
         "<svg xmlns=\"http://www.w3.org/2000/svg\" width=\"64\" height=\"64\" viewBox=\"0 0 24 24\">"
         "<g stroke-linecap=\"round\" stroke-linejoin=\"round\">"
         "<path d=\"M12 21.5c-3.9 0-6.5-2.6-6.5-6 0-3.2 2.2-4.8 3.6-7 .4 1.8 1.3 2.8 2.4 3.3.1-3.1 1.6-5.8 4.2-7.8.2 3.3 2.8 5.1 2.8 10 0 4-2.6 7.5-6.5 7.5z\" fill=\"none\" stroke=\"#E6EAF0\" stroke-width=\"2\"/>"
         "</g></svg>"},
        // ---- flocon
        {"flocon", "Flocon", "\xC3\x89nergie et fluides",
         "froid, gel, neige, flocon, r\xC3\xA9" "frig\xC3\xA9ration",
         true, true, 64, 64,
         "<svg xmlns=\"http://www.w3.org/2000/svg\" width=\"64\" height=\"64\" viewBox=\"0 0 24 24\">"
         "<g stroke-linecap=\"round\" stroke-linejoin=\"round\"><g transform=\"rotate(0 12 12)\">"
         "<path d=\"M12 2.5v19\" fill=\"none\" stroke=\"#E6EAF0\" stroke-width=\"2\"/></g>"
         "<g transform=\"rotate(60 12 12)\">"
         "<path d=\"M12 2.5v19\" fill=\"none\" stroke=\"#E6EAF0\" stroke-width=\"2\"/></g>"
         "<g transform=\"rotate(120 12 12)\">"
         "<path d=\"M12 2.5v19\" fill=\"none\" stroke=\"#E6EAF0\" stroke-width=\"2\"/></g>"
         "<g transform=\"rotate(0 12 12)\">"
         "<path d=\"M9.5 4l2.5 2.5L14.5 4M9.5 20l2.5-2.5 2.5 2.5\" fill=\"none\" stroke=\"#E6EAF0\" stroke-width=\"2\"/>"
         "</g><g transform=\"rotate(60 12 12)\">"
         "<path d=\"M9.5 4l2.5 2.5L14.5 4M9.5 20l2.5-2.5 2.5 2.5\" fill=\"none\" stroke=\"#E6EAF0\" stroke-width=\"2\"/>"
         "</g><g transform=\"rotate(120 12 12)\">"
         "<path d=\"M9.5 4l2.5 2.5L14.5 4M9.5 20l2.5-2.5 2.5 2.5\" fill=\"none\" stroke=\"#E6EAF0\" stroke-width=\"2\"/>"
         "</g></g></svg>"},
        // ---- vapeur
        {"vapeur", "Vapeur", "\xC3\x89nergie et fluides",
         "vapeur, chaleur, fum\xC3\xA9" "e, \xC3\xA9vaporation",
         true, true, 64, 64,
         "<svg xmlns=\"http://www.w3.org/2000/svg\" width=\"64\" height=\"64\" viewBox=\"0 0 24 24\">"
         "<g stroke-linecap=\"round\" stroke-linejoin=\"round\">"
         "<path d=\"M7.5 20c-1.8-2.4 1.8-4 0-6.4s1.8-4 0-6.4\" fill=\"none\" stroke=\"#E6EAF0\" stroke-width=\"2\"/>"
         "<path d=\"M12 18c-1.8-2.4 1.8-4 0-6.4s1.8-4 0-6.4\" fill=\"none\" stroke=\"#E6EAF0\" stroke-width=\"2\"/>"
         "<path d=\"M16.5 20c-1.8-2.4 1.8-4 0-6.4s1.8-4 0-6.4\" fill=\"none\" stroke=\"#E6EAF0\" stroke-width=\"2\"/>"
         "</g></svg>"},
        // ---- vent
        {"vent", "Vent", "\xC3\x89nergie et fluides",
         "vent, air, souffle, ventilation",
         true, true, 64, 64,
         "<svg xmlns=\"http://www.w3.org/2000/svg\" width=\"64\" height=\"64\" viewBox=\"0 0 24 24\">"
         "<g stroke-linecap=\"round\" stroke-linejoin=\"round\">"
         "<path d=\"M3 8.5h10a2.8 2.8 0 1 0-2.8-2.8\" fill=\"none\" stroke=\"#E6EAF0\" stroke-width=\"2\"/>"
         "<path d=\"M3 15.5h13.5a2.8 2.8 0 1 1-2.8 2.8\" fill=\"none\" stroke=\"#E6EAF0\" stroke-width=\"2\"/>"
         "<path d=\"M3 12h16\" fill=\"none\" stroke=\"#E6EAF0\" stroke-width=\"2\"/></g></svg>"},
        // ---- bouteille-gaz
        {"bouteille-gaz", "Bouteille de gaz", "\xC3\x89nergie et fluides",
         "gaz, bouteille, comprim\xC3\xA9, cylindre",
         true, true, 64, 64,
         "<svg xmlns=\"http://www.w3.org/2000/svg\" width=\"64\" height=\"64\" viewBox=\"0 0 24 24\">"
         "<g stroke-linecap=\"round\" stroke-linejoin=\"round\">"
         "<path d=\"M8 9a4 4 0 0 1 8 0v11a1 1 0 0 1-1 1H9a1 1 0 0 1-1-1z\" fill=\"none\" stroke=\"#E6EAF0\" stroke-width=\"2\"/>"
         "<path d=\"M12 5V3M10 3h4\" fill=\"none\" stroke=\"#E6EAF0\" stroke-width=\"2\"/>"
         "<path d=\"M8 12.5h8\" fill=\"none\" stroke=\"#E6EAF0\" stroke-width=\"1.5\" opacity=\"0.6\"/></g></svg>"},
        // ---- air-comprime
        {"air-comprime", "Air comprim\xC3\xA9", "\xC3\x89nergie et fluides",
         "air comprim\xC3\xA9, r\xC3\xA9servoir, pneumatique, pression",
         true, true, 64, 64,
         "<svg xmlns=\"http://www.w3.org/2000/svg\" width=\"64\" height=\"64\" viewBox=\"0 0 24 24\">"
         "<g stroke-linecap=\"round\" stroke-linejoin=\"round\">"
         "<rect x=\"2.5\" y=\"10\" width=\"19\" height=\"8.5\" rx=\"4.25\" fill=\"none\" stroke=\"#E6EAF0\" stroke-width=\"2\"/>"
         "<circle cx=\"12\" cy=\"5.2\" r=\"2.6\" fill=\"none\" stroke=\"#E6EAF0\" stroke-width=\"2\"/>"
         "<path d=\"M12 7.8V10\" fill=\"none\" stroke=\"#E6EAF0\" stroke-width=\"2\"/>"
         "<path d=\"M12 5.2l1.3-1.2\" fill=\"none\" stroke=\"#E6EAF0\" stroke-width=\"1.3\"/>"
         "<path d=\"M7 18.5v3M17 18.5v3\" fill=\"none\" stroke=\"#E6EAF0\" stroke-width=\"2\"/></g></svg>"},
        // ---- huile
        {"huile", "Huile", "\xC3\x89nergie et fluides",
         "huile, lubrifiant, graissage, burette",
         true, true, 64, 64,
         "<svg xmlns=\"http://www.w3.org/2000/svg\" width=\"64\" height=\"64\" viewBox=\"0 0 24 24\">"
         "<g stroke-linecap=\"round\" stroke-linejoin=\"round\">"
         "<path d=\"M3.5 20.5V12.5h8l3 3v5z\" fill=\"none\" stroke=\"#E6EAF0\" stroke-width=\"2\"/>"
         "<path d=\"M14.5 15.5L21 9.5\" fill=\"none\" stroke=\"#E6EAF0\" stroke-width=\"2\"/>"
         "<path d=\"M5.5 12.5v-2h4v2\" fill=\"none\" stroke=\"#E6EAF0\" stroke-width=\"2\"/>"
         "<path d=\"M20.5 13s-1.3 1.5-1.3 2.4a1.3 1.3 0 0 0 2.6 0c0-.9-1.3-2.4-1.3-2.4z\" fill=\"#E6EAF0\"/></g>"
         "</svg>"},
        // ---- ampoule
        {"ampoule", "Ampoule", "\xC3\x89nergie et fluides",
         "ampoule, lumi\xC3\xA8re, \xC3\xA9" "clairage, id\xC3\xA9" "e, lampe",
         true, true, 64, 64,
         "<svg xmlns=\"http://www.w3.org/2000/svg\" width=\"64\" height=\"64\" viewBox=\"0 0 24 24\">"
         "<g stroke-linecap=\"round\" stroke-linejoin=\"round\">"
         "<path d=\"M9 18h6M10 21h4\" fill=\"none\" stroke=\"#E6EAF0\" stroke-width=\"2\"/>"
         "<path d=\"M9 15.5c0-2-3.2-3.6-3.2-7.3a6.2 6.2 0 0 1 12.4 0c0 3.7-3.2 5.3-3.2 7.3z\" fill=\"none\" stroke=\"#E6EAF0\" stroke-width=\"2\"/>"
         "</g></svg>"},
        // ---- compteur-electrique
        {"compteur-electrique", "Compteur \xC3\xA9lectrique", "\xC3\x89nergie et fluides",
         "compteur, \xC3\xA9lectrique, \xC3\xA9nergie, consommation, kWh",
         true, true, 64, 64,
         "<svg xmlns=\"http://www.w3.org/2000/svg\" width=\"64\" height=\"64\" viewBox=\"0 0 24 24\">"
         "<g stroke-linecap=\"round\" stroke-linejoin=\"round\">"
         "<rect x=\"4\" y=\"2.5\" width=\"16\" height=\"19\" rx=\"2\" fill=\"none\" stroke=\"#E6EAF0\" stroke-width=\"2\"/>"
         "<rect x=\"7\" y=\"5.5\" width=\"10\" height=\"4.5\" rx=\"0.8\" fill=\"none\" stroke=\"#E6EAF0\" stroke-width=\"2\"/>"
         "<path d=\"M12.6 12.5l-2.6 3.6h2l-.6 3 2.6-3.6h-2z\" fill=\"#E6EAF0\"/></g></svg>"},
        // ---- pylone
        {"pylone", "Pyl\xC3\xB4ne", "\xC3\x89nergie et fluides",
         "pyl\xC3\xB4ne, ligne, haute tension, r\xC3\xA9seau \xC3\xA9lectrique",
         true, true, 64, 64,
         "<svg xmlns=\"http://www.w3.org/2000/svg\" width=\"64\" height=\"64\" viewBox=\"0 0 24 24\">"
         "<g stroke-linecap=\"round\" stroke-linejoin=\"round\">"
         "<path d=\"M8 21.5L12 2.5l4 19\" fill=\"none\" stroke=\"#E6EAF0\" stroke-width=\"2\"/>"
         "<path d=\"M5 7.5h14M6.5 12h11\" fill=\"none\" stroke=\"#E6EAF0\" stroke-width=\"2\"/>"
         "<path d=\"M9.5 12l5 9.5M14.5 12l-5 9.5\" fill=\"none\" stroke=\"#E6EAF0\" stroke-width=\"1.5\"/>"
         "<path d=\"M5 7.5v2M19 7.5v2\" fill=\"none\" stroke=\"#E6EAF0\" stroke-width=\"1.5\"/></g></svg>"},
        // ---- transformateur
        {"transformateur", "Transformateur", "\xC3\x89nergie et fluides",
         "transformateur, tension, \xC3\xA9lectrique, poste",
         true, true, 64, 64,
         "<svg xmlns=\"http://www.w3.org/2000/svg\" width=\"64\" height=\"64\" viewBox=\"0 0 24 24\">"
         "<g stroke-linecap=\"round\" stroke-linejoin=\"round\">"
         "<circle cx=\"9\" cy=\"12\" r=\"5.5\" fill=\"none\" stroke=\"#E6EAF0\" stroke-width=\"2\"/>"
         "<circle cx=\"15\" cy=\"12\" r=\"5.5\" fill=\"none\" stroke=\"#E6EAF0\" stroke-width=\"2\"/>"
         "<path d=\"M3.5 12H2M20.5 12H22\" fill=\"none\" stroke=\"#E6EAF0\" stroke-width=\"2\"/></g></svg>"},
        // ---- feuille
        {"feuille", "\xC3\x89" "conomie d'\xC3\xA9nergie", "\xC3\x89nergie et fluides",
         "feuille, \xC3\xA9" "cologie, \xC3\xA9" "conomie, environnement, vert",
         true, true, 64, 64,
         "<svg xmlns=\"http://www.w3.org/2000/svg\" width=\"64\" height=\"64\" viewBox=\"0 0 24 24\">"
         "<g stroke-linecap=\"round\" stroke-linejoin=\"round\">"
         "<path d=\"M5 19.5C5 10.5 10.5 5 19.5 4.5c-.5 9-6 15-14.5 15z\" fill=\"none\" stroke=\"#E6EAF0\" stroke-width=\"2\"/>"
         "<path d=\"M5 19.5l8.5-8.5\" fill=\"none\" stroke=\"#E6EAF0\" stroke-width=\"2\"/></g></svg>"},
        // ---- maison
        {"maison", "Maison", "B\xC3\xA2timent et infrastructure",
         "maison, habitation, b\xC3\xA2timent, logement",
         true, true, 64, 64,
         "<svg xmlns=\"http://www.w3.org/2000/svg\" width=\"64\" height=\"64\" viewBox=\"0 0 24 24\">"
         "<g stroke-linecap=\"round\" stroke-linejoin=\"round\">"
         "<path d=\"M3 11.5L12 4l9 7.5\" fill=\"none\" stroke=\"#E6EAF0\" stroke-width=\"2\"/>"
         "<path d=\"M5.5 9.5v11h13v-11\" fill=\"none\" stroke=\"#E6EAF0\" stroke-width=\"2\"/>"
         "<rect x=\"10\" y=\"14.5\" width=\"4\" height=\"6\" fill=\"none\" stroke=\"#E6EAF0\" stroke-width=\"2\"/>"
         "<path d=\"M16 6.5V4h2.5v4.5\" fill=\"none\" stroke=\"#E6EAF0\" stroke-width=\"2\"/></g></svg>"},
        // ---- immeuble
        {"immeuble", "Immeuble", "B\xC3\xA2timent et infrastructure",
         "immeuble, bureaux, b\xC3\xA2timent, \xC3\xA9tages",
         true, true, 64, 64,
         "<svg xmlns=\"http://www.w3.org/2000/svg\" width=\"64\" height=\"64\" viewBox=\"0 0 24 24\">"
         "<g stroke-linecap=\"round\" stroke-linejoin=\"round\">"
         "<rect x=\"5\" y=\"2.5\" width=\"14\" height=\"19\" rx=\"1\" fill=\"none\" stroke=\"#E6EAF0\" stroke-width=\"2\"/>"
         "<rect x=\"8\" y=\"5.5\" width=\"2.5\" height=\"2.2\" rx=\"0.3\" fill=\"#E6EAF0\"/>"
         "<rect x=\"13.5\" y=\"5.5\" width=\"2.5\" height=\"2.2\" rx=\"0.3\" fill=\"#E6EAF0\"/>"
         "<rect x=\"8\" y=\"9.5\" width=\"2.5\" height=\"2.2\" rx=\"0.3\" fill=\"#E6EAF0\"/>"
         "<rect x=\"13.5\" y=\"9.5\" width=\"2.5\" height=\"2.2\" rx=\"0.3\" fill=\"#E6EAF0\"/>"
         "<rect x=\"8\" y=\"13.5\" width=\"2.5\" height=\"2.2\" rx=\"0.3\" fill=\"#E6EAF0\"/>"
         "<rect x=\"13.5\" y=\"13.5\" width=\"2.5\" height=\"2.2\" rx=\"0.3\" fill=\"#E6EAF0\"/>"
         "<path d=\"M10.5 21.5v-3.5h3v3.5\" fill=\"none\" stroke=\"#E6EAF0\" stroke-width=\"2\"/></g></svg>"},
        // ---- entrepot
        {"entrepot", "Entrep\xC3\xB4t", "B\xC3\xA2timent et infrastructure",
         "entrep\xC3\xB4t, hangar, stockage, magasin",
         true, true, 64, 64,
         "<svg xmlns=\"http://www.w3.org/2000/svg\" width=\"64\" height=\"64\" viewBox=\"0 0 24 24\">"
         "<g stroke-linecap=\"round\" stroke-linejoin=\"round\">"
         "<path d=\"M2.5 9l9.5-5 9.5 5v12h-19z\" fill=\"none\" stroke=\"#E6EAF0\" stroke-width=\"2\"/>"
         "<path d=\"M6 21v-8.5h12V21M6 15.5h12M6 18.3h12\" fill=\"none\" stroke=\"#E6EAF0\" stroke-width=\"1.5\"/>"
         "</g></svg>"},
        // ---- porte
        {"porte", "Porte", "B\xC3\xA2timent et infrastructure",
         "porte, acc\xC3\xA8s, entr\xC3\xA9" "e, issue",
         true, true, 64, 64,
         "<svg xmlns=\"http://www.w3.org/2000/svg\" width=\"64\" height=\"64\" viewBox=\"0 0 24 24\">"
         "<g stroke-linecap=\"round\" stroke-linejoin=\"round\">"
         "<rect x=\"6\" y=\"2.5\" width=\"12\" height=\"19\" rx=\"1\" fill=\"none\" stroke=\"#E6EAF0\" stroke-width=\"2\"/>"
         "<circle cx=\"15\" cy=\"12.5\" r=\"1.2\" fill=\"#E6EAF0\"/>"
         "<path d=\"M3.5 21.5h17\" fill=\"none\" stroke=\"#E6EAF0\" stroke-width=\"2\"/></g></svg>"},
        // ---- porte-ouverte
        {"porte-ouverte", "Porte ouverte", "B\xC3\xA2timent et infrastructure",
         "porte, ouverte, acc\xC3\xA8s, passage",
         true, true, 64, 64,
         "<svg xmlns=\"http://www.w3.org/2000/svg\" width=\"64\" height=\"64\" viewBox=\"0 0 24 24\">"
         "<g stroke-linecap=\"round\" stroke-linejoin=\"round\">"
         "<path d=\"M5 21.5V2.5h14v19\" fill=\"none\" stroke=\"#E6EAF0\" stroke-width=\"2\"/>"
         "<path d=\"M5 2.5l8 2.5v18l-8-1.5\" fill=\"none\" stroke=\"#E6EAF0\" stroke-width=\"2\"/>"
         "<circle cx=\"11\" cy=\"12.5\" r=\"1\" fill=\"#E6EAF0\"/>"
         "<path d=\"M3 21.5h18\" fill=\"none\" stroke=\"#E6EAF0\" stroke-width=\"2\"/></g></svg>"},
        // ---- fenetre
        {"fenetre", "Fen\xC3\xAAtre", "B\xC3\xA2timent et infrastructure",
         "fen\xC3\xAAtre, vitre, ouvrant",
         true, true, 64, 64,
         "<svg xmlns=\"http://www.w3.org/2000/svg\" width=\"64\" height=\"64\" viewBox=\"0 0 24 24\">"
         "<g stroke-linecap=\"round\" stroke-linejoin=\"round\">"
         "<rect x=\"4\" y=\"3.5\" width=\"16\" height=\"17\" rx=\"1.5\" fill=\"none\" stroke=\"#E6EAF0\" stroke-width=\"2\"/>"
         "<path d=\"M12 3.5v17M4 11.5h16\" fill=\"none\" stroke=\"#E6EAF0\" stroke-width=\"2\"/></g></svg>"},
        // ---- escalier
        {"escalier", "Escalier", "B\xC3\xA2timent et infrastructure",
         "escalier, marches, \xC3\xA9tage, acc\xC3\xA8s",
         true, true, 64, 64,
         "<svg xmlns=\"http://www.w3.org/2000/svg\" width=\"64\" height=\"64\" viewBox=\"0 0 24 24\">"
         "<g stroke-linecap=\"round\" stroke-linejoin=\"round\">"
         "<path d=\"M2.5 20.5h4.5v-4h4.5v-4h4.5v-4h4.5V4\" fill=\"none\" stroke=\"#E6EAF0\" stroke-width=\"2\"/></g>"
         "</svg>"},
        // ---- ascenseur
        {"ascenseur", "Ascenseur", "B\xC3\xA2timent et infrastructure",
         "ascenseur, monte-charge, \xC3\xA9tage, cabine",
         true, true, 64, 64,
         "<svg xmlns=\"http://www.w3.org/2000/svg\" width=\"64\" height=\"64\" viewBox=\"0 0 24 24\">"
         "<g stroke-linecap=\"round\" stroke-linejoin=\"round\">"
         "<rect x=\"4\" y=\"2.5\" width=\"16\" height=\"19\" rx=\"2\" fill=\"none\" stroke=\"#E6EAF0\" stroke-width=\"2\"/>"
         "<path d=\"M12 5.5l3.2 4H8.8z\" fill=\"#E6EAF0\"/><path d=\"M12 18.5l3.2-4H8.8z\" fill=\"#E6EAF0\"/></g>"
         "</svg>"},
        // ---- parking
        {"parking", "Parking", "B\xC3\xA2timent et infrastructure",
         "parking, stationnement, P, voiture",
         true, true, 64, 64,
         "<svg xmlns=\"http://www.w3.org/2000/svg\" width=\"64\" height=\"64\" viewBox=\"0 0 24 24\">"
         "<g stroke-linecap=\"round\" stroke-linejoin=\"round\">"
         "<rect x=\"3\" y=\"3\" width=\"18\" height=\"18\" rx=\"3\" fill=\"none\" stroke=\"#E6EAF0\" stroke-width=\"2\"/>"
         "<path d=\"M9.5 17V7h3.5a3 3 0 0 1 0 6H9.5\" fill=\"none\" stroke=\"#E6EAF0\" stroke-width=\"2\"/></g></svg>"},
        // ---- eclairage
        {"eclairage", "\xC3\x89" "clairage", "B\xC3\xA2timent et infrastructure",
         "\xC3\xA9" "clairage, plafonnier, lumi\xC3\xA8re, luminaire",
         true, true, 64, 64,
         "<svg xmlns=\"http://www.w3.org/2000/svg\" width=\"64\" height=\"64\" viewBox=\"0 0 24 24\">"
         "<g stroke-linecap=\"round\" stroke-linejoin=\"round\">"
         "<path d=\"M12 2.5v4\" fill=\"none\" stroke=\"#E6EAF0\" stroke-width=\"2\"/>"
         "<path d=\"M5.5 13a6.5 6.5 0 0 1 13 0z\" fill=\"none\" stroke=\"#E6EAF0\" stroke-width=\"2\"/>"
         "<path d=\"M12 16v4M7 15.5L5 18M17 15.5l2 2.5\" fill=\"none\" stroke=\"#E6EAF0\" stroke-width=\"2\"/></g>"
         "</svg>"},
        // ---- radiateur
        {"radiateur", "Chauffage", "B\xC3\xA2timent et infrastructure",
         "chauffage, radiateur, chaleur, temp\xC3\xA9rature",
         true, true, 64, 64,
         "<svg xmlns=\"http://www.w3.org/2000/svg\" width=\"64\" height=\"64\" viewBox=\"0 0 24 24\">"
         "<g stroke-linecap=\"round\" stroke-linejoin=\"round\">"
         "<rect x=\"3\" y=\"4.5\" width=\"3.5\" height=\"13\" rx=\"1.6\" fill=\"none\" stroke=\"#E6EAF0\" stroke-width=\"2\"/>"
         "<rect x=\"7.5\" y=\"4.5\" width=\"3.5\" height=\"13\" rx=\"1.6\" fill=\"none\" stroke=\"#E6EAF0\" stroke-width=\"2\"/>"
         "<rect x=\"12\" y=\"4.5\" width=\"3.5\" height=\"13\" rx=\"1.6\" fill=\"none\" stroke=\"#E6EAF0\" stroke-width=\"2\"/>"
         "<rect x=\"16.5\" y=\"4.5\" width=\"3.5\" height=\"13\" rx=\"1.6\" fill=\"none\" stroke=\"#E6EAF0\" stroke-width=\"2\"/>"
         "<path d=\"M4.5 17.5v3M18.5 17.5v3\" fill=\"none\" stroke=\"#E6EAF0\" stroke-width=\"2\"/></g></svg>"},
        // ---- climatisation
        {"climatisation", "Climatisation", "B\xC3\xA2timent et infrastructure",
         "climatisation, clim, froid, air",
         true, true, 64, 64,
         "<svg xmlns=\"http://www.w3.org/2000/svg\" width=\"64\" height=\"64\" viewBox=\"0 0 24 24\">"
         "<g stroke-linecap=\"round\" stroke-linejoin=\"round\">"
         "<rect x=\"2.5\" y=\"4\" width=\"19\" height=\"8\" rx=\"2\" fill=\"none\" stroke=\"#E6EAF0\" stroke-width=\"2\"/>"
         "<path d=\"M5.5 9h13\" fill=\"none\" stroke=\"#E6EAF0\" stroke-width=\"1.5\"/>"
         "<path d=\"M7.5 15l-1 4.5M12 15v5M16.5 15l1 4.5\" fill=\"none\" stroke=\"#E6EAF0\" stroke-width=\"1.6\"/>"
         "</g></svg>"},
        // ---- ventilation
        {"ventilation", "Ventilation", "B\xC3\xA2timent et infrastructure",
         "ventilation, VMC, extraction, bouche",
         true, true, 64, 64,
         "<svg xmlns=\"http://www.w3.org/2000/svg\" width=\"64\" height=\"64\" viewBox=\"0 0 24 24\">"
         "<g stroke-linecap=\"round\" stroke-linejoin=\"round\">"
         "<rect x=\"3\" y=\"3\" width=\"18\" height=\"18\" rx=\"2.5\" fill=\"none\" stroke=\"#E6EAF0\" stroke-width=\"2\"/>"
         "<circle cx=\"12\" cy=\"12\" r=\"6.5\" fill=\"none\" stroke=\"#E6EAF0\" stroke-width=\"2\"/>"
         "<g transform=\"rotate(0 12 12)\"><path d=\"M12 12c-.3-2.4.2-4 2-5 1.1.8.9 2.5-2 5z\" fill=\"#E6EAF0\"/></g>"
         "<g transform=\"rotate(120 12 12)\"><path d=\"M12 12c-.3-2.4.2-4 2-5 1.1.8.9 2.5-2 5z\" fill=\"#E6EAF0\"/>"
         "</g><g transform=\"rotate(240 12 12)\">"
         "<path d=\"M12 12c-.3-2.4.2-4 2-5 1.1.8.9 2.5-2 5z\" fill=\"#E6EAF0\"/></g>"
         "<circle cx=\"12\" cy=\"12\" r=\"1.2\" fill=\"#E6EAF0\"/></g></svg>"},
        // ---- volet-roulant
        {"volet-roulant", "Volet roulant", "B\xC3\xA2timent et infrastructure",
         "volet, store, fen\xC3\xAAtre, occultation",
         true, true, 64, 64,
         "<svg xmlns=\"http://www.w3.org/2000/svg\" width=\"64\" height=\"64\" viewBox=\"0 0 24 24\">"
         "<g stroke-linecap=\"round\" stroke-linejoin=\"round\">"
         "<rect x=\"4\" y=\"3\" width=\"16\" height=\"18\" rx=\"1\" fill=\"none\" stroke=\"#E6EAF0\" stroke-width=\"2\"/>"
         "<path d=\"M4 6.5h16M4 9.5h16M4 12.5h16\" fill=\"none\" stroke=\"#E6EAF0\" stroke-width=\"1.5\"/>"
         "<path d=\"M12 12.5v3.5\" fill=\"none\" stroke=\"#E6EAF0\" stroke-width=\"1.5\"/></g></svg>"},
        // ---- thermostat
        {"thermostat", "Thermostat", "B\xC3\xA2timent et infrastructure",
         "thermostat, consigne, temp\xC3\xA9rature, r\xC3\xA9gulation",
         true, true, 64, 64,
         "<svg xmlns=\"http://www.w3.org/2000/svg\" width=\"64\" height=\"64\" viewBox=\"0 0 24 24\">"
         "<g stroke-linecap=\"round\" stroke-linejoin=\"round\">"
         "<rect x=\"4.5\" y=\"2.5\" width=\"15\" height=\"19\" rx=\"3\" fill=\"none\" stroke=\"#E6EAF0\" stroke-width=\"2\"/>"
         "<circle cx=\"12\" cy=\"10.5\" r=\"4.5\" fill=\"none\" stroke=\"#E6EAF0\" stroke-width=\"2\"/>"
         "<path d=\"M12 10.5l2.4-2.4\" fill=\"none\" stroke=\"#E6EAF0\" stroke-width=\"2\"/>"
         "<path d=\"M9 17.5h6\" fill=\"none\" stroke=\"#E6EAF0\" stroke-width=\"2\"/></g></svg>"},
        // ---- camera
        {"camera", "Cam\xC3\xA9ra", "B\xC3\xA2timent et infrastructure",
         "cam\xC3\xA9ra, surveillance, vid\xC3\xA9o, s\xC3\xA9" "curit\xC3\xA9",
         true, true, 64, 64,
         "<svg xmlns=\"http://www.w3.org/2000/svg\" width=\"64\" height=\"64\" viewBox=\"0 0 24 24\">"
         "<g stroke-linecap=\"round\" stroke-linejoin=\"round\">"
         "<path d=\"M3.5 9.5l12-4 2.2 6.5-12 4z\" fill=\"none\" stroke=\"#E6EAF0\" stroke-width=\"2\"/>"
         "<path d=\"M17.6 9l3.4-1.6v4.6l-2.6-.9\" fill=\"none\" stroke=\"#E6EAF0\" stroke-width=\"2\"/>"
         "<path d=\"M8 14l1 4H4.5M4.5 15v6\" fill=\"none\" stroke=\"#E6EAF0\" stroke-width=\"2\"/></g></svg>"},
        // ---- detecteur-fumee
        {"detecteur-fumee", "D\xC3\xA9tecteur de fum\xC3\xA9" "e", "B\xC3\xA2timent et infrastructure",
         "d\xC3\xA9tecteur, fum\xC3\xA9" "e, incendie, plafond",
         true, true, 64, 64,
         "<svg xmlns=\"http://www.w3.org/2000/svg\" width=\"64\" height=\"64\" viewBox=\"0 0 24 24\">"
         "<g stroke-linecap=\"round\" stroke-linejoin=\"round\">"
         "<path d=\"M3 5.5h18\" fill=\"none\" stroke=\"#E6EAF0\" stroke-width=\"2\"/>"
         "<path d=\"M5 5.5v2.5a7 7 0 0 0 14 0V5.5\" fill=\"none\" stroke=\"#E6EAF0\" stroke-width=\"2\"/>"
         "<circle cx=\"12\" cy=\"9.5\" r=\"1\" fill=\"#E6EAF0\"/>"
         "<path d=\"M8.5 17.5c2 1.4 5 1.4 7 0M6 20.5c3.4 2 8.6 2 12 0\" fill=\"none\" stroke=\"#E6EAF0\" stroke-width=\"1.5\"/>"
         "</g></svg>"},
        // ---- barriere
        {"barriere", "Barri\xC3\xA8re", "B\xC3\xA2timent et infrastructure",
         "barri\xC3\xA8re, parking, acc\xC3\xA8s, contr\xC3\xB4le",
         true, true, 64, 64,
         "<svg xmlns=\"http://www.w3.org/2000/svg\" width=\"64\" height=\"64\" viewBox=\"0 0 24 24\">"
         "<g stroke-linecap=\"round\" stroke-linejoin=\"round\">"
         "<rect x=\"3\" y=\"12\" width=\"4\" height=\"9\" rx=\"1\" fill=\"none\" stroke=\"#E6EAF0\" stroke-width=\"2\"/>"
         "<rect x=\"7\" y=\"12.5\" width=\"14.5\" height=\"3\" rx=\"1\" fill=\"none\" stroke=\"#E6EAF0\" stroke-width=\"2\"/>"
         "<path d=\"M11 12.5l-2 3M15 12.5l-2 3M19 12.5l-2 3\" fill=\"none\" stroke=\"#E6EAF0\" stroke-width=\"1.5\"/>"
         "<path d=\"M2 21h6\" fill=\"none\" stroke=\"#E6EAF0\" stroke-width=\"2\"/></g></svg>"},
        // ---- automate
        {"automate", "Automate (API)", "R\xC3\xA9seau et informatique",
         "automate, API, PLC, contr\xC3\xB4leur",
         true, true, 64, 64,
         "<svg xmlns=\"http://www.w3.org/2000/svg\" width=\"64\" height=\"64\" viewBox=\"0 0 24 24\">"
         "<g stroke-linecap=\"round\" stroke-linejoin=\"round\">"
         "<rect x=\"2.5\" y=\"5\" width=\"19\" height=\"14\" rx=\"1.5\" fill=\"none\" stroke=\"#E6EAF0\" stroke-width=\"2\"/>"
         "<path d=\"M7 5v14M11.5 5v14M16 5v14\" fill=\"none\" stroke=\"#E6EAF0\" stroke-width=\"1.4\"/>"
         "<circle cx=\"4.75\" cy=\"8\" r=\"0.9\" fill=\"#E6EAF0\"/>"
         "<circle cx=\"4.75\" cy=\"10.8\" r=\"0.9\" fill=\"#E6EAF0\"/>"
         "<path d=\"M9.25 8v2M9.25 13.5h0\" fill=\"none\" stroke=\"#E6EAF0\" stroke-width=\"2\"/>"
         "<path d=\"M13.75 8v8M18.25 8v8\" fill=\"none\" stroke=\"#E6EAF0\" stroke-width=\"1.4\" stroke-dasharray=\"1.2 1.2\" stroke-linecap=\"butt\"/>"
         "</g></svg>"},
        // ---- ecran
        {"ecran", "\xC3\x89" "cran", "R\xC3\xA9seau et informatique",
         "\xC3\xA9" "cran, moniteur, affichage, IHM",
         true, true, 64, 64,
         "<svg xmlns=\"http://www.w3.org/2000/svg\" width=\"64\" height=\"64\" viewBox=\"0 0 24 24\">"
         "<g stroke-linecap=\"round\" stroke-linejoin=\"round\">"
         "<rect x=\"2.5\" y=\"4\" width=\"19\" height=\"12.5\" rx=\"1.5\" fill=\"none\" stroke=\"#E6EAF0\" stroke-width=\"2\"/>"
         "<path d=\"M12 16.5v4M8 20.5h8\" fill=\"none\" stroke=\"#E6EAF0\" stroke-width=\"2\"/></g></svg>"},
        // ---- pc
        {"pc", "Ordinateur", "R\xC3\xA9seau et informatique",
         "ordinateur, PC, poste, unit\xC3\xA9 centrale",
         true, true, 64, 64,
         "<svg xmlns=\"http://www.w3.org/2000/svg\" width=\"64\" height=\"64\" viewBox=\"0 0 24 24\">"
         "<g stroke-linecap=\"round\" stroke-linejoin=\"round\">"
         "<rect x=\"14.5\" y=\"3.5\" width=\"7\" height=\"17\" rx=\"1\" fill=\"none\" stroke=\"#E6EAF0\" stroke-width=\"2\"/>"
         "<path d=\"M16.5 7h3M16.5 9.5h3\" fill=\"none\" stroke=\"#E6EAF0\" stroke-width=\"1.4\"/>"
         "<rect x=\"2.5\" y=\"5\" width=\"10\" height=\"8\" rx=\"1\" fill=\"none\" stroke=\"#E6EAF0\" stroke-width=\"2\"/>"
         "<path d=\"M7.5 13v3.5M5 16.5h5\" fill=\"none\" stroke=\"#E6EAF0\" stroke-width=\"2\"/></g></svg>"},
        // ---- portable
        {"portable", "Ordinateur portable", "R\xC3\xA9seau et informatique",
         "portable, ordinateur, laptop",
         true, true, 64, 64,
         "<svg xmlns=\"http://www.w3.org/2000/svg\" width=\"64\" height=\"64\" viewBox=\"0 0 24 24\">"
         "<g stroke-linecap=\"round\" stroke-linejoin=\"round\">"
         "<rect x=\"4.5\" y=\"4.5\" width=\"15\" height=\"10.5\" rx=\"1.2\" fill=\"none\" stroke=\"#E6EAF0\" stroke-width=\"2\"/>"
         "<path d=\"M2 18.5h20l-1.5-3.5H3.5z\" fill=\"none\" stroke=\"#E6EAF0\" stroke-width=\"2\"/></g></svg>"},
        // ---- serveur
        {"serveur", "Serveur", "R\xC3\xA9seau et informatique",
         "serveur, baie, informatique, donn\xC3\xA9" "es",
         true, true, 64, 64,
         "<svg xmlns=\"http://www.w3.org/2000/svg\" width=\"64\" height=\"64\" viewBox=\"0 0 24 24\">"
         "<g stroke-linecap=\"round\" stroke-linejoin=\"round\">"
         "<rect x=\"4\" y=\"3\" width=\"16\" height=\"5.2\" rx=\"1\" fill=\"none\" stroke=\"#E6EAF0\" stroke-width=\"2\"/>"
         "<rect x=\"4\" y=\"9.4\" width=\"16\" height=\"5.2\" rx=\"1\" fill=\"none\" stroke=\"#E6EAF0\" stroke-width=\"2\"/>"
         "<rect x=\"4\" y=\"15.8\" width=\"16\" height=\"5.2\" rx=\"1\" fill=\"none\" stroke=\"#E6EAF0\" stroke-width=\"2\"/>"
         "<circle cx=\"7\" cy=\"5.6\" r=\"0.9\" fill=\"#E6EAF0\"/>"
         "<circle cx=\"7\" cy=\"12\" r=\"0.9\" fill=\"#E6EAF0\"/>"
         "<circle cx=\"7\" cy=\"18.4\" r=\"0.9\" fill=\"#E6EAF0\"/>"
         "<path d=\"M11 5.6h6M11 12h6M11 18.4h6\" fill=\"none\" stroke=\"#E6EAF0\" stroke-width=\"1.4\"/></g></svg>"},
        // ---- reseau
        {"reseau", "R\xC3\xA9seau", "R\xC3\xA9seau et informatique",
         "r\xC3\xA9seau, n\xC5\x93uds, liaison, topologie",
         true, true, 64, 64,
         "<svg xmlns=\"http://www.w3.org/2000/svg\" width=\"64\" height=\"64\" viewBox=\"0 0 24 24\">"
         "<g stroke-linecap=\"round\" stroke-linejoin=\"round\">"
         "<circle cx=\"12\" cy=\"5\" r=\"2.5\" fill=\"none\" stroke=\"#E6EAF0\" stroke-width=\"2\"/>"
         "<circle cx=\"5\" cy=\"18.5\" r=\"2.5\" fill=\"none\" stroke=\"#E6EAF0\" stroke-width=\"2\"/>"
         "<circle cx=\"19\" cy=\"18.5\" r=\"2.5\" fill=\"none\" stroke=\"#E6EAF0\" stroke-width=\"2\"/>"
         "<path d=\"M10.8 7.2L6.2 16.3M13.2 7.2l4.6 9.1M7.5 18.5h9\" fill=\"none\" stroke=\"#E6EAF0\" stroke-width=\"2\"/>"
         "</g></svg>"},
        // ---- commutateur
        {"commutateur", "Commutateur", "R\xC3\xA9seau et informatique",
         "commutateur, switch, r\xC3\xA9seau, ports",
         true, true, 64, 64,
         "<svg xmlns=\"http://www.w3.org/2000/svg\" width=\"64\" height=\"64\" viewBox=\"0 0 24 24\">"
         "<g stroke-linecap=\"round\" stroke-linejoin=\"round\">"
         "<rect x=\"2\" y=\"8.5\" width=\"20\" height=\"8\" rx=\"1.5\" fill=\"none\" stroke=\"#E6EAF0\" stroke-width=\"2\"/>"
         "<rect x=\"4.5\" y=\"11\" width=\"2.2\" height=\"2.2\" rx=\"0.3\" fill=\"#E6EAF0\"/>"
         "<rect x=\"8\" y=\"11\" width=\"2.2\" height=\"2.2\" rx=\"0.3\" fill=\"#E6EAF0\"/>"
         "<rect x=\"11.5\" y=\"11\" width=\"2.2\" height=\"2.2\" rx=\"0.3\" fill=\"#E6EAF0\"/>"
         "<rect x=\"15\" y=\"11\" width=\"2.2\" height=\"2.2\" rx=\"0.3\" fill=\"#E6EAF0\"/>"
         "<circle cx=\"19.2\" cy=\"12.1\" r=\"0.9\" fill=\"#E6EAF0\"/></g></svg>"},
        // ---- wifi
        {"wifi", "Wi-Fi", "R\xC3\xA9seau et informatique",
         "wifi, sans fil, r\xC3\xA9seau, signal",
         true, true, 64, 64,
         "<svg xmlns=\"http://www.w3.org/2000/svg\" width=\"64\" height=\"64\" viewBox=\"0 0 24 24\">"
         "<g stroke-linecap=\"round\" stroke-linejoin=\"round\">"
         "<path d=\"M2.5 9a13.5 13.5 0 0 1 19 0\" fill=\"none\" stroke=\"#E6EAF0\" stroke-width=\"2\"/>"
         "<path d=\"M5.6 12.6a9 9 0 0 1 12.8 0\" fill=\"none\" stroke=\"#E6EAF0\" stroke-width=\"2\"/>"
         "<path d=\"M8.8 16.1a4.5 4.5 0 0 1 6.4 0\" fill=\"none\" stroke=\"#E6EAF0\" stroke-width=\"2\"/>"
         "<circle cx=\"12\" cy=\"19.5\" r=\"1.6\" fill=\"#E6EAF0\"/></g></svg>"},
        // ---- ethernet
        {"ethernet", "Prise r\xC3\xA9seau", "R\xC3\xA9seau et informatique",
         "ethernet, RJ45, prise, c\xC3\xA2" "ble, r\xC3\xA9seau",
         true, true, 64, 64,
         "<svg xmlns=\"http://www.w3.org/2000/svg\" width=\"64\" height=\"64\" viewBox=\"0 0 24 24\">"
         "<g stroke-linecap=\"round\" stroke-linejoin=\"round\">"
         "<rect x=\"3.5\" y=\"4\" width=\"17\" height=\"16\" rx=\"2\" fill=\"none\" stroke=\"#E6EAF0\" stroke-width=\"2\"/>"
         "<path d=\"M7.5 16.5V11H10V8.5h4V11h2.5v5.5z\" fill=\"none\" stroke=\"#E6EAF0\" stroke-width=\"2\"/>"
         "<path d=\"M10 16.5v-2M12 16.5v-2M14 16.5v-2\" fill=\"none\" stroke=\"#E6EAF0\" stroke-width=\"1.2\"/></g>"
         "</svg>"},
        // ---- antenne
        {"antenne", "Antenne", "R\xC3\xA9seau et informatique",
         "antenne, radio, \xC3\xA9metteur, ondes",
         true, true, 64, 64,
         "<svg xmlns=\"http://www.w3.org/2000/svg\" width=\"64\" height=\"64\" viewBox=\"0 0 24 24\">"
         "<g stroke-linecap=\"round\" stroke-linejoin=\"round\">"
         "<path d=\"M12 10.5V21M8.5 21h7\" fill=\"none\" stroke=\"#E6EAF0\" stroke-width=\"2\"/>"
         "<circle cx=\"12\" cy=\"9\" r=\"1.7\" fill=\"#E6EAF0\"/>"
         "<path d=\"M8.6 5.6a4.8 4.8 0 0 0 0 6.8M15.4 5.6a4.8 4.8 0 0 1 0 6.8\" fill=\"none\" stroke=\"#E6EAF0\" stroke-width=\"2\"/>"
         "<path d=\"M5.6 2.6a9 9 0 0 0 0 12.8M18.4 2.6a9 9 0 0 1 0 12.8\" fill=\"none\" stroke=\"#E6EAF0\" stroke-width=\"2\"/>"
         "</g></svg>"},
        // ---- nuage-informatique
        {"nuage-informatique", "Nuage", "R\xC3\xA9seau et informatique",
         "nuage, cloud, internet, en ligne",
         true, true, 64, 64,
         "<svg xmlns=\"http://www.w3.org/2000/svg\" width=\"64\" height=\"64\" viewBox=\"0 0 24 24\">"
         "<g stroke-linecap=\"round\" stroke-linejoin=\"round\">"
         "<path d=\"M7 18.5a4.5 4.5 0 0 1-.4-9 5.8 5.8 0 0 1 11.2 1.2 3.9 3.9 0 0 1 .2 7.8z\" fill=\"none\" stroke=\"#E6EAF0\" stroke-width=\"2\"/>"
         "</g></svg>"},
        // ---- nuage-envoi
        {"nuage-envoi", "Envoi vers le nuage", "R\xC3\xA9seau et informatique",
         "nuage, envoi, synchroniser, cloud",
         true, true, 64, 64,
         "<svg xmlns=\"http://www.w3.org/2000/svg\" width=\"64\" height=\"64\" viewBox=\"0 0 24 24\">"
         "<g stroke-linecap=\"round\" stroke-linejoin=\"round\">"
         "<path d=\"M8 18.5H7a4.5 4.5 0 0 1-.4-9 5.8 5.8 0 0 1 11.2 1.2 3.9 3.9 0 0 1 .2 7.8h-2\" fill=\"none\" stroke=\"#E6EAF0\" stroke-width=\"2\"/>"
         "<path d=\"M12 21L12 11.5M14.25 14.181L12 11.5L9.75 14.181\" fill=\"none\" stroke=\"#E6EAF0\" stroke-width=\"2\"/>"
         "</g></svg>"},
        // ---- base-de-donnees
        {"base-de-donnees", "Base de donn\xC3\xA9" "es", "R\xC3\xA9seau et informatique",
         "base de donn\xC3\xA9" "es, SQL, stockage, donn\xC3\xA9" "es",
         true, true, 64, 64,
         "<svg xmlns=\"http://www.w3.org/2000/svg\" width=\"64\" height=\"64\" viewBox=\"0 0 24 24\">"
         "<g stroke-linecap=\"round\" stroke-linejoin=\"round\">"
         "<ellipse cx=\"12\" cy=\"5.5\" rx=\"7.5\" ry=\"2.8\" fill=\"none\" stroke=\"#E6EAF0\" stroke-width=\"2\"/>"
         "<path d=\"M4.5 5.5v13c0 1.5 3.4 2.8 7.5 2.8s7.5-1.3 7.5-2.8v-13\" fill=\"none\" stroke=\"#E6EAF0\" stroke-width=\"2\"/>"
         "<path d=\"M4.5 12c0 1.5 3.4 2.8 7.5 2.8s7.5-1.3 7.5-2.8\" fill=\"none\" stroke=\"#E6EAF0\" stroke-width=\"2\"/>"
         "</g></svg>"},
        // ---- cle-usb
        {"cle-usb", "Cl\xC3\xA9 USB", "R\xC3\xA9seau et informatique",
         "USB, cl\xC3\xA9, m\xC3\xA9moire, stockage",
         true, true, 64, 64,
         "<svg xmlns=\"http://www.w3.org/2000/svg\" width=\"64\" height=\"64\" viewBox=\"0 0 24 24\">"
         "<g stroke-linecap=\"round\" stroke-linejoin=\"round\">"
         "<rect x=\"6.5\" y=\"9\" width=\"11\" height=\"12.5\" rx=\"2\" fill=\"none\" stroke=\"#E6EAF0\" stroke-width=\"2\"/>"
         "<rect x=\"8.5\" y=\"3\" width=\"7\" height=\"6\" rx=\"0.8\" fill=\"none\" stroke=\"#E6EAF0\" stroke-width=\"2\"/>"
         "<rect x=\"10\" y=\"5\" width=\"1.4\" height=\"1.4\" fill=\"#E6EAF0\"/>"
         "<rect x=\"12.6\" y=\"5\" width=\"1.4\" height=\"1.4\" fill=\"#E6EAF0\"/></g></svg>"},
        // ---- carte-sd
        {"carte-sd", "Carte m\xC3\xA9moire", "R\xC3\xA9seau et informatique",
         "carte, SD, m\xC3\xA9moire, stockage",
         true, true, 64, 64,
         "<svg xmlns=\"http://www.w3.org/2000/svg\" width=\"64\" height=\"64\" viewBox=\"0 0 24 24\">"
         "<g stroke-linecap=\"round\" stroke-linejoin=\"round\">"
         "<path d=\"M7.5 3h10a1 1 0 0 1 1 1v16a1 1 0 0 1-1 1h-12a1 1 0 0 1-1-1V8z\" fill=\"none\" stroke=\"#E6EAF0\" stroke-width=\"2\"/>"
         "<path d=\"M9 3.5v3M11.5 3.5v3M14 3.5v3M16.5 3.5v3\" fill=\"none\" stroke=\"#E6EAF0\" stroke-width=\"1.3\"/>"
         "</g></svg>"},
        // ---- code
        {"code", "Code", "R\xC3\xA9seau et informatique",
         "code, programme, script, d\xC3\xA9veloppement",
         true, true, 64, 64,
         "<svg xmlns=\"http://www.w3.org/2000/svg\" width=\"64\" height=\"64\" viewBox=\"0 0 24 24\">"
         "<g stroke-linecap=\"round\" stroke-linejoin=\"round\">"
         "<path d=\"M8 7l-5 5 5 5M16 7l5 5-5 5\" fill=\"none\" stroke=\"#E6EAF0\" stroke-width=\"2\"/>"
         "<path d=\"M13.5 4.5l-3 15\" fill=\"none\" stroke=\"#E6EAF0\" stroke-width=\"2\"/></g></svg>"},
        // ---- terminal
        {"terminal", "Terminal", "R\xC3\xA9seau et informatique",
         "terminal, console, commande, invite",
         true, true, 64, 64,
         "<svg xmlns=\"http://www.w3.org/2000/svg\" width=\"64\" height=\"64\" viewBox=\"0 0 24 24\">"
         "<g stroke-linecap=\"round\" stroke-linejoin=\"round\">"
         "<rect x=\"2.5\" y=\"4\" width=\"19\" height=\"16\" rx=\"2\" fill=\"none\" stroke=\"#E6EAF0\" stroke-width=\"2\"/>"
         "<path d=\"M6.5 9l3 3-3 3M11.5 15h6\" fill=\"none\" stroke=\"#E6EAF0\" stroke-width=\"2\"/></g></svg>"},
        // ---- bus-terrain
        {"bus-terrain", "Bus de terrain", "R\xC3\xA9seau et informatique",
         "bus, terrain, Modbus, Profibus, r\xC3\xA9seau",
         true, true, 64, 64,
         "<svg xmlns=\"http://www.w3.org/2000/svg\" width=\"64\" height=\"64\" viewBox=\"0 0 24 24\">"
         "<g stroke-linecap=\"round\" stroke-linejoin=\"round\">"
         "<path d=\"M2 6.5h20\" fill=\"none\" stroke=\"#E6EAF0\" stroke-width=\"2\"/>"
         "<path d=\"M5 6.5V12M12 6.5V12M19 6.5V12\" fill=\"none\" stroke=\"#E6EAF0\" stroke-width=\"2\"/>"
         "<rect x=\"3\" y=\"12\" width=\"4\" height=\"5\" rx=\"1\" fill=\"none\" stroke=\"#E6EAF0\" stroke-width=\"2\"/>"
         "<rect x=\"10\" y=\"12\" width=\"4\" height=\"5\" rx=\"1\" fill=\"none\" stroke=\"#E6EAF0\" stroke-width=\"2\"/>"
         "<rect x=\"17\" y=\"12\" width=\"4\" height=\"5\" rx=\"1\" fill=\"none\" stroke=\"#E6EAF0\" stroke-width=\"2\"/>"
         "<path d=\"M2 4.5v4M22 4.5v4\" fill=\"none\" stroke=\"#E6EAF0\" stroke-width=\"2\"/></g></svg>"},
        // ---- smartphone
        {"smartphone", "Smartphone", "R\xC3\xA9seau et informatique",
         "smartphone, mobile, t\xC3\xA9l\xC3\xA9phone portable",
         true, true, 64, 64,
         "<svg xmlns=\"http://www.w3.org/2000/svg\" width=\"64\" height=\"64\" viewBox=\"0 0 24 24\">"
         "<g stroke-linecap=\"round\" stroke-linejoin=\"round\">"
         "<rect x=\"6\" y=\"2.5\" width=\"12\" height=\"19\" rx=\"2.5\" fill=\"none\" stroke=\"#E6EAF0\" stroke-width=\"2\"/>"
         "<path d=\"M10.5 18.5h3\" fill=\"none\" stroke=\"#E6EAF0\" stroke-width=\"2\"/></g></svg>"},
        // ---- tablette
        {"tablette", "Tablette", "R\xC3\xA9seau et informatique",
         "tablette, \xC3\xA9" "cran tactile, mobile",
         true, true, 64, 64,
         "<svg xmlns=\"http://www.w3.org/2000/svg\" width=\"64\" height=\"64\" viewBox=\"0 0 24 24\">"
         "<g stroke-linecap=\"round\" stroke-linejoin=\"round\">"
         "<rect x=\"3\" y=\"4.5\" width=\"18\" height=\"15\" rx=\"2\" fill=\"none\" stroke=\"#E6EAF0\" stroke-width=\"2\"/>"
         "<circle cx=\"18.6\" cy=\"12\" r=\"0.9\" fill=\"#E6EAF0\"/></g></svg>"},
        // ---- routeur
        {"routeur", "Routeur", "R\xC3\xA9seau et informatique",
         "routeur, box, passerelle, internet",
         true, true, 64, 64,
         "<svg xmlns=\"http://www.w3.org/2000/svg\" width=\"64\" height=\"64\" viewBox=\"0 0 24 24\">"
         "<g stroke-linecap=\"round\" stroke-linejoin=\"round\">"
         "<rect x=\"2.5\" y=\"12.5\" width=\"19\" height=\"6.5\" rx=\"1.5\" fill=\"none\" stroke=\"#E6EAF0\" stroke-width=\"2\"/>"
         "<path d=\"M7 12.5l-2-6M17 12.5l2-6\" fill=\"none\" stroke=\"#E6EAF0\" stroke-width=\"2\"/>"
         "<circle cx=\"6.5\" cy=\"15.8\" r=\"0.9\" fill=\"#E6EAF0\"/>"
         "<circle cx=\"9.5\" cy=\"15.8\" r=\"0.9\" fill=\"#E6EAF0\"/>"
         "<path d=\"M13 15.8h5\" fill=\"none\" stroke=\"#E6EAF0\" stroke-width=\"1.4\"/></g></svg>"},
        // ---- camion
        {"camion", "Camion", "V\xC3\xA9hicules et logistique",
         "camion, transport, livraison, poids lourd",
         true, true, 64, 64,
         "<svg xmlns=\"http://www.w3.org/2000/svg\" width=\"64\" height=\"64\" viewBox=\"0 0 24 24\">"
         "<g stroke-linecap=\"round\" stroke-linejoin=\"round\">"
         "<rect x=\"2\" y=\"5.5\" width=\"12\" height=\"10.5\" rx=\"1\" fill=\"none\" stroke=\"#E6EAF0\" stroke-width=\"2\"/>"
         "<path d=\"M14 9h4.2l3.3 3.8V16H14z\" fill=\"none\" stroke=\"#E6EAF0\" stroke-width=\"2\"/>"
         "<circle cx=\"6\" cy=\"18\" r=\"2.2\" fill=\"none\" stroke=\"#E6EAF0\" stroke-width=\"2\"/>"
         "<circle cx=\"17.5\" cy=\"18\" r=\"2.2\" fill=\"none\" stroke=\"#E6EAF0\" stroke-width=\"2\"/></g></svg>"},
        // ---- chariot-elevateur
        {"chariot-elevateur", "Chariot \xC3\xA9l\xC3\xA9vateur", "V\xC3\xA9hicules et logistique",
         "chariot, \xC3\xA9l\xC3\xA9vateur, fenwick, manutention",
         true, true, 64, 64,
         "<svg xmlns=\"http://www.w3.org/2000/svg\" width=\"64\" height=\"64\" viewBox=\"0 0 24 24\">"
         "<g stroke-linecap=\"round\" stroke-linejoin=\"round\">"
         "<path d=\"M2.5 16.5V12a1 1 0 0 1 1-1h8.8l1.7 2.6v2.9z\" fill=\"#E6EAF0\"/>"
         "<path d=\"M5.2 11V5.2h5l2.3 5.8\" fill=\"none\" stroke=\"#E6EAF0\" stroke-width=\"2\"/>"
         "<path d=\"M15.6 3v15.8H22\" fill=\"none\" stroke=\"#E6EAF0\" stroke-width=\"2\"/>"
         "<circle cx=\"6\" cy=\"18.6\" r=\"2.4\" fill=\"#E6EAF0\"/>"
         "<circle cx=\"12.4\" cy=\"18.6\" r=\"2.4\" fill=\"#E6EAF0\"/></g></svg>"},
        // ---- voiture
        {"voiture", "Voiture", "V\xC3\xA9hicules et logistique",
         "voiture, v\xC3\xA9hicule, automobile",
         true, true, 64, 64,
         "<svg xmlns=\"http://www.w3.org/2000/svg\" width=\"64\" height=\"64\" viewBox=\"0 0 24 24\">"
         "<g stroke-linecap=\"round\" stroke-linejoin=\"round\">"
         "<path d=\"M2.5 16.5v-3.5l2.4-.8L7.5 7.5h8.5l3.5 4.7 2 .8v3.5z\" fill=\"none\" stroke=\"#E6EAF0\" stroke-width=\"2\"/>"
         "<circle cx=\"7\" cy=\"17\" r=\"2.2\" fill=\"none\" stroke=\"#E6EAF0\" stroke-width=\"2\"/>"
         "<circle cx=\"17\" cy=\"17\" r=\"2.2\" fill=\"none\" stroke=\"#E6EAF0\" stroke-width=\"2\"/>"
         "<path d=\"M10 7.5v4.7h9.5\" fill=\"none\" stroke=\"#E6EAF0\" stroke-width=\"1.4\"/></g></svg>"},
        // ---- palette
        {"palette", "Palette", "V\xC3\xA9hicules et logistique",
         "palette, manutention, stockage, charge",
         true, true, 64, 64,
         "<svg xmlns=\"http://www.w3.org/2000/svg\" width=\"64\" height=\"64\" viewBox=\"0 0 24 24\">"
         "<g stroke-linecap=\"round\" stroke-linejoin=\"round\">"
         "<rect x=\"2.5\" y=\"11.5\" width=\"19\" height=\"3\" rx=\"0.5\" fill=\"none\" stroke=\"#E6EAF0\" stroke-width=\"2\"/>"
         "<rect x=\"4\" y=\"14.5\" width=\"3\" height=\"4.5\" fill=\"#E6EAF0\"/>"
         "<rect x=\"10.5\" y=\"14.5\" width=\"3\" height=\"4.5\" fill=\"#E6EAF0\"/>"
         "<rect x=\"17\" y=\"14.5\" width=\"3\" height=\"4.5\" fill=\"#E6EAF0\"/>"
         "<rect x=\"2.5\" y=\"19\" width=\"19\" height=\"2.5\" rx=\"0.5\" fill=\"none\" stroke=\"#E6EAF0\" stroke-width=\"2\"/>"
         "</g></svg>"},
        // ---- colis
        {"colis", "Colis", "V\xC3\xA9hicules et logistique",
         "colis, carton, paquet, exp\xC3\xA9" "dition",
         true, true, 64, 64,
         "<svg xmlns=\"http://www.w3.org/2000/svg\" width=\"64\" height=\"64\" viewBox=\"0 0 24 24\">"
         "<g stroke-linecap=\"round\" stroke-linejoin=\"round\">"
         "<path d=\"M12 3l8.5 4.3v9.4L12 21l-8.5-4.3V7.3z\" fill=\"none\" stroke=\"#E6EAF0\" stroke-width=\"2\"/>"
         "<path d=\"M3.5 7.3L12 11.6l8.5-4.3M12 11.6V21\" fill=\"none\" stroke=\"#E6EAF0\" stroke-width=\"2\"/>"
         "<path d=\"M7.8 5.1l8.5 4.3\" fill=\"none\" stroke=\"#E6EAF0\" stroke-width=\"1.5\"/></g></svg>"},
        // ---- grue
        {"grue", "Grue", "V\xC3\xA9hicules et logistique",
         "grue, levage, chantier, charge",
         true, true, 64, 64,
         "<svg xmlns=\"http://www.w3.org/2000/svg\" width=\"64\" height=\"64\" viewBox=\"0 0 24 24\">"
         "<g stroke-linecap=\"round\" stroke-linejoin=\"round\">"
         "<path d=\"M6.5 21.5V5\" fill=\"none\" stroke=\"#E6EAF0\" stroke-width=\"2\"/>"
         "<path d=\"M2.5 5h19\" fill=\"none\" stroke=\"#E6EAF0\" stroke-width=\"2\"/>"
         "<path d=\"M4.5 5l2-3 2 3\" fill=\"none\" stroke=\"#E6EAF0\" stroke-width=\"2\"/>"
         "<path d=\"M17.5 5v7\" fill=\"none\" stroke=\"#E6EAF0\" stroke-width=\"2\"/>"
         "<path d=\"M17.5 12v1.5a1.8 1.8 0 1 1-1.8 1.8\" fill=\"none\" stroke=\"#E6EAF0\" stroke-width=\"2\"/>"
         "<path d=\"M3.5 21.5h6\" fill=\"none\" stroke=\"#E6EAF0\" stroke-width=\"2\"/>"
         "<rect x=\"2.5\" y=\"5.5\" width=\"3\" height=\"2.5\" fill=\"#E6EAF0\"/></g></svg>"},
        // ---- transpalette
        {"transpalette", "Transpalette", "V\xC3\xA9hicules et logistique",
         "transpalette, manutention, palette",
         true, true, 64, 64,
         "<svg xmlns=\"http://www.w3.org/2000/svg\" width=\"64\" height=\"64\" viewBox=\"0 0 24 24\">"
         "<g stroke-linecap=\"round\" stroke-linejoin=\"round\">"
         "<path d=\"M4 3l3.5 13.5\" fill=\"none\" stroke=\"#E6EAF0\" stroke-width=\"2\"/>"
         "<path d=\"M2.5 3h3\" fill=\"none\" stroke=\"#E6EAF0\" stroke-width=\"2\"/>"
         "<path d=\"M7.5 16.5H21.5\" fill=\"none\" stroke=\"#E6EAF0\" stroke-width=\"2\"/>"
         "<circle cx=\"7.5\" cy=\"19\" r=\"2\" fill=\"none\" stroke=\"#E6EAF0\" stroke-width=\"2\"/>"
         "<circle cx=\"19.5\" cy=\"19.2\" r=\"1.2\" fill=\"none\" stroke=\"#E6EAF0\" stroke-width=\"2\"/></g></svg>"},
        // ---- caddie
        {"caddie", "Chariot", "V\xC3\xA9hicules et logistique",
         "chariot, caddie, achat, approvisionnement",
         true, true, 64, 64,
         "<svg xmlns=\"http://www.w3.org/2000/svg\" width=\"64\" height=\"64\" viewBox=\"0 0 24 24\">"
         "<g stroke-linecap=\"round\" stroke-linejoin=\"round\">"
         "<path d=\"M2.5 4h3l2.6 11h10.5l2-7.5H6.5\" fill=\"none\" stroke=\"#E6EAF0\" stroke-width=\"2\"/>"
         "<circle cx=\"9.5\" cy=\"19\" r=\"1.7\" fill=\"none\" stroke=\"#E6EAF0\" stroke-width=\"2\"/>"
         "<circle cx=\"17\" cy=\"19\" r=\"1.7\" fill=\"none\" stroke=\"#E6EAF0\" stroke-width=\"2\"/></g></svg>"},
        // ---- conteneur
        {"conteneur", "Conteneur", "V\xC3\xA9hicules et logistique",
         "conteneur, container, transport, maritime",
         true, true, 64, 64,
         "<svg xmlns=\"http://www.w3.org/2000/svg\" width=\"64\" height=\"64\" viewBox=\"0 0 24 24\">"
         "<g stroke-linecap=\"round\" stroke-linejoin=\"round\">"
         "<rect x=\"2\" y=\"6.5\" width=\"20\" height=\"12\" rx=\"1\" fill=\"none\" stroke=\"#E6EAF0\" stroke-width=\"2\"/>"
         "<path d=\"M6 6.5v12M10 6.5v12M14 6.5v12M18 6.5v12\" fill=\"none\" stroke=\"#E6EAF0\" stroke-width=\"1.4\"/>"
         "</g></svg>"},
        // ---- rayonnage
        {"rayonnage", "Rayonnage", "V\xC3\xA9hicules et logistique",
         "rayonnage, \xC3\xA9tag\xC3\xA8re, stock, magasin",
         true, true, 64, 64,
         "<svg xmlns=\"http://www.w3.org/2000/svg\" width=\"64\" height=\"64\" viewBox=\"0 0 24 24\">"
         "<g stroke-linecap=\"round\" stroke-linejoin=\"round\">"
         "<path d=\"M4 2.5v19M20 2.5v19\" fill=\"none\" stroke=\"#E6EAF0\" stroke-width=\"2\"/>"
         "<path d=\"M4 8.5h16M4 15h16M4 21h16\" fill=\"none\" stroke=\"#E6EAF0\" stroke-width=\"2\"/>"
         "<rect x=\"6\" y=\"4.5\" width=\"4\" height=\"4\" rx=\"0.4\" fill=\"none\" stroke=\"#E6EAF0\" stroke-width=\"2\"/>"
         "<rect x=\"11.5\" y=\"5.5\" width=\"3.5\" height=\"3\" rx=\"0.4\" fill=\"none\" stroke=\"#E6EAF0\" stroke-width=\"2\"/>"
         "<rect x=\"6.5\" y=\"11\" width=\"6\" height=\"4\" rx=\"0.4\" fill=\"none\" stroke=\"#E6EAF0\" stroke-width=\"2\"/>"
         "<rect x=\"15\" y=\"17.5\" width=\"3.5\" height=\"3.5\" rx=\"0.4\" fill=\"none\" stroke=\"#E6EAF0\" stroke-width=\"2\"/>"
         "</g></svg>"},
        // ---- bateau
        {"bateau", "Bateau", "V\xC3\xA9hicules et logistique",
         "bateau, navire, maritime, port",
         true, true, 64, 64,
         "<svg xmlns=\"http://www.w3.org/2000/svg\" width=\"64\" height=\"64\" viewBox=\"0 0 24 24\">"
         "<g stroke-linecap=\"round\" stroke-linejoin=\"round\">"
         "<path d=\"M2.5 14.5h19l-2.8 5.5H5.3z\" fill=\"none\" stroke=\"#E6EAF0\" stroke-width=\"2\"/>"
         "<rect x=\"6\" y=\"9\" width=\"5\" height=\"5.5\" rx=\"0.5\" fill=\"none\" stroke=\"#E6EAF0\" stroke-width=\"2\"/>"
         "<rect x=\"11\" y=\"10.5\" width=\"5\" height=\"4\" rx=\"0.5\" fill=\"none\" stroke=\"#E6EAF0\" stroke-width=\"2\"/>"
         "<path d=\"M8.5 9V5.5\" fill=\"none\" stroke=\"#E6EAF0\" stroke-width=\"2\"/></g></svg>"},
        // ---- train
        {"train", "Train", "V\xC3\xA9hicules et logistique",
         "train, ferroviaire, rail, transport",
         true, true, 64, 64,
         "<svg xmlns=\"http://www.w3.org/2000/svg\" width=\"64\" height=\"64\" viewBox=\"0 0 24 24\">"
         "<g stroke-linecap=\"round\" stroke-linejoin=\"round\">"
         "<rect x=\"5\" y=\"3\" width=\"14\" height=\"14\" rx=\"3\" fill=\"none\" stroke=\"#E6EAF0\" stroke-width=\"2\"/>"
         "<path d=\"M5 10h14\" fill=\"none\" stroke=\"#E6EAF0\" stroke-width=\"2\"/>"
         "<circle cx=\"8.5\" cy=\"13.5\" r=\"1.1\" fill=\"#E6EAF0\"/>"
         "<circle cx=\"15.5\" cy=\"13.5\" r=\"1.1\" fill=\"#E6EAF0\"/>"
         "<path d=\"M7.5 17L5 21.5M16.5 17l2.5 4.5\" fill=\"none\" stroke=\"#E6EAF0\" stroke-width=\"2\"/></g></svg>"},
        // ---- soleil
        {"soleil", "Soleil", "M\xC3\xA9t\xC3\xA9o",
         "soleil, beau temps, ensoleill\xC3\xA9, jour",
         false, false, 64, 64,
         "<svg xmlns=\"http://www.w3.org/2000/svg\" width=\"64\" height=\"64\" viewBox=\"0 0 24 24\">"
         "<g stroke-linecap=\"round\" stroke-linejoin=\"round\">"
         "<circle cx=\"12\" cy=\"12\" r=\"4.6\" fill=\"#F5C400\"/><g transform=\"rotate(0 12 12)\">"
         "<path d=\"M12 2.5V5\" fill=\"none\" stroke=\"#F5C400\" stroke-width=\"2\"/></g>"
         "<g transform=\"rotate(45 12 12)\">"
         "<path d=\"M12 2.5V5\" fill=\"none\" stroke=\"#F5C400\" stroke-width=\"2\"/></g>"
         "<g transform=\"rotate(90 12 12)\">"
         "<path d=\"M12 2.5V5\" fill=\"none\" stroke=\"#F5C400\" stroke-width=\"2\"/></g>"
         "<g transform=\"rotate(135 12 12)\">"
         "<path d=\"M12 2.5V5\" fill=\"none\" stroke=\"#F5C400\" stroke-width=\"2\"/></g>"
         "<g transform=\"rotate(180 12 12)\">"
         "<path d=\"M12 2.5V5\" fill=\"none\" stroke=\"#F5C400\" stroke-width=\"2\"/></g>"
         "<g transform=\"rotate(225 12 12)\">"
         "<path d=\"M12 2.5V5\" fill=\"none\" stroke=\"#F5C400\" stroke-width=\"2\"/></g>"
         "<g transform=\"rotate(270 12 12)\">"
         "<path d=\"M12 2.5V5\" fill=\"none\" stroke=\"#F5C400\" stroke-width=\"2\"/></g>"
         "<g transform=\"rotate(315 12 12)\">"
         "<path d=\"M12 2.5V5\" fill=\"none\" stroke=\"#F5C400\" stroke-width=\"2\"/></g></g></svg>"},
        // ---- nuage
        {"nuage", "Nuage", "M\xC3\xA9t\xC3\xA9o",
         "nuage, nuageux, couvert",
         false, false, 64, 64,
         "<svg xmlns=\"http://www.w3.org/2000/svg\" width=\"64\" height=\"64\" viewBox=\"0 0 24 24\">"
         "<g stroke-linecap=\"round\" stroke-linejoin=\"round\"><g transform=\"translate(0 0) scale(1)\">"
         "<path d=\"M7 18.5a4.5 4.5 0 0 1-.4-9 5.8 5.8 0 0 1 11.2 1.2 3.9 3.9 0 0 1 .2 7.8z\" fill=\"#D5DCE6\" stroke=\"#8A94A6\" stroke-width=\"1\"/>"
         "</g></g></svg>"},
        // ---- soleil-nuage
        {"soleil-nuage", "\xC3\x89" "claircies", "M\xC3\xA9t\xC3\xA9o",
         "\xC3\xA9" "claircies, soleil, nuage, variable",
         false, false, 64, 64,
         "<svg xmlns=\"http://www.w3.org/2000/svg\" width=\"64\" height=\"64\" viewBox=\"0 0 24 24\">"
         "<g stroke-linecap=\"round\" stroke-linejoin=\"round\"><g transform=\"translate(-2.5 -2.5) scale(0.8)\">"
         "<circle cx=\"12\" cy=\"12\" r=\"4.6\" fill=\"#F5C400\"/><g transform=\"rotate(0 12 12)\">"
         "<path d=\"M12 2.5V5\" fill=\"none\" stroke=\"#F5C400\" stroke-width=\"2\"/></g>"
         "<g transform=\"rotate(45 12 12)\">"
         "<path d=\"M12 2.5V5\" fill=\"none\" stroke=\"#F5C400\" stroke-width=\"2\"/></g>"
         "<g transform=\"rotate(90 12 12)\">"
         "<path d=\"M12 2.5V5\" fill=\"none\" stroke=\"#F5C400\" stroke-width=\"2\"/></g>"
         "<g transform=\"rotate(135 12 12)\">"
         "<path d=\"M12 2.5V5\" fill=\"none\" stroke=\"#F5C400\" stroke-width=\"2\"/></g>"
         "<g transform=\"rotate(180 12 12)\">"
         "<path d=\"M12 2.5V5\" fill=\"none\" stroke=\"#F5C400\" stroke-width=\"2\"/></g>"
         "<g transform=\"rotate(225 12 12)\">"
         "<path d=\"M12 2.5V5\" fill=\"none\" stroke=\"#F5C400\" stroke-width=\"2\"/></g>"
         "<g transform=\"rotate(270 12 12)\">"
         "<path d=\"M12 2.5V5\" fill=\"none\" stroke=\"#F5C400\" stroke-width=\"2\"/></g>"
         "<g transform=\"rotate(315 12 12)\">"
         "<path d=\"M12 2.5V5\" fill=\"none\" stroke=\"#F5C400\" stroke-width=\"2\"/></g></g>"
         "<g transform=\"translate(3 3.5) scale(0.85)\">"
         "<path d=\"M7 18.5a4.5 4.5 0 0 1-.4-9 5.8 5.8 0 0 1 11.2 1.2 3.9 3.9 0 0 1 .2 7.8z\" fill=\"#D5DCE6\" stroke=\"#8A94A6\" stroke-width=\"1\"/>"
         "</g></g></svg>"},
        // ---- pluie
        {"pluie", "Pluie", "M\xC3\xA9t\xC3\xA9o",
         "pluie, pluvieux, averse",
         false, false, 64, 64,
         "<svg xmlns=\"http://www.w3.org/2000/svg\" width=\"64\" height=\"64\" viewBox=\"0 0 24 24\">"
         "<g stroke-linecap=\"round\" stroke-linejoin=\"round\"><g transform=\"translate(0 -3) scale(1)\">"
         "<path d=\"M7 18.5a4.5 4.5 0 0 1-.4-9 5.8 5.8 0 0 1 11.2 1.2 3.9 3.9 0 0 1 .2 7.8z\" fill=\"#D5DCE6\" stroke=\"#8A94A6\" stroke-width=\"1\"/>"
         "</g>"
         "<path d=\"M8 17.5l-1.2 3.5M12 17.5l-1.2 3.5M16 17.5l-1.2 3.5\" fill=\"none\" stroke=\"#4FA3E0\" stroke-width=\"2\"/>"
         "</g></svg>"},
        // ---- neige
        {"neige", "Neige", "M\xC3\xA9t\xC3\xA9o",
         "neige, flocons, hiver, froid",
         false, false, 64, 64,
         "<svg xmlns=\"http://www.w3.org/2000/svg\" width=\"64\" height=\"64\" viewBox=\"0 0 24 24\">"
         "<g stroke-linecap=\"round\" stroke-linejoin=\"round\"><g transform=\"translate(0 -3) scale(1)\">"
         "<path d=\"M7 18.5a4.5 4.5 0 0 1-.4-9 5.8 5.8 0 0 1 11.2 1.2 3.9 3.9 0 0 1 .2 7.8z\" fill=\"#D5DCE6\" stroke=\"#8A94A6\" stroke-width=\"1\"/>"
         "</g><circle cx=\"7.5\" cy=\"18\" r=\"1.1\" fill=\"#A9D8F5\"/>"
         "<circle cx=\"12\" cy=\"19.5\" r=\"1.1\" fill=\"#A9D8F5\"/>"
         "<circle cx=\"16.5\" cy=\"18\" r=\"1.1\" fill=\"#A9D8F5\"/>"
         "<circle cx=\"9.8\" cy=\"21.8\" r=\"1.1\" fill=\"#A9D8F5\"/>"
         "<circle cx=\"14.3\" cy=\"21.8\" r=\"1.1\" fill=\"#A9D8F5\"/></g></svg>"},
        // ---- orage
        {"orage", "Orage", "M\xC3\xA9t\xC3\xA9o",
         "orage, \xC3\xA9" "clair, tonnerre, foudre",
         false, false, 64, 64,
         "<svg xmlns=\"http://www.w3.org/2000/svg\" width=\"64\" height=\"64\" viewBox=\"0 0 24 24\">"
         "<g stroke-linecap=\"round\" stroke-linejoin=\"round\"><g transform=\"translate(0 -3) scale(1)\">"
         "<path d=\"M7 18.5a4.5 4.5 0 0 1-.4-9 5.8 5.8 0 0 1 11.2 1.2 3.9 3.9 0 0 1 .2 7.8z\" fill=\"#8A94A6\" stroke=\"#5B6576\" stroke-width=\"1\"/>"
         "</g><path d=\"M12.8 13.5L9 19h3l-1 4.2 4.2-5.7h-3l1-4z\" fill=\"#F5C400\"/></g></svg>"},
        // ---- vent-meteo
        {"vent-meteo", "Vent fort", "M\xC3\xA9t\xC3\xA9o",
         "vent, rafales, temp\xC3\xAAte",
         false, false, 64, 64,
         "<svg xmlns=\"http://www.w3.org/2000/svg\" width=\"64\" height=\"64\" viewBox=\"0 0 24 24\">"
         "<g stroke-linecap=\"round\" stroke-linejoin=\"round\"><g transform=\"translate(0 0) scale(1)\">"
         "<path d=\"M3 8.5h10a2.8 2.8 0 1 0-2.8-2.8\" fill=\"none\" stroke=\"#4FA3E0\" stroke-width=\"2\"/>"
         "<path d=\"M3 15.5h13.5a2.8 2.8 0 1 1-2.8 2.8\" fill=\"none\" stroke=\"#4FA3E0\" stroke-width=\"2\"/>"
         "<path d=\"M3 12h16\" fill=\"none\" stroke=\"#4FA3E0\" stroke-width=\"2\"/></g></g></svg>"},
        // ---- brouillard
        {"brouillard", "Brouillard", "M\xC3\xA9t\xC3\xA9o",
         "brouillard, brume, visibilit\xC3\xA9",
         false, false, 64, 64,
         "<svg xmlns=\"http://www.w3.org/2000/svg\" width=\"64\" height=\"64\" viewBox=\"0 0 24 24\">"
         "<g stroke-linecap=\"round\" stroke-linejoin=\"round\"><g transform=\"translate(0 -4) scale(1)\">"
         "<path d=\"M7 18.5a4.5 4.5 0 0 1-.4-9 5.8 5.8 0 0 1 11.2 1.2 3.9 3.9 0 0 1 .2 7.8z\" fill=\"#D5DCE6\" stroke=\"#8A94A6\" stroke-width=\"1\"/>"
         "</g><path d=\"M4 17h16M6 20.5h12\" fill=\"none\" stroke=\"#8A94A6\" stroke-width=\"2\"/></g></svg>"},
        // ---- chaud
        {"chaud", "Chaleur", "M\xC3\xA9t\xC3\xA9o",
         "chaud, canicule, temp\xC3\xA9rature, \xC3\xA9t\xC3\xA9",
         false, false, 64, 64,
         "<svg xmlns=\"http://www.w3.org/2000/svg\" width=\"64\" height=\"64\" viewBox=\"0 0 24 24\">"
         "<g stroke-linecap=\"round\" stroke-linejoin=\"round\">"
         "<path d=\"M10 14.3V5a2 2 0 0 1 4 0v9.3a4 4 0 1 1-4 0z\" fill=\"none\" stroke=\"#8A94A6\" stroke-width=\"2\"/>"
         "<circle cx=\"12\" cy=\"17.5\" r=\"2.2\" fill=\"#D93A2B\"/>"
         "<path d=\"M12 17.5V7\" fill=\"none\" stroke=\"#D93A2B\" stroke-width=\"1.8\"/>"
         "<path d=\"M16.5 6h2.5M16.5 9h2.5M16.5 12h2.5\" fill=\"none\" stroke=\"#8A94A6\" stroke-width=\"1.4\"/></g>"
         "</svg>"},
        // ---- froid
        {"froid", "Froid", "M\xC3\xA9t\xC3\xA9o",
         "froid, gel, temp\xC3\xA9rature, hiver",
         false, false, 64, 64,
         "<svg xmlns=\"http://www.w3.org/2000/svg\" width=\"64\" height=\"64\" viewBox=\"0 0 24 24\">"
         "<g stroke-linecap=\"round\" stroke-linejoin=\"round\">"
         "<path d=\"M10 14.3V5a2 2 0 0 1 4 0v9.3a4 4 0 1 1-4 0z\" fill=\"none\" stroke=\"#8A94A6\" stroke-width=\"2\"/>"
         "<circle cx=\"12\" cy=\"17.5\" r=\"2.2\" fill=\"#4FA3E0\"/>"
         "<path d=\"M12 17.5V13\" fill=\"none\" stroke=\"#4FA3E0\" stroke-width=\"1.8\"/>"
         "<path d=\"M16.5 6h2.5M16.5 9h2.5M16.5 12h2.5\" fill=\"none\" stroke=\"#8A94A6\" stroke-width=\"1.4\"/></g>"
         "</svg>"},
        // ---- lune
        {"lune", "Nuit", "M\xC3\xA9t\xC3\xA9o",
         "nuit, lune, soir",
         false, false, 64, 64,
         "<svg xmlns=\"http://www.w3.org/2000/svg\" width=\"64\" height=\"64\" viewBox=\"0 0 24 24\">"
         "<g stroke-linecap=\"round\" stroke-linejoin=\"round\">"
         "<path d=\"M9.69 4.6A8.5 8.5 0 1 0 19.4 14.31A7 7 0 0 1 9.69 4.6z\" fill=\"#F3E3A1\"/></g></svg>"},
        // ---- humidite
        {"humidite", "Humidit\xC3\xA9", "M\xC3\xA9t\xC3\xA9o",
         "humidit\xC3\xA9, hygrom\xC3\xA9trie, eau, pourcentage",
         false, false, 64, 64,
         "<svg xmlns=\"http://www.w3.org/2000/svg\" width=\"64\" height=\"64\" viewBox=\"0 0 24 24\">"
         "<g stroke-linecap=\"round\" stroke-linejoin=\"round\">"
         "<path d=\"M12 2.5s-7 7.5-7 12.2a7 7 0 0 0 14 0C19 10 12 2.5 12 2.5z\" fill=\"#4FA3E0\"/>"
         "<circle cx=\"9.8\" cy=\"13\" r=\"1.2\" fill=\"none\" stroke=\"#FFFFFF\" stroke-width=\"1.2\"/>"
         "<circle cx=\"14.2\" cy=\"17.4\" r=\"1.2\" fill=\"none\" stroke=\"#FFFFFF\" stroke-width=\"1.2\"/>"
         "<path d=\"M14.6 12.3l-5.2 5.8\" fill=\"none\" stroke=\"#FFFFFF\" stroke-width=\"1.3\"/></g></svg>"},
        // ---- operateur
        {"operateur", "Op\xC3\xA9rateur", "Personnes",
         "op\xC3\xA9rateur, personne, casque, conducteur",
         true, true, 64, 64,
         "<svg xmlns=\"http://www.w3.org/2000/svg\" width=\"64\" height=\"64\" viewBox=\"0 0 24 24\">"
         "<g stroke-linecap=\"round\" stroke-linejoin=\"round\">"
         "<circle cx=\"12\" cy=\"9.5\" r=\"3.6\" fill=\"none\" stroke=\"#E6EAF0\" stroke-width=\"2\"/>"
         "<path d=\"M7.6 8.2a4.4 4.4 0 0 1 8.8 0z\" fill=\"#E6EAF0\"/>"
         "<path d=\"M6.5 8.2h11\" fill=\"none\" stroke=\"#E6EAF0\" stroke-width=\"2\"/>"
         "<path d=\"M4.5 21a7.5 7.5 0 0 1 15 0\" fill=\"none\" stroke=\"#E6EAF0\" stroke-width=\"2\"/></g></svg>"},
        // ---- equipe
        {"equipe", "\xC3\x89quipe", "Personnes",
         "\xC3\xA9quipe, groupe, personnes, service",
         true, true, 64, 64,
         "<svg xmlns=\"http://www.w3.org/2000/svg\" width=\"64\" height=\"64\" viewBox=\"0 0 24 24\">"
         "<g stroke-linecap=\"round\" stroke-linejoin=\"round\">"
         "<circle cx=\"12\" cy=\"8\" r=\"3.3\" fill=\"none\" stroke=\"#E6EAF0\" stroke-width=\"2\"/>"
         "<path d=\"M6 20.5a6 6 0 0 1 12 0\" fill=\"none\" stroke=\"#E6EAF0\" stroke-width=\"2\"/>"
         "<circle cx=\"5\" cy=\"10.5\" r=\"2.3\" fill=\"none\" stroke=\"#E6EAF0\" stroke-width=\"2\"/>"
         "<path d=\"M1.8 19a3.5 3.5 0 0 1 4.6-3.2\" fill=\"none\" stroke=\"#E6EAF0\" stroke-width=\"2\"/>"
         "<circle cx=\"19\" cy=\"10.5\" r=\"2.3\" fill=\"none\" stroke=\"#E6EAF0\" stroke-width=\"2\"/>"
         "<path d=\"M22.2 19a3.5 3.5 0 0 0-4.6-3.2\" fill=\"none\" stroke=\"#E6EAF0\" stroke-width=\"2\"/></g></svg>"},
        // ---- technicien
        {"technicien", "Technicien", "Personnes",
         "technicien, maintenance, intervenant",
         true, true, 64, 64,
         "<svg xmlns=\"http://www.w3.org/2000/svg\" width=\"64\" height=\"64\" viewBox=\"0 0 24 24\">"
         "<g stroke-linecap=\"round\" stroke-linejoin=\"round\">"
         "<circle cx=\"9\" cy=\"8\" r=\"3.6\" fill=\"none\" stroke=\"#E6EAF0\" stroke-width=\"2\"/>"
         "<path d=\"M2.5 20.5a6.5 6.5 0 0 1 9.5-5.8\" fill=\"none\" stroke=\"#E6EAF0\" stroke-width=\"2\"/>"
         "<g transform=\"translate(11.5 10.5) scale(0.5)\">"
         "<path d=\"M15.5 3.7a5 5 0 0 0-5.6 6.7L3.8 16.5a1.9 1.9 0 0 0 2.7 2.7l6.1-6.1a5 5 0 0 0 6.7-5.6l-2.9 2.9-2.6-.4-.4-2.6z\" fill=\"none\" stroke=\"#E6EAF0\" stroke-width=\"3.2\"/>"
         "</g></g></svg>"},
        // ---- administrateur
        {"administrateur", "Administrateur", "Personnes",
         "administrateur, admin, gestion, droits",
         true, true, 64, 64,
         "<svg xmlns=\"http://www.w3.org/2000/svg\" width=\"64\" height=\"64\" viewBox=\"0 0 24 24\">"
         "<g stroke-linecap=\"round\" stroke-linejoin=\"round\">"
         "<circle cx=\"9\" cy=\"8\" r=\"3.6\" fill=\"none\" stroke=\"#E6EAF0\" stroke-width=\"2\"/>"
         "<path d=\"M2.5 20.5a6.5 6.5 0 0 1 9.5-5.8\" fill=\"none\" stroke=\"#E6EAF0\" stroke-width=\"2\"/>"
         "<g transform=\"translate(11.5 10.5) scale(0.5)\">"
         "<path d=\"M10.647 4.908 L10.928 2.561 L13.072 2.561 L13.353 4.908 L15.074 5.467 L16.681 3.733 L18.416 4.994 L17.263 7.058 L18.327 8.522 L20.646 8.063 L21.309 10.103 L19.163 11.095 L19.163 12.905 L21.309 13.897 L20.646 15.937 L18.327 15.478 L17.263 16.942 L18.416 19.006 L16.681 20.267 L15.074 18.533 L13.353 19.092 L13.072 21.439 L10.928 21.439 L10.647 19.092 L8.926 18.533 L7.319 20.267 L5.584 19.006 L6.737 16.942 L5.673 15.478 L3.354 15.937 L2.691 13.897 L4.837 12.905 L4.837 11.095 L2.691 10.103 L3.354 8.063 L5.673 8.522 L6.737 7.058 L5.584 4.994 L7.319 3.733 L8.926 5.467Z\" fill=\"none\" stroke=\"#E6EAF0\" stroke-width=\"3.2\"/>"
         "<circle cx=\"12\" cy=\"12\" r=\"3\" fill=\"none\" stroke=\"#E6EAF0\" stroke-width=\"3.2\"/></g></g></svg>"},
        // ---- utilisateur-ajout
        {"utilisateur-ajout", "Ajouter un utilisateur", "Personnes",
         "utilisateur, ajouter, nouveau, compte",
         true, true, 64, 64,
         "<svg xmlns=\"http://www.w3.org/2000/svg\" width=\"64\" height=\"64\" viewBox=\"0 0 24 24\">"
         "<g stroke-linecap=\"round\" stroke-linejoin=\"round\">"
         "<circle cx=\"9.5\" cy=\"8\" r=\"3.6\" fill=\"none\" stroke=\"#E6EAF0\" stroke-width=\"2\"/>"
         "<path d=\"M3 20.5a6.5 6.5 0 0 1 13 0\" fill=\"none\" stroke=\"#E6EAF0\" stroke-width=\"2\"/>"
         "<path d=\"M19 8v6M16 11h6\" fill=\"none\" stroke=\"#E6EAF0\" stroke-width=\"2\"/></g></svg>"},
        // ---- utilisateur-retrait
        {"utilisateur-retrait", "Retirer un utilisateur", "Personnes",
         "utilisateur, retirer, supprimer, compte",
         true, true, 64, 64,
         "<svg xmlns=\"http://www.w3.org/2000/svg\" width=\"64\" height=\"64\" viewBox=\"0 0 24 24\">"
         "<g stroke-linecap=\"round\" stroke-linejoin=\"round\">"
         "<circle cx=\"9.5\" cy=\"8\" r=\"3.6\" fill=\"none\" stroke=\"#E6EAF0\" stroke-width=\"2\"/>"
         "<path d=\"M3 20.5a6.5 6.5 0 0 1 13 0\" fill=\"none\" stroke=\"#E6EAF0\" stroke-width=\"2\"/>"
         "<path d=\"M16 11h6\" fill=\"none\" stroke=\"#E6EAF0\" stroke-width=\"2\"/></g></svg>"},
        // ---- utilisateur-valide
        {"utilisateur-valide", "Utilisateur autoris\xC3\xA9", "Personnes",
         "utilisateur, autoris\xC3\xA9, valid\xC3\xA9, acc\xC3\xA8s",
         true, true, 64, 64,
         "<svg xmlns=\"http://www.w3.org/2000/svg\" width=\"64\" height=\"64\" viewBox=\"0 0 24 24\">"
         "<g stroke-linecap=\"round\" stroke-linejoin=\"round\">"
         "<circle cx=\"9.5\" cy=\"8\" r=\"3.6\" fill=\"none\" stroke=\"#E6EAF0\" stroke-width=\"2\"/>"
         "<path d=\"M3 20.5a6.5 6.5 0 0 1 13 0\" fill=\"none\" stroke=\"#E6EAF0\" stroke-width=\"2\"/>"
         "<path d=\"M15.5 11l2 2 4-4\" fill=\"none\" stroke=\"#E6EAF0\" stroke-width=\"2\"/></g></svg>"},
        // ---- visiteur
        {"visiteur", "Visiteur", "Personnes",
         "visiteur, badge, invit\xC3\xA9, accueil",
         true, true, 64, 64,
         "<svg xmlns=\"http://www.w3.org/2000/svg\" width=\"64\" height=\"64\" viewBox=\"0 0 24 24\">"
         "<g stroke-linecap=\"round\" stroke-linejoin=\"round\">"
         "<circle cx=\"8.5\" cy=\"8\" r=\"3.6\" fill=\"none\" stroke=\"#E6EAF0\" stroke-width=\"2\"/>"
         "<path d=\"M2 20.5a6.5 6.5 0 0 1 13 0\" fill=\"none\" stroke=\"#E6EAF0\" stroke-width=\"2\"/>"
         "<rect x=\"15\" y=\"9.5\" width=\"6.5\" height=\"10\" rx=\"1\" fill=\"none\" stroke=\"#E6EAF0\" stroke-width=\"1.6\"/>"
         "<circle cx=\"18.25\" cy=\"12.8\" r=\"1.3\" fill=\"#E6EAF0\"/>"
         "<path d=\"M16.8 16.5h2.9\" fill=\"none\" stroke=\"#E6EAF0\" stroke-width=\"1.4\"/></g></svg>"},
        // ---- groupe
        {"groupe", "Groupe", "Personnes",
         "groupe, deux personnes, bin\xC3\xB4me",
         true, true, 64, 64,
         "<svg xmlns=\"http://www.w3.org/2000/svg\" width=\"64\" height=\"64\" viewBox=\"0 0 24 24\">"
         "<g stroke-linecap=\"round\" stroke-linejoin=\"round\">"
         "<circle cx=\"9\" cy=\"8\" r=\"3.3\" fill=\"none\" stroke=\"#E6EAF0\" stroke-width=\"2\"/>"
         "<path d=\"M3 20a6 6 0 0 1 12 0\" fill=\"none\" stroke=\"#E6EAF0\" stroke-width=\"2\"/>"
         "<circle cx=\"16.5\" cy=\"8.5\" r=\"2.8\" fill=\"none\" stroke=\"#E6EAF0\" stroke-width=\"2\"/>"
         "<path d=\"M15.5 13.5a5.5 5.5 0 0 1 6.5 5.5\" fill=\"none\" stroke=\"#E6EAF0\" stroke-width=\"2\"/></g>"
         "</svg>"},
        // ---- responsable
        {"responsable", "Responsable", "Personnes",
         "responsable, chef, encadrant, manager",
         true, true, 64, 64,
         "<svg xmlns=\"http://www.w3.org/2000/svg\" width=\"64\" height=\"64\" viewBox=\"0 0 24 24\">"
         "<g stroke-linecap=\"round\" stroke-linejoin=\"round\">"
         "<circle cx=\"12\" cy=\"7.5\" r=\"3.6\" fill=\"none\" stroke=\"#E6EAF0\" stroke-width=\"2\"/>"
         "<path d=\"M4.5 21a7.5 7.5 0 0 1 15 0\" fill=\"none\" stroke=\"#E6EAF0\" stroke-width=\"2\"/>"
         "<path d=\"M12 13.6l1.3 1.4-.9 4.5h-.8l-.9-4.5z\" fill=\"#E6EAF0\"/></g></svg>"},
        // ---- drapeau-fr
        {"drapeau-fr", "France", "Drapeaux",
         "France, fran\xC3\xA7" "ais, FR, langue, drapeau",
         false, false, 96, 64,
         "<svg xmlns=\"http://www.w3.org/2000/svg\" width=\"96\" height=\"64\" viewBox=\"0 0 36 24\">"
         "<g stroke-linecap=\"round\" stroke-linejoin=\"round\">"
         "<rect x=\"0\" y=\"0\" width=\"12\" height=\"24\" fill=\"#0055A4\"/>"
         "<rect x=\"12\" y=\"0\" width=\"12\" height=\"24\" fill=\"#FFFFFF\"/>"
         "<rect x=\"24\" y=\"0\" width=\"12\" height=\"24\" fill=\"#EF4135\"/>"
         "<rect x=\"0.25\" y=\"0.25\" width=\"35.5\" height=\"23.5\" fill=\"none\" stroke=\"#8A94A6\" stroke-width=\"0.5\"/>"
         "</g></svg>"},
        // ---- drapeau-gb
        {"drapeau-gb", "Royaume-Uni", "Drapeaux",
         "Royaume-Uni, anglais, GB, english, langue, drapeau",
         false, false, 96, 64,
         "<svg xmlns=\"http://www.w3.org/2000/svg\" width=\"96\" height=\"64\" viewBox=\"0 0 36 24\">"
         "<g stroke-linecap=\"round\" stroke-linejoin=\"round\">"
         "<rect x=\"0\" y=\"0\" width=\"36\" height=\"24\" fill=\"#012169\"/>"
         "<line x1=\"0\" y1=\"0\" x2=\"36\" y2=\"24\" fill=\"none\" stroke=\"#FFFFFF\" stroke-width=\"4.8\" stroke-linecap=\"butt\"/>"
         "<line x1=\"36\" y1=\"0\" x2=\"0\" y2=\"24\" fill=\"none\" stroke=\"#FFFFFF\" stroke-width=\"4.8\" stroke-linecap=\"butt\"/>"
         "<line x1=\"0\" y1=\"0\" x2=\"36\" y2=\"24\" fill=\"none\" stroke=\"#C8102E\" stroke-width=\"1.6\" stroke-linecap=\"butt\"/>"
         "<line x1=\"36\" y1=\"0\" x2=\"0\" y2=\"24\" fill=\"none\" stroke=\"#C8102E\" stroke-width=\"1.6\" stroke-linecap=\"butt\"/>"
         "<rect x=\"14\" y=\"0\" width=\"8\" height=\"24\" fill=\"#FFFFFF\"/>"
         "<rect x=\"0\" y=\"8\" width=\"36\" height=\"8\" fill=\"#FFFFFF\"/>"
         "<rect x=\"15.6\" y=\"0\" width=\"4.8\" height=\"24\" fill=\"#C8102E\"/>"
         "<rect x=\"0\" y=\"9.6\" width=\"36\" height=\"4.8\" fill=\"#C8102E\"/></g></svg>"},
        // ---- drapeau-de
        {"drapeau-de", "Allemagne", "Drapeaux",
         "Allemagne, allemand, DE, deutsch, langue, drapeau",
         false, false, 96, 64,
         "<svg xmlns=\"http://www.w3.org/2000/svg\" width=\"96\" height=\"64\" viewBox=\"0 0 36 24\">"
         "<g stroke-linecap=\"round\" stroke-linejoin=\"round\">"
         "<rect x=\"0\" y=\"0\" width=\"36\" height=\"8\" fill=\"#000000\"/>"
         "<rect x=\"0\" y=\"8\" width=\"36\" height=\"8\" fill=\"#DD0000\"/>"
         "<rect x=\"0\" y=\"16\" width=\"36\" height=\"8\" fill=\"#FFCE00\"/></g></svg>"},
        // ---- drapeau-es
        {"drapeau-es", "Espagne", "Drapeaux",
         "Espagne, espagnol, ES, espa\xC3\xB1ol, langue, drapeau",
         false, false, 96, 64,
         "<svg xmlns=\"http://www.w3.org/2000/svg\" width=\"96\" height=\"64\" viewBox=\"0 0 36 24\">"
         "<g stroke-linecap=\"round\" stroke-linejoin=\"round\">"
         "<rect x=\"0\" y=\"0\" width=\"36\" height=\"6\" fill=\"#AA151B\"/>"
         "<rect x=\"0\" y=\"6\" width=\"36\" height=\"12\" fill=\"#F1BF00\"/>"
         "<rect x=\"0\" y=\"18\" width=\"36\" height=\"6\" fill=\"#AA151B\"/></g></svg>"},
        // ---- drapeau-it
        {"drapeau-it", "Italie", "Drapeaux",
         "Italie, italien, IT, italiano, langue, drapeau",
         false, false, 96, 64,
         "<svg xmlns=\"http://www.w3.org/2000/svg\" width=\"96\" height=\"64\" viewBox=\"0 0 36 24\">"
         "<g stroke-linecap=\"round\" stroke-linejoin=\"round\">"
         "<rect x=\"0\" y=\"0\" width=\"12\" height=\"24\" fill=\"#009246\"/>"
         "<rect x=\"12\" y=\"0\" width=\"12\" height=\"24\" fill=\"#FFFFFF\"/>"
         "<rect x=\"24\" y=\"0\" width=\"12\" height=\"24\" fill=\"#CE2B37\"/>"
         "<rect x=\"0.25\" y=\"0.25\" width=\"35.5\" height=\"23.5\" fill=\"none\" stroke=\"#8A94A6\" stroke-width=\"0.5\"/>"
         "</g></svg>"},
        // ---- drapeau-nl
        {"drapeau-nl", "Pays-Bas", "Drapeaux",
         "Pays-Bas, n\xC3\xA9" "erlandais, NL, nederlands, langue, drapeau",
         false, false, 96, 64,
         "<svg xmlns=\"http://www.w3.org/2000/svg\" width=\"96\" height=\"64\" viewBox=\"0 0 36 24\">"
         "<g stroke-linecap=\"round\" stroke-linejoin=\"round\">"
         "<rect x=\"0\" y=\"0\" width=\"36\" height=\"8\" fill=\"#AE1C28\"/>"
         "<rect x=\"0\" y=\"8\" width=\"36\" height=\"8\" fill=\"#FFFFFF\"/>"
         "<rect x=\"0\" y=\"16\" width=\"36\" height=\"8\" fill=\"#21468B\"/>"
         "<rect x=\"0.25\" y=\"0.25\" width=\"35.5\" height=\"23.5\" fill=\"none\" stroke=\"#8A94A6\" stroke-width=\"0.5\"/>"
         "</g></svg>"},
        // ---- drapeau-pt
        {"drapeau-pt", "Portugal", "Drapeaux",
         "Portugal, portugais, PT, portugu\xC3\xAAs, langue, drapeau",
         false, false, 96, 64,
         "<svg xmlns=\"http://www.w3.org/2000/svg\" width=\"96\" height=\"64\" viewBox=\"0 0 36 24\">"
         "<g stroke-linecap=\"round\" stroke-linejoin=\"round\">"
         "<rect x=\"0\" y=\"0\" width=\"14.4\" height=\"24\" fill=\"#006600\"/>"
         "<rect x=\"14.4\" y=\"0\" width=\"21.6\" height=\"24\" fill=\"#FF0000\"/>"
         "<circle cx=\"14.4\" cy=\"12\" r=\"4.8\" fill=\"none\" stroke=\"#FFE000\" stroke-width=\"1.6\"/>"
         "<rect x=\"12.4\" y=\"9.4\" width=\"4\" height=\"5.2\" rx=\"1\" fill=\"#FFFFFF\" stroke=\"#FF0000\" stroke-width=\"0.8\"/>"
         "</g></svg>"},
        // ---- drapeau-pl
        {"drapeau-pl", "Pologne", "Drapeaux",
         "Pologne, polonais, PL, polski, langue, drapeau",
         false, false, 96, 64,
         "<svg xmlns=\"http://www.w3.org/2000/svg\" width=\"96\" height=\"64\" viewBox=\"0 0 36 24\">"
         "<g stroke-linecap=\"round\" stroke-linejoin=\"round\">"
         "<rect x=\"0\" y=\"0\" width=\"36\" height=\"12\" fill=\"#FFFFFF\"/>"
         "<rect x=\"0\" y=\"12\" width=\"36\" height=\"12\" fill=\"#DC143C\"/>"
         "<rect x=\"0.25\" y=\"0.25\" width=\"35.5\" height=\"23.5\" fill=\"none\" stroke=\"#8A94A6\" stroke-width=\"0.5\"/>"
         "</g></svg>"},
        // ---- drapeau-be
        {"drapeau-be", "Belgique", "Drapeaux",
         "Belgique, belge, BE, langue, drapeau",
         false, false, 96, 64,
         "<svg xmlns=\"http://www.w3.org/2000/svg\" width=\"96\" height=\"64\" viewBox=\"0 0 36 24\">"
         "<g stroke-linecap=\"round\" stroke-linejoin=\"round\">"
         "<rect x=\"0\" y=\"0\" width=\"12\" height=\"24\" fill=\"#000000\"/>"
         "<rect x=\"12\" y=\"0\" width=\"12\" height=\"24\" fill=\"#FDDA24\"/>"
         "<rect x=\"24\" y=\"0\" width=\"12\" height=\"24\" fill=\"#EF3340\"/></g></svg>"},
        // ---- drapeau-ch
        {"drapeau-ch", "Suisse", "Drapeaux",
         "Suisse, suisse, CH, langue, drapeau",
         false, false, 96, 64,
         "<svg xmlns=\"http://www.w3.org/2000/svg\" width=\"96\" height=\"64\" viewBox=\"0 0 36 24\">"
         "<g stroke-linecap=\"round\" stroke-linejoin=\"round\">"
         "<rect x=\"0\" y=\"0\" width=\"36\" height=\"24\" fill=\"#DA291C\"/>"
         "<rect x=\"15.5\" y=\"5\" width=\"5\" height=\"14\" fill=\"#FFFFFF\"/>"
         "<rect x=\"11\" y=\"9.5\" width=\"14\" height=\"5\" fill=\"#FFFFFF\"/></g></svg>"},
        // ---- drapeau-at
        {"drapeau-at", "Autriche", "Drapeaux",
         "Autriche, autrichien, AT, langue, drapeau",
         false, false, 96, 64,
         "<svg xmlns=\"http://www.w3.org/2000/svg\" width=\"96\" height=\"64\" viewBox=\"0 0 36 24\">"
         "<g stroke-linecap=\"round\" stroke-linejoin=\"round\">"
         "<rect x=\"0\" y=\"0\" width=\"36\" height=\"8\" fill=\"#C8102E\"/>"
         "<rect x=\"0\" y=\"8\" width=\"36\" height=\"8\" fill=\"#FFFFFF\"/>"
         "<rect x=\"0\" y=\"16\" width=\"36\" height=\"8\" fill=\"#C8102E\"/></g></svg>"},
        // ---- drapeau-se
        {"drapeau-se", "Su\xC3\xA8" "de", "Drapeaux",
         "Su\xC3\xA8" "de, su\xC3\xA9" "dois, SE, svenska, langue, drapeau",
         false, false, 96, 64,
         "<svg xmlns=\"http://www.w3.org/2000/svg\" width=\"96\" height=\"64\" viewBox=\"0 0 36 24\">"
         "<g stroke-linecap=\"round\" stroke-linejoin=\"round\">"
         "<rect x=\"0\" y=\"0\" width=\"36\" height=\"24\" fill=\"#006AA7\"/>"
         "<rect x=\"10\" y=\"0\" width=\"4.5\" height=\"24\" fill=\"#FECC02\"/>"
         "<rect x=\"0\" y=\"9.75\" width=\"36\" height=\"4.5\" fill=\"#FECC02\"/></g></svg>"},
        // ---- drapeau-dk
        {"drapeau-dk", "Danemark", "Drapeaux",
         "Danemark, danois, DK, dansk, langue, drapeau",
         false, false, 96, 64,
         "<svg xmlns=\"http://www.w3.org/2000/svg\" width=\"96\" height=\"64\" viewBox=\"0 0 36 24\">"
         "<g stroke-linecap=\"round\" stroke-linejoin=\"round\">"
         "<rect x=\"0\" y=\"0\" width=\"36\" height=\"24\" fill=\"#C8102E\"/>"
         "<rect x=\"10\" y=\"0\" width=\"3.5\" height=\"24\" fill=\"#FFFFFF\"/>"
         "<rect x=\"0\" y=\"10.25\" width=\"36\" height=\"3.5\" fill=\"#FFFFFF\"/></g></svg>"},
        // ---- drapeau-fi
        {"drapeau-fi", "Finlande", "Drapeaux",
         "Finlande, finnois, FI, suomi, langue, drapeau",
         false, false, 96, 64,
         "<svg xmlns=\"http://www.w3.org/2000/svg\" width=\"96\" height=\"64\" viewBox=\"0 0 36 24\">"
         "<g stroke-linecap=\"round\" stroke-linejoin=\"round\">"
         "<rect x=\"0\" y=\"0\" width=\"36\" height=\"24\" fill=\"#FFFFFF\"/>"
         "<rect x=\"10\" y=\"0\" width=\"6\" height=\"24\" fill=\"#003580\"/>"
         "<rect x=\"0\" y=\"9\" width=\"36\" height=\"6\" fill=\"#003580\"/>"
         "<rect x=\"0.25\" y=\"0.25\" width=\"35.5\" height=\"23.5\" fill=\"none\" stroke=\"#8A94A6\" stroke-width=\"0.5\"/>"
         "</g></svg>"},
        // ---- drapeau-no
        {"drapeau-no", "Norv\xC3\xA8ge", "Drapeaux",
         "Norv\xC3\xA8ge, norv\xC3\xA9gien, NO, norsk, langue, drapeau",
         false, false, 96, 64,
         "<svg xmlns=\"http://www.w3.org/2000/svg\" width=\"96\" height=\"64\" viewBox=\"0 0 36 24\">"
         "<g stroke-linecap=\"round\" stroke-linejoin=\"round\">"
         "<rect x=\"0\" y=\"0\" width=\"36\" height=\"24\" fill=\"#BA0C2F\"/>"
         "<rect x=\"9\" y=\"0\" width=\"7\" height=\"24\" fill=\"#FFFFFF\"/>"
         "<rect x=\"0\" y=\"8.5\" width=\"36\" height=\"7\" fill=\"#FFFFFF\"/>"
         "<rect x=\"10.75\" y=\"0\" width=\"3.5\" height=\"24\" fill=\"#00205B\"/>"
         "<rect x=\"0\" y=\"10.25\" width=\"36\" height=\"3.5\" fill=\"#00205B\"/></g></svg>"},
        // ---- drapeau-ie
        {"drapeau-ie", "Irlande", "Drapeaux",
         "Irlande, irlandais, IE, langue, drapeau",
         false, false, 96, 64,
         "<svg xmlns=\"http://www.w3.org/2000/svg\" width=\"96\" height=\"64\" viewBox=\"0 0 36 24\">"
         "<g stroke-linecap=\"round\" stroke-linejoin=\"round\">"
         "<rect x=\"0\" y=\"0\" width=\"12\" height=\"24\" fill=\"#169B62\"/>"
         "<rect x=\"12\" y=\"0\" width=\"12\" height=\"24\" fill=\"#FFFFFF\"/>"
         "<rect x=\"24\" y=\"0\" width=\"12\" height=\"24\" fill=\"#FF883E\"/>"
         "<rect x=\"0.25\" y=\"0.25\" width=\"35.5\" height=\"23.5\" fill=\"none\" stroke=\"#8A94A6\" stroke-width=\"0.5\"/>"
         "</g></svg>"},
        // ---- drapeau-lu
        {"drapeau-lu", "Luxembourg", "Drapeaux",
         "Luxembourg, luxembourgeois, LU, langue, drapeau",
         false, false, 96, 64,
         "<svg xmlns=\"http://www.w3.org/2000/svg\" width=\"96\" height=\"64\" viewBox=\"0 0 36 24\">"
         "<g stroke-linecap=\"round\" stroke-linejoin=\"round\">"
         "<rect x=\"0\" y=\"0\" width=\"36\" height=\"8\" fill=\"#ED2939\"/>"
         "<rect x=\"0\" y=\"8\" width=\"36\" height=\"8\" fill=\"#FFFFFF\"/>"
         "<rect x=\"0\" y=\"16\" width=\"36\" height=\"8\" fill=\"#00A1DE\"/></g></svg>"},
        // ---- drapeau-cz
        {"drapeau-cz", "Tch\xC3\xA9quie", "Drapeaux",
         "Tch\xC3\xA9quie, tch\xC3\xA8que, CZ, \xC4\x8D" "e\xC5\xA1tina, langue, drapeau",
         false, false, 96, 64,
         "<svg xmlns=\"http://www.w3.org/2000/svg\" width=\"96\" height=\"64\" viewBox=\"0 0 36 24\">"
         "<g stroke-linecap=\"round\" stroke-linejoin=\"round\">"
         "<rect x=\"0\" y=\"0\" width=\"36\" height=\"12\" fill=\"#FFFFFF\"/>"
         "<rect x=\"0\" y=\"12\" width=\"36\" height=\"12\" fill=\"#D7141A\"/>"
         "<polygon points=\"0 0 18 12 0 24\" fill=\"#11457E\"/>"
         "<rect x=\"0.25\" y=\"0.25\" width=\"35.5\" height=\"23.5\" fill=\"none\" stroke=\"#8A94A6\" stroke-width=\"0.5\"/>"
         "</g></svg>"},
        // ---- drapeau-ro
        {"drapeau-ro", "Roumanie", "Drapeaux",
         "Roumanie, roumain, RO, langue, drapeau",
         false, false, 96, 64,
         "<svg xmlns=\"http://www.w3.org/2000/svg\" width=\"96\" height=\"64\" viewBox=\"0 0 36 24\">"
         "<g stroke-linecap=\"round\" stroke-linejoin=\"round\">"
         "<rect x=\"0\" y=\"0\" width=\"12\" height=\"24\" fill=\"#002B7F\"/>"
         "<rect x=\"12\" y=\"0\" width=\"12\" height=\"24\" fill=\"#FCD116\"/>"
         "<rect x=\"24\" y=\"0\" width=\"12\" height=\"24\" fill=\"#CE1126\"/></g></svg>"},
        // ---- drapeau-hu
        {"drapeau-hu", "Hongrie", "Drapeaux",
         "Hongrie, hongrois, HU, magyar, langue, drapeau",
         false, false, 96, 64,
         "<svg xmlns=\"http://www.w3.org/2000/svg\" width=\"96\" height=\"64\" viewBox=\"0 0 36 24\">"
         "<g stroke-linecap=\"round\" stroke-linejoin=\"round\">"
         "<rect x=\"0\" y=\"0\" width=\"36\" height=\"8\" fill=\"#CE2939\"/>"
         "<rect x=\"0\" y=\"8\" width=\"36\" height=\"8\" fill=\"#FFFFFF\"/>"
         "<rect x=\"0\" y=\"16\" width=\"36\" height=\"8\" fill=\"#477050\"/></g></svg>"},
        // ---- drapeau-ua
        {"drapeau-ua", "Ukraine", "Drapeaux",
         "Ukraine, ukrainien, UA, langue, drapeau",
         false, false, 96, 64,
         "<svg xmlns=\"http://www.w3.org/2000/svg\" width=\"96\" height=\"64\" viewBox=\"0 0 36 24\">"
         "<g stroke-linecap=\"round\" stroke-linejoin=\"round\">"
         "<rect x=\"0\" y=\"0\" width=\"36\" height=\"12\" fill=\"#0057B7\"/>"
         "<rect x=\"0\" y=\"12\" width=\"36\" height=\"12\" fill=\"#FFD700\"/></g></svg>"},
        // ---- drapeau-eu
        {"drapeau-eu", "Union europ\xC3\xA9" "enne", "Drapeaux",
         "Europe, europ\xC3\xA9" "en, EU, UE, langue, drapeau",
         false, false, 96, 64,
         "<svg xmlns=\"http://www.w3.org/2000/svg\" width=\"96\" height=\"64\" viewBox=\"0 0 36 24\">"
         "<g stroke-linecap=\"round\" stroke-linejoin=\"round\">"
         "<rect x=\"0\" y=\"0\" width=\"36\" height=\"24\" fill=\"#003399\"/>"
         "<polygon points=\"18 3.3 18.353 4.315 19.427 4.336 18.571 4.985 18.882 6.014 18 5.4 17.118 6.014 17.429 4.985 16.573 4.336 17.647 4.315\" fill=\"#FFCC00\"/>"
         "<polygon points=\"21.6 4.265 21.953 5.279 23.027 5.301 22.171 5.95 22.482 6.978 21.6 6.365 20.718 6.978 21.029 5.95 20.173 5.301 21.247 5.279\" fill=\"#FFCC00\"/>"
         "<polygon points=\"24.235 6.9 24.588 7.915 25.662 7.936 24.806 8.585 25.117 9.614 24.235 9 23.354 9.614 23.665 8.585 22.809 7.936 23.883 7.915\" fill=\"#FFCC00\"/>"
         "<polygon points=\"25.2 10.5 25.553 11.515 26.627 11.536 25.771 12.185 26.082 13.214 25.2 12.6 24.318 13.214 24.629 12.185 23.773 11.536 24.847 11.515\" fill=\"#FFCC00\"/>"
         "<polygon points=\"24.235 14.1 24.588 15.115 25.662 15.136 24.806 15.785 25.117 16.814 24.235 16.2 23.354 16.814 23.665 15.785 22.809 15.136 23.883 15.115\" fill=\"#FFCC00\"/>"
         "<polygon points=\"21.6 16.735 21.953 17.75 23.027 17.772 22.171 18.421 22.482 19.449 21.6 18.835 20.718 19.449 21.029 18.421 20.173 17.772 21.247 17.75\" fill=\"#FFCC00\"/>"
         "<polygon points=\"18 17.7 18.353 18.715 19.427 18.736 18.571 19.385 18.882 20.414 18 19.8 17.118 20.414 17.429 19.385 16.573 18.736 17.647 18.715\" fill=\"#FFCC00\"/>"
         "<polygon points=\"14.4 16.735 14.753 17.75 15.827 17.772 14.971 18.421 15.282 19.449 14.4 18.835 13.518 19.449 13.829 18.421 12.973 17.772 14.047 17.75\" fill=\"#FFCC00\"/>"
         "<polygon points=\"11.765 14.1 12.117 15.115 13.191 15.136 12.335 15.785 12.646 16.814 11.765 16.2 10.883 16.814 11.194 15.785 10.338 15.136 11.412 15.115\" fill=\"#FFCC00\"/>"
         "<polygon points=\"10.8 10.5 11.153 11.515 12.227 11.536 11.371 12.185 11.682 13.214 10.8 12.6 9.918 13.214 10.229 12.185 9.373 11.536 10.447 11.515\" fill=\"#FFCC00\"/>"
         "<polygon points=\"11.765 6.9 12.117 7.915 13.191 7.936 12.335 8.585 12.646 9.614 11.765 9 10.883 9.614 11.194 8.585 10.338 7.936 11.412 7.915\" fill=\"#FFCC00\"/>"
         "<polygon points=\"14.4 4.265 14.753 5.279 15.827 5.301 14.971 5.95 15.282 6.978 14.4 6.365 13.518 6.978 13.829 5.95 12.973 5.301 14.047 5.279\" fill=\"#FFCC00\"/>"
         "</g></svg>"},
        // ---- drapeau-us
        {"drapeau-us", "\xC3\x89tats-Unis", "Drapeaux",
         "\xC3\x89tats-Unis, am\xC3\xA9ricain, US, english, langue, drapeau",
         false, false, 96, 64,
         "<svg xmlns=\"http://www.w3.org/2000/svg\" width=\"96\" height=\"64\" viewBox=\"0 0 36 24\">"
         "<g stroke-linecap=\"round\" stroke-linejoin=\"round\">"
         "<rect x=\"0\" y=\"0\" width=\"36\" height=\"24\" fill=\"#FFFFFF\"/>"
         "<rect x=\"0\" y=\"0\" width=\"36\" height=\"1.846\" fill=\"#B22234\"/>"
         "<rect x=\"0\" y=\"3.692\" width=\"36\" height=\"1.846\" fill=\"#B22234\"/>"
         "<rect x=\"0\" y=\"7.385\" width=\"36\" height=\"1.846\" fill=\"#B22234\"/>"
         "<rect x=\"0\" y=\"11.077\" width=\"36\" height=\"1.846\" fill=\"#B22234\"/>"
         "<rect x=\"0\" y=\"14.769\" width=\"36\" height=\"1.846\" fill=\"#B22234\"/>"
         "<rect x=\"0\" y=\"18.462\" width=\"36\" height=\"1.846\" fill=\"#B22234\"/>"
         "<rect x=\"0\" y=\"22.154\" width=\"36\" height=\"1.846\" fill=\"#B22234\"/>"
         "<rect x=\"0\" y=\"0\" width=\"14.4\" height=\"12.923\" fill=\"#3C3B6E\"/>"
         "<circle cx=\"1.6\" cy=\"1.7\" r=\"0.5\" fill=\"#FFFFFF\"/>"
         "<circle cx=\"3.85\" cy=\"1.7\" r=\"0.5\" fill=\"#FFFFFF\"/>"
         "<circle cx=\"6.1\" cy=\"1.7\" r=\"0.5\" fill=\"#FFFFFF\"/>"
         "<circle cx=\"8.35\" cy=\"1.7\" r=\"0.5\" fill=\"#FFFFFF\"/>"
         "<circle cx=\"10.6\" cy=\"1.7\" r=\"0.5\" fill=\"#FFFFFF\"/>"
         "<circle cx=\"12.85\" cy=\"1.7\" r=\"0.5\" fill=\"#FFFFFF\"/>"
         "<circle cx=\"1.6\" cy=\"4.05\" r=\"0.5\" fill=\"#FFFFFF\"/>"
         "<circle cx=\"3.85\" cy=\"4.05\" r=\"0.5\" fill=\"#FFFFFF\"/>"
         "<circle cx=\"6.1\" cy=\"4.05\" r=\"0.5\" fill=\"#FFFFFF\"/>"
         "<circle cx=\"8.35\" cy=\"4.05\" r=\"0.5\" fill=\"#FFFFFF\"/>"
         "<circle cx=\"10.6\" cy=\"4.05\" r=\"0.5\" fill=\"#FFFFFF\"/>"
         "<circle cx=\"12.85\" cy=\"4.05\" r=\"0.5\" fill=\"#FFFFFF\"/>"
         "<circle cx=\"1.6\" cy=\"6.4\" r=\"0.5\" fill=\"#FFFFFF\"/>"
         "<circle cx=\"3.85\" cy=\"6.4\" r=\"0.5\" fill=\"#FFFFFF\"/>"
         "<circle cx=\"6.1\" cy=\"6.4\" r=\"0.5\" fill=\"#FFFFFF\"/>"
         "<circle cx=\"8.35\" cy=\"6.4\" r=\"0.5\" fill=\"#FFFFFF\"/>"
         "<circle cx=\"10.6\" cy=\"6.4\" r=\"0.5\" fill=\"#FFFFFF\"/>"
         "<circle cx=\"12.85\" cy=\"6.4\" r=\"0.5\" fill=\"#FFFFFF\"/>"
         "<circle cx=\"1.6\" cy=\"8.75\" r=\"0.5\" fill=\"#FFFFFF\"/>"
         "<circle cx=\"3.85\" cy=\"8.75\" r=\"0.5\" fill=\"#FFFFFF\"/>"
         "<circle cx=\"6.1\" cy=\"8.75\" r=\"0.5\" fill=\"#FFFFFF\"/>"
         "<circle cx=\"8.35\" cy=\"8.75\" r=\"0.5\" fill=\"#FFFFFF\"/>"
         "<circle cx=\"10.6\" cy=\"8.75\" r=\"0.5\" fill=\"#FFFFFF\"/>"
         "<circle cx=\"12.85\" cy=\"8.75\" r=\"0.5\" fill=\"#FFFFFF\"/>"
         "<circle cx=\"1.6\" cy=\"11.1\" r=\"0.5\" fill=\"#FFFFFF\"/>"
         "<circle cx=\"3.85\" cy=\"11.1\" r=\"0.5\" fill=\"#FFFFFF\"/>"
         "<circle cx=\"6.1\" cy=\"11.1\" r=\"0.5\" fill=\"#FFFFFF\"/>"
         "<circle cx=\"8.35\" cy=\"11.1\" r=\"0.5\" fill=\"#FFFFFF\"/>"
         "<circle cx=\"10.6\" cy=\"11.1\" r=\"0.5\" fill=\"#FFFFFF\"/>"
         "<circle cx=\"12.85\" cy=\"11.1\" r=\"0.5\" fill=\"#FFFFFF\"/></g></svg>"},
        // ---- drapeau-jp
        {"drapeau-jp", "Japon", "Drapeaux",
         "Japon, japonais, JP, langue, drapeau",
         false, false, 96, 64,
         "<svg xmlns=\"http://www.w3.org/2000/svg\" width=\"96\" height=\"64\" viewBox=\"0 0 36 24\">"
         "<g stroke-linecap=\"round\" stroke-linejoin=\"round\">"
         "<rect x=\"0\" y=\"0\" width=\"36\" height=\"24\" fill=\"#FFFFFF\"/>"
         "<circle cx=\"18\" cy=\"12\" r=\"7.2\" fill=\"#BC002D\"/>"
         "<rect x=\"0.25\" y=\"0.25\" width=\"35.5\" height=\"23.5\" fill=\"none\" stroke=\"#8A94A6\" stroke-width=\"0.5\"/>"
         "</g></svg>"},
        // ---- drapeau-cn
        {"drapeau-cn", "Chine", "Drapeaux",
         "Chine, chinois, CN, langue, drapeau",
         false, false, 96, 64,
         "<svg xmlns=\"http://www.w3.org/2000/svg\" width=\"96\" height=\"64\" viewBox=\"0 0 36 24\">"
         "<g stroke-linecap=\"round\" stroke-linejoin=\"round\">"
         "<rect x=\"0\" y=\"0\" width=\"36\" height=\"24\" fill=\"#EE1C25\"/>"
         "<polygon points=\"6 2.4 6.823 4.867 9.424 4.888 7.331 6.433 8.116 8.912 6 7.4 3.884 8.912 4.669 6.433 2.576 4.888 5.177 4.867\" fill=\"#FFFF00\"/>"
         "<polygon points=\"12.41 1.272 12.398 2.132 13.199 2.442 12.378 2.696 12.331 3.554 11.836 2.851 11.005 3.071 11.52 2.383 11.054 1.661 11.868 1.939\" fill=\"#FFFF00\"/>"
         "<polygon points=\"15.171 3.881 14.866 4.684 15.513 5.25 14.654 5.207 14.316 5.997 14.091 5.168 13.236 5.09 13.955 4.62 13.764 3.782 14.433 4.321\" fill=\"#FFFF00\"/>"
         "<polygon points=\"14.4 7.2 14.682 8.012 15.541 8.029 14.857 8.548 15.105 9.371 14.4 8.88 13.695 9.371 13.943 8.548 13.259 8.029 14.118 8.012\" fill=\"#FFFF00\"/>"
         "<polygon points=\"11.59 9.672 12.132 10.339 12.946 10.061 12.48 10.783 12.995 11.471 12.164 11.251 11.669 11.954 11.622 11.096 10.801 10.842 11.602 10.532\" fill=\"#FFFF00\"/>"
         "</g></svg>"},
        // ---- pastille-verte
        {"pastille-verte", "Pastille verte", "Formes et pastilles",
         "pastille, voyant, vert, marche, en service",
         false, false, 64, 64,
         "<svg xmlns=\"http://www.w3.org/2000/svg\" width=\"64\" height=\"64\" viewBox=\"0 0 24 24\">"
         "<g stroke-linecap=\"round\" stroke-linejoin=\"round\">"
         "<circle cx=\"12\" cy=\"12\" r=\"9.5\" fill=\"#1E9A4A\"/>"
         "<circle cx=\"12\" cy=\"12\" r=\"9.5\" fill=\"none\" stroke=\"#14703A\" stroke-width=\"1.2\"/>"
         "<ellipse cx=\"9.5\" cy=\"8.6\" rx=\"3.6\" ry=\"2.1\" fill=\"#FFFFFF\" opacity=\"0.3\"/></g></svg>"},
        // ---- pastille-rouge
        {"pastille-rouge", "Pastille rouge", "Formes et pastilles",
         "pastille, voyant, rouge, d\xC3\xA9" "faut, arr\xC3\xAAt",
         false, false, 64, 64,
         "<svg xmlns=\"http://www.w3.org/2000/svg\" width=\"64\" height=\"64\" viewBox=\"0 0 24 24\">"
         "<g stroke-linecap=\"round\" stroke-linejoin=\"round\">"
         "<circle cx=\"12\" cy=\"12\" r=\"9.5\" fill=\"#D93A2B\"/>"
         "<circle cx=\"12\" cy=\"12\" r=\"9.5\" fill=\"none\" stroke=\"#9E241A\" stroke-width=\"1.2\"/>"
         "<ellipse cx=\"9.5\" cy=\"8.6\" rx=\"3.6\" ry=\"2.1\" fill=\"#FFFFFF\" opacity=\"0.3\"/></g></svg>"},
        // ---- pastille-orange
        {"pastille-orange", "Pastille orange", "Formes et pastilles",
         "pastille, voyant, orange, alerte, attente",
         false, false, 64, 64,
         "<svg xmlns=\"http://www.w3.org/2000/svg\" width=\"64\" height=\"64\" viewBox=\"0 0 24 24\">"
         "<g stroke-linecap=\"round\" stroke-linejoin=\"round\">"
         "<circle cx=\"12\" cy=\"12\" r=\"9.5\" fill=\"#EE8A1F\"/>"
         "<circle cx=\"12\" cy=\"12\" r=\"9.5\" fill=\"none\" stroke=\"#B5620E\" stroke-width=\"1.2\"/>"
         "<ellipse cx=\"9.5\" cy=\"8.6\" rx=\"3.6\" ry=\"2.1\" fill=\"#FFFFFF\" opacity=\"0.3\"/></g></svg>"},
        // ---- pastille-jaune
        {"pastille-jaune", "Pastille jaune", "Formes et pastilles",
         "pastille, voyant, jaune, avertissement",
         false, false, 64, 64,
         "<svg xmlns=\"http://www.w3.org/2000/svg\" width=\"64\" height=\"64\" viewBox=\"0 0 24 24\">"
         "<g stroke-linecap=\"round\" stroke-linejoin=\"round\">"
         "<circle cx=\"12\" cy=\"12\" r=\"9.5\" fill=\"#F5C400\"/>"
         "<circle cx=\"12\" cy=\"12\" r=\"9.5\" fill=\"none\" stroke=\"#B38F00\" stroke-width=\"1.2\"/>"
         "<ellipse cx=\"9.5\" cy=\"8.6\" rx=\"3.6\" ry=\"2.1\" fill=\"#FFFFFF\" opacity=\"0.35\"/></g></svg>"},
        // ---- pastille-bleue
        {"pastille-bleue", "Pastille bleue", "Formes et pastilles",
         "pastille, voyant, bleu, information, manuel",
         false, false, 64, 64,
         "<svg xmlns=\"http://www.w3.org/2000/svg\" width=\"64\" height=\"64\" viewBox=\"0 0 24 24\">"
         "<g stroke-linecap=\"round\" stroke-linejoin=\"round\">"
         "<circle cx=\"12\" cy=\"12\" r=\"9.5\" fill=\"#1F64B5\"/>"
         "<circle cx=\"12\" cy=\"12\" r=\"9.5\" fill=\"none\" stroke=\"#164A87\" stroke-width=\"1.2\"/>"
         "<ellipse cx=\"9.5\" cy=\"8.6\" rx=\"3.6\" ry=\"2.1\" fill=\"#FFFFFF\" opacity=\"0.3\"/></g></svg>"},
        // ---- pastille-grise
        {"pastille-grise", "Pastille grise", "Formes et pastilles",
         "pastille, voyant, gris, inactif, hors service",
         false, false, 64, 64,
         "<svg xmlns=\"http://www.w3.org/2000/svg\" width=\"64\" height=\"64\" viewBox=\"0 0 24 24\">"
         "<g stroke-linecap=\"round\" stroke-linejoin=\"round\">"
         "<circle cx=\"12\" cy=\"12\" r=\"9.5\" fill=\"#8A94A6\"/>"
         "<circle cx=\"12\" cy=\"12\" r=\"9.5\" fill=\"none\" stroke=\"#5B6576\" stroke-width=\"1.2\"/>"
         "<ellipse cx=\"9.5\" cy=\"8.6\" rx=\"3.6\" ry=\"2.1\" fill=\"#FFFFFF\" opacity=\"0.3\"/></g></svg>"},
        // ---- statut-ok
        {"statut-ok", "\xC3\x89tat correct", "Formes et pastilles",
         "\xC3\xA9tat, ok, correct, vert, coche",
         false, false, 64, 64,
         "<svg xmlns=\"http://www.w3.org/2000/svg\" width=\"64\" height=\"64\" viewBox=\"0 0 24 24\">"
         "<g stroke-linecap=\"round\" stroke-linejoin=\"round\">"
         "<circle cx=\"12\" cy=\"12\" r=\"10\" fill=\"#1E9A4A\"/>"
         "<path d=\"M7.3 12.4l3.2 3.2 6.3-6.6\" fill=\"none\" stroke=\"#FFFFFF\" stroke-width=\"2.4\"/></g></svg>"},
        // ---- statut-erreur
        {"statut-erreur", "Erreur", "Formes et pastilles",
         "\xC3\xA9tat, erreur, d\xC3\xA9" "faut, rouge, croix",
         false, false, 64, 64,
         "<svg xmlns=\"http://www.w3.org/2000/svg\" width=\"64\" height=\"64\" viewBox=\"0 0 24 24\">"
         "<g stroke-linecap=\"round\" stroke-linejoin=\"round\">"
         "<circle cx=\"12\" cy=\"12\" r=\"10\" fill=\"#D93A2B\"/>"
         "<path d=\"M8.3 8.3l7.4 7.4M15.7 8.3l-7.4 7.4\" fill=\"none\" stroke=\"#FFFFFF\" stroke-width=\"2.4\"/></g>"
         "</svg>"},
        // ---- statut-alerte
        {"statut-alerte", "Alerte", "Formes et pastilles",
         "\xC3\xA9tat, alerte, avertissement, orange",
         false, false, 64, 64,
         "<svg xmlns=\"http://www.w3.org/2000/svg\" width=\"64\" height=\"64\" viewBox=\"0 0 24 24\">"
         "<g stroke-linecap=\"round\" stroke-linejoin=\"round\">"
         "<circle cx=\"12\" cy=\"12\" r=\"10\" fill=\"#EE8A1F\"/>"
         "<path d=\"M12 6.5v7\" fill=\"none\" stroke=\"#FFFFFF\" stroke-width=\"2.6\"/>"
         "<circle cx=\"12\" cy=\"17.2\" r=\"1.5\" fill=\"#FFFFFF\"/></g></svg>"},
        // ---- statut-info
        {"statut-info", "Information (pastille)", "Formes et pastilles",
         "\xC3\xA9tat, information, bleu, info",
         false, false, 64, 64,
         "<svg xmlns=\"http://www.w3.org/2000/svg\" width=\"64\" height=\"64\" viewBox=\"0 0 24 24\">"
         "<g stroke-linecap=\"round\" stroke-linejoin=\"round\">"
         "<circle cx=\"12\" cy=\"12\" r=\"10\" fill=\"#1F64B5\"/>"
         "<path d=\"M12 10.5v7\" fill=\"none\" stroke=\"#FFFFFF\" stroke-width=\"2.6\"/>"
         "<circle cx=\"12\" cy=\"6.8\" r=\"1.5\" fill=\"#FFFFFF\"/></g></svg>"},
        // ---- statut-aide
        {"statut-aide", "Aide (pastille)", "Formes et pastilles",
         "\xC3\xA9tat, aide, question, bleu",
         false, false, 64, 64,
         "<svg xmlns=\"http://www.w3.org/2000/svg\" width=\"64\" height=\"64\" viewBox=\"0 0 24 24\">"
         "<g stroke-linecap=\"round\" stroke-linejoin=\"round\">"
         "<circle cx=\"12\" cy=\"12\" r=\"10\" fill=\"#1F64B5\"/>"
         "<path d=\"M9.3 9.3a2.8 2.8 0 1 1 3.9 2.6c-.8.4-1.2 1-1.2 1.8v.6\" fill=\"none\" stroke=\"#FFFFFF\" stroke-width=\"2.2\"/>"
         "<circle cx=\"12\" cy=\"17.2\" r=\"1.4\" fill=\"#FFFFFF\"/></g></svg>"},
        // ---- statut-arret
        {"statut-arret", "Arr\xC3\xAAt\xC3\xA9", "Formes et pastilles",
         "\xC3\xA9tat, arr\xC3\xAAt, stopp\xC3\xA9, gris",
         false, false, 64, 64,
         "<svg xmlns=\"http://www.w3.org/2000/svg\" width=\"64\" height=\"64\" viewBox=\"0 0 24 24\">"
         "<g stroke-linecap=\"round\" stroke-linejoin=\"round\">"
         "<circle cx=\"12\" cy=\"12\" r=\"10\" fill=\"#8A94A6\"/>"
         "<rect x=\"8\" y=\"8\" width=\"8\" height=\"8\" rx=\"1\" fill=\"#FFFFFF\"/></g></svg>"},
        // ---- badge-1
        {"badge-1", "Badge 1", "Formes et pastilles",
         "badge, num\xC3\xA9ro, 1, un, \xC3\xA9tape",
         false, false, 64, 64,
         "<svg xmlns=\"http://www.w3.org/2000/svg\" width=\"64\" height=\"64\" viewBox=\"0 0 24 24\">"
         "<g stroke-linecap=\"round\" stroke-linejoin=\"round\">"
         "<circle cx=\"12\" cy=\"12\" r=\"10\" fill=\"#1F64B5\"/><g transform=\"translate(9 7) scale(1)\">"
         "<path d=\"M1.2 2.2L3.6 0v10\" fill=\"none\" stroke=\"#FFFFFF\" stroke-width=\"2.2\"/></g></g></svg>"},
        // ---- badge-2
        {"badge-2", "Badge 2", "Formes et pastilles",
         "badge, num\xC3\xA9ro, 2, deux, \xC3\xA9tape",
         false, false, 64, 64,
         "<svg xmlns=\"http://www.w3.org/2000/svg\" width=\"64\" height=\"64\" viewBox=\"0 0 24 24\">"
         "<g stroke-linecap=\"round\" stroke-linejoin=\"round\">"
         "<circle cx=\"12\" cy=\"12\" r=\"10\" fill=\"#1F64B5\"/><g transform=\"translate(9 7) scale(1)\">"
         "<path d=\"M.3 2.6a2.8 2.8 0 0 1 5.5.4c0 1.6-1 2.5-2.4 3.7L.2 10H6\" fill=\"none\" stroke=\"#FFFFFF\" stroke-width=\"2.2\"/>"
         "</g></g></svg>"},
        // ---- badge-3
        {"badge-3", "Badge 3", "Formes et pastilles",
         "badge, num\xC3\xA9ro, 3, trois, \xC3\xA9tape",
         false, false, 64, 64,
         "<svg xmlns=\"http://www.w3.org/2000/svg\" width=\"64\" height=\"64\" viewBox=\"0 0 24 24\">"
         "<g stroke-linecap=\"round\" stroke-linejoin=\"round\">"
         "<circle cx=\"12\" cy=\"12\" r=\"10\" fill=\"#1F64B5\"/><g transform=\"translate(9 7) scale(1)\">"
         "<path d=\"M.6 0h5.1L3 3.9a3.05 3.05 0 1 1-2.8 4.6\" fill=\"none\" stroke=\"#FFFFFF\" stroke-width=\"2.2\"/>"
         "</g></g></svg>"},
        // ---- badge-4
        {"badge-4", "Badge 4", "Formes et pastilles",
         "badge, num\xC3\xA9ro, 4, quatre, \xC3\xA9tape",
         false, false, 64, 64,
         "<svg xmlns=\"http://www.w3.org/2000/svg\" width=\"64\" height=\"64\" viewBox=\"0 0 24 24\">"
         "<g stroke-linecap=\"round\" stroke-linejoin=\"round\">"
         "<circle cx=\"12\" cy=\"12\" r=\"10\" fill=\"#1F64B5\"/><g transform=\"translate(9 7) scale(1)\">"
         "<path d=\"M4.6 10V0L0 7.1h6\" fill=\"none\" stroke=\"#FFFFFF\" stroke-width=\"2.2\"/></g></g></svg>"},
        // ---- badge-5
        {"badge-5", "Badge 5", "Formes et pastilles",
         "badge, num\xC3\xA9ro, 5, cinq, \xC3\xA9tape",
         false, false, 64, 64,
         "<svg xmlns=\"http://www.w3.org/2000/svg\" width=\"64\" height=\"64\" viewBox=\"0 0 24 24\">"
         "<g stroke-linecap=\"round\" stroke-linejoin=\"round\">"
         "<circle cx=\"12\" cy=\"12\" r=\"10\" fill=\"#1F64B5\"/><g transform=\"translate(9 7) scale(1)\">"
         "<path d=\"M5.6 0H1.2L.7 4.3a3.1 3.1 0 1 1-.5 4.4\" fill=\"none\" stroke=\"#FFFFFF\" stroke-width=\"2.2\"/>"
         "</g></g></svg>"},
        // ---- badge-6
        {"badge-6", "Badge 6", "Formes et pastilles",
         "badge, num\xC3\xA9ro, 6, six, \xC3\xA9tape",
         false, false, 64, 64,
         "<svg xmlns=\"http://www.w3.org/2000/svg\" width=\"64\" height=\"64\" viewBox=\"0 0 24 24\">"
         "<g stroke-linecap=\"round\" stroke-linejoin=\"round\">"
         "<circle cx=\"12\" cy=\"12\" r=\"10\" fill=\"#1F64B5\"/><g transform=\"translate(9 7) scale(1)\">"
         "<path d=\"M4.6.3C2 .9 0 3.3 0 6.6a3.05 3.05 0 1 0 1-2.2\" fill=\"none\" stroke=\"#FFFFFF\" stroke-width=\"2.2\"/>"
         "</g></g></svg>"},
        // ---- badge-7
        {"badge-7", "Badge 7", "Formes et pastilles",
         "badge, num\xC3\xA9ro, 7, sept, \xC3\xA9tape",
         false, false, 64, 64,
         "<svg xmlns=\"http://www.w3.org/2000/svg\" width=\"64\" height=\"64\" viewBox=\"0 0 24 24\">"
         "<g stroke-linecap=\"round\" stroke-linejoin=\"round\">"
         "<circle cx=\"12\" cy=\"12\" r=\"10\" fill=\"#1F64B5\"/><g transform=\"translate(9 7) scale(1)\">"
         "<path d=\"M0 0h6L2.2 10\" fill=\"none\" stroke=\"#FFFFFF\" stroke-width=\"2.2\"/></g></g></svg>"},
        // ---- badge-8
        {"badge-8", "Badge 8", "Formes et pastilles",
         "badge, num\xC3\xA9ro, 8, huit, \xC3\xA9tape",
         false, false, 64, 64,
         "<svg xmlns=\"http://www.w3.org/2000/svg\" width=\"64\" height=\"64\" viewBox=\"0 0 24 24\">"
         "<g stroke-linecap=\"round\" stroke-linejoin=\"round\">"
         "<circle cx=\"12\" cy=\"12\" r=\"10\" fill=\"#1F64B5\"/><g transform=\"translate(9 7) scale(1)\">"
         "<path d=\"M3 4.6A2.3 2.3 0 1 1 3 0a2.3 2.3 0 0 1 0 4.6zm0 0a2.7 2.7 0 1 0 0 5.4 2.7 2.7 0 0 0 0-5.4z\" fill=\"none\" stroke=\"#FFFFFF\" stroke-width=\"2.2\"/>"
         "</g></g></svg>"},
        // ---- badge-9
        {"badge-9", "Badge 9", "Formes et pastilles",
         "badge, num\xC3\xA9ro, 9, neuf, \xC3\xA9tape",
         false, false, 64, 64,
         "<svg xmlns=\"http://www.w3.org/2000/svg\" width=\"64\" height=\"64\" viewBox=\"0 0 24 24\">"
         "<g stroke-linecap=\"round\" stroke-linejoin=\"round\">"
         "<circle cx=\"12\" cy=\"12\" r=\"10\" fill=\"#1F64B5\"/><g transform=\"translate(9 7) scale(1)\">"
         "<path d=\"M1.4 9.7C4 9.1 6 6.7 6 3.4a3.05 3.05 0 1 0-1 2.2\" fill=\"none\" stroke=\"#FFFFFF\" stroke-width=\"2.2\"/>"
         "</g></g></svg>"},
        // ---- cercle
        {"cercle", "Cercle", "Formes et pastilles",
         "cercle, rond, forme",
         true, true, 64, 64,
         "<svg xmlns=\"http://www.w3.org/2000/svg\" width=\"64\" height=\"64\" viewBox=\"0 0 24 24\">"
         "<g stroke-linecap=\"round\" stroke-linejoin=\"round\">"
         "<circle cx=\"12\" cy=\"12\" r=\"9\" fill=\"none\" stroke=\"#E6EAF0\" stroke-width=\"2\"/></g></svg>"},
        // ---- disque
        {"disque", "Disque", "Formes et pastilles",
         "disque, rond plein, point, forme",
         true, true, 64, 64,
         "<svg xmlns=\"http://www.w3.org/2000/svg\" width=\"64\" height=\"64\" viewBox=\"0 0 24 24\">"
         "<g stroke-linecap=\"round\" stroke-linejoin=\"round\">"
         "<circle cx=\"12\" cy=\"12\" r=\"9\" fill=\"#E6EAF0\"/></g></svg>"},
        // ---- carre
        {"carre", "Carr\xC3\xA9", "Formes et pastilles",
         "carr\xC3\xA9, forme, cadre",
         true, true, 64, 64,
         "<svg xmlns=\"http://www.w3.org/2000/svg\" width=\"64\" height=\"64\" viewBox=\"0 0 24 24\">"
         "<g stroke-linecap=\"round\" stroke-linejoin=\"round\">"
         "<rect x=\"3.5\" y=\"3.5\" width=\"17\" height=\"17\" rx=\"0.5\" fill=\"none\" stroke=\"#E6EAF0\" stroke-width=\"2\"/>"
         "</g></svg>"},
        // ---- carre-plein
        {"carre-plein", "Carr\xC3\xA9 plein", "Formes et pastilles",
         "carr\xC3\xA9 plein, forme, bloc",
         true, true, 64, 64,
         "<svg xmlns=\"http://www.w3.org/2000/svg\" width=\"64\" height=\"64\" viewBox=\"0 0 24 24\">"
         "<g stroke-linecap=\"round\" stroke-linejoin=\"round\">"
         "<rect x=\"3.5\" y=\"3.5\" width=\"17\" height=\"17\" rx=\"0.5\" fill=\"#E6EAF0\"/></g></svg>"},
        // ---- carre-arrondi
        {"carre-arrondi", "Carr\xC3\xA9 arrondi", "Formes et pastilles",
         "carr\xC3\xA9 arrondi, bouton, forme",
         true, true, 64, 64,
         "<svg xmlns=\"http://www.w3.org/2000/svg\" width=\"64\" height=\"64\" viewBox=\"0 0 24 24\">"
         "<g stroke-linecap=\"round\" stroke-linejoin=\"round\">"
         "<rect x=\"3.5\" y=\"3.5\" width=\"17\" height=\"17\" rx=\"4.5\" fill=\"none\" stroke=\"#E6EAF0\" stroke-width=\"2\"/>"
         "</g></svg>"},
        // ---- triangle
        {"triangle", "Triangle", "Formes et pastilles",
         "triangle, forme",
         true, true, 64, 64,
         "<svg xmlns=\"http://www.w3.org/2000/svg\" width=\"64\" height=\"64\" viewBox=\"0 0 24 24\">"
         "<g stroke-linecap=\"round\" stroke-linejoin=\"round\">"
         "<path d=\"M12 3.5L21.5 20h-19z\" fill=\"none\" stroke=\"#E6EAF0\" stroke-width=\"2\"/></g></svg>"},
        // ---- triangle-plein
        {"triangle-plein", "Triangle plein", "Formes et pastilles",
         "triangle plein, forme",
         true, true, 64, 64,
         "<svg xmlns=\"http://www.w3.org/2000/svg\" width=\"64\" height=\"64\" viewBox=\"0 0 24 24\">"
         "<g stroke-linecap=\"round\" stroke-linejoin=\"round\"><path d=\"M12 3L22 20.5H2z\" fill=\"#E6EAF0\"/></g>"
         "</svg>"},
        // ---- losange
        {"losange", "Losange", "Formes et pastilles",
         "losange, forme, d\xC3\xA9" "cision",
         true, true, 64, 64,
         "<svg xmlns=\"http://www.w3.org/2000/svg\" width=\"64\" height=\"64\" viewBox=\"0 0 24 24\">"
         "<g stroke-linecap=\"round\" stroke-linejoin=\"round\">"
         "<path d=\"M12 2.5l9.5 9.5-9.5 9.5L2.5 12z\" fill=\"none\" stroke=\"#E6EAF0\" stroke-width=\"2\"/></g></svg>"},
        // ---- hexagone
        {"hexagone", "Hexagone", "Formes et pastilles",
         "hexagone, forme",
         true, true, 64, 64,
         "<svg xmlns=\"http://www.w3.org/2000/svg\" width=\"64\" height=\"64\" viewBox=\"0 0 24 24\">"
         "<g stroke-linecap=\"round\" stroke-linejoin=\"round\">"
         "<polygon points=\"12 2.5 20.2 7.25 20.2 16.75 12 21.5 3.8 16.75 3.8 7.25\" fill=\"none\" stroke=\"#E6EAF0\" stroke-width=\"2\"/>"
         "</g></svg>"},
        // ---- etoile-forme
        {"etoile-forme", "\xC3\x89toile pleine", "Formes et pastilles",
         "\xC3\xA9toile, forme, note",
         true, true, 64, 64,
         "<svg xmlns=\"http://www.w3.org/2000/svg\" width=\"64\" height=\"64\" viewBox=\"0 0 24 24\">"
         "<g stroke-linecap=\"round\" stroke-linejoin=\"round\">"
         "<polygon points=\"12 2.8 14.469 9.402 21.511 9.71 15.994 14.098 17.878 20.89 12 17 6.122 20.89 8.006 14.098 2.489 9.71 9.531 9.402\" fill=\"#E6EAF0\"/>"
         "</g></svg>"},
        // ---- coeur
        {"coeur", "C\xC5\x93ur", "Formes et pastilles",
         "c\xC5\x93ur, favori, sant\xC3\xA9",
         true, true, 64, 64,
         "<svg xmlns=\"http://www.w3.org/2000/svg\" width=\"64\" height=\"64\" viewBox=\"0 0 24 24\">"
         "<g stroke-linecap=\"round\" stroke-linejoin=\"round\">"
         "<path d=\"M12 20.5s-8.5-5.2-8.5-11.2a4.5 4.5 0 0 1 8.5-2 4.5 4.5 0 0 1 8.5 2c0 6-8.5 11.2-8.5 11.2z\" fill=\"none\" stroke=\"#E6EAF0\" stroke-width=\"2\"/>"
         "</g></svg>"},
        // ---- fleche-bloc
        {"fleche-bloc", "Fl\xC3\xA8" "che pleine", "Formes et pastilles",
         "fl\xC3\xA8" "che, pleine, direction, forme",
         true, true, 64, 64,
         "<svg xmlns=\"http://www.w3.org/2000/svg\" width=\"64\" height=\"64\" viewBox=\"0 0 24 24\">"
         "<g stroke-linecap=\"round\" stroke-linejoin=\"round\">"
         "<path d=\"M3 9h10V4.5l8.5 7.5-8.5 7.5V15H3z\" fill=\"#E6EAF0\"/></g></svg>"},
        // ---- bulle
        {"bulle", "Bulle", "Formes et pastilles",
         "bulle, message, commentaire, dialogue",
         true, true, 64, 64,
         "<svg xmlns=\"http://www.w3.org/2000/svg\" width=\"64\" height=\"64\" viewBox=\"0 0 24 24\">"
         "<g stroke-linecap=\"round\" stroke-linejoin=\"round\">"
         "<path d=\"M4 5h16a1.5 1.5 0 0 1 1.5 1.5v9a1.5 1.5 0 0 1-1.5 1.5H10l-4.5 3.5V17H4a1.5 1.5 0 0 1-1.5-1.5v-9A1.5 1.5 0 0 1 4 5z\" fill=\"none\" stroke=\"#E6EAF0\" stroke-width=\"2\"/>"
         "</g></svg>"},
        // ---- anneau
        {"anneau", "Anneau", "Formes et pastilles",
         "anneau, cercle \xC3\xA9pais, forme",
         true, true, 64, 64,
         "<svg xmlns=\"http://www.w3.org/2000/svg\" width=\"64\" height=\"64\" viewBox=\"0 0 24 24\">"
         "<g stroke-linecap=\"round\" stroke-linejoin=\"round\">"
         "<circle cx=\"12\" cy=\"12\" r=\"7.5\" fill=\"none\" stroke=\"#E6EAF0\" stroke-width=\"4\"/></g></svg>"},
    };
    return all;
}

} // namespace hmi::imagelib
