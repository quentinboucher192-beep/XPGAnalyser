# -*- coding: utf-8 -*-
# =============================================================================
#  outils/natives/catalogue.py - 1.12.0 : LE CATALOGUE DES NATIVES DE L'IHM
# -----------------------------------------------------------------------------
#  La seule source des fiches de la branche Natives (IHM > Programmation generale) :
#  les categories, les fonctions, leur syntaxe en ST, C et C++, leurs exemples ; les
#  operateurs, les instructions, les enumerations natives et ce que les types ont en
#  plus du registre (litteraux, C, C++, notes). outils/natives/generer.py en ecrit
#  src/hmi/HmiNativesData.cpp ; l'essai natives (tests/hmi_natives_test.cpp) execute
#  chaque exemple dans le moteur de l'IHM (script ST, expression, texte a trous).
#
#  Variables du projet d'essai : Niveau : REAL = 42.5 ; Compteur : INT = 7 ;
#  Texte : STRING = 'Pompe 3' ; Marche : BOOL = TRUE ; Mot : WORD = 16#00F0 ;
#  Tab : ARRAY[1..5] OF INT.
# =============================================================================

CATS = [
    # id, titre, phrase, groupe
    ("math", "Mathématiques", "Valeur absolue, racine, puissances, logarithmes, trigonométrie, arrondis.", "Fonctions standard"),
    ("select", "Sélection et bornes", "Choisir une valeur, la plus petite, la plus grande, borner.", "Fonctions standard"),
    ("texte", "Chaînes de caractères", "Longueur, extraits, recherche, assemblage, majuscules.", "Fonctions standard"),
    ("bits", "Bits et décalages", "Décaler et faire tourner les bits d'un mot ; compter, tester, mettre à 1 ou à 0 un bit (<bit>).", "Fonctions standard"),
    ("conv", "Conversions", "Passer d'un type à l'autre : TO_xxx et X_TO_Y.", "Fonctions standard"),
    ("tab", "Tableaux", "Taille et bornes d'un tableau.", "Le dialecte de l'IHM"),
    ("map", "MAP et itérateurs", "Les tables associatives (clé → valeur) et leur parcours.", "Le dialecte de l'IHM"),
    ("liste", "Listes, vecteurs et tuples", "Les collections de taille variable (LIST OF T, VECTOR OF T) : ajouter, retirer, chercher, trier ; les TUPLE. Ce qui ne change pas la taille lit aussi un ARRAY.", "Le dialecte de l'IHM"),
    ("ensembles", "Ensembles et tampons (SET, RING)", "SET OF T : chaque valeur une fois, rangée (std::set) ; RING[N] OF T : les N dernières valeurs, la plus ancienne part quand il est plein.", "Le dialecte de l'IHM"),   # 1.12.7
    ("ref", "Références et pointeurs", "Désigner une variable sans la copier.", "Le dialecte de l'IHM"),
    # 1.12.7 : la bibliotheque standard du C++, en ST
    ("numeric", "std::numeric", "Plier, cumuler, remplir une suite ; PGCD, PPCM, milieu ; le calcul saturé (C++26).", "Bibliothèques"),
    ("limits", "std::numeric_limits", "Les bornes et les caractéristiques de chaque type numérique : NUMERIC_LIMITS<T>.MEMBRE.", "Bibliothèques"),
    ("numbers", "std::numbers", "Les constantes mathématiques : NUMBERS.PI, NUMBERS<REAL>.E…", "Bibliothèques"),
    ("utility", "std::utility", "Échanger deux variables ; écrire en gardant l'ancienne valeur.", "Bibliothèques"),   # 1.12.16
    # 1.13.0 (lot J3) : JSON et expressions regulieres (local, du calcul en memoire)
    ("json", "JSON", "Lire et écrire du JSON (JSON_LIRE, JSON_ECRIRE), accéder par clé ['clé'] ou indice [i], lire sûrement (JSON_NOMBRE / TEXTE / BOOL / ENTIER), interroger (JSON_TYPE / A / CLES). Le type ANY porte une valeur JSON / dynamique. Tout en local.", "Bibliothèques"),
    ("regex", "Expressions régulières", "Chercher, extraire, remplacer, découper un texte au motif (grammaire ECMAScript, comme JavaScript) : REGEX_TESTE, REGEX_TROUVE, REGEX_REMPLACE, REGEX_DECOUPE… (<regex>).", "Bibliothèques"),
    # 1.13.0 (lot J6) : les unites de mesure dans les types
    ("unites", "Unités de mesure", "Donner une unité physique à une variable ou à un membre (bar, degC, m3/h, %) : EN convertit, UNITE lit le symbole, SANS_UNITE l'enlève. Le contrôle refuse bar + mbar et convertit d'office ce qui est compatible.", "Le dialecte de l'IHM"),
    ("nav", "Navigation et vues", "Changer de vue, revenir, l'accueil.", "Fonctions IHM_"),
    ("popup", "Popups", "Ouvrir, remplacer, centrer, fermer une popup.", "Fonctions IHM_"),
    ("journal", "Journal et Console", "Écrire une ligne dans le journal ou la Console.", "Fonctions IHM_"),
    ("script", "Scripts et temps", "Appeler un script, le temps de l'IHM.", "Fonctions IHM_"),
    ("user", "Utilisateurs", "Qui est connecté, son groupe, son niveau ; déconnecter.", "Fonctions IHM_"),
    ("alarme", "Alarmes", "Mettre de côté, remettre, faire taire.", "Fonctions IHM_"),
    ("affichage", "Affichage et son", "Langue, thème, paramètres système, sons, couper le son et volumes.", "Fonctions IHM_"),
    ("gif", "GIF animés", "Jouer, mettre en pause, arrêter, rejouer.", "Fonctions IHM_"),
    ("equip", "Équipements et réseau", "État, ping, esclave simulé.", "Fonctions IHM_"),
    ("export", "Exports", "Exporter des données en CSV, XLSX ou PDF.", "Fonctions IHM_"),
    ("couleur", "Couleurs", "Composer, mélanger, éclaircir une couleur ; une couleur selon une valeur.", "Couleurs et aléatoire"),
    ("alea", "Aléatoire", "Des nombres au hasard, et une suite qu'on peut rejouer.", "Couleurs et aléatoire"),
    ("date", "Dates et heures", "Maintenant, la date ou l'heure d'une date et heure, les assembler.", "Couleurs et aléatoire"),
    ("op", "Opérateurs", "Calculer, comparer, combiner : + - * / MOD ** AND OR...", "Le langage"),
    ("instr", "Instructions", "IF, CASE, FOR, FOR EACH, WHILE, REPEAT, EXIT, CONTINUE, RETURN, TRY, ASSERT.", "Le langage"),
]

# Une fonction : dict(name, cat, params=[(nom, type, phrase, facultatif)], ret, short, notes=[...],
#   st (exemple ST), c, cpp (la meme chose en C, en C++), essai=(type R, expression, attendu, prelude),
#   ou : 'S F X T' (Script, Fonction, eXpression fx, Texte {}) ; plc : l'equivalent dans l'automate.
#   1.12.7 : expr=False - pas dans une expression fx (elle ecrit son argument), quelle que soit la categorie.
#   1.12.16 : inc=, incpp= - 'Same', 'Equivalent' ou 'None' : le C et le C++ que dit la fiche (sans elles,
#   le generateur les devine : le meme nom ecrit en majuscules, sinon une autre ecriture).
F = []
def fn(**k):
    F.append(k)

# ------------------------------------------------------------------ mathematiques --
fn(name="ABS", cat="math", params=[("IN", "ANY_NUM", "un nombre", False)], ret="comme IN",
   short="La valeur absolue : le nombre sans son signe.",
   st="Ecart := ABS(Consigne - Mesure);", c="Ecart = fabsf(Consigne - Mesure);   /* abs() pour un entier */",
   cpp="Ecart = std::abs(Consigne - Mesure);",
   essai=("REAL", "ABS(-2.5)", "2.5"), plc="ABS (EF)")
fn(name="SQRT", cat="math", params=[("IN", "REAL", "un nombre positif ou nul", False)], ret="REAL",
   short="La racine carrée.", notes=["Un nombre négatif ne donne pas de racine : le résultat n'est pas un nombre (NaN)."],
   st="Norme := SQRT(X * X + Y * Y);", c="Norme = sqrtf(X * X + Y * Y);", cpp="Norme = std::sqrt(X * X + Y * Y);",
   essai=("REAL", "SQRT(16.0)", "4"), plc="SQRT (EF)")
fn(name="LN", cat="math", params=[("IN", "REAL", "un nombre strictement positif", False)], ret="REAL",
   short="Le logarithme népérien (base e).", st="T := LN(Rapport) / K;", c="T = logf(Rapport) / K;", cpp="T = std::log(Rapport) / K;",
   essai=("REAL", "LN(1.0)", "0"), plc="LN (EF)")
fn(name="LOG", cat="math", params=[("IN", "REAL", "un nombre strictement positif", False)], ret="REAL",
   short="Le logarithme décimal (base 10).", st="Decibels := 20.0 * LOG(Gain);", c="Decibels = 20.0f * log10f(Gain);",
   cpp="Decibels = 20.0f * std::log10(Gain);", essai=("REAL", "LOG(1000.0)", "3"), plc="LOG (EF)")
fn(name="EXP", cat="math", params=[("IN", "REAL", "l'exposant", False)], ret="REAL",
   short="L'exponentielle : e puissance IN.", st="Decharge := EXP(-T / Tau);", c="Decharge = expf(-T / Tau);",
   cpp="Decharge = std::exp(-T / Tau);", essai=("REAL", "EXP(0.0)", "1"), plc="EXP (EF)")
fn(name="EXPT", cat="math", params=[("IN1", "REAL", "la base", False), ("IN2", "ANY_NUM", "l'exposant", False)], ret="REAL",
   short="IN1 puissance IN2.", notes=["S'écrit aussi avec l'opérateur ** : 2.0 ** 3."],
   st="Surface := 3.14159 * EXPT(Rayon, 2);", c="Surface = 3.14159f * powf(Rayon, 2);", cpp="Surface = 3.14159f * std::pow(Rayon, 2);",
   essai=("REAL", "EXPT(2.0, 3)", "8"), plc="EXPT (EF)")
for nm, fr, cfn, cppfn, ex, res in [
        ("SIN", "Le sinus d'un angle en radians.", "sinf", "std::sin", "SIN(0.0)", "0"),
        ("COS", "Le cosinus d'un angle en radians.", "cosf", "std::cos", "COS(0.0)", "1"),
        ("TAN", "La tangente d'un angle en radians.", "tanf", "std::tan", "TAN(0.0)", "0"),
        ("ASIN", "L'arc sinus, en radians (de -π/2 à π/2).", "asinf", "std::asin", "ASIN(1.0)", "1.5707963"),
        ("ACOS", "L'arc cosinus, en radians (de 0 à π).", "acosf", "std::acos", "ACOS(1.0)", "0"),
        ("ATAN", "L'arc tangente, en radians (de -π/2 à π/2).", "atanf", "std::atan", "ATAN(1.0)", "0.7853981"),
]:
    fn(name=nm, cat="math", params=[("IN", "REAL", "un angle en radians" if nm in ("SIN", "COS", "TAN") else "un nombre", False)], ret="REAL",
       short=fr, notes=["Un angle en degrés se convertit : Angle * 3.14159 / 180.0."] if nm in ("SIN", "COS", "TAN") else [],
       st=f"Y := {nm}(Angle);", c=f"Y = {cfn}(Angle);", cpp=f"Y = {cppfn}(Angle);", essai=("REAL", ex, res), plc=f"{nm} (EF)")
fn(name="TRUNC", cat="math", params=[("IN", "REAL", "un réel", False)], ret="DINT",
   short="La partie entière, vers zéro : TRUNC(2.7) = 2, TRUNC(-2.7) = -2.",
   notes=["Pour arrondir au plus proche, REAL_TO_INT ou ROUND."],
   st="Entier := TRUNC(Niveau);", c="Entier = (int32_t)truncf(Niveau);", cpp="Entier = static_cast<DINT>(std::trunc(Niveau));",
   essai=("DINT", "TRUNC(2.7)", "2"), plc="TRUNC (EF)")
fn(name="ROUND", cat="math", params=[("IN", "REAL", "un réel", False), ("N", "INT", "le nombre de décimales", True)], ret="REAL",
   short="L'arrondi au plus proche : à l'entier, ou à N décimales.",
   st="Affiche := ROUND(Niveau, 1);", c="Affiche = roundf(Niveau * 10.0f) / 10.0f;", cpp="Affiche = std::round(Niveau * 10.0f) / 10.0f;",
   essai=("REAL", "ROUND(2.47, 1)", "2.5"), plc="—")
fn(name="NEG", cat="math", params=[("IN", "ANY_NUM", "un nombre", False)], ret="comme IN",
   short="L'opposé : NEG(x) = -x.", st="Oppose := NEG(Vitesse);", c="Oppose = -Vitesse;", cpp="Oppose = -Vitesse;",
   essai=("REAL", "NEG(4.0)", "-4"), plc="NEG (EF)")

# --------------------------------------------------------------------- selection --
fn(name="SEL", cat="select", params=[("G", "BOOL", "la condition", False), ("IN0", "ANY", "si G est faux", False), ("IN1", "ANY", "si G est vrai", False)],
   ret="comme IN0", short="IN0 si G est faux, IN1 si G est vrai.", notes=["Attention à l'ordre : la valeur « faux » vient d'abord."],
   st="Couleur := SEL(Marche, '#808080', '#2ECC71');", c="Couleur = Marche ? \"#2ECC71\" : \"#808080\";   /* SEL(G, si faux, si vrai) */",
   cpp="Couleur = Marche ? \"#2ECC71\"s : \"#808080\"s;", essai=("STRING", "SEL(Marche, 'Arrêt', 'Marche')", "Marche"), plc="SEL (EF)")
fn(name="MUX", cat="select", params=[("K", "INT", "le rang (0 : IN0)", False), ("IN0…INn", "ANY", "les valeurs", False)], ret="comme IN0",
   short="La valeur de rang K : IN0 pour 0, IN1 pour 1...", notes=["Hors des bornes (K négatif ou trop grand), MUX rend IN0."],
   st="Libelle := MUX(Etape, 'Arrêt', 'Démarrage', 'Production');",
   c="static const char* libelles[] = {\"Arrêt\", \"Démarrage\", \"Production\"};\nLibelle = libelles[Etape];",
   cpp="Libelle = std::array{\"Arrêt\"s, \"Démarrage\"s, \"Production\"s}.at(Etape);",
   essai=("STRING", "MUX(1, 'A', 'B', 'C')", "B"), plc="MUX (EF)")
fn(name="MIN", cat="select", params=[("IN1…INn", "ANY", "deux valeurs ou plus", False)], ret="comme IN1",
   short="La plus petite des valeurs.", st="Bas := MIN(Mesure_1, Mesure_2, Mesure_3);", c="Bas = fminf(fminf(Mesure_1, Mesure_2), Mesure_3);",
   cpp="Bas = std::min({Mesure_1, Mesure_2, Mesure_3});", essai=("REAL", "MIN(3.0, 1.5, 2.0)", "1.5"), plc="MIN (EF)")
fn(name="MAX", cat="select", params=[("IN1…INn", "ANY", "deux valeurs ou plus", False)], ret="comme IN1",
   short="La plus grande des valeurs.", st="Haut := MAX(Mesure_1, Mesure_2);", c="Haut = fmaxf(Mesure_1, Mesure_2);",
   cpp="Haut = std::max(Mesure_1, Mesure_2);", essai=("REAL", "MAX(3.0, 1.5)", "3"), plc="MAX (EF)")
fn(name="LIMIT", cat="select", params=[("MN", "ANY", "la borne basse", False), ("IN", "ANY", "la valeur", False), ("MX", "ANY", "la borne haute", False)],
   ret="comme IN", short="IN borné entre MN et MX.", notes=["L'ordre est celui de la norme : la borne basse d'abord."],
   st="Consigne := LIMIT(0.0, Saisie, 100.0);", c="Consigne = fminf(fmaxf(Saisie, 0.0f), 100.0f);", cpp="Consigne = std::clamp(Saisie, 0.0f, 100.0f);",
   essai=("REAL", "LIMIT(0.0, 150.0, 100.0)", "100"), plc="LIMIT (EF)")

# ------------------------------------------------------------------------- texte --
fn(name="LEN", cat="texte", params=[("IN", "STRING", "le texte", False)], ret="INT",
   short="Le nombre de caractères.", st="Longueur := LEN(Texte);", c="Longueur = strlen(Texte);", cpp="Longueur = Texte.size();",
   essai=("INT", "LEN(Texte)", "7"), plc="LEN (EF) ; LEN_INT dans les projets anciens")
fn(name="LEFT", cat="texte", params=[("IN", "STRING", "le texte", False), ("L", "INT", "le nombre de caractères", False)], ret="STRING",
   short="Les L premiers caractères.", st="Prefixe := LEFT(Texte, 5);", c="strncpy(Prefixe, Texte, 5); Prefixe[5] = '\\0';",
   cpp="Prefixe = Texte.substr(0, 5);", essai=("STRING", "LEFT(Texte, 5)", "Pompe"), plc="LEFT (EF) ; LEFT_INT")
fn(name="RIGHT", cat="texte", params=[("IN", "STRING", "le texte", False), ("L", "INT", "le nombre de caractères", False)], ret="STRING",
   short="Les L derniers caractères.", st="Suffixe := RIGHT(Texte, 1);", c="strcpy(Suffixe, Texte + strlen(Texte) - 1);",
   cpp="Suffixe = Texte.substr(Texte.size() - 1);", essai=("STRING", "RIGHT(Texte, 1)", "3"), plc="RIGHT (EF) ; RIGHT_INT")
fn(name="MID", cat="texte", params=[("IN", "STRING", "le texte", False), ("L", "INT", "le nombre de caractères", False), ("P", "INT", "la position du premier (1 : le début)", False)],
   ret="STRING", short="L caractères à partir de la position P.", notes=["Les positions commencent à 1, comme dans l'automate."],
   st="Milieu := MID(Texte, 3, 2);", c="strncpy(Milieu, Texte + 1, 3); Milieu[3] = '\\0';", cpp="Milieu = Texte.substr(1, 3);   // P - 1",
   essai=("STRING", "MID(Texte, 3, 2)", "omp"), plc="MID (EF) ; MID_INT")
fn(name="CONCAT", cat="texte", params=[("IN1…INn", "STRING", "deux textes ou plus", False)], ret="STRING",
   short="Les textes mis bout à bout.", notes=["+ ne colle pas deux textes en ST : CONCAT le fait."],
   st="Titre := CONCAT('Vanne ', Nom, ' : ', Etat);", c="snprintf(Titre, sizeof Titre, \"Vanne %s : %s\", Nom, Etat);",
   cpp="Titre = \"Vanne \" + Nom + \" : \" + Etat;", essai=("STRING", "CONCAT('Vanne ', 'V-201')", "Vanne V-201"), plc="CONCAT (EF) ; CONCAT_STR")
fn(name="INSERT", cat="texte", params=[("IN1", "STRING", "le texte", False), ("IN2", "STRING", "ce qu'on insère", False), ("P", "INT", "après ce caractère", False)],
   ret="STRING", short="IN2 inséré dans IN1 après la position P.", st="Code := INSERT('AB', '-', 1);",
   c="/* pas de fonction en C : memmove puis memcpy */", cpp="Code = std::string(\"AB\").insert(1, \"-\");",
   essai=("STRING", "INSERT('AB', '-', 1)", "A-B"), plc="INSERT (EF) ; INSERT_INT")
fn(name="DELETE", cat="texte", params=[("IN", "STRING", "le texte", False), ("L", "INT", "le nombre de caractères", False), ("P", "INT", "la position du premier", False)],
   ret="STRING", short="Le texte sans ses L caractères à partir de P.", st="Court := DELETE(Texte, 2, 1);",
   c="/* pas de fonction en C : memmove */", cpp="Court = std::string(Texte).erase(0, 2);   // P - 1",
   essai=("STRING", "DELETE(Texte, 2, 1)", "mpe 3"), plc="DELETE (EF) ; DELETE_INT")
fn(name="REPLACE", cat="texte", params=[("IN1", "STRING", "le texte", False), ("IN2", "STRING", "le remplaçant", False), ("L", "INT", "le nombre de caractères remplacés", False), ("P", "INT", "la position du premier", False)],
   ret="STRING", short="L caractères de IN1, à partir de P, remplacés par IN2.", st="Nouveau := REPLACE(Texte, 'Vanne', 5, 1);",
   c="/* pas de fonction en C */", cpp="Nouveau = std::string(Texte).replace(0, 5, \"Vanne\");   // P - 1",
   essai=("STRING", "REPLACE(Texte, 'Vanne', 5, 1)", "Vanne 3"), plc="REPLACE (EF) ; REPLACE_INT")
fn(name="FIND", cat="texte", params=[("IN1", "STRING", "le texte", False), ("IN2", "STRING", "ce qu'on cherche", False)], ret="INT",
   short="La position de IN2 dans IN1 (1 : au début), 0 s'il n'y est pas.",
   st="IF FIND(Message, 'Défaut') > 0 THEN Alerte := TRUE; END_IF", c="const char* p = strstr(Message, \"Défaut\");\nPosition = p ? (int)(p - Message) + 1 : 0;",
   cpp="Position = Message.find(\"Défaut\") + 1;   // npos + 1 = 0", essai=("INT", "FIND(Texte, '3')", "7"), plc="FIND (EF) ; FIND_INT")
fn(name="TO_UPPER", cat="texte", params=[("IN", "STRING", "le texte", False)], ret="STRING",
   short="Le texte en majuscules.", st="Code := TO_UPPER(Saisie);", c="for (char* q = Code; *q; ++q) *q = toupper(*q);",
   cpp="std::transform(Code.begin(), Code.end(), Code.begin(), ::toupper);", essai=("STRING", "TO_UPPER('vanne')", "VANNE"), plc="— (IHM seulement)")
fn(name="TO_LOWER", cat="texte", params=[("IN", "STRING", "le texte", False)], ret="STRING",
   short="Le texte en minuscules.", st="Cle := TO_LOWER(Saisie);", c="for (char* q = Cle; *q; ++q) *q = tolower(*q);",
   cpp="std::transform(Cle.begin(), Cle.end(), Cle.begin(), ::tolower);", essai=("STRING", "TO_LOWER('VANNE')", "vanne"), plc="— (IHM seulement)")

# -------------------------------------------------------------------------- bits --
fn(name="SHL", cat="bits", params=[("IN", "ANY_BIT", "le mot", False), ("N", "INT", "le nombre de bits", False)], ret="comme IN",
   short="Les bits décalés de N vers la gauche (des zéros entrent à droite).", st="Masque := SHL(Mot, 4);", c="Masque = (uint16_t)(Mot << 4);",
   cpp="Masque = static_cast<WORD>(Mot << 4);", essai=("WORD", "SHL(Mot, 4)", "3840"), plc="SHL (EF)")
fn(name="SHR", cat="bits", params=[("IN", "ANY_BIT", "le mot", False), ("N", "INT", "le nombre de bits", False)], ret="comme IN",
   short="Les bits décalés de N vers la droite (des zéros entrent à gauche).", st="Haut := SHR(Mot, 4);", c="Haut = Mot >> 4;",
   cpp="Haut = Mot >> 4;", essai=("WORD", "SHR(Mot, 4)", "15"), plc="SHR (EF)")
fn(name="ROL", cat="bits", params=[("IN", "ANY_BIT", "le mot", False), ("N", "INT", "le nombre de bits", False)], ret="comme IN",
   short="Les bits tournés de N vers la gauche (ceux qui sortent reviennent à droite).", st="Tourne := ROL(Mot, 8);",
   c="Tourne = (uint16_t)((Mot << 8) | (Mot >> 8));", cpp="Tourne = std::rotl(Mot, 8);   // C++20, <bit>", essai=("WORD", "ROL(Mot, 8)", "61440"), plc="ROL (EF)")
fn(name="ROR", cat="bits", params=[("IN", "ANY_BIT", "le mot", False), ("N", "INT", "le nombre de bits", False)], ret="comme IN",
   short="Les bits tournés de N vers la droite.", st="Tourne := ROR(Mot, 4);", c="Tourne = (uint16_t)((Mot >> 4) | (Mot << 12));",
   cpp="Tourne = std::rotr(Mot, 4);   // C++20, <bit>", essai=("WORD", "ROR(Mot, 4)", "15"), plc="ROR (EF)")

# -------------------------------------------------------------------- conversions --
TO_TARGETS = ["TO_STRING", "TO_REAL", "TO_LREAL", "TO_BOOL", "TO_INT", "TO_DINT", "TO_UINT", "TO_UDINT", "TO_SINT", "TO_USINT",
              "TO_LINT", "TO_ULINT", "TO_WORD", "TO_DWORD", "TO_BYTE", "TO_TIME"]
CPP_T = {"STRING": "std::string", "REAL": "float", "LREAL": "double", "BOOL": "bool", "INT": "int16_t", "DINT": "int32_t",
         "UINT": "uint16_t", "UDINT": "uint32_t", "SINT": "int8_t", "USINT": "uint8_t", "LINT": "int64_t", "ULINT": "uint64_t",
         "WORD": "uint16_t", "DWORD": "uint32_t", "BYTE": "uint8_t", "TIME": "uint32_t /* ms */", "LWORD": "uint64_t"}
TO_ESSAI = {"TO_STRING": ("STRING", "TO_STRING(42)", "42"), "TO_REAL": ("REAL", "TO_REAL(Compteur)", "7"),
            "TO_LREAL": ("LREAL", "TO_LREAL(Compteur)", "7"), "TO_BOOL": ("BOOL", "TO_BOOL(1)", "TRUE"),
            "TO_INT": ("INT", "TO_INT(2.6)", "3"), "TO_DINT": ("DINT", "TO_DINT(Niveau)", "43"),
            "TO_UINT": ("UINT", "TO_UINT(Compteur)", "7"), "TO_UDINT": ("UDINT", "TO_UDINT(Compteur)", "7"),
            "TO_SINT": ("INT", "TO_SINT(100)", "100"), "TO_USINT": ("INT", "TO_USINT(200)", "200"),
            "TO_LINT": ("DINT", "TO_LINT(Compteur)", "7"), "TO_ULINT": ("UDINT", "TO_ULINT(Compteur)", "7"),
            "TO_WORD": ("WORD", "TO_WORD(255)", "255"), "TO_DWORD": ("DWORD", "TO_DWORD(Compteur)", "7"),
            "TO_BYTE": ("INT", "TO_BYTE(65)", "65"), "TO_TIME": ("TIME", "TO_TIME(1500)", "T#1500ms")}
for t in TO_TARGETS:
    tgt = t[3:]
    fn(name=t, cat="conv", params=[("IN", "ANY", "la valeur à convertir", False)], ret=tgt,
       short=f"La valeur convertie en {tgt}, quel que soit son type.",
       notes=(["Un réel vers un entier s'arrondit au plus proche (comme REAL_TO_INT)."] if tgt not in ("STRING", "REAL", "LREAL", "BOOL", "TIME") else [])
             + (["Sur une énumération IHM, TO_STRING rend le texte de sa valeur (son opérateur toString, s'il en a un)."] if tgt == "STRING" else []),
       st=f"Resultat := {t}(Valeur);", c=f"Resultat = ({CPP_T[tgt].split(' ')[0] if tgt != 'STRING' else 'char*'})Valeur;" if tgt != "STRING" else "snprintf(Resultat, sizeof Resultat, \"%d\", Valeur);",
       cpp=(f"Resultat = static_cast<{CPP_T[tgt].split(' ')[0]}>(Valeur);" if tgt != "STRING" else "Resultat = std::to_string(Valeur);"),
       essai=TO_ESSAI[t], plc="TO_xxx n'existe pas dans Control Expert : X_TO_Y" if t != "TO_STRING" else "—")

# ----------------------------------------------------------------------- tableaux --
fn(name="SIZEOF", cat="tab", params=[("IN", "ARRAY ou MAP", "un tableau, une MAP", False)], ret="DINT",
   short="Le nombre de cases d'un tableau (toutes dimensions), ou de clés d'une MAP.",
   notes=["Ici, SIZEOF compte des cases, pas des octets (dans Control Expert, des octets)."],
   st="FOR i := 1 TO SIZEOF(Tab) DO Somme := Somme + Tab[i]; END_FOR", c="n = sizeof Tab / sizeof Tab[0];", cpp="n = std::size(Tab);",
   essai=("DINT", "SIZEOF(Tab)", "5"), plc="SIZEOF : des octets")
fn(name="LOWER_BOUND", cat="tab", params=[("IN", "ARRAY", "le tableau", False), ("DIM", "INT", "la dimension (1 : la première)", True)], ret="DINT",
   short="Le premier indice d'une dimension.", st="FOR i := LOWER_BOUND(Tab, 1) TO UPPER_BOUND(Tab, 1) DO ... END_FOR",
   c="/* une constante en C : les tableaux commencent à 0 */", cpp="// 0 en C++ ; Tab.front()", essai=("DINT", "LOWER_BOUND(Tab, 1)", "1"), plc="LOWER_BOUND (EF)")
fn(name="UPPER_BOUND", cat="tab", params=[("IN", "ARRAY", "le tableau", False), ("DIM", "INT", "la dimension (1 : la première)", True)], ret="DINT",
   short="Le dernier indice d'une dimension.", st="Dernier := Tab[UPPER_BOUND(Tab, 1)];", c="Dernier = Tab[sizeof Tab / sizeof Tab[0] - 1];",
   cpp="Dernier = Tab.back();", essai=("DINT", "UPPER_BOUND(Tab, 1)", "5"), plc="UPPER_BOUND (EF)")

# --------------------------------------------------------------------------- MAP --
MAPDECL = "VAR m : MAP[STRING] OF REAL; END_VAR\nm['vis'] := 120.0;\nm['ecrou'] := 80.0;"
fn(name="MAP_HAS", cat="map", params=[("M", "MAP", "la MAP", False), ("CLE", "comme la clé", "la clé cherchée", False)], ret="BOOL",
   short="Vrai si la clé est dans la MAP.", st="IF MAP_HAS(Stock, 'vis') THEN Stock['vis'] := Stock['vis'] - 1.0; END_IF",
   c="/* pas de MAP en C */", cpp="if (stock.count(\"vis\")) stock[\"vis\"] -= 1.0f;", essai=("BOOL", "MAP_HAS(m, 'vis')", "TRUE", MAPDECL), plc="—")
fn(name="MAP_GET", cat="map", params=[("M", "MAP", "la MAP", False), ("CLE", "comme la clé", "la clé", False), ("DEFAUT", "comme les valeurs", "la valeur si la clé manque", True)],
   ret="comme les valeurs", short="La valeur d'une clé, sans la créer (DEFAUT si elle manque).",
   notes=["Lire m['cle'] ne crée pas la clé : une clé absente arrête le script (« clé absente de la MAP ») ; MAP_HAS teste, MAP_GET rend DEFAUT. "
          "Écrire m['cle'] := v crée la clé.",
          "Sans DEFAUT, une clé absente est une erreur (« MAP_GET : clé absente : 'clou' »)."], st="Qte := MAP_GET(Stock, 'vis', 0.0);",
   c="/* pas de MAP en C */", cpp="auto it = stock.find(\"vis\");\nQte = it != stock.end() ? it->second : 0.0f;", essai=("REAL", "MAP_GET(m, 'ecrou', 0.0)", "80", MAPDECL), plc="—")
