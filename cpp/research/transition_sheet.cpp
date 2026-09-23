#include "transition_sheet.hpp"
#include <algorithm>
#include <numbers>
namespace zima::research::transition {
namespace {
using namespace kernel::sheet_material;
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
SheetResult manufacture(const HalfModel& model,const SheetOptions& options) {
    using namespace kernel::sheet_material;
    if(!std::isfinite(options.thickness)||options.thickness<=0||!std::isfinite(options.inside_radius)||options.inside_radius<=0||
        !std::isfinite(options.k_factor)||options.k_factor<0||options.k_factor>1)throw std::invalid_argument("Invalid transition sheet parameters");
    const auto surface=calculate(model);if(!surface.valid())throw std::invalid_argument("Invalid transition surface");
    SheetResult result;result.thickness=options.thickness;
    const double outer_radius=options.inside_radius+options.thickness,neutral_radius=options.inside_radius+options.k_factor*options.thickness;
    std::vector<Vec3> shifts(surface.faces.size());
    for(const auto& face:surface.faces)result.panels.push_back({face.folded,{},face.normal});
    const Vec3 cap0=model.first_origin.origin,normal0=model.first_origin.z;
    const Vec3 cap1=model.first_origin.point(model.second_relative.origin),normal1=model.first_origin.direction(model.second_relative.z);
    for(std::size_t i=0;i<surface.folds.size();++i) {
        const auto& fold=surface.folds[i];const auto& before=surface.faces[i];const auto& after=surface.faces[i+1];
        shifts[i+1]=shifts[i];const double angle=fold.signed_angle_radians;
        if(std::abs(angle)<1e-8)continue;
        if(angle<=0||angle>=std::numbers::pi-1e-6)throw std::invalid_argument("Unsupported reverse transition bend");
        const auto along=unit(sub(fold.second,fold.first));
        auto in_before=unit(cross(before.normal,along));if(dot(sub(centroid(before.folded),fold.first),in_before)<0)in_before=mul(in_before,-1);
        auto in_after=unit(cross(after.normal,along));if(dot(sub(centroid(after.folded),fold.first),in_after)<0)in_after=mul(in_after,-1);
        const double setback=outer_radius*std::tan(angle/2);
        result.panels[i].outer=clip(result.panels[i].outer,fold.first,in_before,setback);
        result.panels[i+1].outer=clip(result.panels[i+1].outer,fold.first,in_after,setback);
        const auto flat_a=after.unfolded[0],flat_b=after.unfolded[1],flat_along=unit(sub(flat_b,flat_a));
        auto across=Vec3{-flat_along.y,flat_along.x,0};if(dot(sub(centroid(after.unfolded),flat_a),across)<0)across=mul(across,-1);
        const double allowance=neutral_radius*angle;
        shifts[i+1]=add(shifts[i],mul(across,allowance-2*setback));
        SheetBend bend;bend.boundary_index=i;bend.along=along;bend.angle=angle;bend.outer_radius=outer_radius;bend.neutral_radius=neutral_radius;bend.thickness=options.thickness;
        bend.center=add(fold.first,mul(add(before.normal,after.normal),outer_radius/(1+dot(before.normal,after.normal))));
        bend.start_radial=mul(before.normal,-1);bend.turn_tangent=cross(along,bend.start_radial);
        // Resolve end extents against authored profile planes at each angular station.
        if(std::abs(dot(normal0,along))<1e-8||std::abs(dot(normal1,along))<1e-8)throw std::invalid_argument("Bend axis parallel to transition rim");
        const auto section=[&](double parameter,bool developed) {
            const auto radial=add(mul(bend.start_radial,std::cos(parameter)),mul(bend.turn_tangent,std::sin(parameter)));
            const auto base=add(bend.center,mul(radial,outer_radius));
            const double first=dot(sub(cap0,base),normal0)/dot(along,normal0),last=dot(sub(cap1,base),normal1)/dot(along,normal1);
            if(last<=first)throw std::invalid_argument("Reversed transition bend extent");
            if(developed) {
                const auto origin=add(add(flat_a,shifts[i]),mul(across,neutral_radius*parameter-setback));
                const auto a=add(origin,mul(flat_along,first)),b=add(origin,mul(flat_along,last));
                // +Z is inward in this material chart; inner-skin axes use z=t.
                return std::array<Vec3,4>{a,b,add(b,{0,0,options.thickness}),add(a,{0,0,options.thickness})};
            }
            const auto a=add(base,mul(along,first)),b=add(base,mul(along,last));
            return std::array<Vec3,4>{a,b,sub(b,mul(radial,options.thickness)),sub(a,mul(radial,options.thickness))};
        };
        const std::size_t steps=std::max<std::size_t>(4,static_cast<std::size_t>(std::ceil(angle/(std::numbers::pi/90))));
        for(std::size_t j=0;j<=steps;++j){const double u=angle*j/steps;bend.sections.push_back(section(u,false));bend.developed_sections.push_back(section(u,true));}
        const auto mid=section(angle/2,true);result.axes.push_back({mid[3],mid[2],PatternRole::BendAxis,angle});
        auto& material=bend.material;material.kind=kernel::SheetMaterialDefinition::Kind::Cylinder;
        material.origin=add(bend.center,mul(bend.start_radial,options.inside_radius));
        material.along=along;material.tangent=bend.turn_tangent;material.radial=bend.start_radial;
        material.radius=options.inside_radius;material.neutral_radius=neutral_radius;material.angle=angle;material.thickness=options.thickness;material.thickness_sign=1;
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
            if(on_rim(cap0,normal0))panel.edge_roles.push_back("round-rim");
            else if(on_rim(cap1,normal1))panel.edge_roles.push_back("rectangle-rim");
            else {
                const auto middle=mul(add(a,b),.5);
                const auto distance_to=[&](std::size_t edge){const auto start=source.folded[edge],direction=unit(sub(source.folded[(edge+1)%source.folded.size()],start));return length(cross(sub(middle,start),direction));};
                panel.edge_roles.push_back(distance_to(0)<distance_to(2)?"entry":"exit");
            }
        }
    }
    return result;
}
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
        kernel::Sweep3DRequest request;request.smooth_loft=true;request.fixed_section_frames=true;request.linear_tolerance=.001;
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
    std::string parent;
    const auto append=[&](const auto& primitive,kernel::SheetMaterialDefinition material,const std::string& role){
        material.owner_id=owner+":"+role;material.feature_owner_id=owner;material.parent_owner_id=parent;parent=material.owner_id;
        result.sheet_regions.push_back(material);ordered.children.push_back(primitive);
    };
    for(std::size_t i=0;i<sheet.panels.size();++i) {
        const auto& panel=sheet.panels[i];kernel::SheetMaterialDefinition plane;plane.kind=kernel::SheetMaterialDefinition::Kind::Plane;
        plane.origin=panel.outer.front();plane.along=unit(sub(panel.outer[1],panel.outer[0]));plane.radial=mul(panel.inward,-1);plane.tangent=cross(plane.radial,plane.along);plane.thickness=sheet.thickness;plane.thickness_sign=-1;
        append(source.children[i],plane,"panel:"+std::to_string(i));
        for(std::size_t b=0;b<sheet.bends.size();++b)if(sheet.bends[b].boundary_index==i) {
            auto material=sheet.bends[b].material;material.curved_source_id="transition:authored-bend:"+std::to_string(b)+":span";
            append(source.children[sheet.panels.size()+b],material,"bend:"+std::to_string(b));
        }
    }
    result.primitive=std::move(ordered);return result;
}
}
