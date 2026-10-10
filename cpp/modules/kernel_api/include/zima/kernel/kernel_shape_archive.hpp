#pragma once

#include <memory>
#include <string>
#include <utility>

namespace zima::kernel {

// Opaque calculated B-Rep bytes. Geometry calculation replaces an archive;
// document, history and Undo snapshots share its immutable contents in memory.
// Native serialization still writes the complete ordinary string value.
class KernelShapeArchive {
public:
    KernelShapeArchive() = default;
    KernelShapeArchive(std::string value)
        : value_(std::make_shared<const std::string>(std::move(value))) {}
    KernelShapeArchive(const char* value) : KernelShapeArchive(std::string(value)) {}
    KernelShapeArchive(std::shared_ptr<const std::string> value) : value_(std::move(value)) {}

    [[nodiscard]] const std::string& get() const {
        static const std::string empty;
        return value_ ? *value_ : empty;
    }
    operator const std::string&() const { return get(); }
    [[nodiscard]] bool empty() const { return get().empty(); }
    [[nodiscard]] std::size_t size() const { return get().size(); }
    void clear() { value_.reset(); }
    [[nodiscard]] std::shared_ptr<const std::string> shared_data() const { return value_; }
    [[nodiscard]] bool shares_with(const KernelShapeArchive& other) const { return value_ == other.value_; }
    bool operator==(const KernelShapeArchive& other) const {
        return shares_with(other) || get() == other.get();
    }
    bool operator==(const std::string& other) const { return get() == other; }
    bool operator==(const char* other) const { return get() == other; }

private:
    std::shared_ptr<const std::string> value_;
};

} // namespace zima::kernel
