#include "transition_sheet.hpp"
#include <algorithm>
#include <numbers>
#include <optional>
namespace zima::research::transition {
namespace {
using namespace kernel::sheet_material;
constexpr double loft_tolerance=.00001;
double length(Vec3 v){return std::sqrt(dot(v,v));}
using Poly=std::vector<Vec3>;
Poly clip(const Poly& poly,Vec3 origin,Vec3 inward,double offset) {
    Poly output;
    for(std::size_t i=0;i<poly.size();++i) {
        const auto a=poly[i],b=poly[(i+1)%poly.size()];const double da=dot(sub(a,origin),inward)-offset,db=dot(sub(b,origin),inward)-offset;
        if(da>=-1e-9)output.push_back(a);
        if((da>1e-9&&db<-1e-9)||(da<-1e-9&&db>1e-9))output.push_back(add(a,mul(sub(b,a),da/(da-db))));
    }
    if(output.size()<3)throw std::invalid_argument("Bend radius consumes a transition panel");return output;
}
Vec3 centroid(const Poly& poly){Vec3 p{};for(auto q:poly)p=add(p,q);return mul(p,1./poly.size());}
}
template<class Model>
SheetResult manufacture_model(const Model& model,const SheetOptions& options) {
    using namespace kernel::sheet_material;
    if(!std::isfinite(options.thickness)||options.thickness<=0||!std::isfinite(options.inside_radius)||options.inside_radius<=0||
        !std::isfinite(options.k_factor)||options.k_factor<0||options.k_factor>1||
        !std::isfinite(options.end_notch_depth)||options.end_notch_depth<0||
        !std::isfinite(options.rectangle_relief_depth)||options.rectangle_relief_depth<0)throw std::invalid_argument("Invalid transition sheet parameters");
    const auto surface=calculate(model);if(!surface.valid())throw std::invalid_argument("Invalid transition surface");
    SheetResult result;result.thickness=options.thickness;
    const double outer_radius=options.inside_radius+options.thickness,neutral_radius=options.inside_radius+options.k_factor*options.thickness;
    std::vector<Vec3> shifts(surface.faces.size());
    struct Relief {Vec3 origin,inward;double offset;};
    std::vector<std::optional<Relief>> reliefs(surface.faces.size());
    for(const auto& face:surface.faces)result.panels.push_back({face.folded,{},face.normal});
    const Vec3 cap0=model.first_origin.origin,normal0=model.first_origin.z;
    const Vec3 cap1=model.first_origin.point(model.second_relative.origin),normal1=model.first_origin.direction(model.second_relative.z);
    // Trim every panel before constructing a bend. A triangulated wall can
    // have two active bends meeting at a rim vertex; the second trim changes
    // the first bend's available tangent interval.
    for(std::size_t i=0;i<surface.folds.size();++i) {
        const auto& fold=surface.folds[i];const double angle=fold.signed_angle_radians;
        if(std::abs(angle)<1e-8)continue;
        const auto along=unit(sub(fold.second,fold.first));
        const double radius=angle>0?outer_radius:options.inside_radius;
        const double setback=radius*std::tan(std::abs(angle)/2);
        for(auto panel:{i,i+1}) {
            auto inward=unit(cross(surface.faces[panel].normal,along));
            if(dot(sub(centroid(surface.faces[panel].folded),fold.first),inward)<0)inward=mul(inward,-1);
            result.panels[panel].outer=clip(result.panels[panel].outer,fold.first,inward,setback);
        }
    }
    // Blunt the narrow planar tip where two finite-radius bends converge.
    // A straight cut through the thickness leaves an intentional weld relief;
    // the neighboring bend ends use this same clipped material boundary.
    for(std::size_t i=1;i+1<surface.faces.size();++i) {
        const auto& entry=surface.folds[i-1];const auto& exit=surface.folds[i];
        if(std::abs(entry.signed_angle_radians)<1e-8||std::abs(exit.signed_angle_radians)<1e-8)continue;
        const bool first=length(sub(entry.first,exit.first))<1e-7;
        const bool last=length(sub(entry.second,exit.second))<1e-7;
        if(!first&&!last)continue;
        const auto origin=first?entry.first:entry.second;
        const auto a=unit(sub(first?entry.second:entry.first,origin));
        const auto b=unit(sub(first?exit.second:exit.first,origin));
        const auto inward=unit(add(a,b));
        auto& polygon=result.panels[i].outer;
        double tip=1e100;
        for(auto p:polygon)tip=std::min(tip,dot(sub(p,origin),inward));
        const double offset=tip+options.thickness;
        polygon=clip(polygon,origin,inward,offset);
        reliefs[i]=Relief{origin,inward,offset};
    }
    for(std::size_t i=0;i<surface.folds.size();++i) {
        const auto& fold=surface.folds[i];const auto& before=surface.faces[i];const auto& after=surface.faces[i+1];
        shifts[i+1]=shifts[i];const double angle=fold.signed_angle_radians;
        if(std::abs(angle)<1e-8)continue;
        const double sign=angle>0?1.:-1.,sweep=std::abs(angle);
        if(sweep>=std::numbers::pi-1e-6)throw std::invalid_argument("Unsupported reverse transition bend");
        const double reference_radius=sign>0?outer_radius:options.inside_radius;
        const auto along=unit(sub(fold.second,fold.first));
        const double setback=reference_radius*std::tan(sweep/2);
        const auto flat_a=after.unfolded[0],flat_b=after.unfolded[1],flat_along=unit(sub(flat_b,flat_a));
        auto across=Vec3{-flat_along.y,flat_along.x,0};if(dot(sub(centroid(after.unfolded),flat_a),across)<0)across=mul(across,-1);
        const double allowance=neutral_radius*sweep;
        shifts[i+1]=add(shifts[i],mul(across,allowance-2*setback));
        SheetBend bend;bend.boundary_index=i;bend.along=along;bend.angle=sweep;bend.outer_radius=outer_radius;bend.neutral_radius=neutral_radius;bend.thickness=options.thickness;
        bend.center=add(fold.first,mul(add(before.normal,after.normal),sign*reference_radius/(1+dot(before.normal,after.normal))));
        bend.start_radial=mul(before.normal,-sign);bend.turn_tangent=mul(cross(along,bend.start_radial),sign);
        const auto tangent_interval=[&](std::size_t panel,Vec3 radial) {
            const auto base=add(bend.center,mul(radial,reference_radius));
            const auto& polygon=result.panels[panel].outer;
            double low=-1e100,high=1e100;
            for(std::size_t edge=0;edge<polygon.size();++edge) {
                const auto a=polygon[edge],direction=unit(sub(polygon[(edge+1)%polygon.size()],a));
                const double constant=dot(cross(direction,sub(base,a)),surface.faces[panel].normal);
                const double slope=dot(cross(direction,along),surface.faces[panel].normal);
                if(slope>1e-9)low=std::max(low,-constant/slope);
                else if(slope<-1e-9)high=std::min(high,-constant/slope);
            }
            return std::array<double,2>{low,high};
        };
        const auto entry=tangent_interval(i,bend.start_radial);
        const auto exit=tangent_interval(i+1,mul(after.normal,-sign));
        const auto shared_vertex=[&](std::size_t other) {
            const auto& adjacent=surface.folds[other];
            if(std::abs(adjacent.signed_angle_radians)<1e-8)return false;
            return length(sub(fold.first,adjacent.first))<1e-7||length(sub(fold.second,adjacent.second))<1e-7;
        };
        const bool junction=(i&&shared_vertex(i-1))||(i+1<surface.folds.size()&&shared_vertex(i+1));
        // Resolve end extents against authored profile planes at each angular station.
        if(std::abs(dot(normal0,along))<1e-8||std::abs(dot(normal1,along))<1e-8)throw std::invalid_argument("Bend axis parallel to transition rim");
        const auto section=[&](double parameter,bool developed) {
            const auto radial=add(mul(bend.start_radial,std::cos(parameter)),mul(bend.turn_tangent,std::sin(parameter)));
            const auto base=add(bend.center,mul(radial,reference_radius));
            double first=dot(sub(cap0,base),normal0)/dot(along,normal0),last=dot(sub(cap1,base),normal1)/dot(along,normal1);
            if(junction) {
                const double fraction=parameter/sweep;
                first=entry[0]*(1-fraction)+exit[0]*fraction;
                last=entry[1]*(1-fraction)+exit[1]*fraction;
            }
            if(parameter>=sweep-1e-12&&!developed)bend.untrimmed_exit=add(base,mul(along,first));
            if(options.end_notch_depth>0) {
                // Straight notch bottoms across the developed bend allowance.
                first=std::max(first,std::max(entry[0],exit[0])+options.end_notch_depth);
                last=std::min(last,std::min(entry[1],exit[1])-options.end_notch_depth);
            }
            if constexpr(std::is_same_v<Model,HalfModel>) {
                if(options.rectangle_relief_bends.contains(result.bends.size()))
                    last=std::min(last,std::min(entry[1],exit[1])-options.rectangle_relief_depth-options.end_notch_depth);
            }
            if(last<=first)throw std::invalid_argument("Reversed transition bend extent");
            if(developed) {
                const auto origin=add(add(flat_a,shifts[i]),mul(across,neutral_radius*parameter-setback));
                const auto a=add(origin,mul(flat_along,first)),b=add(origin,mul(flat_along,last));
                // +Z is inward in this material chart; inner-skin axes use z=t.
                return std::array<Vec3,4>{a,b,add(b,{0,0,options.thickness}),add(a,{0,0,options.thickness})};
            }
            const auto a=add(base,mul(along,first)),b=add(base,mul(along,last));
            return std::array<Vec3,4>{a,b,sub(b,mul(radial,sign*options.thickness)),sub(a,mul(radial,sign*options.thickness))};
        };
        const std::size_t steps=std::max<std::size_t>(4,static_cast<std::size_t>(std::ceil(sweep/(std::numbers::pi/90))));
        for(std::size_t j=0;j<=steps;++j){const double u=sweep*j/steps;bend.sections.push_back(section(u,false));bend.developed_sections.push_back(section(u,true));}
        const auto mid=section(sweep/2,true);result.axes.push_back({mid[3],mid[2],PatternRole::BendAxis,angle});
        auto& material=bend.material;material.kind=kernel::SheetMaterialDefinition::Kind::Cylinder;
        material.origin=add(bend.center,mul(bend.start_radial,options.inside_radius));
        material.along=mul(along,sign);material.tangent=bend.turn_tangent;material.radial=bend.start_radial;
        material.radius=options.inside_radius;material.neutral_radius=neutral_radius;material.angle=sweep;material.thickness=options.thickness;material.thickness_sign=1;
        result.bends.push_back(std::move(bend));
    }
    for(std::size_t i=0;i<surface.faces.size();++i) {
        const auto& source=surface.faces[i];auto& panel=result.panels[i];
        const auto x=unit(sub(source.folded[1],source.folded[0])),y=cross(source.normal,x);
        const auto flat_x=unit(sub(source.unfolded[1],source.unfolded[0]));auto flat_y=Vec3{-flat_x.y,flat_x.x,0};
        if(dot(sub(centroid(source.unfolded),source.unfolded[0]),flat_y)<0)flat_y=mul(flat_y,-1);
        for(auto p:panel.outer){const auto delta=sub(p,source.folded[0]);panel.developed.push_back(add(add(source.unfolded[0],shifts[i]),add(mul(flat_x,dot(delta,x)),mul(flat_y,dot(delta,y)))));}
        // Name authored material boundaries by their geometric role, never by
        // the enumeration of a clipped polygon or resulting kernel edges.
        for(std::size_t j=0;j<panel.outer.size();++j) {
            const auto a=panel.outer[j],b=panel.outer[(j+1)%panel.outer.size()];
            const auto on_rim=[&](Vec3 origin,Vec3 normal){return std::abs(dot(sub(a,origin),normal))<1e-6&&std::abs(dot(sub(b,origin),normal))<1e-6;};
            if(reliefs[i]&&std::abs(dot(sub(a,reliefs[i]->origin),reliefs[i]->inward)-reliefs[i]->offset)<1e-6&&
                std::abs(dot(sub(b,reliefs[i]->origin),reliefs[i]->inward)-reliefs[i]->offset)<1e-6)
                panel.edge_roles.push_back("apex-relief");
            else if(on_rim(cap0,normal0))panel.edge_roles.push_back("round-rim");
            else if(on_rim(cap1,normal1))panel.edge_roles.push_back("rectangle-rim");
            else if(i==surface.folds.size()) {
                // The closing edge of the final triangle may be a rim, not
                // its free end. Identify the incoming tangent by direction.
                const auto entry=unit(sub(source.folded[1],source.folded[0]));
                panel.edge_roles.push_back(length(cross(unit(sub(b,a)),entry))<1e-7?"entry":"exit");
            }
            else {
                const auto middle=mul(add(a,b),.5);
                const auto distance_to=[&](Vec3 a,Vec3 b){return length(cross(sub(middle,a),unit(sub(b,a))));};
                const double entry=i?distance_to(surface.folds[i-1].first,surface.folds[i-1].second):distance_to(source.folded[0],source.folded[1]);
                const double exit=i<surface.folds.size()?distance_to(surface.folds[i].first,surface.folds[i].second):distance_to(source.folded.back(),source.folded.front());
                panel.edge_roles.push_back(entry<exit?"entry":"exit");
            }
        }
    }
    return result;
}
SheetResult manufacture(const HalfModel& model,const SheetOptions& options) {return manufacture_model(model,options);}
SheetResult manufacture(const RectangularModel& model,const SheetOptions& options) {return manufacture_model(model,options);}
kernel::FeatureGroupRequest sheet_request(const SheetResult& sheet,bool unfolded) {
    using namespace kernel::sheet_material;
    kernel::FeatureGroupRequest group;
    for(std::size_t i=0;i<sheet.panels.size();++i) {
        const auto& panel=sheet.panels[i];const auto& points=unfolded?panel.developed:panel.outer;
        kernel::ExtrusionRequest request;request.outer_profile=kernel::ExtrusionRequest::PolygonProfile{points};request.direction=mul(unfolded?Vec3{0,0,1}:panel.inward,sheet.thickness);
        const auto parent="transition:authored-panel:"+std::to_string(i);request.profile_region_id=parent;request.outer_boundary_id=parent+":boundary";
        for(std::size_t j=0;j<points.size();++j){
            request.outer_edge_source_ids.push_back(parent+":"+panel.edge_roles.at(j));
            request.outer_vertex_source_ids.push_back(parent+":junction:"+panel.edge_roles[(j+points.size()-1)%points.size()]+":"+panel.edge_roles[j]);
        }
        group.children.push_back(std::move(request));
    }
    for(std::size_t i=0;i<sheet.bends.size();++i) {
        const auto& bend=sheet.bends[i];const auto& sections=unfolded?bend.developed_sections:bend.sections;
        kernel::Sweep3DRequest request;request.smooth_loft=true;request.fixed_section_frames=true;request.linear_tolerance=loft_tolerance;
        const auto parent="transition:authored-bend:"+std::to_string(i);
        for(std::size_t j=0;j<sections.size();++j) {
            const auto point_id=parent+(j==0?":start":j+1==sections.size()?":end":":interior");
            const auto center=mul(add(sections[j][0],sections[j][2]),.5);request.path_points.push_back(center);request.path_point_ids.push_back(point_id);
            if(j>0&&j+1<sections.size())request.canonical_station_ids.insert(point_id);
            kernel::Sweep3DRequest::Section section;section.profile_id=parent;section.point_id=point_id;section.point_index=j;
            section.profile_normal=unit(cross(sub(sections[j][1],sections[j][0]),sub(sections[j][3],sections[j][0])));
            section.profile.region_id=parent;section.profile.outer_boundary_id=parent+":boundary";
            section.profile.outer_profile=kernel::ExtrusionRequest::PolygonProfile{{sections[j].begin(),sections[j].end()}};
            section.profile.outer_edge_source_ids={parent+":outer:from:profile",parent+":last",parent+":inner:from:profile",parent+":first"};
            section.profile.outer_vertex_source_ids={parent+":first:outer",parent+":last:outer",parent+":last:inner",parent+":first:inner"};
            request.sections.push_back(std::move(section));if(j)request.path_segments.push_back({parent+":span",request.path_points[j-1],center});
        }
        group.children.push_back(std::move(request));
    }
    return group;
}
kernel::HistoryOperation sheet_operation(const SheetResult& sheet,const std::string& owner) {
    using namespace kernel::sheet_material;
    const auto source=sheet_request(sheet);kernel::FeatureGroupRequest ordered;
    kernel::HistoryOperation result;result.owner_id=owner;
    // Join approximated bend skins within a fraction of their fitting budget.
    // The document's coarser tolerance can erase the small converging trims.
    result.boolean_tolerance=loft_tolerance/8;
    std::string parent;
    const auto append=[&](const auto& primitive,kernel::SheetMaterialDefinition material,const std::string& role){
        material.owner_id=owner+":"+role;material.feature_owner_id=owner;material.parent_owner_id=parent;parent=material.owner_id;
        result.sheet_regions.push_back(material);ordered.children.push_back(primitive);
    };
    for(std::size_t i=0;i<sheet.panels.size();++i) {
        const auto& panel=sheet.panels[i];kernel::SheetMaterialDefinition plane;plane.kind=kernel::SheetMaterialDefinition::Kind::Plane;
        plane.origin=panel.outer.front();plane.along=unit(sub(panel.outer[1],panel.outer[0]));plane.radial=mul(panel.inward,-1);plane.tangent=cross(plane.radial,plane.along);plane.thickness=sheet.thickness;plane.thickness_sign=-1;
        // A child plane attaches on its parent's final tangent, not on an
        // arbitrary polygon vertex after clipping both neighboring bends.
        // This point has the exact final angular material coordinate.
        if(i)for(const auto& bend:sheet.bends)if(bend.boundary_index+1==i)
            plane.origin=bend.untrimmed_exit;
        append(source.children[i],plane,"panel:"+std::to_string(i));
        for(std::size_t b=0;b<sheet.bends.size();++b)if(sheet.bends[b].boundary_index==i) {
            auto material=sheet.bends[b].material;material.curved_source_id="transition:authored-bend:"+std::to_string(b)+":span";
            append(source.children[sheet.panels.size()+b],material,"bend:"+std::to_string(b));
        }
    }
    result.primitive=std::move(ordered);return result;
}
}
