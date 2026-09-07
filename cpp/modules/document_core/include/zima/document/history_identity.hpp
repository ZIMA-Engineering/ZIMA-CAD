#pragma once

#include <string>
#include <vector>

namespace zima::document {

enum class PartHistoryKind { Feature, Sketch, Construction };
struct PartHistoryEntry {
    PartHistoryKind kind{PartHistoryKind::Feature};
    std::string id;
    bool operator==(const PartHistoryEntry&) const = default;
};

enum class OriginChildKind { Point, Axis, Plane };

struct OriginChild {
    std::string id;
    std::string parent_id;
    std::string name;
    OriginChildKind kind{OriginChildKind::Point};
    std::string key;
    bool locked{true};
    bool operator==(const OriginChild&) const = default;
};

struct ContainerOrigin {
    std::string id;
    std::string parent_id;
    std::string name{"Container Origin"};
    std::vector<OriginChild> children;
    bool locked{true};
    bool operator==(const ContainerOrigin&) const = default;
};

[[nodiscard]] ContainerOrigin create_container_origin(
    const std::string& parent_id);


} // namespace zima::document
