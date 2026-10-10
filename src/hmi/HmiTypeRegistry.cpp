// =============================================================================
//  hmi/HmiTypeRegistry.cpp - 1.11.19 (refonte des scripts, lot 6) : le registre
//  des types et la regle de conversion (voir l'en-tete)
// =============================================================================
#include "HmiTypeRegistry.hpp"
#include "../core/Edition.hpp"   // 1.12.2 : XPGAnalyser IHM n'a pas d'automate

#include "HmiPopupParams.hpp"

#include <algorithm>
#include <cctype>
#include <cstdint>
#include <cstdio>
#include <limits>
#include <string>
#include <utility>

namespace hmi::typereg {
namespace {

std::string upperOf(std::string_view s) {
    std::string out(s);
    for (auto& c : out) c = static_cast<char>(std::toupper(static_cast<unsigned char>(c)));
    return out;
}
std::string trimmedOf(std::string_view s) {
    std::size_t a = 0, b = s.size();
    while (a < b && std::isspace(static_cast<unsigned char>(s[a]))) ++a;
    while (b > a && std::isspace(static_cast<unsigned char>(s[b - 1]))) --b;
    return std::string(s.substr(a, b - a));
}
bool sameText(std::string_view a, std::string_view b) noexcept {
    if (a.size() != b.size()) return false;
    for (std::size_t i = 0; i < a.size(); ++i)
        if (std::toupper(static_cast<unsigned char>(a[i])) != std::toupper(static_cast<unsigned char>(b[i]))) return false;
    return true;
}
bool identStart(char c) noexcept { return std::isalpha(static_cast<unsigned char>(c)) || c == '_'; }
bool identChar(char c) noexcept { return std::isalnum(static_cast<unsigned char>(c)) || c == '_'; }

// ---- les types de base ---------------------------------------------------------------
//  V : variable IHM ; D : declaration d'un code ; P : parametre de popup ; R : retour ;
//  O : operande. Les drapeaux reproduisent les listes d'avant le registre (kVariableTypes,
//  kLocalTypes et richLocalType, params::baseTypes et typeKnown, kBaseTypes des operateurs).
constexpr unsigned V = UseVariable, D = UseDeclaration, P = UseParameter, R = UseReturn, O = UseOperand;

struct Base {
    const char* name;
    Category    category;
    Numeric     numeric;
    unsigned    uses;
    unsigned    proposed;
    const char* doc;
};

constexpr long double kI8 = 128.0L, kI16 = 32768.0L, kI32 = 2147483648.0L, kI64 = 9223372036854775808.0L;

const Base kBase[] = {
    {"BOOL", Category::Elementary, {Family::Bool, 1, false, false, 0, 1}, V | D | P | R | O, V | D | P | R | O,
     "Bool\xC3\xA9" "en : FALSE ou TRUE."},
    {"SINT", Category::Elementary, {Family::Integer, 8, true, false, -kI8, kI8 - 1}, D | P | R | O, D | R | O,
     "Entier sign\xC3\xA9 8 bits (-128 \xC3\xA0 127)."},
    {"INT", Category::Elementary, {Family::Integer, 16, true, false, -kI16, kI16 - 1}, V | D | P | R | O, V | D | P | R | O,
     "Entier sign\xC3\xA9 16 bits (-32768 \xC3\xA0 32767)."},
    {"DINT", Category::Elementary, {Family::Integer, 32, true, false, -kI32, kI32 - 1}, V | D | P | R | O, V | D | P | R | O,
     "Entier sign\xC3\xA9 32 bits."},
    {"LINT", Category::Elementary, {Family::Integer, 64, true, false, -kI64, kI64 - 1}, D | P | R | O, O,
     "Entier sign\xC3\xA9 64 bits ; l'IHM le calcule sur 32 bits, comme un DINT."},
    {"USINT", Category::Elementary, {Family::Integer, 8, false, false, 0, 255}, D | P | R | O, D | R | O,
     "Entier non sign\xC3\xA9 8 bits (0 \xC3\xA0 255)."},
    {"UINT", Category::Elementary, {Family::Integer, 16, false, false, 0, 65535}, V | D | P | R | O, V | D | P | R | O,
     "Entier non sign\xC3\xA9 16 bits (0 \xC3\xA0 65535)."},
    {"UDINT", Category::Elementary, {Family::Integer, 32, false, false, 0, 4294967295.0L}, V | D | P | R | O, V | D | P | R | O,
     "Entier non sign\xC3\xA9 32 bits."},
    {"ULINT", Category::Elementary, {Family::Integer, 64, false, false, 0, 18446744073709551615.0L}, D | P | R | O, O,
     "Entier non sign\xC3\xA9 64 bits ; l'IHM le calcule sur 32 bits, comme un UDINT."},
    {"BYTE", Category::Elementary, {Family::Integer, 8, false, true, 0, 255}, D | P | R | O, D | R | O,
     "Mot de 8 bits (0 \xC3\xA0 255)."},
    {"WORD", Category::Elementary, {Family::Integer, 16, false, true, 0, 65535}, V | D | P | R | O, V | D | P | R | O,
     "Mot de 16 bits (0 \xC3\xA0 65535)."},
    {"DWORD", Category::Elementary, {Family::Integer, 32, false, true, 0, 4294967295.0L}, V | D | P | R | O, V | D | P | R | O,
     "Mot de 32 bits."},
    {"LWORD", Category::Elementary, {Family::Integer, 64, false, true, 0, 18446744073709551615.0L}, P | O, O,
     "Mot de 64 bits (un op\xC3\xA9rande d'op\xC3\xA9rateur)."},
    {"REAL", Category::Elementary, {Family::Real, 32, true, false, 0, 0}, V | D | P | R | O, V | D | P | R | O,
     "R\xC3\xA9" "el 32 bits (24 bits de mantisse) ; l'IHM le calcule en double pr\xC3\xA9" "cision."},   // 1.12.2 : sans automate
    {"LREAL", Category::Elementary, {Family::Real, 64, true, false, 0, 0}, V | D | P | R | O, V | D | P | R | O,
     "R\xC3\xA9" "el 64 bits."},
    {"STRING", Category::TextTime, {Family::String, 0, false, false, 0, 0}, V | D | P | R | O, V | D | P | R | O,
     "Cha\xC3\xAEne de caract\xC3\xA8res : 'texte' ; STRING[20] en borne la longueur."},
    {"TIME", Category::TextTime, {Family::Time, 32, true, false, 0, 0}, V | D | P | R | O, V | D | P | R | O,
     "Dur\xC3\xA9" "e : T#5s, T#1m30s, T#250ms."},
    // 1.12.1 : les scripts, les fonctions, les parametres, les operateurs - pas encore une
    // variable IHM (sa place Modbus), comme SINT ou BYTE.
    {"CHAR", Category::TextTime, {Family::String, 8, false, false, 0, 0}, D | P | R | O, D | R | O,
     "Un caract\xC3\xA8re : 'A' ; un texte d'un caract\xC3\xA8re pour l'IHM (STRING le re\xC3\xA7oit, l'inverse garde le premier)."},
    {"WSTRING", Category::TextTime, {Family::String, 16, false, false, 0, 0}, D | P | R | O, D | R | O,
     "Cha\xC3\xAEne Unicode : \"texte\" ; l'IHM \xC3\xA9" "crit tous ses textes en UTF-8, WSTRING et STRING s'\xC3\xA9" "changent."},
    {"DATE", Category::TextTime, {Family::Date, 64, false, false, 1, 0}, D | P | R | O, D | R | O,
     "Une date : D#2026-10-09 ; DATE - DATE est une dur\xC3\xA9" "e."},
    {"TIME_OF_DAY", Category::TextTime, {Family::Date, 64, false, false, 2, 0}, D | P | R | O, D | R | O,
     "Une heure du jour : TOD#14:30:00 (TOD) ; TOD + TIME tourne sur 24 h."},
    {"DATE_AND_TIME", Category::TextTime, {Family::Date, 64, false, false, 3, 0}, D | P | R | O, D | R | O,
     "Une date et une heure : DT#2026-10-09-14:30:00 (DT) ; DT + TIME, DT - DT ; MAINTENANT() la donne."},
};

const char* const kIec = "IEC 61131-3";

Entry baseEntry(const Base& b) {
    Entry e;
    e.key = std::string("base:") + b.name;
    e.name = b.name;
    e.category = b.category;
    e.provenance = kIec;
    e.doc = b.doc;
    e.uses = b.uses;
    e.proposed = b.proposed;
    e.numeric = b.numeric;
    return e;
}

const Base* baseByName(std::string_view upper) noexcept {
    for (const auto& b : kBase)
        if (upper == b.name) return &b;
    return nullptr;
}

// ---- la lecture d'un texte de type -------------------------------------------------------
class Reader {
public:
    Reader(const Registry& r, std::string_view text, unsigned use) : reg_(r), s_(text), use_(use) {}

