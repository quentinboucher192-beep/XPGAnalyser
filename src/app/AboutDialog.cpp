#include "AboutDialog.hpp"
#include "../core/Edition.hpp"   // 1.12.20 : le nom de l'application (API Studio, IHM Studio)
#include "../core/CodeStats.hpp"   // 1.11.7 : les lignes de code
#include "../core/Process.hpp"     // 1.21.0 : le dossier du programme (repli)
#include "../ui/DialogFrame.hpp"   // 1.21.0 : la fenetre se deplace et s'agrandit (1.12.21)
#include "../ui/widgets/DataViews.hpp"   // 1.21.0 : l'arbre des fichiers
#include "../ui/widgets/PropertyGridHooks.hpp"   // 1.24.1 : le menu d'un dossier (Ouvrir dans l'explorateur, Aller a)

#include "App.hpp"
#include "ArbreTailles.hpp"   // 1.21.0 : le poids des fichiers (le parcours, sans ecran)
#include "Brand.hpp"          // 1.21.0 : le logo en tete
#include "Dossiers.hpp"
#include "Fenetres.hpp"
#include "Settings.hpp"
#include "ai/AiPaths.hpp"
#include "screens/Screens.hpp"
#include "../core/Version.hpp"
#include "../hmi/HmiCommands.hpp"
#include "../menu/MenuManager.hpp"

#include <algorithm>
#include <atomic>
#include <chrono>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <ctime>
#include <exception>
#include <filesystem>
#include <memory>
#include <mutex>
#include <string_view>
#include <thread>
#include <unordered_map>
#include <utility>
#include <vector>

namespace app {

    namespace {

        constexpr int kAboutPathId = 900;   // 1.24.1 : Ouvrir dans l'explorateur, Aller a, Copier le chemin (900..902)

        // __DATE__ ("Oct  2 2026") -> "02/10/2026", comme la pastille de l'accueil.
        std::string buildDate() {
            const std::string_view d = __DATE__;
            const std::string_view months = "JanFebMarAprMayJunJulAugSepOctNovDec";
            const auto m = d.size() >= 11 ? months.find(d.substr(0, 3)) : std::string_view::npos;
            if (m == std::string_view::npos) return std::string(d);
            const int day = std::atoi(std::string(d.substr(4, 2)).c_str());
            char buf[64];
            std::snprintf(buf, sizeof buf, "%02d/%02d/%.4s", day % 100, static_cast<int>(m / 3 + 1), d.substr(7, 4).data());
            return std::string(buf) + " \xC3\xA0 " + std::string(__TIME__).substr(0, 5);
        }

        std::string systemName() {
#if defined(_WIN32)
            return "Windows";
#elif defined(__APPLE__)
            return "macOS";
#elif defined(__linux__)
            return "Linux";
#else
            return "autre syst\xC3\xA8me";
#endif
        }

        std::string compilerName() {
#if defined(_MSC_VER)
            return "MSVC " + std::to_string(_MSC_VER);
#elif defined(__clang__)
            return std::string("Clang ") + __clang_version__;
#elif defined(__GNUC__)
            return std::string("GCC ") + __VERSION__;
#else
            return "compilateur inconnu";
#endif
        }

        std::string foldersText() {
            const auto& e = dossiers::actuel();
            std::string out;
            for (const auto cle : dossiers::kCles)
                out += "  " + std::string(dossiers::libelle(cle)) + " : " + e[cle].chemin + "\n";
            if (!e.reglages.empty())  out += "  R\xC3\xA9glages : " + e.reglages + "\n";
            if (!e.programme.empty()) out += "  Programme : " + e.programme + "\n";
            return out;
        }

        // Le projet ouvert, sans ses donnees : son nom et ce qu'il contient.
        std::string projectText(const App& app) {
            const auto project = app.project();
            if (!project) return "  aucun projet ouvert\n";
            std::string name = project->header.projectName;
            if (const auto& folder = app.projectFolder(); !folder.empty()) {
                const auto slash = folder.find_last_of("/\\");
                name = slash == std::string::npos ? folder : folder.substr(slash + 1);
            }
            std::string out = "  " + (name.empty() ? std::string("(sans nom)") : name) + " : "
                            + std::to_string(project->sections.size()) + " sections";
            if (const auto hmi = app.hmi()) out += ", " + std::to_string(hmi->project.views.size()) + " vues de l'IHM";
            if (!project->header.productVersion.empty()) out += " ; Control Expert " + project->header.productVersion;
            return out + "\n";
        }

        // 1.21.0 : la ligne de la construction, et celle du code (l'en-tete de la fenetre).
        std::string buildLine() { return "Construit le " + buildDate() + " (" + compilerName() + ", " + systemName() + ")"; }
        std::string codeLine() {
            return "Le code de l'application (compt\xC3\xA9 le " + std::string(xpg::codestats::kCountedOn) + ") : "
                 + std::to_string(xpg::codestats::kTotalLines) + " lignes dans " + std::to_string(xpg::codestats::kTotalFiles) + " fichiers";
        }

        // Tout ce qui suit l'en-tete (nom, version, construction) : la fenetre le montre
        // dans une zone qui defile, au-dessus des fichiers.
        std::string detailsText(const App& app) {
            std::string out;
            // 1.11.7 : le code de l'application (outils/compter_lignes.py).
            out += codeLine() + "\n";
            for (const auto& e : xpg::codestats::kByExtension)
                out += "  " + std::string(e.extension) + " : " + std::to_string(e.lines) + " lignes (" + std::to_string(e.files)
                     + (e.files > 1 ? " fichiers)\n" : " fichier)\n");
            out += "\n";
            out += "Les dossiers de l'application :\n" + foldersText() + "\n";
            out += "Le projet ouvert :\n" + projectText(app) + "\n";
            out += "Composants libres embarqu\xC3\xA9s et leurs licences : SDL3 (zlib), miniz (MIT), nanosvg (zlib), "
                   "stb (domaine public), minimp3 (CC0) ; PDFium, le lecteur PDF de la visionneuse de documents, charg\xC3\xA9 \xC3\xA0 "
                   "la demande (pdfium.dll : BSD-3-Clause et Apache-2.0, ses licences dans licences\\pdfium).\n\n";
            out += "\xC2\xAB Copier les informations pour le support \xC2\xBB met dans le presse-papiers la version, "
                   "le syst\xC3\xA8me, les dossiers et le r\xC3\xA9sum\xC3\xA9 du projet ouvert (nom, nombre de sections et de vues), "
                   "jamais ses donn\xC3\xA9" "es.";
            return out;
        }

