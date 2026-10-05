/* ===========================================================================
 *  xpgliste.c - la liste des bibliotheques de l'installateur de XPGAnalyser
 * ---------------------------------------------------------------------------
 *  Une vraie ListView de Windows (SysListView32, vue Details, groupes par
 *  categorie, style de l'Explorateur) posee sur une page de l'assistant
 *  d'Inno Setup, qui n'en a pas. On y voit la bibliotheque livree, et on y
 *  AJOUTE ses propres fichiers (.dfb, .ddt, .mac) : en les glissant depuis
 *  l'Explorateur (un dossier aussi), ou par les boutons de la page.
 *
 *  Appelee par installateur\XPGAnalyser.iss (external '...@files:xpgliste.dll').
 *  Setup.exe d'Inno Setup est un programme 32 bits : cette DLL aussi.
 *    i686-w64-mingw32-gcc -O2 -shared -municode -Wl,--kill-at -static-libgcc -o xpgliste.dll xpgliste.c
 *      -lcomctl32 -lshell32 -luser32 -lgdi32 -s
 *    (ou : cl /LD /O2 xpgliste.c xpgliste.def comctl32.lib shell32.lib user32.lib gdi32.lib
 *     dans l'invite x86 des Build Tools : le .def garde les noms sans decoration)
 *  La DLL fabriquee est livree a cote (installateur\aide\xpgliste.dll) : la chaine de
 *  compilation n'a pas a la refaire.
 * =========================================================================== */
#ifndef UNICODE
#define UNICODE
#endif
#ifndef _UNICODE
#define _UNICODE
#endif
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <commctrl.h>
#include <shellapi.h>
#include <string.h>
#include <wchar.h>

#define EXPORT __declspec(dllexport)

#define MAX_LIGNES   2000
#define MAX_GROUPES  64
#define GROUPE_AJOUT 1000

typedef struct {
    wchar_t chemin[MAX_PATH];    /* vide pour une ligne livree */
    wchar_t categorie[64];
    wchar_t nom[128];
    int     livree;
} Ligne;

static HWND    g_liste = NULL;
static WNDPROC g_procOrigine = NULL;
static Ligne   g_lignes[MAX_LIGNES];
static int     g_nombre = 0;
static wchar_t g_groupes[MAX_GROUPES][64];
static int     g_nombreGroupes = 0;
static int     g_groupesActifs = 0;
static wchar_t g_categoriesConnues[MAX_GROUPES][64];
static int     g_nombreConnues = 0;
static HWND    g_notifier = NULL;       /* la fenetre qui recoit WM_APP+1 apres un depot */

static int groupeDe(const wchar_t* categorie, int ajoutee) {
    int i;
    if (ajoutee) return GROUPE_AJOUT;
    for (i = 0; i < g_nombreGroupes; ++i)
        if (_wcsicmp(g_groupes[i], categorie) == 0) return i + 1;
    if (g_nombreGroupes >= MAX_GROUPES) return 0;
    wcsncpy(g_groupes[g_nombreGroupes], categorie, 63);
    g_groupes[g_nombreGroupes][63] = 0;
    ++g_nombreGroupes;
    if (g_groupesActifs) {
        LVGROUP g;
        ZeroMemory(&g, sizeof(g));
        g.cbSize = sizeof(g);
        g.mask = LVGF_HEADER | LVGF_GROUPID;
        g.pszHeader = g_groupes[g_nombreGroupes - 1];
        g.iGroupId = g_nombreGroupes;
        SendMessageW(g_liste, LVM_INSERTGROUP, (WPARAM)-1, (LPARAM)&g);
    }
    return g_nombreGroupes;
}

static void colonne(int i, const wchar_t* titre, int largeur) {
    LVCOLUMNW c;
    ZeroMemory(&c, sizeof(c));
    c.mask = LVCF_TEXT | LVCF_WIDTH | LVCF_SUBITEM;
    c.pszText = (LPWSTR)titre;
    c.cx = largeur;
    c.iSubItem = i;
    SendMessageW(g_liste, LVM_INSERTCOLUMNW, (WPARAM)i, (LPARAM)&c);
}

static void texte(int ligne, int col, const wchar_t* t) {
    LVITEMW it;
    ZeroMemory(&it, sizeof(it));
    it.iSubItem = col;
    it.pszText = (LPWSTR)t;
    SendMessageW(g_liste, LVM_SETITEMTEXTW, (WPARAM)ligne, (LPARAM)&it);
}

