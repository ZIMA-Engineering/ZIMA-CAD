#pragma once
#include <cstddef>
#include <stdexcept>

namespace zima::kernel {
enum class OrderedUpToFailure {
    InvalidInput, AmbiguousAdjacency, AmbiguousRetainedPatch,
    EmptyPatch, DisconnectedChain, SelfIntersection
};
struct OrderedUpToDiagnostic {
    OrderedUpToFailure failure;
    std::size_t row{},other_row{};
};
// Domain data reaches the command owner without embedding row numbers in a
// localized message or parsing a kernel exception's prose.
class OrderedUpToError : public std::runtime_error {
public:
    OrderedUpToError(OrderedUpToFailure code,std::size_t first,std::size_t second)
        :std::runtime_error("Invalid ordered Feature Up To chain"),failure(code),row(first),other_row(second) {}
    OrderedUpToFailure failure;
    std::size_t row,other_row;
};
} // namespace zima::kernel