        std::string maintenant() {
            const std::time_t now = std::time(nullptr);
            std::tm tm{};
#if defined(_WIN32)
            localtime_s(&tm, &now);
#else
            localtime_r(&now, &tm);
#endif
            char buf[64];
            std::snprintf(buf, sizeof buf, "%02d/%02d/%04d \xC3\xA0 %02d:%02d", tm.tm_mday, tm.tm_mon + 1, tm.tm_year + 1900, tm.tm_hour, tm.tm_min);
            return buf;
        }

        // =====================================================================
        //  1.21.0 : LES FICHIERS DE L'APPLICATION
        // =====================================================================

        // Les dossiers de l'application, dans l'ordre de l'arbre. Ceux qui sont dans un
        // autre (l'Assistant IA dans les reglages, les projets dans les donnees) ne sont
        // pas des racines : leur dossier porte leur nom dans l'arbre de la racine qui les
        // contient (ArbreTailles : retenir). Ceux qui n'existent pas sont oublies.
        std::vector<tailles::Racine> racinesCandidates() {
            namespace fs = std::filesystem;
            const auto& e = dossiers::actuel();
            std::vector<tailles::Racine> out;
            const auto ajouter = [&out](std::string libelle, const std::string& chemin) {
                if (!chemin.empty()) out.push_back({std::move(libelle), chemin});
            };
            const auto sous = [](const std::string& base, std::string_view nom) {
                return base.empty() ? std::string() : dossiers::utf8De(dossiers::cheminDe(base) / dossiers::cheminDe(nom));
            };
            // Le programme : son dossier (l'exe, ses DLL, les ressources livrees, l'aide).
            std::string programme = e.programme;
            if (programme.empty())
                if (const std::string self = core::process::selfPath(); !self.empty())
                    programme = dossiers::utf8De(dossiers::cheminDe(self).parent_path());
            ajouter("Programme install\xC3\xA9", programme);
            // Les reglages de l'utilisateur, communs aux deux applications (%APPDATA%\XpgAnalyzer).
            const std::string partage = e.reglages.empty() ? Settings::sharedFolder() : e.reglages;
            ajouter("R\xC3\xA9glages de l'utilisateur", partage);
            // Les donnees : installee, le dossier des donnees ; sinon (developpement, portable),
            // le dossier de travail n'est pas a l'application - ses dossiers a elle seulement.
            if (e.installe) ajouter("Donn\xC3\xA9" "es de l'application", e[dossiers::Cle::Donnees].chemin);
            ajouter("Projets", e[dossiers::Cle::Projets].chemin);
            ajouter("Biblioth\xC3\xA8que", e[dossiers::Cle::Bibliotheque].chemin);
            ajouter("Captures", e[dossiers::Cle::Captures].chemin);
            if (!e.installe) ajouter("Ressources", sous(e[dossiers::Cle::Donnees].chemin, "resources"));
            ajouter("Journaux", e.journaux);
            ajouter("Sauvegardes", e.sauvegardes);
            // Ce qui se trouve d'habitude dans une racine : nomme dans l'arbre.
            const std::string edition = dossiers::utf8De(fs::path(Settings::defaultPath()).parent_path());
            if (edition != partage) ajouter("R\xC3\xA9glages d'" + core::productName(), edition);
            ajouter("Assistant IA", ai::root());
            ajouter("Rapports de plantage", sous(edition, "crashs"));
            ajouter("Signalements", sous(edition, "signalements"));
            ajouter("Reprise apr\xC3\xA8s un arr\xC3\xAAt", sous(edition, "reprise"));
            ajouter("Mod\xC3\xA8les de vues", sous(edition, "modeles-vues"));
            ajouter("Th\xC3\xA8mes", sous(partage, "themes"));
            ajouter("Mes images", sous(partage, "images"));
            ajouter("Fen\xC3\xAAtres et presse-papiers partag\xC3\xA9", dossiers::utf8De(fenetres::dossier()));
            // Les fichiers temporaires de l'application (le didacticiel, les exports sans projet).
            std::error_code ec;
            const fs::path temp = fs::temp_directory_path(ec);
            if (!ec && !temp.empty()) {
                ajouter("Bac \xC3\xA0 sable du didacticiel", dossiers::utf8De(temp / "xpg-bac-a-sable"));
                ajouter("Exports temporaires", dossiers::utf8De(temp / "xpg-exports"));
            }
            return out;
        }

        // UN PARCOURS SUR UN FIL DE FOND. Le fil ne connait que cet etat, partage avec
        // la fenetre : elle peut partir avant lui sans laisser de pointeur pendant.
        struct Scan {
            std::atomic<bool>                     arret{false};
            std::atomic<bool>                     fini{false};
            tailles::Progression                  progression;
            std::mutex                            m;
            std::string                           enCours;    // le libelle de la racine parcourue
            std::shared_ptr<const tailles::Bilan> bilan;      // a la fin
            std::string                           erreur;     // une exception du fil (la memoire)
        };

        void travail(Scan& s, const std::vector<tailles::Racine>& candidates) {
            try {
                tailles::Options o;
                o.arret = &s.arret;
                o.progression = &s.progression;
                auto b = std::make_shared<tailles::Bilan>(tailles::mesurer(candidates, o, tailles::Tri::Poids,
                    [&s](std::size_t, const tailles::Racine& r) {
                        const std::lock_guard<std::mutex> lock(s.m);
                        s.enCours = r.libelle;
                    }));
                const std::lock_guard<std::mutex> lock(s.m);
                s.bilan = std::move(b);
            } catch (const std::exception& e) {
                const std::lock_guard<std::mutex> lock(s.m);
                s.erreur = e.what();
            } catch (...) {
                const std::lock_guard<std::mutex> lock(s.m);
                s.erreur = "erreur inconnue";
            }
            s.fini.store(true, std::memory_order_release);
        }