fn(name="MAP_REMOVE", cat="map", params=[("M", "MAP", "la MAP", False), ("CLE", "comme la clé", "la clé à retirer", False)], ret="BOOL",
   short="Retire une clé ; vrai si elle y était.", st="MAP_REMOVE(Stock, 'ecrou');", c="/* pas de MAP en C */", cpp="stock.erase(\"ecrou\");",
   essai=("BOOL", "MAP_REMOVE(m, 'vis')", "TRUE", MAPDECL), plc="—")
fn(name="MAP_SIZE", cat="map", params=[("M", "MAP", "la MAP", False)], ret="DINT", short="Le nombre de clés.",
   st="Nb := MAP_SIZE(Stock);", c="/* pas de MAP en C */", cpp="Nb = stock.size();", essai=("DINT", "MAP_SIZE(m)", "2", MAPDECL), plc="—")
fn(name="MAP_CLEAR", cat="map", params=[("M", "MAP", "la MAP", False)], ret="BOOL", short="Retire toutes les clés.",
   st="MAP_CLEAR(Stock);", c="/* pas de MAP en C */", cpp="stock.clear();", essai=None, plc="—")
fn(name="MAP_KEYS", cat="map", params=[("M", "MAP", "la MAP", False)], ret="ARRAY OF clé", short="Les clés, dans l'ordre (un tableau).",
   st="Cles := MAP_KEYS(Stock);", c="/* pas de MAP en C */", cpp="for (const auto& [cle, v] : stock) cles.push_back(cle);", essai=None, plc="—")
fn(name="MAP_BEGIN", cat="map", params=[("M", "MAP", "la MAP", False)], ret="MAP_ITERATOR", short="Un itérateur sur la première clé.",
   notes=["FOR EACH cle, valeur IN m DO ... END_FOR fait le même parcours, plus simplement."],
   st="it := MAP_BEGIN(Stock);\nWHILE NOT MAP_END(it) DO\n    Total := Total + it.Value;\n    MAP_NEXT(it);\nEND_WHILE",
   c="/* pas de MAP en C */", cpp="for (auto it = stock.begin(); it != stock.end(); ++it) total += it->second;", essai=None, plc="—")
fn(name="MAP_NEXT", cat="map", params=[("IT", "MAP_ITERATOR", "l'itérateur", False)], ret="BOOL", short="Passe à la clé suivante ; faux à la fin.",
   st="MAP_NEXT(it);", c="/* pas de MAP en C */", cpp="++it;", essai=None, plc="—")
fn(name="MAP_END", cat="map", params=[("IT", "MAP_ITERATOR", "l'itérateur", False)], ret="BOOL", short="Vrai quand l'itérateur a passé la dernière clé.",
   st="WHILE NOT MAP_END(it) DO ... END_WHILE", c="/* pas de MAP en C */", cpp="it != stock.end()", essai=None, plc="—")

# ------------------------------------------------------- 1.12.2 : listes et vecteurs --
LDECL = "VAR L : LIST OF INT := [3, 1, 2]; END_VAR"
VDECL = "VAR V : VECTOR OF STRING := ['a', 'b', 'c']; END_VAR"
NOC = "/* pas de liste en C : un tableau et un compteur */"
fn(name="LIST_ADD", cat="liste", params=[("L", "LIST OF T", "la liste", False), ("X", "T", "la valeur ajoutée", False)], ret="DINT",
   short="Ajoute la valeur à la fin ; rend le nombre d'éléments.", notes=["Un ARRAY a une taille fixe : LIST_ADD le refuse (Compiler l'avertit) — LIST_SET ou LIST_FILL l'écrivent.", "1.12.7 : sur un RING, plein, la plus ancienne part (comme RING_ADD) ; sur un SET, la valeur se range à sa place, une seule fois (comme SET_ADD)."], st="LIST_ADD(Mesures, Niveau);", c=NOC,
   cpp="mesures.push_back(niveau);", essai=("DINT", "LIST_ADD(L, 4)", "4", LDECL), plc="—")
fn(name="LIST_INSERT", cat="liste", params=[("L", "LIST OF T", "la liste", False), ("I", "DINT", "l'indice (0 : en tête)", False), ("X", "T", "la valeur", False)],
   ret="DINT", short="Insère la valeur à l'indice ; rend le nombre d'éléments.", notes=["Un indice hors de 0..L.Count est une erreur."],
   st="LIST_INSERT(Alarmes, 0, 'Defaut pompe');", c=NOC, cpp="alarmes.insert(alarmes.begin(), \"Defaut pompe\");",
   essai=("DINT", "LIST_INSERT(L, 0, 9)", "4", LDECL), plc="—")
fn(name="LIST_REMOVE_AT", cat="liste", params=[("L", "LIST OF T", "la liste", False), ("I", "DINT", "l'indice", False)], ret="BOOL",
   short="Retire l'élément de cet indice ; faux s'il est hors de la liste.", st="LIST_REMOVE_AT(Alarmes, 0);", c=NOC,
   cpp="if (i < l.size()) l.erase(l.begin() + i);", essai=("BOOL", "LIST_REMOVE_AT(L, 1)", "TRUE", LDECL), plc="—")
fn(name="LIST_REMOVE", cat="liste", params=[("L", "LIST OF T", "la liste", False), ("X", "T", "la valeur à retirer", False)], ret="BOOL",
   short="Retire la première valeur égale ; faux si elle n'y est pas.", st="LIST_REMOVE(Alarmes, 'Defaut pompe');", c=NOC,
   cpp="if (auto it = std::find(l.begin(), l.end(), x); it != l.end()) l.erase(it);", essai=("BOOL", "LIST_REMOVE(L, 2)", "TRUE", LDECL), plc="—")
fn(name="LIST_CLEAR", cat="liste", params=[("L", "LIST OF T", "la liste", False)], ret="BOOL", short="Vide la liste.",
   st="LIST_CLEAR(Mesures);", c=NOC, cpp="mesures.clear();", essai=("BOOL", "LIST_CLEAR(L)", "TRUE", LDECL), plc="—")
fn(name="LIST_COUNT", cat="liste", params=[("L", "LIST OF T", "la liste", False)], ret="DINT",
   notes=["1.12.7 : un SET (dans son ordre) et un RING (de la plus ancienne à la plus récente) aussi."], short="Le nombre d'éléments (comme L.Count).", st="Nb := LIST_COUNT(Mesures);", c=NOC, cpp="nb = mesures.size();",
   essai=("DINT", "LIST_COUNT(L)", "3", LDECL), plc="—")
fn(name="LIST_CONTAINS", cat="liste", params=[("L", "LIST, VECTOR, ARRAY", "la liste", False), ("X", "T", "la valeur cherchée", False)], ret="BOOL",
   short="Vrai si la valeur est dans la liste.", notes=["1.12.7 : il cherche aussi dans un ARRAY.", "1.12.7 : un SET (dans son ordre) et un RING (de la plus ancienne à la plus récente) aussi."], st="IF LIST_CONTAINS(Actifs, 'Four') THEN ... END_IF", c=NOC,
   cpp="std::find(l.begin(), l.end(), x) != l.end()", essai=("BOOL", "LIST_CONTAINS(L, 2)", "TRUE", LDECL), plc="—")
fn(name="LIST_INDEX_OF", cat="liste", params=[("L", "LIST, VECTOR, ARRAY", "la liste", False), ("X", "T", "la valeur cherchée", False)], ret="DINT",
   short="L'indice de la première valeur égale ; -1 si elle n'y est pas.", notes=["1.12.7 : dans un ARRAY, l'indice du tableau (Tab.Low à Tab.High).", "1.12.7 : un SET (dans son ordre) et un RING (de la plus ancienne à la plus récente) aussi."], st="i := LIST_INDEX_OF(Recettes, 'Pain');", c=NOC,
   cpp="auto it = std::find(l.begin(), l.end(), x);\ni = it == l.end() ? -1 : it - l.begin();", essai=("DINT", "LIST_INDEX_OF(L, 2)", "2", LDECL), plc="—")
fn(name="LIST_FIRST", cat="liste", params=[("L", "LIST OF T", "la liste", False)], ret="T", notes=["1.12.7 : un SET (dans son ordre) et un RING (de la plus ancienne à la plus récente) aussi."], short="Le premier élément (comme L.First) ; une liste vide : une erreur.",
   st="Premier := LIST_FIRST(File);", c=NOC, cpp="premier = file.front();", essai=("INT", "LIST_FIRST(L)", "3", LDECL), plc="—")
fn(name="LIST_LAST", cat="liste", params=[("L", "LIST OF T", "la liste", False)], ret="T", notes=["1.12.7 : un SET (dans son ordre) et un RING (de la plus ancienne à la plus récente) aussi."], short="Le dernier élément (comme L.Last) ; une liste vide : une erreur.",
   st="Dernier := LIST_LAST(Mesures);", c=NOC, cpp="dernier = mesures.back();", essai=("INT", "LIST_LAST(L)", "2", LDECL), plc="—")
fn(name="LIST_GET", cat="liste", params=[("L", "LIST, VECTOR, ARRAY", "la liste", False), ("I", "DINT", "l'indice", False), ("DEFAUT", "T", "la valeur hors de la liste", False)],
   ret="T", short="L'élément de l'indice, ou DEFAUT hors de la liste (sans erreur).", notes=["1.12.7 : il lit aussi un ARRAY, à ses indices (Tab.Low à Tab.High) ; LIST_SET écrit de même.", "1.12.7 : un SET (dans son ordre) et un RING (de la plus ancienne à la plus récente) aussi."], st="V := LIST_GET(Mesures, 5, 0.0);", c=NOC,
   cpp="v = i < mesures.size() ? mesures[i] : 0.0f;", essai=("INT", "LIST_GET(L, 9, -1)", "-1", LDECL), plc="—")
fn(name="LIST_SORT", cat="liste", params=[("L", "LIST, VECTOR, ARRAY", "des valeurs simples", False), ("DECROISSANT", "BOOL", "TRUE : du plus grand au plus petit", True)],
   ret="BOOL", short="Trie la liste (des nombres, des textes, des durées).", notes=["1.12.7 : un ARRAY aussi, en place (un tableau IHM est écrit).", "Une liste de structures : LIST_SORT_BY(L, 'Poids')."], st="LIST_SORT(Mesures);   (* LIST_SORT(Mesures, TRUE) : décroissant *)",
   c="qsort(t, n, sizeof t[0], comparer);", cpp="std::sort(mesures.begin(), mesures.end());", essai=("BOOL", "LIST_SORT(L)", "TRUE", LDECL), plc="—")
fn(name="LIST_REVERSE", cat="liste", params=[("L", "LIST, VECTOR, ARRAY", "la liste", False)], ret="BOOL", short="Renverse l'ordre des éléments.", notes=["1.12.7 : un ARRAY aussi, en place."],
   st="LIST_REVERSE(Historique);", c=NOC, cpp="std::reverse(l.begin(), l.end());", essai=("BOOL", "LIST_REVERSE(L)", "TRUE", LDECL), plc="—")
fn(name="LIST_SUM", cat="liste", params=[("L", "LIST, VECTOR, ARRAY", "des nombres", False)], ret="comme les éléments",
   short="La somme des nombres de la liste.", notes=["1.12.7 : un ARRAY aussi.", "ACCUMULATE (std::numeric) fait de même, avec une opération.", "1.12.7 : un SET (dans son ordre) et un RING (de la plus ancienne à la plus récente) aussi."], st="Total := LIST_SUM(Pesees);", c="for (i = 0; i < n; ++i) total += t[i];",
   cpp="total = std::accumulate(l.begin(), l.end(), 0.0);", essai=("DINT", "LIST_SUM(L)", "6", LDECL), plc="—")
fn(name="LIST_MIN", cat="liste", params=[("L", "LIST, VECTOR, ARRAY", "des valeurs simples", False)], ret="T",
   short="La plus petite valeur ; une liste vide : une erreur.", notes=["1.12.7 : un ARRAY aussi ; LIST_MIN_INDEX dit où elle est.", "1.12.7 : un SET (dans son ordre) et un RING (de la plus ancienne à la plus récente) aussi."], st="Mini := LIST_MIN(Mesures);", c=NOC,
   cpp="mini = *std::min_element(l.begin(), l.end());", essai=("INT", "LIST_MIN(L)", "1", LDECL), plc="—")
fn(name="LIST_MAX", cat="liste", params=[("L", "LIST, VECTOR, ARRAY", "des valeurs simples", False)], ret="T",
   short="La plus grande valeur ; une liste vide : une erreur.", notes=["1.12.7 : un ARRAY aussi ; LIST_MAX_INDEX dit où elle est.", "1.12.7 : un SET (dans son ordre) et un RING (de la plus ancienne à la plus récente) aussi."], st="Maxi := LIST_MAX(Mesures);", c=NOC,
   cpp="maxi = *std::max_element(l.begin(), l.end());", essai=("INT", "LIST_MAX(L)", "3", LDECL), plc="—")
fn(name="LIST_AVG", cat="liste", params=[("L", "LIST, VECTOR, ARRAY", "des nombres", False)], ret="REAL",
   short="La moyenne des nombres ; une liste vide : une erreur.", notes=["1.12.7 : un ARRAY aussi.", "1.12.7 : un SET (dans son ordre) et un RING (de la plus ancienne à la plus récente) aussi."], st="Moyenne := LIST_AVG(Mesures);", c=NOC,
   cpp="moyenne = std::accumulate(l.begin(), l.end(), 0.0) / l.size();", essai=("REAL", "LIST_AVG(L)", "2", LDECL), plc="—")
fn(name="VECTOR_PUSH", cat="liste", params=[("V", "VECTOR OF T", "le vecteur", False), ("X", "T", "la valeur ajoutée", False)], ret="DINT",
   short="Ajoute la valeur à la fin (comme LIST_ADD) ; rend la taille.", st="VECTOR_PUSH(Pile, Code);", c=NOC, cpp="pile.push_back(code);",
   essai=("DINT", "VECTOR_PUSH(V, 'd')", "4", VDECL), plc="—")
fn(name="VECTOR_POP", cat="liste", params=[("V", "VECTOR OF T", "le vecteur", False)], ret="T",
   short="Retire le dernier élément et le rend ; un vecteur vide : une erreur.", st="Code := VECTOR_POP(Pile);", c=NOC,
   cpp="code = pile.back();\npile.pop_back();", essai=("STRING", "VECTOR_POP(V)", "c", VDECL), plc="—")
fn(name="VECTOR_INSERT", cat="liste", params=[("V", "VECTOR OF T", "le vecteur", False), ("I", "DINT", "l'indice", False), ("X", "T", "la valeur", False)],
   ret="DINT", short="Insère la valeur à l'indice (comme LIST_INSERT).", st="VECTOR_INSERT(Etapes, 1, 'Rincage');", c=NOC,
   cpp="etapes.insert(etapes.begin() + 1, \"Rincage\");", essai=("DINT", "VECTOR_INSERT(V, 1, 'x')", "4", VDECL), plc="—")
fn(name="VECTOR_ERASE", cat="liste", params=[("V", "VECTOR OF T", "le vecteur", False), ("I", "DINT", "l'indice", False)], ret="BOOL",
   short="Retire l'élément de cet indice (comme LIST_REMOVE_AT).", st="VECTOR_ERASE(Etapes, 0);", c=NOC, cpp="etapes.erase(etapes.begin());",
   essai=("BOOL", "VECTOR_ERASE(V, 0)", "TRUE", VDECL), plc="—")
fn(name="VECTOR_CLEAR", cat="liste", params=[("V", "VECTOR OF T", "le vecteur", False)], ret="BOOL", short="Vide le vecteur.",
   st="VECTOR_CLEAR(Pile);", c=NOC, cpp="pile.clear();", essai=("BOOL", "VECTOR_CLEAR(V)", "TRUE", VDECL), plc="—")
fn(name="VECTOR_SIZE", cat="liste", params=[("V", "VECTOR OF T", "le vecteur", False)], ret="DINT", short="Le nombre d'éléments (comme V.Count).",
   st="n := VECTOR_SIZE(Pile);", c=NOC, cpp="n = pile.size();", essai=("DINT", "VECTOR_SIZE(V)", "3", VDECL), plc="—")
fn(name="VECTOR_RESIZE", cat="liste", params=[("V", "VECTOR OF T", "le vecteur", False), ("N", "DINT", "la nouvelle taille", False), ("X", "T", "la valeur des nouveaux éléments", True)],
   ret="DINT", short="Change la taille : coupe, ou ajoute des éléments (X, sinon la valeur du type).", st="VECTOR_RESIZE(Courbe, 100, 0.0);", c=NOC,
   cpp="courbe.resize(100, 0.0f);", essai=("DINT", "VECTOR_RESIZE(V, 5, 'z')", "5", VDECL), plc="—")
fn(name="VECTOR_FRONT", cat="liste", params=[("V", "VECTOR OF T", "le vecteur", False)], ret="T", short="Le premier élément.",
   st="Premier := VECTOR_FRONT(Etapes);", c=NOC, cpp="premier = etapes.front();", essai=("STRING", "VECTOR_FRONT(V)", "a", VDECL), plc="—")
fn(name="VECTOR_BACK", cat="liste", params=[("V", "VECTOR OF T", "le vecteur", False)], ret="T", short="Le dernier élément.",
   st="Dernier := VECTOR_BACK(Etapes);", c=NOC, cpp="dernier = etapes.back();", essai=("STRING", "VECTOR_BACK(V)", "c", VDECL), plc="—")
fn(name="JOIN", cat="liste", params=[("L", "LIST, VECTOR, ARRAY", "des valeurs simples", False), ("SEP", "STRING", "le séparateur (', ' sans lui)", True)],
   ret="STRING", short="Les éléments en un seul texte, séparés.", notes=["1.12.7 : un ARRAY aussi.", "1.12.7 : un SET (dans son ordre) et un RING (de la plus ancienne à la plus récente) aussi."], st="Texte := JOIN(Actifs, ', ');", c="/* strcat en boucle */",
   cpp="for (const auto& x : l) texte += (texte.empty() ? \"\" : \", \") + x;", essai=("STRING", "JOIN(V, '-')", "a-b-c", VDECL), plc="—")
fn(name="SPLIT", cat="liste", params=[("TEXTE", "STRING", "le texte à couper", False), ("SEP", "STRING", "le séparateur", False)],
   ret="LIST OF STRING", short="Le texte coupé à chaque séparateur : une liste de textes.", st="Mots := SPLIT('a;b;c', ';');",
   c="strtok(texte, \";\");", cpp="std::getline(flux, mot, ';');   // en boucle",
   essai=("DINT", "LIST_COUNT(SPLIT('a;b;c', ';'))", "3", "VAR Mots : LIST OF STRING; END_VAR"), plc="—")

# ---------------------------------------------------------------------- references --
fn(name="REF", cat="ref", params=[("X", "une variable", "pas une valeur calculée", False)], ret="REF_TO type de X",
   short="Une référence vers une variable : l'écrire par la référence écrit la variable.",
   st="VAR r : REF_TO REAL; END_VAR\nr := REF(Niveau);\nr^ := 50.0;   (* Niveau vaut 50 *)", c="float* r = &Niveau;\n*r = 50.0f;",
   cpp="float& r = Niveau;\nr = 50.0f;", essai=None, plc="REF (EF)")
fn(name="ADR", cat="ref", params=[("X", "une variable", "pas une valeur calculée", False)], ret="POINTER TO type de X",
   short="Un pointeur vers une variable (comme REF, pour POINTER TO).", st="VAR p : POINTER TO INT; END_VAR\np := ADR(Compteur);\np^ := p^ + 1;",
   c="int16_t* p = &Compteur;\n*p += 1;", cpp="auto* p = &Compteur;\n*p += 1;", essai=None, plc="ADR (EF)")

# ------------------------------------------- 1.12.7 : les conteneurs (43 natives de plus) --
#  La maquette 1.12.7 (v3) : lire et ecrire sans erreur, chercher, compter, retirer sous
#  condition (l'enumeration COMPARAISON), trier une liste de structures par un membre,
#  deplacer, faire tourner, extraire, copier vers un ARRAY ; MAP_INCREMENT, MAP_FLOOR_KEY,
#  MAP_ITEMS ; TUPLE_GET, TUPLE_SET, TUPLE_UNPACK, TUPLE_TO_STRING. Celles qui ne changent pas
#  la taille lisent aussi un ARRAY, a ses indices (Tab.Low a Tab.High) ; celles qui la changent
#  le refusent (« un ARRAY a une taille fixe »). Les variables d'essai sont celles de la maquette.
def decl(*lines):
    return "VAR\n" + "\n".join("    " + l + ";" for l in lines) + "\nEND_VAR"
D_MES = "Mesures : LIST OF REAL := [12.5, 8.0, 15.2, 8.0, 3.1]"
D_AUT = "Autres : LIST OF REAL := [1.0, 2.0]"
D_TRI = "Triee : LIST OF REAL := [1.0, 3.0, 4.5, 7.0]"
D_PILE = "Pile : VECTOR OF STRING := ['a', 'b', 'c']"
D_CASES = "Cases : ARRAY[0..4] OF INT := [4, 9, 1, 9, 6]"
D_STOCK = "Stock : MAP[STRING] OF REAL := ['ecrou' := 80.0, 'goujon' := 45.0, 'vis' := 120.0]"
D_LIVR = "Livraison : MAP[STRING] OF REAL := ['clou' := 500.0, 'vis' := 60.0]"
D_PAL = "Paliers : MAP[INT] OF REAL := [0 := 1.0, 100 := 1.2, 500 := 1.5]"
D_POINT = "Point : TUPLE(STRING, REAL) := ('four', 850.0)"
D_LOTS = "Lots : LIST OF TUPLE(STRING, REAL) := [('B12', 2.5), ('A07', 4.0), ('C03', 1.2)]"
NOC = "/* pas de liste en C : un tableau et un compteur */"
NOMAP = "/* pas de MAP en C */"
CMP = "#Egal, #Different, #Inferieur, #InferieurOuEgal, #Superieur, #SuperieurOuEgal"
ARR_NOTE = "Un ARRAY se lit à ses indices : de Tab.Low à Tab.High."
SIZE_NOTE = "Elle change la taille : un ARRAY (taille fixe) est refusé — une LIST ou un VECTOR est attendu."
MEMBER_NOTE = "Une liste de structures : MEMBRE dit lequel comparer ('Poids') ; une liste de TUPLE : 'Item1', 'Item2'…"
SCRIPT_NOTE = "Elle change son premier argument : dans un script ou une fonction (une expression fx n'en change qu'une copie)."

# ---- lire et ecrire
fn(name="LIST_SET", cat="liste", params=[("L", "LIST, VECTOR, ARRAY", "la liste", False), ("I", "DINT", "l'indice", False), ("X", "T", "la valeur", False)],
   ret="BOOL", short="Écrit x à l'indice i ; FALSE, sans erreur, hors de la liste (rien n'est écrit).",
   notes=["Le pendant de LIST_GET ; L[i] := x, lui, s'arrête hors de la liste.", ARR_NOTE, SCRIPT_NOTE],
   st="IF NOT LIST_SET(Mesures, Rang, Niveau) THEN\n    IHM_JOURNAL('Rang hors de la liste');\nEND_IF",
   c=NOC + "\nif (i < n) t[i] = x;", cpp="if (i < mesures.size()) mesures[i] = x;",
   essai=("BOOL", "LIST_SET(Mesures, 1, 9.5) AND Mesures[1] = 9.5", "TRUE", decl(D_MES)), ou="S F")
fn(name="MAP_SET", cat="map", params=[("M", "MAP", "la MAP", False), ("CLE", "comme les clés", "la clé", False), ("V", "comme les valeurs", "la valeur", False)],
   ret="BOOL", short="Écrit la valeur de la clé, en la créant si elle manque (comme m[clé] := v) ; TRUE si la clé est nouvelle.",
   notes=["Une valeur d'un autre type est refusée : MAP_SET(Stock, 'vis', 'abc') s'arrête sur « 'abc' n'est pas un REAL ».", SCRIPT_NOTE],
   st="IF MAP_SET(Stock, Piece, Qte) THEN\n    IHM_JOURNAL(CONCAT('Nouvelle pièce : ', Piece));\nEND_IF",
   c=NOMAP, cpp="bool nouvelle = stock.insert_or_assign(piece, qte).second;   // C++17",
   essai=("BOOL", "MAP_SET(Stock, 'rondelle', 12.0)", "TRUE", decl(D_STOCK)), ou="S F")
fn(name="TUPLE_GET", cat="liste", params=[("T", "TUPLE", "le tuple", False), ("N", "une constante, 1 à la taille", "le numéro du champ", False)],
   ret="le type du champ N", short="Le champ n du TUPLE (1 : le premier), comme t.Item1.",
   notes=["N est une constante : le type du résultat se connaît à Compiler, qui dit un numéro hors des champs."],
   st="Valeur := TUPLE_GET(Point, 2);   (* = Point.Item2 *)", c="valeur = point.valeur;   /* un struct */",
   cpp="valeur = std::get<1>(point);   // std::get compte à partir de 0",
   essai=("REAL", "TUPLE_GET(Point, 2)", "850", decl(D_POINT)), ou="S F")
fn(name="TUPLE_SET", cat="liste", params=[("T", "TUPLE", "le tuple", False), ("N", "une constante", "le numéro du champ", False), ("X", "le type du champ", "la valeur", False)],
   ret="BOOL", short="Écrit le champ n (1 : le premier), comme t.Item1 := x.", notes=["N est une constante.", SCRIPT_NOTE],
   st="TUPLE_SET(Point, 1, 'etuve');   (* = Point.Item1 := 'etuve' *)", c="strcpy(point.nom, \"etuve\");", cpp="std::get<0>(point) = \"etuve\";",
   essai=("STRING", "TUPLE_GET(Point, 1)", "etuve", decl(D_POINT) + "\nTUPLE_SET(Point, 1, 'etuve');"), ou="S F")
fn(name="MAP_FIRST_KEY", cat="map", params=[("M", "MAP", "la MAP", False), ("DEFAUT", "comme les clés", "pour une MAP vide", True)],
   ret="comme les clés", short="La plus petite clé (les clés sont rangées) ; une MAP vide : DEFAUT, ou une erreur sans lui.",
   st="Premiere := MAP_FIRST_KEY(Stock);", c=NOMAP, cpp="premiere = stock.begin()->first;   // la MAP n'est pas vide",
   essai=("STRING", "MAP_FIRST_KEY(Stock)", "ecrou", decl(D_STOCK)), ou="S F")
fn(name="MAP_LAST_KEY", cat="map", params=[("M", "MAP", "la MAP", False), ("DEFAUT", "comme les clés", "pour une MAP vide", True)],
   ret="comme les clés", short="La plus grande clé ; une MAP vide : DEFAUT, ou une erreur sans lui.",
   st="Derniere := MAP_LAST_KEY(Paliers);", c=NOMAP, cpp="derniere = paliers.rbegin()->first;",
   essai=("INT", "MAP_LAST_KEY(Paliers)", "500", decl(D_PAL)), ou="S F")
fn(name="TUPLE_SIZE", cat="liste", params=[("T", "TUPLE", "le tuple", False)], ret="DINT",
   short="Le nombre de champs du TUPLE (une constante : 2 pour un TUPLE(STRING, REAL)).",
   st="n := TUPLE_SIZE(Point);", c="n = 2;   /* un struct : le nombre de ses membres */", cpp="constexpr auto n = std::tuple_size_v<decltype(point)>;",
   essai=("DINT", "TUPLE_SIZE(Point)", "2", decl(D_POINT)), ou="S F")
# ---- ajouter et retirer
fn(name="MAP_INSERT", cat="map", params=[("M", "MAP", "la MAP", False), ("CLE", "comme les clés", "la clé", False), ("V", "comme les valeurs", "sa valeur, si elle est nouvelle", False)],
   ret="BOOL", short="Ajoute la clé seulement si elle manque : n'écrase jamais. TRUE si elle a été ajoutée.", notes=[SCRIPT_NOTE],
   st="MAP_INSERT(Stock, 'rondelle', 0.0);   (* un départ, sans écraser *)", c=NOMAP,
   cpp="bool ajoutee = stock.try_emplace(\"rondelle\", 0.0f).second;   // C++17",
   essai=("REAL", "Stock['vis']", "120", decl(D_STOCK) + "\nMAP_INSERT(Stock, 'vis', 0.0);"), ou="S F")
fn(name="LIST_INSERT_SORTED", cat="liste", params=[("L", "LIST, VECTOR", "une liste triée", False), ("X", "T", "la valeur", False), ("DECROISSANT", "BOOL", "TRUE pour une liste du plus grand au plus petit", True)],
   ret="DINT", short="Insère la valeur à sa place dans une liste triée, après celles qui lui sont égales ; rend l'indice où elle est rangée.",
   notes=["La liste reste triée sans LIST_SORT.", SIZE_NOTE],
   st="LIST_INSERT_SORTED(Echeances, Prochaine);", c="/* pas de liste en C : chercher la place, memmove, écrire */",
   cpp="auto it = std::upper_bound(l.begin(), l.end(), x);\nl.insert(it, x);",
   essai=("DINT", "LIST_INSERT_SORTED(Triee, 4.5)", "3", decl(D_TRI)), ou="S F")
fn(name="LIST_POP_FIRST", cat="liste", params=[("L", "LIST, VECTOR", "la liste", False)], ret="T",
   short="Retire le premier élément et le rend : une file d'attente (le premier entré sort le premier). Une liste vide : une erreur.",
   notes=["VECTOR_POP retire le dernier (une pile).", SIZE_NOTE],
   st="WHILE File.Count > 0 DO\n    Commande := LIST_POP_FIRST(File);\n    (* envoyer la commande *)\nEND_WHILE",
   c="/* pas de liste en C : t[0], puis memmove */", cpp="auto x = file.front();\nfile.erase(file.begin());   // std::deque : file.pop_front()",
   essai=("STRING", "LIST_POP_FIRST(Pile) + JOIN(Pile, '')", "abc", decl(D_PILE)), ou="S F")
fn(name="LIST_REMOVE_ALL", cat="liste", params=[("L", "LIST, VECTOR", "la liste", False), ("X", "T", "la valeur à retirer", False)], ret="DINT",
   short="Retire toutes les valeurs égales ; rend le nombre retiré (0 si aucune).", notes=[SIZE_NOTE],
   st="n := LIST_REMOVE_ALL(Actifs, 'P2');", c="/* pas de liste en C : une boucle qui recopie les autres */", cpp="auto n = std::erase(l, x);   // C++20",
   essai=("DINT", "LIST_REMOVE_ALL(Mesures, 8.0)", "2", decl(D_MES)), ou="S F")
