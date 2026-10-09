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
    ("bits", "Bits et décalages", "Décaler et faire tourner les bits d'un mot.", "Fonctions standard"),
    ("conv", "Conversions", "Passer d'un type à l'autre : TO_xxx et X_TO_Y.", "Fonctions standard"),
    ("tab", "Tableaux", "Taille et bornes d'un tableau.", "Le dialecte de l'IHM"),
    ("map", "MAP et itérateurs", "Les tables associatives (clé → valeur) et leur parcours.", "Le dialecte de l'IHM"),
    ("ref", "Références et pointeurs", "Désigner une variable sans la copier.", "Le dialecte de l'IHM"),
    ("nav", "Navigation et vues", "Changer de vue, revenir, l'accueil.", "Fonctions IHM_"),
    ("popup", "Popups", "Ouvrir, remplacer, centrer, fermer une popup.", "Fonctions IHM_"),
    ("journal", "Journal et Console", "Écrire une ligne dans le journal ou la Console.", "Fonctions IHM_"),
    ("script", "Scripts et temps", "Appeler un script, le temps de l'IHM.", "Fonctions IHM_"),
    ("user", "Utilisateurs", "Qui est connecté, son groupe, son niveau ; déconnecter.", "Fonctions IHM_"),
    ("alarme", "Alarmes", "Mettre de côté, remettre, faire taire.", "Fonctions IHM_"),
    ("affichage", "Affichage et son", "Langue, thème, paramètres système, sons.", "Fonctions IHM_"),
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
   notes=["m['cle'] en lecture crée la clé ; MAP_GET ne crée rien."], st="Qte := MAP_GET(Stock, 'vis', 0.0);",
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

# ---------------------------------------------------------------------- references --
fn(name="REF", cat="ref", params=[("X", "une variable", "pas une valeur calculée", False)], ret="REF_TO type de X",
   short="Une référence vers une variable : l'écrire par la référence écrit la variable.",
   st="VAR r : REF_TO REAL; END_VAR\nr := REF(Niveau);\nr^ := 50.0;   (* Niveau vaut 50 *)", c="float* r = &Niveau;\n*r = 50.0f;",
   cpp="float& r = Niveau;\nr = 50.0f;", essai=None, plc="REF (EF)")
fn(name="ADR", cat="ref", params=[("X", "une variable", "pas une valeur calculée", False)], ret="POINTER TO type de X",
   short="Un pointeur vers une variable (comme REF, pour POINTER TO).", st="VAR p : POINTER TO INT; END_VAR\np := ADR(Compteur);\np^ := p^ + 1;",
   c="int16_t* p = &Compteur;\n*p += 1;", cpp="auto* p = &Compteur;\n*p += 1;", essai=None, plc="ADR (EF)")

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
    ("QUALITE", "La qualité d'une valeur lue sur un équipement.", "", -1,
     [("Bonne", 0, "Bonne", "0"), ("Ancienne", 1, "Ancienne : la dernière lue, trop vieille", "1"), ("Mauvaise", 2, "Mauvaise : illisible", "2")]),
    ("ETAT_MOTEUR", "L'état d'un moteur, d'une pompe, d'un convoyeur (à donner à ses variables).", "", -1,
     [("Arret", 0, "À l'arrêt", "0"), ("Marche", 1, "En marche", "1"), ("Defaut", 2, "En défaut", "2"), ("Maintenance", 3, "En maintenance", "3")]),
    ("MODE_MARCHE", "Le mode de marche d'un équipement (à donner à ses variables).", "", -1,
     [("Auto", 0, "Automatique", "0"), ("Manuel", 1, "Manuel", "1"), ("Local", 2, "Local", "2"), ("Distant", 3, "Distant", "3")]),
]

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
    ("TRY", "Essayer", "TRY ... CATCH Erreur ... END_TRY   (* une erreur du bloc va au CATCH, son message dans Erreur *)",
     "TRY\n    Compteur := 100 / Compteur;\nCATCH Texte\n    Compteur := 0;\n    IHM_LOG(NIVEAU_LOG#WARNING, Texte);\nEND_TRY",
     "/* pas d'équivalent en C : tester avant */\nif (Compteur != 0) Compteur = 100 / Compteur; else Compteur = 0;",
     "try {\n    Compteur = 100 / Compteur;   // une division entière par zéro ne lève rien en C++ : tester avant\n} catch (const std::exception& e) {\n    Compteur = 0;\n}"),
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
]