        // Les fils dont la fenetre est partie avant la fin (fermee, Actualiser) : le
        // drapeau les arrete a l'element suivant, et ils sont joints des qu'ils ont
        // fini - le fil de l'interface n'attend jamais (un partage reseau qui ne repond
        // plus peut tenir une lecture plusieurs secondes). A la sortie du programme,
        // ceux qui restent sont joints.
        class FilsEnFin {
        public:
            static FilsEnFin& tous() {
                static FilsEnFin f;
                return f;
            }
            void confier(std::thread t, std::shared_ptr<Scan> s) {
                const std::lock_guard<std::mutex> lock(m_);
                balayer();
                restants_.emplace_back(std::move(t), std::move(s));
            }
            void nettoyer() {
                const std::lock_guard<std::mutex> lock(m_);
                balayer();
            }
            ~FilsEnFin() {
                for (auto& [t, s] : restants_) {
                    s->arret.store(true);
                    if (t.joinable()) t.join();
                }
            }
            FilsEnFin(const FilsEnFin&) = delete;
            FilsEnFin& operator=(const FilsEnFin&) = delete;
        private:
            FilsEnFin() = default;
            void balayer() {
                for (auto it = restants_.begin(); it != restants_.end();) {
                    if (it->second->fini.load(std::memory_order_acquire)) {
                        if (it->first.joinable()) it->first.join();
                        it = restants_.erase(it);
                    } else {
                        ++it;
                    }
                }
            }
            std::mutex                                                   m_;
            std::vector<std::pair<std::thread, std::shared_ptr<Scan>>> restants_;
        };

        bool memeNom(const std::string& a, const std::string& b) {
#if defined(_WIN32)
            if (a.size() != b.size()) return false;
            for (std::size_t i = 0; i < a.size(); ++i) {
                const auto low = [](char c) { return c >= 'A' && c <= 'Z' ? static_cast<char>(c - 'A' + 'a') : c; };
                if (low(a[i]) != low(b[i])) return false;
            }
            return true;
#else
            return a == b;
#endif
        }

        // L'ARBRE : le bilan a plat, un noeud par ligne. Les identifiants ne bougent pas
        // quand le tri change (seules les listes d'enfants sont triees) : ce qui est
        // deplie le reste.
        class FilesModel final : public ui::ITreeModel {
        public:
            static constexpr ui::NodeId kRacine = 1;

            explicit FilesModel(std::shared_ptr<const tailles::Bilan> b) : bilan_(std::move(b)) {
                lignes_.push_back({});   // la racine invisible
                if (!bilan_) return;
                for (std::size_t i = 0; i < bilan_->racines.size(); ++i) {
                    const ui::NodeId id = ajouter(&bilan_->racines[i].arbre, kRacine, i, true);
                    lignes_[0].enfants.push_back(id);
                }
                // Les dossiers connus a l'interieur d'une racine : leur nom dans l'arbre.
                for (const auto& x : bilan_->incluses) {
                    if (!x.trouvee || x.dans >= lignes_[0].enfants.size()) continue;
                    ui::NodeId n = lignes_[0].enfants[x.dans];
                    for (const auto& nom : x.sousChemin) {
                        ui::NodeId suivant = 0;
                        for (const auto c : ligne(n).enfants)
                            if (ligne(c).noeud && ligne(c).noeud->dossier && memeNom(ligne(c).noeud->nom, nom)) {
                                suivant = c;
                                break;
                            }
                        n = suivant;
                        if (n == 0) break;
                    }
                    if (n == 0) continue;
                    auto& note = notes_[n];
                    note += (note.empty() ? "" : ", ") + x.racine.libelle;
                }
            }

            [[nodiscard]] ui::NodeId  root() const override { return kRacine; }
            [[nodiscard]] std::size_t childCount(ui::NodeId n) const override { return valide(n) ? ligne(n).enfants.size() : 0; }
            [[nodiscard]] ui::NodeId  childAt(ui::NodeId n, std::size_t i) const override {
                return valide(n) && i < ligne(n).enfants.size() ? ligne(n).enfants[i] : 0;
            }
            [[nodiscard]] bool        hasChildren(ui::NodeId n) const override { return childCount(n) > 0; }
            [[nodiscard]] std::string text(ui::NodeId n) const override {
                if (!valide(n) || n == kRacine) return {};
                const auto& l = ligne(n);
                if (l.tete) return bilan_->racines[l.racine].racine.libelle;
                if (l.noeud) return l.noeud->nom;
                const auto* p = ligne(l.parent).noeud;
                const std::uint64_t k = p ? p->nonDetailles : 0;
                return "\xE2\x80\xA6 et " + tailles::milliers(k) + (k > 1 ? " autres fichiers (non d\xC3\xA9taill\xC3\xA9s)" : " autre fichier (non d\xC3\xA9taill\xC3\xA9)");
            }
            [[nodiscard]] ui::CellStyle style(ui::NodeId n) const override {
                ui::CellStyle st;
                if (!valide(n) || n == kRacine) return st;
                const auto& l = ligne(n);
                if (!l.noeud) {
                    const auto* p = ligne(l.parent).noeud;
                    st.fgTone = ui::Tone::Muted;
                    if (p) st.badge = dossiers::tailleLisible(p->octetsNonDetailles);
                    return st;
                }
                st.icon = l.noeud->dossier ? ui::Icon::Folder : ui::Icon::Document;
                st.badge = dossiers::tailleLisible(l.noeud->octets);
                if (l.tete) {
                    st.bold = true;
                    st.badgeTone = ui::Tone::Accent;
                }
                std::string hint;
                if (l.noeud->dossier) hint = tailles::nombreDeFichiers(l.noeud->fichiers);
                if (const auto it = notes_.find(n); it != notes_.end()) hint += (hint.empty() ? "" : " \xC2\xB7 ") + it->second;
                st.hint = std::move(hint);
                return st;
            }

