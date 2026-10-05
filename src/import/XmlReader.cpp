#include "XmlReader.hpp"

#include <algorithm>
#include <charconv>
#include <cstdio>
#include <filesystem>
#include <fstream>

namespace importer {
namespace {

constexpr bool isSpace(char c) noexcept {
    return c == ' ' || c == '\t' || c == '\r' || c == '\n';
}
constexpr bool isNameChar(char c) noexcept {
    return !isSpace(c) && c != '>' && c != '/' && c != '=' && c != '<';
}

} // namespace

XmlReader::XmlReader(std::string_view buffer) noexcept : buf_(buffer) {
    // Tolerate a UTF-8 BOM: Control Expert writes one on some locales.
    if (buf_.size() >= 3 && static_cast<unsigned char>(buf_[0]) == 0xEF
        && static_cast<unsigned char>(buf_[1]) == 0xBB
        && static_cast<unsigned char>(buf_[2]) == 0xBF)
        pos_ = 3;
}

void XmlReader::skipSpace() noexcept {
    while (pos_ < buf_.size() && isSpace(buf_[pos_])) {
        if (buf_[pos_] == '\n') ++line_;
        ++pos_;
    }
}

void XmlReader::countLines(std::size_t from, std::size_t to) noexcept {
    line_ += static_cast<std::size_t>(std::count(buf_.begin() + static_cast<long>(from),
                                                 buf_.begin() + static_cast<long>(to), '\n'));
}

core::Err<core::Error> XmlReader::malformed(std::string what) const {
    return core::fail(core::ErrorCode::XmlMalformed,
                      "line " + std::to_string(line_) + ": " + std::move(what));
}

std::string XmlReader::decodeEntities(std::string_view raw) {
    if (raw.find('&') == std::string_view::npos) return std::string(raw);

    std::string out;
    out.reserve(raw.size());
    for (std::size_t i = 0; i < raw.size();) {
        if (raw[i] != '&') { out.push_back(raw[i++]); continue; }
        const auto end = raw.find(';', i);
        if (end == std::string_view::npos || end - i > 10) { out.push_back(raw[i++]); continue; }
        const auto body = raw.substr(i + 1, end - i - 1);
        if      (body == "lt")   out.push_back('<');
        else if (body == "gt")   out.push_back('>');
        else if (body == "amp")  out.push_back('&');
        else if (body == "quot") out.push_back('"');
        else if (body == "apos") out.push_back('\'');
        else if (!body.empty() && body[0] == '#') {
            unsigned cp = 0;
            const bool hex = body.size() > 1 && (body[1] == 'x' || body[1] == 'X');
            const auto digits = body.substr(hex ? 2 : 1);
            const auto* first = digits.data();
            const auto* last  = digits.data() + digits.size();
            if (std::from_chars(first, last, cp, hex ? 16 : 10).ec == std::errc{}) {
                // Minimal UTF-8 encoder.
                if      (cp < 0x80)   out.push_back(static_cast<char>(cp));
                else if (cp < 0x800) { out.push_back(static_cast<char>(0xC0 | (cp >> 6)));
                                       out.push_back(static_cast<char>(0x80 | (cp & 0x3F))); }
                else if (cp < 0x10000) { out.push_back(static_cast<char>(0xE0 | (cp >> 12)));
                                         out.push_back(static_cast<char>(0x80 | ((cp >> 6) & 0x3F)));
                                         out.push_back(static_cast<char>(0x80 | (cp & 0x3F))); }
                else { out.push_back(static_cast<char>(0xF0 | (cp >> 18)));
                       out.push_back(static_cast<char>(0x80 | ((cp >> 12) & 0x3F)));
                       out.push_back(static_cast<char>(0x80 | ((cp >> 6) & 0x3F)));
                       out.push_back(static_cast<char>(0x80 | (cp & 0x3F))); }
            } else {
                out.append(raw.substr(i, end - i + 1));
            }
        } else {
            out.append(raw.substr(i, end - i + 1));   // unknown entity: pass through
        }
        i = end + 1;
    }
    return out;
}

core::Result<XmlEvent> XmlReader::next() {
    // A self-closing element produces StartElement then a synthetic EndElement.
    if (pendingEnd_) {
        pendingEnd_ = false;
        event_      = XmlEvent::EndElement;
        --depth_;
        return event_;
    }

    attrs_.clear();
    selfClosing_ = false;
    text_        = {};

    if (pos_ >= buf_.size()) { event_ = XmlEvent::EndOfDocument; return event_; }

    // Character data between elements.
    if (buf_[pos_] != '<') {
        const auto start = pos_;
        const auto lt    = buf_.find('<', pos_);
        const auto end   = (lt == std::string_view::npos) ? buf_.size() : lt;
        countLines(start, end);
        text_ = buf_.substr(start, end - start);
        pos_  = end;
        // Whitespace-only runs between elements are structural, not content.
        if (std::all_of(text_.begin(), text_.end(), isSpace)) return next();
        event_ = XmlEvent::Text;
        return event_;
    }

    // Declarations, comments, CDATA, DOCTYPE.
    if (buf_.compare(pos_, 4, "<!--") == 0) {
        const auto end = buf_.find("-->", pos_ + 4);
        if (end == std::string_view::npos) return malformed("unterminated comment");
        countLines(pos_, end);
        pos_ = end + 3;
        return next();
    }
    if (buf_.compare(pos_, 9, "<![CDATA[") == 0) {
        const auto end = buf_.find("]]>", pos_ + 9);
        if (end == std::string_view::npos) return malformed("unterminated CDATA");
        text_ = buf_.substr(pos_ + 9, end - pos_ - 9);
        countLines(pos_, end);
        pos_   = end + 3;
        event_ = XmlEvent::Text;
        return event_;
    }
    if (pos_ + 1 < buf_.size() && (buf_[pos_ + 1] == '?' || buf_[pos_ + 1] == '!')) {
        const auto end = buf_.find('>', pos_);
        if (end == std::string_view::npos) return malformed("unterminated declaration");
        countLines(pos_, end);
        pos_ = end + 1;
        return next();
    }

    // Closing tag.
    if (pos_ + 1 < buf_.size() && buf_[pos_ + 1] == '/') {
        pos_ += 2;
        const auto start = pos_;
        while (pos_ < buf_.size() && isNameChar(buf_[pos_])) ++pos_;
        name_ = buf_.substr(start, pos_ - start);
        skipSpace();
        if (pos_ >= buf_.size() || buf_[pos_] != '>') return malformed("bad closing tag");
        ++pos_;
        if (depth_ == 0) return malformed("closing tag </" + std::string(name_) + "> without opener");
        --depth_;
        event_ = XmlEvent::EndElement;
        return event_;
    }

    // Opening tag.
    ++pos_;
    const auto nameStart = pos_;
    while (pos_ < buf_.size() && isNameChar(buf_[pos_])) ++pos_;
    if (pos_ == nameStart) return malformed("empty element name");
    name_ = buf_.substr(nameStart, pos_ - nameStart);

    for (;;) {
        skipSpace();
        if (pos_ >= buf_.size()) return malformed("unterminated element <" + std::string(name_) + ">");
        if (buf_[pos_] == '>') { ++pos_; break; }
        if (buf_[pos_] == '/') {
            if (pos_ + 1 >= buf_.size() || buf_[pos_ + 1] != '>') return malformed("stray '/'");
            pos_ += 2;
            selfClosing_ = true;
            break;
        }
        const auto keyStart = pos_;
        while (pos_ < buf_.size() && isNameChar(buf_[pos_])) ++pos_;
        if (pos_ == keyStart) return malformed("bad attribute name");
        const auto key = buf_.substr(keyStart, pos_ - keyStart);

        skipSpace();
        if (pos_ >= buf_.size() || buf_[pos_] != '=') return malformed("expected '=' after " + std::string(key));
        ++pos_;
        skipSpace();
        if (pos_ >= buf_.size() || (buf_[pos_] != '"' && buf_[pos_] != '\''))
            return malformed("unquoted value for " + std::string(key));
        const char quote = buf_[pos_++];
        const auto valStart = pos_;
        const auto valEnd   = buf_.find(quote, pos_);
        if (valEnd == std::string_view::npos) return malformed("unterminated value for " + std::string(key));
        countLines(valStart, valEnd);
        attrs_.push_back(XmlAttribute{key, buf_.substr(valStart, valEnd - valStart)});
        pos_ = valEnd + 1;
    }

    ++depth_;
    pendingEnd_ = selfClosing_;
    event_      = XmlEvent::StartElement;
    return event_;
}

std::string_view XmlReader::rawAttr(std::string_view key) const noexcept {
    for (const auto& a : attrs_) if (a.name == key) return a.rawValue;
    return {};
}

bool XmlReader::has(std::string_view key) const noexcept {
    return std::any_of(attrs_.begin(), attrs_.end(),
                       [&](const XmlAttribute& a) { return a.name == key; });
}

std::string XmlReader::attr(std::string_view key, std::string fallback) const {
    for (const auto& a : attrs_) if (a.name == key) return decodeEntities(a.rawValue);
    return fallback;
}

core::Result<std::string> XmlReader::requireAttr(std::string_view key) const {
    for (const auto& a : attrs_) if (a.name == key) return decodeEntities(a.rawValue);
    return core::fail(core::ErrorCode::XmlMissingAttribute,
                      "line " + std::to_string(line_) + ": <" + std::string(name_)
                          + "> has no '" + std::string(key) + "'");
}

core::Result<std::string> XmlReader::readElementText() {
    if (selfClosing_) { pendingEnd_ = false; --depth_; return std::string{}; }

    const std::size_t target = depth_ - 1;
    std::string out;
    for (;;) {
        auto ev = next();
        if (!ev) return core::Err<core::Error>(ev.error());
        if (*ev == XmlEvent::Text)       out += decodeEntities(text_);
        else if (*ev == XmlEvent::EndElement && depth_ == target) return out;
        else if (*ev == XmlEvent::EndOfDocument)
            return malformed("unexpected end of document while reading element text");
    }
}

core::Status XmlReader::skipElement() {
    if (selfClosing_) { pendingEnd_ = false; --depth_; return core::ok(); }
    const std::size_t target = depth_ - 1;
    for (;;) {
        auto ev = next();
        if (!ev) return core::Err<core::Error>(ev.error());
        if (*ev == XmlEvent::EndElement && depth_ == target) return core::ok();
        if (*ev == XmlEvent::EndOfDocument) return malformed("unexpected end of document");
    }
}

core::Result<std::string> readFile(const std::string& path, std::size_t maxBytes) {
    std::error_code ec;
    if (!std::filesystem::exists(path, ec))
        return core::fail(core::ErrorCode::FileNotFound, path);

    const auto size = std::filesystem::file_size(path, ec);
    if (ec) return core::fail(core::ErrorCode::FileUnreadable, ec.message(), path);
    if (size == 0) return core::fail(core::ErrorCode::FileEmpty, path);
    if (size > maxBytes)
        return core::fail(core::ErrorCode::FileTooLarge,
                          std::to_string(size / (1024 * 1024)) + " MB exceeds the import limit", path);

    std::ifstream in(path, std::ios::binary);
    if (!in) return core::fail(core::ErrorCode::FileUnreadable, "open failed", path);

    std::string data;
    data.resize(static_cast<std::size_t>(size));
    in.read(data.data(), static_cast<std::streamsize>(size));
    if (in.gcount() != static_cast<std::streamsize>(size))
        return core::fail(core::ErrorCode::FileUnreadable, "short read", path);
    return data;
}

} // namespace importer
