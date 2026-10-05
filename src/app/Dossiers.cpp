// =============================================================================
//  app/Dossiers.cpp - 1.8.0 : les dossiers de l'application (voir Dossiers.hpp)
// =============================================================================
#include "Dossiers.hpp"

#include <algorithm>
#include <cctype>
#include <chrono>
#include <cstdio>
#include <cstdlib>
#include <fstream>
#include <system_error>

#if defined(_WIN32)
#  ifndef NOMINMAX
#    define NOMINMAX
#  endif
#  ifndef WIN32_LEAN_AND_MEAN
#    define WIN32_LEAN_AND_MEAN
#  endif
#  include <windows.h>
#  include <shellapi.h>
#  include <shlobj.h>
#  if defined(_MSC_VER)
#    pragma comment(lib, "shell32.lib")
#    pragma comment(lib, "ole32.lib")
#    pragma comment(lib, "uuid.lib")   // les GUID FOLDERID_* (MinGW : libuuid, deja lie)
#  endif
#endif

namespace app::dossiers {

namespace fs = std::filesystem;

namespace {

std::size_t indice(Cle c) noexcept { return static_cast<std::size_t>(c); }

std::string trim(std::string_view s) {
    std::size_t a = 0, b = s.size();
    while (a < b && (s[a] == ' ' || s[a] == '\t' || s[a] == '\r' || s[a] == '\n')) ++a;
    while (b > a && (s[b - 1] == ' ' || s[b - 1] == '\t' || s[b - 1] == '\r' || s[b - 1] == '\n')) --b;
    return std::string(s.substr(a, b - a));
}

std::string minuscules(std::string_view s) {
    std::string out(s);
    for (auto& ch : out) ch = static_cast<char>(std::tolower(static_cast<unsigned char>(ch)));
    return out;
}

bool egalSansCasse(std::string_view a, std::string_view b) { return minuscules(a) == minuscules(b); }

// Une valeur entre guillemets (un chemin avec des espaces, ecrit a la main) : sans eux.
std::string sansGuillemets(std::string v) {
    if (v.size() >= 2 && v.front() == '"' && v.back() == '"') v = v.substr(1, v.size() - 2);
    return v;
}

// "[Dossiers]" -> "Dossiers" ; pas une section -> nullopt.
std::optional<std::string> nomSection(std::string_view ligne) {
    const auto t = trim(ligne);
    if (t.size() >= 2 && t.front() == '[' && t.back() == ']') return trim(std::string_view(t).substr(1, t.size() - 2));
    return std::nullopt;
}

bool commentaire(std::string_view t) { return t.empty() || t.front() == ';' || t.front() == '#'; }

std::string joindre(const std::string& base, std::string_view sous) {
    if (sous.empty()) return base;
    return utf8De((cheminDe(base) / cheminDe(sous)).lexically_normal());
}

// Deux chemins designent-ils le meme dossier ? (normalises ; sans la casse sous Windows)
std::string cleDeComparaison(const std::string& utf8) {
    std::error_code ec;
    fs::path p = cheminDe(utf8);
    if (p.is_relative()) p = fs::absolute(p, ec);
    std::string s = utf8De(p.lexically_normal());
    while (s.size() > 1 && (s.back() == '\\' || s.back() == '/')) s.pop_back();
#if defined(_WIN32)
    s = minuscules(s);
    std::replace(s.begin(), s.end(), '/', '\\');
#endif
    return s;
}

bool memeChemin(const std::string& a, const std::string& b) { return cleDeComparaison(a) == cleDeComparaison(b); }

#if defined(_WIN32)
std::string utf8DeLarge(const wchar_t* w) {
    if (!w || !*w) return {};
    const int n = WideCharToMultiByte(CP_UTF8, 0, w, -1, nullptr, 0, nullptr, nullptr);
    if (n <= 1) return {};
    std::string s(static_cast<std::size_t>(n - 1), '\0');
    WideCharToMultiByte(CP_UTF8, 0, w, -1, s.data(), n, nullptr, nullptr);
    return s;
}
std::wstring largeDeUtf8(std::string_view s) {
    if (s.empty()) return {};
    const int n = MultiByteToWideChar(CP_UTF8, 0, s.data(), static_cast<int>(s.size()), nullptr, 0);
    std::wstring w(static_cast<std::size_t>(n > 0 ? n : 0), L'\0');
    if (n > 0) MultiByteToWideChar(CP_UTF8, 0, s.data(), static_cast<int>(s.size()), w.data(), n);
    return w;
}
std::string dossierConnu(REFKNOWNFOLDERID id) {
    PWSTR p = nullptr;
    std::string out;
    if (SUCCEEDED(SHGetKnownFolderPath(id, KF_FLAG_DEFAULT, nullptr, &p)) && p) out = utf8DeLarge(p);
    if (p) CoTaskMemFree(p);
    return out;
}
#else
std::string env(const char* nom) {
    const char* v = std::getenv(nom);
    return (v && *v) ? std::string(v) : std::string{};
}
#endif

Etat& etatGlobal() {
    static Etat e;
    return e;
}
std::vector<std::string>& messagesGlobal() {
    static std::vector<std::string> m;
    return m;
}
bool& etatPose() {
    static bool pose = false;
    return pose;
}

std::string lire(const Ini* ini, std::string_view section, std::string_view cle) {
    if (!ini) return {};
    const auto v = ini->valeur(section, cle);
    return v ? trim(*v) : std::string{};
}

// Projets, bibliotheque et captures, une fois le dossier des donnees connu.
void resoudreSousDossiers(Etat& e, const Ini* installation, const Ini* utilisateur, const Contexte& c) {
    const std::string& donnees = e.dossiers[indice(Cle::Donnees)].chemin;
    for (const Cle k : {Cle::Projets, Cle::Bibliotheque, Cle::Captures}) {
        Dossier& d = e.dossiers[indice(k)];
        d.cle = k;
        std::string brut = lire(utilisateur, "Dossiers", nomCle(k));
        Source source = Source::Utilisateur;
        if (brut.empty()) {
            brut = lire(installation, "Dossiers", nomCle(k));
            source = Source::Installation;
        }
        if (brut.empty()) {
            d.source = Source::ParDefaut;
            d.brut.clear();
            d.chemin = joindre(donnees, sousDossier(k));
        } else {
            d.source = source;
            d.brut = brut;
            d.chemin = absolu(brut, c, donnees);
        }
    }
}

// L'etat de la version de developpement : tout suit le dossier de travail.
Etat etatDeveloppement(std::string_view dossierTravail, std::string_view reglages, const std::string& programme) {
    Etat e;
    e.installe = false;
    e.reglages = std::string(reglages);
    e.programme = programme;
    std::error_code ec;
    const std::string base = dossierTravail.empty() ? utf8De(fs::current_path(ec)) : std::string(dossierTravail);
    for (const Cle k : kCles) {
        Dossier& d = e.dossiers[indice(k)];
        d.cle = k;
        d.source = Source::Developpement;
        d.chemin = k == Cle::Donnees ? base : joindre(base, sousDossier(k));
    }
    return e;
}

} // namespace

// ================================================================= les cles ====
std::string_view nomCle(Cle c) noexcept {
    switch (c) {
        case Cle::Donnees:      return "donnees";
        case Cle::Projets:      return "projets";
        case Cle::Bibliotheque: return "bibliotheque";
        case Cle::Captures:     return "captures";
    }
    return "donnees";
}

std::string_view libelle(Cle c) noexcept {
    switch (c) {
        case Cle::Donnees:      return "Donn\xC3\xA9" "es";
        case Cle::Projets:      return "Projets";
        case Cle::Bibliotheque: return "Biblioth\xC3\xA8que";
        case Cle::Captures:     return "Captures";
    }
    return "";
}

std::string_view sousDossier(Cle c) noexcept {
    switch (c) {
        case Cle::Donnees:      return "";
        case Cle::Projets:      return "projets";
        case Cle::Bibliotheque: return "libs";
        case Cle::Captures:     return "captures";
    }
    return "";
}

std::string_view texteSource(Source s) noexcept {
    switch (s) {
        case Source::Utilisateur:   return "r\xC3\xA9gl\xC3\xA9 dans XPGAnalyser.ini";
        case Source::Installation:  return "choisi \xC3\xA0 l'installation";
        case Source::ParDefaut:     return "par d\xC3\xA9" "faut";
        case Source::Developpement: return "version de d\xC3\xA9veloppement : le dossier de travail";
    }
    return "";
}

// =================================================================== le .ini ====
Ini Ini::depuisTexte(std::string_view texte) {
    Ini ini;
    if (texte.size() >= 3 && static_cast<unsigned char>(texte[0]) == 0xEF && static_cast<unsigned char>(texte[1]) == 0xBB &&
        static_cast<unsigned char>(texte[2]) == 0xBF)
        texte.remove_prefix(3);
    std::size_t debut = 0;
    while (debut <= texte.size()) {
        const auto fin = texte.find('\n', debut);
        std::string ligne(texte.substr(debut, (fin == std::string_view::npos ? texte.size() : fin) - debut));
        if (!ligne.empty() && ligne.back() == '\r') ligne.pop_back();
        ini.lignes_.push_back(std::move(ligne));
        if (fin == std::string_view::npos) break;
        debut = fin + 1;
    }
    // La derniere ligne vide d'un fichier qui finit par un saut de ligne.
    while (!ini.lignes_.empty() && ini.lignes_.back().empty()) ini.lignes_.pop_back();
    return ini;
}

std::string Ini::texte() const {
    std::string out;
    for (const auto& l : lignes_) {
        out += l;
        out += "\r\n";
    }
    return out;
}

std::optional<std::string> Ini::valeur(std::string_view section, std::string_view cle) const {
    bool dedans = false;
    for (const auto& ligne : lignes_) {
        if (const auto nom = nomSection(ligne)) {
            dedans = egalSansCasse(*nom, section);
            continue;
        }
        if (!dedans) continue;
        const auto t = trim(ligne);
        if (commentaire(t)) continue;
        const auto egal = t.find('=');
        if (egal == std::string::npos) continue;
        if (egalSansCasse(trim(std::string_view(t).substr(0, egal)), cle))
            return sansGuillemets(trim(std::string_view(t).substr(egal + 1)));
    }
    return std::nullopt;
}

void Ini::poser(std::string_view section, std::string_view cle, std::string_view valeur) {
    const std::string nouvelle = std::string(cle) + " = " + std::string(valeur);
    bool dedans = false;
    std::optional<std::size_t> finSection;   // apres la derniere ligne non vide de la section
    for (std::size_t i = 0; i < lignes_.size(); ++i) {
        if (const auto nom = nomSection(lignes_[i])) {
            if (dedans) break;                  // la section suivante : on s'arrete
            dedans = egalSansCasse(*nom, section);
            if (dedans) finSection = i + 1;
            continue;
        }
        if (!dedans) continue;
        const auto t = trim(lignes_[i]);
        if (!t.empty()) finSection = i + 1;
        if (commentaire(t)) continue;
        const auto egal = t.find('=');
        if (egal != std::string::npos && egalSansCasse(trim(std::string_view(t).substr(0, egal)), cle)) {
            lignes_[i] = nouvelle;
            return;
        }
    }
    if (finSection) {
        lignes_.insert(lignes_.begin() + static_cast<std::ptrdiff_t>(*finSection), nouvelle);
        return;
    }
    if (!lignes_.empty() && !trim(lignes_.back()).empty()) lignes_.emplace_back();
    lignes_.push_back("[" + std::string(section) + "]");
    lignes_.push_back(nouvelle);
}

std::optional<Ini> lireIni(const fs::path& fichier) {
    std::error_code ec;
    if (!fs::is_regular_file(fichier, ec)) return std::nullopt;
    std::ifstream in(fichier, std::ios::binary);
    if (!in) return std::nullopt;
    std::string texte((std::istreambuf_iterator<char>(in)), std::istreambuf_iterator<char>());
    return Ini::depuisTexte(texte);
}

bool ecrireIni(const fs::path& fichier, const Ini& ini, std::string* pourquoi) {
    std::error_code ec;
    if (fichier.has_parent_path()) fs::create_directories(fichier.parent_path(), ec);
    fs::path tmp = fichier;
    tmp += ".tmp";
    {
        std::ofstream out(tmp, std::ios::binary | std::ios::trunc);
        if (!out) {
            if (pourquoi) *pourquoi = "impossible d'\xC3\xA9" "crire " + utf8De(tmp);
            return false;
        }
        out << "\xEF\xBB\xBF" << ini.texte();
        if (!out) {
            if (pourquoi) *pourquoi = "\xC3\xA9" "criture interrompue : " + utf8De(tmp);
            return false;
        }
    }
    fs::rename(tmp, fichier, ec);
    if (ec) {
        // Un antivirus qui tient le fichier : on ecrit par-dessus, simplement.
        fs::copy_file(tmp, fichier, fs::copy_options::overwrite_existing, ec);
        std::error_code ignore;
        fs::remove(tmp, ignore);
        if (ec) {
            if (pourquoi) *pourquoi = utf8De(fichier) + " : " + ec.message();
            return false;
        }
    }
    return true;
}

std::string modeleIniUtilisateur() {
    return
        "; XPGAnalyser.ini - les dossiers de XPGAnalyser, pour ce compte Windows.\n"
        ";\n"
        "; L'accueil (Dossiers) et le menu Projet > Dossiers de l'application... les\n"
        "; montrent, les ouvrent et les changent. Tu peux aussi modifier ce fichier\n"
        "; \xC3\xA0 la main, l'application ferm\xC3\xA9" "e.\n"
        ";\n"
        "; Une valeur vide : le dossier par d\xC3\xA9" "faut (celui choisi \xC3\xA0 l'installation\n"
        "; pour \"donnees\", un sous-dossier du dossier des donn\xC3\xA9" "es pour les autres).\n"
        "; Un chemin peut commencer par {Documents}, {AppData}, {LocalAppData},\n"
        "; {ProgramData}, {Programme} (le dossier du programme) ou {Donnees}, et contenir\n"
        "; des variables comme %USERPROFILE%. Un chemin relatif part du dossier des donn\xC3\xA9" "es.\n"
        ";\n"
        "; \"donnees\" et \"bibliotheque\" changent au prochain d\xC3\xA9marrage ; \"projets\" et\n"
        "; \"captures\", tout de suite quand on les change depuis la fen\xC3\xAAtre Dossiers.\n"
        "\n"
        "[Dossiers]\n"
        "; Tes donn\xC3\xA9" "es : projets\\, libs\\ (la biblioth\xC3\xA8que), resources\\ (le catalogue), captures\\.\n"
        "donnees =\n"
        "; Tes projets (par d\xC3\xA9" "faut : <donnees>\\projets).\n"
        "projets =\n"
        "; La biblioth\xC3\xA8que partag\xC3\xA9" "e : DFB, DDT, macros (par d\xC3\xA9" "faut : <donnees>\\libs).\n"
        "bibliotheque =\n"
        "; Les captures d'\xC3\xA9" "cran F12 (par d\xC3\xA9" "faut : <donnees>\\captures).\n"
        "captures =\n";
}

// ============================================================== les chemins ====
fs::path cheminDe(std::string_view utf8) {
    const std::u8string u8(utf8.begin(), utf8.end());
    return fs::path(u8);
}

std::string utf8De(const fs::path& p) {
    const auto u8 = p.u8string();
    return std::string(u8.begin(), u8.end());
}

Contexte contexteSysteme(const fs::path& dossierExe) {
    Contexte c;
    c.programme = utf8De(dossierExe);
#if defined(_WIN32)
    c.documents = dossierConnu(FOLDERID_Documents);
    c.appData = dossierConnu(FOLDERID_RoamingAppData);
    c.localAppData = dossierConnu(FOLDERID_LocalAppData);
    c.programData = dossierConnu(FOLDERID_ProgramData);
    c.variable = [](std::string_view nom) -> std::optional<std::string> {
        const std::wstring w = largeDeUtf8(nom);
        const DWORD n = GetEnvironmentVariableW(w.c_str(), nullptr, 0);
        if (n == 0) return std::nullopt;
        std::wstring v(n, L'\0');
        const DWORD m = GetEnvironmentVariableW(w.c_str(), v.data(), n);
        v.resize(m);
        return utf8DeLarge(v.c_str());
    };
#else
    const std::string home = env("HOME");
    const std::string docs = env("XDG_DOCUMENTS_DIR");
    c.documents = !docs.empty() ? docs : home + "/Documents";
    const std::string config = env("XDG_CONFIG_HOME");
    c.appData = !config.empty() ? config : home + "/.config";
    const std::string data = env("XDG_DATA_HOME");
    c.localAppData = !data.empty() ? data : home + "/.local/share";
    c.programData = c.localAppData;
    c.variable = [](std::string_view nom) -> std::optional<std::string> {
        const char* v = std::getenv(std::string(nom).c_str());
        if (!v) return std::nullopt;
        return std::string(v);
    };
#endif
    return c;
}

std::string developper(std::string_view brut, const Contexte& c) {
    std::string out;
    out.reserve(brut.size() + 64);
    std::size_t i = 0;
    while (i < brut.size()) {
        const char ch = brut[i];
        if (ch == '{') {
            const auto fin = brut.find('}', i + 1);
            if (fin != std::string_view::npos) {
                const std::string nom = minuscules(brut.substr(i + 1, fin - i - 1));
                const std::string* valeur = nullptr;
                if (nom == "documents") valeur = &c.documents;
                else if (nom == "appdata") valeur = &c.appData;
                else if (nom == "localappdata") valeur = &c.localAppData;
                else if (nom == "programdata") valeur = &c.programData;
                else if (nom == "programme") valeur = &c.programme;
                else if (nom == "donnees") valeur = &c.donnees;
                if (valeur && !valeur->empty()) {
                    out += *valeur;
                    i = fin + 1;
                    continue;
                }
            }
        } else if (ch == '%') {
            const auto fin = brut.find('%', i + 1);
            if (fin != std::string_view::npos && fin > i + 1 && c.variable) {
                if (const auto v = c.variable(brut.substr(i + 1, fin - i - 1))) {
                    out += *v;
                    i = fin + 1;
                    continue;
                }
            }
        }
        out += ch;
        ++i;
    }
#if !defined(_WIN32)
    // Les valeurs sont ecrites pour Windows ({Documents}\XPGAnalyser) : ailleurs,
    // la barre oblique inverse n'est pas un separateur.
    std::replace(out.begin(), out.end(), '\\', '/');
#endif
    return out;
}

std::string absolu(std::string_view brut, const Contexte& c, std::string_view base) {
    const std::string v = sansGuillemets(trim(developper(trim(brut), c)));
    fs::path p = cheminDe(v);
    if (p.is_relative()) {
        fs::path b = cheminDe(base);
        if (b.is_relative()) {
            std::error_code ec;
            b = fs::absolute(b, ec);
        }
        p = b / p;
    }
    std::string s = utf8De(p.lexically_normal());
    // Sans separateur final ("C:\" garde le sien).
    while (s.size() > 1 && (s.back() == '\\' || s.back() == '/') && !(s.size() == 3 && s[1] == ':')) s.pop_back();
    return s;
}

// =========================================================== la resolution ====
Etat resoudre(const Ini* installation, const Ini* utilisateur, const Contexte& c0, std::string_view dossierTravail,
              std::string_view reglages) {
    if (!installation) return etatDeveloppement(dossierTravail, reglages, c0.programme);
    Etat e;
    e.installe = true;
    e.reglages = std::string(reglages);
    e.programme = c0.programme;
    e.portee = lire(installation, "Installation", "portee");
    e.versionInstallee = lire(installation, "Installation", "version");
    e.epinglerBarre = lire(installation, "Installation", "epingler_barre_taches") == "oui";
    Contexte c = c0;

    Dossier& donnees = e.dossiers[indice(Cle::Donnees)];
    donnees.cle = Cle::Donnees;
    std::string brut = lire(utilisateur, "Dossiers", "donnees");
    donnees.source = Source::Utilisateur;
    if (brut.empty()) {
        brut = lire(installation, "Dossiers", "donnees");
        donnees.source = Source::Installation;
    }
    if (brut.empty()) donnees.source = Source::ParDefaut;
    donnees.brut = donnees.source == Source::ParDefaut ? std::string{} : brut;
    donnees.chemin = absolu(brut.empty() ? std::string(kDonneesParDefaut) : brut, c, c.programme);
    c.donnees = donnees.chemin;
    resoudreSousDossiers(e, installation, utilisateur, c);

    if (const auto j = lire(installation, "Maintenance", "journaux"); !j.empty()) e.journaux = absolu(j, c, c.programme);
    if (const auto s = lire(installation, "Maintenance", "sauvegardes"); !s.empty()) e.sauvegardes = absolu(s, c, c.programme);
    return e;
}

// ============================================================= le demarrage ====
Demarrage preparer(const fs::path& dossierExe, std::string_view reglages) {
    Demarrage d;
    std::error_code ec;
    const fs::path iniInstallation = dossierExe / cheminDe(kNomIniInstallation);
    const fs::path iniUtilisateur = cheminDe(reglages) / cheminDe(kNomIniUtilisateur);
    const Contexte c = contexteSysteme(dossierExe);

    if (dossierExe.empty() || !fs::is_regular_file(iniInstallation, ec)) {
        Etat e = etatDeveloppement({}, reglages, c.programme);
        e.iniUtilisateur = utf8De(iniUtilisateur);
        etatGlobal() = std::move(e);
        etatPose() = true;
        return d;
    }

    const auto installation = lireIni(iniInstallation);
    const auto utilisateur = lireIni(iniUtilisateur);
    Etat e = resoudre(installation ? &*installation : nullptr, utilisateur ? &*utilisateur : nullptr, c, {}, reglages);
    if (!installation) e = resoudre(nullptr, nullptr, c, {}, reglages);   // illisible : comme avant
    e.iniInstallation = utf8De(iniInstallation);
    e.iniUtilisateur = utf8De(iniUtilisateur);
    if (!e.installe) {
        d.messages.push_back("installation.ini illisible : les dossiers suivent le dossier de travail");
        etatGlobal() = std::move(e);
        etatPose() = true;
        messagesGlobal() = d.messages;
        return d;
    }

    // LE DOSSIER DES DONNEES : celui qui est regle, sinon Documents\XPGAnalyser,
    // sinon %LOCALAPPDATA%\XpgAnalyzer\Donnees (toujours local, toujours a toi).
    std::string pourquoi;
    if (!dossierInscriptible(cheminDe(e[Cle::Donnees].chemin), &pourquoi)) {
        d.messages.push_back("Le dossier des donn\xC3\xA9" "es " + e[Cle::Donnees].chemin + " est inaccessible (" + pourquoi + ").");
        Contexte c2 = c;
        const std::string replis[2] = {absolu(kDonneesParDefaut, c, c.programme),
                                       absolu("{LocalAppData}\\XpgAnalyzer\\Donnees", c, c.programme)};
        bool trouve = false;
        for (const auto& repli : replis) {
            if (memeChemin(repli, e[Cle::Donnees].chemin)) continue;
            if (!dossierInscriptible(cheminDe(repli), &pourquoi)) continue;
            Dossier& dd = e.dossiers[indice(Cle::Donnees)];
            dd.chemin = repli;
            dd.source = Source::ParDefaut;
            c2.donnees = repli;
            resoudreSousDossiers(e, installation ? &*installation : nullptr, utilisateur ? &*utilisateur : nullptr, c2);
            d.messages.push_back("Les donn\xC3\xA9" "es vont dans " + repli + " pour cette fois (Dossiers, sur l'accueil, pour le changer).");
            trouve = true;
            break;
        }
        if (!trouve) d.messages.push_back("Aucun dossier des donn\xC3\xA9" "es utilisable : enregistrer un projet \xC3\xA9" "chouera.");
    }

    // L'AMORCE : resources\ et libs\ livres (dans le dossier du programme), copies
    // sans rien ecraser - la premiere fois, ou pour un autre compte du PC.
    const fs::path donnees = cheminDe(e[Cle::Donnees].chemin);
    if (!fs::is_regular_file(donnees / "resources" / "schneider_library.txt", ec) && fs::is_directory(dossierExe / "resources", ec)) {
        const auto k = copierContenu(dossierExe / "resources", donnees / "resources");
        if (k.erreurs) d.messages.push_back("resources\\ copi\xC3\xA9 en partie : " + k.premiereErreur);
    }
    const fs::path bibliotheque = cheminDe(e[Cle::Bibliotheque].chemin);
    if (!fs::is_regular_file(bibliotheque / "index.txt", ec) && fs::is_directory(dossierExe / "libs", ec)) {
        if (dossierInscriptible(bibliotheque, &pourquoi)) {
            const auto k = copierContenu(dossierExe / "libs", bibliotheque);
            if (k.erreurs) d.messages.push_back("Biblioth\xC3\xA8que copi\xC3\xA9" "e en partie : " + k.premiereErreur);
        } else {
            d.messages.push_back("Biblioth\xC3\xA8que inaccessible : " + e[Cle::Bibliotheque].chemin + " (" + pourquoi + ").");
        }
    }

    fs::current_path(donnees, ec);
    if (ec) d.messages.push_back("Impossible de se placer dans " + e[Cle::Donnees].chemin + " : " + ec.message());
    d.installe = true;
    d.donnees = e[Cle::Donnees].chemin;
    etatGlobal() = std::move(e);
    etatPose() = true;
    messagesGlobal() = d.messages;
    return d;
}

const Etat& actuel() {
    Etat& e = etatGlobal();
    if (!e.installe) {
        // La version de developpement suit le dossier de travail du moment.
        std::string reglages = e.reglages, ini = e.iniUtilisateur, programme = e.programme;
        e = etatDeveloppement({}, reglages, programme);
        e.iniUtilisateur = ini;
    }
    return e;
}

std::string actif(Cle c) {
    const Etat& e = etatGlobal();
    if (!e.installe) return {};
    return e[c].chemin;
}

std::vector<std::string> prendreMessages() {
    std::vector<std::string> m;
    m.swap(messagesGlobal());
    return m;
}

void poserEtat(Etat e) {
    etatGlobal() = std::move(e);
    etatPose() = true;
}

// ================================================================= changer ====
Bilan changer(const std::vector<Changement>& changements, const Contexte& c0) {
    Bilan b;
    Etat& e = etatGlobal();
    if (!e.installe) {
        b.ok = false;
        b.lignes.push_back("Version de d\xC3\xA9veloppement (pas d'installation.ini \xC3\xA0 c\xC3\xB4t\xC3\xA9 du programme) : "
                           "les dossiers suivent le dossier de travail, rien n'a chang\xC3\xA9.");
        return b;
    }
    Contexte c = c0;
    c.donnees = e[Cle::Donnees].chemin;
    const auto installation = e.iniInstallation.empty() ? std::nullopt : lireIni(cheminDe(e.iniInstallation));

    // 1. TOUT VERIFIER D'ABORD : un dossier refuse, et rien ne change.
    struct Plan {
        Changement  ch;
        std::string nouveau, ancien;
    };
    std::vector<Plan> plans;
    for (const auto& ch : changements) {
        std::string nouveau;
        const std::string brut = trim(ch.brut);
        if (brut.empty()) {
            if (ch.cle == Cle::Donnees) {
                const std::string inst = lire(installation ? &*installation : nullptr, "Dossiers", "donnees");
                nouveau = absolu(inst.empty() ? std::string(kDonneesParDefaut) : inst, c, c.programme);
            } else {
                nouveau = joindre(e[Cle::Donnees].chemin, sousDossier(ch.cle));
            }
        } else {
            nouveau = absolu(brut, c, ch.cle == Cle::Donnees ? c.programme : e[Cle::Donnees].chemin);
        }
        std::string pourquoi;
        if (!dossierInscriptible(cheminDe(nouveau), &pourquoi)) {
            b.ok = false;
            b.lignes.push_back(std::string(libelle(ch.cle)) + " : " + nouveau + " n'est pas utilisable (" + pourquoi +
                               "). Rien n'a chang\xC3\xA9.");
            return b;
        }
        Changement retenu = ch;
        retenu.brut = brut;
        plans.push_back({retenu, nouveau, e[ch.cle].chemin});
    }

    // 2. LES COPIES, sans rien ecraser ; l'ancien dossier reste tel quel.
    std::error_code ec;
    for (const auto& p : plans) {
        if (!p.ch.copier || memeChemin(p.ancien, p.nouveau)) continue;
        if (!fs::is_directory(cheminDe(p.ancien), ec)) continue;
        if (contient(cheminDe(p.ancien), cheminDe(p.nouveau)) || contient(cheminDe(p.nouveau), cheminDe(p.ancien))) {
            b.lignes.push_back(std::string(libelle(p.ch.cle)) + " : pas de copie, l'un des dossiers est dans l'autre.");
            continue;
        }
        const auto k = copierContenu(cheminDe(p.ancien), cheminDe(p.nouveau));
        std::string ligne = std::string(libelle(p.ch.cle)) + " : " + std::to_string(k.fichiers) + " fichier" + (k.fichiers > 1 ? "s" : "") +
                            " copi\xC3\xA9" + (k.fichiers > 1 ? "s" : "") + " (" + tailleLisible(k.octets) + ")";
        if (k.gardes) ligne += ", " + std::to_string(k.gardes) + " d\xC3\xA9j\xC3\xA0 pr\xC3\xA9sent" + (k.gardes > 1 ? "s" : "") + " gard\xC3\xA9" + (k.gardes > 1 ? "s" : "");
        ligne += " ; l'ancien dossier reste tel quel.";
        b.lignes.push_back(ligne);
        if (k.erreurs) {
            b.ok = false;
            b.lignes.push_back("  " + std::to_string(k.erreurs) + " erreur(s), dont : " + k.premiereErreur);
        }
    }

    // 3. XPGANALYSER.INI, l'ancien garde en .precedent.
    const fs::path fichier = cheminDe(e.iniUtilisateur);
    auto ini = lireIni(fichier);
    if (ini) {
        fs::path precedent = fichier;
        precedent += ".precedent";
        fs::copy_file(fichier, precedent, fs::copy_options::overwrite_existing, ec);
    } else {
        ini = Ini::depuisTexte(modeleIniUtilisateur());
    }
    for (const auto& p : plans) ini->poser("Dossiers", nomCle(p.ch.cle), p.ch.brut);
    std::string pourquoi;
    if (!ecrireIni(fichier, *ini, &pourquoi)) {
        b.ok = false;
        b.lignes.push_back("XPGAnalyser.ini n'a pas pu \xC3\xAAtre \xC3\xA9" "crit : " + pourquoi);
        return b;
    }
    b.ecrit = true;

    // 4. APPLIQUER : projets et captures tout de suite ; le reste au prochain demarrage.
    for (const auto& p : plans) {
        const Source source = p.ch.brut.empty() ? Source::ParDefaut : Source::Utilisateur;
        if (p.ch.cle == Cle::Projets || p.ch.cle == Cle::Captures) {
            e.dossiers[indice(p.ch.cle)] = Dossier{p.ch.cle, p.nouveau, p.ch.brut, source};
            b.lignes.push_back(std::string(libelle(p.ch.cle)) + " : " + p.nouveau + " (tout de suite).");
        } else {
            b.redemarrer = true;
            b.lignes.push_back(std::string(libelle(p.ch.cle)) + " : " + p.nouveau + " (au prochain d\xC3\xA9marrage).");
        }
    }
    return b;
}

// ============================================================= utilitaires ====
bool dossierInscriptible(const fs::path& p, std::string* pourquoi) {
    std::error_code ec;
    if (p.empty()) {
        if (pourquoi) *pourquoi = "chemin vide";
        return false;
    }
    fs::create_directories(p, ec);
    std::error_code e2;
    if (ec && !fs::is_directory(p, e2)) {
        if (pourquoi) *pourquoi = ec.message();
        return false;
    }
    if (!fs::is_directory(p, e2)) {
        if (pourquoi) *pourquoi = "ce n'est pas un dossier";
        return false;
    }
    const auto t = std::chrono::steady_clock::now().time_since_epoch().count();
    const fs::path essai = p / (".xpganalyser-essai-" + std::to_string(static_cast<long long>(t % 1000000007LL)) + ".tmp");
    {
        std::ofstream out(essai, std::ios::binary | std::ios::trunc);
        if (!out) {
            if (pourquoi) *pourquoi = "\xC3\xA9" "criture refus\xC3\xA9" "e";
            return false;
        }
        out << "essai";
    }
    fs::remove(essai, ec);
    return true;
}

Copie copierContenu(const fs::path& de, const fs::path& vers) {
    Copie k;
    std::error_code ec;
    if (!fs::is_directory(de, ec)) return k;
    fs::create_directories(vers, ec);
    fs::recursive_directory_iterator it(de, fs::directory_options::skip_permission_denied, ec), fin;
    if (ec) {
        ++k.erreurs;
        k.premiereErreur = utf8De(de) + " : " + ec.message();
        return k;
    }
    for (; it != fin; it.increment(ec)) {
        if (ec) {
            ++k.erreurs;
            if (k.premiereErreur.empty()) k.premiereErreur = ec.message();
            ec.clear();
            continue;
        }
        std::error_code e2;
        const fs::path source = it->path();
        const fs::path cible = vers / source.lexically_relative(de);
        if (it->is_directory(e2)) {
            fs::create_directories(cible, e2);
            continue;
        }
        if (!it->is_regular_file(e2)) continue;
        if (fs::exists(cible, e2)) {
            ++k.gardes;
            continue;
        }
        fs::create_directories(cible.parent_path(), e2);
        e2.clear();
        fs::copy_file(source, cible, fs::copy_options::none, e2);
        if (e2) {
            ++k.erreurs;
            if (k.premiereErreur.empty()) k.premiereErreur = utf8De(source.lexically_relative(de)) + " : " + e2.message();
            continue;
        }
        ++k.fichiers;
        std::error_code e3;
        const auto taille = fs::file_size(cible, e3);
        if (!e3) k.octets += taille;
    }
    return k;
}

bool contient(const fs::path& parent, const fs::path& enfant) {
    const std::string p = cleDeComparaison(utf8De(parent));
    const std::string e = cleDeComparaison(utf8De(enfant));
    if (p.empty() || e.size() < p.size() || e.compare(0, p.size(), p) != 0) return false;
    return e.size() == p.size() || e[p.size()] == '\\' || e[p.size()] == '/' || p.back() == '\\' || p.back() == '/';
}

std::size_t compterFichiers(const fs::path& p, std::uintmax_t* octets, std::size_t limite) {
    std::size_t n = 0;
    std::error_code ec;
    if (octets) *octets = 0;
    if (!fs::is_directory(p, ec)) return 0;
    for (fs::recursive_directory_iterator it(p, fs::directory_options::skip_permission_denied, ec), fin; !ec && it != fin; it.increment(ec)) {
        std::error_code e2;
        if (!it->is_regular_file(e2)) continue;
        ++n;
        if (octets) {
            const auto t = it->file_size(e2);
            if (!e2) *octets += t;
        }
        if (n >= limite) break;
    }
    return n;
}

std::string ouvrirDansLeSysteme(const std::string& cheminUtf8) {
    std::error_code ec;
    if (!fs::exists(cheminDe(cheminUtf8), ec)) return cheminUtf8 + " n'existe pas encore.";
#if defined(_WIN32)
    const std::wstring w = largeDeUtf8(cheminUtf8);
    const auto r = reinterpret_cast<INT_PTR>(ShellExecuteW(nullptr, L"open", w.c_str(), nullptr, nullptr, SW_SHOWNORMAL));
    if (r <= 32) return "Windows n'a pas pu ouvrir " + cheminUtf8 + " (code " + std::to_string(static_cast<long long>(r)) + ").";
    return {};
#else
    if (cheminUtf8.find('"') != std::string::npos) return "chemin refus\xC3\xA9 : " + cheminUtf8;
#  if defined(__APPLE__)
    const std::string commande = "open \"" + cheminUtf8 + "\"";
#  else
    const std::string commande = "xdg-open \"" + cheminUtf8 + "\" >/dev/null 2>&1 &";
#  endif
    if (std::system(commande.c_str()) != 0) return "aucun programme n'a pu ouvrir " + cheminUtf8;
    return {};
#endif
}

std::string tailleLisible(std::uintmax_t octets) {
    const char* unites[] = {"o", "Ko", "Mo", "Go", "To"};
    double v = static_cast<double>(octets);
    int u = 0;
    while (v >= 1024.0 && u < 4) {
        v /= 1024.0;
        ++u;
    }
    char buf[32];
    if (u == 0) std::snprintf(buf, sizeof buf, "%llu %s", static_cast<unsigned long long>(octets), unites[u]);
    else std::snprintf(buf, sizeof buf, "%.1f %s", v, unites[u]);
    std::string s(buf);
    std::replace(s.begin(), s.end(), '.', ',');
    return s;
}

} // namespace app::dossiers