    Resolved read() {
        Resolved out;
        skip();
        if (i_ >= s_.size()) {
            out.why = "un type est obligatoire";
            return out;
        }
        out = type(use_, 0);
        skip();
        if (out.ok && i_ < s_.size()) {
            out.ok = false;
            out.why = "\xC2\xAB " + std::string(s_.substr(i_)) + " \xC2\xBB en trop apr\xC3\xA8s le type";
        }
        return out;
    }

private:
    void skip() {
        while (i_ < s_.size() && std::isspace(static_cast<unsigned char>(s_[i_]))) ++i_;
    }
    bool accept(char c) {
        skip();
        if (i_ < s_.size() && s_[i_] == c) {
            ++i_;
            return true;
        }
        return false;
    }
    bool acceptText(std::string_view t) {
        skip();
        if (s_.size() - i_ >= t.size() && s_.compare(i_, t.size(), t) == 0) {
            i_ += t.size();
            return true;
        }
        return false;
    }
    std::string word() {
        skip();
        const std::size_t a = i_;
        if (i_ < s_.size() && identStart(s_[i_]))
            while (i_ < s_.size() && identChar(s_[i_])) ++i_;
        return std::string(s_.substr(a, i_ - a));
    }
    bool atWord(std::string_view upper) {
        skip();
        std::size_t j = i_;
        while (j < s_.size() && identChar(s_[j])) ++j;
        return sameText(s_.substr(i_, j - i_), upper);
    }
    // Une borne : un entier signe.
    bool bound(long long& v) {
        skip();
        const std::size_t a = i_;
        if (i_ < s_.size() && (s_[i_] == '-' || s_[i_] == '+')) ++i_;
        const std::size_t d = i_;
        while (i_ < s_.size() && std::isdigit(static_cast<unsigned char>(s_[i_]))) ++i_;
        if (i_ == d) {
            i_ = a;
            return false;
        }
        try {
            v = std::stoll(std::string(s_.substr(a, i_ - a)));
        } catch (...) {
            return false;
        }
        return true;
    }
    static Resolved fail(std::string why) {
        Resolved r;
        r.why = std::move(why);
        return r;
    }
    // Un usage refuse : le type est connu, mais pas permis ici.
    static std::string useText(unsigned use) {
        if (use == UseVariable) return "une variable IHM";
        if (use == UseDeclaration) return "une d\xC3\xA9" "claration";
        if (use == UseParameter) return "un param\xC3\xA8tre de popup";
        if (use == UseReturn) return "un retour de fonction";
        if (use == UseOperand) return "un op\xC3\xA9rande";
        return "cet usage";
    }