            // Les enfants de chaque dossier dans l'ordre demande (les racines gardent le leur).
            void trier(tailles::Tri t) {
                for (std::size_t i = 1; i < lignes_.size(); ++i) {
                    auto& e = lignes_[i].enfants;
                    std::stable_sort(e.begin(), e.end(), [this, t](ui::NodeId a, ui::NodeId b) {
                        const auto* x = ligne(a).noeud;
                        const auto* y = ligne(b).noeud;
                        if (!x || !y) return x != nullptr;          // "... et N autres" a la fin
                        return tailles::avant(*x, *y, t);
                    });
                }
                modelReset->emit();
            }

            // Le chemin complet d'une ligne (UTF-8) ; "... et N autres" : celui de son dossier.
            [[nodiscard]] std::string chemin(ui::NodeId n) const {
                if (!valide(n) || n == kRacine) return {};
                if (!ligne(n).noeud) n = ligne(n).parent;
                std::vector<const std::string*> noms;
                while (valide(n) && n != kRacine && !ligne(n).tete) {
                    noms.push_back(&ligne(n).noeud->nom);
                    n = ligne(n).parent;
                }
                if (!valide(n) || n == kRacine) return {};
                std::filesystem::path p = dossiers::cheminDe(bilan_->racines[ligne(n).racine].racine.chemin);
                for (auto it = noms.rbegin(); it != noms.rend(); ++it) p /= dossiers::cheminDe(**it);
                return dossiers::utf8De(p.make_preferred());
            }
            // Le dossier a ouvrir : le sien, ou celui qui contient le fichier.
            [[nodiscard]] std::string dossierDe(ui::NodeId n) const {
                if (!valide(n) || n == kRacine) return {};
                const auto& l = ligne(n);
                if (l.noeud && !l.noeud->dossier) return chemin(l.parent);
                return chemin(n);
            }
            [[nodiscard]] std::string infobulle(ui::NodeId n) const {
                if (!valide(n) || n == kRacine) return {};
                const auto& l = ligne(n);
                std::string out = chemin(n);
                if (l.noeud) {
                    out += "\n" + dossiers::tailleLisible(l.noeud->octets);
                    if (l.noeud->octets >= 1024) out += " (" + tailles::milliers(l.noeud->octets) + " octets)";
                    if (l.noeud->dossier) out += " dans " + tailles::nombreDeFichiers(l.noeud->fichiers);
                } else {
                    out += "\nAu-del\xC3\xA0 de " + tailles::milliers(tailles::Options{}.maxNoeuds)
                         + " \xC3\xA9l\xC3\xA9ments, les fichiers sont compt\xC3\xA9s sans \xC3\xAAtre list\xC3\xA9s.";
                }
                if (const auto it = notes_.find(n); it != notes_.end()) out += "\nDossier de l'application : " + it->second;
                return out;
            }
            [[nodiscard]] const std::vector<ui::NodeId>& tetes() const { return lignes_[0].enfants; }

        private:
            struct Ligne {
                const tailles::Noeud*   noeud{nullptr};   // nul : "... et N autres fichiers" de `parent`
                ui::NodeId              parent{0};
                std::size_t             racine{0};        // son rang dans Bilan::racines
                bool                    tete{false};      // une racine (le premier niveau)
                std::vector<ui::NodeId> enfants;
            };
            [[nodiscard]] bool valide(ui::NodeId n) const { return n >= 1 && n <= lignes_.size(); }
            [[nodiscard]] const Ligne& ligne(ui::NodeId n) const { return lignes_[static_cast<std::size_t>(n - 1)]; }

            ui::NodeId ajouter(const tailles::Noeud* noeud, ui::NodeId parent, std::size_t racine, bool tete) {
                Ligne l;
                l.noeud = noeud;
                l.parent = parent;
                l.racine = racine;
                l.tete = tete;
                lignes_.push_back(std::move(l));
                const ui::NodeId id = lignes_.size();
                for (const auto& e : noeud->enfants) {
                    const ui::NodeId c = ajouter(&e, id, racine, false);
                    lignes_[static_cast<std::size_t>(id - 1)].enfants.push_back(c);
                }
                if (noeud->nonDetailles > 0) {
                    Ligne reste;
                    reste.parent = id;
                    reste.racine = racine;
                    lignes_.push_back(std::move(reste));
                    lignes_[static_cast<std::size_t>(id - 1)].enfants.push_back(lignes_.size());
                }
                return id;
            }

            std::shared_ptr<const tailles::Bilan>       bilan_;
            std::vector<Ligne>                          lignes_;
            std::unordered_map<ui::NodeId, std::string> notes_;   // "Assistant IA" sur le dossier IA
        };

        menu::MenuTraits aboutTraits() {
            menu::MenuTraits t;
            t.kind = menu::MenuKind::Dialog;
            t.rendersBelow = true;
            t.updatesBelow = true;      // une simulation en marche continue dessous
            t.blocksInput = true;
            t.dimsBelow = true;
            return t;
        }

        constexpr gfx::FontId kTitle{17};
        constexpr gfx::FontId kSection{17};
        constexpr gfx::FontId kTotal{20};
        constexpr gfx::FontId kBody{15};
        constexpr gfx::FontId kSmall{13};

        std::string fit(gfx::IRenderer& r, const std::string& s, gfx::FontId f, float w) {
            if (w <= 8.f) return {};
            if (r.measure(s, f).width <= w) return s;
            std::string t = s;
            while (!t.empty() && r.measure(t + "\xE2\x80\xA6", f).width > w) {
                while (!t.empty() && (static_cast<unsigned char>(t.back()) & 0xC0) == 0x80) t.pop_back();   // 1.44.0 : un caractere entier
                if (!t.empty()) t.pop_back();
            }
            return t + "\xE2\x80\xA6";
        }

        double secondes() {
            return std::chrono::duration<double>(std::chrono::steady_clock::now().time_since_epoch()).count();
        }

        class AboutWindow;

