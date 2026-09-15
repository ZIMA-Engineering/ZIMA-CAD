#pragma once
namespace zima::sketcher { struct Sketch; }
namespace zima::document {
enum class ProfileStatus { Empty, Open, Closed, Invalid };
// Inspect native Sketch profile data with the same builders as modeling.
// This never invokes the solid kernel or changes the input Sketch.
[[nodiscard]] ProfileStatus profile_status(const sketcher::Sketch&);
}