    Resolved type(unsigned use, int depth) {
        if (depth > 8) return fail("type trop imbriqu\xC3\xA9");
        const std::size_t start = i_;
        skip();
        if (atWord("ARRAY")) return array(use, depth);
        // 1.12.2 : LIST OF T, VECTOR OF T, TUPLE(T1, T2...).
        if (atWord("LIST") || atWord("VECTOR")) {
            std::size_t save = i_;
            const bool vector = atWord("VECTOR");
            (void)word();
            if (atWord("OF")) {
                (void)word();
                return list(use, depth, vector);
            }
            i_ = save;                                  // un type IHM nomme LIST : un nom
        }
        if (atWord("TUPLE")) {
            std::size_t save = i_;
            (void)word();
            skip();
            if (i_ < s_.size() && s_[i_] == '(') return tuple(use, depth);
            i_ = save;
        }
        if (atWord("MAP")) {
            std::size_t save = i_;
            (void)word();
            skip();
            if (i_ < s_.size() && s_[i_] == '[') return map(use, depth);
            i_ = save;                                  // MAP_ITERATOR : un nom
        }
        if (atWord("REF_TO")) {
            (void)word();
            return reference(use, depth, "REF_TO ", "ref:");
        }
        if (atWord("REFERENCE")) {
            (void)word();
            if (!atWord("TO")) return fail("REFERENCE TO attendu");
            (void)word();
            return reference(use, depth, "REF_TO ", "ref:");
        }
        if (atWord("POINTER")) {
            (void)word();
            if (!atWord("TO")) return fail("POINTER TO attendu");
            (void)word();
            return reference(use, depth, "POINTER TO ", "ptr:");
        }
        const std::string name = word();
        if (name.empty()) {
            i_ = start;
            return fail("un nom de type est attendu");
        }
        const std::string up = upperOf(name);
        if (up == "MAP_ITERATOR" || up == "ITERATOR") {
            Resolved r;
            r.text = "MAP_ITERATOR";
            r.key = "iter";
            r.category = Category::Collection;
            r.ok = (use & (UseDeclaration | UseReturn)) != 0;
            if (!r.ok) r.why = "MAP_ITERATOR ne sert qu'\xC3\xA0 " + useText(UseDeclaration);
            return r;
        }
        if (up == "STRING") {
            std::string text = "STRING";
            const std::size_t save = i_;
            if (accept('[')) {
                long long n = 0;
                if (!bound(n) || n < 1 || !accept(']')) {
                    i_ = save;
                    return fail("STRING[n] : une longueur enti\xC3\xA8re, 1 ou plus");
                }
                text += "[" + std::to_string(n) + "]";
            }
            Resolved r = named("STRING", use);
            r.text = text;
            return r;
        }
        return named(name, use);
    }