        // Le corps : l'en-tete (logo, nom, version), les informations (elles defilent),
        // les fichiers de l'application (le total, l'arbre), les boutons.
        class AboutBody final : public ui::Widget {
        public:
            AboutBody(AboutWindow& owner, App& app);
            ~AboutBody() override { arreter(); }
            AboutBody(const AboutBody&) = delete;
            AboutBody& operator=(const AboutBody&) = delete;

            void lancer();
            void poll();
            [[nodiscard]] bool menuOpen() const { return menu_ && menu_->isOpen(); }

        protected:
            void onLayout() override;
            void onPaint(const ui::PaintContext& ctx) override;
            ui::EventResult onEvent(const ui::InputEvent& ev) override;

        private:
            void arreter();
            void montrer();
            void syncButtons();
            void ouvrir(ui::NodeId n);
            void copierResume();
            void basculerTri();
            void dire(std::string texte, bool erreur = false) {
                flash_ = std::move(texte);
                flashErreur_ = erreur;
                flashJusque_ = secondes() + 5.0;
                invalidate();
            }

            AboutWindow&                          owner_;
            App&                                  app_;
            ui::MultiLineText*                    info_{nullptr};
            ui::TreeView*                         tree_{nullptr};
            ui::Button*                           ouvrir_{nullptr};
            ui::Button*                           tri_{nullptr};
            ui::Button*                           actualiser_{nullptr};
            ui::Button*                           copier_{nullptr};
            ui::Button*                           support_{nullptr};
            ui::Button*                           fermer_{nullptr};
            ui::PopupMenu*                        menu_{nullptr};
            std::shared_ptr<FilesModel>           model_;
            std::shared_ptr<const tailles::Bilan> bilan_;
            std::string                           erreur_;
            std::shared_ptr<Scan>                 scan_;
            std::thread                           thread_;
            tailles::Tri                          triCourant_{tailles::Tri::Poids};
            ui::NodeId                            menuNode_{0};
            std::string                           flash_;
            bool                                  flashErreur_{false};
            double                                flashJusque_{0.0};
            gfx::Rect                             panel_{}, close_{}, logo_{}, treeBox_{};
            float                                 sectionY_{0.f}, statusY_{0.f};
            core::ConnectionScope                 links_;
        };

        class AboutWindow final : public menu::WidgetMenu {
        public:
            explicit AboutWindow(App& app) : menu::WidgetMenu("dialog.about"), app_(app) {}
            [[nodiscard]] menu::MenuTraits traits() const override { return aboutTraits(); }
            [[nodiscard]] std::string title() const override { return "\xC3\x80 propos d'" + core::productName(); }
            void Update(const menu::FrameContext& f) override {
                menu::WidgetMenu::Update(f);
                if (body_) body_->poll();
            }
            ui::EventResult HandleEvent(const ui::InputEvent& ev) override {
                if (const auto* k = std::get_if<ui::KeyDown>(&ev); k && k->key == ui::Key::Escape && !(body_ && body_->menuOpen())) {
                    fermer();
                    return ui::EventResult::Consumed;
                }
                return menu::WidgetMenu::HandleEvent(ev);
            }
            void fermer() {
                if (done_) return;
                done_ = true;
                manager().CloseDialog(menu::DialogResult{menu::DialogResult::Button::Cancel, {}});
            }

        protected:
            core::Status buildUi() override {
                auto body = std::make_unique<AboutBody>(*this, app_);
                body_ = body.get();
                setRoot(std::move(body));
                body_->lancer();   // la fenetre s'ouvre tout de suite ; le calcul part sur son fil
                return core::ok();
            }

        private:
            App&       app_;
            AboutBody* body_{nullptr};
            bool       done_{false};
        };

