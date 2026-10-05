#include "Result.hpp"

namespace core {
namespace {
constexpr const char* text(ErrorCode c) {
    switch (c) {
        case ErrorCode::None:                return "no error";
        case ErrorCode::FileNotFound:        return "file not found";
        case ErrorCode::FileUnreadable:      return "file could not be read";
        case ErrorCode::FileTooLarge:        return "file exceeds the import size limit";
        case ErrorCode::FileEmpty:           return "file is empty";
        case ErrorCode::XmlMalformed:        return "malformed XML";
        case ErrorCode::XmlUnexpectedRoot:   return "not a Control Expert exchange file";
        case ErrorCode::XmlMissingAttribute: return "required attribute missing";
        case ErrorCode::XmlUnsupportedDtd:   return "unsupported DTD version";
        case ErrorCode::UnknownCpuReference: return "CPU reference is not in the hardware catalog";
        case ErrorCode::DuplicateSymbol:     return "symbol declared twice";
        case ErrorCode::UnresolvedType:      return "type could not be resolved";
        case ErrorCode::IncompleteProject:   return "project data is partial";
        case ErrorCode::SdlInit:             return "SDL could not be initialised";
        case ErrorCode::SdlRenderer:         return "renderer could not be created";
        case ErrorCode::SdlTexture:          return "texture could not be created";
        case ErrorCode::FontLoad:            return "font could not be loaded";
        case ErrorCode::InvalidArgument:     return "invalid argument";
        case ErrorCode::OutOfRange:          return "out of range";
        case ErrorCode::NotImplemented:      return "not implemented";
        case ErrorCode::Cancelled:           return "cancelled";
    }
    return "unknown error";
}
} // namespace

std::string Error::message() const {
    std::string m = text(code);
    if (!context.empty()) m += ": " + context;
    if (!source.empty())  m += " [" + source + "]";
    return m;
}
} // namespace core