    Resolved named(const std::string& name, unsigned use) {
        Resolved r;
        const Entry* e = reg_.byName(name);
        if (!e) {
            r.text = name;
            r.missing = true;
            r.unknown = name;
            r.why = "type \xC2\xAB " + name + " \xC2\xBB inconnu : ni un type de base, ni un type IHM du projet"
                    + std::string((use & UseParameter) ? ", ni un DDT du programme" : "");
            return r;
        }
        r.text = e->name;
        r.key = e->key;
        r.category = e->category;
        r.entry = e;
        r.ok = e->usable(use);
        if (!r.ok) r.why = e->name + " ne convient pas \xC3\xA0 " + useText(use);
        return r;
    }

    Resolved array(unsigned use, int depth) {
        (void)word();                                   // ARRAY
        if (!accept('[')) return fail("ARRAY : [ attendu apr\xC3\xA8s ARRAY");
        std::vector<std::pair<long long, long long>> dims;
        for (;;) {
            long long lo = 0, hi = 0;
            if (!bound(lo)) return fail("ARRAY : une borne enti\xC3\xA8re est attendue");
            if (!acceptText("..")) return fail("ARRAY : .. attendu entre les bornes");
            if (!bound(hi)) return fail("ARRAY : une borne enti\xC3\xA8re est attendue");
            if (hi < lo) return fail("ARRAY : la borne haute est sous la borne basse");
            dims.emplace_back(lo, hi);
            if (!accept(',')) break;
        }
        if (!accept(']')) return fail("ARRAY : ] attendu apr\xC3\xA8s les bornes");
        if (!atWord("OF")) return fail("ARRAY : OF attendu apr\xC3\xA8s les bornes");
        (void)word();
        // Un tableau d'une variable IHM ou d'un parametre de popup : 1 ou 2 dimensions.
        if (dims.size() > 2 && (use & (UseVariable | UseParameter)) && !(use & UseDeclaration))
            return fail("ARRAY : deux dimensions au plus pour " + useText(use & (UseVariable | UseParameter)));
        // L'element : les memes usages (un retour de fonction se lit comme une declaration).
        unsigned elementUse = use & (UseVariable | UseDeclaration | UseParameter);
        if (use & UseReturn) elementUse |= UseDeclaration;
        Resolved e = type(elementUse, depth + 1);
        Resolved r;
        std::string b, k;
        for (const auto& [lo, hi] : dims) {
            b += (b.empty() ? "" : ", ") + std::to_string(lo) + ".." + std::to_string(hi);
            k += (k.empty() ? "" : ",") + std::to_string(lo) + ".." + std::to_string(hi);
        }
        r.text = "ARRAY[" + b + "] OF " + e.text;
        r.key = "array[" + k + "]:" + e.key;
        r.category = Category::Collection;
        r.entry = e.entry;
        r.missing = e.missing;
        r.unknown = e.unknown;
        r.dynamic = e.dynamic;                          // 1.12.2 : un tableau de listes...
        r.rich = e.rich;
        r.ok = e.ok && (use & (UseVariable | UseDeclaration | UseParameter | UseReturn)) != 0;
        r.why = e.ok ? (r.ok ? std::string{} : "un tableau ne convient pas \xC3\xA0 " + useText(use)) : e.why;
        return r;
    }

    Resolved map(unsigned use, int depth) {
        if (!accept('[')) return fail("MAP : [ attendu");
        Resolved key = type(UseDeclaration, depth + 1);
        if (!key.ok && !key.missing) return fail("MAP : " + key.why);
        // Une cle d'un nom inconnu ici (une enumeration, lue sans le projet) : on la suit.
        const bool keyOk = key.missing || key.category == Category::Enumeration || key.text == "STRING"
                           || (key.entry && key.entry->numeric.family == Family::Integer);
        if (!keyOk) return fail("MAP : une cl\xC3\xA9 est un STRING, un entier ou une \xC3\xA9num\xC3\xA9ration");
        if (!accept(']')) return fail("MAP : ] attendu apr\xC3\xA8s la cl\xC3\xA9");
        if (!atWord("OF")) return fail("MAP : OF attendu");
        (void)word();
        Resolved e = type(UseDeclaration, depth + 1);
        Resolved r;
        r.text = "MAP[" + key.text + "] OF " + e.text;
        r.key = "map[" + key.key + "]:" + e.key;
        r.category = Category::Collection;
        r.entry = e.entry;
        r.missing = e.missing || key.missing;
        r.unknown = key.missing ? key.unknown : e.unknown;
        r.dynamic = true;
        r.rich = true;
        if (key.missing) {
            r.ok = false;
            r.why = key.why;
            return r;
        }
        // 1.12.2 : une variable IHM aussi (dans la memoire de l'IHM) ; pas un parametre de popup.
        r.ok = e.ok && (use & (UseVariable | UseDeclaration | UseReturn)) != 0;
        r.why = e.ok ? (r.ok ? std::string{} : "une MAP ne convient pas \xC3\xA0 " + useText(use)) : e.why;
        return r;
    }

