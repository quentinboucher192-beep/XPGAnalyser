// =============================================================================
//  app/ArbreTailles.cpp - 1.21.0 : le poids des fichiers de l'application
// =============================================================================
#include "ArbreTailles.hpp"

#include "Dossiers.hpp"   // cheminDe, utf8De, contient, tailleLisible

#include <algorithm>
#include <utility>

namespace fs = std::filesystem;

namespace app::tailles {

namespace {

char minuscule(char c) noexcept { return c >= 'A' && c <= 'Z' ? static_cast<char>(c - 'A' + 'a') : c; }

// Deux noms sans tenir compte de la casse (ASCII), puis tels quels.
int comparerNoms(const std::string& a, const std::string& b) {
    const std::size_t n = std::min(a.size(), b.size());
    for (std::size_t i = 0; i < n; ++i) {
        const char x = minuscule(a[i]), y = minuscule(b[i]);
        if (x != y) return static_cast<unsigned char>(x) < static_cast<unsigned char>(y) ? -1 : 1;
    }
    if (a.size() != b.size()) return a.size() < b.size() ? -1 : 1;
    return a.compare(b) < 0 ? -1 : (a == b ? 0 : 1);
}

bool memeNom(const std::string& a, const std::string& b) {
#if defined(_WIN32)
    if (a.size() != b.size()) return false;
    for (std::size_t i = 0; i < a.size(); ++i)
        if (minuscule(a[i]) != minuscule(b[i])) return false;
    return true;
#else
    return a == b;
#endif
}

// Le chemin a comparer : les liens resolus (un dossier de projets qui est un
// lien vers les donnees n'est pas compte deux fois), sans separateur final.
fs::path normal(const std::string& utf8) {
    std::error_code ec;
    const fs::path p = dossiers::cheminDe(utf8);
    fs::path n = fs::weakly_canonical(p, ec);
    if (ec || n.empty()) {
        std::error_code e2;
        n = fs::absolute(p, e2).lexically_normal();
    }
    std::string s = dossiers::utf8De(n);
    while (s.size() > 1 && (s.back() == '/' || s.back() == '\\')) {
        if (s.size() == 3 && s[1] == ':') break;   // la racine d'un disque garde son separateur
        s.pop_back();
    }
    return dossiers::cheminDe(s);
}

// Les dossiers de `enfant` sous `parent` (enfant est dans parent : contient()).
std::vector<std::string> sousChemin(const fs::path& parent, const fs::path& enfant) {
    const std::string p = dossiers::utf8De(parent), e = dossiers::utf8De(enfant);
    std::vector<std::string> out;
    if (e.size() <= p.size()) return out;
    std::string morceau;
    for (std::size_t i = p.size(); i < e.size(); ++i) {
        const char c = e[i];
        if (c == '/' || c == '\\') {
            if (!morceau.empty()) out.push_back(std::move(morceau));
            morceau.clear();
        } else {
            morceau += c;
        }
    }
    if (!morceau.empty()) out.push_back(std::move(morceau));
    return out;
}

// La marche dans un arbre de dossiers (la recursion s'arrete a profondeurMax).
class Marche {
public:
    explicit Marche(const Options& o) : o_(o) {}
    std::uint64_t illisibles = 0;
    std::size_t   noeuds = 0;
    bool          annule = false;