/* Une ligne de plus ; rend son rang, -1 si la liste est pleine ou si le fichier y est deja. */
static int inserer(const wchar_t* nom, const wchar_t* categorie, const wchar_t* type, const wchar_t* version,
                   const wchar_t* origine, const wchar_t* chemin, int livree) {
    int i, rang;
    LVITEMW it;
    if (!g_liste || g_nombre >= MAX_LIGNES) return -1;
    for (i = 0; i < g_nombre; ++i) {
        if (!livree && !g_lignes[i].livree && _wcsicmp(g_lignes[i].chemin, chemin) == 0) return -1;
    }
    ZeroMemory(&it, sizeof(it));
    it.mask = LVIF_TEXT | LVIF_PARAM;
    it.iItem = g_nombre;
    it.pszText = (LPWSTR)nom;
    it.lParam = g_nombre;
    if (g_groupesActifs) {
        it.mask |= LVIF_GROUPID;
        it.iGroupId = groupeDe(categorie, !livree);
    } else {
        groupeDe(categorie, !livree);
    }
    rang = (int)SendMessageW(g_liste, LVM_INSERTITEMW, 0, (LPARAM)&it);
    if (rang < 0) return -1;
    texte(rang, 1, categorie);
    texte(rang, 2, type);
    texte(rang, 3, version);
    texte(rang, 4, origine);
    wcsncpy(g_lignes[g_nombre].nom, nom, 127);
    g_lignes[g_nombre].nom[127] = 0;
    wcsncpy(g_lignes[g_nombre].categorie, categorie, 63);
    g_lignes[g_nombre].categorie[63] = 0;
    wcsncpy(g_lignes[g_nombre].chemin, chemin ? chemin : L"", MAX_PATH - 1);
    g_lignes[g_nombre].chemin[MAX_PATH - 1] = 0;
    g_lignes[g_nombre].livree = livree;
    ++g_nombre;
    return rang;
}

/* Un fichier de bibliotheque ? .dfb, .ddt, .mac (rend le type affiche, ou NULL). */
static const wchar_t* typeDe(const wchar_t* chemin) {
    const wchar_t* point = wcsrchr(chemin, L'.');
    if (!point) return NULL;
    if (_wcsicmp(point, L".dfb") == 0) return L"DFB";
    if (_wcsicmp(point, L".ddt") == 0) return L"DDT";
    if (_wcsicmp(point, L".mac") == 0) return L"Macro";
    return NULL;
}

/* La categorie d'un fichier ajoute : celle de la bibliotheque dont son dossier porte le
   nom (Alarms, IO...), Macros pour une macro, sinon Perso. */
static void categorieDe(const wchar_t* chemin, const wchar_t* type, wchar_t* sortie, int taille) {
    wchar_t dossier[MAX_PATH];
    wchar_t* fin;
    wchar_t* nom;
    int i;
    if (wcscmp(type, L"Macro") == 0) { wcsncpy(sortie, L"Macros", (size_t)taille - 1); sortie[taille - 1] = 0; return; }
    wcsncpy(dossier, chemin, MAX_PATH - 1);
    dossier[MAX_PATH - 1] = 0;
    fin = wcsrchr(dossier, L'\\');
    if (fin) *fin = 0;
    nom = wcsrchr(dossier, L'\\');
    nom = nom ? nom + 1 : dossier;
    for (i = 0; i < g_nombreConnues; ++i) {
        if (_wcsicmp(g_categoriesConnues[i], nom) == 0) {
            wcsncpy(sortie, g_categoriesConnues[i], (size_t)taille - 1);
            sortie[taille - 1] = 0;
            return;
        }
    }
    wcsncpy(sortie, L"Perso", (size_t)taille - 1);
    sortie[taille - 1] = 0;
}

/* L'en-tete d'un bloc : "name = X", "version = 1.03" (les .dfb et .ddt de la bibliotheque) ;
   ASCII / UTF-8, les 4 premiers Ko. Rend 1 si le nom est trouve. */
static void valeurEntete(const char* texte, const char* cle, wchar_t* sortie, int taille) {
    const char* p = texte;
    size_t lc = strlen(cle);
    sortie[0] = 0;
    while (p && *p) {
        const char* debut = p;
        const char* fin = strchr(p, '\n');
        while (*debut == ' ' || *debut == '\t') ++debut;
        if (_strnicmp(debut, cle, lc) == 0) {
            const char* v = debut + lc;
            while (*v == ' ' || *v == '\t') ++v;
            if (*v == '=') {
                const char* e;
                int n;
                ++v;
                while (*v == ' ' || *v == '\t') ++v;
                e = fin ? fin : v + strlen(v);
                while (e > v && (e[-1] == '\r' || e[-1] == ' ' || e[-1] == '\t')) --e;
                n = MultiByteToWideChar(CP_UTF8, 0, v, (int)(e - v), sortie, taille - 1);
                sortie[n > 0 ? n : 0] = 0;
                return;
            }
        }
        p = fin ? fin + 1 : NULL;
    }
}

