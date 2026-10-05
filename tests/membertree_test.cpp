// tests/membertree_test.cpp - lot API 7 : deplier sans limite.
//
//   membertree_test <MAST.XPG>
//
//  Les tableaux lus dans le texte de leur type (une, deux, trois dimensions ;
//  une borne qui n'est pas un nombre : pas un tableau) ; 150 elements en deux
//  paquets ; 25 000 en paquets de 10 000 puis de 100 ; une ligne [2, ...] puis
//  sa case Grille[2,5] ; une deuxieme dimension trop longue, en paquets aussi.
//  Sur le projet d'essai : armoires[0].sorties.V3.Mat[4] (six niveaux), les
//  broches, publiques et privees d'une instance de DFB, les broches d'un TON.
#include "../src/import/ProjectImporter.hpp"
#include "../src/project/MemberTree.hpp"

#include <cstdio>
#include <string>

namespace m = project::members;

namespace {

int failures = 0;
void check(bool ok, const std::string& what) {
    std::printf("  %s  %s\n", ok ? "ok  " : "FAIL", what.c_str());
    if (!ok) ++failures;
}

const m::Node* named(const std::vector<m::Node>& v, const std::string& label) {
    for (const auto& n : v)
        if (n.label == label) return &n;
    return nullptr;
}

} // namespace

