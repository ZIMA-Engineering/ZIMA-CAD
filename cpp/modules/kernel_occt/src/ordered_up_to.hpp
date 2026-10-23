#pragma once

#include <TopoDS_Face.hxx>
#include <TopoDS_Shell.hxx>
#include <Bnd_Box.hxx>
#include <gp_Trsf.hxx>
#include <zima/kernel/ordered_up_to_diagnostic.hpp>
#include <optional>
#include <stdexcept>
#include <vector>

namespace zima::kernel::detail {

// Operation-local OCCT data. References and retained choices belong to the
// calling Feature; this builder never changes source topology or document data.
struct OrderedLimitInput {
    TopoDS_Face authored_face;
    // Positive means the oriented normal side of the adjacent support.
    // Absence is allowed only while seeding a new, unambiguous authored choice.
    std::optional<bool> keep_previous_positive;
    std::optional<bool> keep_next_positive;
};
struct OrderedLimitPatch {
    TopoDS_Face face;
    std::optional<bool> keep_previous_positive;
    std::optional<bool> keep_next_positive;
};
struct OrderedLimitResult {
    TopoDS_Shell shell;
    // Same semantic order as the inputs, never OCCT face enumeration order.
    std::vector<OrderedLimitPatch> patches;
};
using OrderedLimitFailure=OrderedUpToFailure;
class OrderedLimitError final : public OrderedUpToError {
public:
    OrderedLimitError(OrderedLimitFailure failure, std::size_t row,
        std::size_t other_row);
};

// Build only on explicit calculation. Envelope includes the complete operation
// region and source domains, with an overrun margin supplied by the caller.
OrderedLimitResult build_ordered_up_to(const std::vector<OrderedLimitInput>& inputs,
    const Bnd_Box& envelope, double tolerance);

// Keep every semantic row attached to the actual transformed shell member.
// Independent per-face transforms would break this topology/history relation.
OrderedLimitResult transform_ordered_up_to(const OrderedLimitResult& source,
    const gp_Trsf& transform);

} // namespace zima::kernel::detail
