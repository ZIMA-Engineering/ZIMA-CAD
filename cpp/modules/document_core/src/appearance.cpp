#include <cmath>
#include <nlohmann/json.hpp>
#include <set>
#include <stdexcept>
#include <zima/document/appearance.hpp>
namespace zima::document {
using nlohmann::json;
namespace {
void validate(const kernel::SurfaceStyle &s) {
  if ((s.color.size() != 7 && s.color.size() != 9) || s.color.front() != '#' ||
      s.color.find_first_not_of("0123456789abcdefABCDEF", 1) !=
          std::string::npos ||
      !std::isfinite(s.roughness) || s.roughness < 0.04 || s.roughness > 1 ||
      !std::isfinite(s.metallic) || s.metallic < 0 || s.metallic > 1)
    throw std::invalid_argument("Invalid surface appearance");
}
json encode(const kernel::SurfaceStyle &s) {
  validate(s);
  return {
      {"color", s.color}, {"roughness", s.roughness}, {"metallic", s.metallic}};
}
kernel::SurfaceStyle decode(const json &j) {
  kernel::SurfaceStyle s{j.at("color"), j.at("roughness"), j.at("metallic")};
  validate(s);
  return s;
}
} // namespace
void validate_appearance(const kernel::Appearance &a) {
  validate(a.body);
  for (const auto &[id, s] : a.bodies) {
    if (id.empty())
      throw std::invalid_argument("Empty body ID");
    validate(s);
  }
  std::set<std::string> ids, faces;
  for (const auto &g : a.groups) {
    validate(g.style);
    if (g.id.empty() || g.name.empty() || !ids.insert(g.id).second)
      throw std::invalid_argument("Invalid appearance group");
    for (const auto &f : g.faces)
      if (f.find("::") == std::string::npos || !faces.insert(f).second)
        throw std::invalid_argument(
            "A face belongs to exactly one appearance group");
  }
}
std::string serialize_appearance(const kernel::Appearance &a) {
  validate_appearance(a);
  json j{{"body", encode(a.body)},
         {"bodies", json::object()},
         {"owners", a.owner_bodies},
         {"groups", json::array()}};
  for (const auto &[id, s] : a.bodies)
    j["bodies"][id] = encode(s);
  for (const auto &g : a.groups)
    j["groups"].push_back({{"id", g.id},
                           {"name", g.name},
                           {"body_id", g.body_id},
                           {"style", encode(g.style)},
                           {"faces", g.faces}});
  return j.dump();
}
kernel::Appearance deserialize_appearance(const std::string &value) {
  const auto j = json::parse(value);
  kernel::Appearance a;
  if (j.empty())
    return a;
  a.body = decode(j.at("body"));
  for (const auto &[id, s] : j.at("bodies").items())
    a.bodies[id] = decode(s);
  a.owner_bodies = j.at("owners").get<std::map<std::string, std::string>>();
  for (const auto &g : j.at("groups"))
    a.groups.push_back({g.at("id"), g.at("name"), g.at("body_id"),
                        decode(g.at("style")), g.at("faces")});
  validate_appearance(a);
  return a;
}
std::string serialize_palette(const std::vector<kernel::NamedStyle> &palette) {
  json j = json::array();
  for (const auto &p : palette)
    j.push_back({{"id", p.id},
                 {"name", p.name},
                 {"category", p.category},
                 {"style", encode(p.style)}});
  return j.dump();
}
std::vector<kernel::NamedStyle> deserialize_palette(const std::string &value) {
  std::vector<kernel::NamedStyle> result;
  std::set<std::string> ids;
  for (const auto &p : json::parse(value)) {
    kernel::NamedStyle s{p.at("id"), p.at("name"), p.at("category"),
                         decode(p.at("style"))};
    if (s.id.empty() || s.name.empty() || s.category.empty() ||
        !ids.insert(s.id).second)
      throw std::invalid_argument("Invalid palette item");
    result.push_back(std::move(s));
  }
  return result;
}
std::vector<kernel::NamedStyle> default_surface_palette() {
  std::vector<kernel::NamedStyle> p;
  const auto add = [&](std::string id, std::string name, std::string category,
                       const char *color, double roughness, double metal) {
    p.push_back({id, name, category, {color, roughness, metal}});
  };
  add("white", "Bílá", "Základní barvy", "#ECEFF1", .55, 0);
  add("graphite", "Grafitová", "Základní barvy", "#30343B", .55, 0);
  add("silver", "Stříbrná", "Základní barvy", "#B9C2CC", .55, 0);
  add("blue", "Modrá", "Základní barvy", "#3F6F9F", .55, 0);
  add("green", "Zelená", "Základní barvy", "#3F7652", .55, 0);
  add("violet", "Fialová", "Základní barvy", "#6B5A8E", .55, 0);
  add("burgundy", "Vínová", "Základní barvy", "#7A4654", .55, 0);
  add("sand", "Písková", "Základní barvy", "#B59A68", .55, 0);
  add("red", "Červená", "Základní barvy", "#E53935", .55, 0);
  add("orange", "Oranžová", "Základní barvy", "#FB8C00", .55, 0);
  add("yellow", "Žlutá", "Základní barvy", "#FDD835", .55, 0);
  add("bright-green", "Jasná zelená", "Základní barvy", "#43A047", .55, 0);
  add("bright-blue", "Jasná modrá", "Základní barvy", "#1E88E5", .55, 0);
  add("purple", "Purpurová", "Základní barvy", "#8E24AA", .55, 0);
  add("plastic-matte", "Matný plast", "Plasty", "#D7DBE0", .8, 0);
  add("plastic-gloss", "Lesklý plast", "Plasty", "#34495E", .17, 0);
  add("paint-blue", "Modrý lak", "Laky", "#225FC2", .18, 0);
  add("paint-red", "Červený lak", "Laky", "#B62520", .2, 0);
  add("steel", "Ocel", "Kovy", "#9BA1A6", .35, 1);
  add("stainless-matte", "Nerez matná", "Kovy", "#C1C5C8", .48, 1);
  add("stainless-polished", "Nerez leštěná", "Kovy", "#C1C5C8", .1, 1);
  add("aluminium", "Hliník", "Kovy", "#D5D8DC", .32, 1);
  add("bronze", "Bronz matný", "Kovy", "#AD7945", .42, 1);
  add("bronze-polished", "Bronz leštěný", "Kovy", "#AD7945", .12, 1);
  add("brass", "Mosaz", "Kovy", "#C6A55F", .23, 1);
  add("copper", "Měď", "Kovy", "#C57D5C", .24, 1);
  return p;
}
} // namespace zima::document
