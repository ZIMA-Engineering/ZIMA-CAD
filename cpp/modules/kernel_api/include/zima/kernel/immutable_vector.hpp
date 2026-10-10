#pragma once

#include <memory>
#include <initializer_list>
#include <utility>
#include <vector>

namespace zima::kernel {

// Calculated metadata is published once. Editing/capturing code constructs a
// new vector; documents and calculation/Undo snapshots share immutable data.
template<class T> class ImmutableVector {
public:
    using value_type = T;
    ImmutableVector() = default;
    ImmutableVector(std::vector<T> values)
        : values_(std::make_shared<const std::vector<T>>(std::move(values))) {}
    ImmutableVector(std::initializer_list<T> values) : ImmutableVector(std::vector<T>(values)) {}
    [[nodiscard]] const std::vector<T>& get() const {
        static const std::vector<T> empty;
        return values_ ? *values_ : empty;
    }
    operator const std::vector<T>&() const { return get(); }
    [[nodiscard]] auto begin() const { return get().begin(); }
    [[nodiscard]] auto end() const { return get().end(); }
    [[nodiscard]] bool empty() const { return get().empty(); }
    [[nodiscard]] std::size_t size() const { return get().size(); }
    [[nodiscard]] const T& front() const { return get().front(); }
    [[nodiscard]] const T& back() const { return get().back(); }
    [[nodiscard]] const T& at(std::size_t index) const { return get().at(index); }
    [[nodiscard]] const T& operator[](std::size_t index) const { return get()[index]; }
    void clear() { values_.reset(); }
    [[nodiscard]] bool shares_with(const ImmutableVector& other) const { return values_ == other.values_; }
    bool operator==(const ImmutableVector& other) const { return shares_with(other) || get() == other.get(); }
    bool operator==(const std::vector<T>& other) const { return get() == other; }
private:
    std::shared_ptr<const std::vector<T>> values_;
};

} // namespace zima::kernel