    void dossier(const fs::path& p, Noeud& n, int profondeur) {
        std::error_code ec;
        fs::directory_iterator it(p, fs::directory_options::none, ec);
        if (ec) {
            illisible();
            return;
        }
        for (const fs::directory_iterator fin{}; it != fin; it.increment(ec)) {
            if (arreter()) return;
            const fs::directory_entry& e = *it;
            std::error_code e2;
            const fs::file_status st = e.symlink_status(e2);
            if (e2) {
                illisible();
                continue;
            }
            if (fs::is_symlink(st)) continue;          // un lien : ni suivi, ni compte
            if (fs::is_directory(st)) {
                Noeud d;
                d.nom = dossiers::utf8De(e.path().filename());
                d.dossier = true;
                if (profondeur + 1 > o_.profondeurMax) {
                    illisible();
                    continue;
                }
                const bool garde = noeuds < o_.maxNoeuds;
                dossier(e.path(), d, profondeur + 1);
                if (annule) return;
                n.octets += d.octets;
                n.fichiers += d.fichiers;
                if (garde) {
                    ++noeuds;
                    n.enfants.push_back(std::move(d));
                } else {
                    // Au-dela de la limite : compte, pas garde (la memoire reste bornee).
                    n.nonDetailles += d.fichiers;
                    n.octetsNonDetailles += d.octets;
                }
            } else if (fs::is_regular_file(st)) {
                const std::uintmax_t t = e.file_size(e2);
                if (e2) {
                    illisible();
                    continue;
                }
                n.octets += t;
                ++n.fichiers;
                if (o_.progression) {
                    o_.progression->fichiers.fetch_add(1, std::memory_order_relaxed);
                    o_.progression->octets.fetch_add(t, std::memory_order_relaxed);
                }
                if (noeuds < o_.maxNoeuds) {
                    Noeud f;
                    f.nom = dossiers::utf8De(e.path().filename());
                    f.octets = t;
                    f.fichiers = 1;
                    ++noeuds;
                    n.enfants.push_back(std::move(f));
                } else {
                    ++n.nonDetailles;
                    n.octetsNonDetailles += t;
                }
            }
            // Le reste (un peripherique, un tube) n'est pas un fichier de l'application.
        }
        if (ec) illisible();   // la lecture du dossier s'est arretee en route
    }

private:
    bool arreter() {
        if (!annule && o_.arret && o_.arret->load(std::memory_order_relaxed)) annule = true;
        return annule;
    }
    void illisible() {
        ++illisibles;
        if (o_.progression) o_.progression->illisibles.fetch_add(1, std::memory_order_relaxed);
    }
    const Options& o_;
};

const Noeud* trouver(const Noeud& racine, const std::vector<std::string>& chemin) {
    const Noeud* n = &racine;
    for (const auto& nom : chemin) {
        const Noeud* suivant = nullptr;
        for (const auto& e : n->enfants)
            if (e.dossier && memeNom(e.nom, nom)) {
                suivant = &e;
                break;
            }
        if (!suivant) return nullptr;
        n = suivant;
    }
    return n;
}

} // namespace

// ============================================================== parcourir ====
Parcours parcourir(const fs::path& dossier, const Options& o) {
    Parcours r;
    r.racine.dossier = true;
    r.racine.nom = dossiers::utf8De(dossier.filename());
    if (r.racine.nom.empty()) r.racine.nom = dossiers::utf8De(dossier);
    std::error_code ec;
    if (!fs::is_directory(dossier, ec)) {
        r.illisibles = 1;
        if (o.progression) o.progression->illisibles.fetch_add(1, std::memory_order_relaxed);
        return r;
    }
    Marche m(o);
    m.dossier(dossier, r.racine, 0);
    r.illisibles = m.illisibles;
    r.noeuds = m.noeuds;
    r.annule = m.annule;
    return r;
}

bool avant(const Noeud& a, const Noeud& b, Tri t) {
    if (t == Tri::Poids) {
        if (a.octets != b.octets) return a.octets > b.octets;
        return comparerNoms(a.nom, b.nom) < 0;
    }
    if (a.dossier != b.dossier) return a.dossier;
    return comparerNoms(a.nom, b.nom) < 0;
}

void trier(Noeud& n, Tri t) {
    std::stable_sort(n.enfants.begin(), n.enfants.end(), [t](const Noeud& a, const Noeud& b) { return avant(a, b, t); });
    for (auto& e : n.enfants)
        if (e.dossier) trier(e, t);
}

// ================================================================ racines ====
Retenues retenir(const std::vector<Racine>& candidates) {
    struct Vue {
        std::size_t rang;
        fs::path    cle;
    };
    std::vector<Vue> la;
    for (std::size_t i = 0; i < candidates.size(); ++i) {
        if (candidates[i].chemin.empty()) continue;
        std::error_code ec;
        if (!fs::is_directory(dossiers::cheminDe(candidates[i].chemin), ec)) continue;
        la.push_back({i, normal(candidates[i].chemin)});
    }
    // Dedans : contenue par une autre (strictement), ou la meme qu'une plus haut.
    const auto dedans = [&](std::size_t i) {
        for (std::size_t j = 0; j < la.size(); ++j) {
            if (j == i || !dossiers::contient(la[j].cle, la[i].cle)) continue;
            if (!dossiers::contient(la[i].cle, la[j].cle) || j < i) return true;
        }
        return false;
    };
    Retenues r;
    std::vector<std::size_t> gardees;   // les rangs dans `la`
    for (std::size_t i = 0; i < la.size(); ++i)
        if (!dedans(i)) {
            gardees.push_back(i);
            r.racines.push_back(candidates[la[i].rang]);
        }
    for (std::size_t i = 0; i < la.size(); ++i) {
        if (std::find(gardees.begin(), gardees.end(), i) != gardees.end()) continue;
        for (std::size_t k = 0; k < gardees.size(); ++k) {
            const fs::path& cle = la[gardees[k]].cle;
            if (!dossiers::contient(cle, la[i].cle)) continue;
            Incluse x;
            x.racine = candidates[la[i].rang];
            x.dans = k;
            x.sousChemin = sousChemin(cle, la[i].cle);
            r.incluses.push_back(std::move(x));
            break;
        }
    }
    return r;
}

Bilan mesurer(const std::vector<Racine>& candidates, const Options& o, Tri tri,
              const std::function<void(std::size_t, const Racine&)>& debut) {
    Bilan b;
    Retenues r = retenir(candidates);
    std::size_t reste = o.maxNoeuds;
    for (std::size_t i = 0; i < r.racines.size(); ++i) {
        if (o.arret && o.arret->load(std::memory_order_relaxed)) {
            b.annule = true;
            break;
        }
        if (debut) debut(i, r.racines[i]);
        Options oi = o;
        oi.maxNoeuds = reste;
        Parcours p = parcourir(dossiers::cheminDe(r.racines[i].chemin), oi);
        b.illisibles += p.illisibles;
        if (p.annule) {
            b.annule = true;
            break;
        }
        reste -= std::min(reste, p.noeuds);
        trier(p.racine, tri);
        b.octets += p.racine.octets;
        b.fichiers += p.racine.fichiers;
        b.racines.push_back({std::move(r.racines[i]), std::move(p.racine)});
    }
    for (auto& x : r.incluses) {
        if (x.dans < b.racines.size())
            if (const Noeud* n = trouver(b.racines[x.dans].arbre, x.sousChemin)) {
                x.trouvee = true;
                x.octets = n->octets;
                x.fichiers = n->fichiers;
            }
        b.incluses.push_back(std::move(x));
    }
    return b;
}

// ================================================================= texte ====
std::string milliers(std::uint64_t n) {
    const std::string d = std::to_string(n);
    std::string out;
    for (std::size_t i = 0; i < d.size(); ++i) {
        if (i && (d.size() - i) % 3 == 0) out += "\xE2\x80\xAF";   // espace fine insecable
        out += d[i];
    }
    return out;
}

std::string nombreDeFichiers(std::uint64_t n) { return milliers(n) + (n > 1 ? " fichiers" : " fichier"); }

std::string resume(const Bilan& b, std::string_view entete) {
    std::string out(entete);
    if (!out.empty() && out.back() != '\n') out += '\n';
    out += "Poids total : " + dossiers::tailleLisible(b.octets) + " (" + nombreDeFichiers(b.fichiers) + ", "
         + std::to_string(b.racines.size()) + (b.racines.size() > 1 ? " dossiers" : " dossier") + ")\n";
    for (std::size_t i = 0; i < b.racines.size(); ++i) {
        const auto& m = b.racines[i];
        out += "  " + m.racine.libelle + " : " + dossiers::tailleLisible(m.arbre.octets) + " (" + nombreDeFichiers(m.arbre.fichiers)
             + ") - " + m.racine.chemin + "\n";
        for (const auto& x : b.incluses)
            if (x.dans == i && x.trouvee && !x.sousChemin.empty())
                out += "      dont " + x.racine.libelle + " : " + dossiers::tailleLisible(x.octets) + " (" + nombreDeFichiers(x.fichiers) + ")\n";
    }
    if (b.illisibles > 0)
        out += milliers(b.illisibles) + (b.illisibles > 1 ? " \xC3\xA9l\xC3\xA9ments illisibles (ignor\xC3\xA9s)\n"
                                                          : " \xC3\xA9l\xC3\xA9ment illisible (ignor\xC3\xA9)\n");
    if (b.annule) out += "Calcul interrompu : les nombres sont partiels.\n";
    return out;
}

} // namespace app::tailles
