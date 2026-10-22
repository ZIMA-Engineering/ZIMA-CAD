#include <zima/sketcher/sketch_trim.hpp>
#include <zima/sketcher/curve_geometry.hpp>
#include <zima/document/document_session.hpp>
#include <zima/workspace/sketch_reference_operations.hpp>
#include <zima/workspace/model_calculation.hpp>
#include <algorithm>
#include <cmath>
#include <iostream>
#include <set>
#include <tuple>

using namespace zima;
namespace {
void check(bool value,const char* message){if(!value)throw std::runtime_error(message);}
void inspect_file(const std::filesystem::path& path) {
    std::vector<kernel::BodyResult> bodies;
    const auto part=document::PartDocument::load(path,&bodies);
    const auto& sketch=part.sketches.back();
    workspace::Workspace live;live.add_part(part,bodies,path);live.activate(part.document_id);
    const auto geometry=part.sketch_reference_geometry_for(sketch,
        workspace::sketch_external_reference_source_geometry(part,bodies));
    std::set<std::tuple<std::string,std::string,std::string>> faces;
    for(const auto& face:geometry.triangle_references)faces.emplace(face.owner_id,face.semantic_key,face.instance_path);
    nlohmann::json report=nlohmann::json::array();
    for(const auto& [owner,key,instance]:faces) {
        nlohmann::json row{{"owner",owner},{"key",key}};
        if(const auto* feature=part.find_container(owner))row["name"]=feature->name;
        try {
            const auto ref=workspace::prepare_sketch_external_reference(live,part.document_id,sketch,
                sketcher::ExternalReferenceKind::Face,owner,key,instance);
            auto pending=sketch;pending.add_external_reference(ref);pending.validate();
            row["accepted"]=true;row["infinite"]=ref.infinite;row["paths"]=ref.cached_paths.size();row["points"]=ref.cached_points.size();
        }catch(const std::exception& error){row["accepted"]=false;row["error"]=error.what();}
        report.push_back(std::move(row));
    }
    std::cout<<report.dump(2)<<std::endl;
}
void line_boundaries() {
    using namespace sketcher;
    for(bool reverse:{false,true})for(bool infinite:{false,true})for(auto kind:{ExternalReferenceKind::Edge,ExternalReferenceKind::Face,ExternalReferenceKind::Axis}) {
        if(kind==ExternalReferenceKind::Axis&&!infinite)continue;
        auto sketch=Sketch::create_default();
        const auto curve=sketch.add_segment(-10,4,10,4);
        auto ref=Sketch::create_external_reference(kind);ref.source_document_id="source";
        ref.source_owner_id="original";ref.source_semantic_key="boundary";ref.infinite=infinite;
        ref.cached_points=reverse?std::vector<std::array<double,2>>{{0,8},{0,2}}:std::vector<std::array<double,2>>{{0,2},{0,8}};
        if(kind==ExternalReferenceKind::Face&&!infinite){ref.cached_paths={ref.cached_points};ref.cached_points.clear();}
        sketch.add_external_reference(ref);const auto references=sketch.external_references;
        auto before_trim=sketch;
        auto pieces=sketch_trim_topology(sketch,false);
        check(pieces.size()==2,"External line did not split the native segment");
        check(std::ranges::all_of(pieces,[&](const auto& p){return p.geometry_id==curve;}),"Read-only reference became trimmable");
        const auto cut=nearest_sketch_trim_piece(pieces,{7,4},.1);check(cut.has_value(),"Trim piece not selectable");
        static_cast<void>(apply_sketch_trim(sketch,{*cut}));
        check(sketch.external_references==references,"Trim modified its external source");
        const auto& segment=sketch.segments.front();const auto* end=sketch.find_point(segment.second_point_id);
        check(std::hypot(end->x,end->y-4)<1e-8,"Trim endpoint is not on the independent line equation");
        check(std::ranges::any_of(sketch.constraints,[&](const auto& c){return c.first_point_id==end->id&&c.geometry_id==ref.id&&c.kind==ConstraintKind::PointOnLine;}),"Trim endpoint lost external incidence");
        auto reopened=Sketch::from_serialized(sketch.serialized());check(reopened.external_references==references,"Reopen changed external identity");
        check(reopened.constraints==sketch.constraints,"Reopen changed endpoint incidence");
        auto part=document::PartDocument::create_default();const auto owner=document::PartDocument::create_sketch_container();
        before_trim.owner_container_id=sketch.owner_container_id=owner.id;part.history={owner};part.sketches={before_trim};
        document::BodyHistoryGraph graph;static_cast<void>(graph.create_body("External trim transaction"));graph.insert({document::PartHistoryKind::Feature,owner.id});part.set_body_history(graph);
        document::DocumentSession session(part);auto changed=part;changed.sketches={sketch};session.commit(std::move(changed));
        check(session.undo()&&session.document().sketches.front().serialized()==before_trim.serialized(),"Native trim Undo lost original geometry");
        check(session.redo()&&session.document().sketches.front().serialized()==sketch.serialized(),"Native trim Redo lost external incidence");
        const auto file=std::filesystem::temp_directory_path()/("zima-external-trim-"+sketch.id+".prtz");session.document().save(file);
        check(document::PartDocument::load(file).sketches.front().serialized()==sketch.serialized(),"Native Part reopen changed external trim");
        std::filesystem::remove(file);
    }
}
void circular_boundaries() {
    using namespace sketcher;
    for(bool reverse:{false,true})for(bool source_first:{false,true}) {
        auto sketch=Sketch::create_default();auto ref=Sketch::create_external_reference(ExternalReferenceKind::Edge);
        ref.source_document_id="source";ref.source_owner_id="circle";ref.source_semantic_key="original-circle";
        ref.exact_spline=circular_curve_geometry({0,0,0},5,0,2*std::acos(-1.));
        if(reverse){std::ranges::reverse(ref.exact_spline->poles);kernel::reverse_bspline_parameters(ref.exact_spline->knots,ref.exact_spline->weights);}
        for(int i=0;i<=32;++i){const auto p=kernel::bspline_value(*ref.exact_spline,i/32.);ref.cached_points.push_back({p.x,p.y});}
        if(source_first)sketch.add_external_reference(ref);
        const auto line=sketch.add_segment(-10,0,10,0);
        if(!source_first)sketch.add_external_reference(ref);
        const auto source=sketch.external_references;
        const auto pieces=sketch_trim_topology(sketch,false);check(pieces.size()==3,"Exact external circle did not provide both trim boundaries");
        const auto cut=nearest_sketch_trim_piece(pieces,{7,0},.1);check(cut&&cut->geometry_id==line,"External circular trim piece not selectable");
        static_cast<void>(apply_sketch_trim(sketch,{*cut}));const auto endpoint=sketch.segments.front().second_point_id;
        for(const auto& p:{std::array{3.,4.},std::array{4.,-3.},std::array{5.,0.}}) {
            check(sketch.move_point(endpoint,p[0],p[1]),"Point on external circle cannot be dragged");
            const auto* actual=sketch.find_point(endpoint);check(std::abs(std::hypot(actual->x,actual->y)-5)<1e-8,"Dragged trim endpoint escaped exact external circle");
            check(std::hypot(actual->x-p[0],actual->y-p[1])<1e-7,"A permitted endpoint drag did not move");
            check(sketch.external_references==source,"Endpoint drag modified external source");
        }
        auto reopened=Sketch::from_serialized(sketch.serialized());check(reopened.constraints==sketch.constraints,"Circular trim reopen lost its contact");
    }
    for(bool diameter:{false,true})for(bool reference_dimension:{false,true})for(bool locked:{false,true}) {
        auto sketch=Sketch::create_default();const auto circle=sketch.add_circle(0,0,5);
        auto ref=Sketch::create_external_reference(ExternalReferenceKind::Face);ref.source_document_id="source";ref.source_owner_id="plane";
        ref.source_semantic_key="original-plane";ref.infinite=true;ref.cached_points={{3,-1},{3,1}};sketch.add_external_reference(ref);
        const auto source=sketch.external_references;
        const auto pieces=sketch_trim_topology(sketch,false);const auto cut=nearest_sketch_trim_piece(pieces,{5,0},.1);
        check(cut.has_value(),"External plane did not split native circle");static_cast<void>(apply_sketch_trim(sketch,{*cut}));
        check(sketch.circles.empty()&&sketch.arcs.size()==1&&sketch.bsplines.empty(),"External trim lost native circular geometry");
        const auto arc=sketch.arcs.front();
        for(const auto* axis:{"sketch_axis:x","sketch_axis:y"}){auto d=sketch.create_axis_dimension(arc.center_point_id,axis);d.driving=true;d.locked=true;sketch.apply_dimension(d);}
        auto dimension=diameter?sketch.create_arc_diameter_dimension(arc.id):sketch.create_arc_radius_dimension(arc.id);
        dimension.driving=!reference_dimension;dimension.locked=locked;
        if(reference_dimension&&locked) {
            const auto before=sketch.serialized();bool rejected=false;try{sketch.apply_dimension(dimension);}catch(const std::exception&){rejected=true;}
            check(rejected&&sketch.serialized()==before,"Unsupported locked reference dimension changed the trimmed Sketch");continue;
        }
        sketch.apply_dimension(dimension);
        for(double radius:{5.5,4.5,5.}) {
            const auto points=sketch.points;
            check(sketch.set_dimension_value(dimension.id,radius*(diameter?2:1)),"Successive trimmed R/D edit failed");
            if(reference_dimension)check(sketch.points==points,"Reference R/D edit moved geometry");
            const auto& current=sketch.arcs.front();const auto* center=sketch.find_point(current.center_point_id);
            if(!reference_dimension)check(std::abs(current.radius-radius)<1e-8,"Driving R/D edit did not set actual radius");
            for(const auto& id:{current.start_point_id,current.end_point_id}) {
                const auto* p=sketch.find_point(id);check(std::abs(p->x-3)<1e-8&&std::abs(std::hypot(p->x-center->x,p->y-center->y)-current.radius)<1e-8,"R/D edit broke external plane/circle equations");
            }
            const auto endpoint=current.start_point_id;const auto before_drag=sketch.serialized();const auto point=*sketch.find_point(endpoint);
            if(!reference_dimension) {
                check(!sketch.move_point(endpoint,point.x+1,point.y+1)&&sketch.serialized()==before_drag,
                    "Fully constrained trimmed arc escaped its dimensions on drag");
            } else {
                check(sketch.move_point(endpoint,point.x,point.y+.1),"Underconstrained trimmed arc cannot be dragged on its plane");
                const auto* moved=sketch.find_point(endpoint);check(std::abs(moved->x-3)<1e-8&&std::abs(moved->y-point.y)>.01,
                    "Permitted trimmed-arc drag did not move on the plane");
            }
            check(sketch.external_references==source,"R/D edit changed read-only plane");
            auto again=Sketch::from_serialized(sketch.serialized());check(again.constraints==sketch.constraints,"R/D reopen changed incidence");
        }
    }
    for(bool reverse:{false,true}) {
        auto sketch=Sketch::create_default();const auto curve=sketch.add_segment(-10,0,10,0);
        auto ref=Sketch::create_external_reference(ExternalReferenceKind::Edge);ref.source_document_id="source";ref.source_owner_id="arc";ref.source_semantic_key="finite-arc";
        ref.exact_spline=circular_curve_geometry({0,0,0},5,-.3,.3);
        if(reverse){std::ranges::reverse(ref.exact_spline->poles);kernel::reverse_bspline_parameters(ref.exact_spline->knots,ref.exact_spline->weights);}
        for(int i=0;i<=16;++i){const auto p=kernel::bspline_value(*ref.exact_spline,i/16.);ref.cached_points.push_back({p.x,p.y});}sketch.add_external_reference(ref);
        const auto pieces=sketch_trim_topology(sketch,false);check(pieces.size()==2,"Finite external arc cut on its absent opposite branch");
        const auto cut=nearest_sketch_trim_piece(pieces,{7,0},.1);check(cut&&cut->geometry_id==curve,"Finite arc trim cannot be selected");
        static_cast<void>(apply_sketch_trim(sketch,{*cut}));const auto endpoint=sketch.segments.front().second_point_id;
        for(double angle:{-.1,.1,0.}) {
            check(sketch.move_point(endpoint,5*std::cos(angle),5*std::sin(angle)),"Trim endpoint cannot move on finite arc");const auto* p=sketch.find_point(endpoint);
            check(std::abs(std::hypot(p->x,p->y)-5)<1e-8&&std::abs(std::atan2(p->y,p->x))<=.3+1e-10,"Trim endpoint escaped finite arc domain");
        }
        auto coincident=Sketch::create_default();const auto circle=coincident.add_circle(0,0,5);coincident.add_external_reference(ref);
        const auto unchanged=sketch_trim_topology(coincident,false);check(unchanged.size()==1&&unchanged.front().geometry_id==circle,"Coincident circular support invented trim intersections");
    }
}
}
int main(int argc,char** argv){try {
    if(argc>1){inspect_file(std::filesystem::u8path(argv[1]));return 0;}
    line_boundaries();circular_boundaries();std::cout<<"External read-only trim line/circle boundaries, repeated drag/R/D edits and native type passed\n";return 0;
}catch(const std::exception& error){std::cerr<<error.what()<<std::endl;return 1;}}