static void lireEntete(const wchar_t* chemin, wchar_t* nom, int tn, wchar_t* version, int tv) {
    char tampon[4097];
    DWORD lus = 0;
    HANDLE f = CreateFileW(chemin, GENERIC_READ, FILE_SHARE_READ | FILE_SHARE_WRITE, NULL, OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, NULL);
    nom[0] = 0;
    version[0] = 0;
    if (f == INVALID_HANDLE_VALUE) return;
    if (!ReadFile(f, tampon, 4096, &lus, NULL)) lus = 0;
    CloseHandle(f);
    tampon[lus] = 0;
    valeurEntete(tampon, "name", nom, tn);
    valeurEntete(tampon, "version", version, tv);
}

/* Un bloc livre de ce nom (sans la casse) ? */
static int estLivree(const wchar_t* nom) {
    int i;
    for (i = 0; i < g_nombre; ++i)
        if (g_lignes[i].livree && _wcsicmp(g_lignes[i].nom, nom) == 0) return 1;
    return 0;
}

static int ajouterUnFichier(const wchar_t* chemin) {
    const wchar_t* type = typeDe(chemin);
    const wchar_t* base;
    wchar_t nom[128], categorie[64], origine[MAX_PATH + 64], entete[128], version[32];
    wchar_t* point;
    if (!type) return 0;
    base = wcsrchr(chemin, L'\\');
    base = base ? base + 1 : chemin;
    wcsncpy(nom, base, 127);
    nom[127] = 0;
    point = wcsrchr(nom, L'.');
    if (point) *point = 0;
    /* Le nom et la version ecrits DANS le fichier (le nom du fichier peut differer). */
    lireEntete(chemin, entete, 128, version, 32);
    if (entete[0]) {
        wcsncpy(nom, entete, 127);
        nom[127] = 0;
    }
    categorieDe(chemin, type, categorie, 64);
    if (estLivree(nom))
        _snwprintf(origine, MAX_PATH + 63, L"à toi, remplace le bloc livré (rangé à côté) : %ls", chemin);
    else
        _snwprintf(origine, MAX_PATH + 63, L"à toi : %ls", chemin);
    origine[MAX_PATH + 63] = 0;
    return inserer(nom, categorie, type, version, origine, chemin, 0) >= 0 ? 1 : 0;
}

static int ajouterUnDossier(const wchar_t* dossier, int profondeur) {
    wchar_t motif[MAX_PATH];
    WIN32_FIND_DATAW d;
    HANDLE h;
    int n = 0;
    if (profondeur > 8) return 0;
    _snwprintf(motif, MAX_PATH - 1, L"%ls\\*", dossier);
    motif[MAX_PATH - 1] = 0;
    h = FindFirstFileW(motif, &d);
    if (h == INVALID_HANDLE_VALUE) return 0;
    do {
        wchar_t chemin[MAX_PATH];
        if (wcscmp(d.cFileName, L".") == 0 || wcscmp(d.cFileName, L"..") == 0) continue;
        _snwprintf(chemin, MAX_PATH - 1, L"%ls\\%ls", dossier, d.cFileName);
        chemin[MAX_PATH - 1] = 0;
        if (d.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY) n += ajouterUnDossier(chemin, profondeur + 1);
        else n += ajouterUnFichier(chemin);
    } while (FindNextFileW(h, &d));
    FindClose(h);
    return n;
}

/* Un fichier ou un dossier (tout ce qu'il contient) ; rend le nombre de lignes ajoutees. */
EXPORT int __stdcall ListeAjouterChemin(const wchar_t* chemin) {
    DWORD a;
    if (!chemin || !*chemin) return 0;
    a = GetFileAttributesW(chemin);
    if (a == INVALID_FILE_ATTRIBUTES) return 0;
    if (a & FILE_ATTRIBUTE_DIRECTORY) return ajouterUnDossier(chemin, 0);
    return ajouterUnFichier(chemin);
}

