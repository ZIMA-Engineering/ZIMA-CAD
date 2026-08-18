#pragma once

#include <cstdint>
#include <bit>
#include <algorithm>
#include <string>
#include <vector>
#include <variant>
#include <type_traits>

namespace zima::kernel {

struct Vec3 {
    double x{};
    double y{};
    double z{};
};

struct FaceReference {
    std::string owner_id;
    std::string semantic_key;
    std::string instance_path;

    [[nodiscard]] bool valid() const {
        return !owner_id.empty() && !semantic_key.empty();
    }
    bool operator==(const FaceReference&) const = default;
};

struct EdgeReference {
    std::string owner_id;
    std::string semantic_key;
    std::string instance_path;
    [[nodiscard]] bool valid() const {
        return !owner_id.empty() && !semantic_key.empty();
    }
    bool operator==(const EdgeReference&) const = default;
};

struct VertexReference {
    std::string owner_id;
    std::string semantic_key;
    std::string instance_path;
    [[nodiscard]] bool valid() const {
        return !owner_id.empty() && !semantic_key.empty();
    }
    bool operator==(const VertexReference&) const = default;
};

struct AxisReference {
    std::string owner_id;
    std::string semantic_key;
    std::string instance_path;
    [[nodiscard]] bool valid() const {
        return !owner_id.empty() && !semantic_key.empty();
    }
    bool operator==(const AxisReference&) const = default;
};

struct ViewerEdge {
    std::vector<Vec3> points;
    EdgeReference reference;
};

struct ViewerPoint {
    Vec3 position;
    VertexReference reference;
};

struct ViewerAxis {
    Vec3 point;
    Vec3 direction{0.0, 0.0, 1.0};
    double display_length{100.0};
    AxisReference reference;
};

struct ViewerDimension {
    Vec3 witness_first;
    Vec3 witness_second;
    Vec3 line_first;
    Vec3 line_second;
    double value{};
    EdgeReference reference;
    std::string label_prefix;
};

struct ViewerMesh {
    std::vector<Vec3> vertices;
    std::vector<std::uint32_t> triangles;
    // One entry per triangle. Invalid entries are displayed but never offered
    // as persistent modeling references.
    std::vector<FaceReference> triangle_references;
    std::vector<ViewerEdge> edges;
    std::vector<ViewerPoint> points;
    std::vector<ViewerAxis> axes;
    std::vector<ViewerDimension> dimensions;
};

struct BoxRequest {
    BoxRequest() = default;
    BoxRequest(double box_length, double box_width, double box_height)
        : length(box_length), width(box_width), height(box_height) {}

