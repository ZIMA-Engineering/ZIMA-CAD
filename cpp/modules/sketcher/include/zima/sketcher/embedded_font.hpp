#pragma once
#include <memory>
#include <string>
#include <utility>
namespace zima::sketcher {
// Immutable, embedded font bytes are shared by draft/Undo copies. The serialized
// value is self-contained; no installed font or source path is required to edit.
struct EmbeddedFont {
    std::shared_ptr<const std::string> base64;
    [[nodiscard]] bool empty() const {return !base64 || base64->empty();}
    [[nodiscard]] const std::string& encoded() const {
        static const std::string none;return base64?*base64:none;
    }
    explicit EmbeddedFont(std::string value):base64(value.empty()?nullptr:std::make_shared<const std::string>(std::move(value))) {}
    EmbeddedFont()=default;
    bool operator==(const EmbeddedFont& other) const {
        return base64==other.base64 || encoded()==other.encoded();
    }
};
}