/* Les fichiers glisses depuis l'Explorateur (un dossier aussi). */
static LRESULT CALLBACK procListe(HWND h, UINT msg, WPARAM wp, LPARAM lp) {
    if (msg == WM_DROPFILES) {
        HDROP depot = (HDROP)wp;
        UINT i, n = DragQueryFileW(depot, 0xFFFFFFFF, NULL, 0);
        int ajoutees = 0;
        for (i = 0; i < n; ++i) {
            wchar_t chemin[MAX_PATH];
            if (DragQueryFileW(depot, i, chemin, MAX_PATH)) ajoutees += ListeAjouterChemin(chemin);
        }
        DragFinish(depot);
        if (g_notifier) PostMessageW(g_notifier, WM_APP + 1, (WPARAM)ajoutees, 0);
        return 0;
    }
    return CallWindowProcW(g_procOrigine, h, msg, wp, lp);
}

typedef BOOL(WINAPI* FnFiltre)(HWND, UINT, DWORD, void*);
typedef HRESULT(WINAPI* FnTheme)(HWND, LPCWSTR, LPCWSTR);

/* La liste, enfant de `parent` (la page de l'assistant), aux coordonnees donnees. */
EXPORT int __stdcall ListeCreer(HWND parent, int x, int y, int largeur, int hauteur, HFONT police, HWND notifier) {
    INITCOMMONCONTROLSEX icc;
    HMODULE m;
    icc.dwSize = sizeof(icc);
    icc.dwICC = ICC_LISTVIEW_CLASSES;
    InitCommonControlsEx(&icc);
    if (g_liste) return 1;
    g_liste = CreateWindowExW(WS_EX_CLIENTEDGE, WC_LISTVIEWW, L"",
                              WS_CHILD | WS_VISIBLE | WS_TABSTOP | LVS_REPORT | LVS_SHOWSELALWAYS,
                              x, y, largeur, hauteur, parent, NULL, GetModuleHandleW(NULL), NULL);
    if (!g_liste) return 0;
    g_notifier = notifier;
    if (police) SendMessageW(g_liste, WM_SETFONT, (WPARAM)police, TRUE);
    SendMessageW(g_liste, LVM_SETEXTENDEDLISTVIEWSTYLE, 0,
                 LVS_EX_FULLROWSELECT | LVS_EX_DOUBLEBUFFER | LVS_EX_LABELTIP | LVS_EX_INFOTIP);
    /* Le style de l'Explorateur (selection, en-tetes), s'il est la. */
    m = LoadLibraryW(L"uxtheme.dll");
    if (m) {
        FnTheme theme = (FnTheme)(void*)GetProcAddress(m, "SetWindowTheme");
        if (theme) theme(g_liste, L"Explorer", NULL);
    }
    colonne(0, L"Nom", largeur * 30 / 100);
    colonne(1, L"Catégorie", largeur * 15 / 100);
    colonne(2, L"Type", largeur * 10 / 100);
    colonne(3, L"Version", largeur * 10 / 100);
    colonne(4, L"Origine", largeur * 35 / 100 - 24);
    g_groupesActifs = (SendMessageW(g_liste, LVM_ENABLEGROUPVIEW, TRUE, 0) >= 0) ? 1 : 0;
    if (g_groupesActifs) {
        LVGROUP g;
        ZeroMemory(&g, sizeof(g));
        g.cbSize = sizeof(g);
        g.mask = LVGF_HEADER | LVGF_GROUPID;
        g.pszHeader = (LPWSTR)L"Tes ajouts (copiés dans ta bibliothèque à l'installation)";
        g.iGroupId = GROUPE_AJOUT;
        SendMessageW(g_liste, LVM_INSERTGROUP, (WPARAM)0, (LPARAM)&g);
    }
    /* Glisser-deposer : aussi quand l'assistant tourne avec les droits d'administrateur
       (UIPI filtre sinon les messages venus de l'Explorateur). */
    m = GetModuleHandleW(L"user32.dll");
    if (m) {
        FnFiltre filtre = (FnFiltre)(void*)GetProcAddress(m, "ChangeWindowMessageFilterEx");
        if (filtre) {
            filtre(g_liste, WM_DROPFILES, 1 /* MSGFLT_ALLOW */, NULL);
            filtre(g_liste, WM_COPYDATA, 1, NULL);
            filtre(g_liste, 0x0049 /* WM_COPYGLOBALDATA */, 1, NULL);
        }
    }
    DragAcceptFiles(g_liste, TRUE);
    g_procOrigine = (WNDPROC)SetWindowLongPtrW(g_liste, GWLP_WNDPROC, (LONG_PTR)procListe);
    return 1;
}