fn(name="LIST_REMOVE_IF", cat="liste", params=[("L", "LIST, VECTOR", "la liste", False), ("COMPARAISON", "COMPARAISON", CMP, False),
                                              ("VALEUR", "T, ou le type du membre", "à quoi comparer", False), ("MEMBRE", "STRING", "'Poids' pour une liste de structures", True)],
   ret="DINT", short="Retire les éléments qui vérifient la comparaison avec VALEUR ; rend le nombre retiré.", notes=[MEMBER_NOTE, SIZE_NOTE],
   st="n := LIST_REMOVE_IF(Mesures, COMPARAISON#Inferieur, 0.0);   (* les mesures négatives *)",
   c="/* pas de liste en C : une boucle qui recopie les autres */", cpp="auto n = std::erase_if(mesures, [](REAL x) { return x < 0.0f; });   // C++20",
   essai=("DINT", "LIST_REMOVE_IF(Mesures, COMPARAISON#Inferieur, 9.0)", "3", decl(D_MES)), ou="S F")
fn(name="MAP_REMOVE_IF", cat="map", params=[("M", "MAP", "la MAP", False), ("COMPARAISON", "COMPARAISON", CMP, False), ("VALEUR", "comme les valeurs", "à quoi comparer", False)],
   ret="DINT", short="Retire les clés dont la valeur vérifie la comparaison ; rend le nombre retiré.", notes=[SCRIPT_NOTE],
   st="MAP_REMOVE_IF(Stock, COMPARAISON#InferieurOuEgal, 0.0);   (* les pièces épuisées *)", c=NOMAP,
   cpp="std::erase_if(stock, [](const auto& kv) { return kv.second <= 0.0f; });   // C++20",
   essai=("DINT", "MAP_REMOVE_IF(Stock, COMPARAISON#Inferieur, 100.0)", "2", decl(D_STOCK)), ou="S F")
fn(name="LIST_DISTINCT", cat="liste", params=[("L", "LIST, VECTOR", "la liste", False)], ret="DINT",
   short="Retire les doublons : garde la première de chaque valeur, dans l'ordre ; rend le nombre retiré.", notes=[SIZE_NOTE],
   st="LIST_DISTINCT(Codes_Vus);", c="/* pas de liste en C : deux boucles */",
   cpp="std::set<REAL> vus;\nstd::erase_if(l, [&](REAL x) { return !vus.insert(x).second; });",
   essai=("DINT", "LIST_DISTINCT(Mesures)", "1", decl(D_MES)), ou="S F")
fn(name="MAP_INCREMENT", cat="map", params=[("M", "MAP de nombres", "la MAP", False), ("CLE", "comme les clés", "la clé", False), ("X", "comme les valeurs", "ce qu'on ajoute : 1 sans lui", True)],
   ret="comme les valeurs", short="Ajoute x (1 sans lui) à la valeur de la clé, en partant de 0 si elle manque ; rend la nouvelle valeur.",
   notes=["Le compteur par clé, sans tester MAP_HAS.", SCRIPT_NOTE],
   st="MAP_INCREMENT(Pieces_Par_Poste, Poste);\n(* au lieu de : IF MAP_HAS(...) THEN m[k] := m[k] + 1 ELSE m[k] := 1 END_IF *)", c=NOMAP,
   cpp="auto v = (pieces[poste] += 1);   // [] crée la clé à 0",
   essai=("REAL", "MAP_INCREMENT(Stock, 'vis', 10.0)", "130", decl(D_STOCK)), ou="S F")
fn(name="LIST_APPEND", cat="liste", params=[("L", "LIST, VECTOR", "la liste", False), ("AUTRE", "LIST, VECTOR, ARRAY", "les éléments à ajouter", False)],
   ret="DINT", short="Ajoute à la fin tous les éléments d'une autre liste (ou d'un ARRAY) ; rend le nombre d'éléments.", notes=[SIZE_NOTE],
   st="LIST_APPEND(Historique, Lot_Courant);", c="/* pas de liste en C : memcpy à la fin */",
   cpp="l.insert(l.end(), autre.begin(), autre.end());   // C++23 : l.append_range(autre)",
   essai=("DINT", "LIST_APPEND(Mesures, Autres)", "7", decl(D_MES, D_AUT)), ou="S F")
fn(name="MAP_MERGE", cat="map", params=[("M", "MAP", "la MAP", False), ("AUTRE", "MAP du même type", "les clés à ajouter", False), ("REMPLACER", "BOOL", "FALSE sans lui", True)],
   ret="DINT", short="Ajoute les clés d'une autre MAP ; une clé déjà là garde sa valeur, sauf avec REMPLACER à TRUE. Rend le nombre de clés ajoutées.",
   notes=["AUTRE ne change pas.", SCRIPT_NOTE],
   st="MAP_MERGE(Stock, Livraison, TRUE);", c=NOMAP,
   cpp="stock.insert(autre.begin(), autre.end());          // sans remplacer\nfor (const auto& [k, v] : autre) stock.insert_or_assign(k, v);   // en remplaçant",
   essai=("REAL", "Stock['vis']", "60", decl(D_STOCK, D_LIVR) + "\nMAP_MERGE(Stock, Livraison, TRUE);"), ou="S F")
fn(name="LIST_FILL", cat="liste", params=[("L", "ARRAY, LIST, VECTOR", "le tableau ou la liste", False), ("X", "T", "la valeur", False)], ret="DINT",
   short="Met x dans toutes les cases, sans changer la taille ; rend le nombre de cases.",
   notes=["Remet un tableau à zéro d'un coup ; un tableau IHM est écrit en place.", SCRIPT_NOTE],
   st="LIST_FILL(Niveaux, 0.0);", c="for (i = 0; i < n; ++i) t[i] = 0.0f;   /* memset pour 0 */",
   cpp="std::ranges::fill(niveaux, 0.0f);   // tab.fill(0) pour un std::array",
   essai=("STRING", "JOIN(Cases, ',')", "0,0,0,0,0", decl(D_CASES) + "\nLIST_FILL(Cases, 0);"), ou="S F")
# ---- chercher
fn(name="LIST_LAST_INDEX_OF", cat="liste", params=[("L", "LIST, VECTOR, ARRAY", "la liste", False), ("X", "T", "la valeur cherchée", False)], ret="DINT",
   short="L'indice de la dernière valeur égale ; −1 si elle n'y est pas.", notes=[ARR_NOTE],
   st="i := LIST_LAST_INDEX_OF(Journal, 'Arret');", c="/* une boucle à l'envers */",
   cpp="auto it = std::find(l.rbegin(), l.rend(), x);\ni = it == l.rend() ? -1 : l.rend() - it - 1;",
   essai=("DINT", "LIST_LAST_INDEX_OF(Mesures, 8.0)", "3", decl(D_MES)), ou="S F X T")
fn(name="MAP_KEY_OF", cat="map", params=[("M", "MAP", "la MAP", False), ("V", "comme les valeurs", "la valeur cherchée", False), ("DEFAUT", "comme les clés", "si aucune", False)],
   ret="comme les clés", short="La première clé (dans leur ordre) dont la valeur est égale à v ; DEFAUT si aucune. La recherche à l'envers d'une MAP.",
   st="Piece := MAP_KEY_OF(Stock, 0.0, '');   (* une pièce épuisée *)", c=NOMAP,
   cpp="auto it = std::ranges::find_if(stock, [&](const auto& kv) { return kv.second == v; });\ncle = it != stock.end() ? it->first : \"\";",
   essai=("STRING", "MAP_KEY_OF(Stock, 45.0, '')", "goujon", decl(D_STOCK)), ou="S F X T")
fn(name="LIST_COUNT_OF", cat="liste", params=[("L", "LIST, VECTOR, ARRAY", "la liste", False), ("X", "T", "la valeur comptée", False)], ret="DINT",
   short="Le nombre de fois où la valeur est dans la liste.", st="Arrets := LIST_COUNT_OF(Journal, 'Arret');", c="/* une boucle et un compteur */",
   cpp="auto n = std::ranges::count(l, x);", essai=("DINT", "LIST_COUNT_OF(Cases, 9)", "2", decl(D_CASES)), ou="S F X T")
fn(name="LIST_FIND_IF", cat="liste", params=[("L", "LIST, VECTOR, ARRAY", "la liste", False), ("COMPARAISON", "COMPARAISON", "#Egal, #Superieur…", False),
                                            ("VALEUR", "T, ou le type du membre", "à quoi comparer", False), ("MEMBRE", "STRING", "le membre comparé", True)],
   ret="DINT", short="L'indice du premier élément qui vérifie la comparaison ; −1 si aucun.", notes=[MEMBER_NOTE, ARR_NOTE],
   st="i := LIST_FIND_IF(Recettes, COMPARAISON#Egal, 'Pain', 'Nom');\nIF i >= 0 THEN Recette := Recettes[i]; END_IF", c="/* une boucle */",
   cpp="auto it = std::ranges::find_if(recettes, [](const T_Recette& r) { return r.Nom == \"Pain\"; });\ni = it == recettes.end() ? -1 : it - recettes.begin();",
   essai=("DINT", "LIST_FIND_IF(Lots, COMPARAISON#Egal, 'A07', 'Item1')", "1", decl(D_LOTS)), ou="S F X T")
fn(name="LIST_COUNT_IF", cat="liste", params=[("L", "LIST, VECTOR, ARRAY", "la liste", False), ("COMPARAISON", "COMPARAISON", "#Superieur…", False),
                                             ("VALEUR", "T, ou le type du membre", "à quoi comparer", False), ("MEMBRE", "STRING", "le membre comparé", True)],
   ret="DINT", short="Le nombre d'éléments qui vérifient la comparaison.", notes=[MEMBER_NOTE],
   st="Hors_Tolerance := LIST_COUNT_IF(Mesures, COMPARAISON#Superieur, Seuil);", c="/* une boucle et un compteur */",
   cpp="auto n = std::ranges::count_if(mesures, [&](REAL x) { return x > seuil; });",
   essai=("DINT", "LIST_COUNT_IF(Mesures, COMPARAISON#Superieur, 10.0)", "2", decl(D_MES)), ou="S F X T")
for nm, fr, ex, cc, es in [
        ("LIST_ALL", "Vrai si tous les éléments vérifient la comparaison (vrai pour une liste vide).", "Tout_Ok := LIST_ALL(Pressions, COMPARAISON#Inferieur, P_Max);",
         "bool tout_ok = std::ranges::all_of(pressions, [&](REAL p) { return p < p_max; });", ("BOOL", "LIST_ALL(Mesures, COMPARAISON#Superieur, 3.0)", "TRUE")),
        ("LIST_ANY", "Vrai si au moins un élément vérifie la comparaison.", "Alerte := LIST_ANY(Temperatures, COMPARAISON#Superieur, 90.0);",
         "bool alerte = std::ranges::any_of(temperatures, [](REAL t) { return t > 90.0f; });", ("BOOL", "LIST_ANY(Mesures, COMPARAISON#Superieur, 15.0)", "TRUE")),
        ("LIST_NONE", "Vrai si aucun élément ne vérifie la comparaison.", "Aucun_Defaut := LIST_NONE(Etats, COMPARAISON#Egal, 2);",
         "bool aucun = std::ranges::none_of(etats, [](Etat e) { return e == Etat::Defaut; });", ("BOOL", "LIST_NONE(Mesures, COMPARAISON#Inferieur, 0.0)", "TRUE"))]:
    fn(name=nm, cat="liste", params=[("L", "LIST, VECTOR, ARRAY", "la liste", False), ("COMPARAISON", "COMPARAISON", CMP, False),
                                    ("VALEUR", "T, ou le type du membre", "à quoi comparer", False), ("MEMBRE", "STRING", "le membre comparé", True)],
       ret="BOOL", short=fr, notes=[MEMBER_NOTE], st=ex, c="/* une boucle */", cpp=cc, essai=es + (decl(D_MES),), ou="S F X T")
fn(name="LIST_MIN_INDEX", cat="liste", params=[("L", "LIST, VECTOR, ARRAY", "la liste", False), ("MEMBRE", "STRING", "pour une liste de structures", True)], ret="DINT",
   short="L'indice de la plus petite valeur (la première, s'il y en a plusieurs) ; −1 pour une liste vide.",
   notes=["Où est le minimum, pas seulement sa valeur.", ARR_NOTE],
   st="Poste_Lent := Postes[LIST_MIN_INDEX(Cadences)];", c="/* une boucle */", cpp="auto i = std::ranges::min_element(cadences) - cadences.begin();",
   essai=("DINT", "LIST_MIN_INDEX(Mesures)", "4", decl(D_MES)), ou="S F X T")
fn(name="LIST_MAX_INDEX", cat="liste", params=[("L", "LIST, VECTOR, ARRAY", "la liste", False), ("MEMBRE", "STRING", "pour une liste de structures", True)], ret="DINT",
   short="L'indice de la plus grande valeur (la première, s'il y en a plusieurs) ; −1 pour une liste vide.", notes=[ARR_NOTE],
   st="Plus_Lourd := Lots[LIST_MAX_INDEX(Lots, 'Poids')].Nom;", c="/* une boucle */",
   cpp="auto i = std::ranges::max_element(lots, {}, &T_Lot::Poids) - lots.begin();",
   essai=("DINT", "LIST_MAX_INDEX(Lots, 'Item2')", "1", decl(D_LOTS)), ou="S F X T")
fn(name="MAP_FLOOR_KEY", cat="map", params=[("M", "MAP", "les paliers", False), ("K", "comme les clés", "la valeur cherchée", False), ("DEFAUT", "comme les clés", "s'il n'y a pas de palier dessous", False)],
   ret="comme les clés", short="La plus grande clé inférieure ou égale à k ; DEFAUT s'il n'y en a pas.",
   notes=["Une table de paliers : 230 tombe sur le palier 100."],
   st="Coef := Paliers[MAP_FLOOR_KEY(Paliers, Debit, 0)];", c="/* pas de MAP en C : une boucle sur un tableau trié */",
   cpp="auto it = paliers.upper_bound(debit);\npalier = it == paliers.begin() ? 0 : std::prev(it)->first;",
   essai=("INT", "MAP_FLOOR_KEY(Paliers, 230, -1)", "100", decl(D_PAL)), ou="S F X T")
fn(name="MAP_CEIL_KEY", cat="map", params=[("M", "MAP", "la MAP", False), ("K", "comme les clés", "la valeur cherchée", False), ("DEFAUT", "comme les clés", "s'il n'y en a pas au-dessus", False)],
   ret="comme les clés", short="La plus petite clé supérieure ou égale à k ; DEFAUT s'il n'y en a pas.",
   st="Prochain := MAP_CEIL_KEY(Paliers, Debit, -1);", c=NOMAP, cpp="auto it = paliers.lower_bound(debit);\nprochain = it == paliers.end() ? -1 : it->first;",
   essai=("INT", "MAP_CEIL_KEY(Paliers, 230, -1)", "500", decl(D_PAL)), ou="S F X T")
# ---- ranger
fn(name="LIST_SORT_BY", cat="liste", params=[("L", "LIST, VECTOR, ARRAY de structures", "la liste", False), ("MEMBRE", "STRING", "'Poids', 'Nom', 'Item2'…", False),
                                            ("DECROISSANT", "BOOL", "TRUE : du plus grand au plus petit", True)],
   ret="BOOL", short="Trie une liste de structures (ou de TUPLE : 'Item2') par un de ses membres.",
   notes=["À valeur égale, l'ordre ne change pas (un tri stable : un tableau de l'IHM ne saute pas).", "Un membre qui n'existe pas : Compiler le dit (« veux-tu dire Poids ? »).", SCRIPT_NOTE],
   st="LIST_SORT_BY(Lots, 'Poids', TRUE);   (* le plus lourd d'abord *)", c="qsort(lots, n, sizeof lots[0], par_poids);",
   cpp="std::ranges::stable_sort(lots, std::greater{}, &T_Lot::Poids);   // C++20",
   essai=("STRING", "Lots[0].Item1", "C03", decl(D_LOTS) + "\nLIST_SORT_BY(Lots, 'Item2');"), ou="S F")
fn(name="LIST_IS_SORTED", cat="liste", params=[("L", "LIST, VECTOR, ARRAY", "la liste", False), ("DECROISSANT", "BOOL", "TRUE : du plus grand au plus petit", True)], ret="BOOL",
   short="Vrai si la liste est triée (dans le sens demandé).", st="ASSERT(LIST_IS_SORTED(Seuils), 'les seuils doivent croître');", c="/* une boucle */",
   cpp="bool trie = std::ranges::is_sorted(seuils);", essai=("BOOL", "LIST_IS_SORTED(Triee)", "TRUE", decl(D_TRI)), ou="S F X T")
fn(name="LIST_SWAP", cat="liste", params=[("L", "LIST, VECTOR, ARRAY", "la liste", False), ("I", "DINT", "un indice", False), ("J", "DINT", "l'autre", False)], ret="BOOL",
   short="Échange deux éléments ; FALSE si un indice est hors de la liste.", notes=[ARR_NOTE, SCRIPT_NOTE],
   st="LIST_SWAP(Etapes, Rang, Rang - 1);   (* le bouton ↑ d'un tableau *)", c="tmp = t[i]; t[i] = t[j]; t[j] = tmp;", cpp="std::swap(etapes[i], etapes[j]);",
   essai=("STRING", "JOIN(Pile, '')", "cba", decl(D_PILE) + "\nLIST_SWAP(Pile, 0, 2);"), ou="S F")
fn(name="LIST_MOVE", cat="liste", params=[("L", "LIST, VECTOR, ARRAY", "la liste", False), ("DE", "DINT", "l'indice de l'élément", False), ("VERS", "DINT", "son nouvel indice", False)],
   ret="BOOL", short="Déplace un élément d'un indice à un autre ; ceux d'entre les deux glissent d'une place. FALSE hors de la liste.",
   notes=["Réordonner les étapes d'une recette.", SCRIPT_NOTE],
   st="LIST_MOVE(Etapes, 3, 0);   (* l'étape 3 passe en premier *)", c="/* memmove */",
   cpp="if (de < vers) std::rotate(l.begin() + de, l.begin() + de + 1, l.begin() + vers + 1);\nelse std::rotate(l.begin() + vers, l.begin() + de, l.begin() + de + 1);",
   essai=("STRING", "JOIN(Mesures, ';')", "3.1;12.5;8;15.2;8", decl(D_MES) + "\nLIST_MOVE(Mesures, 4, 0);"), ou="S F")
fn(name="LIST_ROTATE", cat="liste", params=[("L", "LIST, VECTOR, ARRAY", "la liste", False), ("N", "DINT", "le nombre de places", False)], ret="BOOL",
   short="Fait tourner les éléments de n places vers le début (n négatif : vers la fin) ; ce qui sort revient de l'autre côté.", notes=[SCRIPT_NOTE],
   st="LIST_ROTATE(Equipes, 1);   (* l'équipe suivante prend le premier poste *)", c="/* trois inversions */",
   cpp="std::ranges::rotate(equipes, equipes.begin() + 1);",
   essai=("STRING", "JOIN(Cases, ',')", "9,1,9,6,4", decl(D_CASES) + "\nLIST_ROTATE(Cases, 1);"), ou="S F")
# ---- extraire et convertir
fn(name="LIST_SLICE", cat="liste", params=[("L", "LIST, VECTOR, ARRAY", "la liste", False), ("DEBUT", "DINT", "l'indice du premier", False), ("NOMBRE", "DINT", "combien", False)],
   ret="LIST OF T", short="Une copie d'une partie : NOMBRE éléments à partir de DEBUT (ce qui dépasse la fin est ignoré).", notes=[ARR_NOTE],
   st="Dernieres := LIST_SLICE(Mesures, Mesures.Count - 10, 10);   (* les 10 dernières *)", c="memcpy(part, t + debut, nombre * sizeof t[0]);",
   cpp="std::vector<REAL> part(l.begin() + debut, l.begin() + debut + nombre);\n// sans copie : std::span(l).subspan(debut, nombre)   // C++20",
   essai=("STRING", "JOIN(LIST_SLICE(Mesures, 1, 3), ';')", "8;15.2;8", decl(D_MES)), ou="S F X T")
fn(name="LIST_CONCAT", cat="liste", params=[("A", "LIST, VECTOR, ARRAY", "les premiers", False), ("B", "LIST, VECTOR, ARRAY du même type", "les suivants", False)],
   ret="LIST OF T", short="Une nouvelle liste : les éléments de A, puis ceux de B. A et B ne changent pas.",
   st="Tous := LIST_CONCAT(Ligne_1, Ligne_2);", c="/* deux memcpy */", cpp="auto tous = a;\ntous.insert(tous.end(), b.begin(), b.end());",
   essai=("DINT", "LIST_COUNT(LIST_CONCAT(Mesures, Autres))", "7", decl(D_MES, D_AUT)), ou="S F X T")
fn(name="LIST_COPY", cat="liste", params=[("SOURCE", "ARRAY, LIST, VECTOR", "les valeurs", False), ("DESTINATION", "ARRAY, LIST, VECTOR du même type d'éléments", "écrite en place", False)],
   ret="DINT", short="Copie SOURCE dans DESTINATION, case par case depuis le début ; DESTINATION garde sa taille. Rend le nombre copié.",
   notes=["Ce qui dépasse est ignoré, ce qui manque garde sa valeur : une LIST vers un ARRAY d'équipement.",
          "Une valeur qui ne va pas dans DESTINATION est refusée (12.5 dans un INT : « n'est pas un entier »)."],
   st="LIST_COPY(Consignes, Automate.Consignes);   (* une LIST vers un ARRAY[0..9] *)", c="memcpy(dst, src, min(n_src, n_dst) * sizeof src[0]);",
   cpp="auto n = std::min(src.size(), dst.size());\nstd::copy_n(src.begin(), n, dst.begin());",
   essai=("STRING", "JOIN(Mesures, ';')", "1;2;15.2;8;3.1", decl(D_MES, D_AUT) + "\nLIST_COPY(Autres, Mesures);"), ou="S F")
fn(name="TO_LIST", cat="liste", params=[("X", "ARRAY, VECTOR, LIST", "les éléments", False)], ret="LIST OF T",
   short="Les éléments d'un ARRAY (ligne par ligne s'il a deux dimensions), d'un VECTOR, d'une liste : une nouvelle LIST.",
   notes=["Une LIST qu'on peut agrandir, trier, filtrer."],
   st="Valeurs := TO_LIST(Automate.Mesures);   (* un ARRAY[0..9] OF REAL *)", c="/* memcpy */",
   cpp="std::vector<INT> l(std::begin(tab), std::end(tab));   // C++23 : std::ranges::to<std::vector>(tab)",
   essai=("DINT", "LIST_COUNT(TO_LIST(Cases))", "5", decl(D_CASES)), ou="S F X T")
fn(name="MAP_VALUES", cat="map", params=[("M", "MAP", "la MAP", False)], ret="LIST OF valeur",
   short="Les valeurs, dans l'ordre des clés : une LIST (pour LIST_SUM, LIST_MAX…).",
   st="Total := LIST_SUM(MAP_VALUES(Stock));", c=NOMAP, cpp="auto valeurs = std::ranges::to<std::vector>(stock | std::views::values);   // C++23",
   essai=("REAL", "LIST_SUM(MAP_VALUES(Stock))", "245", decl(D_STOCK)), ou="S F X T")
fn(name="MAP_ITEMS", cat="map", params=[("M", "MAP", "la MAP", False)], ret="LIST OF TUPLE(clé, valeur)",
   short="Les paires (clé, valeur), dans l'ordre des clés : une LIST de TUPLE.",
   notes=["Triée par valeur avec LIST_SORT_BY(…, 'Item2'), elle donne un classement."],
   st="Classement := MAP_ITEMS(Stock);\nLIST_SORT_BY(Classement, 'Item2', TRUE);   (* la plus grande quantité d'abord *)", c=NOMAP,
   cpp="std::vector<std::pair<std::string, REAL>> items(stock.begin(), stock.end());",
   essai=("STRING", "Lots[0].Item1", "vis", decl(D_STOCK, D_LOTS) + "\nLots := MAP_ITEMS(Stock);\nLIST_SORT_BY(Lots, 'Item2', TRUE);"), ou="S F")
fn(name="TUPLE_TO_STRING", cat="liste", params=[("T", "TUPLE", "le tuple", False)], ret="STRING",
   short="Le TUPLE écrit comme un littéral : ('four', 850.0). Pour un journal, une étiquette.",
   st="IHM_JOURNAL(TUPLE_TO_STRING(Point));", c="snprintf(texte, sizeof texte, \"('%s', %.1f)\", point.nom, point.valeur);",
   cpp="auto texte = std::format(\"{}\", point);   // C++23 : (\"four\", 850)",
   essai=("STRING", "TUPLE_TO_STRING(Point)", "('four', 850.0)", decl(D_POINT)), ou="S F X T")
fn(name="TUPLE_UNPACK", cat="liste", params=[("T", "TUPLE", "le tuple", False), ("A, B…", "des variables", "une par champ, chacune du bon type", False)],
   ret="BOOL", short="Écrit chaque champ dans une variable, dans l'ordre : autant de variables que de champs.",
   notes=["Une variable d'un autre type que son champ est refusée (« le champ 1 est un STRING, Valeur un REAL »)."],
   st="TUPLE_UNPACK(Point, Nom, Valeur);   (* Nom := Point.Item1 ; Valeur := Point.Item2 *)", c="strcpy(nom, point.nom);\nvaleur = point.valeur;",
   cpp="std::tie(nom, valeur) = point;\n// C++17 : auto [nom, valeur] = point;",
   essai=("STRING", "Nom", "four", decl(D_POINT, "Nom : STRING", "Valeur : REAL") + "\nTUPLE_UNPACK(Point, Nom, Valeur);"), ou="S F")

# ------------------------------------- 1.12.7 (choix 5 de la maquette v3) : SET et RING --
#  SET OF T (std::set) : chaque valeur une fois, rangee - SET_ADD rend TRUE si elle est nouvelle ;
#  RING[N] OF T (un tampon circulaire) : les N dernieres valeurs, la plus ancienne part quand il
#  est plein. Les natives LIST_ qui lisent (LIST_AVG, LIST_MIN, JOIN, LIST_GET...) les lisent
#  aussi ; FOR EACH parcourt un SET dans l'ordre, un RING de la plus ancienne a la plus recente.
D_POSTES = "Postes_Actifs : SET OF STRING := ['P3', 'P1', 'P3']"
D_AUTRE = "Autre_Choix : SET OF STRING := ['P2', 'P3', 'P4']"
D_RING = "Dernieres : RING[5] OF REAL := [12.0, 12.4, 13.1, 12.8, 13.5]"
NOSET = "/* pas d'ensemble en C : un tableau trié, chercher avant d'insérer */"
SET_NOTE = "Un SET se range tout seul : le littéral ['P3', 'P1', 'P3'] devient ['P1', 'P3'] ; S[0] se lit (la plus petite), ne s'écrit pas."
SETOP_NOTE = "A et B : des SET, ou une LIST, un RING, un ARRAY (rangés, sans doublon) ; les valeurs de B prennent le type de celles de A."
fn(name="SET_ADD", cat="ensembles", params=[("S", "SET OF T", "l'ensemble", False), ("X", "T", "la valeur", False)], ret="BOOL",
   short="Ajoute la valeur à sa place (le SET reste rangé) : TRUE si elle est nouvelle ; déjà là, rien ne change (FALSE).",
   notes=["Plus besoin de LIST_CONTAINS avant chaque LIST_ADD : le SET garantit qu'une valeur n'y est qu'une fois.",
          "LIST_ADD sur un SET fait de même (il rend le nombre de valeurs).",
          "Une valeur d'un autre type est refusée : SET_ADD(Postes, 5) s'arrête sur « 5 n'est pas une STRING ».", SET_NOTE, SCRIPT_NOTE],
   st="IF SET_ADD(Postes_Actifs, Poste) THEN\n    IHM_JOURNAL(CONCAT('Nouveau poste : ', Poste));\nEND_IF",
   c=NOSET, cpp="if (postes_actifs.insert(poste).second)   // std::set : second vaut true si elle est nouvelle\n    journal(\"Nouveau poste : \" + poste);",
   essai=("STRING", "TO_STRING(SET_ADD(Postes_Actifs, 'P2')) + ' ' + JOIN(Postes_Actifs, ',')", "TRUE P1,P2,P3", decl(D_POSTES)), ou="S F")
fn(name="SET_REMOVE", cat="ensembles", params=[("S", "SET OF T", "l'ensemble", False), ("X", "T", "la valeur", False)], ret="BOOL",
   short="Retire la valeur : TRUE si elle y était.", notes=[SCRIPT_NOTE],
   st="SET_REMOVE(Postes_Actifs, Poste);", c=NOSET, cpp="bool retiree = postes_actifs.erase(poste) > 0;",
   essai=("STRING", "TO_STRING(SET_REMOVE(Postes_Actifs, 'P1')) + ' ' + JOIN(Postes_Actifs, ',')", "TRUE P3", decl(D_POSTES)), ou="S F")
fn(name="SET_HAS", cat="ensembles", params=[("S", "SET OF T", "l'ensemble", False), ("X", "T", "la valeur", False)], ret="BOOL",
   short="Vrai si la valeur est dans le SET.", notes=["LIST_CONTAINS lit aussi un SET."],
   st="IF SET_HAS(Postes_Actifs, 'P2') THEN ... END_IF", c=NOSET, cpp="bool la = postes_actifs.contains(\"P2\");   // C++20",
   essai=("BOOL", "SET_HAS(Postes_Actifs, 'P3') AND NOT SET_HAS(Postes_Actifs, 'P2')", "TRUE", decl(D_POSTES)), ou="S F")
fn(name="SET_UNION", cat="ensembles", params=[("A", "SET OF T", "le premier", False), ("B", "SET OF T", "le second", False)], ret="SET OF T",
   short="Un SET neuf : les valeurs de A et celles de B, chacune une fois, rangées.", notes=[SETOP_NOTE],
   st="Tous := SET_UNION(Postes_Actifs, Autre_Choix);", c=NOSET,
   cpp="std::set<std::string> tous;\nstd::set_union(a.begin(), a.end(), b.begin(), b.end(), std::inserter(tous, tous.begin()));",
   essai=("STRING", "JOIN(SET_UNION(Postes_Actifs, Autre_Choix), ',')", "P1,P2,P3,P4", decl(D_POSTES, D_AUTRE)), ou="S F")
fn(name="SET_INTERSECTION", cat="ensembles", params=[("A", "SET OF T", "le premier", False), ("B", "SET OF T", "le second", False)], ret="SET OF T",
   short="Un SET neuf : les valeurs qui sont dans A et dans B (ce que deux choix ont en commun).", notes=[SETOP_NOTE],
   st="Communs := SET_INTERSECTION(Postes_Actifs, Autre_Choix);", c=NOSET,
   cpp="std::set<std::string> communs;\nstd::set_intersection(a.begin(), a.end(), b.begin(), b.end(), std::inserter(communs, communs.begin()));",
   essai=("STRING", "JOIN(SET_INTERSECTION(Postes_Actifs, Autre_Choix), ',')", "P3", decl(D_POSTES, D_AUTRE)), ou="S F")
