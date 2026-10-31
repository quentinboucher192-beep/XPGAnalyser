// tests/ai_contexte_test.cpp - 1.21.0 : LE CONTEXTE DE LA DISCUSSION ET SES FICHIERS JOINTS (section 5, essais 5.10 et 9.5).
//
//   ai_contexte_test [dossier third_party/pdfium]
//
//  Sans ecran ni fournisseur, sur un dossier IA/ jetable :
//  1. ajouter un fichier, plusieurs fichiers, deux fichiers de meme nom (deux pieces distinctes) ;
//  2. la liste restauree apres un redemarrage ; une ancienne discussion (1.20) rouverte et completee ;
//  3. lire : un texte (encodage, fins de ligne gardes), du code, un PDF (PDFium : sinon, SAUTE et dit), un tableur (ses
//     feuilles, ses cellules), un document Word, une image (reconnue) ; sur le fil d'analyse ;
//  4. comparer deux fichiers ; retrouver celui dont parle une demande ; ce qui part chez le fournisseur ;
//  5. un fichier manquant, deplace, corrompu, trop volumineux ;
//  6. retirer de la discussion, remettre ; une copie de travail (l'original intact) ; un doublon (pas de copie inutile) ;
//  7. vider le cache sans perdre une piece jointe (octet pour octet), le texte extrait refait ;
//  8. l'isolation des pieces jointes entre conversations ;
//  9. le volet << Contexte de la discussion >> : le compteur, la liste, les gestes, la recherche, son dessin.
#include "../src/app/ai/AiAttachments.hpp"
#include "../src/app/ai/AiContextPane.hpp"
#include "../src/app/ai/AiMemory.hpp"
#include "../src/app/ai/AiPaths.hpp"
#include "../src/app/ai/AiStore.hpp"
#include "../src/export/DocKit.hpp"
#include "../src/hmi/HmiDocument.hpp"
#include "../src/hmi/HmiPdf.hpp"
#include "../src/ui/Theme.hpp"
#include "../src/ui/Widget.hpp"

#include <algorithm>
#include <chrono>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <iterator>
#include <string>
#include <vector>

namespace fs = std::filesystem;
namespace ai = app::ai;
namespace at = app::ai::attach;