/* Une categorie de la bibliotheque livree : un fichier ajoute dans un dossier de ce nom y va. */
EXPORT void __stdcall ListeCategorieConnue(const wchar_t* categorie) {
    if (g_nombreConnues >= MAX_GROUPES || !categorie) return;
    wcsncpy(g_categoriesConnues[g_nombreConnues], categorie, 63);
    g_categoriesConnues[g_nombreConnues][63] = 0;
    ++g_nombreConnues;
}

/* Une ligne de la bibliotheque livree. */
EXPORT int __stdcall ListeAjouterLivree(const wchar_t* nom, const wchar_t* categorie, const wchar_t* type,
                                        const wchar_t* version, const wchar_t* description) {
    return inserer(nom, categorie, type, version, description, L"", 1) >= 0 ? 1 : 0;
}

/* Retire les lignes ajoutees qui sont selectionnees (les livrees restent) ; rend le nombre retire. */
EXPORT int __stdcall ListeRetirerSelection(void) {
    int i, retirees = 0;
    if (!g_liste) return 0;
    for (i = (int)SendMessageW(g_liste, LVM_GETITEMCOUNT, 0, 0) - 1; i >= 0; --i) {
        LVITEMW it;
        if (!(SendMessageW(g_liste, LVM_GETITEMSTATE, (WPARAM)i, LVIS_SELECTED) & LVIS_SELECTED)) continue;
        ZeroMemory(&it, sizeof(it));
        it.mask = LVIF_PARAM;
        it.iItem = i;
        if (!SendMessageW(g_liste, LVM_GETITEMW, 0, (LPARAM)&it)) continue;
        if (it.lParam < 0 || it.lParam >= g_nombre || g_lignes[it.lParam].livree) continue;
        g_lignes[it.lParam].chemin[0] = 0;          /* la ligne ne compte plus */
        SendMessageW(g_liste, LVM_DELETEITEM, (WPARAM)i, 0);
        ++retirees;
    }
    return retirees;
}

/* Le nombre de lignes selectionnees qui sont livrees (pour dire qu'elles restent). */
EXPORT int __stdcall ListeSelectionLivrees(void) {
    int i, n = 0;
    if (!g_liste) return 0;
    for (i = (int)SendMessageW(g_liste, LVM_GETITEMCOUNT, 0, 0) - 1; i >= 0; --i) {
        LVITEMW it;
        if (!(SendMessageW(g_liste, LVM_GETITEMSTATE, (WPARAM)i, LVIS_SELECTED) & LVIS_SELECTED)) continue;
        ZeroMemory(&it, sizeof(it));
        it.mask = LVIF_PARAM;
        it.iItem = i;
        if (SendMessageW(g_liste, LVM_GETITEMW, 0, (LPARAM)&it) && it.lParam >= 0 && it.lParam < g_nombre && g_lignes[it.lParam].livree) ++n;
    }
    return n;
}

EXPORT int __stdcall ListeNombreLivrees(void) {
    int i, n = 0;
    for (i = 0; i < g_nombre; ++i) if (g_lignes[i].livree) ++n;
    return n;
}

EXPORT int __stdcall ListeNombreAjoutees(void) {
    int i, n = 0;
    for (i = 0; i < g_nombre; ++i) if (!g_lignes[i].livree && g_lignes[i].chemin[0]) ++n;
    return n;
}

/* La n-ieme ligne ajoutee (0...) : "chemin<TAB>categorie" dans `tampon` ; rend sa longueur, 0 sinon. */
EXPORT int __stdcall ListeAjoutee(int n, wchar_t* tampon, int taille) {
    int i, k = 0;
    if (!tampon || taille <= 0) return 0;
    tampon[0] = 0;
    for (i = 0; i < g_nombre; ++i) {
        if (g_lignes[i].livree || !g_lignes[i].chemin[0]) continue;
        if (k++ == n) {
            _snwprintf(tampon, (size_t)taille - 1, L"%ls\t%ls", g_lignes[i].chemin, g_lignes[i].categorie);
            tampon[taille - 1] = 0;
            return (int)wcslen(tampon);
        }
    }
    return 0;
}

EXPORT void __stdcall ListeDetruire(void) {
    if (g_liste) {
        DragAcceptFiles(g_liste, FALSE);
        if (g_procOrigine) SetWindowLongPtrW(g_liste, GWLP_WNDPROC, (LONG_PTR)g_procOrigine);
        DestroyWindow(g_liste);
    }
    g_liste = NULL;
    g_procOrigine = NULL;
}

BOOL WINAPI DllMain(HINSTANCE h, DWORD raison, LPVOID r) {
    (void)h; (void)r;
    if (raison == DLL_PROCESS_DETACH) ListeDetruire();
    return TRUE;
}
