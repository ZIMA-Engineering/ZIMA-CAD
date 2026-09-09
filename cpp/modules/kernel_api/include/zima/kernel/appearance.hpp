#pragma once
#include <map>
#include <string>
#include <vector>
namespace zima::kernel {
struct SurfaceStyle {
  std::string color{"#B9C2CC"};
  double roughness{0.55};
  double metallic{};
  bool operator==(const SurfaceStyle &) const = default;
};
struct SurfaceGroup {
  std::string id;
  std::string name;
  std::string body_id;
  SurfaceStyle style;
  std::vector<std::string>
      faces; // persisted result-face owner ID :: semantic key (appearance only)
  bool operator==(const SurfaceGroup &) const = default;
};
struct Appearance {
  SurfaceStyle body;
  std::map<std::string, SurfaceStyle> bodies;
  std::map<std::string, std::string> owner_bodies;
  std::vector<SurfaceGroup> groups;
  bool operator==(const Appearance &) const = default;
};
struct NamedStyle {
  std::string id, name, category;
  SurfaceStyle style;
  bool operator==(const NamedStyle &) const = default;
};
} // namespace zima::kernel
