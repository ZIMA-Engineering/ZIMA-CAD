#pragma once
#include <zima/kernel/geometry_kernel.hpp>
#include <stdexcept>
#include <string>
#include <vector>

namespace zima::cam {

using Point = kernel::Vec3;
enum class Units { Millimetres, Inches };
enum class MachineKind { Milling, Turning };
enum class MotionKind { Rapid, Linear, ArcClockwise, ArcCounterclockwise };
enum class BlockKind { Tool, Spindle, Feed, Motion, Stop };

struct Machine {
    std::string id, name;
    MachineKind kind{MachineKind::Milling};
    unsigned axes{3};
    // Travel is expressed in machine coordinates, independently of the work frame.
    Point minimum{-500,-500,-500}, maximum{500,500,500};
    double maximum_feed_mm_min{10000}, maximum_spindle_rpm{24000};
    double rapid_mm_min{15000};
    std::string postprocessor_id;
    bool operator==(const Machine&) const = default;
};

// A radial station describes a solid of revolution around local Z. Equal Z
// stations represent radial shoulders; successive stations otherwise define
// exact conical/cylindrical portions. The authored origin remains the boundary
// between cutting (Z <= 0) and non-cutting (Z >= 0) portions.
struct ToolStation {
    double z_mm{}, radius_mm{};
    bool operator==(const ToolStation&) const = default;
};
struct Tool {
    std::string id, name;
    std::vector<ToolStation> cutting, non_cutting;
    double feed_mm_min{300}, spindle_rpm{6000};
    bool clockwise{true};
    bool operator==(const Tool&) const = default;
};

struct Block {
    std::string id, operation_id;
    BlockKind kind{BlockKind::Motion};
    std::string tool_id;
    double value{}; // Feed in mm/min or spindle speed in rpm.
    bool clockwise{true};
    MotionKind motion{MotionKind::Rapid};
    Point end, center; // Absolute work coordinates, always stored in millimetres.
    bool operator==(const Block&) const = default;
};
struct Program {
    Units display_units{Units::Millimetres};
    Point initial_tip;
    std::vector<Block> blocks;
    bool operator==(const Program&) const = default;
};

struct Motion {
    std::string block_id, operation_id, tool_id;
    MotionKind kind{MotionKind::Rapid};
    Point start, end, center;
    double sweep_radians{};
    double feed_mm_min{}, spindle_rpm{};
    bool spindle_clockwise{true};
};
struct CompiledProgram {
    std::vector<Motion> motions;
    // The generic text is an inspection/interchange representation. Executable
    // controller output belongs to an independently selected postprocessor.
    std::vector<std::string> lines, line_block_ids;
};

enum class Error {
    InvalidMachine, UnsupportedMachine, InvalidTool, DuplicateIdentity,
    InvalidProgram, UnknownTool, MissingTool, InvalidFeed, InvalidSpindle,
    InvalidArc, MotionAfterStop, TravelExceeded, InvalidOperation
};
class ProgramError final : public std::runtime_error {
public:
    Error code;
    std::string block_id;
    ProgramError(Error, std::string block = {});
};

void validate(const Machine&);
void validate(const Tool&);
[[nodiscard]] double tip_offset_mm(const Tool&);
[[nodiscard]] Point tool_origin(const Tool&, Point tip);
// The axial mounting point is the end of the authored non-cutting assembly.
// Machine travel applies there; NC coordinates continue to describe the tip.
[[nodiscard]] Point tool_mount(const Tool&, Point tip);
[[nodiscard]] CompiledProgram compile(const Program&, const Machine&, const std::vector<Tool>&,
    Point machine_work_origin = {});
[[nodiscard]] Point point_at(const Motion&, double fraction);
[[nodiscard]] double length_mm(const Motion&);
// Includes complete arc extrema, not only the two endpoints.
[[nodiscard]] std::pair<Point,Point> motion_bounds(const Motion&);

} // namespace zima::cam