        AboutBody::AboutBody(AboutWindow& owner, App& app) : ui::Widget("dialog.about.body"), owner_(owner), app_(app) {
            auto info = std::make_unique<ui::MultiLineText>("dialog.about.info");
            info->setText(detailsText(app_));
            info->setReadOnly(true);
            info_ = &static_cast<ui::MultiLineText&>(addChild(std::move(info)));

            tree_ = &static_cast<ui::TreeView&>(addChild(std::make_unique<ui::TreeView>("dialog.about.fichiers")));
            tree_->setShowRootNode(false);
            tree_->setVisibility(ui::Visibility::Hidden);   // montre a la fin du calcul
            tree_->setNodeTooltip([this](ui::NodeId n) { return model_ ? model_->infobulle(n) : std::string(); });
            links_ += tree_->activated->connect([this](ui::NodeId n) {
                if (model_ && model_->hasChildren(n)) tree_->toggle(n);
            });
            links_ += tree_->selectionChanged->connect([this](ui::NodeId) { syncButtons(); });
            links_ += tree_->contextMenuRequested->connect([this](ui::NodeId n, gfx::Point at) {
                if (!model_ || n == ui::kInvalidNode) return;
                menuNode_ = n;
                tree_->setCurrentNode(n);
                syncButtons();
                std::vector<ui::PopupMenu::Item> items;
                ui::PopupMenu::Item head;
                head.heading = true;
                head.label = model_->text(n);
                items.push_back(std::move(head));
                // 1.24.1 : comme partout ou un chemin se montre - Ouvrir dans l'explorateur, Aller a, Copier le chemin
                // (ui::paths) ; sans l'ecran du projet pour les faire : les deux entrees d'avant.
                if (auto pathItems = ui::paths::menuItems(model_->chemin(n), kAboutPathId); !pathItems.empty()) {
                    for (auto& it : pathItems) items.push_back(std::move(it));
                } else {
                    items.push_back({"Ouvrir le dossier", "", "", ui::Icon::FolderOpen, true, false, 1});
                    items.push_back({"Copier le chemin", "", "", ui::Icon::Copy, true, false, 2});
                }
                menu_->setItems(std::move(items));
                menu_->openAtSurface(at);   // `at` est dans la surface (Widget.hpp)
            });

            const auto bouton = [this](const char* texte, const char* id, ui::Icon icone) {
                auto b = std::make_unique<ui::Button>(texte, id);
                b->setIcon(icone);
                return &static_cast<ui::Button&>(addChild(std::move(b)));
            };
            ouvrir_ = bouton("Ouvrir le dossier", "dialog.about.ouvrir", ui::Icon::FolderOpen);
            tri_ = bouton("Trier par nom", "dialog.about.tri", ui::Icon::Sort);
            actualiser_ = bouton("Actualiser", "dialog.about.actualiser", ui::Icon::Refresh);
            copier_ = bouton("Copier le r\xC3\xA9sum\xC3\xA9", "dialog.about.resume", ui::Icon::Copy);
            ouvrir_->setTooltip("Ouvre dans l'Explorateur le dossier choisi dans l'arbre (celui du fichier choisi).");
            tri_->setTooltip("Les fichiers par poids d\xC3\xA9" "croissant, ou par nom (les dossiers d'abord).");
            actualiser_->setTooltip("Refait le compte des fichiers (sur un fil de fond).");
            copier_->setTooltip("Met dans le presse-papiers le poids total, et celui de chaque dossier avec son chemin.");
            links_ += ouvrir_->clicked->connect([this] { ouvrir(tree_->currentNode()); });
            links_ += tri_->clicked->connect([this] { basculerTri(); });
            links_ += actualiser_->clicked->connect([this] { lancer(); });
            links_ += copier_->clicked->connect([this] { copierResume(); });

            support_ = &static_cast<ui::Button&>(addChild(std::make_unique<ui::Button>("Copier les informations pour le support", "dialog.about.support")));
            links_ += support_->clicked->connect([this] {
                ui::setClipboardText(supportInformation(app_));
                dire("Informations pour le support copi\xC3\xA9" "es.");
                app_.events().publish(StatusNotice{
                    "Informations pour le support copi\xC3\xA9" "es : version, syst\xC3\xA8me, dossiers, projet (sans ses donn\xC3\xA9" "es).",
                    6.0});
            });
            fermer_ = &static_cast<ui::Button&>(addChild(std::make_unique<ui::Button>("Fermer", "dialog.about.fermer")));
            fermer_->setStyle(ui::Button::Style::Primary);
            links_ += fermer_->clicked->connect([this] { owner_.fermer(); });

            // Le menu du clic droit de l'arbre, par-dessus tout (le dernier enfant).
            menu_ = &static_cast<ui::PopupMenu&>(addChild(std::make_unique<ui::PopupMenu>("dialog.about.menu")));
            links_ += menu_->itemChosen->connect([this](int a) {
                if (!model_) return;
                if (ui::paths::owns(a, kAboutPathId)) {   // 1.24.1
                    if (const std::string no = ui::paths::run(a, kAboutPathId, model_->chemin(menuNode_)); !no.empty()) dire(no, true);
                    return;
                }
                if (a == 1) ouvrir(menuNode_);
                if (a == 2) {
                    ui::setClipboardText(model_->chemin(menuNode_));
                    dire("Chemin copi\xC3\xA9 : " + model_->chemin(menuNode_));
                }
            });
            syncButtons();
        }

        void AboutBody::lancer() {
            arreter();
            FilsEnFin::tous().nettoyer();
            model_.reset();
            bilan_.reset();
            erreur_.clear();
            tree_->setModel(nullptr);
            tree_->setVisibility(ui::Visibility::Hidden);
            auto s = std::make_shared<Scan>();
            std::vector<tailles::Racine> candidates = racinesCandidates();
            try {
                thread_ = std::thread([s, candidates = std::move(candidates)] { travail(*s, candidates); });
                scan_ = std::move(s);
            } catch (const std::exception& e) {
                erreur_ = std::string("le calcul n'a pas pu d\xC3\xA9marrer (") + e.what() + ")";
            }
            syncButtons();
            invalidate();
        }

        // Le drapeau, puis le join : tout de suite si le fil a fini, sinon des qu'il aura
        // fini (FilsEnFin) - jamais en attendant ici.
        void AboutBody::arreter() {
            if (scan_) scan_->arret.store(true, std::memory_order_relaxed);
            if (thread_.joinable()) {
                if (!scan_ || scan_->fini.load(std::memory_order_acquire)) thread_.join();
                else FilsEnFin::tous().confier(std::move(thread_), scan_);
            }
            scan_.reset();
        }

        // A chaque image : le compteur avance ; a la fin, l'arbre.
        void AboutBody::poll() {
            if (flashJusque_ > 0.0 && secondes() > flashJusque_) {
                flashJusque_ = 0.0;
                flash_.clear();
                invalidate();
            }
            if (!scan_) return;
            if (!scan_->fini.load(std::memory_order_acquire)) {
                invalidate();
                return;
            }
            {
                const std::lock_guard<std::mutex> lock(scan_->m);
                bilan_ = scan_->bilan;
                erreur_ = scan_->erreur;
            }
            if (thread_.joinable()) thread_.join();   // il a fini : ne bloque pas
            scan_.reset();
            montrer();
        }

        void AboutBody::montrer() {
            if (bilan_) {
                model_ = std::make_shared<FilesModel>(bilan_);
                if (triCourant_ != tailles::Tri::Poids) model_->trier(triCourant_);
                tree_->setModel(model_);
                // Replies au depart, sauf le premier niveau : chaque racine montre ce qu'elle contient.
                for (const auto n : model_->tetes()) tree_->expand(n);
                tree_->setVisibility(ui::Visibility::Visible);
            }
            syncButtons();
            invalidateLayout();
            invalidate();
        }

        void AboutBody::syncButtons() {
            const bool pret = static_cast<bool>(model_);
            ouvrir_->setEnabled(pret);
            copier_->setEnabled(pret);
            tri_->setEnabled(pret);
            ouvrir_->setTooltip(pret && tree_->currentNode() != ui::kInvalidNode
                                    ? "Ouvre " + model_->dossierDe(tree_->currentNode())
                                    : std::string("Ouvre dans l'Explorateur le dossier choisi dans l'arbre (la premi\xC3\xA8re racine si rien n'est choisi)."));
        }

