#include "HmiSignature.hpp"

#include <algorithm>
#include <cctype>
#include <cmath>
#include <utility>

namespace hmi {

namespace {

std::string trimmed(std::string_view s) {
    while (!s.empty() && std::isspace(static_cast<unsigned char>(s.front()))) s.remove_prefix(1);
    while (!s.empty() && std::isspace(static_cast<unsigned char>(s.back()))) s.remove_suffix(1);
    return std::string(s);
}

bool in(const Box& b, double x, double y) { return b.w > 0 && b.h > 0 && b.contains(x, y); }

} // namespace

std::vector<std::string> signatureReasons(const Object& o) {
    std::vector<std::string> out;
    const std::string list = o.text("signatureReasons");
    std::size_t from = 0;
    while (from <= list.size()) {
        const auto at = list.find(';', from);
        std::string item = trimmed(std::string_view(list).substr(from, at == std::string::npos ? std::string::npos : at - from));
        if (!item.empty() && std::find(out.begin(), out.end(), item) == out.end()) out.push_back(std::move(item));
        if (at == std::string::npos) break;
        from = at + 1;
    }
    return out;
}

std::string signatureMode(const Object& o) {
    std::string m = trimmed(o.text("signature", "aucune"));
    for (auto& c : m) c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
    if (m == "simple" || m == "double") return m;
    return "aucune";
}

bool signatureCommits(const Object& o, std::string_view part) {
    const auto starts = [&](std::string_view p) { return part.substr(0, p.size()) == p; };
    switch (o.kind) {
        case Kind::Selector: return starts("position:") || part == "suivant";
        case Kind::RadioGroup: return starts("option:");
        case Kind::ComboBox: return starts("choix:");
        case Kind::DateTimePicker: return part == "valider";
        case Kind::WeeklySchedule: return starts("case:") || starts("jour:") || starts("heure:");
        default: return false;
    }
}

SignatureLayout signatureLayout(double w, double h, bool twoSigners, bool reasonList, bool chooseSigner) {
    SignatureLayout l;
    l.twoSigners = twoSigners;
    l.reasonList = reasonList;
    l.chooseSigner = chooseSigner;
    // Les lignes : titre, ce qui est signe (2), signataire, mot de passe, motif,
    // [visa : titre, compte, mot de passe], boutons, message, etat.
    const double rows = twoSigners ? 13.4 : 10.2;
    l.rowH = std::clamp((std::min(h, 760.0) - 30) / rows, 18.0, 38.0);
    l.fontSize = std::clamp(l.rowH * 0.44, 9.0, 16.0);
    const double pw = std::max(200.0, std::min(w - 24, 660.0)), ph = std::min(h - 12, l.rowH * rows + 16);
    const double px = (w - pw) / 2, py = std::max(6.0, (h - ph) / 2);
    l.panel = {px, py, pw, ph};
    const double titleH = l.rowH * 1.25;
    l.title = {px, py, pw, titleH};
    l.close = {px + pw - titleH, py, titleH, titleH};
    const double left = px + 18, inner = pw - 36;
    const double labelW = std::min(170.0, inner * 0.3);
    const double x0 = left + labelW, fieldW = std::max(40.0, left + inner - x0);
    double y = py + titleH + l.rowH * 0.3;
    l.what = {left, y, inner, l.rowH * 1.6};
    y += l.rowH * 1.9;
    const auto chooser = [&](Box& label, Box& prev, Box& value, Box& next, bool arrows) {
        label = {left, y, labelW - 8, l.rowH};
        if (arrows) {
            prev = {x0, y, l.rowH, l.rowH};
            value = {x0 + l.rowH + 6, y, std::max(10.0, fieldW - 2 * l.rowH - 12), l.rowH};
            next = {value.right() + 6, y, l.rowH, l.rowH};
        } else {
            value = {x0, y, fieldW, l.rowH};
        }
    };
    chooser(l.signerLabel, l.signerPrev, l.signer, l.signerNext, chooseSigner);
    y += l.rowH * 1.3;
    l.password = {x0, y, fieldW, l.rowH};
    y += l.rowH * 1.3;
    if (reasonList) {
        chooser(l.reasonLabel, l.reasonPrev, l.reason, l.reasonNext, true);
    } else {
        l.reasonLabel = {left, y, labelW - 8, l.rowH};
        l.reasonField = {x0, y, fieldW, l.rowH};
    }
    y += l.rowH * 1.4;
    if (twoSigners) {
        l.visaTitle = {left, y, inner, l.rowH};
        y += l.rowH * 1.1;
        chooser(l.visaLabel, l.visaPrev, l.visa, l.visaNext, true);
        y += l.rowH * 1.3;
        l.visaPassword = {x0, y, fieldW, l.rowH};
        y += l.rowH * 1.4;
    }
    const double bw = std::min(200.0, (fieldW - 10) / 2);
    l.sign = {x0, y, bw, l.rowH};
    l.cancel = {x0 + bw + 10, y, bw, l.rowH};
    y += l.rowH * 1.35;
    l.message = {left, y, inner, l.rowH};
    const double statusH = std::max(18.0, l.rowH * 0.85);
    l.status = {px, py + ph - statusH, pw, statusH};
    return l;
}

std::string signatureHit(const SignatureLayout& l, double x, double y) {
    if (!l.panel.contains(x, y)) return "dehors";
    if (in(l.close, x, y)) return "fermer";
    if (in(l.signerPrev, x, y)) return "signataire:precedent";
    if (in(l.signerNext, x, y)) return "signataire:suivant";
    if (in(l.password, x, y)) return "champ:motdepasse";
    if (in(l.reasonPrev, x, y)) return "motif:precedent";
    if (in(l.reasonNext, x, y)) return "motif:suivant";
    if (in(l.reason, x, y)) return "motif:suivant";          // toucher le motif : le suivant
    if (in(l.reasonField, x, y)) return "champ:motif";
    if (in(l.visaPrev, x, y)) return "visa:precedent";
    if (in(l.visaNext, x, y)) return "visa:suivant";
    if (in(l.visaPassword, x, y)) return "champ:visa";
    if (in(l.sign, x, y)) return "bouton:signer";
    if (in(l.cancel, x, y)) return "bouton:annuler";
    return {};
}

bool signaturePartBox(const SignatureLayout& l, std::string_view part, Box& out) {
    const auto set = [&](const Box& b) {
        if (b.w <= 0) return false;
        out = b;
        return true;
    };
    if (part == "fermer") return set(l.close);
    if (part == "signataire:precedent") return set(l.signerPrev);
    if (part == "signataire:suivant") return set(l.signerNext);
    if (part == "champ:motdepasse") return set(l.password);
    if (part == "motif:precedent") return set(l.reasonPrev);
    if (part == "motif:suivant") return set(l.reasonNext);
    if (part == "champ:motif") return set(l.reasonField);
    if (part == "visa:precedent") return set(l.visaPrev);
    if (part == "visa:suivant") return set(l.visaNext);
    if (part == "champ:visa") return set(l.visaPassword);
    if (part == "bouton:signer") return set(l.sign);
    if (part == "bouton:annuler") return set(l.cancel);
    return false;
}

} // namespace hmi