fn(name="SET_DIFFERENCE", cat="ensembles", params=[("A", "SET OF T", "le premier", False), ("B", "SET OF T", "ce qu'on retire", False)], ret="SET OF T",
   short="Un SET neuf : les valeurs de A qui ne sont pas dans B.", notes=[SETOP_NOTE],
   st="Seulement_A := SET_DIFFERENCE(Postes_Actifs, Autre_Choix);", c=NOSET,
   cpp="std::set<std::string> seulement_a;\nstd::set_difference(a.begin(), a.end(), b.begin(), b.end(), std::inserter(seulement_a, seulement_a.begin()));",
   essai=("STRING", "JOIN(SET_DIFFERENCE(Postes_Actifs, Autre_Choix), ',')", "P1", decl(D_POSTES, D_AUTRE)), ou="S F")
fn(name="RING_ADD", cat="ensembles", params=[("R", "RING[N] OF T", "le tampon circulaire", False), ("X", "T", "la valeur", False)], ret="DINT",
   short="Ajoute la valeur à la fin ; plein (N valeurs), la plus ancienne part. Rend le nombre de valeurs (au plus N).",
   notes=["LIST_ADD sur un RING fait de même.", "R.Count, R.Capacity (N), R.Full ; FOR EACH va de la plus ancienne à la plus récente ; "
          "LIST_AVG, LIST_MIN, LIST_MAX, LIST_SUM, JOIN, LIST_GET le lisent comme une liste (une moyenne glissante).",
          "N est une constante entière de 1 à 100000 : RING[100] OF REAL.", SCRIPT_NOTE],
   st="RING_ADD(Dernieres, Mesure);\nMoyenne := LIST_AVG(Dernieres);   (* les 100 dernières mesures *)",
   c="/* pas de tampon circulaire en C : un tableau et un indice qui tourne */\nt[(debut + n) % N] = x;\nif (n < N) ++n; else debut = (debut + 1) % N;",
   cpp="dernieres.push_back(mesure);   // std::deque<float> (boost::circular_buffer le borne seul)\nif (dernieres.size() > 100) dernieres.pop_front();",
   essai=("STRING", "JOIN(Dernieres, ';') + ' ' + TO_STRING(Dernieres.Full)", "12.4;13.1;12.8;13.5;14.2 TRUE", decl(D_RING) + "\nRING_ADD(Dernieres, 14.2);"), ou="S F")

# ------------------------------------------------ 1.12.7 : std::numeric (19 fonctions) --
#  La bibliotheque <numeric> du C++ jusqu'au C++26 : plier (ACCUMULATE, REDUCE, INNER_PRODUCT,
#  TRANSFORM_REDUCE), cumuler (PARTIAL_SUM, les SCAN, ADJACENT_DIFFERENCE), remplir (IOTA), les
#  entiers (GCD, LCM, MIDPOINT) et le calcul sature (ADD_SAT... SATURATE_CAST<T>). OPERATION :
#  Somme, Produit, Min, Max, Et, Ou ; TRANSFORMATION : Identite, Abs, Carre, Racine, Neg, Inverse.
D_PAS = "Pas : ARRAY[0..4] OF REAL := [3.0, 1.0, 4.0, 1.0, 5.0]"
D_DEP = "Deplacements : LIST OF REAL := [3.0, -1.0, 4.0, -1.0, 5.0]"
D_SEUILS = "Seuils : LIST OF REAL := [10.0, 20.0, 50.0]"
D_POIDS = "Poids : LIST OF REAL := [0.5, 0.3, 0.2]"
OPN = "#Somme (par défaut), #Produit, #Min, #Max, #Et, #Ou"
TRN = "#Identite, #Abs, #Carre, #Racine, #Neg, #Inverse"
SEQ = "ARRAY, LIST, VECTOR de nombres"
RET_SEQ = "Une LIST rend une LIST, un ARRAY un ARRAY aux mêmes bornes."
fn(name="ACCUMULATE", cat="numeric", params=[("T", SEQ, "les éléments", False), ("INIT", "un nombre", "le départ : 0.0 pour une somme, 1 pour un produit", False),
                                            ("OP", "OPERATION", OPN, True)], ret="comme INIT",
   short="Plie les éléments de T dans INIT, du premier au dernier : la somme, ou avec OP le produit, le minimum, le maximum.",
   notes=["Le type de INIT compte, comme en C++ : ACCUMULATE(Mesures, 0) plie en entiers ; 0.0 plie en réels.", "LIST_SUM fait la somme seule."],
   st="Total_Pas := ACCUMULATE(Pas_Mesure, 0.0);", c="/* C n'a pas <numeric> : la boucle */\ndouble total = 0.0;\nfor (size_t i = 0; i < 10; ++i)\n    total += Pas_Mesure[i];",
   cpp="#include <numeric>\ndouble total = std::accumulate(std::begin(Pas_Mesure), std::end(Pas_Mesure), 0.0);\n// avec op : std::accumulate(…, 1.0, std::multiplies<>{})",
   essai=("REAL", "ACCUMULATE(Pas, 1.0, OPERATION#Produit)", "60", decl(D_PAS)), ou="S F X T")
fn(name="REDUCE", cat="numeric", params=[("T", SEQ, "les éléments", False), ("INIT", "un nombre", "0 sans lui", True), ("OP", "OPERATION", "#Somme sans lui", True)],
   ret="comme les éléments", short="Comme ACCUMULATE, sans ordre imposé (le C++ peut le paralléliser) ; en ST, le même résultat. INIT vaut 0 s'il manque.",
   st="Somme_Seuils := REDUCE(Seuils);", c="double somme = 0.0;\nfor (size_t i = 0; i < n; ++i)\n    somme += Seuils[i];",
   cpp="#include <numeric>\ndouble somme = std::reduce(Seuils.begin(), Seuils.end());\n// en parallèle : std::reduce(std::execution::par, Seuils.begin(), Seuils.end())",
   essai=("REAL", "REDUCE(Seuils)", "80", decl(D_SEUILS)), ou="S F X T")
fn(name="INNER_PRODUCT", cat="numeric", params=[("A", "ARRAY, LIST, VECTOR", "les valeurs", False), ("B", "idem, au moins aussi long", "les poids", False), ("INIT", "un nombre", "le départ, 0.0", False)],
   ret="comme INIT", short="Le produit scalaire : INIT plus la somme des A[i] × B[i]. Une moyenne pondérée, une énergie (puissances × durées).",
   notes=["B plus court que A : une erreur (« B est plus court que A : B[3] manque »)."],
   st="Note := INNER_PRODUCT(Seuils, Poids, 0.0);   (* Poids : [0.5, 0.3, 0.2] *)", c="double note = 0.0;\nfor (size_t i = 0; i < n; ++i)\n    note += Seuils[i] * Poids[i];",
   cpp="#include <numeric>\ndouble note = std::inner_product(Seuils.begin(), Seuils.end(),\n                                 Poids.begin(), 0.0);",
   essai=("REAL", "INNER_PRODUCT(Seuils, Poids, 0.0)", "21", decl(D_SEUILS, D_POIDS)), ou="S F X T")
fn(name="TRANSFORM_REDUCE", cat="numeric", params=[("T", SEQ, "les éléments", False), ("INIT", "un nombre", "le départ", False), ("OP", "OPERATION", "#Somme, #Produit, #Min, #Max…", False),
                                                  ("TRANSFORMATION", "TRANSFORMATION", TRN, False)], ret="comme INIT",
   short="Transforme chaque élément (Abs, Carre, Racine…), puis plie avec OP : la somme des carrés, la somme des valeurs absolues.",
   notes=["Sans ordre imposé, comme REDUCE. La forme à deux suites, A et B, est INNER_PRODUCT."],
   st="Energie := TRANSFORM_REDUCE(Ecarts, 0.0, OPERATION#Somme, TRANSFORMATION#Carre);", c="double energie = 0.0;\nfor (size_t i = 0; i < n; ++i)\n    energie += Ecarts[i] * Ecarts[i];",
   cpp="#include <numeric>\ndouble energie = std::transform_reduce(Ecarts.begin(), Ecarts.end(), 0.0,\n    std::plus<>{}, [](double e) { return e * e; });",
   essai=("REAL", "TRANSFORM_REDUCE(Seuils, 0.0, OPERATION#Somme, TRANSFORMATION#Carre)", "3000", decl(D_SEUILS)), ou="S F X T")
fn(name="PARTIAL_SUM", cat="numeric", params=[("T", SEQ, "les éléments", False), ("OP", "OPERATION", "#Somme sans lui", True)], ret="comme T",
   short="Les sommes cumulées : [a, a+b, a+b+c…] ; avec OP, le produit cumulé, le minimum atteint…", notes=[RET_SEQ],
   st="Couples_Cumules := PARTIAL_SUM(Couples);", c="double cumul = 0.0;\nfor (size_t i = 0; i < n; ++i) {\n    cumul += v[i];\n    cumules[i] = cumul;\n}",
   cpp="#include <numeric>\nstd::partial_sum(v.begin(), v.end(), cumules.begin());",
   essai=("STRING", "JOIN(PARTIAL_SUM(Pas), ',')", "3, 4, 8, 9, 14".replace(" ", ""), decl(D_PAS)), ou="S F X T")
fn(name="INCLUSIVE_SCAN", cat="numeric", params=[("T", "ARRAY, LIST, VECTOR", "les éléments", False), ("OP", "OPERATION", "#Somme sans lui", True)], ret="comme T",
   short="Comme PARTIAL_SUM, sans ordre imposé : le maximum atteint jusque-là, le minimum…", notes=[RET_SEQ],
   st="Pics := INCLUSIVE_SCAN(Mesures, OPERATION#Max);", c="double pic = m[0];\nfor (size_t i = 0; i < n; ++i) {\n    if (m[i] > pic) pic = m[i];\n    pics[i] = pic;\n}",
   cpp="#include <numeric>\nstd::inclusive_scan(m.begin(), m.end(), pics.begin(),\n    [](double a, double b) { return std::max(a, b); });",
   essai=("STRING", "JOIN(INCLUSIVE_SCAN(Pas, OPERATION#Max), ',')", "3,3,4,4,5", decl(D_PAS)), ou="S F X T")
fn(name="EXCLUSIVE_SCAN", cat="numeric", params=[("T", "ARRAY, LIST, VECTOR", "les éléments", False), ("INIT", "un nombre", "la première valeur", False), ("OP", "OPERATION", "#Somme sans lui", True)],
   ret="comme T", short="Le cumul de ce qui précède : [init, init+a, init+a+b…]. Des positions de départ à partir de longueurs.", notes=[RET_SEQ],
   st="Debuts := EXCLUSIVE_SCAN(Longueurs, 0);", c="long pos = 0;\nfor (size_t i = 0; i < n; ++i) {\n    debuts[i] = pos;\n    pos += longueurs[i];\n}",
   cpp="#include <numeric>\nstd::exclusive_scan(l.begin(), l.end(), debuts.begin(), 0L);",
   essai=("STRING", "JOIN(EXCLUSIVE_SCAN(Pas, 0.0), ',')", "0,3,4,8,9", decl(D_PAS)), ou="S F X T")
fn(name="TRANSFORM_INCLUSIVE_SCAN", cat="numeric", params=[("T", "ARRAY, LIST, VECTOR", "les éléments", False), ("OP", "OPERATION", "#Somme, #Max…", False),
                                                          ("TRANSFORMATION", "TRANSFORMATION", "#Abs, #Carre…", False)], ret="comme T",
   short="Transforme chaque élément, puis cumule : le chemin parcouru à partir de déplacements signés (Abs), l'énergie cumulée (Carre).", notes=[RET_SEQ],
   st="Parcours := TRANSFORM_INCLUSIVE_SCAN(Deplacements, OPERATION#Somme, TRANSFORMATION#Abs);",
   c="double cumul = 0.0;\nfor (size_t i = 0; i < n; ++i) {\n    cumul += fabs(d[i]);\n    parcours[i] = cumul;\n}",
   cpp="#include <numeric>\nstd::transform_inclusive_scan(d.begin(), d.end(), parcours.begin(),\n    std::plus<>{}, [](double x) { return std::abs(x); });",
   essai=("STRING", "JOIN(TRANSFORM_INCLUSIVE_SCAN(Deplacements, OPERATION#Somme, TRANSFORMATION#Abs), ',')", "3,4,8,9,14", decl(D_DEP)), ou="S F X T")
fn(name="TRANSFORM_EXCLUSIVE_SCAN", cat="numeric", params=[("T", "ARRAY, LIST, VECTOR", "les éléments", False), ("INIT", "un nombre", "la première valeur", False),
                                                          ("OP", "OPERATION", "#Somme…", False), ("TRANSFORMATION", "TRANSFORMATION", "#Abs, #Carre…", False)], ret="comme T",
   short="Transforme chaque élément, puis cumule ce qui précède : la position où commence chaque déplacement.", notes=[RET_SEQ],
   st="Departs := TRANSFORM_EXCLUSIVE_SCAN(Deplacements, 0.0, OPERATION#Somme, TRANSFORMATION#Abs);",
   c="double pos = 0.0;\nfor (size_t i = 0; i < n; ++i) {\n    departs[i] = pos;\n    pos += fabs(d[i]);\n}",
   cpp="#include <numeric>\nstd::transform_exclusive_scan(d.begin(), d.end(), departs.begin(), 0.0,\n    std::plus<>{}, [](double x) { return std::abs(x); });",
   essai=("STRING", "JOIN(TRANSFORM_EXCLUSIVE_SCAN(Deplacements, 0.0, OPERATION#Somme, TRANSFORMATION#Abs), ',')", "0,3,4,8,9", decl(D_DEP)), ou="S F X T")
fn(name="ADJACENT_DIFFERENCE", cat="numeric", params=[("T", SEQ, "les éléments", False)], ret="comme T",
   short="Les écarts entre voisins : [a, b−a, c−b…]. Des pas, des vitesses à partir de positions.", notes=[RET_SEQ],
   st="Ecarts := ADJACENT_DIFFERENCE(Seuils);", c="ecarts[0] = s[0];\nfor (size_t i = 1; i < n; ++i)\n    ecarts[i] = s[i] - s[i - 1];",
   cpp="#include <numeric>\nstd::adjacent_difference(s.begin(), s.end(), ecarts.begin());",
   essai=("STRING", "JOIN(ADJACENT_DIFFERENCE(Seuils), ',')", "10,10,30", decl(D_SEUILS)), ou="S F X T")
fn(name="IOTA", cat="numeric", params=[("T", "ARRAY, LIST, VECTOR", "à remplir, en place", False), ("DEPART", "un nombre", "la première valeur", False), ("PAS", "un nombre", "1 sans lui", True)],
   ret="DINT", short="Remplit T de départ, départ + pas, départ + 2 pas… ; rend le nombre de cases.",
   notes=["Le pas vaut 1 s'il manque ; le pas est un ajout de l'IHM (le C++ n'a que 1).", "Un tableau IHM est écrit en place.", SCRIPT_NOTE],
   st="IOTA(Pas_Mesure, 0.0, 2.5);", c="for (size_t i = 0; i < 10; ++i)\n    Pas_Mesure[i] = 0.0 + i * 2.5;",
   cpp="#include <numeric>\nstd::iota(std::begin(Rangs), std::end(Rangs), 1);   // 1, 2, 3…\nstd::ranges::iota(Rangs, 1);                       // C++23, le même\n// un autre pas que 1 : std::generate",
   essai=("STRING", "JOIN(Pas, ',')", "0,2.5,5,7.5,10", decl(D_PAS) + "\nIOTA(Pas, 0.0, 2.5);"), expr=False, ou="S F")
fn(name="GCD", cat="numeric", params=[("A", "un entier", "", False), ("B", "un entier", "", False)], ret="un entier",
   short="Le plus grand diviseur commun de deux entiers ; 0 si les deux sont nuls.",
   st="Pgcd := GCD(Reglages.Cadence_A, Reglages.Cadence_B);", c="/* C n'a pas gcd : la boucle d'Euclide */\nlong a = 120, b = 84;\nwhile (b != 0) { long t = a % b; a = b; b = t; }",
   cpp="#include <numeric>\nauto pgcd = std::gcd(120, 84);   // 12", essai=("DINT", "GCD(120, 84)", "12"), ou="S F X T")
fn(name="LCM", cat="numeric", params=[("A", "un entier", "", False), ("B", "un entier", "", False)], ret="un entier",
   short="Le plus petit multiple commun de deux entiers ; 0 si l'un est nul.",
   st="Cadence_Commune := LCM(Reglages.Cadence_A, Reglages.Cadence_B);", c="/* C n'a pas lcm : avec le pgcd de la boucle de GCD */\nlong ppcm = a / pgcd(a, b) * b;",
   cpp="#include <numeric>\nauto ppcm = std::lcm(120, 84);   // 840", essai=("DINT", "LCM(120, 84)", "840"), ou="S F X T")
fn(name="MIDPOINT", cat="numeric", params=[("<T>", "un type numérique", "facultatif : INT, DINT, REAL…", True), ("A", "un nombre", "", False), ("B", "un nombre", "", False)],
   ret="T, ou le type de A", short="Le milieu de a et b sans débordement : en INT, 32000 et 32002 donnent 32001, là où (a + b) / 2 déborde.",
   notes=["Entre deux entiers, arrondi vers a. T se donne entre < > ou vient de a."],
   st="Milieu := MIDPOINT<INT>(32000, 32002);", c="/* C n'a pas midpoint : a et b passent en int (32 bits), sans débordement */\nint16_t m = (int16_t)(a + (b - a) / 2);   /* arrondi vers a */",
   cpp="#include <numeric>\nINT m = std::midpoint<INT>(32000, 32002);   // 32001 (INT : int16_t)\n// des réels : std::midpoint(0.5, 2.5) donne 1.5",
   essai=("INT", "MIDPOINT<INT>(32000, 32002)", "32001"), ou="S F X T")
for nm, sym, fr, ex, res, cline in [
        ("ADD_SAT", "+", "a + b, bloqué aux bornes de T au lieu de boucler : en INT, 32000 + 1000 donne 32767 (et non −32536).", "ADD_SAT<INT>(32000, 1000)", "32767",
         "/* C n'a pas add_sat : le calcul en 32 bits, puis borné */\nint32_t s = (int32_t)a + b;\nint16_t r = s > INT16_MAX ? INT16_MAX : s < INT16_MIN ? INT16_MIN : (int16_t)s;"),
        ("SUB_SAT", "-", "a − b, bloqué aux bornes : en INT, −32000 − 1000 donne −32768 ; en UINT, 5 − 8 donne 0.", "SUB_SAT<INT>(-32000, 1000)", "-32768",
         "int32_t s = (int32_t)a - b;   /* puis borné, comme ADD_SAT */"),
        ("MUL_SAT", "*", "a × b, bloqué aux bornes : en INT, 300 × 200 donne 32767.", "MUL_SAT<INT>(300, 200)", "32767",
         "int32_t p = (int32_t)a * b;   /* puis borné, comme ADD_SAT */"),
        ("DIV_SAT", "/", "a / b, bloqué : en INT, −32768 / −1 donne 32767, le seul cas qui déborde. b = 0 reste une erreur.", "DIV_SAT<INT>(-32768, -1)", "32767",
         "int16_t q = (a == INT16_MIN && b == -1) ? INT16_MAX : a / b;")]:
    fn(name=nm, cat="numeric", params=[("<T>", "un type entier", "facultatif : il fixe les bornes", True), ("A", "un entier", "", False), ("B", "un entier" if nm != "DIV_SAT" else "un entier non nul", "", False)],
       ret="T, ou le type de A", short=fr, notes=["T se donne entre < > ou vient de a (son type déclaré : une SINT garde ses bornes)."],
       st=("Total := " if nm == "ADD_SAT" else "Ecart := " if nm == "SUB_SAT" else "Produit := " if nm == "MUL_SAT" else "Q := ") + ex + ";",
       c=cline, cpp="#include <numeric>   // C++26\nINT r = std::" + nm.lower() + "<INT>" + ex[ex.index("("):] + ";   // " + res,
       essai=("INT", ex, res), ou="S F X T")
fn(name="SATURATE_CAST", cat="numeric", params=[("<T>", "un type entier", "obligatoire : SATURATE_CAST<INT>(x)", False), ("X", "un nombre", "arrondi, puis borné", False)],
   ret="T", short="Convertit x vers T en bloquant aux bornes : 40000 donne 32767 en INT, −5 donne 0 en UINT, là où TO_INT et TO_UINT bouclent.",
   notes=["Le type entre < > est obligatoire : SATURATE_CAST(x) seul est une erreur.", "En C++26, x est un entier ; en ST, un réel est d'abord arrondi."],
   st="Consigne := SATURATE_CAST<INT>(Mesure_Brute);   (* 40000 *)", c="/* C n'a pas saturate_cast : borné à la main */\nint16_t c = x > INT16_MAX ? INT16_MAX : x < INT16_MIN ? INT16_MIN : (int16_t)x;",
   cpp="#include <numeric>   // C++26\nINT c = std::saturate_cast<INT>(40000);   // 32767\n// un réel : std::saturate_cast<INT>(std::llround(x))",
   essai=("INT", "SATURATE_CAST<INT>(40000)", "32767"), ou="S F X T")

# ------------------------------------- 1.12.7 : std::numeric_limits et std::numbers --
fn(name="NUMERIC_LIMITS", cat="limits", params=[("<T>", "SINT … ULINT, BYTE … LWORD, REAL, LREAL, BOOL", "obligatoire", False),
                                               ("MEMBRE", "un des 32 membres", "MIN() … : une fonction (avec ()) ; DIGITS … : une constante (sans)", False)],
   ret="T (une fonction), BOOL ou DINT (une constante)",
   short="Les bornes et les caractéristiques d'un type numérique, comme std::numeric_limits : NUMERIC_LIMITS<UINT>.MAX() = 65535.",
   notes=["9 fonctions, appelées avec () : MIN, MAX, LOWEST, EPSILON, ROUND_ERROR, INFINITY, QUIET_NAN, SIGNALING_NAN, DENORM_MIN.",
          "23 constantes, sans parenthèses : IS_SPECIALIZED, IS_SIGNED, IS_INTEGER, IS_EXACT, HAS_INFINITY, HAS_QUIET_NAN, HAS_SIGNALING_NAN, HAS_DENORM, HAS_DENORM_LOSS, "
          "ROUND_STYLE, IS_IEC559, IS_BOUNDED, IS_MODULO, DIGITS, DIGITS10, MAX_DIGITS10, RADIX, MIN_EXPONENT, MIN_EXPONENT10, MAX_EXPONENT, MAX_EXPONENT10, TRAPS, TINYNESS_BEFORE.",
          "Pour REAL et LREAL, MIN() est le plus petit positif normalisé ; le plus négatif, c'est LOWEST().",
          "L'aide à la saisie propose les types après NUMERIC_LIMITS<, puis les membres et leur valeur après NUMERIC_LIMITS<UINT>.",
          "Compiler dit un type refusé (NUMERIC_LIMITS<STRING>), un membre inconnu (« veux-tu dire MAX ? ») et les parenthèses : « DIGITS est une constante : sans parenthèses », « MIN est une fonction : NUMERIC_LIMITS<UINT>.MIN() »."],
   st="IF Compteur_Pieces = NUMERIC_LIMITS<UINT>.MAX() THEN\n    Compteur_Pieces := 0;   (* il bouclerait *)\nEND_IF",
   c="if (compteur_pieces == UINT16_MAX)   /* <stdint.h> ; FLT_EPSILON : <float.h> */\n    compteur_pieces = 0;",
   cpp="#include <limits>\nif (compteur_pieces == std::numeric_limits<UINT>::max())\n    compteur_pieces = 0;",
   essai=("UDINT", "NUMERIC_LIMITS<UINT>.MAX()", "65535"), ou="S F X T")
fn(name="NUMBERS", cat="numbers", params=[("<T>", "REAL ou LREAL", "facultatif : LREAL", True), ("CONSTANTE", "une des 13 constantes", "sans parenthèses", False)],
   ret="T (LREAL sans <T>)",
   short="Les constantes mathématiques de std::numbers : NUMBERS.PI (en LREAL), NUMBERS<REAL>.PI (arrondi au REAL).",
   notes=["Les 13 constantes : E, LOG2E, LOG10E, PI, INV_PI, INV_SQRTPI, LN2, LN10, SQRT2, SQRT3, INV_SQRT3, EGAMMA (γ, la constante d'Euler-Mascheroni), PHI (φ, le nombre d'or).",
          "Une variable du projet nommée NUMBERS garde son sens : NUMBERS.X est alors son membre.",
          "L'aide à la saisie propose les constantes et leur valeur après NUMBERS. et NUMBERS<REAL>."],
   st="Surface := NUMBERS.PI * Rayon * Rayon;", c="surface = M_PI * rayon * rayon;   /* <math.h> ; SQRT3, EGAMMA, PHI : la valeur */",
   cpp="#include <numbers>\nsurface = std::numbers::pi * rayon * rayon;   // pi_v<float> pour un REAL",
   essai=("BOOL", "NUMBERS.PI > 3.14159 AND NUMBERS.PI < 3.1416", "TRUE"), ou="S F X T")

# ------------------------------------------------------------- 1.12.7 : <bit> --
#  Les fonctions de <bit> du C++20 sur les mots (BYTE, WORD, DWORD, LWORD) et les entiers : un
#  mot d'alarme d'un equipement, seize defauts. Elles ne changent pas le mot (BIT_SET rend la
#  nouvelle valeur).
fn(name="BIT_COUNT", cat="bits", params=[("W", "ANY_BIT ou un entier", "le mot", False)], ret="DINT",
   short="Le nombre de bits à 1 : combien de défauts dans un mot d'alarme.", st="Nb_Defauts := BIT_COUNT(Defauts);",
   c="n = __builtin_popcount(defauts);   /* GCC ; une boucle sinon */", cpp="#include <bit>\nauto n = std::popcount(defauts);   // C++20",
   essai=("DINT", "BIT_COUNT(Mot)", "4"), ou="S F X T")
fn(name="BIT_FIRST", cat="bits", params=[("W", "ANY_BIT ou un entier", "le mot", False)], ret="DINT",
   short="Le numéro du plus bas bit à 1 (0 : le bit de poids faible) ; −1 si aucun.",
   notes=["std::countr_zero rend la largeur du mot (16 pour un WORD) quand aucun bit n'est à 1 ; l'IHM rend −1."],
   st="Premier_Defaut := BIT_FIRST(Defauts);", c="i = defauts ? __builtin_ctz(defauts) : -1;   /* GCC */",
   cpp="#include <bit>\nint i = defauts ? std::countr_zero(defauts) : -1;   // C++20", essai=("DINT", "BIT_FIRST(Mot)", "4"), ou="S F X T")
fn(name="BIT_TEST", cat="bits", params=[("W", "ANY_BIT ou un entier", "le mot", False), ("N", "INT", "le numéro du bit (0 : le plus bas)", False)], ret="BOOL",
   short="Vrai si le bit n est à 1.", notes=["Un bit hors du mot est une erreur : « le bit 16 n'existe pas dans un WORD (0..15) »."],
   st="Defaut_Pompe := BIT_TEST(Defauts, 4);", c="defaut_pompe = (defauts >> 4) & 1;", cpp="defaut_pompe = (defauts >> 4) & 1u;   // std::bitset : test(4)",
   essai=("BOOL", "BIT_TEST(Mot, 4)", "TRUE"), ou="S F X T")
fn(name="BIT_SET", cat="bits", params=[("W", "ANY_BIT ou un entier", "le mot", False), ("N", "INT", "le numéro du bit", False)], ret="comme W",
   short="La valeur avec le bit n à 1 ; le mot ne change pas (écrire Mot := BIT_SET(Mot, n) pour le changer).",
   st="Commande := BIT_SET(Commande, 0);", c="commande |= 1u << 0;", cpp="commande |= 1u << 0;   // std::bitset : set(0)",
   essai=("WORD", "BIT_SET(Mot, 0)", "241"), ou="S F X T")
fn(name="BIT_RESET", cat="bits", params=[("W", "ANY_BIT ou un entier", "le mot", False), ("N", "INT", "le numéro du bit", False)], ret="comme W",
   short="La valeur avec le bit n à 0 ; le mot ne change pas.",
   st="Commande := BIT_RESET(Commande, 0);", c="commande &= ~(1u << 0);", cpp="commande &= ~(1u << 0);   // std::bitset : reset(0)",
   essai=("WORD", "BIT_RESET(Mot, 4)", "224"), ou="S F X T")