        void AboutBody::ouvrir(ui::NodeId n) {
            if (!model_) return;
            if (n == ui::kInvalidNode && !model_->tetes().empty()) n = model_->tetes().front();
            const std::string chemin = model_->dossierDe(n);
            if (chemin.empty()) return;
            const std::string pourquoi = dossiers::ouvrirDansLeSysteme(chemin);
            if (pourquoi.empty()) dire("Ouvert : " + chemin);
            else dire(pourquoi, true);
        }

        void AboutBody::copierResume() {
            if (!bilan_) return;
            const std::string entete = core::productName() + " " + XPG_ANALYZER_VERSION + " \xE2\x80\x94 les fichiers de l'application ("
                                     + maintenant() + ")";
            ui::setClipboardText(tailles::resume(*bilan_, entete));
            dire("R\xC3\xA9sum\xC3\xA9 copi\xC3\xA9 : " + dossiers::tailleLisible(bilan_->octets) + " dans "
                 + tailles::nombreDeFichiers(bilan_->fichiers) + ".");
        }

        void AboutBody::basculerTri() {
            triCourant_ = triCourant_ == tailles::Tri::Poids ? tailles::Tri::Nom : tailles::Tri::Poids;
            tri_->setText(triCourant_ == tailles::Tri::Poids ? "Trier par nom" : "Trier par poids");
            if (model_) {
                const auto ouverts = tree_->expandedNodes();
                const auto courant = tree_->currentNode();
                model_->trier(triCourant_);   // l'arbre se refait (modelReset)...
                tree_->setExpandedNodes(ouverts);   // ... deplie comme avant (les identifiants restent)
                if (courant != ui::kInvalidNode) tree_->setCurrentNode(courant);
            }
            invalidateLayout();
        }

        void AboutBody::onLayout() {
            const auto r = bounds();
            const float w = std::min(940.f, std::max(520.f, r.w - 40.f));
            const float h = std::min(800.f, std::max(460.f, r.h - 40.f));
            panel_ = ui::dialogframe::place(*this, {std::floor(r.x + (r.w - w) * 0.5f), std::floor(r.y + (r.h - h) * 0.5f), w, h});
            const float x = panel_.x + 16.f, W = panel_.w - 32.f;
            close_ = {panel_.right() - 36.f, panel_.y + 9.f, 24.f, 24.f};
            logo_ = {x, panel_.y + 54.f, 64.f, 64.f};
            float y = panel_.y + 132.f;
            // Les informations : environ un quart de la hauteur, l'arbre prend le reste.
            const float infoH = std::clamp((panel_.h - 132.f - 150.f - 56.f) * 0.3f, 64.f, 190.f);
            info_->setBounds({x, y, W, infoH});
            y += infoH + 14.f;
            sectionY_ = y;
            statusY_ = y + 28.f;
            y += 50.f;
            // Une fenetre etroite : les quatre boutons sur leur icone (le libelle en infobulle).
            // (La largeur avec libelle, comme Button::sizeHint : sans basculer le bouton,
            // qui redemanderait une mise en page.)
            float need = 0.f;
            for (auto* b : {ouvrir_, tri_, actualiser_, copier_})
                need += std::max(96.f, ui::measureWidth(b->text(), gfx::FontId{16}) + ui::lineHeight(gfx::FontId{16}) + 30.f) + 8.f;
            for (auto* b : {ouvrir_, tri_, actualiser_, copier_}) b->setCompact(need > W);
            float bx = x;
            for (auto* b : {ouvrir_, tri_, actualiser_, copier_}) {
                const float bw = b->compact() ? b->sizeHint().preferred.w : std::max(96.f, b->sizeHint().preferred.w);
                b->setBounds({bx, y, bw, 30.f});
                bx += bw + 8.f;
            }
            y += 40.f;
            const float bottom = panel_.bottom() - 52.f;
            treeBox_ = {x, y, W, std::max(40.f, bottom - 6.f - y)};
            tree_->setBounds(treeBox_);
            const float fw = std::max(110.f, fermer_->sizeHint().preferred.w);
            fermer_->setBounds({panel_.right() - 16.f - fw, panel_.bottom() - 44.f, fw, 32.f});
            const float sw = support_->sizeHint().preferred.w;
            support_->setBounds({fermer_->bounds().x - 10.f - sw, panel_.bottom() - 44.f, sw, 32.f});
        }

