// =============================================================================
//  export/XpgWriter.hpp — writing a Control Expert program export
// -----------------------------------------------------------------------------
//  This is the half of the job that carries the risk. Reading a file that is
//  wrong costs a diagnostic; writing one that is wrong costs an engineer an
//  afternoon in front of a machine that will not import.
//
//  Three decisions, all deliberate:
//
//  * ELEMENT ORDER IS FIXED. DTD 41 is a sequence, not a set: fileHeader,
//    contentHeader, logicConf, then DDTSource*, FBSource*, programUnit*,
//    program*, dataBlock. Re-ordering produces a file that parses and that
//    Control Expert refuses. The writer emits in that order regardless of the
//    order things were created in the editor.
//
//  * CHECKSUMS ARE WRITTEN AS ZERO, on instruction. Control Expert recomputes
//    TypeSignatureCheckSumString on import; carrying a stale value across an
//    edit would be worse than carrying none. Every DDT therefore gets
//    value="0", and the writer says so in a comment in the file itself.
//
//  * ROUND-TRIP IS TESTED, not assumed. tests/roundtrip_test.cpp imports the
//    real export, writes it back, re-imports the result and compares the two
//    models entity by entity. A writer without that test is a guess.
// =============================================================================
#pragma once

#include "../core/Result.hpp"
#include "../domain/ProjectModel.hpp"

#include <string>

namespace exporter {

struct XpgWriteOptions {
    std::string exportedBy{"XpgAnalyzer 1.0"};
    std::string dateTime;              // empty: generated from the clock
    bool        includeAnimationTables{true};
    bool        zeroChecksums{true};   // see the header comment
    std::string task{"MAST"};          // the task whose sections are exported
};

// Produces the document as a string; the caller decides where it goes.
[[nodiscard]] core::Result<std::string> writeXpg(const domain::Project&,
                                                 const XpgWriteOptions& = {});

// Convenience: writes to disk, creating parent directories.
[[nodiscard]] core::Status writeXpgFile(const domain::Project&,
                                        const std::string& path,
                                        const XpgWriteOptions& = {});

// The hardware export, for the same reason: a project edited here must be able
// to go back into Control Expert complete.
[[nodiscard]] core::Result<std::string> writeXhw(const domain::Project&,
                                                 const XpgWriteOptions& = {});
[[nodiscard]] core::Status writeXhwFile(const domain::Project&,
                                        const std::string& path,
                                        const XpgWriteOptions& = {});

// XML text escaping, exposed because the project format uses it too.
[[nodiscard]] std::string escapeXml(std::string_view);

} // namespace exporter
