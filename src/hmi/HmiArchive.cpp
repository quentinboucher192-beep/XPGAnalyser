#include "HmiArchive.hpp"

#include "HmiCrypto.hpp"
#include "../../third_party/miniz.h"

#include <chrono>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <ctime>
#include <filesystem>
#include <fstream>
#include <map>
#include <set>
#include <system_error>

namespace fs = std::filesystem;

namespace hmi {

namespace {

// ---- un zip "stored", ecrit a la main -------------------------------------------
void put16(std::string& out, std::uint32_t v) {
    out += static_cast<char>(v & 0xFF);
    out += static_cast<char>((v >> 8) & 0xFF);
}
void put32(std::string& out, std::uint32_t v) {
    put16(out, v & 0xFFFF);
    put16(out, (v >> 16) & 0xFFFF);
}

struct DosTime { std::uint16_t time{0}, date{0}; };
DosTime dosNow() {
    const std::time_t t = std::chrono::system_clock::to_time_t(std::chrono::system_clock::now());
    std::tm tm{};
#ifdef _WIN32
    localtime_s(&tm, &t);
#else
    localtime_r(&t, &tm);
#endif
    DosTime d;
    d.time = static_cast<std::uint16_t>((tm.tm_hour << 11) | (tm.tm_min << 5) | (tm.tm_sec / 2));
    d.date = static_cast<std::uint16_t>(((tm.tm_year - 80) << 9) | ((tm.tm_mon + 1) << 5) | tm.tm_mday);
    return d;
}

struct ZipItem {
    std::string                  name;
    std::shared_ptr<const Bytes> data;
};

core::Status writeZip(const std::string& path, const std::vector<ZipItem>& items, std::uint64_t* written) {
    const DosTime when = dosNow();
    std::string central;
    std::ofstream out(path, std::ios::binary | std::ios::trunc);
    if (!out) return core::fail(core::ErrorCode::FileUnreadable, "\xC3\xA9" "criture impossible : " + path);
    std::uint32_t offset = 0;
    for (const auto& it : items) {
        const auto& data = *it.data;
        if (data.size() > 0xFFFFFFF0u || offset > 0xFFFFFFF0u)
            return core::fail(core::ErrorCode::InvalidArgument, "archive trop grande (zip 32 bits) : " + it.name);
        const auto crc = static_cast<std::uint32_t>(mz_crc32(MZ_CRC32_INIT, data.data(), data.size()));
        const auto size = static_cast<std::uint32_t>(data.size());
        std::string local;
        put32(local, 0x04034b50);
        put16(local, 20);            // version
        put16(local, 0x0800);        // noms en UTF-8
        put16(local, 0);             // stored
        put16(local, when.time);
        put16(local, when.date);
        put32(local, crc);
        put32(local, size);
        put32(local, size);
        put16(local, static_cast<std::uint32_t>(it.name.size()));
        put16(local, 0);
        local += it.name;
        out.write(local.data(), static_cast<std::streamsize>(local.size()));
        if (!data.empty()) out.write(reinterpret_cast<const char*>(data.data()), static_cast<std::streamsize>(data.size()));

        put32(central, 0x02014b50);
        put16(central, 20);
        put16(central, 20);
        put16(central, 0x0800);
        put16(central, 0);
        put16(central, when.time);
        put16(central, when.date);
        put32(central, crc);
        put32(central, size);
        put32(central, size);
        put16(central, static_cast<std::uint32_t>(it.name.size()));
        put16(central, 0);           // extra
        put16(central, 0);           // commentaire
        put16(central, 0);           // disque
        put16(central, 0);           // attributs internes
        put32(central, 0);           // attributs externes
        put32(central, offset);
        central += it.name;
        offset += static_cast<std::uint32_t>(local.size()) + size;
    }
    std::string end;
    put32(end, 0x06054b50);
    put16(end, 0);
    put16(end, 0);
    put16(end, static_cast<std::uint32_t>(items.size()));
    put16(end, static_cast<std::uint32_t>(items.size()));
    put32(end, static_cast<std::uint32_t>(central.size()));
    put32(end, offset);
    put16(end, 0);
    out.write(central.data(), static_cast<std::streamsize>(central.size()));
    out.write(end.data(), static_cast<std::streamsize>(end.size()));
    out.flush();
    if (!out) return core::fail(core::ErrorCode::FileUnreadable, "\xC3\xA9" "criture incompl\xC3\xA8" "te : " + path);
    if (written) *written = offset + central.size() + end.size();
    return core::ok();
}

std::string digestOf(const Bytes& data) { return toHex(sha256(data.data(), data.size())); }

std::shared_ptr<const Bytes> bytesOf(const std::string& text) {
    return std::make_shared<const Bytes>(text.begin(), text.end());
}

// Les sections du manifeste : leur cle (enregistree) et leur libelle (affiche).
struct SectionName { const char* key; const char* label; };
constexpr SectionName kSections[] = {
    {"vues", "Vues"}, {"objets", "Objets"}, {"calques", "Calques"}, {"ressources", "Ressources"},
    {"fichiers_externes", "Fichiers externes"}, {"scripts", "Scripts"}, {"variables", "Variables IHM"},
    {"fonctions", "Fonctions IHM"},                                      // lot 7 (absente des archives plus anciennes)
    {"actions", "Actions"}, {"alarmes", "Alarmes"}, {"recettes", "Recettes"}, {"jeux", "Jeux de recette"},
    {"utilisateurs", "Utilisateurs"}, {"groupes", "Groupes"}, {"roles", "R\xC3\xB4les"},
    {"historique", "Entr\xC3\xA9" "es d'historique"},
};

} // namespace

std::vector<ArchiveCount> projectCounts(const Project& p, const History* h) {
    const auto st = p.statistics();
    long long jeux = 0;
    for (const auto& r : p.recipes) jeux += static_cast<long long>(r.records.size());
    const long long history = h ? static_cast<long long>(h->alarms.size() + h->events.size() + h->system.size() + h->samples.size()
                                                        + h->audit.size()) : 0;     // lot 13 : l'audit compte
    const long long values[] = {
        static_cast<long long>(st.views), static_cast<long long>(st.objects), static_cast<long long>(st.layers),
        static_cast<long long>(st.resources), static_cast<long long>(p.assets.files.size()), static_cast<long long>(st.scripts),
        static_cast<long long>(p.programs.variables.size()), static_cast<long long>(p.programs.functions.size()),
        static_cast<long long>(st.actions),
        static_cast<long long>(p.alarms.size()), static_cast<long long>(p.recipes.size()), jeux,
        static_cast<long long>(p.security.users.size()), static_cast<long long>(p.security.groups.size()),
        static_cast<long long>(p.security.roles.size()), history,
    };
    std::vector<ArchiveCount> out;
    for (std::size_t i = 0; i < std::size(kSections); ++i) out.push_back({kSections[i].label, values[i], values[i]});
    return out;
}

core::Status exportArchive(const Project& p, const History* history, const std::string& zipPath, ArchiveReport* report) {
    std::vector<ZipItem> items;
    for (const auto& f : serializeProject(p)) items.push_back({"ihm/" + f.path, f.data});
    if (history && !history->empty())
        for (const auto which : kHistoryFiles)
            items.push_back({"ihm/historique/" + std::string(which) + ".csv", bytesOf(historyCsv(*history, which))});

    const std::string created = nowStamp();
    std::string manifest = "# XpgAnalyzer - archive du projet IHM\n";
    manifest += "archive format=1 outil=XpgAnalyzer format_ihm=" + std::to_string(kFormatVersion) + " cree=" + quote(created)
              + " projet=" + quote(p.config.name) + "\n";
    const auto counts = projectCounts(p, history);
    for (std::size_t i = 0; i < counts.size(); ++i)
        manifest += "section nom=" + std::string(kSections[i].key) + " nombre=" + std::to_string(counts[i].expected) + "\n";
    ArchiveReport local;
    auto& rep = report ? *report : local;
    rep = ArchiveReport{};
    for (const auto& it : items) {
        ArchiveEntry e{it.name, it.data->size(), digestOf(*it.data), true};
        manifest += "fichier chemin=" + quote(e.path) + " octets=" + std::to_string(e.bytes) + " sha256=" + e.sha256 + "\n";
        rep.entries.push_back(std::move(e));
    }
    manifest += "fin\n";
    items.insert(items.begin(), ZipItem{"manifeste.txt", bytesOf(manifest)});
    rep.counts = counts;
    rep.created = created;
    rep.projectName = p.config.name;
    std::error_code ec;
    if (const auto parent = fs::path(zipPath).parent_path(); !parent.empty()) fs::create_directories(parent, ec);
    return writeZip(zipPath, items, &rep.archiveBytes);
}

core::Result<Project> importArchive(const std::string& zipPath, History* history, ArchiveReport* report, LoadReport* load) {
    ArchiveReport local;
    auto& rep = report ? *report : local;
    rep = ArchiveReport{};

    // Toutes les entrees, en memoire (un projet IHM tient en memoire). Une
    // entree que le zip lui-meme refuse (CRC faux) est notee a part : elle est
    // alteree, pas absente.
    std::map<std::string, std::string> files;
    std::set<std::string> broken;
    {
        mz_zip_archive zip;
        std::memset(&zip, 0, sizeof zip);
        if (!mz_zip_reader_init_file(&zip, zipPath.c_str(), 0))
            return core::fail(core::ErrorCode::FileUnreadable, "archive illisible (pas un zip) : " + zipPath);
        const mz_uint n = mz_zip_reader_get_num_files(&zip);
        for (mz_uint i = 0; i < n; ++i) {
            mz_zip_archive_file_stat st;
            if (!mz_zip_reader_file_stat(&zip, i, &st) || st.m_is_directory) continue;
            std::size_t size = 0;
            std::string name = st.m_filename;
            for (auto& c : name) if (c == '\\') c = '/';
            void* data = mz_zip_reader_extract_to_heap(&zip, i, &size, 0);
            if (!data) {
                broken.insert(name);
                continue;
            }
            files[name].assign(static_cast<const char*>(data), size);
            mz_free(data);
        }
        mz_zip_reader_end(&zip);
    }
    std::error_code ec;
    rep.archiveBytes = fs::file_size(zipPath, ec);

    // Le manifeste : ce que l'archive doit contenir.
    std::map<std::string, long long> expected;
    const auto manifestIt = files.find("manifeste.txt");
    if (manifestIt == files.end()) {
        rep.problems.push_back("manifeste.txt absent : l'archive n'a pas \xC3\xA9t\xC3\xA9 faite par Exporter, rien \xC3\xA0 v\xC3\xA9rifier");
    } else {
        std::size_t pos = 0;
        const std::string& m = manifestIt->second;
        while (pos < m.size()) {
            const std::size_t eol = m.find('\n', pos);
            std::string_view line(m.data() + pos, (eol == std::string::npos ? m.size() : eol) - pos);
            pos = eol == std::string::npos ? m.size() : eol + 1;
            if (line.empty() || line[0] == '#') continue;
            Record r;
            std::string error;
            if (!parseRecord(line, r, error)) { rep.problems.push_back("manifeste : " + error); continue; }
            if (r.word == "archive") {
                if (const auto* c = r.get("cree")) rep.created = *c;
                if (const auto* c = r.get("projet")) rep.projectName = *c;
            } else if (r.word == "section") {
                const auto* name = r.get("nom");
                const auto* count = r.get("nombre");
                if (name && count) expected[*name] = std::strtoll(count->c_str(), nullptr, 10);
            } else if (r.word == "fichier") {
                ArchiveEntry e;
                e.path = r.get("chemin") ? *r.get("chemin") : std::string{};
                e.bytes = r.get("octets") ? std::strtoull(r.get("octets")->c_str(), nullptr, 10) : 0;
                e.sha256 = r.get("sha256") ? *r.get("sha256") : std::string{};
                const auto it = files.find(e.path);
                if (broken.count(e.path)) {
                    e.ok = false;
                    rep.problems.push_back(e.path + " : alt\xC3\xA9r\xC3\xA9 (le contr\xC3\xB4le CRC du zip \xC3\xA9" "choue)");
                    broken.erase(e.path);
                } else if (it == files.end()) {
                    e.ok = false;
                    rep.problems.push_back(e.path + " : absent de l'archive");
                } else {
                    const Bytes data(it->second.begin(), it->second.end());
                    if (data.size() != e.bytes || digestOf(data) != e.sha256) {
                        e.ok = false;
                        rep.problems.push_back(e.path + " : alt\xC3\xA9r\xC3\xA9 (taille ou empreinte SHA-256 diff\xC3\xA9rente)");
                    }
                }
                rep.entries.push_back(std::move(e));
            }
        }
    }

    for (const auto& name : broken)
        rep.problems.push_back(name + " : entr\xC3\xA9" "e illisible (le contr\xC3\xB4le CRC du zip \xC3\xA9" "choue)");

    // Le projet, relu par le meme code que le dossier ihm/.
    const auto read = [&files](const std::string& path, std::string& content) {
        const auto it = files.find("ihm/" + path);
        if (it == files.end()) return false;
        content = it->second;
        return true;
    };
    auto project = parseProject(read, load);
    if (!project) return core::fail(project.error().code, "archive : " + project.error().context);

    History imported;
    for (const auto which : kHistoryFiles) {
        const auto it = files.find("ihm/historique/" + std::string(which) + ".csv");
        if (it == files.end()) continue;
        std::string error;
        if (!parseHistoryCsv(it->second, which, imported, &error))
            rep.problems.push_back("historique " + std::string(which) + ".csv : " + error);
    }

    // Le controle de coherence : chaque compte annonce, retrouve.
    rep.counts = projectCounts(*project, &imported);
    for (std::size_t i = 0; i < rep.counts.size() && i < std::size(kSections); ++i) {
        const auto it = expected.find(kSections[i].key);
        if (it == expected.end()) continue;
        rep.counts[i].expected = it->second;
        if (!rep.counts[i].ok())
            rep.problems.push_back(rep.counts[i].section + " : " + std::to_string(it->second) + " annonc\xC3\xA9(s), "
                                   + std::to_string(rep.counts[i].found) + " relu(s)");
    }
    if (history) *history = std::move(imported);
    return project;
}

} // namespace hmi