        void AboutBody::onPaint(const ui::PaintContext& ctx) {
            auto& r = ctx.r;
            const auto& c = ctx.theme.color;
            r.fillRect(panel_, c.panelBg);
            r.strokeRect(panel_, c.borderStrong, 1.f);
            // La barre de titre (elle deplace la fenetre) et la croix.
            r.fillRect({panel_.x, panel_.y, panel_.w, 42.f}, c.headerBg);
            r.drawText({panel_.x + 16.f, panel_.y + (42.f - r.lineHeight(kTitle)) * 0.5f},
                       fit(r, "\xC3\x80 propos d'" + core::productName(), kTitle, panel_.w - 70.f), kTitle, c.text);
            r.drawText({close_.x + 7.f, close_.y + 2.f}, "\xC3\x97", kTitle, c.textMuted);
            r.fillRect({panel_.x, panel_.y + 42.f, panel_.w, 1.f}, c.border);

            // L'en-tete : le logo, le nom de l'edition et sa version, la construction, le code.
            brand::drawLogo(r, logo_);
            const float tx = logo_.right() + 16.f, tw = panel_.right() - 16.f - tx;
            const auto big = ctx.theme.font.title;
            r.drawText({tx, logo_.y - 2.f}, fit(r, core::productName() + " " + XPG_ANALYZER_VERSION, big, tw), big, c.text);
            const float y2 = logo_.y + r.lineHeight(big) + 2.f;
            r.drawText({tx, y2}, fit(r, buildLine(), kBody, tw), kBody, c.textMuted);
            r.drawText({tx, y2 + r.lineHeight(kBody) + 3.f}, fit(r, codeLine(), kBody, tw), kBody, c.textMuted);

            // Les fichiers de l'application : le titre, le total (bien visible), l'etat.
            const float x = panel_.x + 16.f, W = panel_.w - 32.f;
            r.fillRect({x, sectionY_ - 7.f, W, 1.f}, c.border);
            r.drawText({x, sectionY_ + 2.f}, "Fichiers de l'application", kSection, c.text);
            const bool calcul = static_cast<bool>(scan_);
            std::string total;
            if (bilan_) total = "Poids total : " + dossiers::tailleLisible(bilan_->octets);
            else if (calcul) total = "Poids total : " + dossiers::tailleLisible(scan_->progression.octets.load(std::memory_order_relaxed)) + "\xE2\x80\xA6";
            if (!total.empty()) {
                const float ww = r.measure(total, kTotal).width;
                r.drawText({panel_.right() - 16.f - ww, sectionY_}, total, kTotal, bilan_ ? c.accent : c.textMuted);
            }
            std::string etat;
            std::string alerte;   // en couleur d'avertissement, apres l'etat
            if (calcul) {
                std::string enCours;
                {
                    const std::lock_guard<std::mutex> lock(scan_->m);
                    enCours = scan_->enCours;
                }
                etat = "Calcul en cours\xE2\x80\xA6 " + tailles::nombreDeFichiers(scan_->progression.fichiers.load(std::memory_order_relaxed)) + ", "
                     + dossiers::tailleLisible(scan_->progression.octets.load(std::memory_order_relaxed))
                     + (enCours.empty() ? std::string() : " \xC2\xB7 " + enCours);
            } else if (bilan_) {
                const auto n = bilan_->racines.size();
                etat = tailles::nombreDeFichiers(bilan_->fichiers) + " dans " + std::to_string(n) + (n > 1 ? " dossiers" : " dossier")
                     + " (chaque fichier compt\xC3\xA9 une fois, les liens symboliques non suivis)";
                if (bilan_->illisibles > 0)
                    alerte = " \xC2\xB7 " + tailles::milliers(bilan_->illisibles)
                           + (bilan_->illisibles > 1 ? " \xC3\xA9l\xC3\xA9ments illisibles (ignor\xC3\xA9s)" : " \xC3\xA9l\xC3\xA9ment illisible (ignor\xC3\xA9)");
                if (bilan_->annule) alerte += " \xC2\xB7 calcul interrompu";
            } else if (!erreur_.empty()) {
                alerte = "Le calcul n'a pas abouti : " + erreur_;
            }
            if (!flash_.empty()) {
                etat = flash_;
                alerte.clear();
            }
            const gfx::Color etatCol = !flash_.empty() && flashErreur_ ? c.error : (!flash_.empty() ? c.ok : c.textMuted);
            const std::string e1 = fit(r, etat, kSmall, W);
            r.drawText({x, statusY_}, e1, kSmall, etatCol);
            if (!alerte.empty()) {
                const float used = e1.empty() ? 0.f : r.measure(e1, kSmall).width;
                r.drawText({x + used, statusY_}, fit(r, alerte, kSmall, W - used), kSmall, erreur_.empty() ? c.warning : c.error);
            }

            // Pendant le calcul : l'arbre attend, un compteur et une barre qui va et vient.
            if (!model_) {
                r.fillRect(treeBox_, c.inputBg);
                r.strokeRect(treeBox_, c.border, 1.f);
                const float cy = treeBox_.y + treeBox_.h * 0.5f;
                if (calcul) {
                    const std::string l1 = "Calcul en cours\xE2\x80\xA6";
                    const std::string l2 = "Les fichiers sont compt\xC3\xA9s sur un fil de fond : la fen\xC3\xAAtre reste utilisable, "
                                           "la fermer arr\xC3\xAAte le calcul.";
                    r.drawText({treeBox_.x + (treeBox_.w - r.measure(l1, kSection).width) * 0.5f, cy - 34.f}, l1, kSection, c.text);
                    const std::string l2f = fit(r, l2, kSmall, treeBox_.w - 24.f);
                    r.drawText({treeBox_.x + (treeBox_.w - r.measure(l2f, kSmall).width) * 0.5f, cy - 8.f}, l2f, kSmall, c.textMuted);
                    const float bw = std::min(320.f, treeBox_.w - 40.f);
                    const gfx::Rect bar{treeBox_.x + (treeBox_.w - bw) * 0.5f, cy + 18.f, bw, 6.f};
                    r.fillRoundedRect(bar, c.border, 3.f);
                    const float phase = static_cast<float>(std::fmod(ctx.time * 0.8, 1.0));
                    const float seg = bw * 0.28f;
                    const float sx = bar.x + (bw - seg) * (phase < 0.5f ? phase * 2.f : 2.f - phase * 2.f);
                    r.fillRoundedRect({sx, bar.y, seg, bar.h}, c.accent, 3.f);
                } else if (!erreur_.empty()) {
                    const std::string l = fit(r, "Le calcul n'a pas abouti : " + erreur_, kBody, treeBox_.w - 24.f);
                    r.drawText({treeBox_.x + (treeBox_.w - r.measure(l, kBody).width) * 0.5f, cy - 8.f}, l, kBody, c.error);
                }
            }
        }

        ui::EventResult AboutBody::onEvent(const ui::InputEvent& ev) {
            if (const auto* d = std::get_if<ui::MouseDown>(&ev); d && close_.contains(d->pos)) {
                owner_.fermer();
                return ui::EventResult::Consumed;
            }
            return ui::EventResult::Ignored;
        }

    } // namespace

    std::string aboutText(const App& app) {
        std::string out;
        out += std::string(XPG_ANALYZER_NAME) + " " + XPG_ANALYZER_VERSION + "\n";
        out += buildLine() + "\n\n";
        out += detailsText(app);
        return out;
    }

    std::string supportInformation(const App& app) {
        std::string out;
        out += std::string(XPG_ANALYZER_NAME) + " " + XPG_ANALYZER_VERSION + "\n";
        out += "Construit le " + buildDate() + " (" + compilerName() + ")\n";
        out += "Syst\xC3\xA8me : " + systemName() + "\n";
        out += "Dossiers :\n" + foldersText();
        out += "Projet ouvert :\n" + projectText(app);
        return out;
    }

    void showAboutDialog(App& app) {
        app.menus().ShowDialog(std::make_unique<AboutWindow>(app), nullptr);
    }

} // namespace app