namespace {

int failures = 0;
int checks = 0;
int skipped = 0;

void check(bool ok, const std::string& what) {
    ++checks;
    static const bool all = [] {
        const char* e = std::getenv("AI_CONTEXTE_TOUT");
        return e && *e == '1';
    }();
    if (!ok) {
        ++failures;
        std::printf("ECHEC : %s\n", what.c_str());
    } else if (all) {
        std::printf("  ok : %s\n", what.c_str());
    }
}

void skip(const std::string& what) {
    ++skipped;
    std::printf("SAUTE : %s\n", what.c_str());
}

bool has(const std::string& s, const std::string& part) { return s.find(part) != std::string::npos; }

fs::path scratch() {
    static const fs::path p = fs::temp_directory_path()
                              / ("xpg_ai_contexte_" + std::to_string(std::chrono::steady_clock::now().time_since_epoch().count() % 1000000007));
    return p;
}

std::string readAll(const fs::path& p) {
    std::ifstream in(p, std::ios::binary);
    return std::string(std::istreambuf_iterator<char>(in), std::istreambuf_iterator<char>());
}

fs::path writeFile(const fs::path& p, const std::string& bytes) {
    std::error_code ec;
    fs::create_directories(p.parent_path(), ec);
    std::ofstream out(p, std::ios::binary | std::ios::trunc);
    out.write(bytes.data(), static_cast<std::streamsize>(bytes.size()));
    return p;
}

std::string toString(const std::vector<std::uint8_t>& b) { return std::string(b.begin(), b.end()); }

// ---- des fichiers de chaque format, faits ici ----------------------------------------------------
std::string xlsxBytes() {
    const std::vector<std::pair<std::string, std::string>> items = {
        {"[Content_Types].xml", "<?xml version=\"1.0\"?><Types xmlns=\"http://schemas.openxmlformats.org/package/2006/content-types\"/>"},
        {"xl/workbook.xml",
         "<?xml version=\"1.0\"?><workbook xmlns=\"http://schemas.openxmlformats.org/spreadsheetml/2006/main\" "
         "xmlns:r=\"http://schemas.openxmlformats.org/officeDocument/2006/relationships\"><sheets>"
         "<sheet name=\"Variables\" sheetId=\"1\" r:id=\"rId1\"/><sheet name=\"Notes\" sheetId=\"2\" r:id=\"rId2\" state=\"hidden\"/></sheets></workbook>"},
        {"xl/_rels/workbook.xml.rels",
         "<?xml version=\"1.0\"?><Relationships xmlns=\"http://schemas.openxmlformats.org/package/2006/relationships\">"
         "<Relationship Id=\"rId1\" Type=\"worksheet\" Target=\"worksheets/sheet1.xml\"/>"
         "<Relationship Id=\"rId2\" Type=\"worksheet\" Target=\"worksheets/sheet2.xml\"/></Relationships>"},
        {"xl/sharedStrings.xml",
         "<?xml version=\"1.0\"?><sst count=\"4\" uniqueCount=\"4\"><si><t>Nom</t></si><si><t>Adresse</t></si><si><t>Pompe_1_Marche</t></si>"
         "<si><r><t>Vanne </t></r><r><t>&amp; clapet</t></r></si></sst>"},
        {"xl/worksheets/sheet1.xml",
         "<?xml version=\"1.0\"?><worksheet><sheetData>"
         "<row r=\"1\"><c r=\"A1\" t=\"s\"><v>0</v></c><c r=\"B1\" t=\"s\"><v>1</v></c></row>"
         "<row r=\"2\"><c r=\"A2\" t=\"s\"><v>2</v></c><c r=\"B2\"><v>100</v></c></row>"
         "<row r=\"3\"><c r=\"A3\" t=\"s\"><v>3</v></c><c r=\"B3\"><v>101</v></c></row>"
         "</sheetData></worksheet>"},
        {"xl/worksheets/sheet2.xml", "<?xml version=\"1.0\"?><worksheet><sheetData><row r=\"1\"><c r=\"A1\"><v>42</v></c></row></sheetData></worksheet>"},
    };
    return toString(exporter::doc::zip(items));
}

std::string docxBytes() {
    const std::vector<std::pair<std::string, std::string>> items = {
        {"[Content_Types].xml", "<?xml version=\"1.0\"?><Types xmlns=\"http://schemas.openxmlformats.org/package/2006/content-types\"/>"},
        {"word/document.xml",
         "<?xml version=\"1.0\"?><w:document xmlns:w=\"http://schemas.openxmlformats.org/wordprocessingml/2006/main\"><w:body>"
         "<w:p><w:r><w:t>Notice de la pompe P101</w:t></w:r></w:p>"
         "<w:p><w:r><w:t xml:space=\"preserve\">D\xC3\xA9" "bit : </w:t></w:r><w:r><w:t>12 m3/h &amp; 3 bar</w:t></w:r></w:p>"
         "<w:tbl><w:tr><w:tc><w:p><w:r><w:t>Repere</w:t></w:r></w:p></w:tc><w:tc><w:p><w:r><w:t>P101</w:t></w:r></w:p></w:tc></w:tr></w:tbl>"
         "</w:body></w:document>"},
    };
    return toString(exporter::doc::zip(items));
}

std::string pngBytes(std::uint32_t w, std::uint32_t h) {
    std::string b = std::string("\x89PNG\r\n\x1A\n", 8) + std::string("\x00\x00\x00\x0D", 4) + "IHDR";
    for (const std::uint32_t v : {w, h}) {
        b += static_cast<char>((v >> 24) & 0xFF);
        b += static_cast<char>((v >> 16) & 0xFF);
        b += static_cast<char>((v >> 8) & 0xFF);
        b += static_cast<char>(v & 0xFF);
    }
    b += std::string("\x08\x06\x00\x00\x00", 5) + std::string("\x00\x00\x00\x00", 4);
    return b;
}

std::string jpegBytes(std::uint16_t w, std::uint16_t h) {
    std::string b("\xFF\xD8\xFF\xE0\x00\x10JFIF\x00\x01\x01\x00\x00\x01\x00\x01\x00\x00", 20);
    b += std::string("\xFF\xC0\x00\x11\x08", 5);
    b += static_cast<char>(h >> 8);
    b += static_cast<char>(h & 0xFF);
    b += static_cast<char>(w >> 8);
    b += static_cast<char>(w & 0xFF);
    b += std::string(12, '\x01');
    b += std::string("\xFF\xD9", 2);
    return b;
}

// ---- une conversation et ses pieces --------------------------------------------------------------
ai::Conversation newConversation(const std::string& title) {
    ai::Conversation c;
    c.id = ai::newId("c");
    c.title = title;
    c.created = c.modified = ai::isoDate(ai::now());
    return c;
}

// Joint le fichier a un nouveau message de l'utilisateur (comme l'Assistant a l'envoi).
const ai::Attachment* attachTo(ai::Conversation& c, const fs::path& file, std::string* why = nullptr) {
    ai::Attachment a;
    if (!ai::addAttachment(file.string(), a, why)) return nullptr;
    at::adopt(a, c.id);
    ai::Message m;
    m.id = "m" + std::to_string(c.messages.size() + 1);
    m.role = "utilisateur";
    m.date = ai::isoDate(ai::now());
    m.text = "Voici " + a.name;
    a.message = m.id;
    m.attachments.push_back(std::move(a));
    c.messages.push_back(std::move(m));
    return &c.messages.back().attachments.back();
}

ai::Attachment* find(ai::Conversation& c, const std::string& id) {
    for (auto& m : c.messages)
        for (auto& a : m.attachments)
            if (a.id == id) return &a;
    return nullptr;
}

// Analyse sur le fil, puis remet le resultat (le fil de l'interface).
void analyzeAll(ai::Conversation& c, const at::Limits& limits = {}) {
    at::Analyzer an(limits);
    for (auto& m : c.messages)
        for (auto& a : m.attachments)
            if (a.analysis.empty() || a.analysis == "en-cours") {
                at::markRunning(a);
                an.submit(a);
            }
    check(an.waitIdle(60000), "le fil d'analyse finit");
    for (const auto& o : an.poll()) check(at::applyOutcome(c, o), "le resultat remis a sa piece (" + o.attachment + ")");
}

const at::ContextFile* fileNamed(const at::DiscussionContext& ctx, const std::string& name) {
    for (const auto& f : ctx.files)
        if (f.attachment.name == name) return &f;
    return nullptr;
}

std::string lastSummary(const ai::Attachment& a) { return a.analyses.empty() ? std::string{} : a.analyses.back().summary; }
std::string lastError(const ai::Attachment& a) { return a.analyses.empty() ? std::string{} : a.analyses.back().error; }

// =========================================================================== les essais ===
fs::path src() { return scratch() / "fichiers"; }

void addFiles(ai::Conversation& c) {
    std::printf("== 1. ajouter un fichier, plusieurs, deux de meme nom ==\n");
    const fs::path txt = writeFile(src() / "PL7_3.txt", "(* programme *)\r\nLD %I0.1\r\nST %Q0.2\r\n");
    const auto* a = attachTo(c, txt);
    check(a != nullptr, "un fichier joint");
    if (!a) return;
    check(a->id.rfind("p-", 0) == 0 && a->conversation == c.id && a->ext == "txt" && a->mime == "text/plain" && !a->added.empty()
              && a->bytes == fs::file_size(txt) && a->originalPath == txt.string(),
          "ses champs : id, conversation, extension, MIME, taille, date d'ajout, chemin d'origine");
    check(readAll(fs::path(ai::attachmentPath(*a))) == readAll(txt), "la copie, a l'identique, dans pieces-jointes/<id>/");
    // plusieurs
    writeFile(src() / "prog.st", "PROGRAM Main\nVAR\n  Compteur : INT;\nEND_VAR\nCompteur := Compteur + 1;\nEND_PROGRAM\n");
    writeFile(src() / "Variables.xlsx", xlsxBytes());
    writeFile(src() / "Notice.docx", docxBytes());
    writeFile(src() / "Vue_Production.png", pngBytes(1920, 1080));
    writeFile(src() / "photo.jpg", jpegBytes(64, 32));
    writeFile(src() / "Schema_Pompe.pdf", toString(hmi::doc::sampleNotice()));
    for (const char* n : {"prog.st", "Variables.xlsx", "Notice.docx", "Vue_Production.png", "photo.jpg", "Schema_Pompe.pdf"})
        check(attachTo(c, src() / n) != nullptr, std::string("joint : ") + n);
    // deux fichiers DIFFERENTS de meme nom
    const fs::path v1 = writeFile(src() / "a" / "Variables.txt", "Niveau;REAL\nDebit;REAL\n");
    const fs::path v2 = writeFile(src() / "b" / "Variables.txt", "Niveau;REAL\nPression;REAL\nTemperature;REAL\n");
    const auto* x1 = attachTo(c, v1);
    const std::string id1 = x1 ? x1->id : std::string{};
    const auto* x2 = attachTo(c, v2);
    check(x2 && !id1.empty() && x2->id != id1, "deux fichiers de meme nom : deux identifiants");
    const auto* y1 = find(c, id1);
    check(y1 && x2 && ai::attachmentPath(*y1) != ai::attachmentPath(*x2) && readAll(fs::path(ai::attachmentPath(*y1))) == readAll(v1)
              && readAll(fs::path(ai::attachmentPath(*x2))) == readAll(v2),
          "deux copies distinctes, chacune son contenu");
    const auto ctx = at::buildContext(c);
    check(ctx.count() == 9 && ctx.unavailable() == 0, "le contexte : 9 fichiers joints, tous disponibles");
    check(has(at::contextPrompt(ctx), "Fichiers joints : 9"), "le texte de la demande : << Fichiers joints : 9 >>");
    const auto ref = at::resolveReference(ctx, "Reprends Variables.txt");
    check(ref.ambiguous && ref.ids.size() == 2, "un nom porte par deux fichiers : les deux candidates (comparer ou demander)");
}

void analyses(ai::Conversation& c) {
    std::printf("== 3. lire chaque format (sur le fil d'analyse) ==\n");
    analyzeAll(c);
    const auto ctx = at::buildContext(c);
    const auto* txt = fileNamed(ctx, "PL7_3.txt");
    check(txt && txt->attachment.analysis == "analysee" && txt->attachment.analyses.back().lines == 3, "un texte : analyse, 3 lignes");
    check(txt && txt->line() == "PL7_3.txt \xE2\x80\x94 Fichier texte \xE2\x80\x94 Analys\xC3\xA9", "sa ligne : " + (txt ? txt->line() : std::string{}));
    check(txt && std::any_of(txt->attachment.analyses.back().details.begin(), txt->attachment.analyses.back().details.end(),
                             [](const std::string& d) { return has(d, "CRLF"); }),
          "les fins de ligne Windows dites (et gardees dans l'original)");
    check(txt && !txt->attachment.sha256.empty() && txt->attachment.sha256.size() == 64, "son empreinte SHA-256");
    std::string text, why;
    check(txt && at::textOf(txt->attachment, text, &why) && text == "(* programme *)\r\nLD %I0.1\r\nST %Q0.2\r\n", "le texte EXACT (les CRLF compris)");
    const auto* st = fileNamed(ctx, "prog.st");
    check(st && st->attachment.analysis == "analysee" && st->kindLabel == "Programme ST" && st->attachment.analyses.back().lines == 6, "du code ST : 6 lignes");
    const auto* xl = fileNamed(ctx, "Variables.xlsx");
    check(xl && xl->attachment.analysis == "analysee" && xl->attachment.analyses.back().sheets == 2, "un tableur : 2 feuilles");
    check(xl && at::textOf(xl->attachment, text, &why) && has(text, "Feuille \xC2\xAB Variables \xC2\xBB") && has(text, "Pompe_1_Marche | 100")
              && has(text, "Vanne & clapet | 101") && has(text, "(masqu\xC3\xA9" "e)"),
          "ses cellules (chaines partagees, nombres, la feuille masquee dite)");
    check(xl && !xl->attachment.analyses.back().textFile.empty() && fs::exists(fs::path(ai::root()) / xl->attachment.analyses.back().textFile),
          "son texte extrait dans cache/documents/");
    const auto* dx = fileNamed(ctx, "Notice.docx");
    check(dx && at::textOf(dx->attachment, text, &why) && has(text, "Notice de la pompe P101\n") && has(text, "D\xC3\xA9" "bit : 12 m3/h & 3 bar")
              && has(text, "Repere\tP101"),
          "un document Word : ses paragraphes, son tableau");
    const auto* png = fileNamed(ctx, "Vue_Production.png");
    check(png && png->attachment.analysis == "analysee" && png->attachment.analyses.back().width == 1920 && png->attachment.analyses.back().height == 1080
              && png->line() == "Vue_Production.png \xE2\x80\x94 Image \xE2\x80\x94 Disponible",
          "une image PNG reconnue (1920 x 1080) ; sa ligne : " + (png ? png->line() : std::string{}));
    const auto* jpg = fileNamed(ctx, "photo.jpg");
    check(jpg && jpg->attachment.analyses.back().width == 64 && jpg->attachment.analyses.back().height == 32, "une image JPEG reconnue (64 x 32)");
    // le PDF : avec PDFium, son texte ; sans, l'echec dit tel quel
    const auto* pdf = fileNamed(ctx, "Schema_Pompe.pdf");
    if (hmi::pdf::library().ok && hmi::pdf::canExtractText()) {
        check(pdf && pdf->attachment.analysis == "analysee" && pdf->attachment.analyses.back().pages == 4, "un PDF : 4 pages lues (PDFium)");
        check(pdf && at::textOf(pdf->attachment, text, &why) && has(text, "VARIATEUR") && has(text, "--- Page 2 ---"), "son texte, page par page");
        check(pdf && pdf->line() == "Schema_Pompe.pdf \xE2\x80\x94 Document PDF \xE2\x80\x94 Analys\xC3\xA9", "sa ligne : " + (pdf ? pdf->line() : std::string{}));
    } else {
        check(pdf && pdf->attachment.analysis == "echec" && has(lastError(pdf->attachment), "PDF"),
              "sans PDFium : l'echec du PDF est dit (" + (pdf ? lastError(pdf->attachment) : std::string{}) + ")");
        skip("lecture du texte d'un PDF : PDFium absent (" + hmi::pdf::library().why + ")");
    }
    // ce qui part chez le fournisseur
    const auto pXl = at::forProvider(xl->attachment, true);
    check(pXl && pXl->name == "Variables.xlsx (texte extrait).txt" && pXl->mediaType == "text/plain" && has(pXl->path, "cache"),
          "un tableur part comme son texte extrait");
    const auto pSt = at::forProvider(st->attachment, true);
    check(pSt && pSt->name == "prog.st" && pSt->path == ai::attachmentPath(st->attachment), "du code part tel quel (l'original)");
    const auto pPdf = at::forProvider(pdf->attachment, true);
    check(pPdf && pPdf->name == "Schema_Pompe.pdf", "un PDF part tel quel chez un fournisseur qui lit les PDF");
    // retrouver la piece dont parle une demande
    const auto r1 = at::resolveReference(ctx, "Reprends le programme ST que je t'ai envoy\xC3\xA9");
    check(!r1.ids.empty() && find(c, r1.ids.front()) && find(c, r1.ids.front())->name == "prog.st", "<< le programme >> : prog.st");
    const auto r2 = at::resolveReference(ctx, "Retrouve l'adresse de cette entr\xC3\xA9" "e dans mon tableur");
    check(r2.ids.size() == 1 && find(c, r2.ids.front())->name == "Variables.xlsx", "<< mon tableur >> : Variables.xlsx");
    const auto r3 = at::resolveReference(ctx, "Continue \xC3\xA0 partir du PDF que j'avais joint");
    check(r3.ids.size() == 1 && find(c, r3.ids.front())->name == "Schema_Pompe.pdf", "<< le PDF >> : Schema_Pompe.pdf");
    const auto r4 = at::resolveReference(ctx, "Compare ces deux fichiers");
    check(r4.ids.size() == 2 && !r4.ambiguous, "<< ces deux fichiers >> : les deux plus recents");
    check(at::searchFiles(ctx, "feuilles").size() == 1 && at::searchFiles(ctx, "Variables").size() == 3, "chercher parmi les fichiers");
}

void compareFiles(ai::Conversation& c) {
    std::printf("== 4. comparer deux fichiers ==\n");
    std::vector<ai::Attachment*> vars;
    for (auto& m : c.messages)
        for (auto& a : m.attachments)
            if (a.name == "Variables.txt") vars.push_back(&a);
    if (vars.size() != 2) {
        check(false, "les deux Variables.txt");
        return;
    }
    const auto cmp = at::compare(*vars[0], *vars[1]);
    check(cmp.comparable && !cmp.identical && cmp.common == 1 && cmp.onlyA == 1 && cmp.onlyB == 2, "1 ligne commune, 1 et 2 propres : " + cmp.summary);
    check(!cmp.sampleA.empty() && cmp.sampleA[0] == "Debit;REAL" && has(cmp.summary, "Variables.txt"), "les lignes propres, le resume");
    const auto same = at::compare(*vars[0], *vars[0]);
    check(same.identical, "un fichier avec lui-meme : identique");
}

void restart(const ai::Conversation& c) {
    std::printf("== 2. la liste restauree apres un redemarrage ; une ancienne discussion rouverte ==\n");
    check(ai::saveConversation(c), "la conversation enregistree");
    std::vector<std::string> problems;
    const auto all = ai::loadConversations(&problems);
    const ai::Conversation* back = nullptr;
    for (const auto& x : all)
        if (x.id == c.id) back = &x;
    check(back != nullptr, "relue apres un redemarrage");
    if (!back) return;
    const auto before = at::buildContext(c), after = at::buildContext(*back);
    check(after.count() == before.count() && after.files.size() == before.files.size(), "les memes fichiers joints");
    // une analyse relue : ses champs (les champs inconnus, eux, reviennent en objet vide)
    const auto sameAnalysis = [](const ai::AttachmentAnalysis& u, const ai::AttachmentAnalysis& v) {
        return u.date == v.date && u.kind == v.kind && u.state == v.state && u.summary == v.summary && u.error == v.error && u.textFile == v.textFile
               && u.textBytes == v.textBytes && u.pages == v.pages && u.sheets == v.sheets && u.width == v.width && u.height == v.height
               && u.lines == v.lines && u.details == v.details && u.message == v.message;
    };
    bool same = after.files.size() == before.files.size();
    for (std::size_t i = 0; same && i < after.files.size(); ++i) {
        const auto& x = after.files[i].attachment;
        const auto& y = before.files[i].attachment;
        same = x.id == y.id && x.sha256 == y.sha256 && x.analysis == y.analysis && x.analyses.size() == y.analyses.size() && x.conversation == y.conversation
               && x.message == y.message && after.files[i].availability == before.files[i].availability;
        for (std::size_t k = 0; same && k < x.analyses.size(); ++k) same = sameAnalysis(x.analyses[k], y.analyses[k]);
    }
    check(same, "chacun avec son id, son empreinte, son etat et ses analyses, sa conversation et son message");
    // une discussion de la 1.20 : ses pieces n'avaient que id, nom, type, octets, fichier
    const std::string body = "(* ancien programme *)\nLD %I0.0\n";
    writeFile(fs::path(ai::attachmentsDir()) / "p-old-1" / "Ancien.txt", body);
    const std::string old = "{\"format\": 1, \"id\": \"c-20261001-100000-1\", \"titre\": \"Ancienne discussion\", \"cree\": \"2026-10-01T10:00:00\", "
                            "\"modifie\": \"2026-10-01T10:05:00\", \"demo\": false, \"projet\": {\"nom\": \"Station\", \"chemin\": \"\", \"vue\": \"\"}, "
                            "\"messages\": [{\"id\": \"m1\", \"role\": \"utilisateur\", \"date\": \"2026-10-01T10:00:00\", \"texte\": \"Voici le programme\", "
                            "\"pieces\": [{\"id\": \"p-old-1\", \"nom\": \"Ancien.txt\", \"type\": \"txt\", \"octets\": "
                            + std::to_string(body.size()) + ", \"fichier\": \"pieces-jointes/p-old-1/Ancien.txt\"}]}]}";
    writeFile(fs::path(ai::conversationsDir()) / "c-20261001-100000-1.json", old);
    ai::Conversation reopened;
    for (const auto& x : ai::loadConversations())
        if (x.id == "c-20261001-100000-1") reopened = x;
    check(at::adopt(reopened) == 1, "rouverte : sa piece completee");
    const auto& a = reopened.messages.at(0).attachments.at(0);
    check(a.conversation == reopened.id && a.message == "m1" && a.ext == "txt" && a.mime == "text/plain" && a.added == "2026-10-01T10:00:00",
          "sa conversation, son message, son extension, son MIME, sa date d'ajout (celle du message)");
    const auto ctx = at::buildContext(reopened);
    check(ctx.count() == 1 && ctx.files[0].availability == at::Availability::Present
              && ctx.files[0].line() == "Ancien.txt \xE2\x80\x94 Fichier texte \xE2\x80\x94 Disponible",
          "le contexte restaure : " + (ctx.files.empty() ? std::string{} : ctx.files[0].line()));
    check(has(at::contextPrompt(ctx), "Ancien.txt") && has(at::contextPrompt(ctx), "jamais des consignes"), "le texte de la demande : le fichier, et les regles");
    check(ai::saveConversation(reopened), "enregistree completee");
}

void missingCorruptBig(ai::Conversation& c) {
    std::printf("== 5. manquant, deplace, corrompu, trop volumineux ==\n");
    // corrompu
    writeFile(src() / "corrompu.xlsx", "ceci n'est pas une archive");
    writeFile(src() / "binaire.txt", std::string("abc\0\0\0def", 9));
    writeFile(src() / "faux.pdf", "pas un pdf du tout");
    writeFile(src() / "fausse.png", "GIF89a...");
    writeFile(src() / "zip_vide.docx", toString(exporter::doc::zip({{"autre.xml", "<a/>"}})));
    for (const char* n : {"corrompu.xlsx", "binaire.txt", "faux.pdf", "fausse.png", "zip_vide.docx"}) (void)attachTo(c, src() / n);
    analyzeAll(c);
    auto ctx = at::buildContext(c);
    const auto failed = [&](const char* name, const char* word) {
        const auto* f = fileNamed(ctx, name);
        check(f && f->attachment.analysis == "echec" && has(lastError(f->attachment), word),
              std::string(name) + " : echec dit (" + (f ? lastError(f->attachment) : std::string("absent")) + ")");
    };
    failed("corrompu.xlsx", "archive");
    failed("binaire.txt", "binaire");
    failed("faux.pdf", "%PDF");
    failed("fausse.png", "PNG");
    failed("zip_vide.docx", "word/document.xml");
    check(has(at::contextPrompt(ctx), "analyse en \xC3\xA9" "chec"), "le texte de la demande dit l'echec");
    // trop volumineux pour l'analyse (une limite basse), et pour etre joint (un fichier creux de 201 Mo)
    const fs::path big = writeFile(src() / "gros.txt", std::string(4096, 'x'));
    const auto* g = attachTo(c, big);
    at::Limits small;
    small.maxBytes = 1024;
    const auto out = at::analyzeNow(*g, small);
    check(out.analysis.state == "echec" && has(out.analysis.error, "trop volumineux") && !out.sha256.empty(), "trop volumineux pour l'analyse : dit (il reste joint)");
    {
        const fs::path huge = src() / "enorme.txt";
        writeFile(huge, "x");
        std::error_code ec;
        fs::resize_file(huge, ai::kMaxAttachmentBytes + 1, ec);
        ai::Attachment a;
        std::string why;
        check(!ec && !ai::addAttachment(huge.string(), a, &why) && has(why, "trop volumineux"), "trop volumineux pour etre joint : refuse (" + why + ")");
        fs::remove(huge, ec);
    }
    // manquant, puis deplace
    ai::Attachment* p = nullptr;
    for (auto& m : c.messages)
        for (auto& a : m.attachments)
            if (a.name == "prog.st") p = &a;
    const fs::path copy = ai::attachmentPath(*p);
    const std::string bytes = readAll(copy);
    std::error_code ec;
    fs::rename(copy, copy.parent_path() / "renomme.st", ec);
    std::string where;
    check(at::availability(*p, &where) == at::Availability::Moved && has(where, "renomme.st"), "renommee dans son dossier : deplacee, retrouvee");
    check(at::relocate(*p) && at::availability(*p) == at::Availability::Present && readAll(copy) == bytes, "remise a sa place, a l'identique");
    fs::remove(copy, ec);
    fs::remove(copy.parent_path() / "renomme.st", ec);
    check(at::availability(*p) == at::Availability::Moved, "la copie partie, l'original sur le PC : deplacee (retrouvee a son emplacement d'origine)");
    fs::rename(src() / "prog.st", src() / "prog.bak", ec);
    check(at::availability(*p) == at::Availability::Missing, "ni copie ni original : manquante");
    const auto miss = at::analyzeNow(*p);
    check(miss.analysis.state == "echec" && has(miss.analysis.error, "introuvable"), "l'analyser : echec, introuvable");
    std::string text;
    check(!at::textOf(*p, text) && !at::relocate(*p), "son texte ne se lit pas, elle ne se remet pas");
    ctx = at::buildContext(c);
    check(ctx.unavailable() == 1 && has(at::contextPrompt(ctx), "MANQUANT") && has(at::contextPrompt(ctx), "ne pr\xC3\xA9tends pas l'avoir consult\xC3\xA9"),
          "le contexte et la demande le disent (ne pas pretendre l'avoir lu)");
    fs::rename(src() / "prog.bak", src() / "prog.st", ec);
    check(at::relocate(*p) && at::availability(*p) == at::Availability::Present, "l'original revenu : la copie refaite");
}

void removeCopyDuplicate(ai::Conversation& c) {
    std::printf("== 6. retirer, remettre ; la copie de travail ; un doublon ==\n");
    ai::Attachment* jpg = nullptr;
    for (auto& m : c.messages)
        for (auto& a : m.attachments)
            if (a.name == "photo.jpg") jpg = &a;
    const std::string id = jpg->id;
    const std::size_t before = at::buildContext(c).count();
    std::string why;
    check(at::removeFromDiscussion(c, id, false, &why) && find(c, id)->removed, "retiree de la discussion (la copie gardee)");
    auto ctx = at::buildContext(c);
    check(ctx.count() == before - 1 && !has(at::contextPrompt(ctx), "photo.jpg") && !at::forProvider(*find(c, id), true), "elle ne part plus");
    check(at::availability(*find(c, id)) == at::Availability::Present, "sa copie reste");
    check(at::restoreToDiscussion(c, id, &why) && at::buildContext(c).count() == before, "remise dans la discussion");
    check(at::removeFromDiscussion(c, id, true, &why) && !fs::exists(fs::path(ai::attachmentsDir()) / id), "retiree, sa copie supprimee");
    check(at::availability(*find(c, id)) == at::Availability::Moved, "son original est encore sur le PC : retrouvable (deplacee)");
    std::error_code ec;
    fs::rename(src() / "photo.jpg", src() / "photo.bak", ec);
    check(!at::restoreToDiscussion(c, id, &why) && find(c, id)->removed, "ni copie ni original : elle ne revient pas (" + why + ")");
    fs::rename(src() / "photo.bak", src() / "photo.jpg", ec);
    check(at::restoreToDiscussion(c, id, &why) && at::availability(*find(c, id)) == at::Availability::Present, "l'original revenu : remise, sa copie refaite");
    check(at::removeFromDiscussion(c, id, false, &why), "retiree de nouveau (la copie gardee)");
    // la copie de travail : l'original ne change jamais
    ai::Attachment* txt = nullptr;
    for (auto& m : c.messages)
        for (auto& a : m.attachments)
            if (a.name == "PL7_3.txt") txt = &a;
    const std::string original = readAll(ai::attachmentPath(*txt));
    const std::string txtId = txt->id;
    ai::Attachment work;
    check(at::makeWorkingCopy(c, txtId, "(* modifie *)\r\nLD %I0.1\r\nST %Q0.3\r\n", &work, &why), "une copie de travail : " + why);
    check(work.origin == "copie-de-travail" && work.workingCopyOf == txtId && work.name == "PL7_3 (copie de travail).txt" && work.id != txtId,
          "une nouvelle piece, nommee, liee a l'original");
    check(readAll(ai::attachmentPath(*find(c, txtId))) == original, "l'original intact, octet pour octet");
    check(readAll(ai::attachmentPath(work)) == "(* modifie *)\r\nLD %I0.1\r\nST %Q0.3\r\n", "la copie porte la modification");
    ai::Attachment work2;
    check(at::makeWorkingCopy(c, txtId, "v2", &work2) && work2.name == "PL7_3 (copie de travail 2).txt", "une seconde copie : un autre nom");
    check(has(at::contextPrompt(at::buildContext(c)), "copie de travail (l'original reste intact)"), "la demande le dit");
    // un doublon : le meme contenu joint deux fois -> la meme copie, pas de copie inutile
    const auto* d1 = attachTo(c, src() / "a" / "Variables.txt");
    const std::string dupId = d1 ? d1->id : std::string{};
    analyzeAll(c);
    const auto* d = find(c, dupId);
    check(d && !d->duplicateOf.empty() && find(c, d->duplicateOf) && find(c, d->duplicateOf)->file == d->file, "un doublon : il reprend la copie existante");
    check(d && !fs::exists(fs::path(ai::attachmentsDir()) / dupId), "sa propre copie liberee");
    check(d && at::availability(*d) == at::Availability::Present, "et il reste lisible");
}

void cacheClean(ai::Conversation& c) {
    std::printf("== 7. vider le cache sans perdre une piece jointe ==\n");
    check(ai::saveConversation(c), "la conversation enregistree");
    const fs::path convFile = fs::path(ai::conversationsDir()) / (c.id + ".json");
    const std::string convBytes = readAll(convFile);
    std::vector<std::pair<fs::path, std::string>> originals;
    for (const auto& e : fs::recursive_directory_iterator(ai::attachmentsDir()))
        if (e.is_regular_file()) originals.emplace_back(e.path(), readAll(e.path()));
    const ai::Attachment* xl = nullptr;
    for (const auto& m : c.messages)
        for (const auto& a : m.attachments)
            if (a.name == "Variables.xlsx") xl = &a;
    std::string before, why;
    check(xl && at::textOf(*xl, before, &why) && !before.empty(), "le texte extrait du tableur, avant");
    const fs::path cached = fs::path(ai::root()) / xl->analyses.back().textFile;
    check(fs::exists(cached), "il est dans le cache");
    std::vector<app::ai::memory::Part> parts;
    for (const auto& pi : {app::ai::memory::Part::CacheRequests, app::ai::memory::Part::CacheProcessing, app::ai::memory::Part::CacheImages,
                           app::ai::memory::Part::CacheDocuments, app::ai::memory::Part::CachePreviews, app::ai::memory::Part::CacheDownloads,
                           app::ai::memory::Part::CacheOther})
        parts.push_back(pi);
    const auto r = app::ai::memory::clearCache(parts);
    check(r.ok() && !fs::exists(cached), "le cache vide (le texte extrait parti)");
    bool intact = !originals.empty();
    for (const auto& [path, bytes] : originals) intact = intact && fs::exists(path) && readAll(path) == bytes;
    check(intact, "toutes les pieces jointes intactes, octet pour octet (" + std::to_string(originals.size()) + " fichiers)");
    check(readAll(convFile) == convBytes, "la conversation intacte");
    const auto ctx = at::buildContext(c);
    const auto* f = fileNamed(ctx, "Variables.xlsx");
    check(f && f->textMissing && f->attachment.analysis == "analysee", "le contexte sait que le texte est a refaire (l'analyse reste)");
    std::string after;
    check(at::textOf(*xl, after, &why) && after == before && fs::exists(cached), "le texte refait depuis l'original, le meme");
}

void isolation() {
    std::printf("== 8. l'isolation des pieces jointes entre conversations ==\n");
    auto c1 = newConversation("Premiere");
    auto c2 = newConversation("Seconde");
    const fs::path f1 = writeFile(src() / "x1" / "plan.txt", "plan de la premiere\n");
    const fs::path f2 = writeFile(src() / "x2" / "plan.txt", "plan de la premiere\n");   // le meme contenu, une autre conversation
    const auto* a1 = attachTo(c1, f1);
    const auto* a2 = attachTo(c2, f2);
    const std::string id1 = a1->id, id2 = a2->id;
    analyzeAll(c1);
    analyzeAll(c2);
    const auto k1 = at::buildContext(c1), k2 = at::buildContext(c2);
    check(k1.count() == 1 && k2.count() == 1 && k1.find(id1) && !k1.find(id2) && k2.find(id2) && !k2.find(id1), "chacune ne voit que ses fichiers");
    check(find(c2, id2)->duplicateOf.empty() && find(c2, id2)->file != find(c1, id1)->file, "le meme contenu dans deux conversations : deux copies (pas partagees)");
    at::Outcome o;
    o.conversation = c1.id;
    o.attachment = id2;
    check(!at::applyOutcome(c1, o) && !at::applyOutcome(c2, o), "un resultat ne va qu'a sa conversation");
}

// ---------------------------------------------------------------- 9. le volet ---
class Recorder final : public gfx::IRenderer {
public:
    gfx::Size size{1280.f, 800.f};
    gfx::Rect frame{};
    std::vector<std::string> outside, all;
    std::vector<gfx::Rect> clips;
    void beginFrame(gfx::Color) override {}
    void endFrame() override {}
    void pushClip(const gfx::Rect& r) override {
        gfx::Rect c = r;
        if (!clips.empty()) {
            const auto& o = clips.back();
            const float x0 = std::max(c.x, o.x), y0 = std::max(c.y, o.y);
            const float x1 = std::min(c.right(), o.right()), y1 = std::min(c.bottom(), o.bottom());
            c = {x0, y0, std::max(0.f, x1 - x0), std::max(0.f, y1 - y0)};
        }
        clips.push_back(c);
    }
    void popClip() override {
        if (!clips.empty()) clips.pop_back();
    }
    void fillRect(const gfx::Rect&, gfx::Color) override {}
    void strokeRect(const gfx::Rect&, gfx::Color, float) override {}
    void fillRoundedRect(const gfx::Rect&, gfx::Color, float) override {}
    void line(gfx::Point, gfx::Point, gfx::Color, float) override {}
    void drawText(gfx::Point o, std::string_view t, gfx::FontId f, gfx::Color) override {
        float x0 = o.x, y0 = o.y, x1 = o.x + measure(t, f).width, y1 = o.y + static_cast<float>(f.v);
        if (!clips.empty()) {
            const auto& c = clips.back();
            x0 = std::max(x0, c.x);
            y0 = std::max(y0, c.y);
            x1 = std::min(x1, c.right());
            y1 = std::min(y1, c.bottom());
            if (x1 <= x0 || y1 <= y0) return;
        }
        all.emplace_back(t);
        if (x0 < frame.x - 0.5f || x1 > frame.right() + 1.f || y0 < frame.y - 0.5f || y1 > frame.bottom() + 1.f) outside.emplace_back(t);
    }
    void drawTexture(const gfx::Rect&, gfx::TextureId, gfx::Color) override {}
    [[nodiscard]] gfx::TextMetrics measure(std::string_view t, gfx::FontId f) const override {
        return {0.55f * static_cast<float>(f.v) * static_cast<float>(t.size()) * 0.9f, static_cast<float>(f.v), 0.76f * f.v, 0.24f * f.v};
    }
    [[nodiscard]] float lineHeight(gfx::FontId f) const override { return static_cast<float>(f.v); }
    [[nodiscard]] gfx::Size surfaceSize() const override { return size; }
    [[nodiscard]] float dpiScale() const override { return 1.f; }
    [[nodiscard]] std::size_t fitCharacters(std::string_view t, gfx::FontId f, float maxWidth) const override {
        const float per = 0.55f * static_cast<float>(f.v) * 0.9f;
        return std::min(t.size(), static_cast<std::size_t>(std::max(0.f, maxWidth / per)));
    }
};

void paintPane(ai::AiContextPane& pane, const gfx::Rect& r, const ui::Theme& theme, const std::string& label, Recorder& rec) {
    pane.setBounds(r);
    pane.layout();
    rec.frame = r;
    rec.outside.clear();
    rec.all.clear();
    std::vector<ui::Widget*> overlays;
    const ui::PaintContext ctx{rec, theme, {0.f, 0.f, rec.size.w, rec.size.h}, 0.0, &overlays};
    pane.invalidate();
    pane.render(ctx);
    check(!rec.all.empty(), label + " : le volet se dessine");
    check(rec.outside.empty(), label + " : aucun texte hors du volet" + (rec.outside.empty() ? std::string{} : " : " + rec.outside.front()));
}

void pane(ai::Conversation& c) {
    std::printf("== 9. le volet << Contexte de la discussion >> ==\n");
    std::vector<std::string> calls;
    ai::AiContextPane::Callbacks cb;
    cb.open = [&](const std::string& id) { calls.push_back("ouvrir " + id); };
    cb.details = [&](const std::string& id) { calls.push_back("detail " + id); };
    cb.analyses = [&](const std::string& id) { calls.push_back("analyses " + id); };
    cb.remove = [&](const std::string& id) { calls.push_back("retirer " + id); };
    cb.restore = [&](const std::string& id) { calls.push_back("remettre " + id); };
    cb.toggled = [&](bool open) { calls.push_back(open ? "ouvert" : "ferme"); };
    ai::AiContextPane p("ia.contexte", cb);
    const auto ctx = at::buildContext(c);
    p.setContext(ctx);
    const std::string counter = "Fichiers joints : " + std::to_string(ctx.count());
    check(p.counterText().rfind(counter, 0) == 0, "le compteur : " + p.counterText());
    check(p.heightFor(600.f, 500.f) == 28.f && p.lines().size() == 2 && p.lines()[0] == "Contexte de la discussion", "replie : une ligne, le titre et le compteur");
    Recorder rec;
    paintPane(p, {20.f, 20.f, 600.f, p.heightFor(600.f, 500.f)}, ui::Theme::dark(), "replie", rec);
    bool counted = false;
    for (const auto& t : rec.all) counted = counted || t.rfind("Fichiers joints", 0) == 0;
    check(counted, "le compteur dessine");
    // deplier (un clic sur la ligne du haut)
    ui::MouseDown down{{40.f, 30.f}, ui::MouseButton::Left, 1, {}};
    check(p.dispatch(down) == ui::EventResult::Consumed && p.expanded() && !calls.empty() && calls.back() == "ouvert", "un clic : deplie");
    const float h = p.heightFor(600.f, 500.f);
    check(h > 28.f + 26.f + 38.f, "deplie : la section et les fichiers (" + std::to_string(h) + " px)");
    paintPane(p, {20.f, 20.f, 600.f, h}, ui::Theme::dark(), "deplie", rec);
    const auto shown = p.shown();
    check(shown.size() == ctx.count(), "les fichiers montres : ceux de la discussion (sans les retires)");
    const auto lines = p.lines();
    check(lines.size() >= 4 && lines[2] == "Fichiers joints", "la section << Fichiers joints >>");
    bool hasSt = false;
    for (const auto& l : lines) hasSt = hasSt || has(l, "prog.st \xE2\x80\x94 Programme ST");
    check(hasSt, "une ligne par fichier (nom, type, etat)");
    // les gestes
    const gfx::Rect remove = p.actionRect(0, ai::AiContextPane::Action::Remove);
    check(remove.w > 0.f, "le bouton retirer de la premiere ligne");
    ui::MouseDown onRemove{{remove.x + 4.f, remove.y + 4.f}, ui::MouseButton::Left, 1, {}};
    (void)p.dispatch(onRemove);
    check(!calls.empty() && calls.back() == "retirer " + shown[0]->attachment.id, "retirer : le rappel, avec l'id de la piece");
    const gfx::Rect an = p.actionRect(0, ai::AiContextPane::Action::Analyses);
    (void)p.dispatch(ui::MouseDown{{an.x + 4.f, an.y + 4.f}, ui::MouseButton::Left, 1, {}});
    check(calls.back() == "analyses " + shown[0]->attachment.id, "les analyses precedentes");
    const gfx::Rect row = p.rowRect(0);
    (void)p.dispatch(ui::MouseDown{{row.x + 40.f, row.y + 10.f}, ui::MouseButton::Left, 1, {}});
    check(calls.back() == "detail " + shown[0]->attachment.id, "un clic sur la ligne : le detail");
    (void)p.dispatch(ui::MouseDown{{row.x + 40.f, row.y + 10.f}, ui::MouseButton::Left, 2, {}});
    check(calls.back() == "ouvrir " + shown[0]->attachment.id, "un double-clic : ouvrir");
    // survol : les gestes dessines a la place des puces, toujours dans le volet
    (void)p.dispatch(ui::MouseMove{{row.x + 40.f, row.y + 10.f}, {0.f, 0.f}, {}});
    paintPane(p, {20.f, 20.f, 600.f, h}, ui::Theme::light(), "deplie, survole", rec);
    // la recherche
    p.setSearch("xlsx");
    check(p.shown().size() == 2, "chercher << xlsx >> : deux fichiers (" + std::to_string(p.shown().size()) + ")");
    p.setSearch("introuvablezz");
    check(p.shown().empty() && p.lines().back() == "Aucun fichier ne correspond.", "rien : dit");
    paintPane(p, {20.f, 20.f, 600.f, p.heightFor(600.f, 500.f)}, ui::Theme::dark(), "recherche sans resultat", rec);
    p.setSearch({});
    // les retires
    p.setShowRemoved(true);
    p.setBounds({20.f, 20.f, 600.f, p.heightFor(600.f, 500.f)});
    p.layout();
    check(p.shown().size() == ctx.files.size(), "les retires montres a la demande");
    const auto all = p.shown();
    std::size_t removedAt = all.size();
    for (std::size_t i = 0; i < all.size(); ++i)
        if (all[i]->attachment.removed) removedAt = i;
    if (removedAt < all.size()) {
        // amener la ligne dans la vue (la molette)
        for (int k = 0; k < 20 && p.rowRect(removedAt).w <= 0.f; ++k) (void)p.dispatch(ui::MouseWheel{{100.f, 120.f}, 0.f, -1.f, {}});
        const gfx::Rect back = p.actionRect(removedAt, ai::AiContextPane::Action::Remove);
        if (back.w > 0.f) {
            (void)p.dispatch(ui::MouseDown{{back.x + 4.f, back.y + 4.f}, ui::MouseButton::Left, 1, {}});
            check(calls.back() == "remettre " + all[removedAt]->attachment.id, "une piece retiree : << remettre >>");
        } else {
            check(false, "la ligne de la piece retiree atteinte par la molette");
        }
    } else {
        check(false, "une piece retiree dans la liste");
    }
    // un contexte vide
    ai::AiContextPane empty("ia.contexte.vide", {});
    empty.setContext(at::buildContext(newConversation("Vide")));
    empty.setExpanded(true);
    check(empty.counterText() == "Fichiers joints : 0" && empty.lines().back() == "Aucun fichier joint dans cette discussion.", "aucun fichier : dit");
    paintPane(empty, {20.f, 20.f, 360.f, empty.heightFor(360.f, 300.f)}, ui::Theme::light(), "vide, etroit", rec);
    // replier
    (void)p.dispatch(ui::MouseDown{{40.f, 30.f}, ui::MouseButton::Left, 1, {}});
    check(!p.expanded() && calls.back() == "ferme", "un clic : replie (la conversation ne change pas)");
}

} // namespace

int main(int argc, char** argv) {
    std::error_code ec;
    fs::remove_all(scratch(), ec);
    fs::create_directories(scratch(), ec);
    ai::setRoot((scratch() / "IA").string());
    // PDFium : le dossier donne (third_party/pdfium), sinon les chemins habituels ; absent, la lecture des PDF est SAUTEE
    if (argc > 1) {
#if defined(_WIN32)
        hmi::pdf::setSearchPaths({(fs::path(argv[1]) / "win-x64").string()});
#else
        hmi::pdf::setSearchPaths({(fs::path(argv[1]) / "linux-x64").string()});
#endif
    }
    auto c = newConversation("Analyse du programme PL7");
    addFiles(c);
    analyses(c);
    compareFiles(c);
    restart(c);
    missingCorruptBig(c);
    removeCopyDuplicate(c);
    cacheClean(c);
    isolation();
    pane(c);
    if (!std::getenv("AI_CONTEXTE_GARDER")) fs::remove_all(scratch(), ec);
    else std::printf("garde : %s\n", scratch().string().c_str());
    std::printf("%d verification(s), %d echec(s), %d saute(s)\n", checks, failures, skipped);
    return failures == 0 ? 0 : 1;
}
