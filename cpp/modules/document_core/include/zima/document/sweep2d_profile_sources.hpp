#pragma once
#include <zima/document/part_document.hpp>
#include <set>
#include <cmath>
#include <stdexcept>

namespace zima::document {
// Inheritance belongs to the authored path order, independently of which end
// the user currently traverses first. Returned pointers refer to parameters.
inline std::vector<const Sweep3DProfile*> sweep2d_profile_sources(
        const Sweep2DParameters& parameters,const Curve3DRoute& route) {
    std::vector<const Sweep3DProfile*> populated;
    std::set<std::pair<std::string,bool>> identities;
    for(const auto& profile:parameters.profiles) {
        if(!identities.emplace(profile.point_id,profile.incoming).second)
            throw std::runtime_error("Sweep/Loft permits at most one profile per path Point");
        if(sweep3d_profile_has_geometry(sketcher::Sketch::from_serialized(profile.sketch_serialized)))
            populated.push_back(&profile);
    }
    std::vector<const Sweep3DProfile*> result(route.stations.size());
    const auto index=[&](std::size_t i){return parameters.path_reversed?result.size()-1-i:i;};
    const Sweep3DProfile* inherited=nullptr;
    for(std::size_t n=0;n<result.size();++n) {
        const auto i=index(n);const auto& station=route.stations[i];
        for(const auto* profile:populated)
            if(profile->point_id==station.point_id&&profile->incoming==station.incoming){inherited=profile;break;}
        result[i]=inherited;
    }
    // A profile authored at the opposite endpoint extends over the otherwise
    // undefined leading span without inserting another native Sketch.
    for(std::size_t n=result.size();n>0;--n) {
        auto& source=result[index(n-1)];if(source)inherited=source;else source=inherited;
    }
    if(!result.empty()) {
        const auto a=route.stations.front().origin,b=route.stations.back().origin;
        if(std::hypot(a.x-b.x,a.y-b.y,a.z-b.z)<1e-7)result[index(result.size()-1)]=result[index(0)];
    }
    return result;
}
}
