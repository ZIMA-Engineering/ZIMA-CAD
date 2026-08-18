#pragma once

#include <zima/kernel/geometry_kernel.hpp>

#include <filesystem>
#include <optional>
#include <string>
#include <vector>

namespace zima::document {

enum class CombineMode { Add, Subtract };

struct BoxParameters {
    double length{100.0};
    double width{80.0};
    double height{50.0};
};

struct Placement {
    double x{};
    double y{};
    double z{};
    double rotation_x{};
    double rotation_y{};
    double rotation_z{};
};

struct HistoryContainer {
    std::string id;
    std::string name{"Kvádr"};
    CombineMode combine_mode{CombineMode::Add};
    Placement placement;
    BoxParameters box;
};

class PartDocument {
public:
    std::string document_id;
    std::string name{"Nový díl"};
    std::vector<HistoryContainer> history;

    [[nodiscard]] static PartDocument create_default();
    [[nodiscard]] static HistoryContainer create_box_container();
    [[nodiscard]] HistoryContainer* find_container(const std::string& id);
    [[nodiscard]] const HistoryContainer* find_container(const std::string& id) const;
    [[nodiscard]] std::optional<std::size_t> history_index(
        const std::string& id) const;
    [[nodiscard]] std::vector<zima::kernel::BoxOperation> box_operations() const;
    [[nodiscard]] static PartDocument load(
        const std::filesystem::path& path,
        std::vector<zima::kernel::BodyResult>* calculated_boundaries = nullptr);
    void save(
        const std::filesystem::path& path,
        const std::vector<zima::kernel::BodyResult>& calculated_boundaries = {}) const;
};

}  // namespace zima::document