    // 1.12.2 : LIST OF T, VECTOR OF T - une suite de taille variable (indices a partir de 0).
    Resolved list(unsigned use, int depth, bool vector) {
        Resolved e = type(UseDeclaration, depth + 1);
        Resolved r;
        const std::string head = vector ? "VECTOR" : "LIST";
        r.text = head + " OF " + e.text;
        r.key = (vector ? "vector:" : "list:") + e.key;
        r.category = Category::Collection;
        r.entry = e.entry;
        r.missing = e.missing;
        r.unknown = e.unknown;
        r.dynamic = true;
        r.rich = true;
        r.ok = e.ok && (use & (UseVariable | UseDeclaration | UseReturn)) != 0;
        r.why = e.ok ? (r.ok ? std::string{} : "une " + head + " ne convient pas \xC3\xA0 " + useText(use)) : e.why;
        return r;
    }

    // 1.12.2 : TUPLE(T1, T2...) - des valeurs de types fixes, t.Item1, t.Item2...
    Resolved tuple(unsigned use, int depth) {
        if (!accept('(')) return fail("TUPLE : ( attendu");
        Resolved r;
        r.category = Category::Collection;
        r.rich = true;
        r.ok = true;
        std::string text, key;
        int count = 0;
        skip();
        if (i_ < s_.size() && s_[i_] == ')') return fail("TUPLE : au moins un type, TUPLE(INT, STRING)");
        for (;;) {
            Resolved e = type(UseDeclaration, depth + 1);
            if (!e.ok && !e.missing) return e;
            text += (text.empty() ? "" : ", ") + e.text;
            key += (key.empty() ? "" : ",") + e.key;
            if (e.missing && !r.missing) {
                r.missing = true;
                r.unknown = e.unknown;
                r.why = e.why;
                r.ok = false;
            }
            r.dynamic = r.dynamic || e.dynamic;
            if (!r.entry) r.entry = e.entry;
            ++count;
            if (!accept(',')) break;
        }
        if (!accept(')')) return fail("TUPLE : ) attendu apr\xC3\xA8s les types");
        if (count > 16) return fail("TUPLE : 16 valeurs au plus");
        r.text = "TUPLE(" + text + ")";
        r.key = "tuple(" + key + ")";
        if (r.ok && (use & (UseVariable | UseDeclaration | UseReturn)) == 0) {
            r.ok = false;
            r.why = "un TUPLE ne convient pas \xC3\xA0 " + useText(use);
        }
        return r;
    }

    Resolved reference(unsigned use, int depth, const char* word, const char* keyPrefix) {
        Resolved e = type(UseDeclaration, depth + 1);
        Resolved r;
        r.text = word + e.text;
        r.key = keyPrefix + e.key;
        r.category = Category::Reference;
        r.entry = e.entry;
        r.missing = e.missing;
        r.unknown = e.unknown;
        r.ok = e.ok && (use & (UseDeclaration | UseReturn)) != 0;
        r.why = e.ok ? (r.ok ? std::string{} : "une r\xC3\xA9" "f\xC3\xA9rence ne sert qu'\xC3\xA0 " + useText(UseDeclaration)) : e.why;
        return r;
    }

