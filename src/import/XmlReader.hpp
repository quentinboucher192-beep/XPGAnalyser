// =============================================================================
//  import/XmlReader.hpp — zero-copy pull parser
// -----------------------------------------------------------------------------
//  Why not a DOM library?
//
//  A Control Expert export of a mid-sized machine is 0.5–50 MB of XML. A DOM
//  materialises every element and every attribute as heap nodes; on a 50 MB
//  export that is several hundred megabytes and a visible stall. A pull parser
//  hands out string_views into a single buffer the caller already owns, so the
//  importer copies exactly the bytes it decides to keep.
//
//  The parser is intentionally small and total: it never throws, never allocates
//  per element, and reports position information so a malformed file produces a
//  diagnostic the user can act on ("line 4127: unterminated element <variables>")
//  rather than a silent empty tree.
// =============================================================================
#pragma once

#include "../core/Result.hpp"

#include <cstddef>
#include <string>
#include <string_view>
#include <vector>

namespace importer {

struct XmlAttribute {
    std::string_view name;
    std::string_view rawValue;      // still entity-encoded
};

enum class XmlEvent : std::uint8_t {
    None, StartElement, EndElement, Text, EndOfDocument
};

class XmlReader {
public:
    // The buffer must outlive the reader: nothing is copied.
    explicit XmlReader(std::string_view buffer) noexcept;

    core::Result<XmlEvent> next();

    [[nodiscard]] XmlEvent         event() const noexcept { return event_; }
    [[nodiscard]] std::string_view name() const noexcept { return name_; }
    [[nodiscard]] std::string_view text() const noexcept { return text_; }   // raw, encoded
    [[nodiscard]] const std::vector<XmlAttribute>& attributes() const noexcept { return attrs_; }
    [[nodiscard]] bool             selfClosing() const noexcept { return selfClosing_; }
    [[nodiscard]] std::size_t      line() const noexcept { return line_; }
    [[nodiscard]] std::size_t      depth() const noexcept { return depth_; }

    // Attribute lookup. Returns the decoded value, or the fallback when absent.
    [[nodiscard]] std::string attr(std::string_view key, std::string fallback = {}) const;
    [[nodiscard]] std::string_view rawAttr(std::string_view key) const noexcept;
    [[nodiscard]] bool has(std::string_view key) const noexcept;

    // Required attribute: turns a missing field into a located error instead of
    // a default-constructed value that silently corrupts the model.
    [[nodiscard]] core::Result<std::string> requireAttr(std::string_view key) const;

    // Consume everything up to the matching close tag of the current element,
    // returning the concatenated decoded character data. This is how a 20 kB
    // <STSource> body is lifted out in one call.
    [[nodiscard]] core::Result<std::string> readElementText();

    // Skip the current element and its whole subtree.
    core::Status skipElement();

    static std::string decodeEntities(std::string_view raw);

private:
    void skipSpace() noexcept;
    void countLines(std::size_t from, std::size_t to) noexcept;
    [[nodiscard]] core::Err<core::Error> malformed(std::string what) const;

    std::string_view          buf_;
    std::size_t               pos_{0};
    std::size_t               line_{1};
    std::size_t               depth_{0};
    XmlEvent                  event_{XmlEvent::None};
    std::string_view          name_;
    std::string_view          text_;
    std::vector<XmlAttribute> attrs_;
    bool                      selfClosing_{false};
    bool                      pendingEnd_{false};   // synthesised </x> for <x/>
};

// Loads a whole file into memory once. Enforces a size ceiling so a mis-selected
// 4 GB file fails with a message instead of an OOM kill.
core::Result<std::string> readFile(const std::string& path,
                                   std::size_t maxBytes = 256ull * 1024 * 1024);

} // namespace importer