int main(int argc, char** argv) {
    domain::Project empty;

    std::printf("1. Les tableaux, dans le texte de leur type\n");
    {
        const auto a = m::parseArray("ARRAY[0..149] OF INT");
        check(a.valid() && a.dims.size() == 1 && a.count() == 150 && a.element == "INT", "ARRAY[0..149] OF INT : 150 INT");
        const auto b = m::parseArray("ARRAY[0..3, 0..7] OF INT");
        check(b.valid() && b.dims.size() == 2 && b.count() == 32, "ARRAY[0..3, 0..7] : deux dimensions, 32 cases");
        const auto c = m::parseArray("array[1..2,1..3,0..1] of REAL");
        check(c.valid() && c.dims.size() == 3 && c.count() == 12 && c.element == "REAL", "trois dimensions, la casse ignoree");
        const auto d = m::parseArray("ARRAY[-5..5] OF armoire");
        check(d.valid() && d.dims[0].first == -5 && d.count() == 11 && d.element == "armoire", "une borne negative, un DDT");
        check(!m::parseArray("ARRAY[0..N] OF INT").valid(), "une borne nommee : pas lu (rien d'invente)");
        check(!m::parseArray("INT").valid() && !m::parseArray("").valid(), "INT n'est pas un tableau");
        check(m::indexSuffix({2, 5}) == "[2,5]" && m::indexSuffix({7}) == "[7]", "le nom d'une case : [2,5], sans espace");
        check(m::isElementary("BOOL") && m::isElementary("string[16]") && !m::isElementary("armoire"), "BOOL, STRING[16] : rien dessous");
    }

    std::printf("2. Les paquets\n");
    {
        const auto r = m::root("tempon", "ARRAY[0..149] OF INT");
        const auto kids = m::children(empty, r);
        check(kids.size() == 2 && !kids[0].real && kids[0].first == 0 && kids[0].last == 99 && kids[1].first == 100 && kids[1].last == 149,
              "150 elements : [0 ... 99] et [100 ... 149]");
        check(kids[1].key == "tempon[100..149]" && kids[1].label == "[100 \xE2\x80\xA6 149]" && m::groupType(kids[1]) == "paquet de 50",
              "la cle, le libelle, \"paquet de 50\"");
        const auto el = m::children(empty, kids[1]);
        check(el.size() == 50 && el.front().path == "tempon[100]" && el.back().path == "tempon[149]" && el.front().real && el.front().type == "INT",
              "le paquet : tempon[100] a tempon[149], des INT");
        const auto small = m::children(empty, m::root("t", "ARRAY[0..99] OF BOOL"));
        check(small.size() == 100 && small.front().real, "100 elements : pas de paquet");
        const auto big = m::children(empty, m::root("big", "ARRAY[0..24999] OF INT"));
        check(big.size() == 3 && big[0].last == 9999 && big[2].first == 20000 && big[2].last == 24999, "25 000 : trois paquets de 10 000");
        const auto sub = m::children(empty, big[1]);
        check(sub.size() == 100 && sub[0].first == 10000 && sub[0].last == 10099 && !sub[0].real, "un paquet de 10 000 : cent paquets de 100");
        const auto cells = m::children(empty, sub[99]);
        check(cells.size() == 100 && cells.back().path == "big[19999]", "puis cent elements : big[19999]");
        check(m::hasChildren(empty, big[0]) && !m::hasChildren(empty, cells[0]), "un paquet se deplie, un INT non");
    }

    std::printf("3. Plusieurs dimensions\n");
    {
        const auto rows = m::children(empty, m::root("Grille", "ARRAY[0..3, 0..7] OF INT"));
        check(rows.size() == 4 && !rows[2].real && rows[2].key == "Grille[2,*]" && rows[2].label == "[2, \xE2\x80\xA6]", "quatre lignes : [2, ...]");
        check(m::groupType(rows[2]) == "ARRAY[0..7] OF INT", "une ligne : ARRAY[0..7] OF INT");
        const auto cells = m::children(empty, rows[2]);
        check(cells.size() == 8 && cells[5].path == "Grille[2,5]" && cells[5].label == "[2, 5]" && cells[5].real, "puis ses cases : Grille[2,5]");
        const auto c3 = m::children(empty, m::root("C", "ARRAY[1..2, 1..3, 0..1] OF REAL"));
        const auto c3b = m::children(empty, c3[1]);
        const auto c3c = m::children(empty, c3b[2]);
        check(c3.size() == 2 && c3b.size() == 3 && c3c.size() == 2 && c3c[0].path == "C[2,3,0]" && c3c[0].label == "[2, 3, 0]",
              "trois dimensions : C[2,3,0]");
        const auto wide = m::children(empty, m::root("B", "ARRAY[0..1, 0..299] OF BOOL"));
        const auto packs = m::children(empty, wide[0]);
        check(packs.size() == 3 && packs[1].key == "B[0,100..199]" && packs[1].label == "[0, 100 \xE2\x80\xA6 199]", "une deuxieme dimension trop longue : en paquets");
        const auto inPack = m::children(empty, packs[1]);
        check(inPack.size() == 100 && inPack[5].path == "B[0,105]", "et ses cases : B[0,105]");
    }

    std::printf("4. Sur le projet d'essai\n");
    if (argc < 2) {
        std::printf("       (sans MAST.XPG : rien d'essaye ici)\n");
    } else {
        core::EventBus bus;
        importer::ProjectImporter importer(bus);
        auto imported = importer.importFile(argv[1]);
        check(static_cast<bool>(imported), "le projet d'essai se lit");
        if (imported) {
            const auto& p = *imported->project;
            auto node = m::root("armoires", "ARRAY[0..1] OF armoire");
            auto kids = m::children(p, node);
            check(kids.size() == 2 && kids[0].path == "armoires[0]" && kids[0].type == "armoire", "armoires : [0] et [1], des armoire");
            const auto fields = m::children(p, kids[0]);
            const auto* sorties = named(fields, ".sorties");
            check(fields.size() >= 30 && sorties && sorties->type == "Q" && sorties->what == "champ", "[0] : ses champs, dont .sorties (Q)");
            const auto outs = sorties ? m::children(p, *sorties) : std::vector<m::Node>{};
            const auto* v3 = named(outs, ".V3");
            check(v3 && v3->path == "armoires[0].sorties.V3" && v3->type == "sortie_tor", ".V3 : sortie_tor");
            const auto v3kids = v3 ? m::children(p, *v3) : std::vector<m::Node>{};
            const auto* mat = named(v3kids, ".Mat");
            check(mat && m::hasChildren(p, *mat), ".Mat : un tableau, qui se deplie");
            const auto bits = mat ? m::children(p, *mat) : std::vector<m::Node>{};
            check(bits.size() == 16 && bits[4].path == "armoires[0].sorties.V3.Mat[4]" && bits[4].type == "BOOL",
                  "armoires[0].sorties.V3.Mat[4] : six niveaux, pas de limite");
            const auto configs = m::children(p, m::root("ConfigsGaz", "ARRAY[0..19] OF config_gaz"));
            const auto c12 = configs.size() > 12 ? m::children(p, configs[12]) : std::vector<m::Node>{};
            const auto* purge = named(c12, ".purge");
            const auto p2 = purge ? m::children(p, *purge) : std::vector<m::Node>{};
            const auto p2kids = p2.size() > 2 ? m::children(p, p2[2]) : std::vector<m::Node>{};
            const auto* dl4 = named(p2kids, ".DL4");
            check(dl4 && dl4->path == "ConfigsGaz[12].purge[2].DL4" && dl4->type == "INT", "ConfigsGaz[12].purge[2].DL4 : des tableaux de structures");
            const auto engine = m::children(p, m::root("G", "DFB_GRAFCETENGINE"));
            bool hasIn = false, hasPrivate = false;
            for (const auto& c : engine) {
                hasIn = hasIn || c.what == "entr\xC3\xA9" "e";
                hasPrivate = hasPrivate || c.what == "priv\xC3\xA9" "e";
            }
            check(!engine.empty() && hasIn, "une instance de DFB : ses broches");
            std::printf("       DFB_GRAFCETENGINE : %zu membres (privees : %s)\n", engine.size(), hasPrivate ? "oui" : "non");
            const auto ton = m::children(p, m::root("Tempo", "TON"));
            check(ton.size() >= 4 && named(ton, ".Q") && named(ton, ".ET"), "un TON : IN, PT, Q, ET");
            check(!m::hasChildren(p, m::root("x", "REAL")) && m::hasChildren(p, m::root("y", "armoire")), "REAL : rien ; armoire : ses champs");
        }
    }

    std::printf(failures ? "ECHEC : %d echec(s)\n" : "membertree_test : tout est bon\n", failures);
    return failures ? 1 : 0;
}
