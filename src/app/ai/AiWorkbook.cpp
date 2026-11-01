// =============================================================================
//  app/ai/AiWorkbook.cpp - voir AiWorkbook.hpp
// -----------------------------------------------------------------------------
//  LA DETECTION, EN CHIFFRES. Pour chaque feuille :
//
//    - la LIGNE D'EN-TETE : parmi les 40 premieres, celle qui a le plus
//      d'intitules reconnus (au moins deux), un intitule exact comptant plus
//      qu'un intitule qui contient le mot, une case numerique comptant contre ;
//    - LES VALEURS sous l'en-tete (400 lignes au plus) : par colonne, la part
//      des adresses d'E/S, des adresses memoire, des adresses IP, des types de
//      signal, des types de donnee ;
//    - LE SCORE de chaque genre = une part des valeurs + une part des intitules :
//        E/S     0,55 x max(adresses d'E/S, 0,9 x types DI/DO/AI/AO)
//                + repere 0,12, designation 0,08, type 0,10 (0,05 sans valeurs
//                de signal), adresse 0,10, rack/slot/voie/carte 0,12 (+ 0,05 si
//                deux d'entre eux), echelle ou unite 0,06 - plafond 0,5 ;
//        reseau  0,55 x (adresses IP + 0,1 x adresses MAC)
//                + IP 0,20, masque 0,08, passerelle 0,08, VLAN 0,06, MAC 0,06,
//                protocole 0,05, port 0,04, un nom d'equipement 0,08 - plafond 0,5 ;
//        reports 0,60 x adresses memoire (%M, %MW, %MD, %MF, MW, DB..DBW)
//                + repere 0,10, designation 0,05, adresse ou mot 0,10, sens 0,10,
//                bit 0,05, types de donnee 0,06, << report >>, << echange >>,
//                << %MW >>... dans le nom ou le titre de la feuille 0,10 -
//                plafond 0,45 ;
//    - LE GENRE est le meilleur score s'il atteint kMinScore (0,35), sinon
//      << autre >> ; la CONFIANCE est ce score (borne a 1), reduite de 15 %
//      quand le second genre le suit a moins de 0,1.
//  Le classeur prend la feuille la plus sure (puis la plus longue).
// =============================================================================
#include "AiWorkbook.hpp"

#include "../../core/AtomicFile.hpp"
#include "../../import/XmlReader.hpp"
#include "../../ui/TextSearch.hpp"
#include "../../xls/Workbook.hpp"

#include "../../../third_party/miniz.h"

#include <algorithm>
#include <cctype>
#include <cstdlib>
#include <cstring>
#include <filesystem>
#include <initializer_list>
#include <map>
#include <set>
#include <unordered_map>

