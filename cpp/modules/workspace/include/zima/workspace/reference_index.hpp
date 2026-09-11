#pragma once
#include <set>
#include <tuple>
#include <string>

namespace zima::workspace {
struct ReferenceIndex {
    std::set<std::tuple<std::string,std::string,std::string>> keys;
    template<class Ref> void add(const Ref& ref) {
        keys.emplace(ref.instance_path,ref.owner_id,ref.semantic_key);
    }
    template<class Geometry> void add_geometry(const Geometry& mesh) {
        for (const auto& ref : mesh.triangle_references) add(ref);
        for (const auto& edge : mesh.edges) add(edge.reference);
        for (const auto& point : mesh.points) add(point.reference);
        for (const auto& axis : mesh.axes) add(axis.reference);
    }
    bool contains(const std::string& owner,const std::string& semantic,const std::string& path={}) const {
        return keys.contains({path,owner,semantic});
    }
    template<class Ref> std::string missing(const Ref& ref) const {
        if (contains(ref.owner_id,ref.semantic_key,ref.instance_path)) return {};
        return "["+ref.instance_path+"|"+ref.owner_id+"|"+ref.semantic_key+"]";
    }
};
}
