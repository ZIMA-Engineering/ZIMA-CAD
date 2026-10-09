#include <zima/document/sheet_form_definition.hpp>
#include <zima/document/feature_sketches.hpp>
#include <zima/document/placement_orientation.hpp>
#include <zima/kernel/sheet_material.hpp>
#include <zima/kernel/stable_id.hpp>
#include <nlohmann/json.hpp>
#include <algorithm>
#include <cmath>
#include <cctype>
#include <set>
#include <deque>
#include <mutex>

namespace zima::document {
namespace {
constexpr std::array<const char*,4> names{"FORM_CUT","FORM","FORM_FLAT","FORM_SYMBOL"};
void local_references(const sketcher::Sketch& sketch,const std::string& document) {
    for(const auto& reference:sketch.external_references)
        if(reference.broken||(!reference.source_document_id.empty()&&reference.source_document_id!=document)||
            !reference.source_instance_path.empty()||!reference.context_assembly_document_id.empty())
            throw std::invalid_argument("FORM requires self-contained native references.");
}
std::string role_sketch(const PartDocument& part,const BodyHistory& body,bool optional) {
    if(optional&&body.entries.empty())return {};
    if(body.suppressed||body.cursor!=body.entries.size()||body.entries.size()!=1)
        throw std::invalid_argument("A FORM sketch role requires one active standalone Sketch.");
    const auto* feature=part.find_container(body.entries.front().id);
    const bool sketch_feature=feature&&(feature->feature_kind==FeatureKind::Sketch||
        (feature->feature_kind==FeatureKind::Feature&&feature->feature.type==FeatureType::Sketch));
    if(!sketch_feature||feature->suppressed)
        throw std::invalid_argument("A FORM sketch role requires one active standalone Sketch.");
    const auto sketch=std::ranges::find(part.sketches,feature->id,&sketcher::Sketch::owner_container_id);
    if(sketch==part.sketches.end())
        throw std::invalid_argument("A FORM sketch role requires one active standalone Sketch.");
    sketch->validate();
    return sketch->id;
}
std::vector<std::string> cut_sketches(const PartDocument& part,const BodyHistory& body) {
    if(body.entries.size()==1)return {role_sketch(part,body,false)};
    if(body.suppressed||body.cursor!=body.entries.size()||body.entries.size()!=2)
        throw std::invalid_argument("FORM_CUT requires one Sketch or two perpendicular corner Sketches.");
    std::array<const sketcher::Sketch*,2> sketches{};
    for(std::size_t i=0;i<2;++i) {
        auto single=body;single.entries={body.entries[i]};single.cursor=1;
        const auto id=role_sketch(part,single,false);
        sketches[i]=&*std::ranges::find(part.sketches,id,&sketcher::Sketch::id);
    }
    using namespace kernel::sheet_material;
    if(std::abs(dot(unit(sketches[0]->normal()),unit(sketches[1]->normal())))>1e-10||
       dot(unit(sketches[0]->resolved_x_axis),unit(sketches[1]->resolved_x_axis))<1.-1e-10)
        throw std::invalid_argument("FORM_CUT requires one Sketch or two perpendicular corner Sketches.");
    // Native definition planes determine the attachment frame, independently
    // of the order in which the two cutting Sketches were authored.
    if(std::abs(dot(unit(sketches[1]->normal()),kernel::Vec3{0,0,1}))>1.-1e-10)std::swap(sketches[0],sketches[1]);
    if(std::abs(dot(unit(sketches[0]->normal()),kernel::Vec3{0,0,1}))<1.-1e-10||
       std::abs(dot(sub(sketches[1]->resolved_origin,sketches[0]->resolved_origin),sketches[1]->normal()))>1e-8)
        throw std::invalid_argument("FORM_CUT requires one Sketch or two perpendicular corner Sketches.");
    return {sketches[0]->id,sketches[1]->id};
}
}
namespace {
SheetFormDefinition definition(PartDocument part,const std::vector<kernel::BodyResult>& calculated,
        const std::array<std::string,4>* stored) {
    part.validate_body_ownership();
    SheetFormDefinition result;
    for(unsigned role=0;role<names.size();++role) {
        const BodyHistory* selected=nullptr;
        for(const auto& body:part.body_history.bodies())if(stored?body.scope.id==(*stored)[role]:body.name==names[role]) {
            if(selected)throw std::invalid_argument("FORM requires four distinct named Body roles.");
            selected=&body;
        }
        if(!selected||selected->derived_copy||selected->link||selected->scale||selected->suppressed)
            throw std::invalid_argument("FORM requires four distinct named Body roles.");
        result.bodies[role]=selected->scope.id;
    }
    if(!part.body_history.booleans().empty())
        throw std::invalid_argument("FORM requires independent native Body roles.");
    if(std::set<std::string>(result.bodies.begin(),result.bodies.end()).size()!=names.size())
        throw std::invalid_argument("FORM requires four distinct named Body roles.");
    for(const auto& feature:part.history)if(feature.feature_kind==FeatureKind::SheetForm)
        throw std::invalid_argument("FORM requires independent native Body roles.");
    for(const auto& body:part.body_history.bodies())if(body.link)
        throw std::invalid_argument("FORM requires self-contained native references.");
    for(const auto& sketch:part.sketches)local_references(sketch,part.document_id);
    for(const auto& feature:part.history)visit_feature_sketches(feature,[&](const auto& data,std::size_t) {
        local_references(sketcher::Sketch::from_serialized(data),part.document_id);
    });
    const auto& cutting=*part.body_history.find(result.bodies[0]);
    if(!cutting.entries.empty()) {
        result.cut_sketches=cut_sketches(part,cutting);
        result.cut_sketch=result.cut_sketches.front();
    }
    result.flat_sketch=role_sketch(part,*part.body_history.find(result.bodies[2]),true);
    result.symbol_sketch=role_sketch(part,*part.body_history.find(result.bodies[3]),false);
    const auto& shape=*part.body_history.find(result.bodies[1]);
    if(shape.entries.empty()||shape.cursor!=shape.entries.size())
        throw std::invalid_argument("FORM requires a calculated outer surface.");
    if(calculated.empty()||!calculated.back().calculation_errors.empty())
        throw std::invalid_argument("FORM requires a calculated outer surface.");
    const auto source=calculated.back().body_outputs.find(shape.scope.id);
    if(source==calculated.back().body_outputs.end()||source->second->mesh.triangle_references.empty()||
        source->second->volume<0.||!source->second->calculation_errors.empty())
        throw std::invalid_argument("FORM requires a calculated outer surface.");
    std::set<std::pair<std::string,std::string>> faces;
    for(const auto& face:source->second->mesh.triangle_references) {
        if(!face.valid()||(source->second->volume==0.&&!face.surface_result)||!face.instance_path.empty())
            throw std::invalid_argument("FORM requires a calculated outer surface.");
        faces.emplace(face.owner_id,face.semantic_key);
    }
    // Choose an existing persisted face as the shell anchor. This does not
    // create topology identity from traversal position.
    result.surface={faces.begin()->first,faces.begin()->second,{}};
    result.surface.surface_result=source->second->volume==0.;
    if(source->second->volume>0.) {
        if(!result.cut_sketches.empty())throw std::invalid_argument("Invalid FORM definition.");
        std::vector<kernel::HistoryOperation> local;
        for(auto operation:part.kernel_operations())if(operation.body.id==shape.scope.id) {
            operation.body={};local.push_back(std::move(operation));
        }
        const auto boundary=calculated.back().body_boundaries.find(shape.scope.id);
        if(boundary==calculated.back().body_boundaries.end()||boundary->second.empty()||
           boundary->second.back().source_fingerprint!=kernel::history_fingerprint(local,local.size()))
            throw std::invalid_argument("FORM definition calculation failed.");
        using namespace kernel::sheet_material;
        for(const auto normal:{kernel::Vec3{0,0,1},kernel::Vec3{0,1,0}}) {
            std::map<std::pair<std::string,std::string>,kernel::FaceReference> closing;
            for(auto face:source->second->mesh.triangle_references) {
                // Display triangle tags persist identity; the matching native
                // reference packet owns the analytic geometry after reopening.
                if(!face.surface) {
                    const auto& originals=source->second->mesh.original_references.triangle_references;
                    const auto original=std::ranges::find(originals,face);
                    if(original!=originals.end())face.surface=original->surface;
                }
                if(face.surface&&face.surface->kind==kernel::SurfaceGeometry::Kind::Plane&&
                    std::abs(dot(unit(face.surface->axis),normal))>1.-1e-10&&
                    std::abs(dot(face.surface->origin,normal))<1e-8)
                    closing.emplace(std::pair{face.owner_id,face.semantic_key},face);
            }
            if(closing.size()!=1)throw std::invalid_argument("Invalid FORM definition.");
            result.solid_opening_faces.push_back(closing.begin()->second);
        }
    }else if(result.cut_sketch.empty())throw std::invalid_argument("Invalid FORM definition.");
    result.part=std::move(part);
    result.calculated=std::make_shared<const std::vector<kernel::BodyResult>>(calculated);
    return result;
}
}
SheetFormDefinition sheet_form_definition(PartDocument part,const std::vector<kernel::BodyResult>& calculated) {
    return definition(std::move(part),calculated,nullptr);
}
SheetFormDefinition read_sheet_form_definition(const std::filesystem::path& path) {
    auto extension=path.extension().string();
    std::ranges::transform(extension,extension.begin(),[](unsigned char c){return static_cast<char>(std::tolower(c));});
    if(extension!=".prtz")
        throw std::invalid_argument("Select a native FORM Part (.prtz).");
    std::vector<kernel::BodyResult> calculated;
    auto part=PartDocument::load(path,&calculated);
    return sheet_form_definition(std::move(part),calculated);
}
SheetFormDefinition stored_sheet_form_definition(const SheetFormParameters& value) {
    validate_sheet_form_parameters(value);
    // Placement solving and fingerprints consume the same immutable embedded
    // source repeatedly. Parse it once; never memoize mutable Part documents.
    // Weak source ownership retires entries after their native definition dies.
    struct Parsed {std::weak_ptr<const std::string> source;std::shared_ptr<const SheetFormDefinition> model;};
    static std::mutex mutex;
    static std::deque<Parsed> cache;
    std::shared_ptr<const SheetFormDefinition> source;
    {
        const std::lock_guard lock(mutex);
        std::erase_if(cache,[](const auto& item){return item.source.expired();});
        for(const auto& item:cache)if(item.source.lock()==value.definition){source=item.model;break;}
    }
    if(!source) {
        std::vector<kernel::BodyResult> calculated;
        auto part=PartDocument::from_serialized(nlohmann::json::parse(*value.definition),&calculated);
        source=std::make_shared<const SheetFormDefinition>(definition(std::move(part),calculated,&value.bodies));
        const std::lock_guard lock(mutex);
        cache.push_back({value.definition,source});
        while(cache.size()>4)cache.pop_front();
    }
    if(source->bodies!=value.bodies||source->cut_sketch!=value.cut_sketch||source->flat_sketch!=value.flat_sketch||
        source->symbol_sketch!=value.symbol_sketch||source->surface!=value.surface)
        throw std::invalid_argument("Invalid FORM definition.");
    return *source;
}
SheetFormParameters copy_sheet_form_definition(const SheetFormDefinition& source,
        const std::vector<kernel::BodyResult>& calculated) {
    const auto& boundaries=calculated.empty()&&source.calculated?*source.calculated:calculated;
    auto root=source.part.serialized(boundaries,{kernel::make_stable_id(),{}, {}});
    root["name"]=source.part.name;
    SheetFormParameters value;value.definition=std::make_shared<const std::string>(root.dump());
    value.bodies=source.bodies;value.cut_sketch=source.cut_sketch;value.flat_sketch=source.flat_sketch;
    value.symbol_sketch=source.symbol_sketch;value.source_name=source.part.name;value.surface=source.surface;
    // Namespace remapping may affect a document-owned topology parent.
    const auto copied=stored_sheet_form_definition(value);
    value.surface=copied.surface;return value;
}
HistoryContainer create_sheet_form() {
    auto feature=PartDocument::create_sketch_container();feature.feature_kind=FeatureKind::SheetForm;feature.name="FORM";return feature;
}
std::vector<kernel::ViewerEdge> sheet_form_preview_edges(const SheetFormParameters& value) {
    const auto source=stored_sheet_form_definition(value);
    const auto request=sheet_form_request(source,{}, {},{0,1,0},{1,0,0},value.thickness);
    const auto& outputs=source.calculated->back().body_outputs;
    const auto body=outputs.find(source.bodies[1]);
    if(body==outputs.end())throw std::invalid_argument("FORM requires a calculated outer surface.");
    auto edges=body->second->mesh.edges;
    const auto z=kernel::sheet_material::cross(request.source_x,request.source_normal);
    for(auto& edge:edges) {
        for(auto& point:edge.points) {
            const auto relative=kernel::sheet_material::sub(point,request.source_origin);
            point={kernel::sheet_material::dot(relative,request.source_x),
                kernel::sheet_material::dot(relative,request.source_normal),kernel::sheet_material::dot(relative,z)};
        }
        // A transient wire offers no source-document topology for picking.
        edge.reference={};edge.display_owner_id.clear();edge.exact_spline.reset();
        edge.edge_treatment_side_references.clear();edge.edge_treatment_side_directions.clear();
        edge.edge_treatment_endpoint_references.clear();
    }
    std::erase_if(edges,[](const auto& edge){return edge.parameter_seam;});return edges;
}
Placement sheet_form_attachment(const kernel::FaceReference& face,
        const std::string& body_origin,const kernel::ViewerReferenceGeometry& geometry,
        Placement seed,kernel::Vec3 point) {
    if(!face.valid()||!face.instance_path.empty()||!face.surface||
        face.surface->kind!=kernel::SurfaceGeometry::Kind::Plane||
        (face.sheet_role!=kernel::SheetFaceRole::SideA&&face.sheet_role!=kernel::SheetFaceRole::SideB)||
        !(face.sheet_thickness>0))
        throw std::invalid_argument("Select a planar outer sheet face for FORM.");
    ConstructionReference support;support.owner_id=face.owner_id;
    support.semantic_key=face.semantic_key;support.supports_offset=true;
    const auto plane_normal=[&](const ConstructionReference& reference) {
        const auto base=measure_placement_reference_offset(reference,geometry,{});
        const auto x=measure_placement_reference_offset(reference,geometry,{1,0,0});
        const auto y=measure_placement_reference_offset(reference,geometry,{0,1,0});
        const auto z=measure_placement_reference_offset(reference,geometry,{0,0,1});
        if(!base||!x||!y||!z)throw std::invalid_argument("Navržené reference umístění prvku nelze vyřešit.");
        return kernel::Vec3{*x-*base,*y-*base,*z-*base};
    };
    const auto normal=plane_normal(support);
    const auto distance=measure_placement_reference_offset(support,geometry,point);
    if(!distance)throw std::invalid_argument("Select a planar outer sheet face for FORM.");
    point=kernel::sheet_material::sub(point,kernel::sheet_material::mul(normal,*distance));
    std::array<ConstructionReference,3> datums;
    std::array<kernel::Vec3,3> normals;
    constexpr std::array keys{"origin:plane:xy","origin:plane:xz","origin:plane:yz"};
    for(std::size_t i=0;i<datums.size();++i) {
        auto& reference=datums[i];reference.owner_id=body_origin;
        reference.semantic_key=keys[i];reference.supports_offset=true;
        normals[i]=plane_normal(reference);
        reference.offset=*measure_placement_reference_offset(reference,geometry,point);
    }
    double independent=0;std::array<std::size_t,2> pair{};
    for(std::size_t a=0;a<3;++a)for(std::size_t b=a+1;b<3;++b) {
        const auto determinant=std::abs(kernel::sheet_material::dot(normal,kernel::sheet_material::cross(normals[a],normals[b])));
        if(determinant>independent){independent=determinant;pair={a,b};}
    }
    if(independent<1e-8)throw std::invalid_argument("Navržené reference umístění prvku nelze vyřešit.");
    auto front=support;front.orientation_drives_rotation=true;
    front.orientation_only=true;front.orientation_role="front";
    seed.references={support,datums[pair[0]],datums[pair[1]],front};
    seed.x=point.x;seed.y=point.y;seed.z=point.z;
    seed.rotation_offset_x=0;seed.rotation_offset_z=0;
    seed.orientation_back=false;seed.orientation_quarter_turns=0;
    if(!resolve_placement(seed,geometry))throw std::invalid_argument("Navržené reference umístění prvku nelze vyřešit.");
    // Present the default datums in the same order as point-coordinate entry:
    // X first, then Z in the zero-angle sheet frame. This also permits replacing
    // one datum at a time without temporarily duplicating the other equation.
    auto zero=seed;zero.absolute_rotation_y=0;
    if(!resolve_placement(zero,geometry))throw std::invalid_argument("Navržené reference umístění prvku nelze vyřešit.");
    const auto x=construction_direction_from_local_axis("x",{zero.rotation_x,zero.rotation_y,zero.rotation_z});
    if(std::abs(kernel::sheet_material::dot(normals[pair[0]],x))<std::abs(kernel::sheet_material::dot(normals[pair[1]],x)))
        std::swap(seed.references[1],seed.references[2]);
    return seed;
}
bool sheet_form_position_reference_available(ConstructionReference ref,
        const kernel::ViewerReferenceGeometry& geometry,kernel::Vec3 normal) {
    using namespace kernel::sheet_material;
    const auto same=[&](const auto& a) {return a.owner_id==ref.owner_id&&a.semantic_key==ref.semantic_key&&a.instance_path==ref.instance_path;};
    if(ref.owner_id.empty())return false;
    if(std::ranges::any_of(geometry.points,[&](const auto& p){return same(p.reference);}))return true;
    const auto face=std::ranges::find_if(geometry.triangle_references,same);
    const bool datum=ref.semantic_key=="plane"||ref.semantic_key.starts_with("plane:")||ref.semantic_key.starts_with("origin:plane:");
    if(face!=geometry.triangle_references.end()||datum) {
        if(!datum&&(!face->surface||face->surface->kind!=kernel::SurfaceGeometry::Kind::Plane))return false;
        ref.supports_offset=true;
        const auto a=measure_placement_reference_offset(ref,geometry,{}),x=measure_placement_reference_offset(ref,geometry,{1,0,0});
        const auto y=measure_placement_reference_offset(ref,geometry,{0,1,0}),z=measure_placement_reference_offset(ref,geometry,{0,0,1});
        if(!a||!x||!y||!z)return false;
        const auto projected=cross(normal,{*x-*a,*y-*a,*z-*a});return dot(projected,projected)>1e-16;
    }
    kernel::Vec3 direction;
    const auto axis=std::ranges::find_if(geometry.axes,[&](const auto& a){return same(a.reference);});
    if(axis!=geometry.axes.end())direction=axis->direction;
    else {
        const auto edge=std::ranges::find_if(geometry.edges,[&](const auto& e){return same(e.reference);});
        if(edge==geometry.edges.end()||edge->points.size()<2)return false;
        direction=sub(edge->points.back(),edge->points.front());const auto length=std::sqrt(dot(direction,direction));
        if(length<1e-10)return false;const auto unit=mul(direction,1./length);
        const auto straight=[&](const auto& points){return std::ranges::all_of(points,[&](const auto& p){const auto v=cross(sub(p,edge->points.front()),unit);return dot(v,v)<=1e-14;});};
        if(!straight(edge->points)||(edge->exact_spline&&!straight(edge->exact_spline->poles)))return false;
    }
    const auto projected=cross(normal,direction);return dot(projected,projected)>1e-16;
}
bool sheet_form_corner_faces_available(const kernel::FaceReference& first,
        const kernel::FaceReference& second,const kernel::ViewerReferenceGeometry& geometry) {
    using namespace kernel::sheet_material;
    const auto planar=[](const auto& face) {return face.surface&&
        face.surface->kind==kernel::SurfaceGeometry::Kind::Plane&&face.sheet_thickness>0.&&
        (face.sheet_role==kernel::SheetFaceRole::SideA||face.sheet_role==kernel::SheetFaceRole::SideB);};
    if(!planar(first)||!planar(second)||first.instance_path!=second.instance_path||
        std::abs(first.sheet_thickness-second.sheet_thickness)>1e-10)return false;
    const auto a=mul(unit(first.surface->axis),first.surface->reversed?-1.:1.),
        b=mul(unit(second.surface->axis),second.surface->reversed?-1.:1.);
    if(std::abs(dot(a,b))>1e-10)return false;
    return std::ranges::any_of(geometry.triangle_references,[&](const auto& face) {
        if(!face.surface||face.surface->kind!=kernel::SurfaceGeometry::Kind::Cylinder||
            face.instance_path!=first.instance_path||face.sheet_owner.empty()||
            std::abs(face.sheet_thickness-first.sheet_thickness)>1e-10)return false;
        const auto& cylinder=*face.surface;
        // Outward normals distinguish the outer virtual sharp corner from the
        // inner pair, including zero offsets. No kernel traversal is needed.
        return std::abs(dot(unit(cylinder.axis),unit(cross(a,b))))>1.-1e-10&&
            std::abs(dot(sub(first.surface->origin,cylinder.origin),a)-cylinder.radius)<1e-7&&
            std::abs(dot(sub(second.surface->origin,cylinder.origin),b)-cylinder.radius)<1e-7;
    });
}
namespace {
bool resolve_form_placement(Placement& placement,
        const kernel::ViewerReferenceGeometry& geometry,kernel::Vec3* base_rotation,
        bool* orientation_from_reference,bool corner,bool outer_corner=false) {
    using namespace kernel::sheet_material;
    const auto fail=[&] {placement.reference_valid=false;return false;};
    const auto same=[](const auto& a,const auto& b) {return a.owner_id==b.owner_id&&
        a.semantic_key==b.semantic_key&&a.instance_path==b.instance_path;};
    std::vector<ConstructionReference> rows;
    for(const auto& ref:placement.references)if(!ref.orientation_only)rows.push_back(ref);
    if(rows.size()!=3||std::ranges::any_of(rows,[](const auto& r){return r.owner_id.empty();}))return fail();
    const auto support=std::ranges::find_if(geometry.triangle_references,[&](const auto& f){return same(f,rows[0]);});
    if(support==geometry.triangle_references.end()||!support->surface||
       support->surface->kind!=kernel::SurfaceGeometry::Kind::Plane||
       rows[0].offset!=0.)return fail();
    auto next=placement;auto front=rows[0];front.orientation_only=true;
    front.orientation_drives_rotation=true;front.orientation_role="front";
    next.references={rows[0],front};
    if(corner) {
        const auto other=std::ranges::find_if(geometry.triangle_references,[&](const auto& f){return same(f,rows[1]);});
        if(other==geometry.triangle_references.end()||!other->surface||
           other->surface->kind!=kernel::SurfaceGeometry::Kind::Plane||rows[1].offset!=0.||
           std::abs(other->sheet_thickness-support->sheet_thickness)>1e-10||
           (other->sheet_role!=kernel::SheetFaceRole::SideA&&other->sheet_role!=kernel::SheetFaceRole::SideB)||
           std::abs(dot(unit(support->surface->axis),unit(other->surface->axis)))>1e-10)return fail();
        if(outer_corner&&!sheet_form_corner_faces_available(*support,*other,geometry))return fail();
        // The two actual sheet sides fix the corner frame. Local +Z faces
        // opposite the second outward normal, so +X follows their intersection.
        auto top=rows[1];top.orientation_only=true;top.orientation_drives_rotation=true;
        top.orientation_role="top";top.flip=!top.flip;next.references.push_back(std::move(top));
        next.absolute_rotation_y=0.;next.rotation_offset_x=next.rotation_offset_y=next.rotation_offset_z=0.;
    }
    kernel::Vec3 base;bool oriented=false;
    if(!resolve_placement(next,geometry,&base,&oriented))return fail();
    auto zero=next;zero.absolute_rotation_y=0;
    if(!resolve_placement(zero,geometry))return fail();
    const auto x=construction_direction_from_local_axis("x",{zero.rotation_x,zero.rotation_y,zero.rotation_z});
    const auto z=construction_direction_from_local_axis("z",{zero.rotation_x,zero.rotation_y,zero.rotation_z});
    const auto normal=construction_direction_from_local_axis("y",{zero.rotation_x,zero.rotation_y,zero.rotation_z});
    const auto origin=support->surface->origin;
    std::array<kernel::Vec3,2> normals;std::array<double,2> rhs;
    for(std::size_t row=0;row<2;++row) {
        const auto& ref=rows[row+1];if(!std::isfinite(ref.offset))return fail();
        const auto face=std::ranges::find_if(geometry.triangle_references,[&](const auto& f){return same(f,ref);});
        const bool datum_plane=ref.semantic_key=="plane"||ref.semantic_key.starts_with("plane:")||
            ref.semantic_key.starts_with("origin:plane:");
        if(face!=geometry.triangle_references.end()||datum_plane) {
            if(!datum_plane&&face!=geometry.triangle_references.end()&&
               (!face->surface||face->surface->kind!=kernel::SurfaceGeometry::Kind::Plane))return fail();
            const auto b=measure_placement_reference_offset(ref,geometry,{});
            const auto dx=measure_placement_reference_offset(ref,geometry,{1,0,0});
            const auto dy=measure_placement_reference_offset(ref,geometry,{0,1,0});
            const auto dz=measure_placement_reference_offset(ref,geometry,{0,0,1});
            if(!b||!dx||!dy||!dz)return fail();
            normals[row]={*dx-*b,*dy-*b,*dz-*b};rhs[row]=ref.offset-*b;
            continue;
        }
        const auto point=std::ranges::find_if(geometry.points,[&](const auto& p){return same(p.reference,ref);});
        if(point!=geometry.points.end()) {normals[row]=(corner||row==0)?x:z;
            rhs[row]=dot(normals[row],point->position)+ref.offset;continue;}
        kernel::Vec3 anchor,direction;
        const auto axis=std::ranges::find_if(geometry.axes,[&](const auto& a){return same(a.reference,ref);});
        if(axis!=geometry.axes.end()) {anchor=axis->point;direction=axis->direction;}
        else {
            const auto edge=std::ranges::find_if(geometry.edges,[&](const auto& e){return same(e.reference,ref);});
            if(edge==geometry.edges.end()||edge->points.size()<2)return fail();
            anchor=edge->points.front();direction=sub(edge->points.back(),anchor);
            const auto length=std::sqrt(dot(direction,direction));if(length<1e-10)return fail();
            const auto unit=mul(direction,1./length);
            for(const auto& p:edge->points) {const auto v=cross(sub(p,anchor),unit);
                if(dot(v,v)>1e-14)return fail();}
            if(edge->exact_spline)for(const auto& p:edge->exact_spline->poles) {const auto v=cross(sub(p,anchor),unit);if(dot(v,v)>1e-14)return fail();}
        }
        auto perpendicular=cross(normal,direction);const auto length=std::sqrt(dot(perpendicular,perpendicular));
        if(length<1e-10)return fail();normals[row]=mul(perpendicular,1./length);
        rhs[row]=dot(normals[row],anchor)+ref.offset;
    }
    const auto a=dot(normals[0],x),b=dot(normals[0],z);
    const auto c=dot(normals[1],x),d=dot(normals[1],z);const auto determinant=a*d-b*c;
    if(std::abs(determinant)<1e-8)return fail();
    const auto e=rhs[0]-dot(normals[0],origin),f=rhs[1]-dot(normals[1],origin);
    const auto result=add(origin,add(mul(x,(e*d-b*f)/determinant),mul(z,(a*f-e*c)/determinant)));
    if(!std::isfinite(result.x)||!std::isfinite(result.y)||!std::isfinite(result.z))return fail();
    next.x=result.x;next.y=result.y;next.z=result.z;next.references=placement.references;
    next.reference_valid=true;placement=std::move(next);
    if(base_rotation)*base_rotation=base;if(orientation_from_reference)*orientation_from_reference=oriented;
    return true;
}
}
bool resolve_sheet_form_placement(Placement& placement,
        const kernel::ViewerReferenceGeometry& geometry,kernel::Vec3* base_rotation,
        bool* orientation_from_reference) {
    return resolve_form_placement(placement,geometry,base_rotation,orientation_from_reference,false);
}
bool resolve_sheet_form_feature_placement(const SheetFormParameters& definition,Placement& placement,
        const kernel::ViewerReferenceGeometry& geometry,kernel::Vec3* base_rotation,
        bool* orientation_from_reference) {
    const auto source=stored_sheet_form_definition(definition);
    return resolve_form_placement(placement,geometry,base_rotation,orientation_from_reference,
        source.corner(),!source.solid_opening_faces.empty());
}
void validate_sheet_form_parameters(const SheetFormParameters& value) {
    if(!value.definition||value.definition->empty()||!value.surface.valid()||
        !value.surface.instance_path.empty()||value.symbol_sketch.empty()||
        std::set<std::string>(value.bodies.begin(),value.bodies.end()).size()!=4||
        std::ranges::any_of(value.bodies,[](const auto& id){return id.empty();})||
        !std::isfinite(value.thickness)||value.thickness<=0)
        throw std::invalid_argument("Invalid FORM definition.");
}
kernel::HistoryOperation sheet_form_operation(const PartDocument& part,const HistoryContainer& feature) {
    if(feature.feature_kind!=FeatureKind::SheetForm)throw std::invalid_argument("Invalid FORM definition.");
    const auto& value=feature.sheet_form;const auto source=stored_sheet_form_definition(value);
    const auto& p=feature.placement;const kernel::Vec3 angles{p.rotation_x,p.rotation_y,p.rotation_z};
    auto request=sheet_form_request(source,value.support,{p.x,p.y,p.z},
        construction_direction_from_local_axis("y",angles),construction_direction_from_local_axis("x",angles),value.thickness);
    kernel::HistoryOperation operation{feature.id,std::move(request),kernel::BooleanOperation::Add,feature.suppressed};
    kernel::SheetMaterialDefinition material;material.kind=kernel::SheetMaterialDefinition::Kind::Form;
    material.owner_id=feature.id;material.feature_owner_id=feature.id;material.parent_owner_id=value.support.sheet_owner;
    material.origin={p.x,p.y,p.z};material.thickness=value.thickness;
    material.along=construction_direction_from_local_axis("x",angles);
    material.radial=construction_direction_from_local_axis("y",angles);
    material.tangent=kernel::sheet_material::cross(material.radial,material.along);
    operation.sheet_material=material;return operation;
}
kernel::SheetFormRequest sheet_form_request(const SheetFormDefinition& definition,
        kernel::FaceReference support,kernel::Vec3 position,kernel::Vec3 normal,
        kernel::Vec3 x_direction,double thickness) {
    if(!std::isfinite(thickness)||thickness<=0)
        throw std::invalid_argument("Invalid FORM definition.");
    auto part=definition.part;
    const auto cut=std::ranges::find(part.sketches,definition.cut_sketch,&sketcher::Sketch::id);
    if(cut==part.sketches.end()&&definition.solid_opening_faces.empty())throw std::invalid_argument("Invalid FORM definition.");
    kernel::SheetFormRequest result;
    result.definition_id=part.document_id;result.shape_body=definition.bodies[1];
    result.cut_body=definition.bodies[0];result.flat_body=definition.flat_sketch.empty()?std::string{}:definition.bodies[2];
    result.surface=definition.surface;result.support=std::move(support);
    result.position=position;result.normal=normal;result.x_direction=x_direction;result.thickness=thickness;
    if(!definition.solid_opening_faces.empty()) {
        result.solid_opening_faces=definition.solid_opening_faces;
        const auto& plane=*definition.solid_opening_faces.front().surface;
        result.source_origin={};
        result.source_normal=kernel::sheet_material::mul(plane.axis,plane.reversed?-1.:1.);
        const auto& other=*definition.solid_opening_faces[1].surface;
        result.source_x=kernel::sheet_material::unit(kernel::sheet_material::cross(
            kernel::sheet_material::mul(other.axis,other.reversed?-1.:1.),result.source_normal));
    }else {
    const auto& placement=part.body_history.find(result.cut_body)->scope.placement;
    const kernel::Vec3 angles{placement.rotation_x,placement.rotation_y,placement.rotation_z};
    const auto x=construction_direction_from_local_axis("x",angles),
        y=construction_direction_from_local_axis("y",angles),z=construction_direction_from_local_axis("z",angles);
    const auto rotate=[&](kernel::Vec3 v){return kernel::sheet_material::add(
        kernel::sheet_material::mul(x,v.x),kernel::sheet_material::add(
        kernel::sheet_material::mul(y,v.y),kernel::sheet_material::mul(z,v.z)));};
    result.source_origin=kernel::sheet_material::add(rotate(cut->resolved_origin),{placement.x,placement.y,placement.z});
    result.source_normal=rotate(cut->resolved_normal);result.source_x=rotate(cut->resolved_x_axis);
    }
    const auto symbol=std::ranges::find(part.sketches,definition.symbol_sketch,&sketcher::Sketch::id);
    if(symbol==part.sketches.end())throw std::invalid_argument("Invalid FORM definition.");
    auto symbol_mesh=part.place_body_mesh(symbol->evaluated_profile_sketch().viewer_mesh(),definition.bodies[3]);
    const auto source_z=kernel::sheet_material::cross(result.source_x,result.source_normal);
    for(auto edge:symbol_mesh.edges) {
        if(edge.construction)continue;
        const auto local_point=[&](kernel::Vec3 point) {
            const auto delta=kernel::sheet_material::sub(point,result.source_origin);
            return kernel::Vec3{kernel::sheet_material::dot(delta,result.source_x),
                kernel::sheet_material::dot(delta,result.source_normal),kernel::sheet_material::dot(delta,source_z)};
        };
        for(auto& point:edge.points)point=local_point(point);
        if(edge.exact_spline)for(auto& point:edge.exact_spline->poles)point=local_point(point);
        edge.reference.semantic_key="form:symbol:from:"+std::to_string(result.definition_id.size())+":"+
            result.definition_id+":"+std::to_string(edge.reference.owner_id.size())+":"+
            edge.reference.owner_id+":"+edge.reference.semantic_key;
        edge.overlay=true;result.symbol_edges.push_back(std::move(edge));
    }
    if(definition.calculated&&!definition.calculated->empty()) {
        const auto& histories=definition.calculated->back().body_boundaries;
        if(const auto body=histories.find(result.shape_body);body!=histories.end()&&!body->second.empty()) {
            const auto& surface=body->second.back();
            std::vector<kernel::HistoryOperation> local;
            for(auto operation:part.kernel_operations())if(operation.body.id==result.shape_body) {
                operation.body={};local.push_back(std::move(operation));
            }
            if(surface.calculation_errors.empty()&&surface.source_fingerprint==kernel::history_fingerprint(local,local.size())) {
                if(!definition.solid_opening_faces.empty())
                    result.solid_cut_snapshot=std::make_shared<const kernel::BodyResult>(surface);
                else if(!surface.kernel_faces.empty())result.surface_snapshot=std::make_shared<const kernel::BodyResult>(surface);
            }
        }
    }
    // The retained independent definition stays untouched. The explicit tool
    // calculation temporarily consumes the same owned Sketch and native IDs.
    auto profiles=definition.cut_sketches;
    if(!definition.flat_sketch.empty())profiles.push_back(definition.flat_sketch);
    for(const auto& id:profiles) {
        const auto sketch=std::ranges::find(part.sketches,id,&sketcher::Sketch::id);
        auto* feature=sketch==part.sketches.end()?nullptr:part.find_container(sketch->owner_container_id);
        if(!feature)throw std::invalid_argument("Invalid FORM definition.");
        feature->feature_kind=FeatureKind::Feature;feature->feature.type=FeatureType::Modeling;
        feature->feature.sketch_id=id;feature->combine_mode=CombineMode::Add;
        feature->feature.result_type=ProfileResultType::Solid;feature->feature.symmetric=false;
        feature->feature.sides={};
        for(auto& side:feature->feature.sides){side.operation=FeatureSideOperation::Extrusion;side.length=thickness*2;}
        // Corner profiles lie on the inner sheet planes. Cut outward through
        // each plate, retaining the curved material between those planes.
        // Ordinary planar FORM keeps its established two-sided cut.
        if(definition.cut_sketches.size()==2&&id!=definition.flat_sketch)
            feature->feature.sides[0].operation=FeatureSideOperation::None;
    }
    result.definition=std::make_shared<const std::vector<kernel::HistoryOperation>>(part.kernel_operations());
    return result;
}
} // namespace zima::document