# ------------------------------------------------------------------- fonctions IHM_ --
IHM = [
    # nom, cat, params, ret, phrase, exemple ST, essai (type, expression, attendu) ou None, lecture seule (fx)
    ("IHM_NAVIGUER", "nav", [("VUE", "STRING", "le nom de la vue", False), ("TRANSITION", "STRING", "Fondu, Glissement, Zoom, Rotation...", True), ("PARAMETRES", "STRING", "'Moteur := Pompes[3]'", True)], "BOOL",
     "Affiche une autre vue, avec une transition et des paramètres.", "IHM_NAVIGUER('Vue_Armoire_A', 'Glissement');", None, False),
    ("IHM_PRECEDENTE", "nav", [], "BOOL", "Revient à la vue d'avant (l'historique) ; vrai si la vue a changé.", "IHM_PRECEDENTE();", None, False),
    ("IHM_SUIVANTE", "nav", [], "BOOL", "Repart vers la vue quittée par Précédente.", "IHM_SUIVANTE();", None, False),
    ("IHM_ACCUEIL", "nav", [], "BOOL", "Ouvre la vue d'accueil : celle du groupe de l'utilisateur, sinon la vue de démarrage.", "IHM_ACCUEIL();", None, False),
    ("IHM_VUE", "nav", [], "STRING", "Le nom de la vue affichée.", "IF IHM_VUE() = 'Vue_Accueil' THEN ... END_IF", ("STRING", "IHM_VUE()", "Vue_Essai"), True),
    ("IHM_POPUP", "popup", [("VUE", "STRING", "la popup", False), ("PARAMETRES", "STRING", "'Moteur := Pompes[3]'", True), ("POSITION", "STRING", "centre, objet, haut-droite, derniere, x,y", True)], "BOOL",
     "Ouvre une popup, avec ses paramètres et sa place ; déjà ouverte, elle revient devant.", "IHM_POPUP('Popup_Pompe', 'Moteur := Pompes[3]', 'objet');", None, False),
    ("IHM_CHANGER_POPUP", "popup", [("VUE", "STRING", "la popup", False), ("PARAMETRES", "STRING", "ses paramètres", True)], "BOOL",
     "Remplace la popup du dessus par une autre (ou la même, avec d'autres paramètres), au même endroit.", "IHM_CHANGER_POPUP('Popup_Pompe', 'Moteur := Pompes[4]');", None, False),
    ("IHM_POPUP_PRECEDENTE", "popup", [], "BOOL", "Revient à la popup d'avant le changement, à sa place.", "IHM_POPUP_PRECEDENTE();", None, False),
    ("IHM_CENTRER_POPUP", "popup", [("VUE", "STRING", "la popup (sans : celle du dessus)", True)], "BOOL", "Ramène une popup au milieu de la vue.", "IHM_CENTRER_POPUP();", None, False),
    ("IHM_FERMER_POPUP", "popup", [("VUE", "STRING", "la popup (sans : celle du dessus)", True)], "BOOL", "Ferme la popup du dessus (ou celle-ci).", "IHM_FERMER_POPUP();", None, False),
    ("IHM_FERMER_POPUPS", "popup", [], "BOOL", "Ferme toutes les popups.", "IHM_FERMER_POPUPS();", None, False),
    ("IHM_POPUP_OUVERTE", "popup", [("VUE", "STRING", "la popup", False)], "BOOL", "Vrai si cette popup est ouverte.", "IF NOT IHM_POPUP_OUVERTE('Popup_Aide') THEN IHM_POPUP('Popup_Aide'); END_IF", ("BOOL", "IHM_POPUP_OUVERTE('Popup_Aide')", "FALSE"), True),
    ("IHM_JOURNAL", "journal", [("MESSAGE", "STRING", "{Variable} ou {Variable:0.0} y met sa valeur", False)], "BOOL",
     "Écrit une ligne dans le journal de la séance.", "IHM_JOURNAL('Passage en maintenance à {IHM_TEMPS():t}');", None, False),
    ("IHM_LOG", "journal", [("NIVEAU", "NIVEAU_LOG", "TRACE, DEBUG, INFO, SUCCESS, WARNING, ERROR, CRITICAL", False), ("MESSAGE", "STRING", "{Variable} y met sa valeur", False)], "BOOL",
     "Écrit une ligne dans la Console : l'heure, le niveau, la source et la ligne du code.", "IHM_LOG(WARNING, 'Pression haute : {Pression:0.00} bar');", None, False),
    ("IHM_APPELER", "script", [("SCRIPT", "STRING", "un script général", False)], "BOOL", "Exécute tout de suite un script général.", "IHM_APPELER('Recalcul');", None, False),
    ("IHM_TEMPS", "script", [], "TIME", "Le temps écoulé depuis le lancement de l'IHM.", "IF IHM_TEMPS() > T#10s THEN ... END_IF", None, True),
    ("IHM_UTILISATEUR", "user", [], "STRING", "L'identifiant de l'utilisateur connecté ('' sans).", "IF IHM_UTILISATEUR() = '' THEN IHM_MENU_CONNEXION(); END_IF", ("STRING", "IHM_UTILISATEUR()", ""), True),
    ("IHM_NOM_UTILISATEUR", "user", [], "STRING", "Le nom de l'utilisateur connecté.", "Bienvenue := CONCAT('Bonjour ', IHM_NOM_UTILISATEUR());", None, True),
    ("IHM_GROUPE", "user", [], "STRING", "Le groupe de l'utilisateur connecté.", "Admin := IHM_GROUPE() = 'Maintenance';", None, True),
    ("IHM_NIVEAU", "user", [], "INT", "Le niveau d'accès de l'utilisateur connecté : 0 si personne ne l'est ; sans gestion des utilisateurs, 99 (tous les droits).", "Reglage_Permis := IHM_NIVEAU() >= 2;", ("INT", "IHM_NIVEAU()", "99"), True),
    ("IHM_DECONNECTER", "user", [], "BOOL", "Déconnecte l'utilisateur.", "IHM_DECONNECTER();", None, False),
    ("IHM_MENU_CONNEXION", "user", [("ONGLET", "STRING", "Comptes, Accès, Journal, Mon compte", True)], "BOOL",
     "Ouvre le menu natif de connexion (sur cet onglet, s'il se voit).", "IHM_MENU_CONNEXION('Mon compte');", None, False),
    ("IHM_METTRE_DE_COTE", "alarme", [("ALARME", "STRING", "une alarme, ou 'groupe:Zone'", False), ("MINUTES", "REAL", "0 : sans limite", True), ("RAISON", "STRING", "pour le journal", True)], "INT",
     "Met une alarme de côté : elle n'apparaît plus jusqu'à la fin du délai ; rend le nombre mis de côté.", "IHM_METTRE_DE_COTE('Pression_Haute', 30.0, 'Capteur en réparation');", None, False),
    ("IHM_REMETTRE", "alarme", [("ALARME", "STRING", "'*' : toutes", False)], "INT", "Remet en service une alarme mise de côté.", "IHM_REMETTRE('*');", None, False),
    ("IHM_FAIRE_TAIRE", "alarme", [], "BOOL", "Coupe le son des alarmes jusqu'à la prochaine apparition.", "IHM_FAIRE_TAIRE();", None, False),
    ("IHM_LANGUE", "affichage", [("LANGUE", "STRING", "un code ('en'), un nom ('English') ou 'suivante'", False)], "BOOL",
     "Change la langue de l'IHM ; vrai si elle est connue (Configuration › Langues).", "IHM_LANGUE('en');", None, False),
    ("IHM_THEME", "affichage", [("THEME", "STRING", "'jour', 'nuit' ; sans : l'autre", True)], "BOOL", "Change le thème de l'IHM.", "IHM_THEME('nuit');", None, False),
    ("IHM_PARAMETRES_SYSTEME", "affichage", [("ONGLET", "STRING", "'Diagnostic' ; sinon Réglages", True)], "BOOL", "Ouvre le menu natif Paramètres système.", "IHM_PARAMETRES_SYSTEME('Diagnostic');", None, False),
    ("IHM_SON", "affichage", [("SON", "STRING", "un son des ressources (WAV, MP3, OGG)", False)], "BOOL", "Joue un son des ressources.", "IHM_SON('Bip.wav');", None, False),
    # 1.14.0 : couper le son, le mélangeur de volume (les canaux du son).
    ("IHM_SON_MUET", "affichage", [("MUET", "BOOL", "TRUE : tout se tait ; FALSE : le son revient", False)], "BOOL",
     "Coupe tous les sons (ce qui joue se tait, les suivants ne partent pas) ou les rétablit, sans rien rejouer ; SYS.SoundEnabled suit.", "IHM_SON_MUET(TRUE);", None, False),
    ("IHM_SON_EST_MUET", "affichage", [], "BOOL", "Vrai si tous les sons sont coupés (NOT SYS.SoundEnabled).",
     "IF IHM_SON_EST_MUET() THEN IHM_SON_MUET(FALSE); END_IF", ("BOOL", "IHM_SON_EST_MUET()", "FALSE"), True),
    ("IHM_SON_VOLUME", "affichage", [("CANAL", "STRING", "'General', 'Alarmes', 'Scripts', 'Actions', 'Videos' ou 'Relecture'", False), ("VOLUME", "INT", "de 0 à 100 %", False)], "BOOL",
     "Règle le volume d'un canal du son ('General' : tous) ; les sons qui jouent le prennent tout de suite. Faux si le canal est inconnu.", "IHM_SON_VOLUME('Alarmes', 80);", None, False),
    ("IHM_SON_CANAL_MUET", "affichage", [("CANAL", "STRING", "'General', 'Alarmes', 'Scripts', 'Actions', 'Videos' ou 'Relecture'", False), ("MUET", "BOOL", "TRUE : le canal se tait ; FALSE : il revient", False)], "BOOL",
     "Coupe ou rétablit un canal du son : ses sons qui jouent se taisent tout de suite, les suivants ne partent pas. Faux si le canal est inconnu.", "IHM_SON_CANAL_MUET('Videos', TRUE);", None, False),
    ("IHM_GIF_JOUER", "gif", [("OBJET", "STRING", "le GIF de la vue (ou d'une popup ouverte)", False), ("TOURS", "INT", "0 : sans fin", True)], "BOOL",
     "Joue un GIF animé ; en pause, il reprend là où il s'était arrêté.", "IHM_GIF_JOUER('Vue_Ligne.Gif_Convoyeur');", None, False),
    ("IHM_GIF_PAUSE", "gif", [("OBJET", "STRING", "le GIF", False)], "BOOL", "Fige un GIF animé sur l'image montrée.", "IHM_GIF_PAUSE('Vue_Ligne.Gif_Convoyeur');", None, False),
    ("IHM_GIF_ARRETER", "gif", [("OBJET", "STRING", "le GIF", False)], "BOOL", "Ramène un GIF à sa première image ; il attend.", "IHM_GIF_ARRETER('Vue_Ligne.Gif_Convoyeur');", None, False),
    ("IHM_GIF_REJOUER", "gif", [("OBJET", "STRING", "le GIF", False), ("TOURS", "INT", "0 : sans fin", False)], "BOOL",
     "Rejoue un GIF depuis le début, pour ce nombre de tours.", "IHM_GIF_REJOUER('Vue_Ligne.Gif_Convoyeur', 3);", None, False),
    ("IHM_EQUIPEMENT_OK", "equip", [("EQUIPEMENT", "STRING", "Configuration › Équipements", False)], "BOOL", "Vrai si l'équipement est actif et répond.", "Voyant_Reseau := IHM_EQUIPEMENT_OK('Variateur_ATV320');", None, True),
    ("IHM_EQUIPEMENT_PING", "equip", [("EQUIPEMENT", "STRING", "l'équipement", False)], "REAL", "Le dernier ping de l'équipement, en ms (-1 : pas de réponse).", "Latence := IHM_EQUIPEMENT_PING('Variateur_ATV320');", None, True),
    ("IHM_ESCLAVE_SIMULE", "equip", [("EQUIPEMENT", "STRING", "l'équipement", False)], "BOOL",
     "Vrai si l'IHM lit cet équipement sur son esclave simulé.", "Bandeau_Simule := IHM_ESCLAVE_SIMULE('Centrale_PM5560');", None, True),
    ("IHM_EXPORTER", "export", [("SOURCE", "STRING", "alarmes, historique, evenements, systeme, mesures, recette:Nom, objet:Nom", False), ("FICHIER", "STRING", "l'extension choisit le format (.csv, .xlsx, .pdf)", True), ("DEMANDER", "BOOL", "FALSE : ne jamais demander où enregistrer", True)], "BOOL",
     "Exporte des données dans exports/ du projet.", "IHM_EXPORTER('alarmes', 'alarmes_du_jour.xlsx');", None, False),
]
def c_call(name, st):
    s = st.replace("'", "\"").replace(":=", "=").replace("IHM_CENTRER_POPUP()", "IHM_CENTRER_POPUP(NULL)").replace("IHM_FERMER_POPUP()", "IHM_FERMER_POPUP(NULL)")
    s = s.replace(" THEN ", " { ").replace(" END_IF", " }").replace("IF NOT ", "if (!").replace("IF ", "if (")
    return s
for name, cat, params, ret, short, st, essai, ro in IHM:
    c = st.replace("'", "\"")
    if c.startswith("IF "):
        cond = c[3:c.index(" THEN ")]
        body = c[c.index(" THEN ") + 6:c.rindex(" END_IF")]
        c = "if (" + cond.replace("NOT ", "!").replace(" = ", " == ").replace(" >= ", " >= ") + ") " + body.replace("...", "/* ... */")
    elif ":=" in c:
        c = c.replace(":=", "=")
    cpp = c
    if name in ("IHM_CENTRER_POPUP", "IHM_FERMER_POPUP"):
        c = c.replace("()", "(NULL)")
    if name == "IHM_LOG":
        c = c.replace("WARNING,", "LOG_WARNING,"); cpp = cpp.replace("WARNING,", "NiveauLog::WARNING,")
    if name in ("IHM_UTILISATEUR",) :
        c = 'if (strcmp(IHM_UTILISATEUR(), "") == 0) IHM_MENU_CONNEXION(NULL);'; cpp = 'if (IHM_UTILISATEUR().empty()) IHM_MENU_CONNEXION();'
    if name == "IHM_NOM_UTILISATEUR":
        c = 'snprintf(Bienvenue, sizeof Bienvenue, "Bonjour %s", IHM_NOM_UTILISATEUR());'; cpp = 'Bienvenue = "Bonjour " + IHM_NOM_UTILISATEUR();'
    if name == "IHM_GROUPE":
        c = 'Admin = strcmp(IHM_GROUPE(), "Maintenance") == 0;'; cpp = 'Admin = IHM_GROUPE() == "Maintenance";'
    if name == "IHM_VUE":
        c = 'if (strcmp(IHM_VUE(), "Vue_Accueil") == 0) { /* ... */ }'; cpp = 'if (IHM_VUE() == "Vue_Accueil") { /* ... */ }'
    if name == "IHM_TEMPS":
        c = "if (IHM_TEMPS() > 10000) { /* ... */ }   /* TIME : des ms */"; cpp = "if (IHM_TEMPS() > 10s) { /* ... */ }   // <chrono>"
    if name == "IHM_POPUP_OUVERTE":
        c = 'if (!IHM_POPUP_OUVERTE("Popup_Aide")) IHM_POPUP("Popup_Aide", NULL, NULL);'; cpp = 'if (!IHM_POPUP_OUVERTE("Popup_Aide")) IHM_POPUP("Popup_Aide");'
    if name == "IHM_JOURNAL":
        pass
    fn(name=name, cat=cat, params=params, ret=ret, short=short, st=st, c=c, cpp=cpp,
       essai=essai, ro=ro, plc="— (IHM seulement)",
       notes=(["Dans une expression (fx) ou un texte à trous : oui, elle ne fait que lire."] if ro else
              ["Elle agit : un script ou une fonction l'appelle, pas une expression (fx)."]))

# ------------------------------------------------------------------------ couleurs --
#  Une couleur est un texte : '#RRGGBB' ou '#RRGGBBAA' (l'opacite en dernier), comme dans
#  les proprietes des objets. Les fonctions lisent aussi '16#RRGGBB' et 'RRGGBB'.
COUL = "Couleurs et aléatoire"
fn(name="RGB", cat="couleur", params=[("R", "INT", "le rouge, 0 à 255", False), ("G", "INT", "le vert, 0 à 255", False), ("B", "INT", "le bleu, 0 à 255", False)], ret="STRING",
   short="La couleur de ces trois composantes : '#RRGGBB'.", notes=["Hors de 0 à 255, une composante est ramenée à la borne ; un réel est arrondi."],
   st="Fond := RGB(46, 204, 113);", c="snprintf(Fond, sizeof Fond, \"#%02X%02X%02X\", 46, 204, 113);",
   cpp="Fond = std::format(\"#{:02X}{:02X}{:02X}\", 46, 204, 113);   // C++20", essai=("STRING", "RGB(46, 204, 113)", "#2ECC71"))
fn(name="RGBA", cat="couleur", params=[("R", "INT", "le rouge, 0 à 255", False), ("G", "INT", "le vert", False), ("B", "INT", "le bleu", False), ("A", "INT", "l'opacité, 0 (transparent) à 255", False)], ret="STRING",
   short="La couleur et son opacité : '#RRGGBBAA'.", st="Voile := RGBA(0, 0, 0, 128);",
   c="snprintf(Voile, sizeof Voile, \"#%02X%02X%02X%02X\", 0, 0, 0, 128);", cpp="Voile = std::format(\"#{:02X}{:02X}{:02X}{:02X}\", 0, 0, 0, 128);",
   essai=("STRING", "RGBA(46, 204, 113, 128)", "#2ECC7180"))
fn(name="HSL", cat="couleur", params=[("H", "REAL", "la teinte, 0 à 360°", False), ("S", "REAL", "la saturation, 0 à 100 %", False), ("L", "REAL", "la luminosité, 0 à 100 %", False)], ret="STRING",
   short="La couleur d'une teinte, d'une saturation et d'une luminosité.", notes=["0° : rouge ; 120° : vert ; 240° : bleu. Une teinte qui suit une mesure fait un arc-en-ciel."],
   st="Teinte := HSL(Niveau * 1.2, 80.0, 50.0);", c="/* pas de fonction en C : la conversion TSL vers RVB est à écrire */", cpp="/* pas dans la bibliothèque standard */",
   essai=("STRING", "HSL(120.0, 100.0, 50.0)", "#00FF00"))
fn(name="COULEUR_MELANGER", cat="couleur", params=[("C1", "STRING", "la première couleur", False), ("C2", "STRING", "la seconde", False), ("T", "REAL", "0 : C1 ; 1 : C2 ; 0.5 : à mi-chemin", False)], ret="STRING",
   short="Le mélange de deux couleurs (opacité comprise).", st="Milieu := COULEUR_MELANGER('#000000', '#FFFFFF', 0.5);",
   c="/* pas de fonction en C : composante par composante, c1 + (c2 - c1) * t */", cpp="/* composante par composante : std::lerp(c1, c2, t) */",
   essai=("STRING", "COULEUR_MELANGER('#000000', '#FFFFFF', 0.5)", "#808080"))
fn(name="COULEUR_DEGRADE", cat="couleur", params=[("V", "REAL", "la valeur", False), ("MIN", "REAL", "la valeur de C1", False), ("MAX", "REAL", "la valeur de C2", False), ("C1", "STRING", "la couleur à MIN", False), ("C2", "STRING", "la couleur à MAX", False)], ret="STRING",
   short="La couleur d'une valeur sur un dégradé : C1 à MIN, C2 à MAX, entre les deux au prorata.",
   notes=["Hors de MIN..MAX, la couleur du bout le plus proche. Pour une barre qui passe du vert au rouge quand elle se remplit."],
   st="Couleur_Barre := COULEUR_DEGRADE(Niveau, 0.0, 100.0, '#2ECC71', '#E74C3C');", c="/* pas de fonction en C */", cpp="/* pas dans la bibliothèque standard */",
   essai=("STRING", "COULEUR_DEGRADE(Niveau, 0.0, 100.0, '#2ECC71', '#E74C3C')", "#7D965A"))
fn(name="COULEUR_ECLAIRCIR", cat="couleur", params=[("C", "STRING", "la couleur", False), ("P", "REAL", "de combien, 0 à 100 %", False)], ret="STRING",
   short="La couleur mêlée de blanc : 100 % donne du blanc.", st="Survol := COULEUR_ECLAIRCIR(Fond, 20.0);",
   c="/* pas de fonction en C */", cpp="/* pas dans la bibliothèque standard */", essai=("STRING", "COULEUR_ECLAIRCIR('#2ECC71', 50.0)", "#97E6B8"))
fn(name="COULEUR_ASSOMBRIR", cat="couleur", params=[("C", "STRING", "la couleur", False), ("P", "REAL", "de combien, 0 à 100 %", False)], ret="STRING",
   short="La couleur mêlée de noir : 100 % donne du noir.", st="Appui := COULEUR_ASSOMBRIR(Fond, 20.0);",
   c="/* pas de fonction en C */", cpp="/* pas dans la bibliothèque standard */", essai=("STRING", "COULEUR_ASSOMBRIR('#2ECC71', 50.0)", "#176639"))
fn(name="COULEUR_OPACITE", cat="couleur", params=[("C", "STRING", "la couleur", False), ("P", "REAL", "l'opacité, 0 (transparent) à 100 %", False)], ret="STRING",
   short="La même couleur, avec cette opacité : '#RRGGBBAA'.", st="Voile := COULEUR_OPACITE('#000000', 40.0);",
   c="/* pas de fonction en C */", cpp="/* pas dans la bibliothèque standard */", essai=("STRING", "COULEUR_OPACITE('#2ECC71', 50.0)", "#2ECC7180"))
fn(name="COULEUR_CONTRASTE", cat="couleur", params=[("C", "STRING", "la couleur du fond", False)], ret="STRING",
   short="Le noir ou le blanc, celui qui se lit le mieux sur ce fond.", st="Couleur_Texte := COULEUR_CONTRASTE(Fond);",
   c="/* pas de fonction en C : la luminance 0.299 R + 0.587 G + 0.114 B */", cpp="/* pas dans la bibliothèque standard */",
   essai=("STRING", "COULEUR_CONTRASTE('#2ECC71')", "#000000"))
for nm, comp, fr, ex, res in [("COULEUR_ROUGE", "R", "Le rouge d'une couleur, 0 à 255.", "COULEUR_ROUGE('#2ECC71')", "46"),
                              ("COULEUR_VERT", "G", "Le vert d'une couleur, 0 à 255.", "COULEUR_VERT('#2ECC71')", "204"),
                              ("COULEUR_BLEU", "B", "Le bleu d'une couleur, 0 à 255.", "COULEUR_BLEU('#2ECC71')", "113"),
                              ("COULEUR_ALPHA", "A", "L'opacité d'une couleur, 0 (transparent) à 255 ; 255 sans opacité écrite.", "COULEUR_ALPHA('#2ECC7180')", "128")]:
    fn(name=nm, cat="couleur", params=[("C", "STRING", "la couleur", False)], ret="INT", short=fr,
       st=f"{comp} := {nm}(Fond);", c=f"/* pas de fonction en C : strtoul sur deux chiffres hexadécimaux */", cpp="/* pas dans la bibliothèque standard */",
       essai=("INT", ex, res))

# ----------------------------------------------------------------------- aleatoire --
#  Une suite pseudo-aleatoire (Mersenne Twister), la meme d'une machine a l'autre :
#  RANDOM_SEED(n) la fait repartir ; sans lui, elle part de l'heure au lancement de l'IHM.
SEED = "RANDOM_SEED(42);"
fn(name="RANDOM", cat="alea", params=[], ret="REAL", short="Un réel au hasard, de 0 (compris) à 1 (exclu).",
   notes=["Dans une expression fx, la valeur change à chaque cycle d'affichage : un script la tire plutôt une fois."],
   st="IF RANDOM() < 0.1 THEN Panne := TRUE; END_IF   (* une fois sur dix *)", c="if (rand() / (RAND_MAX + 1.0) < 0.1) Panne = 1;",
   cpp="std::mt19937 gen{std::random_device{}()};\nif (std::uniform_real_distribution<>(0.0, 1.0)(gen) < 0.1) Panne = true;", essai=("REAL", "RANDOM()", "0.37454", SEED))
fn(name="RANDOM_INT", cat="alea", params=[("MIN", "DINT", "le plus petit", False), ("MAX", "DINT", "le plus grand (compris)", False)], ret="DINT",
   short="Un entier au hasard, de MIN à MAX compris.", st="De := RANDOM_INT(1, 6);", c="De = 1 + rand() % 6;",
   cpp="De = std::uniform_int_distribution<int>(1, 6)(gen);", essai=("DINT", "RANDOM_INT(1, 6)", "1", SEED))
fn(name="RANDOM_REAL", cat="alea", params=[("MIN", "REAL", "le plus petit", False), ("MAX", "REAL", "la borne haute (exclue)", False)], ret="REAL",
   short="Un réel au hasard, de MIN (compris) à MAX (exclu).", st="Mesure_Simulee := RANDOM_REAL(18.0, 22.0);",
   c="Mesure_Simulee = 18.0f + 4.0f * (rand() / (RAND_MAX + 1.0f));", cpp="Mesure_Simulee = std::uniform_real_distribution<float>(18.0f, 22.0f)(gen);",
   essai=("REAL", "RANDOM_REAL(18.0, 22.0)", "19.4982", SEED))
fn(name="RANDOM_SEED", cat="alea", params=[("N", "DINT", "la graine", False)], ret="BOOL",
   short="Fait repartir la suite : la même graine donne les mêmes nombres (pour rejouer un essai).", st="RANDOM_SEED(42);",
   c="srand(42);", cpp="gen.seed(42);", essai=None)

# ------------------------------------------------------------- dates et heures (1.12.1) --
fn(name="MAINTENANT", cat="date", params=[], ret="DATE_AND_TIME",
   short="La date et l'heure du poste (en simulation : celles du PC), à la milliseconde.",
   notes=["L'heure du mur, sans fuseau : DT#2026-10-09-14:30:00 est 14 h 30 là où tourne l'IHM."],
   st="Debut := MAINTENANT();", c="time_t debut = time(NULL);   /* localtime() pour la date */",
   cpp="auto debut = std::chrono::system_clock::now();", essai=("BOOL", "MAINTENANT() > DT#2020-01-01-00:00:00", "TRUE"))
fn(name="DT_TO_DATE", cat="date", params=[("IN", "DATE_AND_TIME", "une date et heure", False)], ret="DATE",
   short="La date d'une date et heure (à minuit).", st="Jour := DT_TO_DATE(MAINTENANT());",
   c="/* pas d'équivalent : localtime(), puis tm_year, tm_mon, tm_mday */", cpp="auto jour = std::chrono::floor<std::chrono::days>(maintenant);",
   essai=("BOOL", "DT_TO_DATE(DT#2026-10-09-14:30:00) = D#2026-10-09", "TRUE"), plc="DT_TO_DATE (EF)")
fn(name="DT_TO_TOD", cat="date", params=[("IN", "DATE_AND_TIME", "une date et heure", False)], ret="TIME_OF_DAY",
   short="L'heure du jour d'une date et heure.", st="Heure := DT_TO_TOD(MAINTENANT());",
   c="/* pas d'équivalent : localtime(), puis tm_hour, tm_min, tm_sec */", cpp="auto heure = maintenant - std::chrono::floor<std::chrono::days>(maintenant);",
   essai=("BOOL", "DT_TO_TOD(DT#2026-10-09-14:30:00) = TOD#14:30:00", "TRUE"), plc="DT_TO_TOD (EF)")
fn(name="CONCAT_DATE_TOD", cat="date", params=[("D", "DATE", "la date", False), ("TOD", "TIME_OF_DAY", "l'heure", False)], ret="DATE_AND_TIME",
   short="Une date et une heure du jour assemblées en une date et heure.", st="Rendez_vous := CONCAT_DATE_TOD(D#2026-10-09, TOD#14:30:00);",
   c="/* pas d'équivalent : mktime() sur un struct tm rempli */", cpp="auto rdv = jour + heure;   // std::chrono",
   essai=("BOOL", "CONCAT_DATE_TOD(D#2026-10-09, TOD#14:30:00) = DT#2026-10-09-14:30:00", "TRUE"), plc="CONCAT_DATE_TOD (EF)")

# ------------------------------------------------ 1.12.16 : les natives std:: (39 fonctions) --
#  La maquette 1.12.16 (natives1216.py) : <cmath> (CEIL, FLOOR, FMOD, HYPOT, ATAN2, LERP, ISNAN,
#  ISFINITE), <string> et <format> (STARTS_WITH, ENDS_WITH, CONTAINS, TRIM, FORMAT), <chrono> (YEAR
#  ... WEEKDAY, ADD_DAYS, DAYS_BETWEEN, FORMAT_DT), <algorithm> et les statistiques (LIST_LOWER_BOUND,
#  LIST_BINARY_SEARCH, LIST_TRANSFORM, LIST_MEDIAN, LIST_VARIANCE, LIST_STDDEV, INTERPOLER_TABLE), les
#  ensembles (SET_SYMMETRIC_DIFFERENCE, SET_INCLUDES), RANDOM_NORMAL, <bit> (BYTESWAP, WORDSWAP) et
#  <utility> (SWAP, EXCHANGE : elles ecrivent leurs arguments, jamais dans une expression fx).
#  inc / incpp : le C et le C++ que dit la fiche (le generateur ne devine pas std::ceil).
fn(name="CEIL", cat="math", params=[("IN", "ANY_NUM", "un nombre", False)], ret="comme IN",
   short="L'entier supérieur ou égal, dans le type de IN : CEIL(2.1) = 3.0, CEIL(-2.1) = -2.0.",
   notes=["ROUND arrondit au plus proche, TRUNC vers zéro ; CEIL toujours vers le haut."],
   st="Bacs := CEIL(Volume / 20.0);   (* 41 litres : 3 bacs *)",
   c="bacs = ceilf(volume / 20.0f);",
   cpp="bacs = std::ceil(volume / 20.0f);",
   essai=("REAL", "CEIL(2.1)", "3.0"), inc="Same", incpp="Same", expr=True, ou="S F X T")
fn(name="FLOOR", cat="math", params=[("IN", "ANY_NUM", "un nombre", False)], ret="comme IN",
   short="L'entier inférieur ou égal, dans le type de IN : FLOOR(2.9) = 2.0, FLOOR(-2.1) = -3.0.",
   notes=["TRUNC(-2.1) vaut -2.0 : FLOOR descend, TRUNC va vers zéro."],
   st="Pleins := FLOOR(Volume / 20.0);   (* 41 litres : 2 bacs pleins *)",
   c="pleins = floorf(volume / 20.0f);",
   cpp="pleins = std::floor(volume / 20.0f);",
   essai=("REAL", "FLOOR(-2.1)", "-3.0"), inc="Same", incpp="Same", expr=True, ou="S F X T")
fn(name="FMOD", cat="math", params=[("X", "REAL", "le dividende", False), ("Y", "REAL", "le diviseur (pas 0)", False)], ret="REAL",
   short="Le reste réel de X / Y, du signe de X : FMOD(370.0, 360.0) = 10.0. MOD ne prend que des entiers.",
   notes=["Y = 0 : une erreur (division par zéro), comme MOD."],
   st="Angle := FMOD(Angle + 15.0, 360.0);",
   c="angle = fmodf(angle + 15.0f, 360.0f);",
   cpp="angle = std::fmod(angle + 15.0f, 360.0f);",
   essai=("REAL", "FMOD(370.0, 360.0)", "10.0"), inc="Same", incpp="Same", expr=True, ou="S F X T")
fn(name="HYPOT", cat="math", params=[("X", "REAL", "un côté", False), ("Y", "REAL", "l'autre côté", False)], ret="REAL",
   short="La longueur SQRT(X² + Y²), sans débordement : la distance entre deux points, le module d'un vecteur.",
   st="Distance := HYPOT(X2 - X1, Y2 - Y1);",
   c="distance = hypotf(x2 - x1, y2 - y1);",
   cpp="distance = std::hypot(x2 - x1, y2 - y1);",
   essai=("REAL", "HYPOT(3.0, 4.0)", "5.0"), inc="Same", incpp="Same", expr=True, ou="S F X T")
fn(name="ATAN2", cat="math", params=[("Y", "REAL", "l'ordonnée", False), ("X", "REAL", "l'abscisse", False)], ret="REAL",
   short="L'angle du point (X, Y), en radians de -π à π, dans le bon quadrant (ATAN(Y / X) se trompe de moitié pour X < 0).",
   notes=["L'ordre des arguments est celui de C : Y d'abord, puis X."],
   st="Cap := ATAN2(Dy, Dx) * 180.0 / NUMBERS.PI;   (* en degrés *)",
   c="cap = atan2f(dy, dx) * 180.0f / M_PI;",
   cpp="cap = std::atan2(dy, dx) * 180.0f / std::numbers::pi_v<float>;",
   essai=("REAL", "ATAN2(1.0, 1.0) * 4.0", "3.1415927"), inc="Same", incpp="Same", expr=True, ou="S F X T")
fn(name="LERP", cat="math", params=[("A", "REAL", "la valeur à T = 0", False), ("B", "REAL", "la valeur à T = 1", False), ("T", "REAL", "la position (0 à 1, ou au-delà)", False)], ret="REAL",
   short="L'interpolation linéaire A + T × (B - A) : une mise à l'échelle, une animation, un mélange. T = 0.5 : le milieu.",
   notes=["T hors de 0..1 extrapole (LIMIT(0.0, T, 1.0) pour rester entre A et B)."],
   st="Ouverture := LERP(Mini, Maxi, Avance);   (* Avance de 0.0 à 1.0 *)",
   c="ouverture = mini + avance * (maxi - mini);",
   cpp="ouverture = std::lerp(mini, maxi, avance);   // C++20",
   essai=("REAL", "LERP(10.0, 20.0, 0.25)", "12.5"), inc="Equivalent", incpp="Same", expr=True, ou="S F X T")