    double length{100.0};
    double width{80.0};
    double height{50.0};
    Vec3 translation;
    Vec3 rotation_degrees;
};

struct CylinderRequest {
    double radius{40.0};
    double height{50.0};
    Vec3 translation;
    Vec3 rotation_degrees;
};

enum class BooleanOperation { Add, Subtract };

struct BoxOperation {
    std::string owner_id;
    BoxRequest box;
    BooleanOperation operation{BooleanOperation::Add};
};

using PrimitiveRequest = std::variant<BoxRequest, CylinderRequest>;

struct HistoryOperation {
    std::string owner_id;
    PrimitiveRequest primitive;
    BooleanOperation operation{BooleanOperation::Add};
};

struct BodyResult {
    ViewerMesh mesh;
    double volume{};
    double surface_area{};
    std::string source_fingerprint;
};

[[nodiscard]] inline std::string box_history_fingerprint(
    const std::vector<BoxOperation>& operations,
    std::size_t operation_count) {
    operation_count = std::min(operation_count, operations.size());
    std::uint64_t hash = 1469598103934665603ULL;
    const auto append_byte = [&](std::uint8_t value) {
        hash ^= value;
        hash *= 1099511628211ULL;
    };
    const auto append_u64 = [&](std::uint64_t value) {
        for (unsigned shift = 0; shift < 64; shift += 8) {
            append_byte(static_cast<std::uint8_t>((value >> shift) & 0xffU));
        }
    };
    append_u64(operation_count);
    for (std::size_t index = 0; index < operation_count; ++index) {
        const auto& operation = operations[index];
        append_u64(operation.owner_id.size());
        for (const unsigned char value : operation.owner_id) append_byte(value);
        append_byte(static_cast<std::uint8_t>(operation.operation));
        for (const double value : {
                operation.box.length, operation.box.width, operation.box.height,
                operation.box.translation.x, operation.box.translation.y,
                operation.box.translation.z, operation.box.rotation_degrees.x,
                operation.box.rotation_degrees.y,
                operation.box.rotation_degrees.z}) {
            append_u64(std::bit_cast<std::uint64_t>(value));
        }
    }
    constexpr char digits[] = "0123456789abcdef";
    std::string result(16, '0');
    for (int index = 15; index >= 0; --index) {
        result[static_cast<std::size_t>(index)] = digits[hash & 0xfU];
        hash >>= 4;
    }
    return result;
}

[[nodiscard]] inline std::string history_fingerprint(
    const std::vector<HistoryOperation>& operations,
    std::size_t operation_count) {
    // A dedicated byte stream keeps primitive kind part of the identity while
    // preserving the established Box fingerprint for existing tests.
    std::uint64_t hash = 1469598103934665603ULL;
    const auto byte = [&](std::uint8_t value) {
        hash ^= value;
        hash *= 1099511628211ULL;
    };
    const auto u64 = [&](std::uint64_t value) {
        for (unsigned shift = 0; shift < 64; shift += 8) {
            byte(static_cast<std::uint8_t>((value >> shift) & 0xffU));
        }
    };
    operation_count = std::min(operation_count, operations.size());
    u64(operation_count);
    for (std::size_t index = 0; index < operation_count; ++index) {
        const auto& operation = operations[index];
        u64(operation.owner_id.size());
        for (const unsigned char value : operation.owner_id) byte(value);
        byte(static_cast<std::uint8_t>(operation.operation));
        byte(static_cast<std::uint8_t>(operation.primitive.index()));
        std::visit([&](const auto& primitive) {
            using Request = std::decay_t<decltype(primitive)>;
            if constexpr (std::is_same_v<Request, BoxRequest>) {
                for (const double value : {
                        primitive.length, primitive.width, primitive.height,
                        primitive.translation.x, primitive.translation.y,
                        primitive.translation.z, primitive.rotation_degrees.x,
                        primitive.rotation_degrees.y, primitive.rotation_degrees.z}) {
                    u64(std::bit_cast<std::uint64_t>(value));
                }
            } else {
                for (const double value : {
                        primitive.radius, primitive.height,
                        primitive.translation.x, primitive.translation.y,
                        primitive.translation.z, primitive.rotation_degrees.x,
                        primitive.rotation_degrees.y, primitive.rotation_degrees.z}) {
                    u64(std::bit_cast<std::uint64_t>(value));
                }
            }
        }, operation.primitive);
    }
    constexpr char digits[] = "0123456789abcdef";
    std::string result(16, '0');
    for (int index = 15; index >= 0; --index) {
        result[static_cast<std::size_t>(index)] = digits[hash & 0xfU];
        hash >>= 4;
    }
    return result;
}

class GeometryKernel {
public:
    virtual ~GeometryKernel() = default;
    [[nodiscard]] virtual std::string name() const = 0;
    [[nodiscard]] virtual BodyResult make_box(const BoxRequest& request) const = 0;
    [[nodiscard]] virtual BodyResult evaluate_boxes(
        const std::vector<BoxOperation>& operations) const = 0;
    [[nodiscard]] virtual std::vector<BodyResult> evaluate_box_boundaries(
        const std::vector<BoxOperation>& operations) const = 0;
    [[nodiscard]] virtual std::vector<BodyResult> evaluate_history(
        const std::vector<HistoryOperation>& operations) const = 0;
};

}  // namespace zima::kernel