namespace app::ai::sheets {

namespace fs = std::filesystem;

namespace {

std::string_view localName(std::string_view n) {
    const auto colon = n.rfind(':');
    return colon == std::string_view::npos ? n : n.substr(colon + 1);
}

std::string trim(std::string_view s) {
    std::size_t a = 0, b = s.size();
    while (a < b && std::isspace(static_cast<unsigned char>(s[a]))) ++a;
    while (b > a && std::isspace(static_cast<unsigned char>(s[b - 1]))) --b;
    return std::string(s.substr(a, b - a));
}

// Les majuscules sans blancs : << %mw 100 >> -> << %MW100 >>.
std::string compact(std::string_view s) {
    std::string out;
    out.reserve(s.size());
    for (const char ch : s)
        if (!std::isspace(static_cast<unsigned char>(ch))) out += static_cast<char>(std::toupper(static_cast<unsigned char>(ch)));
    return out;
}

bool isDigit(char c) { return c >= '0' && c <= '9'; }
bool allDigits(std::string_view s) { return !s.empty() && std::all_of(s.begin(), s.end(), isDigit); }

// Le texte d'un element lu tel quel (entre sa balise ouvrante et sa fermante), les entites decodees.
std::string elementText(std::string_view raw) {
    const auto open = raw.find('>');
    const auto close = raw.rfind('<');
    if (open == std::string_view::npos || close == std::string_view::npos || close <= open) return {};
    return importer::XmlReader::decodeEntities(raw.substr(open + 1, close - open - 1));
}

// ---- un zip en memoire (miniz)
class Zip {
public:
    explicit Zip(std::string_view bytes) {
        std::memset(&zip_, 0, sizeof zip_);
        ok_ = bytes.size() >= 22 && mz_zip_reader_init_mem(&zip_, bytes.data(), bytes.size(), 0) != 0;
    }
    ~Zip() {
        if (ok_) mz_zip_reader_end(&zip_);
    }
    Zip(const Zip&) = delete;
    Zip& operator=(const Zip&) = delete;
    [[nodiscard]] bool ok() const noexcept { return ok_; }
    [[nodiscard]] bool has(const char* entry) { return ok_ && mz_zip_reader_locate_file(&zip_, entry, nullptr, 0) >= 0; }
    bool read(const char* entry, std::string& out, std::uint64_t max, bool* tooBig = nullptr) {
        if (tooBig) *tooBig = false;
        if (!ok_) return false;
        const int at = mz_zip_reader_locate_file(&zip_, entry, nullptr, 0);
        if (at < 0) return false;
        mz_zip_archive_file_stat st;
        if (!mz_zip_reader_file_stat(&zip_, static_cast<mz_uint>(at), &st)) return false;
        if (st.m_uncomp_size > max) {
            if (tooBig) *tooBig = true;
            return false;
        }
        std::size_t size = 0;
        void* raw = mz_zip_reader_extract_to_heap(&zip_, static_cast<mz_uint>(at), &size, 0);
        if (!raw) return false;
        out.assign(static_cast<const char*>(raw), size);
        mz_free(raw);
        return true;
    }

private:
    mz_zip_archive zip_{};
    bool           ok_{false};
};

bool isZip(std::string_view b) { return b.size() >= 4 && b.compare(0, 4, std::string_view("PK\x03\x04", 4)) == 0; }

std::string guill(std::string_view s) { return "\xC2\xAB " + std::string(s) + " \xC2\xBB"; }

// "A2:C5" -> une plage (faux : illisible).
bool parseRange(std::string_view ref, Merge& m) {
    const auto colon = ref.find(':');
    if (colon == std::string_view::npos) return false;
    const auto a = ref.substr(0, colon), b = ref.substr(colon + 1);
    if (a.empty() || b.empty() || !std::isdigit(static_cast<unsigned char>(a.back())) || !std::isdigit(static_cast<unsigned char>(b.back())))
        return false;
    m.r0 = xls::rowOf(a);
    m.c0 = xls::columnOf(a);
    m.r1 = xls::rowOf(b);
    m.c1 = xls::columnOf(b);
    if (m.r1 < m.r0) std::swap(m.r0, m.r1);
    if (m.c1 < m.c0) std::swap(m.c0, m.c1);
    return true;
}

// Les lignes d'une feuille A LEUR PLACE (l'attribut r de <row> et de <c>), ses plages fusionnees.
void parseRows(std::string_view xml, const std::vector<std::string>& shared, std::size_t maxRows, Sheet& s, bool& formulas) {
    importer::XmlReader r(xml);
    std::vector<std::string> cur;
    std::size_t row = 0, nextRow = 0, col = 0, nextCol = 0;
    std::string type, value;
    bool inV = false, inF = false, inT = false, skipping = false;
    constexpr std::size_t kMaxCols = 16384;
    while (auto ev = r.next()) {
        if (*ev == importer::XmlEvent::EndOfDocument) break;
        const auto name = localName(r.name());
        if (*ev == importer::XmlEvent::StartElement) {
            if (name == "row") {
                cur.clear();
                const std::string ra = r.attr("r");
                const long long n = ra.empty() ? 0 : std::atoll(ra.c_str());
                row = n > 0 ? static_cast<std::size_t>(n - 1) : nextRow;
                nextCol = 0;
                skipping = row >= maxRows;
                if (skipping) s.truncated = true;
            } else if (name == "c") {
                const std::string ref = r.attr("r");
                col = ref.empty() ? nextCol : xls::columnOf(ref);
                type = r.attr("t");
                value.clear();
            } else if (name == "v") {
                inV = true;
            } else if (name == "f") {
                inF = true;
                formulas = true;
            } else if (name == "t" && type == "inlineStr") {
                inT = true;
            } else if (name == "mergeCell") {
                Merge m;
                if (parseRange(r.attr("ref"), m) && s.merges.size() < 200000) s.merges.push_back(m);
            }
        } else if (*ev == importer::XmlEvent::Text) {
            if ((inV || inT) && !inF) value += importer::XmlReader::decodeEntities(r.text());
        } else if (*ev == importer::XmlEvent::EndElement) {
            if (name == "v") {
                inV = false;
            } else if (name == "t") {
                inT = false;
            } else if (name == "f") {
                inF = false;
            } else if (name == "c") {
                std::string text = value;
                if (type == "s") {
                    const auto idx = static_cast<std::size_t>(std::atoll(value.c_str()));
                    text = idx < shared.size() ? shared[idx] : std::string{};
                } else if (type == "b") {
                    text = value == "1" ? "VRAI" : value == "0" ? "FAUX" : value;
                }
                text = trim(text);
                if (!skipping && col < kMaxCols && !text.empty()) {
                    if (cur.size() <= col) cur.resize(col + 1);
                    cur[col] = std::move(text);
                }
                nextCol = col + 1;
                value.clear();
            } else if (name == "row") {
                if (!skipping) {
                    if (s.rows.size() <= row) s.rows.resize(row + 1);
                    s.rows[row] = std::move(cur);
                }
                cur.clear();
                nextRow = row + 1;
            }
        }
    }
    // les lignes vides de la fin ne comptent pas
    while (!s.rows.empty() && std::all_of(s.rows.back().begin(), s.rows.back().end(), [](const std::string& c) { return c.empty(); }))
        s.rows.pop_back();
}

// ---- les mots du metier, par role (ecrits replies : normalizeHeader les remet en forme une fois)
struct RoleWords {
    const char*              key;
    const char*              label;
    std::vector<const char*> words;
};
const std::vector<RoleWords>& roleWords() {
    static const std::vector<RoleWords> all = {
    {"repere", "Rep\xC3\xA8re",
     {"repere", "tag", "tag name", "tagname", "nom", "name", "mnemonique", "mnemonic", "mnemonique api", "symbole", "symbol", "variable",
      "nom variable", "nom de la variable", "nom de variable", "identifiant", "identifier", "code", "nom api", "nom automate", "instance",
      "signal name", "nom du signal", "nom signal", "point name", "repere fonctionnel", "repere api", "repere automate"}},
    {"designation", "D\xC3\xA9signation",
     {"designation", "description", "libelle", "label", "texte", "text", "intitule", "denomination", "fonction", "desc", "libelle long",
      "description du signal", "designation du signal"}},
    {"type", "Type",
     {"type", "type signal", "type de signal", "type e s", "type es", "type io", "io type", "i o type", "type i o", "nature", "nature signal",
      "nature du signal", "signal type", "type de donnee", "data type", "datatype", "format", "type variable", "type de variable", "e s", "i o"}},
    {"adresse", "Adresse",
     {"adresse", "address", "adr", "addr", "adresse automate", "adresse api", "adresse plc", "plc address", "topologie", "adresse topologique",
      "%i", "%q", "%m", "bit %m", "adresse %m", "adresse logique", "adresse physique", "adresse modbus", "adresse memoire", "memory address",
      "adresse e s", "adresse es", "io address"}},
    {"mot", "Mot (%MW)",
     {"mot", "word", "mot %mw", "%mw", "registre", "register", "adresse mot", "n mot", "numero mot", "numero de mot", "index mot",
      "holding register", "registre modbus"}},
    {"bit", "Bit", {"bit", "n bit", "numero bit", "numero de bit", "rang", "rang bit", "bit n"}},
    {"rack", "Rack", {"rack", "chassis", "baie", "n rack", "numero rack", "rack n", "numero de rack"}},
    {"slot", "Emplacement (slot)", {"slot", "emplacement", "position", "n emplacement", "pos", "slot n", "n slot", "numero emplacement"}},
    {"voie", "Voie",
     {"voie", "channel", "canal", "n voie", "numero voie", "numero de voie", "voie n", "channel n", "ch", "n canal", "entree sortie n"}},
    {"carte", "Carte / module",
     {"carte", "card", "module", "coupleur", "reference carte", "ref carte", "type carte", "type de carte", "module e s", "carte e s",
      "io module", "module io", "reference module", "ref module"}},
    {"min", "\xC3\x89" "chelle min",
     {"min", "mini", "minimum", "echelle min", "echelle mini", "valeur min", "valeur mini", "range min", "low", "plage basse", "bas echelle",
      "bas d echelle", "echelle basse", "lo", "min echelle", "scale min", "low range", "min physique"}},
    {"max", "\xC3\x89" "chelle max",
     {"max", "maxi", "maximum", "echelle max", "echelle maxi", "valeur max", "valeur maxi", "range max", "high", "plage haute", "haut echelle",
      "haut d echelle", "echelle haute", "hi", "max echelle", "scale max", "high range", "max physique"}},
    {"unite", "Unit\xC3\xA9", {"unite", "unit", "ui", "eu", "engineering unit", "unite physique", "unite de mesure"}},
    {"equipement", "\xC3\x89quipement",
     {"equipement", "equipment", "appareil", "device", "station", "host", "hostname", "nom d hote", "nom hote", "machine", "poste", "automate",
      "plc", "materiel", "noeud", "node", "nom equipement", "nom de l equipement", "nom de equipement", "designation equipement",
      "device name", "nom appareil"}},
    {"ip", "Adresse IP",
     {"ip", "adresse ip", "ip address", "ipv4", "adresse ipv4", "addr ip", "ip adresse", "adr ip", "ip v4", "adresse ip v4"}},
    {"masque", "Masque",
     {"masque", "mask", "subnet", "subnet mask", "netmask", "masque sous reseau", "masque de sous reseau", "sous reseau", "masque reseau",
      "masque ip"}},
    {"passerelle", "Passerelle",
     {"passerelle", "gateway", "gw", "default gateway", "passerelle par defaut", "routeur", "router", "passerelle ip"}},
    {"vlan", "VLAN", {"vlan", "vlan id", "id vlan", "n vlan", "numero vlan"}},
    {"protocole", "Protocole",
     {"protocole", "protocol", "bus", "liaison", "communication", "type de liaison", "type liaison", "type de communication",
      "protocole de communication", "reseau de communication"}},
    {"port", "Port", {"port", "port tcp", "tcp port", "port udp", "udp port", "port ip", "n port", "numero port"}},
    {"mac", "Adresse MAC", {"mac", "adresse mac", "mac address", "adresse physique mac"}},
    {"esclave", "Esclave (Unit Id)",
     {"esclave", "unit id", "slave", "slave id", "id esclave", "n esclave", "numero esclave", "adresse esclave", "id modbus", "unit identifier"}},
    {"sens", "Sens (lecture/\xC3\xA9" "criture)",
     {"sens", "direction", "lecture ecriture", "l e", "r w", "rw", "acces", "access", "read write", "lecture", "ecriture", "sens d echange",
      "sens echange", "type d acces", "type acces", "mode d acces"}},
    {"commentaire", "Commentaire", {"commentaire", "comment", "remarque", "observation", "note", "remark", "info", "information", "notes"}},
    };
    return all;
}

struct Keyword {
    std::string              text;
    std::vector<std::string> tokens;
};
struct RoleTable {
    std::vector<RoleInfo>                 infos;
    std::vector<std::vector<Keyword>>     words;   // par role (l'ordre de infos)
};

std::vector<std::string> tokensOf(const std::string& norm) {
    std::vector<std::string> out;
    std::size_t i = 0;
    while (i < norm.size()) {
        while (i < norm.size() && norm[i] == ' ') ++i;
        std::size_t j = i;
        while (j < norm.size() && norm[j] != ' ') ++j;
        if (j > i) out.push_back(norm.substr(i, j - i));
        i = j;
    }
    return out;
}

const RoleTable& table() {
    static const RoleTable t = [] {
        RoleTable rt;
        for (const auto& rw : roleWords()) {
            rt.infos.push_back({rw.key, rw.label});
            std::vector<Keyword> list;
            for (const char* w : rw.words) {
                Keyword k;
                k.text = normalizeHeader(w);
                k.tokens = tokensOf(k.text);
                if (!k.text.empty()) list.push_back(std::move(k));
            }
            rt.words.push_back(std::move(list));
        }
        return rt;
    }();
    return t;
}

bool containsTokens(const std::vector<std::string>& hay, const std::vector<std::string>& needle) {
    if (needle.empty() || needle.size() > hay.size()) return false;
    for (std::size_t i = 0; i + needle.size() <= hay.size(); ++i) {
        bool ok = true;
        for (std::size_t k = 0; k < needle.size() && ok; ++k) ok = hay[i + k] == needle[k];
        if (ok) return true;
    }
    return false;
}

// Le texte d'en-tete d'une case : le sien, ou celui de la plage fusionnee qui la couvre.
std::string headerAt(const Sheet& s, std::size_t r, std::size_t c) {
    const std::string& own = s.cell(r, c);
    if (!own.empty()) return own;
    for (const auto& m : s.merges)
        if (r >= m.r0 && r <= m.r1 && c >= m.c0 && c <= m.c1 && (m.r0 != r || m.c0 != c)) return s.cell(m.r0, m.c0);
    return {};
}

std::size_t headerWidth(const Sheet& s, std::size_t r) {
    std::size_t w = r < s.rows.size() ? s.rows[r].size() : 0;
    for (const auto& m : s.merges)
        if (r >= m.r0 && r <= m.r1) w = std::max(w, m.c1 + 1);
    return std::min<std::size_t>(w, 512);
}

bool numeric(std::string_view v) {
    if (v.empty()) return false;
    char* end = nullptr;
    const std::string s(v);
    (void)std::strtod(s.c_str(), &end);
    return end && *end == '\0';
}

// Les colonnes d'une ligne d'en-tete : un role par colonne, une colonne par role (la plus sure ; a egalite, la premiere).
std::vector<Column> headerMapping(const Sheet& s, std::size_t row) {
    struct Cand {
        std::size_t col;
        std::string role, header;
        int         score;
    };
    std::vector<Cand> cands;
    const std::size_t w = headerWidth(s, row);
    for (std::size_t c = 0; c < w; ++c) {
        const std::string h = headerAt(s, row, c);
        if (h.empty()) continue;
        auto [role, score] = roleOfHeader(h);
        if (score <= 0) continue;
        cands.push_back({c, role, h, score});
    }
    std::vector<Column> out;
    std::set<std::size_t> used;
    std::stable_sort(cands.begin(), cands.end(), [](const Cand& a, const Cand& b) { return a.score > b.score; });
    for (const auto& c : cands) {
        if (used.count(c.col)) continue;
        if (std::any_of(out.begin(), out.end(), [&](const Column& x) { return x.role == c.role; })) continue;
        out.push_back({c.role, c.col, c.header});
        used.insert(c.col);
    }
    std::sort(out.begin(), out.end(), [](const Column& a, const Column& b) { return a.col < b.col; });
    return out;
}

// Ce qu'il y a sous l'en-tete, colonne par colonne.
struct Stats {
    std::size_t              dataRows{0};
    std::vector<std::size_t> nonEmpty, io, mem, ip, mac, sig, dtype;
    void grow(std::size_t w) {
        for (auto* v : {&nonEmpty, &io, &mem, &ip, &mac, &sig, &dtype})
            if (v->size() < w) v->resize(w, 0);
    }
    [[nodiscard]] double frac(const std::vector<std::size_t>& v, std::size_t c) const {
        return dataRows == 0 || c >= v.size() ? 0.0 : static_cast<double>(v[c]) / static_cast<double>(dataRows);
    }
    [[nodiscard]] std::pair<double, std::size_t> best(const std::vector<std::size_t>& v) const {
        double b = 0;
        std::size_t at = 0;
        for (std::size_t c = 0; c < v.size(); ++c)
            if (frac(v, c) > b) {
                b = frac(v, c);
                at = c;
            }
        return {b, at};
    }
};

Stats statsUnder(const Sheet& s, std::size_t from, std::size_t limit = 400) {
    Stats st;
    for (std::size_t r = from; r < s.rows.size() && st.dataRows < limit; ++r) {
        const auto& row = s.rows[r];
        if (std::all_of(row.begin(), row.end(), [](const std::string& c) { return c.empty(); })) continue;
        ++st.dataRows;
        st.grow(row.size());
        for (std::size_t c = 0; c < row.size(); ++c) {
            const std::string& v = row[c];
            if (v.empty()) continue;
            ++st.nonEmpty[c];
            if (isIoAddress(v)) ++st.io[c];
            else if (isMemoryAddress(v)) ++st.mem[c];
            else if (isIpv4(v)) ++st.ip[c];
            else if (isMac(v)) ++st.mac[c];
            if (!signalOf(v).empty()) ++st.sig[c];
            if (!dataTypeOf(v).empty()) ++st.dtype[c];
        }
    }
    return st;
}

// Un role libre recoit la colonne libre ou ses valeurs dominent (au moins la moitie des lignes).
void claim(Layout& L, const Sheet& s, std::size_t headerRow, const Stats& st, const std::vector<std::size_t>& counts, std::string_view role,
           double minFrac = 0.5) {
    if (L.has(role)) return;
    double best = 0;
    int at = -1;
    for (std::size_t c = 0; c < counts.size(); ++c) {
        if (std::any_of(L.columns.begin(), L.columns.end(), [&](const Column& x) { return x.col == c; })) continue;
        const double f = st.frac(counts, c);
        if (f > best) {
            best = f;
            at = static_cast<int>(c);
        }
    }
    if (at >= 0 && best >= minFrac) L.set(role, at, headerAt(s, headerRow, static_cast<std::size_t>(at)));
}

// Un role dont la colonne porte d'autres valeurs que les siennes passe a son vrai role (une << Adresse >> pleine d'IP).
void move(Layout& L, const Stats& st, const std::vector<std::size_t>& counts, std::string_view from, std::string_view to, double minFrac = 0.6) {
    const int c = L.colOf(from);
    if (c < 0 || L.has(to)) return;
    if (st.frac(counts, static_cast<std::size_t>(c)) < minFrac) return;
    for (auto& x : L.columns)
        if (x.role == from) x.role = std::string(to);
}

Rules defaultRules(Genre g, const Layout& L, const Stats& st) {
    Rules r;
    r.keyRoles = defaultKeyRoles(g);
    // une colonne de rack, d'emplacement, de carte (ou d'equipement) remplie une fois par groupe : recopiee vers le bas
    for (const char* role : {"rack", "slot", "carte", "equipement"}) {
        const int c = L.colOf(role);
        if (c < 0 || st.dataRows < 4) continue;
        const double f = st.frac(st.nonEmpty, static_cast<std::size_t>(c));
        if (f > 0.0 && f < 0.6) r.carryDown.emplace_back(role);
    }
    return r;
}

bool titleSays(const Sheet& s, std::size_t headerRow, std::initializer_list<const char*> words) {
    std::string all = normalizeHeader(s.name);
    for (std::size_t r = 0; r < headerRow && r < s.rows.size(); ++r)
        for (const auto& c : s.rows[r])
            if (!c.empty() && c.size() < 120) all += " " + normalizeHeader(c);
    const auto toks = tokensOf(all);
    for (const char* w : words) {
        const auto need = tokensOf(normalizeHeader(w));
        if (containsTokens(toks, need)) return true;
        // un nom de feuille colle ("REPORTS_TOR") : le mot au debut d'un jeton
        for (const auto& t : toks)
            if (need.size() == 1 && t.size() > need[0].size() && t.rfind(need[0], 0) == 0) return true;
    }
    return false;
}

std::string percent(double v) { return std::to_string(static_cast<int>(v * 100.0 + 0.5)) + " %"; }

std::string countText(std::size_t n, std::string_view what, std::size_t of) {
    return std::to_string(n) + " " + std::string(what) + " sur " + std::to_string(of) + (of > 1 ? " lignes" : " ligne");
}

} // namespace

// ------------------------------------------------------------------ le classeur ---
const std::string& Sheet::cell(std::size_t r, std::size_t c) const noexcept {
    static const std::string none;
    if (r >= rows.size() || c >= rows[r].size()) return none;
    return rows[r][c];
}

std::size_t Sheet::width() const noexcept {
    std::size_t w = 0;
    for (const auto& r : rows) w = std::max(w, r.size());
    return w;
}

const Sheet* Workbook::sheet(std::string_view name) const noexcept {
    for (const auto& s : sheets)
        if (s.name == name) return &s;
    const std::string want = ui::foldForSearch(name);
    for (const auto& s : sheets)
        if (ui::foldForSearch(s.name) == want) return &s;
    return nullptr;
}

bool workbookExt(std::string_view ext) noexcept {
    if (ext.size() != 4) return false;
    std::string e(ext);
    for (auto& c : e) c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
    return e == "xlsx" || e == "xlsm";
}

std::string columnLetter(std::size_t col) {
    std::string out;
    std::size_t n = col + 1;
    while (n > 0) {
        const std::size_t rem = (n - 1) % 26;
        out.insert(out.begin(), static_cast<char>('A' + rem));
        n = (n - 1) / 26;
    }
    return out;
}

bool readWorkbook(std::string_view bytes, Workbook& out, std::string* why, const ReadLimits& L) {
    out = Workbook{};
    const auto fail = [&](std::string w) {
        if (why) *why = std::move(w);
        return false;
    };
    if (!isZip(bytes)) return fail("pas un classeur lisible : ce n'est pas une archive (fichier ab\xC3\xAEm\xC3\xA9 ou mal nomm\xC3\xA9 ; un .xls ancien n'est pas lu)");
    Zip zip(bytes);
    if (!zip.ok()) return fail("archive ab\xC3\xAEm\xC3\xA9" "e : le classeur ne s'ouvre pas");
    std::string wb, rels, sst;
    if (!zip.read("xl/workbook.xml", wb, L.maxXmlBytes)) return fail("xl/workbook.xml absent : l'archive s'ouvre mais ce n'est pas un classeur");
    (void)zip.read("xl/_rels/workbook.xml.rels", rels, L.maxXmlBytes);
    (void)zip.read("xl/sharedStrings.xml", sst, L.maxXmlBytes);
    out.macros = zip.has("xl/vbaProject.bin");
    // rId -> la feuille (une cible absolue part de la racine du paquet)
    std::map<std::string, std::string> targets;
    {
        importer::XmlReader r(rels);
        while (auto ev = r.next()) {
            if (*ev == importer::XmlEvent::EndOfDocument) break;
            if (*ev != importer::XmlEvent::StartElement || localName(r.name()) != "Relationship") continue;
            std::string t = r.attr("Target");
            if (!t.empty() && t[0] == '/') t.erase(0, 1);
            else if (!t.empty()) t = "xl/" + t;
            targets[r.attr("Id")] = t;
        }
    }
    struct Entry {
        std::string name, path;
        bool        hidden{false};
    };
    std::vector<Entry> entries;
    {
        importer::XmlReader r(wb);
        while (auto ev = r.next()) {
            if (*ev == importer::XmlEvent::EndOfDocument) break;
            if (*ev != importer::XmlEvent::StartElement || localName(r.name()) != "sheet") continue;
            Entry e;
            e.name = r.attr("name");
            const std::string state = r.attr("state");
            e.hidden = state == "hidden" || state == "veryHidden";
            std::string rid = r.attr("r:id");
            if (rid.empty())
                for (const auto& a : r.attributes())
                    if (a.name.size() >= 3 && a.name.substr(a.name.size() - 3) == ":id") rid = std::string(a.rawValue);
            if (const auto it = targets.find(rid); it != targets.end()) e.path = it->second;
            entries.push_back(std::move(e));
        }
    }
    if (entries.empty()) return fail("aucune feuille dans le classeur");
    // les chaines partagees : chaque <si>, ses <t> mis bout a bout (pas la phonetique)
    std::vector<std::string> shared;
    {
        importer::XmlReader r(sst);
        bool inSi = false, inPh = false;
        std::string cur;
        while (auto ev = r.next()) {
            if (*ev == importer::XmlEvent::EndOfDocument) break;
            const auto name = localName(r.name());
            if (*ev == importer::XmlEvent::StartElement) {
                if (name == "si") {
                    inSi = true;
                    cur.clear();
                } else if (name == "t" && inSi && !inPh && !r.selfClosing()) {
                    if (auto raw = r.readElementRaw()) cur += elementText(*raw);
                } else if (name == "rPh") {
                    inPh = true;
                }
            } else if (*ev == importer::XmlEvent::EndElement) {
                if (name == "rPh") inPh = false;
                else if (name == "si") {
                    shared.push_back(cur);
                    inSi = false;
                }
            }
        }
    }
    for (const auto& e : entries) {
        if (out.sheets.size() >= L.maxSheets) {
            ++out.skippedSheets;
            continue;
        }
        std::string xml;
        bool tooBig = false;
        if (e.path.empty() || !zip.read(e.path.c_str(), xml, L.maxXmlBytes, &tooBig)) {
            out.notes.push_back("feuille " + guill(e.name) + (tooBig ? " trop volumineuse : non lue" : " illisible"));
            continue;
        }
        Sheet s;
        s.name = e.name;
        s.hidden = e.hidden;
        parseRows(xml, shared, L.maxRows, s, out.formulas);
        if (s.truncated) out.notes.push_back("feuille " + guill(e.name) + " : " + std::to_string(L.maxRows) + " lignes lues au plus (la limite)");
        out.sheets.push_back(std::move(s));
    }
    if (out.skippedSheets > 0) out.notes.push_back(std::to_string(out.skippedSheets) + " feuille(s) de plus non lue(s) (la limite)");
    return true;
}

bool readWorkbookFile(const std::string& path, Workbook& out, std::string* why, const ReadLimits& L) {
    std::string bytes;
    if (path.empty() || !core::readFileAll(fs::path(std::u8string(path.begin(), path.end())), bytes)) {
        out = Workbook{};
        if (why) *why = "fichier introuvable ou illisible";
        return false;
    }
    return readWorkbook(bytes, out, why, L);
}

// ------------------------------------------------------------------- les genres ---
std::string_view genreKey(Genre g) noexcept {
    switch (g) {
        case Genre::IoList: return "es";
        case Genre::Network: return "reseau";
        case Genre::Exchange: return "reports";
        case Genre::Other: break;
    }
    return "autre";
}

std::optional<Genre> genreFromKey(std::string_view key) noexcept {
    for (const Genre g : kGenres)
        if (genreKey(g) == key) return g;
    return std::nullopt;
}

std::string genreLabel(Genre g) {
    switch (g) {
        case Genre::IoList: return "Liste d'entr\xC3\xA9" "es/sorties";
        case Genre::Network: return "R\xC3\xA9seau et adressage IP";
        case Genre::Exchange: return "Reports / table d'\xC3\xA9" "change";
        case Genre::Other: break;
    }
    return "Autre";
}

std::string genreShort(Genre g) {
    switch (g) {
        case Genre::IoList: return "E/S";
        case Genre::Network: return "R\xC3\xA9seau";
        case Genre::Exchange: return "Reports";
        case Genre::Other: break;
    }
    return "Autre";
}

// -------------------------------------------------------------------- les roles ---
const std::vector<RoleInfo>& roles() { return table().infos; }

std::string roleLabel(std::string_view key) {
    for (const auto& r : roles())
        if (r.key == key) return std::string(r.label);
    return std::string(key);
}

bool knownRole(std::string_view key) noexcept {
    for (const auto& rw : roleWords())
        if (key == rw.key) return true;
    return false;
}

std::vector<std::string> rolesFor(Genre g) {
    switch (g) {
        case Genre::IoList:
            return {"repere", "designation", "type", "adresse", "rack", "slot", "voie", "carte", "min", "max", "unite", "commentaire"};
        case Genre::Network:
            return {"equipement", "repere", "designation", "ip", "masque", "passerelle", "vlan", "protocole", "port", "esclave", "mac", "commentaire"};
        case Genre::Exchange:
            return {"repere", "designation", "type", "adresse", "mot", "bit", "sens", "min", "max", "unite", "commentaire"};
        case Genre::Other: break;
    }
    std::vector<std::string> all;
    for (const auto& r : roles()) all.emplace_back(r.key);
    return all;
}

std::string normalizeHeader(std::string_view text) {
    const std::string f = ui::foldForSearch(text);
    std::string spaced;
    spaced.reserve(f.size());
    for (const char ch : f) {
        const auto c = static_cast<unsigned char>(ch);
        if ((c >= 'a' && c <= 'z') || (c >= '0' && c <= '9') || c == '%') spaced += ch;
        else if (c >= 'A' && c <= 'Z') spaced += static_cast<char>(c - 'A' + 'a');
        else spaced += ' ';
    }
    std::string out;
    for (auto t : tokensOf(spaced)) {
        // au singulier : adresses -> adresse, canaux -> canal (pas << process >>, pas les mots courts)
        if (t.size() > 4 && t.compare(t.size() - 3, 3, "aux") == 0) t.replace(t.size() - 3, 3, "al");
        else if (t.size() > 3 && (t.back() == 's' || t.back() == 'x') && t[t.size() - 2] != 's' && !(t.back() == 's' && t[t.size() - 2] == 'u')) t.pop_back();   // pas modbus, status
        if (!out.empty()) out += ' ';
        out += t;
    }
    return out;
}

std::pair<std::string, int> roleOfHeader(std::string_view text) {
    const std::string norm = normalizeHeader(text);
    if (norm.empty() || norm.size() > 48) return {{}, 0};
    const auto toks = tokensOf(norm);
    const auto& t = table();
    std::string best;
    int score = 0;
    for (std::size_t i = 0; i < t.infos.size(); ++i)
        for (const auto& k : t.words[i]) {
            int s = 0;
            if (norm == k.text) s = 1000 + static_cast<int>(k.text.size());
            else if (toks.size() <= 5 && containsTokens(toks, k.tokens)) s = 500 + static_cast<int>(k.text.size());
            if (s > score) {
                score = s;
                best = std::string(t.infos[i].key);
            }
        }
    return {best, score};
}

// ------------------------------------------------------------- les valeurs lues ---
bool isIoAddress(std::string_view v) {
    std::string s = compact(v);
    if (s.size() < 2 || s.size() > 32) return false;
    const bool pct = s[0] == '%';
    if (pct) s.erase(0, 1);
    bool periph = false;
    if (!pct && s.size() > 2 && s[0] == 'P' && (s[1] == 'I' || s[1] == 'Q' || s[1] == 'E' || s[1] == 'A')) {
        s.erase(0, 1);
        periph = true;
    }
    if (s.empty()) return false;
    const char z = s[0];
    if (!(z == 'I' || z == 'Q' || (!pct && (z == 'E' || z == 'A')))) return false;
    s.erase(0, 1);
    char size = 0;
    if (!s.empty() && (s[0] == 'X' || s[0] == 'B' || s[0] == 'W' || s[0] == 'D')) {
        size = s[0];
        s.erase(0, 1);
    }
    if (s.empty() || !isDigit(s.front()) || !isDigit(s.back())) return false;
    if (pct) return std::all_of(s.begin(), s.end(), [](char c) { return isDigit(c) || c == '.' || c == ':' || c == '/' || c == '\\'; });
    // sans % (I0.0, Q4.1, IW64, PEW256) : un bit de 0 a 7 apres un point, ou un mot
    if (size == 'W' || (periph && (size == 'W' || size == 'D' || size == 'B'))) return allDigits(s);
    if (size != 0) return false;
    const auto dot = s.find('.');
    if (dot == std::string::npos) return false;
    const std::string a = s.substr(0, dot), b = s.substr(dot + 1);
    return allDigits(a) && b.size() == 1 && b[0] >= '0' && b[0] <= '7';
}

bool isMemoryAddress(std::string_view v) {
    std::string s = compact(v);
    if (s.size() < 2 || s.size() > 32) return false;
    // Siemens : DB10.DBW2, DB1.DBX0.0, DB3.DBD8
    if (s.rfind("DB", 0) == 0) {
        const auto dot = s.find('.');
        if (dot == std::string::npos || !allDigits(s.substr(2, dot - 2))) return false;
        std::string rest = s.substr(dot + 1);
        if (rest.size() < 4 || rest.rfind("DB", 0) != 0) return false;
        const char sz = rest[2];
        if (sz != 'X' && sz != 'B' && sz != 'W' && sz != 'D') return false;
        rest = rest.substr(3);
        const auto d2 = rest.find('.');
        if (d2 == std::string::npos) return allDigits(rest) && sz != 'X';
        return sz == 'X' && allDigits(rest.substr(0, d2)) && rest.size() == d2 + 2 && rest.back() >= '0' && rest.back() <= '7';
    }
    const bool pct = s[0] == '%';
    if (pct) s.erase(0, 1);
    if (s.empty() || s[0] != 'M') return false;
    s.erase(0, 1);
    char size = 0;
    if (!s.empty() && (s[0] == 'X' || s[0] == 'B' || s[0] == 'W' || s[0] == 'D' || (pct && s[0] == 'F'))) {
        size = s[0];
        s.erase(0, 1);
    }
    if (s.empty() || !isDigit(s[0])) return false;
    std::size_t i = 0;
    while (i < s.size() && isDigit(s[i])) ++i;
    std::string tail = s.substr(i);
    if (tail.empty()) return pct || size == 'W' || size == 'D' || size == 'B';
    // un bit de mot : %MW100.3, %MW100:X3 ; Siemens M10.0
    if (tail.rfind(":X", 0) == 0) tail = tail.substr(2);
    else if (tail[0] == '.') tail = tail.substr(1);
    else return false;
    if (!allDigits(tail)) return false;
    if (!pct && size == 0) return tail.size() == 1 && tail[0] <= '7';
    return true;
}

bool isIpv4(std::string_view v) {
    std::string s = trim(v);
    if (const auto slash = s.find('/'); slash != std::string::npos) {
        const std::string bits = s.substr(slash + 1);
        if (!allDigits(bits) || bits.size() > 2 || std::atoi(bits.c_str()) > 32) return false;
        s.resize(slash);
    }
    int parts = 0;
    std::size_t i = 0;
    while (i <= s.size()) {
        std::size_t j = s.find('.', i);
        if (j == std::string::npos) j = s.size();
        const std::string p = s.substr(i, j - i);
        if (p.empty() || p.size() > 3 || !allDigits(p) || std::atoi(p.c_str()) > 255) return false;
        ++parts;
        i = j + 1;
        if (j == s.size()) break;
    }
    return parts == 4;
}

bool isMac(std::string_view v) {
    const std::string s = compact(v);
    const auto hex = [](char c) { return isDigit(c) || (c >= 'A' && c <= 'F'); };
    if (s.size() == 17) {
        const char sep = s[2];
        if (sep != ':' && sep != '-') return false;
        for (std::size_t i = 0; i < 17; ++i) {
            if (i % 3 == 2) {
                if (s[i] != sep) return false;
            } else if (!hex(s[i])) {
                return false;
            }
        }
        return true;
    }
    if (s.size() == 14 && s[4] == '.' && s[9] == '.') {   // aabb.ccdd.eeff
        for (std::size_t i = 0; i < 14; ++i)
            if (i != 4 && i != 9 && !hex(s[i])) return false;
        return true;
    }
    return false;
}

std::string signalOf(std::string_view v) {
    std::string k;
    for (const char ch : ui::foldForSearch(v))
        if (std::isalnum(static_cast<unsigned char>(ch))) k += static_cast<char>(std::toupper(static_cast<unsigned char>(ch)));
    if (k.empty() || k.size() > 24) return {};
    static const std::map<std::string, std::string> table = {
        {"DI", "DI"}, {"DE", "DI"}, {"ETOR", "DI"}, {"TORE", "DI"}, {"EL", "DI"}, {"ENTREETOR", "DI"}, {"DIGITALINPUT", "DI"}, {"BI", "DI"},
        {"DIN", "DI"}, {"ENTREELOGIQUE", "DI"}, {"ENTREETOUTOURIEN", "DI"}, {"ENTREENUMERIQUE", "DI"},
        {"DO", "DO"}, {"DS", "DO"}, {"STOR", "DO"}, {"TORS", "DO"}, {"SL", "DO"}, {"SORTIETOR", "DO"}, {"DIGITALOUTPUT", "DO"}, {"BO", "DO"},
        {"DOUT", "DO"}, {"SORTIELOGIQUE", "DO"}, {"SORTIETOUTOURIEN", "DO"}, {"SORTIENUMERIQUE", "DO"},
        {"AI", "AI"}, {"AE", "AI"}, {"EA", "AI"}, {"EANA", "AI"}, {"ANAE", "AI"}, {"ENTREEANA", "AI"}, {"ENTREEANALOGIQUE", "AI"},
        {"ANALOGINPUT", "AI"}, {"AIN", "AI"},
        {"AO", "AO"}, {"AS", "AO"}, {"SA", "AO"}, {"SANA", "AO"}, {"ANAS", "AO"}, {"SORTIEANA", "AO"}, {"SORTIEANALOGIQUE", "AO"},
        {"ANALOGOUTPUT", "AO"}, {"AOUT", "AO"},
        {"TOR", "TOR"}, {"ANA", "ANA"}, {"ANALOGIQUE", "ANA"}, {"LOGIQUE", "TOR"}, {"DIGITAL", "TOR"}, {"ANALOG", "ANA"}};
    const auto it = table.find(k);
    return it == table.end() ? std::string{} : it->second;
}

std::string dataTypeOf(std::string_view v) {
    const std::string k = compact(v);
    if (k.empty() || k.size() > 12) return {};
    static const std::map<std::string, std::string> table = {
        {"BOOL", "BOOL"}, {"EBOOL", "BOOL"}, {"BOOLEAN", "BOOL"}, {"BIT", "BOOL"},
        {"INT", "INT"}, {"SINT", "INT"}, {"INT16", "INT"}, {"INTEGER", "INT"}, {"ENTIER", "INT"},
        {"DINT", "DINT"}, {"INT32", "DINT"}, {"LONG", "DINT"},
        {"UINT", "UINT"}, {"UINT16", "UINT"}, {"USINT", "UINT"},
        {"UDINT", "UDINT"}, {"UINT32", "UDINT"},
        {"WORD", "WORD"}, {"DWORD", "DWORD"},
        {"REAL", "REAL"}, {"FLOAT", "REAL"}, {"FLOAT32", "REAL"}, {"REEL", "REAL"}, {"SINGLE", "REAL"},
        {"LREAL", "LREAL"}, {"DOUBLE", "LREAL"}, {"FLOAT64", "LREAL"},
        {"STRING", "STRING"}, {"CHAINE", "STRING"},
        {"TIME", "TIME"}};
    const auto it = table.find(k);
    return it == table.end() ? std::string{} : it->second;
}

std::string typeOfAddress(std::string_view v) {
    if (isMemoryAddress(v)) {
        std::string s = compact(v);
        if (s.rfind("DB", 0) == 0) {
            const auto dot = s.find(".DB");
            const char sz = dot != std::string::npos && dot + 3 < s.size() ? s[dot + 3] : 0;
            return sz == 'X' ? "BOOL" : sz == 'D' ? "DINT" : "INT";
        }
        if (s[0] == '%') s.erase(0, 1);
        const char sz = s.size() > 1 ? s[1] : 0;
        const bool bitOfWord = s.find(":X") != std::string::npos || s.find('.') != std::string::npos;
        if (sz == 'W' || sz == 'B') return bitOfWord ? "BOOL" : "INT";
        if (sz == 'D') return bitOfWord ? "BOOL" : "DINT";
        if (sz == 'F') return "REAL";
        return "BOOL";   // %M10, %MX10, M10.0
    }
    if (isIoAddress(v)) {
        std::string s = compact(v);
        if (s[0] == '%') s.erase(0, 1);
        if (s[0] == 'P') s.erase(0, 1);
        const char sz = s.size() > 1 ? s[1] : 0;
        if (sz == 'W' || sz == 'B') return "INT";   // %IW0.3.2 (rack, emplacement, voie) : un mot
        if (sz == 'D') return "DINT";
        return "BOOL";
    }
    return {};
}

// --------------------------------------------------------------- la mise en page ---
int Layout::colOf(std::string_view role) const noexcept {
    for (const auto& c : columns)
        if (c.role == role) return static_cast<int>(c.col);
    return -1;
}

void Layout::set(std::string_view role, int col, std::string header) {
    columns.erase(std::remove_if(columns.begin(), columns.end(), [&](const Column& c) { return c.role == role; }), columns.end());
    if (col < 0) return;
    // une colonne n'a qu'un role
    columns.erase(std::remove_if(columns.begin(), columns.end(), [&](const Column& c) { return c.col == static_cast<std::size_t>(col); }),
                  columns.end());
    columns.push_back({std::string(role), static_cast<std::size_t>(col), std::move(header)});
    std::sort(columns.begin(), columns.end(), [](const Column& a, const Column& b) { return a.col < b.col; });
}

std::vector<std::string> defaultKeyRoles(Genre g) {
    switch (g) {
        case Genre::IoList: return {"repere", "adresse"};
        case Genre::Network: return {"ip"};
        case Genre::Exchange: return {"repere", "adresse", "mot"};
        case Genre::Other: break;
    }
    return {};
}

std::string headerText(const Sheet& s, std::size_t row, std::size_t col) { return headerAt(s, row, col); }
std::size_t headerWidthOf(const Sheet& s, std::size_t row) { return headerWidth(s, row); }

std::vector<std::string> headerCells(const Sheet& s, std::size_t row) {
    std::vector<std::string> out;
    const std::size_t w = headerWidth(s, row);
    for (std::size_t c = 0; c < w; ++c) out.push_back(normalizeHeader(headerAt(s, row, c)));
    while (!out.empty() && out.back().empty()) out.pop_back();
    return out;
}

// ------------------------------------------------------------------ la detection ---
Layout layoutAt(const Sheet& s, std::size_t headerRow, Genre genre) {
    Layout L;
    L.genre = genre;
    L.sheet = s.name;
    L.headerRow = headerRow;
    L.columns = headerMapping(s, headerRow);
    const Stats st = statsUnder(s, headerRow + 1);
    // ce que disent les valeurs, selon le genre
    switch (genre) {
        case Genre::Network:
            move(L, st, st.ip, "adresse", "ip");
            claim(L, s, headerRow, st, st.ip, "ip");
            claim(L, s, headerRow, st, st.mac, "mac");
            break;
        case Genre::IoList:
            move(L, st, st.sig, "adresse", "type");   // une colonne << E/S >> de DI/DO, prise pour une adresse
            move(L, st, st.io, "type", "adresse");    // une colonne << E/S >> d'adresses %I, prise pour un type
            claim(L, s, headerRow, st, st.io, "adresse");
            claim(L, s, headerRow, st, st.sig, "type");
            break;
        case Genre::Exchange:
            move(L, st, st.mem, "type", "adresse");
            claim(L, s, headerRow, st, st.mem, "adresse");
            claim(L, s, headerRow, st, st.dtype, "type");
            // une seule colonne d'adresses, intitulee << Mot >> : elle est l'adresse
            if (!L.has("adresse") && L.has("mot")) {
                const int c = L.colOf("mot");
                if (st.frac(st.mem, static_cast<std::size_t>(c)) >= 0.5)
                    for (auto& x : L.columns)
                        if (x.role == "mot") x.role = "adresse";
            }
            break;
        case Genre::Other: break;
    }
    L.rules = defaultRules(genre, L, st);
    return L;
}

SheetDetection detectSheet(const Sheet& s, std::size_t index) {
    SheetDetection d;
    d.sheet = s.name;
    d.index = index;
    // 1. la ligne d'en-tete
    const std::size_t scan = std::min<std::size_t>(s.rows.size(), 40);
    int bestScore = 0;
    std::size_t headerRow = 0;
    for (std::size_t r = 0; r < scan; ++r) {
        const std::size_t w = headerWidth(s, r);
        std::set<std::string> found;
        int exact = 0, numbers = 0;
        for (std::size_t c = 0; c < w; ++c) {
            const std::string h = headerAt(s, r, c);
            if (h.empty()) continue;
            if (numeric(h)) {
                ++numbers;
                continue;
            }
            const auto [role, score] = roleOfHeader(h);
            if (score <= 0) continue;
            found.insert(role);
            if (score >= 1000) ++exact;
        }
        if (found.size() < 2) continue;
        const int sc = static_cast<int>(found.size()) * 10 + exact * 3 - numbers * 2;
        if (sc > bestScore) {
            bestScore = sc;
            headerRow = r;
        }
    }
    d.headerFound = bestScore > 0;
    if (!d.headerFound) {
        // sans en-tete reconnu : la premiere ligne non vide, et les valeurs seules
        while (headerRow < s.rows.size() && s.rows[headerRow].empty()) ++headerRow;
        if (headerRow >= s.rows.size()) headerRow = 0;
    }
    // 2. les valeurs sous l'en-tete
    const Stats st = statsUnder(s, d.headerFound ? headerRow + 1 : headerRow);
    d.dataRows = st.dataRows;
    Layout probe = layoutAt(s, headerRow, Genre::Other);
    if (!d.headerFound) probe.columns.clear();
    const auto has = [&](const char* r) { return probe.has(r) ? 1.0 : 0.0; };
    const auto [ioF, ioC] = st.best(st.io);
    const auto [memF, memC] = st.best(st.mem);
    const auto [ipF, ipC] = st.best(st.ip);
    const auto [macF, macC] = st.best(st.mac);
    const auto [sigF, sigC] = st.best(st.sig);
    const auto [dtF, dtC] = st.best(st.dtype);
    (void)macC;
    (void)dtC;
    // 3. les scores
    {
        const double content = std::max(ioF, 0.9 * sigF);
        double hdr = 0.12 * has("repere") + 0.08 * has("designation") + (probe.has("type") ? (sigF > 0.2 ? 0.10 : 0.05) : 0.0) + 0.10 * has("adresse");
        const double hw = has("rack") + has("slot") + has("voie") + has("carte");
        hdr += (hw > 0 ? 0.12 : 0.0) + (hw >= 2 ? 0.05 : 0.0) + (has("min") + has("max") + has("unite") > 0 ? 0.06 : 0.0);
        d.scores[static_cast<int>(Genre::IoList)] = 0.55 * content + std::min(hdr, 0.5);
    }
    {
        const double content = std::min(1.0, ipF + 0.1 * macF);
        double hdr = 0.20 * has("ip") + 0.08 * has("masque") + 0.08 * has("passerelle") + 0.06 * has("vlan") + 0.06 * has("mac") + 0.05 * has("protocole")
                     + 0.04 * has("port") + ((probe.has("equipement") || probe.has("repere")) ? 0.08 : 0.0);
        d.scores[static_cast<int>(Genre::Network)] = 0.55 * content + std::min(hdr, 0.5);
    }
    {
        const double content = memF;
        double hdr = 0.10 * has("repere") + 0.05 * has("designation") + ((probe.has("adresse") || probe.has("mot")) ? 0.10 : 0.0) + 0.10 * has("sens")
                     + 0.05 * has("bit") + (dtF > 0.3 ? 0.06 : 0.0);
        if (titleSays(s, headerRow, {"report", "echange", "exchange", "table d echange", "%mw", "%m", "modbus", "mot interne", "memoire"})) hdr += 0.10;
        d.scores[static_cast<int>(Genre::Exchange)] = 0.6 * content + std::min(hdr, 0.45);
    }
    // 4. le genre et sa confiance
    Genre best = Genre::Other;
    double bestS = 0, second = 0;
    for (const Genre g : {Genre::IoList, Genre::Network, Genre::Exchange}) {
        const double v = d.scores[static_cast<int>(g)];
        if (v > bestS) {
            second = bestS;
            bestS = v;
            best = g;
        } else if (v > second) {
            second = v;
        }
    }
    if (bestS < kMinScore) {
        d.genre = Genre::Other;
        d.confidence = std::clamp(1.0 - bestS, 0.3, 0.95);
        d.scores[static_cast<int>(Genre::Other)] = 1.0 - bestS;
    } else {
        d.genre = best;
        d.confidence = std::min(1.0, bestS) * (second > bestS - 0.1 ? 0.85 : 1.0);
    }
    d.confidence = static_cast<double>(static_cast<int>(d.confidence * 100.0 + 0.5)) / 100.0;
    d.layout = layoutAt(s, headerRow, d.genre);
    if (!d.headerFound) d.layout.columns.clear();
    // 5. pourquoi
    if (d.headerFound) {
        std::string list;
        std::size_t n = 0;
        for (const auto& c : d.layout.columns) {
            if (n++ >= 6) {
                list += ", \xE2\x80\xA6";
                break;
            }
            list += (list.empty() ? "" : ", ") + c.header;
        }
        d.reasons.push_back("en-t\xC3\xAA" "te ligne " + std::to_string(headerRow + 1) + " : " + list);
    } else {
        d.reasons.push_back("aucune ligne d'en-t\xC3\xAA" "te reconnue");
    }
    if (st.dataRows > 0) {
        if (ioF > 0) d.reasons.push_back(countText(st.io[ioC], "adresses d'E/S", st.dataRows));
        if (memF > 0) d.reasons.push_back(countText(st.mem[memC], "adresses m\xC3\xA9moire (%M, %MW...)", st.dataRows));
        if (ipF > 0) d.reasons.push_back(countText(st.ip[ipC], "adresses IP", st.dataRows));
        if (sigF > 0) d.reasons.push_back(countText(st.sig[sigC], "types de signal (DI, DO, AI, AO...)", st.dataRows));
    }
    if (d.genre != Genre::Other) d.reasons.push_back(genreLabel(d.genre) + " : confiance " + percent(d.confidence));
    return d;
}

Detection detect(const Workbook& wb) {
    Detection out;
    double bestKey = -1;
    for (std::size_t i = 0; i < wb.sheets.size(); ++i) {
        out.sheets.push_back(detectSheet(wb.sheets[i], i));
        out.sheets.back().hidden = wb.sheets[i].hidden;
        const auto& d = out.sheets.back();
        // la plus sure d'abord (un genre reconnu passe avant << autre >>), puis la plus longue ; une feuille masquee en dernier
        const double key = (d.genre != Genre::Other ? 10.0 + d.confidence : 0.0) + std::min<double>(static_cast<double>(d.dataRows), 9999.0) / 100000.0
                           - (wb.sheets[i].hidden ? 5.0 : 0.0);
        if (key > bestKey) {
            bestKey = key;
            out.best = i;
        }
    }
    return out;
}

// ------------------------------------------------------------------ lire les lignes ---
const std::string& Row::value(std::string_view role) const noexcept {
    static const std::string none;
    for (const auto& [r, v] : values)
        if (r == role) return v;
    return none;
}

Reading read(const Workbook& wb, const Layout& L, std::size_t maxRows) {
    Reading out;
    out.sheet = L.sheet;
    out.headerLine = L.headerRow + 1;
    out.columns = L.columns;
    const Sheet* s = wb.sheet(L.sheet);
    if (!s) {
        out.problems.push_back("feuille " + guill(L.sheet) + " absente du classeur");
        return out;
    }
    out.sheet = s->name;
    if (s->truncated) out.problems.push_back("feuille " + guill(s->name) + " coup\xC3\xA9" "e \xC3\xA0 la limite des lignes lues");
    // les cases fusionnees : la valeur du coin dans chaque case de la plage (sous l'en-tete)
    std::unordered_map<std::uint64_t, const std::string*> merged;
    const auto key = [](std::size_t r, std::size_t c) { return (static_cast<std::uint64_t>(r) << 20) | static_cast<std::uint64_t>(c); };
    if (L.rules.mergedCells) {
        std::size_t budget = 200000;
        for (const auto& m : s->merges) {
            if (m.r1 <= L.headerRow) continue;
            const std::string& v = s->cell(m.r0, m.c0);
            if (v.empty()) continue;
            for (std::size_t r = std::max(m.r0, L.headerRow + 1); r <= m.r1 && budget > 0; ++r)
                for (std::size_t c = m.c0; c <= m.c1 && budget > 0; ++c, --budget)
                    if (!(r == m.r0 && c == m.c0)) merged[key(r, c)] = &v;
        }
    }
    std::set<std::string> empties;
    for (const auto& e : L.rules.emptyValues) empties.insert(normalizeHeader(e).empty() ? e : normalizeHeader(e));
    const auto isEmptyValue = [&](const std::string& v) {
        if (v.empty()) return true;
        if (v.size() > 6) return false;   // << n/a >>, << neant >> : des mots courts
        if (empties.count(v)) return true;
        const std::string n = normalizeHeader(v);
        return !n.empty() && empties.count(n) > 0;
    };
    std::vector<std::string> keys = L.rules.keyRoles.empty() ? defaultKeyRoles(L.genre) : L.rules.keyRoles;
    keys.erase(std::remove_if(keys.begin(), keys.end(), [&](const std::string& k) { return !L.has(k); }), keys.end());
    std::map<std::string, std::string> last;   // la valeur du dessus (recopiee vers le bas)
    const std::size_t first = L.headerRow + 1 + L.rules.skipAfterHeader;
    for (std::size_t r = first; r < s->rows.size() && out.rows.size() < maxRows; ++r) {
        const auto& raw = s->rows[r];
        bool any = std::any_of(raw.begin(), raw.end(), [](const std::string& c) { return !c.empty(); });
        Row row;
        row.line = r + 1;
        bool mapped = false;
        for (const auto& col : L.columns) {
            std::string v = s->cell(r, col.col);
            if (v.empty() && L.rules.mergedCells)
                if (const auto it = merged.find(key(r, col.col)); it != merged.end()) v = *it->second;
            if (isEmptyValue(v)) v.clear();
            if (v.empty() && std::find(L.rules.carryDown.begin(), L.rules.carryDown.end(), col.role) != L.rules.carryDown.end() && any)
                v = last[col.role];
            else if (!v.empty())
                last[col.role] = v;
            mapped = mapped || !v.empty();
            row.values.emplace_back(col.role, std::move(v));
        }
        if (!any) {
            ++out.empty;
            continue;
        }
        const bool keyed = keys.empty() ? mapped : std::any_of(keys.begin(), keys.end(), [&](const std::string& k) { return !row.value(k).empty(); });
        if (!keyed) {
            ++out.ignored;
            if (out.ignoredLines.size() < 20) out.ignoredLines.push_back(row.line);
            continue;
        }
        out.rows.push_back(std::move(row));
    }
    // les lignes vides de la fin ne sont pas des lignes du tableau
    out.ok = true;
    return out;
}

} // namespace app::ai::sheets