fn(name="ISNAN", cat="math", params=[("IN", "REAL", "un réel", False)], ret="BOOL",
   short="Vrai si IN n'est pas un nombre (NaN) : un capteur en défaut, un calcul 0.0 / 0.0.",
   st="IF ISNAN(Mesure) THEN Defaut_Capteur := TRUE; END_IF",
   c="if (isnan(mesure)) defaut_capteur = true;",
   cpp="if (std::isnan(mesure)) defaut_capteur = true;",
   essai=("BOOL", "ISNAN(NUMERIC_LIMITS<REAL>.QUIET_NAN())", "TRUE"), inc="Same", incpp="Same", expr=True, ou="S F X T")
fn(name="ISFINITE", cat="math", params=[("IN", "REAL", "un réel", False)], ret="BOOL",
   short="Vrai si IN est un nombre fini : ni NaN, ni l'infini. Une mesure à afficher.",
   st="IF NOT ISFINITE(Debit) THEN Debit := 0.0; END_IF",
   c="if (!isfinite(debit)) debit = 0.0f;",
   cpp="if (!std::isfinite(debit)) debit = 0.0f;",
   essai=("BOOL", "ISFINITE(1.0 / 3.0)", "TRUE"), inc="Same", incpp="Same", expr=True, ou="S F X T")
fn(name="STARTS_WITH", cat="texte", params=[("S", "STRING", "le texte", False), ("DEBUT", "STRING", "ce par quoi il commence", False)], ret="BOOL",
   short="Vrai si S commence par DEBUT (majuscules et minuscules comptent).",
   st="IF STARTS_WITH(Code, 'E') THEN Erreurs := Erreurs + 1; END_IF",
   c="if (strncmp(code, \"E\", 1) == 0) erreurs++;",
   cpp="if (code.starts_with(\"E\")) ++erreurs;   // C++20",
   essai=("BOOL", "STARTS_WITH('E12-four', 'E12')", "TRUE"), inc="Equivalent", incpp="Same", expr=True, ou="S F X T")
fn(name="ENDS_WITH", cat="texte", params=[("S", "STRING", "le texte", False), ("FIN", "STRING", "ce par quoi il finit", False)], ret="BOOL",
   short="Vrai si S finit par FIN (majuscules et minuscules comptent).",
   st="IF ENDS_WITH(Fichier, '.csv') THEN … END_IF",
   c="/* comparer la fin : strlen, strcmp */",
   cpp="if (fichier.ends_with(\".csv\")) { … }   // C++20",
   essai=("BOOL", "ENDS_WITH('releve.csv', '.csv')", "TRUE"), inc="Equivalent", incpp="Same", expr=True, ou="S F X T")
fn(name="CONTAINS", cat="texte", params=[("S", "STRING", "le texte", False), ("PARTIE", "STRING", "ce qu'on y cherche", False)], ret="BOOL",
   short="Vrai si PARTIE est dans S. Plus lisible que FIND(S, PARTIE) > 0.",
   st="Alerte := CONTAINS(Message, 'défaut');",
   c="alerte = strstr(message, \"defaut\") != NULL;",
   cpp="alerte = message.contains(\"défaut\");   // C++23",
   essai=("BOOL", "CONTAINS('pompe en défaut', 'défaut')", "TRUE"), inc="Equivalent", incpp="Same", expr=True, ou="S F X T")
fn(name="TRIM", cat="texte", params=[("S", "STRING", "le texte", False)], ret="STRING",
   short="S sans les espaces, tabulations et retours à la ligne du début et de la fin.",
   st="Nom := TRIM(Saisie);",
   c="/* avancer le debut, reculer la fin (isspace) */",
   cpp="auto b = s.find_first_not_of(\" \\t\\r\\n\"); auto e = s.find_last_not_of(\" \\t\\r\\n\");",
   essai=("STRING", "TRIM('  Four 2  ')", "Four 2"), inc="Equivalent", incpp="Equivalent", expr=True, ou="S F X T")
fn(name="TRIM_LEFT", cat="texte", params=[("S", "STRING", "le texte", False)], ret="STRING",
   short="S sans les espaces du début.",
   st="Ligne := TRIM_LEFT(Ligne);",
   c="/* avancer le debut (isspace) */",
   cpp="s.erase(0, s.find_first_not_of(\" \\t\\r\\n\"));",
   essai=("STRING", "CONCAT(TRIM_LEFT('  a '), '|')", "a |"), inc="Equivalent", incpp="Equivalent", expr=True, ou="S F X T")
fn(name="TRIM_RIGHT", cat="texte", params=[("S", "STRING", "le texte", False)], ret="STRING",
   short="S sans les espaces de la fin.",
   st="Ligne := TRIM_RIGHT(Ligne);",
   c="/* reculer la fin (isspace) */",
   cpp="s.erase(s.find_last_not_of(\" \\t\\r\\n\") + 1);",
   essai=("STRING", "CONCAT(TRIM_RIGHT('  a '), '|')", "  a|"), inc="Equivalent", incpp="Equivalent", expr=True, ou="S F X T")
fn(name="FORMAT", cat="texte", params=[("MODELE", "STRING", "le texte, ses {} à remplir", False), ("VALEURS", "ANY", "une valeur par {} (autant que de {})", False)], ret="STRING",
   short="Compose un texte : chaque {} prend la valeur suivante. {:.1f} : 1 décimale ; {:5} : 5 caractères au moins ; {:05} : des zéros devant ; {:x} : en hexadécimal ; {{ et }} : une accolade.",
   notes=["Plus de {} que de valeurs, ou l'inverse : une erreur qui dit combien."],
   st="Txt := FORMAT('{:.1f} bar au poste {}', Pression, Poste);",
   c="snprintf(txt, sizeof txt, \"%.1f bar au poste %d\", pression, poste);",
   cpp="txt = std::format(\"{:.1f} bar au poste {}\", pression, poste);   // C++20",
   essai=("STRING", "FORMAT('{:.1f} bar au poste {:02}', 2.345, 7)", "2.3 bar au poste 07"), inc="Equivalent", incpp="Same", expr=True, ou="S F X T")
fn(name="YEAR", cat="date", params=[("DT", "DATE_AND_TIME, DATE", "la date", False)], ret="INT",
   short="L'année d'une date (2026).",
   st="An := YEAR(MAINTENANT());",
   c="an = tm.tm_year + 1900;",
   cpp="an = int(std::chrono::year_month_day{std::chrono::floor<std::chrono::days>(t)}.year());",
   essai=("INT", "YEAR(DT#2026-10-19-14:30:05)", "2026"), inc="Equivalent", incpp="Equivalent", expr=True, ou="S F X T")
fn(name="MONTH", cat="date", params=[("DT", "DATE_AND_TIME, DATE", "la date", False)], ret="INT",
   short="Le mois d'une date, de 1 (janvier) à 12.",
   st="Mois := MONTH(MAINTENANT());",
   c="mois = tm.tm_mon + 1;",
   cpp="mois = unsigned(ymd.month());",
   essai=("INT", "MONTH(DT#2026-10-19-14:30:05)", "10"), inc="Equivalent", incpp="Equivalent", expr=True, ou="S F X T")
fn(name="DAY", cat="date", params=[("DT", "DATE_AND_TIME, DATE", "la date", False)], ret="INT",
   short="Le jour du mois d'une date, de 1 à 31.",
   st="Jour := DAY(MAINTENANT());",
   c="jour = tm.tm_mday;",
   cpp="jour = unsigned(ymd.day());",
   essai=("INT", "DAY(DT#2026-10-19-14:30:05)", "19"), inc="Equivalent", incpp="Equivalent", expr=True, ou="S F X T")
fn(name="HOUR", cat="date", params=[("DT", "DATE_AND_TIME, TIME_OF_DAY", "l'heure", False)], ret="INT",
   short="L'heure, de 0 à 23.",
   st="IF HOUR(MAINTENANT()) >= 22 THEN Nuit := TRUE; END_IF",
   c="heure = tm.tm_hour;",
   cpp="heure = std::chrono::hh_mm_ss{t - jour}.hours().count();",
   essai=("INT", "HOUR(DT#2026-10-19-14:30:05)", "14"), inc="Equivalent", incpp="Equivalent", expr=True, ou="S F X T")
fn(name="MINUTE", cat="date", params=[("DT", "DATE_AND_TIME, TIME_OF_DAY", "l'heure", False)], ret="INT",
   short="La minute, de 0 à 59.",
   st="Minute := MINUTE(MAINTENANT());",
   c="minute = tm.tm_min;",
   cpp="minute = hms.minutes().count();",
   essai=("INT", "MINUTE(DT#2026-10-19-14:30:05)", "30"), inc="Equivalent", incpp="Equivalent", expr=True, ou="S F X T")
fn(name="SECOND", cat="date", params=[("DT", "DATE_AND_TIME, TIME_OF_DAY", "l'heure", False)], ret="INT",
   short="La seconde, de 0 à 59.",
   st="Seconde := SECOND(MAINTENANT());",
   c="seconde = tm.tm_sec;",
   cpp="seconde = hms.seconds().count();",
   essai=("INT", "SECOND(DT#2026-10-19-14:30:05)", "5"), inc="Equivalent", incpp="Equivalent", expr=True, ou="S F X T")
fn(name="WEEKDAY", cat="date", params=[("DT", "DATE_AND_TIME, DATE", "la date", False)], ret="INT",
   short="Le jour de la semaine : 1 lundi … 7 dimanche (la norme ISO 8601).",
   notes=["Le 19/10/2026 est un lundi."],
   st="Weekend := WEEKDAY(MAINTENANT()) >= 6;",
   c="jour = tm.tm_wday == 0 ? 7 : tm.tm_wday;",
   cpp="jour = std::chrono::weekday{std::chrono::floor<std::chrono::days>(t)}.iso_encoding();",
   essai=("INT", "WEEKDAY(DT#2026-10-19-14:30:05)", "1"), inc="Equivalent", incpp="Equivalent", expr=True, ou="S F X T")
fn(name="ADD_DAYS", cat="date", params=[("DT", "DATE_AND_TIME, DATE", "la date", False), ("N", "DINT", "le nombre de jours (négatif : en arrière)", False)], ret="comme DT",
   short="La date N jours plus tard (ou plus tôt), à la même heure.",
   st="Echeance := ADD_DAYS(Livraison, 30);",
   c="t += n * 86400;",
   cpp="t += std::chrono::days{n};",
   essai=("BOOL", "ADD_DAYS(DT#2026-10-19-14:30:05, 14) = DT#2026-11-02-14:30:05", "TRUE"), inc="Equivalent", incpp="Same", expr=True, ou="S F X T")
fn(name="DAYS_BETWEEN", cat="date", params=[("A", "DATE_AND_TIME, DATE", "la première date", False), ("B", "DATE_AND_TIME, DATE", "la seconde", False)], ret="DINT",
   short="Le nombre de jours entiers de A à B (négatif si B est avant A).",
   st="Age := DAYS_BETWEEN(Mise_En_Service, MAINTENANT());",
   c="jours = (long)((b - a) / 86400);",
   cpp="jours = std::chrono::floor<std::chrono::days>(b - a).count();",
   essai=("DINT", "DAYS_BETWEEN(DT#2026-10-01-08:00:00, DT#2026-10-19-14:30:05)", "18"), inc="Equivalent", incpp="Same", expr=True, ou="S F X T")
fn(name="FORMAT_DT", cat="date", params=[("DT", "DATE_AND_TIME, DATE, TIME_OF_DAY", "la date", False), ("MODELE", "STRING", "%d %m %Y %y %H %M %S %A %a %B %b %%", False)], ret="STRING",
   short="Écrit une date selon un modèle : %d jour, %m mois, %Y année, %H:%M:%S l'heure, %A lundi, %a lun., %B octobre, %b oct.",
   notes=["Les jours et les mois en français."],
   st="Txt := FORMAT_DT(MAINTENANT(), '%A %d %B, %H:%M');",
   c="strftime(txt, sizeof txt, \"%d/%m %H:%M\", &tm);",
   cpp="txt = std::format(\"{:%d/%m %H:%M}\", t);   // C++20",
   essai=("STRING", "FORMAT_DT(DT#2026-10-19-14:30:05, '%A %d %B, %H:%M')", "lundi 19 octobre, 14:30"), inc="Equivalent", incpp="Equivalent", expr=True, ou="S F X T")
fn(name="LIST_LOWER_BOUND", cat="liste", params=[("L", "LIST, VECTOR, ARRAY triée", "la liste, rangée en croissant", False), ("VALEUR", "T", "la valeur cherchée", False)], ret="DINT",
   short="Le premier indice dont l'élément est ≥ VALEUR, dans une liste triée ; L.Count si aucun. Une recherche rapide (par moitiés).",
   notes=["Une liste non triée donne un indice sans valeur : LIST_SORT d'abord. LOWER_BOUND (sans LIST_) reste la borne basse d'un tableau."],
   st="i := LIST_LOWER_BOUND(Paliers, Niveau);",
   c="/* une recherche par dichotomie */",
   cpp="i = std::ranges::lower_bound(paliers, niveau) - paliers.begin();",
   essai=("DINT", "LIST_LOWER_BOUND(L, 30.0)", "2", "VAR\n    L : LIST OF REAL := [10.0, 20.0, 30.0, 40.0];\nEND_VAR"), inc="Equivalent", incpp="Same", expr=True, ou="S F X T")
fn(name="LIST_BINARY_SEARCH", cat="liste", params=[("L", "LIST, VECTOR, ARRAY triée", "la liste, rangée en croissant", False), ("VALEUR", "T", "la valeur cherchée", False)], ret="BOOL",
   short="Vrai si VALEUR est dans la liste triée (une recherche par moitiés, plus rapide que LIST_CONTAINS sur une grande liste).",
   st="Connu := LIST_BINARY_SEARCH(Codes_Tries, Code);",
   c="/* bsearch */",
   cpp="connu = std::ranges::binary_search(codes, code);",
   essai=("BOOL", "LIST_BINARY_SEARCH(L, 25.0)", "FALSE", "VAR\n    L : LIST OF REAL := [10.0, 20.0, 30.0, 40.0];\nEND_VAR"), inc="Equivalent", incpp="Same", expr=True, ou="S F X T")
fn(name="LIST_TRANSFORM", cat="liste", params=[("SOURCE", "LIST, VECTOR, ARRAY", "les éléments", False), ("DESTINATION", "LIST, VECTOR", "vidée, puis un résultat par élément", False), ("OP", "TRANSFORMATION, ou STRING", "TRANSFORMATION#Carre… ou le nom d'une fonction IHM du projet à un paramètre", False)], ret="DINT",
   short="Applique OP à chaque élément de SOURCE et range les résultats dans DESTINATION ; rend leur nombre. OP peut être une fonction IHM du projet (son nom) : comme une lambda.",
   notes=["La fonction nommée prend un paramètre du type des éléments et rend le type des éléments de DESTINATION ; Compiler le vérifie."],
   st="LIST_TRANSFORM(Celsius, Kelvins, 'VersKelvin');",
   c="for (i = 0; i < n; ++i) kelvins[i] = vers_kelvin(celsius[i]);",
   cpp="std::ranges::transform(celsius, std::back_inserter(kelvins), vers_kelvin);",
   essai=("DINT", "LIST_TRANSFORM(L, Carres, TRANSFORMATION#Carre)", "3", "VAR\n    L : LIST OF REAL := [1.0, 2.0, 3.0];\n    Carres : LIST OF REAL;\nEND_VAR"), inc="Equivalent", incpp="Same", expr=False, ou="S F")
fn(name="LIST_MEDIAN", cat="liste", params=[("L", "LIST, VECTOR, ARRAY, RING de nombres", "les valeurs", False)], ret="REAL",
   short="La médiane : la valeur du milieu une fois rangées (la moyenne des deux du milieu pour un nombre pair). Elle filtre une mesure bruitée mieux qu'une moyenne.",
   notes=["Une liste vide : une erreur (la Console le dit)."],
   st="Filtree := LIST_MEDIAN(Dernieres);   (* un RING des 5 dernieres mesures *)",
   c="/* copier, trier (qsort), prendre le milieu */",
   cpp="std::ranges::nth_element(v, v.begin() + v.size() / 2);",
   essai=("REAL", "LIST_MEDIAN(L)", "3.0", "VAR\n    L : LIST OF REAL := [9.0, 1.0, 3.0, 2.0, 4.0];\nEND_VAR"), inc="Equivalent", incpp="Equivalent", expr=True, ou="S F X T")
fn(name="LIST_VARIANCE", cat="liste", params=[("L", "LIST, VECTOR, ARRAY, RING de nombres", "les valeurs", False), ("ECHANTILLON", "BOOL", "TRUE : divise par n - 1 (un échantillon)", True)], ret="REAL",
   short="La variance : la moyenne des carrés des écarts à la moyenne (divisée par n, ou par n - 1 pour un échantillon).",
   st="Dispersion := LIST_VARIANCE(Mesures);",
   c="/* deux boucles : la moyenne, puis les carres des ecarts */",
   cpp="auto m = std::reduce(v.begin(), v.end()) / v.size();",
   essai=("REAL", "LIST_VARIANCE(L)", "4.0", "VAR\n    L : LIST OF REAL := [2.0, 4.0, 4.0, 4.0, 5.0, 5.0, 7.0, 9.0];\nEND_VAR"), inc="Equivalent", incpp="Equivalent", expr=True, ou="S F X T")
fn(name="LIST_STDDEV", cat="liste", params=[("L", "LIST, VECTOR, ARRAY, RING de nombres", "les valeurs", False), ("ECHANTILLON", "BOOL", "TRUE : divise par n - 1 (un échantillon)", True)], ret="REAL",
   short="L'écart type : la racine de la variance. À peu près 68 % des valeurs sont à moins d'un écart type de la moyenne.",
   st="IF ABS(Mesure - LIST_AVG(Mesures)) > 3.0 * LIST_STDDEV(Mesures) THEN Aberrante := TRUE; END_IF",
   c="/* sqrt de la variance */",
   cpp="auto ecart = std::sqrt(variance);",
   essai=("REAL", "LIST_STDDEV(L)", "2.0", "VAR\n    L : LIST OF REAL := [2.0, 4.0, 4.0, 4.0, 5.0, 5.0, 7.0, 9.0];\nEND_VAR"), inc="Equivalent", incpp="Equivalent", expr=True, ou="S F X T")
fn(name="INTERPOLER_TABLE", cat="numeric", params=[("X", "LIST, ARRAY de nombres croissants", "les abscisses des points", False), ("Y", "LIST, ARRAY de nombres", "les ordonnées (autant que X)", False), ("V", "REAL", "là où lire la courbe", False)], ret="REAL",
   short="Lit une courbe donnée par des points, en ligne droite entre deux points : la courbe d'une vanne, d'un capteur. Avant le premier point, la première valeur ; après le dernier, la dernière.",
   notes=["X doit croître, et X et Y avoir autant de valeurs : sinon une erreur qui le dit."],
   st="Debit := INTERPOLER_TABLE(Ouvertures, Debits, Ouverture);",
   c="/* chercher l'intervalle, puis interpoler */",
   cpp="auto it = std::ranges::upper_bound(x, v); /* ... */ std::lerp(y[i - 1], y[i], t);",
   essai=("REAL", "INTERPOLER_TABLE(X, Y, 15.0)", "150.0", "VAR\n    X : LIST OF REAL := [0.0, 10.0, 20.0];\n    Y : LIST OF REAL := [0.0, 100.0, 200.0];\nEND_VAR"), inc="Equivalent", incpp="Equivalent", expr=True, ou="S F X T")
fn(name="SET_SYMMETRIC_DIFFERENCE", cat="ensembles", params=[("A", "SET", "un ensemble", False), ("B", "SET", "un autre, du même type", False)], ret="comme A",
   short="Les valeurs qui sont dans un seul des deux ensembles : ce qui a changé entre deux relevés.",
   st="Changes := SET_SYMMETRIC_DIFFERENCE(Actifs_Avant, Actifs);",
   c="/* deux boucles */",
   cpp="std::ranges::set_symmetric_difference(a, b, std::inserter(r, r.end()));",
   essai=("STRING", "JOIN(SET_SYMMETRIC_DIFFERENCE(A, B), ',')", "P1,P4", "VAR\n    A : SET OF STRING := ['P1', 'P2', 'P3'];\n    B : SET OF STRING := ['P2', 'P3', 'P4'];\nEND_VAR"), inc="Equivalent", incpp="Same", expr=True, ou="S F X T")
fn(name="SET_INCLUDES", cat="ensembles", params=[("A", "SET", "l'ensemble", False), ("B", "SET", "celui qui doit y être", False)], ret="BOOL",
   short="Vrai si chaque valeur de B est dans A : tout ce qui est requis est là.",
   st="Pret := SET_INCLUDES(Postes_Prets, Postes_Requis);",
   c="/* une boucle */",
   cpp="pret = std::ranges::includes(prets, requis);",
   essai=("BOOL", "SET_INCLUDES(A, B)", "TRUE", "VAR\n    A : SET OF STRING := ['P1', 'P2', 'P3'];\n    B : SET OF STRING := ['P3', 'P1'];\nEND_VAR"), inc="Equivalent", incpp="Same", expr=True, ou="S F X T")
fn(name="RANDOM_NORMAL", cat="alea", params=[("MOYENNE", "REAL", "la valeur du milieu", False), ("ECART", "REAL", "l'écart type (≥ 0)", False)], ret="REAL",
   short="Un nombre au hasard selon une loi normale (une cloche autour de MOYENNE) : un bruit réaliste pour une mesure simulée. RANDOM_SEED la rend rejouable.",
   notes=["ECART = 0 : toujours MOYENNE."],
   st="Mesure := Consigne + RANDOM_NORMAL(0.0, 0.2);",
   c="/* Box-Muller : deux rand() */",
   cpp="std::normal_distribution<float> loi(0.0f, 0.2f); mesure = consigne + loi(gen);",
   essai=("REAL", "RANDOM_NORMAL(5.0, 0.0)", "5.0"), inc="Equivalent", incpp="Same", expr=True, ou="S F X T")
fn(name="BYTESWAP", cat="bits", params=[("IN", "WORD, DWORD, INT, DINT, UINT, UDINT, LWORD", "un entier", False)], ret="comme IN",
   short="Les octets de IN dans l'ordre inverse : 16#1234 devient 16#3412. Pour un équipement Modbus qui envoie ses octets à l'envers.",
   st="Valeur := BYTESWAP(Brut);",
   c="v = (uint16_t)((brut >> 8) | (brut << 8));",
   cpp="v = std::byteswap(brut);   // C++23",
   essai=("WORD", "BYTESWAP(16#1234)", "16#3412"), inc="Equivalent", incpp="Same", expr=True, ou="S F X T")
fn(name="WORDSWAP", cat="bits", params=[("IN", "DWORD, DINT, UDINT, LWORD", "un entier de 32 bits (ou 64)", False)], ret="comme IN",
   short="Les mots de 16 bits de IN dans l'ordre inverse : 16#12345678 devient 16#56781234. L'ordre des registres d'un équipement Modbus.",
   st="Compteur := WORDSWAP(Brut);",
   c="v = (brut >> 16) | (brut << 16);",
   cpp="v = std::rotl(brut, 16);",
   essai=("DWORD", "WORDSWAP(16#12345678)", "16#56781234"), inc="Equivalent", incpp="Same", expr=True, ou="S F X T")
fn(name="SWAP", cat="utility", params=[("A", "T (une variable)", "reçoit la valeur de B", False), ("B", "T (une variable)", "reçoit la valeur de A", False)], ret="BOOL",
   short="Échange les valeurs de deux variables du même type ; rend TRUE.",
   notes=["Deux variables (ou membres, ou éléments) ; pas une expression."],
   st="SWAP(Cuve_Haute, Cuve_Basse);",
   c="tmp = a; a = b; b = tmp;",
   cpp="std::swap(a, b);",
   essai=("INT", "A * 10 + B", "21", "VAR\n    A : INT := 1;\n    B : INT := 2;\nEND_VAR\nSWAP(A, B);"), inc="Equivalent", incpp="Same", expr=False, ou="S F")
fn(name="EXCHANGE", cat="utility", params=[("X", "T (une variable)", "la variable écrite", False), ("NOUVELLE", "T", "sa nouvelle valeur", False)], ret="comme X",
   short="Écrit NOUVELLE dans X et rend l'ancienne valeur de X : un changement d'état, un front.",
   st="Ancien := EXCHANGE(Etat, Nouvel_Etat);\nIF Ancien <> Nouvel_Etat THEN Changements := Changements + 1; END_IF",
   c="ancien = etat; etat = nouvel_etat;",
   cpp="ancien = std::exchange(etat, nouvel_etat);",
   essai=("INT", "EXCHANGE(A, 7) * 10 + A", "37", "VAR\n    A : INT := 3;\nEND_VAR"), inc="Equivalent", incpp="Same", expr=False, ou="S F")