    const Registry&  reg_;
    std::string_view s_;
    unsigned         use_;
    std::size_t      i_{0};
};

std::string intText(long double v) {
    char buf[64];
    std::snprintf(buf, sizeof buf, "%.0Lf", v);
    return buf;
}

} // namespace

// ------------------------------------------------------------------ libelles --
std::string_view categoryLabel(Category c) noexcept {
    switch (c) {
        case Category::Elementary: return "\xC3\x89l\xC3\xA9mentaires";
        case Category::TextTime: return "Textes, dur\xC3\xA9" "es et dates";   // 1.12.1 : + CHAR, WSTRING, DATE, TOD, DT
        case Category::Collection: return "Tableaux et collections";
        case Category::Structure: return "Structures IHM";
        case Category::Enumeration: return "\xC3\x89num\xC3\xA9rations";
        case Category::PlcType: return "DDT de l'API";
        case Category::Reference: return "R\xC3\xA9" "f\xC3\xA9rences";
        case Category::Generic: return "G\xC3\xA9n\xC3\xA9riques";
        case Category::Void: return "Sans valeur";
    }
    return {};
}

std::string_view conversionLabel(Conversion c) noexcept {
    switch (c) {
        case Conversion::Exact: return "exacte";
        case Conversion::Widening: return "\xC3\xA9largie";
        case Conversion::Lossy: return "avec perte possible";
        case Conversion::Narrowing: return "r\xC3\xA9tr\xC3\xA9" "cie";
        case Conversion::Forbidden: return "interdite";
    }
    return {};
}

// ------------------------------------------------------------------ le registre --
Registry::Registry() {
    for (const auto& b : kBase) entries_.push_back(baseEntry(b));
    Entry any;
    any.key = "any";
    any.name = "ANY";
    any.category = Category::Generic;
    any.provenance = "XPGAnalyser";
    any.doc = "N'importe quel type : le param\xC3\xA8tre prend ce qu'on lui donne.";
    any.uses = UseParameter;
    any.proposed = UseParameter;
    entries_.push_back(std::move(any));
    Entry none;
    none.key = "void";
    none.name = "Aucun";
    none.category = Category::Void;
    none.provenance = "XPGAnalyser";
    none.doc = "Pas de valeur de retour : la fonction est une proc\xC3\xA9" "dure.";
    none.uses = UseReturn;
    none.proposed = UseReturn;
    entries_.push_back(std::move(none));
}

std::shared_ptr<const Registry> Registry::build(const Project& p, const params::PlcTypes* plc) {
    auto r = std::make_shared<Registry>();
    for (const auto& t : p.programs.types) {
        if (t.name.empty() || r->byName(t.name)) continue;          // un nom de base, ou en double : le premier
        Entry e;
        e.key = "ihm:" + std::to_string(t.id);
        e.name = t.name;
        e.id = t.id;
        const bool en = t.kind == HmiTypeKind::Enumeration;
        e.category = en ? Category::Enumeration : Category::Structure;
        e.provenance = en ? "Projet \xC2\xB7 \xC3\x89num\xC3\xA9rations" : "Projet \xC2\xB7 Types IHM";
        e.doc = !t.description.empty() ? t.description
                                      : (en ? "Une \xC3\xA9num\xC3\xA9ration du projet." : "Une structure du projet.");
        e.uses = V | D | P | R | O;
        e.proposed = V | D | P | R | O;
        for (const auto& m : t.members) e.members.emplace_back(m.name, m.type);
        for (const auto& v : t.values) e.values.push_back(v.name);
        r->entries_.push_back(std::move(e));
    }
    if (plc && plc->names)
        for (auto& n : plc->names()) {
            if (n.empty() || r->byName(n)) continue;
            Entry e;
            e.key = "api:" + upperOf(n);
            e.name = n;
            e.category = Category::PlcType;
            e.provenance = "API \xC2\xB7 DDT du programme";
            e.doc = "Un type d\xC3\xA9riv\xC3\xA9 (DDT) du programme de l'automate.";
            e.uses = P;
            e.proposed = P;
            if (plc->members) e.members = plc->members(n);
            r->entries_.push_back(std::move(e));
        }
    return r;
}

const Entry* Registry::byKey(std::string_view key) const noexcept {
    for (const auto& e : entries_)
        if (e.key == key) return &e;
    return nullptr;
}

const Entry* Registry::byName(std::string_view name) const noexcept {
    std::string_view n = name;
    while (!n.empty() && std::isspace(static_cast<unsigned char>(n.front()))) n.remove_prefix(1);
    while (!n.empty() && std::isspace(static_cast<unsigned char>(n.back()))) n.remove_suffix(1);
    if (n.size() > 7 && sameText(n.substr(0, 7), "STRING[")) n = n.substr(0, 6);
    if (n.size() > 8 && sameText(n.substr(0, 8), "WSTRING[")) n = n.substr(0, 7);   // 1.12.1
    if (sameText(n, "EBOOL")) n = "BOOL";                 // un BOOL de l'automate
    if (sameText(n, "TOD")) n = "TIME_OF_DAY";            // 1.12.1 : les noms courts de la norme
    if (sameText(n, "DT")) n = "DATE_AND_TIME";
    for (const auto& e : entries_)
        if (sameText(e.name, n)) return &e;
    return nullptr;
}

std::vector<const Entry*> Registry::usable(unsigned use, bool proposedOnly) const {
    std::vector<const Entry*> out;
    for (const auto& e : entries_)
        if ((proposedOnly ? e.proposed : e.uses) & use) out.push_back(&e);
    return out;
}

std::vector<std::string> Registry::names(unsigned use, bool proposedOnly) const {
    std::vector<std::string> out;
    for (const auto* e : usable(use, proposedOnly)) out.push_back(e->name);
    return out;
}

Resolved Registry::resolve(std::string_view text, unsigned use) const {
    Reader rd(*this, text, use);
    return rd.read();
}

std::string Registry::nameOfKey(std::string_view key) const {
    const auto* e = byKey(key);
    return e ? e->name : std::string{};
}

const Registry& baseRegistry() {
    static const Registry r;
    return r;
}

Numeric numericOf(std::string_view type) noexcept {
    std::string u;
    for (const char c : type)
        if (!std::isspace(static_cast<unsigned char>(c))) u += static_cast<char>(std::toupper(static_cast<unsigned char>(c)));
    if (u.rfind("STRING[", 0) == 0) u = "STRING";
    if (u.rfind("WSTRING[", 0) == 0) u = "WSTRING";       // 1.12.1
    if (u == "EBOOL") u = "BOOL";
    if (u == "TOD") u = "TIME_OF_DAY";
    if (u == "DT") u = "DATE_AND_TIME";
    if (const auto* b = baseByName(u)) return b->numeric;
    return {};
}

std::string comparable(std::string_view type) {
    std::string out;
    for (const char c : type)
        if (!std::isspace(static_cast<unsigned char>(c))) out += static_cast<char>(std::toupper(static_cast<unsigned char>(c)));
    if (out.empty()) return "ANY";
    if (out.rfind("STRING[", 0) == 0) return "STRING";
    if (out.rfind("WSTRING[", 0) == 0) return "WSTRING";  // 1.12.1
    if (out == "EBOOL") return "BOOL";
    if (out == "TOD") return "TIME_OF_DAY";
    if (out == "DT") return "DATE_AND_TIME";
    return out;
}

bool isNumber(std::string_view type) noexcept {
    const auto f = numericOf(type).family;
    return f == Family::Integer || f == Family::Real;
}

bool isInteger(std::string_view type) noexcept { return numericOf(type).family == Family::Integer; }

sim::Type simTypeOf(std::string_view type) noexcept {
    const Numeric n = numericOf(type);
    switch (n.family) {
        case Family::Bool: return sim::Type::Bool;
        case Family::Real: return sim::Type::Real;
        case Family::String: return sim::Type::String;
        case Family::Time: return sim::Type::Time;
        case Family::Date:                                   // 1.12.1 : low dit lequel
            return n.low == 1 ? sim::Type::Date : n.low == 2 ? sim::Type::Tod : sim::Type::Dt;
        case Family::Integer:
            if (n.bitString) return n.bits <= 8 ? sim::Type::Byte : n.bits <= 16 ? sim::Type::Word : sim::Type::DWord;
            if (n.isSigned) return n.bits <= 16 ? sim::Type::Int : sim::Type::DInt;
            return n.bits <= 16 ? sim::Type::UInt : sim::Type::UDInt;
        case Family::None: break;
    }
    return sim::Type::Unknown;
}

// ------------------------------------------------------------------ la regle --
Verdict conversion(std::string_view from, std::string_view to) {
    Verdict v;
    const auto done = [&v](Conversion k, int cost, std::string why = {}) {
        v.kind = k;
        v.cost = cost;
        v.why = std::move(why);
        return v;
    };
    if (trimmedOf(from).empty()) return done(Conversion::Exact, 0);        // inconnu : aucune erreur sure
    const std::string f = comparable(from), t = comparable(to);
    if (f == "ANY" || t == "ANY" || f == t) return done(Conversion::Exact, 0);
    const Numeric a = numericOf(f), b = numericOf(t);
    const std::string fn = trimmedOf(from), tn = trimmedOf(to);
    if (a.family == Family::Integer && b.family == Family::Integer) {
        if (b.low <= a.low && b.high >= a.high) return done(Conversion::Widening, 1);
        if (b.bits >= a.bits)
            return done(Conversion::Lossy, 4,
                        fn + " vers " + tn + " : une valeur hors de " + intText(b.low) + ".." + intText(b.high) + " ne tient pas");
        return done(Conversion::Narrowing, -1, fn + " vers " + tn + " : un entier plus petit - \xC3\xA9" "crivez TO_" + t + "(\xE2\x80\xA6)");
    }
    if (a.family == Family::Integer && b.family == Family::Real) {
        if (b.bits >= 64 || a.bits <= 16) return done(Conversion::Widening, 2);
        return done(Conversion::Lossy, 4, fn + " vers " + tn + (core::hasApi() ? " : un REAL de l'automate garde 24 bits de pr\xC3\xA9" "cision"
                                                                               : " : un REAL garde 24 bits de pr\xC3\xA9" "cision"));   // 1.12.2
    }
    if (a.family == Family::Real && b.family == Family::Real) {
        if (b.bits >= a.bits) return done(Conversion::Widening, 1);
        return done(Conversion::Lossy, 4, fn + " vers " + tn + " : moins de pr\xC3\xA9" "cision");
    }
    // 1.12.1 : CHAR, STRING, WSTRING entre eux ; les dates, chacune la sienne.
    if (a.family == Family::String && b.family == Family::String) {
        if (t == "CHAR") return done(Conversion::Lossy, 4, fn + " vers CHAR : le premier caract\xC3\xA8re seul");
        return done(Conversion::Widening, 1);
    }
    if (a.family == Family::Date || b.family == Family::Date) {
        if (f == "DATE_AND_TIME" && (t == "DATE" || t == "TIME_OF_DAY"))
            return done(Conversion::Forbidden, -1, "DATE_AND_TIME vers " + tn + " : \xC3\xA9" "crivez DT_TO_" + (t == "DATE" ? "DATE" : "TOD") + "(\xE2\x80\xA6)");
        return done(Conversion::Forbidden, -1, fn + " n'est pas " + tn + " : une date, une heure du jour, une date et heure et une dur\xC3\xA9" "e ne s'\xC3\xA9" "changent pas");
    }
    if (a.family == Family::Real && b.family == Family::Integer)
        return done(Conversion::Forbidden, -1, "un r\xC3\xA9" "el ne devient pas " + tn + " sans conversion : \xC3\xA9" "crivez TO_" + t + "(\xE2\x80\xA6)");
    if (b.family == Family::String && a.family != Family::None)
        return done(Conversion::Forbidden, -1, fn + " vers STRING : \xC3\xA9" "crivez TO_STRING(\xE2\x80\xA6)");
    if (a.family == Family::String && (b.family == Family::Integer || b.family == Family::Real))
        return done(Conversion::Forbidden, -1, "un texte n'est pas un nombre");
    return done(Conversion::Forbidden, -1, fn + " n'est pas compatible avec " + tn);
}

namespace {
// `from` remplace par `to`, mot entier, sans casse (un texte de type n'a ni chaine ni commentaire).
std::string replaceWord(const std::string& text, const std::string& from, const std::string& to) {
    std::string out;
    std::size_t i = 0;
    while (i < text.size()) {
        if (identStart(text[i]) && (i == 0 || !identChar(text[i - 1]))) {
            std::size_t j = i;
            while (j < text.size() && identChar(text[j])) ++j;
            const std::string_view w(text.data() + i, j - i);
            out += sameText(w, from) ? to : std::string(w);
            i = j;
            continue;
        }
        out += text[i++];
    }
    return out;
}
} // namespace

std::vector<std::string> followTypeKeys(Project& p) {
    std::vector<std::string> said;
    std::shared_ptr<const Registry> reg;
    forEachDeclarations(p, [&](std::vector<Declaration>& list) {
        for (auto& d : list) {
            if (d.typeKey.empty()) continue;
            const std::string key = std::exchange(d.typeKey, std::string{});
            if (!reg) reg = Registry::build(p);
            const auto r = reg->resolve(d.type);
            if (!r.missing || r.unknown.empty()) continue;            // le nom se lit : rien a suivre
            const auto at = key.rfind("ihm:");
            if (at == std::string::npos) continue;
            std::size_t end = at + 4;
            while (end < key.size() && std::isdigit(static_cast<unsigned char>(key[end]))) ++end;
            const std::string now = reg->nameOfKey(key.substr(at, end - at));
            if (now.empty()) continue;                                 // le type n'est plus : la faute le dira
            const auto again = reg->resolve(replaceWord(d.type, r.unknown, now));
            if (!again.ok) continue;
            said.push_back(d.name + " : son type " + r.unknown + " est devenu " + now + " (suivi par sa cl\xC3\xA9 " + key + ")");
            d.type = again.text;
        }
    });
    return said;
}

bool valueFits(long long value, std::string_view to) noexcept {
    const Numeric n = numericOf(to);
    if (n.family != Family::Integer) return false;
    const auto v = static_cast<long double>(value);
    return v >= n.low && v <= n.high;
}

} // namespace hmi::typereg