# ------------------------------------------------------------ enumerations natives --
#  Un litteral NOM#Valeur vaut son nombre (un DINT), comme une enumeration du projet ;
#  la fonction qui l'attend (colonne `fonction`, argument `rang`) recoit le mot `arg`.
#  (nom, phrase, fonction, rang, valeurs [(nom, nombre, texte, arg)] [, proprietes])
#  1.12.1 : `proprietes` - les proprietes d'objet qui l'acceptent dans leur expression fx :
#  [(genre, cle)] ; genre : la cle du genre (Kind : "Text", "Valve"...) ou "*" (tout objet
#  qui a la propriete). En marche, NOM#Valeur y devient le mot `arg` que l'objet lit deja ;
#  Compiler refuse la valeur d'une autre enumeration. `arg` : exactement un des choix de
#  l'inspecteur (l'essai le verifie objet par objet).
ENUMS = [
    ("NIVEAU_LOG", "Le niveau d'une ligne de la Console.", "IHM_LOG", 0,
     [("TRACE", 0, "Trace : le détail, pas à pas", "TRACE"), ("DEBUG", 1, "Débogage", "DEBUG"), ("INFO", 2, "Information", "INFO"),
      ("SUCCESS", 3, "Succès", "SUCCESS"), ("WARNING", 4, "Avertissement", "WARNING"), ("ERROR", 5, "Erreur", "ERROR"), ("CRITICAL", 6, "Critique", "CRITICAL")]),
    ("TRANSITION", "Le passage d'une vue à la suivante.", "IHM_NAVIGUER", 1,
     [("Instantanee", 0, "Instantanée", "Instantanée"), ("Fondu", 1, "Fondu", "Fondu"), ("Glissement", 2, "Glissement", "Glissement"),
      ("Zoom", 3, "Zoom", "Zoom"), ("Rotation", 4, "Rotation", "Rotation")],
     [("NavBar", "transition")]),
    ("POSITION_POPUP", "La place d'une popup à son ouverture.", "IHM_POPUP", 2,
     [("Centre", 0, "Au centre de la vue", "centre"), ("Objet", 1, "Sous l'objet qui l'ouvre", "objet"), ("HautGauche", 2, "En haut à gauche", "haut-gauche"),
      ("HautDroite", 3, "En haut à droite", "haut-droite"), ("BasGauche", 4, "En bas à gauche", "bas-gauche"), ("BasDroite", 5, "En bas à droite", "bas-droite"),
      ("Derniere", 6, "À sa dernière place", "derniere")]),
    ("THEME_IHM", "Le thème de l'IHM (SYS.Theme).", "IHM_THEME", 0,
     [("Jour", 0, "Jour : clair", "jour"), ("Nuit", 1, "Nuit : les couleurs de la conception", "nuit")]),
    ("SOURCE_EXPORT", "Ce qu'IHM_EXPORTER exporte (et le bouton d'export).", "IHM_EXPORTER", 0,
     [("Alarmes", 0, "Les alarmes", "alarmes"), ("Historique", 1, "L'historique des alarmes", "historique"), ("Evenements", 2, "Les événements", "événements"),
      ("Systeme", 3, "Le journal système", "système"), ("Mesures", 4, "Les mesures archivées", "mesures"), ("Audit", 5, "Le journal d'audit", "audit")],
     [("ExportButton", "exportSource")]),
    ("ONGLET_CONNEXION", "L'onglet du menu de connexion.", "IHM_MENU_CONNEXION", 0,
     [("Comptes", 0, "Comptes", "Comptes"), ("Acces", 1, "Accès", "Accès"), ("Journal", 2, "Journal", "Journal"), ("MonCompte", 3, "Mon compte", "Mon compte"),
      ("Connexion", 4, "Connexion", "Connexion")],
     [("LoginMenuButton", "tab")]),
    ("PRIORITE_ALARME", "La priorité d'une alarme : 1 la plus forte.", "", -1,
     [("Critique", 1, "Critique", "1"), ("Haute", 2, "Haute", "2"), ("Moyenne", 3, "Moyenne", "3"), ("Basse", 4, "Basse", "4")]),
    ("POLICE", "La police d'un texte.", "", -1,
     [("Sans", 0, "Sans : la police de l'interface", "Sans"), ("Mono", 1, "Mono : à chasse fixe", "Mono")],
     [("*", "font")]),
    ("ALIGNEMENT", "L'alignement d'un texte, la place d'un titre.", "", -1,
     [("Gauche", 0, "À gauche", "gauche"), ("Centre", 1, "Au centre", "centre"), ("Droite", 2, "À droite", "droite")],
     [("*", "align"), ("*", "titleAlign")]),
    ("FORME_VOYANT", "La forme d'un voyant.", "", -1,
     [("Rond", 0, "Rond", "rond"), ("Carre", 1, "Carré", "carré")],
     [("*", "shape")]),
    ("ORIENTATION", "Le sens d'une barre, d'un curseur, d'une liste de boutons.", "", -1,
     [("Horizontale", 0, "Horizontale", "horizontale"), ("Verticale", 1, "Verticale", "verticale")],
     [("*", "orientation")]),
    ("ETIREMENT", "Comment une image remplit son cadre.", "", -1,
     [("Ajuster", 0, "Ajuster : entière, ses proportions gardées", "ajuster"), ("Etirer", 1, "Étirer : tout le cadre", "étirer"), ("Aucun", 2, "Aucun : sa taille", "aucun")],
     [("*", "stretch")]),
    ("RECOLORATION", "Ce qu'une image recoloriée change.", "", -1,
     [("Tout", 0, "Tout", "tout"), ("Remplissages", 1, "Les remplissages", "remplissages"), ("Contours", 2, "Les contours", "contours"), ("UneCouleur", 3, "Une couleur", "une couleur")],
     [("*", "recolorMode")]),
    ("SENS_DEFILEMENT", "Le sens d'un texte défilant.", "", -1,
     [("Gauche", 0, "Vers la gauche", "gauche"), ("Droite", 1, "Vers la droite", "droite")],
     [("Marquee", "direction")]),
    ("CORRECTION_QR", "Le niveau de correction d'erreurs d'un code QR.", "", -1,
     [("L", 0, "L : 7 %", "L"), ("M", 1, "M : 15 %", "M"), ("Q", 2, "Q : 25 %", "Q"), ("H", 3, "H : 30 %", "H")],
     [("*", "ecLevel")]),
    ("TYPE_SAISIE", "Ce qu'un champ de saisie accepte.", "", -1,
     [("Numerique", 0, "Un nombre", "numérique"), ("Texte", 1, "Un texte", "texte"), ("MotDePasse", 2, "Un mot de passe (masqué)", "mot de passe")],
     [("InputField", "mode")]),
    ("CLAVIER_VIRTUEL", "Le clavier qui s'ouvre sur un champ.", "", -1,
     [("Aucun", 0, "Aucun", "aucun"), ("Numerique", 1, "Numérique", "numérique"), ("Complet", 2, "Complet", "complet")],
     [("*", "keyboard")]),
    ("CONFIRMATION", "Comment une commande sensible se confirme.", "", -1,
     [("Aucune", 0, "Aucune", "aucune"), ("SecondClic", 1, "Un second clic", "second clic"), ("Maintien", 2, "Un appui maintenu", "maintien")],
     [("*", "confirmMode")]),
    ("OPERATION_BOUTON", "Ce que fait un bouton lumineux sur sa variable.", "", -1,
     [("Basculer", 0, "Basculer", "basculer"), ("Impulsion", 1, "Une impulsion", "impulsion"), ("MettreA1", 2, "Mettre à 1", "mettre à 1"), ("MettreA0", 3, "Mettre à 0", "mettre à 0"), ("Aucune", 4, "Aucune", "aucune")],
     [("*", "operation")]),
    ("STYLE_SELECTEUR", "La forme d'un sélecteur.", "", -1,
     [("Rotatif", 0, "Rotatif", "rotatif"), ("Boutons", 1, "Des boutons", "boutons")],
     [("Selector", "style")]),
    ("CHAMPS_DATE", "Ce qu'un sélecteur de date et heure montre.", "", -1,
     [("DateEtHeure", 0, "La date et l'heure", "date et heure"), ("Date", 1, "La date", "date"), ("Heure", 2, "L'heure", "heure")],
     [("*", "fields")]),
    ("RESOLUTION_PLANNING", "Le pas d'un programmateur horaire.", "", -1,
     [("Heure", 0, "1 h", "1 h"), ("DemiHeure", 1, "30 min", "30 min"), ("QuartDHeure", 2, "15 min", "15 min")],
     [("*", "resolution")]),
    ("NIVEAU_ACCES", "Le niveau d'accès d'un objet (et celui d'un utilisateur, IHM_NIVEAU()).", "", -1,
     [("ToutLeMonde", 0, "Tout le monde", "0"), ("Operateur", 1, "Opérateur", "1"), ("Maintenance", 2, "Maintenance", "2"), ("Superviseur", 3, "Superviseur", "3"), ("Administrateur", 4, "Administrateur", "4")],
     [("*", "access")]),
    ("SIGNATURE", "La signature électronique d'une commande.", "", -1,
     [("Aucune", 0, "Aucune", "aucune"), ("Simple", 1, "Simple : l'utilisateur", "simple"), ("Double", 2, "Double : l'utilisateur et un second", "double")],
     [("*", "signature")]),
    ("STYLE_HORLOGE", "La forme d'une horloge.", "", -1,
     [("Analogique", 0, "Analogique : des aiguilles", "analogique"), ("Numerique", 1, "Numérique", "numérique")],
     [("*", "clockStyle")]),
    ("FORMAT_DUREE", "L'écriture d'un compteur horaire.", "", -1,
     [("HMMSS", 0, "h:mm:ss", "h:mm:ss"), ("Heures", 1, "En heures", "heures"), ("Jours", 2, "En jours", "jours")],
     [("*", "durationFormat")]),
    ("MODE_COURBE", "D'où une courbe tire ses points.", "", -1,
     [("TempsReel", 0, "Temps réel : à chaque cycle", "temps réel"), ("Historique", 1, "Historique : les mesures archivées", "historique")],
     [("Trend", "mode")]),
    ("ECHELLE", "L'axe Y d'une courbe ou d'un graphique.", "", -1,
     [("Fixe", 0, "Fixe : min et max donnés", "fixe"), ("Auto", 1, "Auto : suit les valeurs", "auto")],
     [("*", "scale")]),
    ("INTERPOLATION", "Comment une courbe relie ses points.", "", -1,
     [("Lineaire", 0, "Linéaire", "linéaire"), ("Escalier", 1, "En escalier", "escalier"), ("Points", 2, "Des points seuls", "points")],
     [("*", "interpolation")]),
    ("ETIQUETTES_PARTS", "Ce qu'un camembert écrit sur ses parts.", "", -1,
     [("Pourcentage", 0, "Le pourcentage", "pourcentage"), ("Valeur", 1, "La valeur", "valeur"), ("LesDeux", 2, "Les deux", "les deux"), ("Aucune", 3, "Rien", "aucune")],
     [("*", "labelMode")]),
    ("SOURCE_HISTORIQUE", "Ce qu'un historique montre.", "", -1,
     [("Alarmes", 0, "Les alarmes actives", "alarmes"), ("Acquittees", 1, "Les alarmes acquittées", "acquittées"), ("Historique", 2, "Les alarmes terminées", "historique"), ("Evenements", 3, "Les événements", "événements"), ("Systeme", 4, "Le journal de l'IHM", "système"), ("MisesDeCote", 5, "Les alarmes mises de côté", "mises de côté"), ("Audit", 6, "Le journal d'audit", "audit")],
     [("History", "source")]),
    ("ALARME_MONTREE", "L'alarme qu'un bandeau montre.", "", -1,
     [("PlusGrave", 0, "La plus grave", "la plus grave"), ("PlusRecente", 1, "La plus récente", "la plus récente"), ("Defilement", 2, "Toutes, en défilement", "défilement")],
     [("AlarmBanner", "show")]),
    ("ALARMES_COMPTEES", "Ce qu'un compteur d'alarmes compte.", "", -1,
     [("AAcquitter", 0, "À acquitter", "à acquitter"), ("Actives", 1, "Actives", "actives"), ("EnCours", 2, "En cours", "en cours"), ("MisesDeCote", 3, "Mises de côté", "mises de côté")],
     [("AlarmCounter", "count")]),
    ("PERIODE_STATS", "La période des statistiques d'alarmes.", "", -1,
     [("DepuisLancement", 0, "Depuis le lancement", "depuis le lancement"), ("Jour", 1, "24 h", "24 h"), ("Semaine", 2, "7 jours", "7 jours"), ("Tout", 3, "Tout l'historique", "tout l'historique")],
     [("AlarmStats", "range")]),
    ("CLASSEMENT", "Le classement des statistiques d'alarmes.", "", -1,
     [("Nombre", 0, "Par nombre", "nombre"), ("Duree", 1, "Par durée", "durée")],
     [("AlarmStats", "sort")]),
    ("FORMAT_FICHIER", "Le format d'un export.", "", -1,
     [("CSV", 0, "CSV", "CSV"), ("Excel", 1, "Excel (XLSX)", "Excel"), ("PDF", 2, "PDF", "PDF")],
     [("*", "fileFormat")]),
    ("PLACE_LIBELLE", "La place du libellé d'un symbole de synoptique.", "", -1,
     [("Dessous", 0, "Dessous", "dessous"), ("Dessus", 1, "Dessus", "dessus"), ("Aucun", 2, "Caché", "aucun")],
     [("*", "labelPosition")]),
    ("COMMANDE_VANNE", "La commande dessinée d'une vanne.", "", -1,
     [("Manuelle", 0, "Manuelle", "manuelle"), ("Motorisee", 1, "Motorisée", "motorisée"), ("Pneumatique", 2, "Pneumatique", "pneumatique"), ("Reglante", 3, "Réglante", "réglante")],
     [("*", "valveType")]),
    ("FORME_TUBE", "La forme d'une tuyauterie.", "", -1,
     [("Droit", 0, "Droit", "droit"), ("Coude", 1, "Coude", "coude"), ("Te", 2, "Té", "té"), ("Croix", 3, "Croix", "croix")],
     [("*", "pipeShape")]),
    ("MONTAGE_ISA", "Le montage d'un instrument ISA.", "", -1,
     [("Terrain", 0, "Terrain", "terrain"), ("Tableau", 1, "Tableau", "tableau"), ("TableauArriere", 2, "Tableau arrière", "tableau arrière")],
     [("*", "mounting")]),
    ("VANNE3_VARIANTE", "La variante d'une vanne 3 voies.", "", -1,
     [("Melangeuse", 0, "Mélangeuse", "mélangeuse"), ("Repartitrice", 1, "Répartitrice", "répartitrice")],
     [("*", "valve3Function")]),
    ("VANNE3_BOISSEAU", "Le boisseau d'une vanne 3 voies.", "", -1,
     [("T", 0, "En T", "T"), ("L", 1, "En L", "L")],
     [("*", "valve3Bore")]),
    ("VANNE3_COMMANDE", "Comment se commande la position d'une vanne 3 voies.", "", -1,
     [("Entier", 0, "Un entier", "entier"), ("DeuxBooleens", 1, "Deux booléens", "deux booléens")],
     [("*", "positionMode")]),
    ("STYLE_NAVIGATION", "La forme d'une barre de navigation.", "", -1,
     [("Onglets", 0, "Des onglets", "onglets"), ("Boutons", 1, "Des boutons", "boutons"), ("Liens", 2, "Des liens", "liens")],
     [("*", "tabStyle")]),
    ("CHEMIN_ARIANE", "Ce qu'un fil d'Ariane suit.", "", -1,
     [("Hierarchie", 0, "La hiérarchie des vues", "hiérarchie"), ("Historique", 1, "L'historique de navigation", "historique")],
     [("*", "trail")]),
    ("PLACE_ONGLETS", "La place des onglets d'un conteneur.", "", -1,
     [("Haut", 0, "En haut", "haut"), ("Bas", 1, "En bas", "bas")],
     [("*", "tabPosition")]),
    ("STYLE_TITRE", "Le titre d'un cadre.", "", -1,
     [("Bordure", 0, "Dans la bordure", "bordure"), ("Bandeau", 1, "Un bandeau", "bandeau")],
     [("*", "titleStyle")]),
    ("DEFILEMENT", "Le sens de défilement d'un panneau.", "", -1,
     [("Verticale", 0, "Vertical", "verticale"), ("Horizontale", 1, "Horizontal", "horizontale"), ("LesDeux", 2, "Les deux", "les deux")],
     [("*", "scrollDirection")]),
    ("STYLE_LANGUE", "La forme d'un sélecteur de langue.", "", -1,
     [("Boutons", 0, "Des boutons", "boutons"), ("Bascule", 1, "Une bascule", "bascule")],
     [("*", "languageStyle")]),
    ("LIBELLE_LANGUE", "Ce qu'un sélecteur de langue écrit.", "", -1,
     [("CodeEtNom", 0, "Le code et le nom", "code et nom"), ("Code", 1, "Le code", "code"), ("Nom", 2, "Le nom", "nom")],
     [("*", "languageLabel")]),
    ("ONGLET_PARAMETRES", "L'onglet ouvert des paramètres système.", "", -1,
     [("Reglages", 0, "Réglages", "Réglages"), ("Diagnostic", 1, "Diagnostic", "Diagnostic"), ("Simulation", 2, "Simulation", "Simulation")],
     [("SystemButton", "tab")]),
    ("LECTURE_GIF", "Comment un GIF animé se joue.", "", -1,
     [("EnBoucle", 0, "En boucle", "en boucle"), ("UneFois", 1, "Une fois", "une fois"), ("NFois", 2, "N fois", "N fois")],
     [("AnimatedGif", "play")]),
    ("DEPART_GIF", "Quand un GIF animé démarre.", "", -1,
     [("AAffichage", 0, "À l'affichage", "à l'affichage"), ("SurAction", 1, "Sur action", "sur action"), ("SurCondition", 2, "Sur condition", "sur condition")],
     [("AnimatedGif", "start")]),
    ("FIN_GIF", "Ce qu'un GIF animé montre à la fin.", "", -1,
     [("DerniereImage", 0, "La dernière image", "dernière image"), ("PremiereImage", 1, "La première image", "première image"), ("Cache", 2, "Il se cache", "caché")],
     [("AnimatedGif", "end")]),
    ("JOUR", "Le jour de la semaine, comme SYS.WeekDay (1 lundi ... 7 dimanche).", "", -1,
     [("Lundi", 1, "Lundi", "1"), ("Mardi", 2, "Mardi", "2"), ("Mercredi", 3, "Mercredi", "3"), ("Jeudi", 4, "Jeudi", "4"), ("Vendredi", 5, "Vendredi", "5"), ("Samedi", 6, "Samedi", "6"), ("Dimanche", 7, "Dimanche", "7")]),
    ("MOIS", "Le mois, comme SYS.Month (1 janvier ... 12 décembre).", "", -1,
     [("Janvier", 1, "Janvier", "1"), ("Fevrier", 2, "Février", "2"), ("Mars", 3, "Mars", "3"), ("Avril", 4, "Avril", "4"), ("Mai", 5, "Mai", "5"), ("Juin", 6, "Juin", "6"), ("Juillet", 7, "Juillet", "7"), ("Aout", 8, "Août", "8"), ("Septembre", 9, "Septembre", "9"), ("Octobre", 10, "Octobre", "10"), ("Novembre", 11, "Novembre", "11"), ("Decembre", 12, "Décembre", "12")]),
    # 1.12.7 : les enumerations des natives des conteneurs et de std::numeric (le rang est le nombre)
    ("COMPARAISON", "La comparaison des fonctions qui cherchent, comptent ou retirent sous condition (LIST_FIND_IF, LIST_COUNT_IF, LIST_REMOVE_IF, LIST_ALL…).", "", -1,
     [("Egal", 0, "Égal (=)", "Egal"), ("Different", 1, "Différent (<>)", "Different"), ("Inferieur", 2, "Inférieur (<)", "Inferieur"),
      ("InferieurOuEgal", 3, "Inférieur ou égal (<=)", "InferieurOuEgal"), ("Superieur", 4, "Supérieur (>)", "Superieur"),
      ("SuperieurOuEgal", 5, "Supérieur ou égal (>=)", "SuperieurOuEgal")]),
    ("OPERATION", "L'opération des fonctions de std::numeric (ACCUMULATE, REDUCE, PARTIAL_SUM, les SCAN…).", "", -1,
     [("Somme", 0, "La somme (+)", "Somme"), ("Produit", 1, "Le produit (*)", "Produit"), ("Min", 2, "Le minimum", "Min"), ("Max", 3, "Le maximum", "Max"),
      ("Et", 4, "Le ET bit à bit", "Et"), ("Ou", 5, "Le OU bit à bit", "Ou")]),
    ("TRANSFORMATION", "La transformation de chaque élément (TRANSFORM_REDUCE, TRANSFORM_INCLUSIVE_SCAN, TRANSFORM_EXCLUSIVE_SCAN).", "", -1,
     [("Identite", 0, "La valeur elle-même", "Identite"), ("Abs", 1, "La valeur absolue", "Abs"), ("Carre", 2, "Le carré", "Carre"),
      ("Racine", 3, "La racine carrée", "Racine"), ("Neg", 4, "L'opposé", "Neg"), ("Inverse", 5, "L'inverse (1/x)", "Inverse")]),
    ("QUALITE", "La qualité d'une valeur lue sur un équipement.", "", -1,
     [("Bonne", 0, "Bonne", "0"), ("Ancienne", 1, "Ancienne : la dernière lue, trop vieille", "1"), ("Mauvaise", 2, "Mauvaise : illisible", "2")]),
    ("ETAT_MOTEUR", "L'état d'un moteur, d'une pompe, d'un convoyeur (à donner à ses variables).", "", -1,
     [("Arret", 0, "À l'arrêt", "0"), ("Marche", 1, "En marche", "1"), ("Defaut", 2, "En défaut", "2"), ("Maintenance", 3, "En maintenance", "3")]),
    ("MODE_MARCHE", "Le mode de marche d'un équipement (à donner à ses variables).", "", -1,
     [("Auto", 0, "Automatique", "0"), ("Manuel", 1, "Manuel", "1"), ("Local", 2, "Local", "2"), ("Distant", 3, "Distant", "3")]),
    # 1.13.0 (lot J3) : le genre d'une valeur JSON, rendu par JSON_TYPE. Valeurs nues (JSON_TYPE(v) = JSON_Objet).
    ("JSON_Type", "Le genre d'une valeur JSON, rendu par JSON_TYPE(v). Les valeurs portent le préfixe JSON_ : elles s'écrivent nues (JSON_TYPE(v) = JSON_Objet).", "JSON_TYPE", -1,
     [("JSON_Objet", 0, "Un objet { }", "JSON_Objet"), ("JSON_Liste", 1, "Un tableau [ ]", "JSON_Liste"), ("JSON_Texte", 2, "Un texte", "JSON_Texte"),
      ("JSON_Nombre", 3, "Un nombre", "JSON_Nombre"), ("JSON_Bool", 4, "TRUE ou FALSE", "JSON_Bool"), ("JSON_Nul", 5, "Un null", "JSON_Nul"), ("JSON_Absent", 6, "Une clé qui manque", "JSON_Absent")]),
]

# ------------------------------------------------------------------ attente (1.13.0) --
#  WAIT_UNTIL est une native (groupe attente du moteur) ; LEVER et RELEVER sont des
#  instructions (INSTR). Note : WAIT (1.12.26) et les fonctions de fichiers ne sont pas
#  encore dans ce catalogue - leurs fiches ont ete ajoutees a la main dans HmiNativesData.cpp.
fn(name="WAIT_UNTIL", cat="script",
   params=[("CONDITION", "BOOL", "ce qu'on attend (revu à chaque reprise)", False),
           ("DELAI", "TIME, DINT", "l'attente maximale (T#2m) ; sans : indéfiniment", True)],
   ret="BOOL",
   short="Attend que CONDITION (un BOOL) devienne vraie, revue à chaque reprise de la tâche. Employé (IF, affectation), il rend TRUE devenu vrai, FALSE au délai ; seul en instruction, un délai dépassé lève E_ATTENTE. Demande un script détaché.",
   st="Vanne_Entree := TRUE;\nWAIT_UNTIL(Niveau_Cuve >= Consigne, T#2m);\nVanne_Entree := FALSE;",
   c="while (!(Niveau_Cuve >= Consigne)) WAIT(20);   /* ms ; dans un script détaché */",
   cpp="while (!(Niveau_Cuve >= Consigne)) WAIT(20ms);   // WAIT_UNTIL demande un script détaché",
   inc="Equivalent", incpp="Equivalent", expr=False,
   notes=["Dans un script non détaché, l'IHM est figée : la condition ne changera pas - coche Détaché (l'attente est plafonnée à 10 s, comme WAIT).",
          "Un délai dépassé lève E_ATTENTE quand WAIT_UNTIL est seul (une instruction) ; dans un IF ou une affectation, il rend FALSE sans erreur. Sans délai, il attend indéfiniment (IHM_SCRIPT_ARRETER l'arrête)."])
# 1.13.0 (lot J2) : le debogueur des scripts IHM. IHM_POINT_ARRET est une native du dialecte (groupe debogage du
#  moteur) ; sa fiche a ete ajoutee a la main dans HmiNativesData.cpp (comme WAIT), reportee ici pour la reconciliation.
fn(name="IHM_POINT_ARRET", cat="script",
   params=[("CONDITION", "BOOL", "s'arrêter seulement si elle est vraie ; sans : à chaque passage", True)],
   ret="BOOL",
   short="Un point d'arrêt écrit dans le code : en simulation, le script s'arrête ici et le volet Débogage montre la pile, les locales et les valeurs de la ligne ; avec un BOOL, seulement s'il est vrai. Hors simulation (le poste d'exploitation), il ne fait rien.",
   st="IHM_POINT_ARRET(Fours[f].Temperature > 900.0);   (* s'arrête au 1er four trop chaud *)",
   c="IHM_POINT_ARRET(Fours[f].Temperature > 900.0);   /* sans effet à la cible */",
   cpp="IHM_POINT_ARRET(Fours[f].Temperature > 900.0);   // sans effet à la cible",
   inc="Same", incpp="Same", expr=False, essai=("BOOL", "IHM_POINT_ARRET(FALSE)", "FALSE", "VAR x : INT; END_VAR"),
   notes=["Sans argument, il s'arrête à chaque passage : pratique dans une boucle ou dans un script détaché.",
          "Dans une expression (fx), il est refusé, comme WAIT : une expression calcule sa valeur tout de suite.",
          "Un point posé dans la marge de l'éditeur (un clic à gauche des numéros de ligne) fait la même chose sans changer le code."])

# ------------------------------------------------------------------ 1.13.0 (lot J3) : JSON --
#  Le type ANY porte une valeur JSON / dynamique. Lis le texte avec FILE_READ / RESOURCE_READ, puis JSON_LIRE.
fn(name="JSON_LIRE", cat="json", params=[("TEXTE", "STRING", "un texte JSON (d'un FILE_READ, d'une ressource…)", False)], ret="ANY",
   short="Analyse un texte JSON : un objet devient une MAP[STRING] OF ANY, un tableau une LIST OF ANY. Un texte mal formé lève E_JSON (la position citée), rattrapable par TRY…CATCH.",
   st="Recettes := JSON_LIRE(FILE_READ('recettes.json'));", c="/* un analyseur JSON : cJSON, jsmn… */", cpp="auto j = nlohmann::json::parse(texte);",
   inc="None", incpp="None", essai=("STRING", "JSON_TEXTE(JSON_LIRE('{\"gaz\":\"A\"}')['gaz'])", "A"),
   notes=["Lis le texte avec FILE_READ ou RESOURCE_READ, puis JSON_LIRE."])
fn(name="JSON_ECRIRE", cat="json", params=[("VALEUR", "ANY", "ANY, MAP, LIST, SET, ARRAY, TUPLE, une structure, un scalaire", False), ("INDENTE", "BOOL", "deux espaces par niveau (sinon compact)", True)], ret="STRING",
   short="Écrit une valeur en JSON. Une structure IHM donne un objet (ses membres) ; un REAL infini ou NAN, une référence, un rappel sont refusés.",
   st="FILE_WRITE('etat.json', JSON_ECRIRE(Etat, TRUE));", c="/* nlohmann::json ou autre : j.dump() */", cpp="std::string s = j.dump(2);",
   inc="None", incpp="None", essai=("STRING", "JSON_ECRIRE(JSON_LIRE('[1,2]'))", "[1,2]"))
fn(name="JSON_NOMBRE", cat="json", params=[("V", "ANY", "une valeur JSON", False), ("DEFAUT", "REAL", "rendu si V manque ou n'est pas un nombre", True)], ret="REAL",
   short="Le nombre porté par une valeur JSON. Avec DEFAUT : rendu si la valeur manque ou n'est pas un nombre ; sans lui, un mauvais genre lève E_JSON.",
   st="Pression := JSON_NOMBRE(Config['pression'], 9.5);", c="double p = json_number(v);", cpp="double p = v.is_number() ? v.get<double>() : 9.5;",
   inc="None", incpp="None", essai=("REAL", "JSON_NOMBRE(JSON_LIRE('7.5'))", "7.5"))
fn(name="JSON_ENTIER", cat="json", params=[("V", "ANY", "une valeur JSON", False), ("DEFAUT", "DINT", "rendu si V manque ou n'est pas un nombre", True)], ret="DINT",
   short="L'entier porté par une valeur JSON (un réel est tronqué). DEFAUT si elle manque ou n'est pas un nombre ; sans lui, E_JSON.",
   st="N := JSON_ENTIER(Config['repetitions'], 1);", c="int n = json_int(v);", cpp="int n = v.is_number() ? v.get<int>() : 1;",
   inc="None", incpp="None", essai=("DINT", "JSON_ENTIER(JSON_LIRE('42'))", "42"))
fn(name="JSON_TEXTE", cat="json", params=[("V", "ANY", "une valeur JSON", False), ("DEFAUT", "STRING", "rendu si V manque ou n'est pas un texte", True)], ret="STRING",
   short="Le texte porté par une valeur JSON. DEFAUT si elle manque ou n'est pas un texte ; sans lui, E_JSON.",
   st="Nom := JSON_TEXTE(Config['gaz'], 'inconnu');", c="const char* s = json_string(v);", cpp="std::string s = v.is_string() ? v.get<std::string>() : \"inconnu\";",
   inc="None", incpp="None", essai=("STRING", "JSON_TEXTE(JSON_LIRE('\"ok\"'))", "ok"))
fn(name="JSON_BOOL", cat="json", params=[("V", "ANY", "une valeur JSON", False), ("DEFAUT", "BOOL", "rendu si V manque ou n'est pas un booléen", True)], ret="BOOL",
   short="Le booléen porté par une valeur JSON. DEFAUT si elle manque ou n'est pas un booléen ; sans lui, E_JSON.",
   st="Actif := JSON_BOOL(Config['actif'], FALSE);", c="bool b = json_bool(v);", cpp="bool b = v.is_boolean() ? v.get<bool>() : false;",
   inc="None", incpp="None", essai=("BOOL", "JSON_BOOL(JSON_LIRE('true'))", "TRUE"))
fn(name="JSON_TYPE", cat="json", params=[("V", "ANY", "une valeur JSON", False)], ret="JSON_Type",
   short="Le genre d'une valeur JSON : JSON_Objet, JSON_Liste, JSON_Texte, JSON_Nombre, JSON_Bool, JSON_Nul, JSON_Absent (une clé qui manque).",
   st="IF JSON_TYPE(Config['seuils']) = JSON_Objet THEN ... END_IF", c="switch (json_type(v)) { /* ... */ }", cpp="if (v.is_object()) { /* ... */ }",
   inc="None", incpp="None", essai=("BOOL", "JSON_TYPE(JSON_LIRE('{}')) = JSON_Objet", "TRUE"))
fn(name="JSON_A", cat="json", params=[("OBJET", "ANY", "un objet JSON", False), ("CLE", "STRING", "la clé cherchée", False)], ret="BOOL",
   short="Vrai si l'objet JSON a cette clé.",
   st="IF JSON_A(Config, 'version') THEN ... END_IF", c="bool has = json_has(o, \"version\");", cpp="bool has = o.contains(\"version\");",
   inc="None", incpp="None", essai=("BOOL", "JSON_A(JSON_LIRE('{\"v\":1}'), 'v')", "TRUE"))
fn(name="JSON_EST_NUL", cat="json", params=[("V", "ANY", "une valeur JSON", False)], ret="BOOL",
   short="Vrai si la valeur JSON est un null (ou une clé absente).",
   st="IF JSON_EST_NUL(Config['limite']) THEN ... END_IF", c="bool n = json_is_null(v);", cpp="bool n = v.is_null();",
   inc="None", incpp="None", essai=("BOOL", "JSON_EST_NUL(JSON_LIRE('null'))", "TRUE"))
fn(name="JSON_CLES", cat="json", params=[("OBJET", "ANY", "un objet JSON", False)], ret="LIST OF STRING",
   short="Les clés d'un objet JSON, rangées : une LIST OF STRING.",
   st="FOR EACH k IN JSON_CLES(Config) DO ... END_FOR;", c="/* parcourir les cles de l'objet */", cpp="for (auto& [k, v] : o.items()) { /* ... */ }",
   inc="None", incpp="None", essai=("STRING", "JOIN(JSON_CLES(JSON_LIRE('{\"a\":1,\"b\":2}')), ',')", "a,b"))
fn(name="JSON_OBJET", cat="json", params=[], ret="ANY",
   short="Un objet JSON vide (MAP[STRING] OF ANY) à remplir avec o['clé'] := … ou MAP_SET.",
   st="VAR o : ANY; END_VAR\no := JSON_OBJET();\no['gaz'] := 'A';", c="/* json_object() */", cpp="nlohmann::json o = nlohmann::json::object();",
   inc="None", incpp="None", essai=("STRING", "JSON_ECRIRE(JSON_OBJET())", "{}"))
fn(name="JSON_LISTE", cat="json", params=[], ret="ANY",
   short="Un tableau JSON vide (LIST OF ANY) à remplir avec LIST_ADD.",
   st="VAR t : ANY; END_VAR\nt := JSON_LISTE();\nLIST_ADD(t, 1);", c="/* json_array() */", cpp="nlohmann::json t = nlohmann::json::array();",
   inc="None", incpp="None", essai=("STRING", "JSON_ECRIRE(JSON_LISTE())", "[]"))

# ------------------------------------------------------- 1.13.0 (lot J3) : expressions regulieres --
#  Grammaire ECMAScript (std::regex). En ST, $ est le caractere d'echappement : l'ancre $ s'ecrit $$.
fn(name="REGEX_TESTE", cat="regex", params=[("TEXTE", "STRING", "le texte fouillé", False), ("MOTIF", "STRING", "le motif (ECMAScript)", False)], ret="BOOL",
   short="Vrai si le MOTIF se trouve quelque part dans TEXTE. ^ et $ pour exiger tout le texte (en ST, $ s'écrit $$). Un motif invalide lève E_REGEX.",
   st="IF REGEX_TESTE(Code, '^[0-9]{13}$$') THEN ... END_IF", c="regexec(&re, s, 0, NULL, 0) == 0;", cpp="std::regex_search(s, std::regex(motif));",
   inc="None", incpp="Equivalent", essai=("BOOL", "REGEX_TESTE('PRES=7.5bar', 'PRES=[0-9.]+bar')", "TRUE"),
   notes=["Grammaire ECMAScript (comme JavaScript), sensible à la casse. \\w \\d \\s travaillent sur les octets UTF-8 : pour les accents (Hélium), écris des classes explicites.",
          "En ST, $ est le caractère d'échappement : écris l'ancre $ comme $$ (ou emploie le dialecte C / C++)."])
fn(name="REGEX_TROUVE", cat="regex", params=[("TEXTE", "STRING", "le texte fouillé", False), ("MOTIF", "STRING", "le motif (ECMAScript)", False), ("GROUPE", "DINT", "0 = tout ; 1…9 : un groupe capturé", True)], ret="STRING",
   short="La 1re correspondance (groupe 0 = tout ; 1…9 : un groupe entre parenthèses) ; '' si rien.",
   st="Pression := TO_REAL(REGEX_TROUVE(Trame, 'PRES=([0-9.]+)bar', 1));", c="regexec avec des groupes (regmatch_t).", cpp="std::smatch m; std::regex_search(s, m, re); m[1].str();",
   inc="None", incpp="Equivalent", essai=("STRING", "REGEX_TROUVE('PRES=7.5bar', 'PRES=([0-9.]+)bar', 1)", "7.5"))
fn(name="REGEX_TROUVE_TOUT", cat="regex", params=[("TEXTE", "STRING", "le texte fouillé", False), ("MOTIF", "STRING", "le motif (ECMAScript)", False)], ret="LIST OF STRING",
   short="Toutes les correspondances entières, dans l'ordre : une LIST OF STRING.",
   st="Nombres := REGEX_TROUVE_TOUT(Ligne, '[0-9]+');", c="/* boucle sur regexec */", cpp="std::sregex_iterator(s.begin(), s.end(), re);",
   inc="None", incpp="Equivalent", essai=("STRING", "JOIN(REGEX_TROUVE_TOUT('a1b22', '[0-9]+'), ',')", "1,22"))
fn(name="REGEX_GROUPES", cat="regex", params=[("TEXTE", "STRING", "le texte fouillé", False), ("MOTIF", "STRING", "le motif (ECMAScript)", False)], ret="LIST OF STRING",
   short="Les groupes capturés de la 1re correspondance (le groupe 1, puis 2…) : une LIST OF STRING.",
   st="Champs := REGEX_GROUPES(Trame, '([0-9]+):([0-9]+)');", c="/* regmatch_t[1..] */", cpp="std::smatch m; std::regex_search(s, m, re);",
   inc="None", incpp="Equivalent", essai=("STRING", "JOIN(REGEX_GROUPES('12-34', '([0-9]+)-([0-9]+)'), ',')", "12,34"))
fn(name="REGEX_REMPLACE", cat="regex", params=[("TEXTE", "STRING", "le texte", False), ("MOTIF", "STRING", "le motif (ECMAScript)", False), ("REMPLACEMENT", "STRING", "$1…$9 rappellent un groupe", False)], ret="STRING",
   short="Remplace toutes les correspondances du motif ($1…$9 rappellent un groupe capturé).",
   st="Nom := REGEX_REMPLACE(Saisie, '[^A-Za-z0-9_]', '_');", c="/* pas de remplacement standard en C */", cpp="std::regex_replace(s, re, remplacement);",
   inc="None", incpp="Equivalent", essai=("STRING", "REGEX_REMPLACE('a b', ' ', '_')", "a_b"))
fn(name="REGEX_DECOUPE", cat="regex", params=[("TEXTE", "STRING", "le texte", False), ("MOTIF", "STRING", "le motif (ECMAScript)", False)], ret="LIST OF STRING",
   short="Coupe TEXTE à chaque correspondance du MOTIF : une LIST OF STRING.",
   st="Champs := REGEX_DECOUPE(Ligne, ';');", c="/* strtok, ou une boucle */", cpp="std::sregex_token_iterator(s.begin(), s.end(), re, -1);",
   inc="None", incpp="Equivalent", essai=("STRING", "JOIN(REGEX_DECOUPE('a;b;c', ';'), '|')", "a|b|c"))
fn(name="REGEX_VALIDE", cat="regex", params=[("MOTIF", "STRING", "le motif à tester", False)], ret="BOOL",
   short="Vrai si le MOTIF est une expression régulière valide (à tester avant d'employer un motif saisi).",
   st="IF REGEX_VALIDE(Saisie) THEN ... END_IF", c="regcomp(&re, motif, REG_EXTENDED) == 0;", cpp="try { std::regex r(motif); } catch (...) { /* invalide */ }",
   inc="None", incpp="Equivalent", essai=("BOOL", "REGEX_VALIDE('[0-9]+')", "TRUE"))
# ------------------------------------------------------------------ unites de mesure (lot J6) --
#  EN / UNITE / SANS_UNITE : edite A LA MAIN dans HmiNativesData.cpp (ne PAS relancer generer.py) ;
#  ce miroir sert la reconciliation future. L'unite vit sur la declaration (le creneau), pas sur la valeur.
fn(name="EN", cat="unites", params=[("X", "REAL", "une valeur numérique qui porte une unité", False),
                                     ("UNITE", "STRING", "l'unité cible, de la même famille, entre apostrophes", False)], ret="REAL",
   short="Convertit X vers l'unité cible (même famille obligatoire) : EN(PressionBasse, 'bar') donne les mbar en bar. La température est affine (facteur ET décalage). Une valeur sans unité, ou une autre famille : refusé à Générer.",
   st="Ecart := Pression - EN(PressionBasse, 'bar');   (* bar … *)", c="/* conversion a la main : pb_bar = pb_mbar * 0.001f */", cpp="/* conversion a la main selon l'unite de la mesure */",
   inc="None", incpp="None", notes=["EN est la conversion explicite partout où une règle refuse (l'addition, la comparaison, une affectation en mode strict)."])
fn(name="UNITE", cat="unites", params=[("X", "REAL", "une valeur", False)], ret="STRING",
   short="Le symbole de l'unité de X ('bar') ; '' si X n'en porte pas. L'unité vient de la déclaration (le créneau), pas de la valeur.",
   st="Txt := UNITE(Pression);   (* 'bar' … *)", c="/* aucune : l'unite n'existe pas en C */", cpp="/* aucune : l'unite n'existe pas en C++ */",
   inc="None", incpp="None")
fn(name="SANS_UNITE", cat="unites", params=[("X", "REAL", "une valeur numérique", False)], ret="REAL",
   short="Enlève l'unité de X : le même nombre, nu (un REAL sans unité), pour un calcul libre ou une sortie brute.",
   st="Brut := SANS_UNITE(Pression);   (* un REAL nu … *)", c="Brut = Pression;   /* l'unite n'existe pas en C */", cpp="Brut = Pression;   /* l'unite n'existe pas en C++ */",
   inc="None", incpp="None", essai=("REAL", "SANS_UNITE(7.5)", "7.5"))

# ---------------------------------------------------------------- operateurs, instructions --
OPS = [
    # symbole, nom, phrase, exemple ST, C, C++, essai
    (":=", "Affectation", "Donne une valeur à une variable.", "Vitesse := 1500.0;", "Vitesse = 1500.0f;", "Vitesse = 1500.0f;", None),
    ("+ - * /", "Arithmétique", "Les quatre opérations ; entre entiers, / est la division entière (7 / 2 = 3).", "Moyenne := (A + B) / 2.0;", "Moyenne = (A + B) / 2.0f;", "Moyenne = (A + B) / 2.0f;", ("INT", "7 / 2", "3")),
    ("MOD", "Reste", "Le reste de la division entière.", "Pair := (Compteur MOD 2) = 0;", "Pair = (Compteur % 2) == 0;", "Pair = (Compteur % 2) == 0;", ("INT", "7 MOD 2", "1")),
    ("**", "Puissance", "La puissance (comme EXPT).", "Carre := X ** 2;", "Carre = powf(X, 2);", "Carre = std::pow(X, 2);", ("REAL", "2.0 ** 3", "8")),
    ("= <>", "Égal, différent", "Comparer deux valeurs du même genre.", "Arret := Etat = 0;", "Arret = Etat == 0;", "Arret = Etat == 0;", ("BOOL", "Compteur <> 7", "FALSE")),
    ("< <= > >=", "Comparaisons", "Comparer deux nombres, deux durées, deux textes.", "Alerte := Pression > 3.5;", "Alerte = Pression > 3.5f;", "Alerte = Pression > 3.5f;", ("BOOL", "Niveau >= 42.5", "TRUE")),
    ("AND OR XOR", "Logique et bits", "Sur des BOOL : et, ou, ou exclusif ; sur des mots : bit à bit.", "Pret := Marche AND NOT Defaut;", "Pret = Marche && !Defaut;   /* & | ^ sur des mots */", "Pret = Marche && !Defaut;", ("BOOL", "Marche AND NOT FALSE", "TRUE")),
    ("NOT", "Négation", "Le contraire d'un BOOL ; sur un mot, tous ses bits inversés.", "Arret := NOT Marche;", "Arret = !Marche;   /* ~Mot pour un mot */", "Arret = !Marche;", ("BOOL", "NOT Marche", "FALSE")),
    ("&", "Et (autre écriture)", "AND, écrit &.", "Pret := Marche & Ok;", "Pret = Marche && Ok;", "Pret = Marche && Ok;", ("BOOL", "Marche & (Compteur = 7)", "TRUE")),
    ("^", "Déréférence", "La variable désignée par une référence ou un pointeur.", "r^ := 0.0;", "*r = 0.0f;", "r = 0.0f;   // une référence C++", None),
    ("+= -= *= /=", "Affectation combinée (opérateurs de type)", "Un opérateur écrit pour un type IHM (Programmation générale › Types).", "Total += Mesure;", "Total += Mesure;", "Total += Mesure;", None),
    # 1.12.1
    ("? :", "Conditionnel", "c ? a : b vaut a si c est vrai, b sinon. Seul le côté choisi est lu : x <> 0 ? 100 / x : 0 ne divise jamais par zéro. Le plus faible des opérateurs ; c ? a : d ? e : f se lit de droite à gauche.",
     "Vitesse := Marche ? 1500.0 : 0.0;", "Vitesse = Marche ? 1500.0f : 0.0f;", "Vitesse = Marche ? 1500.0f : 0.0f;", ("REAL", "Compteur > 5 ? Niveau * 2.0 : 0.0", "85")),
    ("??", "Valeur de secours", "a ?? b vaut a, ou b quand a ne se lit pas : un nom inconnu, une case hors du tableau, une clé absente d'une MAP, une division par zéro, NULL. Plus fort que ? :, plus faible que OR.",
     "Texte := Recettes['B'] ?? 'aucune';", "/* pas d'équivalent : tester avant de lire */", "Texte = recettes.count(\"B\") ? recettes[\"B\"] : \"aucune\";", ("INT", "(100 / (Compteur - 7)) ?? -1", "-1")),
    ("IN [ ]", "Dans la liste", "x IN [a, b, c..d] : vrai si x vaut une des valeurs ou tombe dans une des plages (bornes comprises). Des nombres, des textes, des valeurs d'une énumération (Mode IN [Auto, Manu]).",
     "Arret := Etat IN [0, 3, 7..9];", "Arret = Etat == 0 || Etat == 3 || (Etat >= 7 && Etat <= 9);", "Arret = Etat == 0 || Etat == 3 || (Etat >= 7 && Etat <= 9);", ("BOOL", "Compteur IN [1, 3, 5..8]", "TRUE")),
    ("ENTRE … ET", "Entre deux bornes", "x ENTRE a ET b : vrai si a <= x <= b (bornes comprises). Au rang des comparaisons : Niveau ENTRE 10 ET 90 AND Marche.",
     "Normal := Pression ENTRE 0.5 ET 3.5;", "Normal = Pression >= 0.5f && Pression <= 3.5f;", "Normal = Pression >= 0.5f && Pression <= 3.5f;", ("BOOL", "Niveau ENTRE 40 ET 50", "TRUE")),
]
INSTR = [
    ("IF", "Si", "IF cond THEN ... ELSIF cond THEN ... ELSE ... END_IF", "IF Pression > 3.5 THEN\n    Alarme := TRUE;\nELSIF Pression < 0.5 THEN\n    Vide := TRUE;\nELSE\n    Alarme := FALSE;\nEND_IF",
     "if (Pression > 3.5f) {\n    Alarme = 1;\n} else if (Pression < 0.5f) {\n    Vide = 1;\n} else {\n    Alarme = 0;\n}", "if (Pression > 3.5f) {\n    Alarme = true;\n} else if (Pression < 0.5f) {\n    Vide = true;\n} else {\n    Alarme = false;\n}"),
    ("CASE", "Selon", "CASE x OF 1: ... 2, 3: ... 4..9: ... -5..-1: ... ELSE ... END_CASE ; sur un texte : 'Auto': ... ; sur une énumération : T_MODE#Auto: ...", "CASE Mode OF\n    T_MODE#Auto: Texte := 'Automatique';\n    T_MODE#Manu: Texte := 'Manuel';\nELSE\n    Texte := '?';\nEND_CASE",
     "switch (Mode) {\n    case MODE_AUTO: strcpy(Texte, \"Automatique\"); break;\n    case MODE_MANU: strcpy(Texte, \"Manuel\"); break;\n    default: strcpy(Texte, \"?\");\n}", "switch (Mode) {\n    case T_MODE::Auto: Texte = \"Automatique\"; break;\n    case T_MODE::Manu: Texte = \"Manuel\"; break;\n    default: Texte = \"?\";\n}"),
    ("FOR", "Pour", "FOR i := début TO fin BY pas DO ... END_FOR", "FOR i := 1 TO 5 DO\n    Somme := Somme + Tab[i];\nEND_FOR", "for (i = 1; i <= 5; ++i)\n    Somme += Tab[i];", "for (int i = 1; i <= 5; ++i)\n    Somme += Tab[i];"),
    ("FOR EACH", "Pour chaque", "FOR EACH v IN tableau DO ... ; FOR EACH cle, valeur IN map DO ...", "FOR EACH cle, qte IN Stock DO\n    Total := Total + qte;\nEND_FOR", "/* pas d'équivalent : une boucle sur les indices */", "for (auto& [cle, qte] : stock)\n    total += qte;"),
    ("WHILE", "Tant que", "WHILE cond DO ... END_WHILE", "WHILE i < 10 AND Tab[i] <> 0 DO\n    i := i + 1;\nEND_WHILE", "while (i < 10 && Tab[i] != 0)\n    ++i;", "while (i < 10 && Tab[i] != 0)\n    ++i;"),
    ("REPEAT", "Répéter", "REPEAT ... UNTIL cond END_REPEAT", "REPEAT\n    i := i + 1;\nUNTIL Tab[i] = 0 END_REPEAT", "do {\n    ++i;\n} while (Tab[i] != 0);", "do {\n    ++i;\n} while (Tab[i] != 0);"),
    ("EXIT", "Sortir", "Quitte la boucle en cours.", "FOR i := 1 TO 5 DO\n    IF Tab[i] = 0 THEN EXIT; END_IF\nEND_FOR", "if (Tab[i] == 0) break;", "if (Tab[i] == 0) break;"),
    # 1.12.1
    ("CONTINUE", "Continuer", "Passe au tour suivant de la boucle en cours (FOR, FOR EACH, WHILE, REPEAT).", "FOR i := 1 TO 5 DO\n    IF Tab[i] = 0 THEN CONTINUE; END_IF\n    Compteur := Compteur + 100 / Tab[i];\nEND_FOR",
     "for (i = 1; i <= 5; ++i) {\n    if (Tab[i] == 0) continue;\n    Compteur += 100 / Tab[i];\n}", "for (int i = 1; i <= 5; ++i) {\n    if (Tab[i] == 0) continue;\n    Compteur += 100 / Tab[i];\n}"),
    ("TRY", "Essayer", "TRY ... CATCH e ... END_TRY   (* une erreur du bloc va au CATCH ; e.Code, e.Message, e.Ligne, e seul = e.Message *)",
     "TRY\n    Compteur := 100 / Compteur;\nCATCH e\n    Compteur := 0;\n    IHM_LOG(NIVEAU_LOG#WARNING, '{e.Code} : {e.Message}');\nEND_TRY",
     "/* pas d'équivalent en C : tester avant */\nif (Compteur != 0) Compteur = 100 / Compteur; else Compteur = 0;",
     "try {\n    Compteur = 100 / Compteur;   // une division entière par zéro ne lève rien en C++ : tester avant\n} catch (const std::exception& e) {\n    Compteur = 0;\n}"),
    # 1.13.0 : lever ses erreurs
    ("LEVER", "Lever", "LEVER('E_Code' [, 'message'])   (* lève une erreur à soi ; un TRY la rattrape, son CATCH lit .Code et .Message *)",
     "IF Niveau < 0.0 OR Niveau > 100.0 THEN\n    LEVER('E_Niveau', 'niveau hors plage : {Niveau:0.0} %');\nEND_IF",
     "/* pas d'équivalent direct : un code de retour, errno */", "throw std::runtime_error(\"niveau hors plage\");"),
    ("RELEVER", "Relever", "RELEVER   (* dans un CATCH : réémet l'erreur attrapée vers le TRY du dessus, même code, même message *)",
     "TRY\n    Charger_Recette(Nom);\nCATCH e\n    IHM_JOURNAL('échec : {e.Message}');\n    RELEVER;\nEND_TRY",
     "/* propager le code de retour à l'appelant */", "throw;   // relance l'exception courante"),
    ("ASSERT", "Vérifier", "ASSERT(condition, 'message')   (* faux : le script s'arrête et le dit ; dans un TRY, son CATCH *)",
     "ASSERT(Niveau ENTRE 0 ET 100, 'niveau hors de 0..100');", "assert(Niveau >= 0.0f && Niveau <= 100.0f);   /* <assert.h> */", "assert(Niveau >= 0.0f && Niveau <= 100.0f);   // <cassert>"),
    ("RETURN", "Retour", "Quitte le script ou la fonction.", "IF NOT Marche THEN RETURN; END_IF", "if (!Marche) return;", "if (!Marche) return;"),
    ("VAR … END_VAR", "Déclarations", "VAR, VAR_TEMP, VAR CONSTANT ; dans une fonction aussi VAR_INPUT, VAR_IN_OUT, VAR_OUTPUT.", "VAR\n    Total : REAL;       (* gardée d'un appel à l'autre *)\nEND_VAR\nVAR_TEMP\n    i : INT;\nEND_VAR",
     "static float Total;   /* gardée */\nint16_t i;", "static float Total;   // gardée\nint16_t i;"),
]

# ------------------------------------------------------- les generiques (1.12.1) --
#  nom, ses types, ou on le lit. Ils ne se declarent pas : ils disent, dans la
#  signature d'une native, ce qu'un parametre accepte (ABS(IN : ANY_NUM)).
GENERIQUES = [
    ("ANY", "tous les types", "Un paramètre de popup ou de symbole qui accepte toute valeur ; LIMIT, SEL, MIN, MAX rendent le type de leur entrée."),
    ("ANY_ELEMENTARY", "les 22 types de base", "Une valeur simple, ni tableau, ni structure, ni MAP."),
    ("ANY_NUM", "SINT, INT, DINT, LINT, USINT, UINT, UDINT, ULINT, REAL, LREAL", "Un nombre : ABS, les comparaisons, + - * / **."),
    ("ANY_INT", "SINT, INT, DINT, LINT, USINT, UINT, UDINT, ULINT", "Un entier : MOD, les bornes d'un FOR, un indice de tableau, RANDOM_INT."),
    ("ANY_REAL", "REAL, LREAL", "Un réel : SQRT, LN, LOG, EXP, SIN, COS, TAN..."),
    ("ANY_BIT", "BOOL, BYTE, WORD, DWORD, LWORD", "Des bits : AND, OR, XOR, NOT bit à bit, SHL, SHR, ROL, ROR."),
    ("ANY_STRING", "STRING, WSTRING, CHAR", "Un texte : LEN, LEFT, RIGHT, MID, CONCAT, FIND, TO_UPPER..."),
    ("ANY_DATE", "DATE, TIME_OF_DAY, DATE_AND_TIME", "Une date ou une heure : DT_TO_DATE, DT_TO_TOD, CONCAT_DATE_TOD, les comparaisons."),
]

LITERALS = {
    "BOOL": ["TRUE", "FALSE", "BOOL#1"], "SINT": ["-12", "SINT#-12"], "INT": ["42", "-7", "INT#42", "16#FF", "2#1010", "8#17"],
    "DINT": ["100000", "DINT#-5", "16#7FFF_FFFF"], "LINT": ["LINT#5"], "USINT": ["200", "USINT#200"], "UINT": ["65535", "UINT#7"],
    "UDINT": ["4000000000", "UDINT#7"], "ULINT": ["ULINT#5"], "BYTE": ["16#FF", "BYTE#65"], "WORD": ["16#00F0", "2#0000_1111_0000_0000"],
    "DWORD": ["16#DEAD_BEEF"], "LWORD": ["16#FF"], "REAL": ["3.14", "-0.5", "1.5E3", "REAL#1"], "LREAL": ["3.141592653589793", "LREAL#1.5"],
    "STRING": ["'texte'", "''", "'l$'armoire'", "'ligne 1$Nligne 2'", "'5 $$'"], "TIME": ["T#5s", "T#1m30s", "T#250ms", "TIME#1h", "T#1d2h"],
    # 1.12.1
    "CHAR": ["'A'", "'$N'"], "WSTRING": ["'texte'", "'été'"],
    "DATE": ["D#2026-10-09", "DATE#2026-01-01"], "TIME_OF_DAY": ["TOD#14:30:00", "TOD#06:00", "TIME_OF_DAY#23:59:59.5"],
    "DATE_AND_TIME": ["DT#2026-10-09-14:30:00", "DATE_AND_TIME#2026-01-01-00:00:00"],
}

# ------------------------------------------------------------ les types, en plus du registre --
#  La plage (MIN, MAX), la taille et la famille viennent du registre des types
#  (src/hmi/HmiTypeRegistry) ; ici : C, C++, la valeur par defaut, la place Modbus
#  d'une variable IHM (src/hmi/HmiTypes : wordsOf), les notes.
TYPE_EXTRA = {
    "BOOL": {"c": "bool", "cpp": "BOOL (bool)", "def": "FALSE", "modbus": "1 bit (16 BOOL à la suite par mot), sinon 1 mot"},
    "SINT": {"c": "int8_t", "cpp": "SINT (int8_t)", "def": "0", "modbus": "—",
             "notes": ["Pas une variable IHM (il n'a pas de place Modbus) : une locale, un paramètre, un opérande."]},
    "INT": {"c": "int16_t", "cpp": "INT (int16_t)", "def": "0", "modbus": "1 mot (%MW)"},
    "DINT": {"c": "int32_t", "cpp": "DINT (int32_t)", "def": "0", "modbus": "2 mots (%MD)"},
    "LINT": {"c": "int64_t", "cpp": "LINT (int64_t)", "def": "0", "modbus": "—",
             "notes": ["L'IHM le calcule sur 32 bits, comme un DINT : au-delà, la valeur est fausse.",
                       "Pas une variable IHM : une locale, un paramètre, un opérande."]},
    "USINT": {"c": "uint8_t", "cpp": "USINT (uint8_t)", "def": "0", "modbus": "—",
              "notes": ["Pas une variable IHM (il n'a pas de place Modbus) : une locale, un paramètre, un opérande."]},
    "UINT": {"c": "uint16_t", "cpp": "UINT (uint16_t)", "def": "0", "modbus": "1 mot (%MW)"},
    "UDINT": {"c": "uint32_t", "cpp": "UDINT (uint32_t)", "def": "0", "modbus": "2 mots (%MD)"},
    "ULINT": {"c": "uint64_t", "cpp": "ULINT (uint64_t)", "def": "0", "modbus": "—",
              "notes": ["L'IHM le calcule sur 32 bits, comme un UDINT : au-delà, la valeur est fausse.",
                        "Pas une variable IHM : une locale, un paramètre, un opérande."]},
    "BYTE": {"c": "uint8_t", "cpp": "BYTE (uint8_t)", "def": "16#00", "modbus": "—",
             "notes": ["Pas une variable IHM (il n'a pas de place Modbus) : une locale, un paramètre, un opérande."]},
    "WORD": {"c": "uint16_t", "cpp": "WORD (uint16_t)", "def": "16#0000", "modbus": "1 mot (%MW)"},
    "DWORD": {"c": "uint32_t", "cpp": "DWORD (uint32_t)", "def": "16#0000_0000", "modbus": "2 mots (%MD)"},
    "LWORD": {"c": "uint64_t", "cpp": "LWORD (uint64_t)", "def": "16#0", "modbus": "—",
              "notes": ["L'IHM le calcule sur 32 bits, comme un DWORD. Un opérande d'opérateur, un paramètre."]},
    "REAL": {"c": "float", "cpp": "REAL (float)", "def": "0.0", "modbus": "2 mots (%MF)",
             "notes": ["Environ 7 chiffres significatifs sur 32 bits ; l'IHM calcule en double précision, puis écrit un REAL de 32 bits.",
                       "Le plus petit réel normalisé : 1.175494E-38."]},
    "LREAL": {"c": "double", "cpp": "LREAL (double)", "def": "0.0", "modbus": "2 mots (%MF)",
              "notes": ["Environ 15 chiffres significatifs. Sur Modbus, l'IHM l'écrit en 2 mots, comme un REAL."]},
    "STRING": {"c": "char[33]", "cpp": "STRING (std::string)", "def": "''", "modbus": "16 mots (32 caractères)",
               "notes": ["Sur Modbus, une STRING prend 16 mots : 32 caractères, deux par mot.",
                         "Échappements : $' (apostrophe), $N (ligne), $T (tabulation), $$ (dollar)."]},
    "TIME": {"c": "uint32_t   /* des ms */", "cpp": "TIME (std::chrono::milliseconds)", "def": "T#0ms", "modbus": "2 mots (%MD)",
             "notes": ["Une durée en millisecondes sur 32 bits. Pas de durée négative : T#-5s est refusé.",
                       "L'affichage du moteur compte en millisecondes (T#5000ms) ; {d:t} l'écrit lisiblement (2 min 05 s)."]},
    # 1.12.1
    "CHAR": {"c": "char", "cpp": "CHAR (char)", "def": "''", "modbus": "—",
             "notes": ["Un texte d'un caractère pour l'IHM : une STRING le reçoit ; vers CHAR, le premier caractère.",
                       "Pas une variable IHM (pas de place Modbus) : une locale, un paramètre, un opérande."]},
    "WSTRING": {"c": "wchar_t[33]", "cpp": "WSTRING (std::wstring)", "def": "''", "modbus": "—",
                "notes": ["L'IHM écrit ses textes en UTF-8 : WSTRING et STRING s'échangent sans conversion.",
                          "Pas une variable IHM (pas de place Modbus) : une locale, un paramètre, un opérande."]},
    "DATE": {"c": "time_t   /* minuit */", "cpp": "DATE (std::chrono::sys_days)", "def": "D#1970-01-01", "modbus": "—",
             "notes": ["DATE - DATE est une durée (TIME) ; DT_TO_DATE(dt) en tire la date.",
                       "Pas une variable IHM (pas de place Modbus) : une locale, un paramètre, un opérande."]},
    "TIME_OF_DAY": {"c": "uint32_t   /* ms depuis minuit */", "cpp": "TIME_OF_DAY (std::chrono::milliseconds)", "def": "TOD#00:00:00", "modbus": "—",
                    "notes": ["Son nom court : TOD. TOD + TIME tourne sur 24 h (TOD#23:00:00 + T#2h = TOD#01:00:00) ; TOD - TOD est une durée.",
                              "Pas une variable IHM (pas de place Modbus) : une locale, un paramètre, un opérande."]},
    "DATE_AND_TIME": {"c": "time_t", "cpp": "DATE_AND_TIME (std::chrono::system_clock::time_point)", "def": "DT#1970-01-01-00:00:00", "modbus": "—",
                      "notes": ["Son nom court : DT. DT + TIME, DT - TIME, DT - DT (une durée) ; MAINTENANT() la donne.",
                                "Pas une variable IHM (pas de place Modbus) : une locale, un paramètre, un opérande."]},
}

# Les types construits : (nom, exemple, ce que c'est, C, C++, ST)
CONSTRUCTED = [
    ("ARRAY", "ARRAY[1..10] OF REAL", "Un tableau, à une dimension ou plus : ARRAY[0..3, 0..9] OF REAL. Ses propriétés : Length, Rows, Columns.",
     "float tab[10];", "std::array<REAL, 10> tab;", "VAR Mesures : ARRAY[1..10] OF REAL; END_VAR"),
    ("MAP", "MAP[STRING] OF REAL", "Une table associative : une clé (STRING ou entier) donne une valeur. Se parcourt avec FOR EACH.",
     "/* pas de MAP en C */", "std::map<std::string, REAL> m;", "VAR Stock : MAP[STRING] OF REAL; END_VAR"),
    ("REF_TO", "REF_TO REAL", "Une référence : r^ lit et écrit la variable désignée par REF(x).", "float* r = &x;", "REAL& r = x;",
     "VAR r : REF_TO REAL; END_VAR"),
    ("POINTER TO", "POINTER TO INT", "Un pointeur : p^ lit et écrit la variable désignée par ADR(x).", "int16_t* p = &x;", "INT* p = &x;",
     "VAR p : POINTER TO INT; END_VAR"),
    ("MAP_ITERATOR", "MAP_ITERATOR", "Un itérateur sur une MAP : MAP_BEGIN, MAP_NEXT, MAP_END ; it.Key, it.Value.", "/* pas de MAP en C */",
     "auto it = m.begin();", "VAR it : MAP_ITERATOR; END_VAR"),
    ("STRING[n]", "STRING[20]", "Une chaîne bornée à n caractères (STRING seule : sans borne dans l'IHM, 32 caractères sur Modbus).",
     "char texte[21];", "std::string texte;   // borne à vérifier", "VAR Nom : STRING[20]; END_VAR"),
    # 1.12.2
    ("LIST OF", "LIST OF REAL", "Une liste de taille variable, indices à partir de 0 : L[0], L.Count, L.First, L.Last ; LIST_ADD, LIST_REMOVE, FOR EACH x IN L. "
     "Un littéral : [1.5, 2, 3]. Une variable IHM de ce type vit dans la mémoire de l'IHM (ni équipement, ni adresse).",
     "/* pas de liste en C */", "std::list<REAL> l;   // ou std::vector", "VAR Mesures : LIST OF REAL := [1.5, 2.0]; END_VAR"),
    ("VECTOR OF", "VECTOR OF INT", "Un vecteur de taille variable : V[0], V.Count ; VECTOR_PUSH, VECTOR_POP, VECTOR_RESIZE. Comme une liste, dans la mémoire de l'IHM.",
     "/* un tableau alloué : malloc, realloc */", "std::vector<INT> v;", "VAR Pile : VECTOR OF INT; END_VAR"),
    # 1.12.7 (choix 5 de la maquette v3)
    ("SET OF", "SET OF STRING", "Un ensemble : chaque valeur une seule fois, rangée (std::set). SET_ADD rend TRUE si elle est nouvelle ; SET_REMOVE, SET_HAS, "
     "SET_UNION, SET_INTERSECTION, SET_DIFFERENCE ; S.Count, FOR EACH x IN S (dans l'ordre). Un littéral ['P3', 'P1', 'P3'] devient ['P1', 'P3'].",
     "/* pas d'ensemble en C */", "std::set<std::string> s;", "VAR Postes_Actifs : SET OF STRING := ['P1', 'P3']; END_VAR"),
    ("RING", "RING[100] OF REAL", "Un tampon circulaire : les N dernières valeurs (N, une constante de 1 à 100000). RING_ADD (ou LIST_ADD) ajoute à la fin ; plein, "
     "la plus ancienne part. R.Count, R.Capacity, R.Full ; LIST_AVG, LIST_MIN, LIST_MAX le lisent ; FOR EACH de la plus ancienne à la plus récente.",
     "/* un tableau et un indice qui tourne */", "std::deque<REAL> r;   // bornée à la main, ou boost::circular_buffer<REAL> r(100);",
     "VAR Dernieres : RING[100] OF REAL; END_VAR"),
    ("TUPLE", "TUPLE(STRING, REAL)", "Des valeurs de types fixes, dans l'ordre : t.Item1, t.Item2... Un littéral : ('four', 850.0). Sans plan mémoire : "
     "une variable IHM de ce type vit dans l'IHM (pour un équipement, une structure IHM).",
     "struct { char nom[33]; float valeur; } t;", "std::tuple<STRING, REAL> t;", "VAR Mesure : TUPLE(STRING, REAL) := ('four', 850.0); END_VAR"),
    # 1.13.0 : l'erreur attrapee par CATCH e (produite par le moteur ou un LEVER)
    ("T_Erreur", "T_Erreur", "L'erreur attrapée par CATCH e : e.Code (le code, STRING), e.Message (le message, STRING), e.Ligne (la ligne, DINT) ; "
     "e seul se lit comme e.Message. En lecture seule, le temps du CATCH ; produite par une erreur du moteur (E_DIVISION, E_CLE...) ou un LEVER.",
     "struct { const char* Code; const char* Message; int32_t Ligne; } e;", "struct T_Erreur { std::string Code, Message; DINT Ligne; };",
     "TRY\n    Debit := Mesures['debit'];\nCATCH e\n    IHM_LOG(WARNING, '{e.Code} (ligne {e.Ligne}) : {e.Message}');\nEND_TRY"),
]
