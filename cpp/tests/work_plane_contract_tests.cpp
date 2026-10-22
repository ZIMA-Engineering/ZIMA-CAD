#include "profile_command_fixture.hpp"
#include "profile_solid_fixture.hpp"
#include <zima/command_host/host.hpp>
#include <zima/workspace/sketch_operations.hpp>
#include <zima/document/holes.hpp>
#include <zima/document/sketch_placement.hpp>
#include <zima/document/body_origin_attachment.hpp>
#include <zima/document/placement_reference_assignment.hpp>
#include <zima/workspace/model_calculation.hpp>
#include <cmath>
#include <fstream>
#include <iostream>
#include <numbers>
using namespace zima;
using commands::Json;
namespace fs=std::filesystem;
namespace {
void check(bool ok,const char* text){if(!ok)throw std::runtime_error(text);}
void near(double a,double b){if(!std::isfinite(a)||std::abs(a-b)>1e-5)throw std::runtime_error("Expected "+std::to_string(b)+", got "+std::to_string(a));}
Json run(command_host::Host& host,const std::string& name,Json args=Json::object()) {
    auto result=host.execute({{"command",name},{"arguments",std::move(args)}});
    if(!result.ok)throw std::runtime_error(name+": "+result.code+": "+result.message);
    return result.data;
}
double dot(kernel::Vec3 a,kernel::Vec3 b){return a.x*b.x+a.y*b.y+a.z*b.z;}
void first_face_xy(kernel::OcctKernel& kernel,const fs::path& directory) {
    unsigned cases=0;
    for(const auto* plane:{"xy","xz","yz"})for(bool flip:{false,true})for(bool back:{false,true})for(unsigned turn=0;turn<4;++turn) {
        auto doc=document::PartDocument::create_default();auto sketch=sketcher::Sketch::create_default();
        static_cast<void>(sketch.add_circle(12,9,3));auto feature=document::PartDocument::create_extrusion_container(sketch.id);
        sketch.owner_container_id=feature.id;feature.extrusion.length_forward=7;
        document::ConstructionReference face;face.owner_id=doc.document_id+":origin";face.semantic_key=std::string("origin:plane:")+plane;
        face.supports_offset=true;face.flip=flip;face.offset=std::array{0.,-0.,3.,-3.}[turn];
        document::prepare_profile_plane_reference(0,face,{});
        std::vector<document::ConstructionReference> position,orientation;std::array<bool,3> locks{};
        check(document::assign_placement_reference({position,orientation,locks},true,0,face).error==document::PlacementReferenceError::None,"TOP first plane assignment failed");
        feature.placement.references=document::combined_placement_references(position,orientation);
        const auto authored=feature.placement.references;document::normalize_container_front_references(feature.placement.references);
        check(feature.placement.references==authored&&std::signbit(feature.placement.references.front().offset)==std::signbit(face.offset),"Normalization changed an authored axis or signed offset");
        feature.placement.orientation_back=back;feature.placement.orientation_quarter_turns=turn;
        doc.history={feature};doc.sketches={sketch};document::BodyHistoryGraph graph;graph.create_body("XY first face");graph.insert({document::PartHistoryKind::Feature,feature.id});doc.set_body_history(graph);
        doc.resolve_constructions();const auto& resolved=doc.sketches.front();const auto& placed=doc.history.front().placement;
        check(resolved.plane==sketcher::SketchPlane::XY&&placed.reference_valid,"First face did not resolve to local XY");
        const kernel::Vec3 rotation{placed.rotation_x,placed.rotation_y,placed.rotation_z};
        near(dot(resolved.resolved_x_axis,document::construction_direction_from_local_axis("x",rotation)),1);
        near(dot(resolved.resolved_y_axis,document::construction_direction_from_local_axis("y",rotation)),1);
        near(dot(resolved.resolved_normal,document::construction_direction_from_local_axis("z",rotation)),1);
        const auto result=kernel.evaluate_history(doc.kernel_operations());check(!result.empty()&&result.back().calculation_errors.empty(),"First-face XY extrusion failed");
        near(result.back().volume,std::numbers::pi*9*7);
        const auto path=directory/("first-face-xy-"+std::to_string(cases++)+".prtz");doc.save(path,result);
        auto reopened=document::PartDocument::load(path);reopened.resolve_constructions();
        check(reopened.history.front().placement.references==placed.references&&reopened.sketches.front().serialized()==resolved.serialized(),"First-face XY changed after native reopen");
        // An explicitly authored FRONT still means local Y. It must never be
        // silently converted to TOP when existing geometry is opened/edited.
        auto front=authored;for(auto& ref:front)ref.orientation_role="front";
        const auto before=front;document::normalize_container_front_references(front);check(front==before,"Explicit FRONT was reinterpreted as TOP");
    }
    std::cout<<"First face: "<<cases<<" XY/XZ/YZ supports, sides, camera-independent back/quarter poses, offsets, calculated volume and native reopen passed\n";
}
void origin_frames() {
    const auto canonical=document::PartDocument::create_default();
    const auto geometry=canonical.origin_viewer_mesh().original_references;
    auto refs=document::body_origin_attachment(canonical.document_id).references;
    check(document::placement_references_use_whole_origin(refs),"Complete Origin not recognized");
    check(!document::sketch_placement_uses_front_plane(refs),"Complete Origin forced XZ");
    for(unsigned slot=0;slot<3;++slot) {
        auto partial=refs;partial.erase(partial.begin()+slot);
        check(!document::placement_references_use_whole_origin(partial),"Partial Origin mistaken for complete frame");
        auto other=refs;other[slot].instance_path="another-occurrence";
        check(!document::placement_references_use_whole_origin(other),"Distinct Origin occurrences merged");
        other=refs;other[slot].owner_id="another-origin";
        check(!document::placement_references_use_whole_origin(other),"Distinct Origins merged");
    }
    for(const auto plane:{sketcher::SketchPlane::XY,sketcher::SketchPlane::XZ,sketcher::SketchPlane::YZ})
    for(const bool automatic:{false,true})for(const bool flipped:{false,true}) {
        auto doc=canonical;auto feature=document::PartDocument::create_sketch_container();
        feature.placement.references=refs;
        for(auto& ref:feature.placement.references)if(ref.semantic_key=="origin:plane:xz")ref.flip=flipped;
        feature.placement.rotation_offset_z=17.;
        auto sketch=sketcher::Sketch::create_default();sketch.owner_container_id=feature.id;
        sketch.plane=plane;sketch.plane_auto=automatic;sketch.plane_offset=3.;
        static_cast<void>(sketch.add_rectangle(0,0,10,20));
        doc.history={feature};doc.sketches={sketch};doc.resolve_constructions(geometry);
        const auto& resolved=doc.sketches.front();
        check(resolved.plane==plane,"Whole Origin rewrote a persisted work plane");
        const auto axes=document::sketch_container_frame_axes(resolved);
        const auto& p=doc.history.front().placement;
        const kernel::Vec3 rotation{p.rotation_x,p.rotation_y,p.rotation_z};
        for(unsigned i=0;i<3;++i) {
            const auto axis=document::construction_direction_from_local_axis(std::array<std::string,3>{"x","y","z"}[i],rotation);
            near(dot(axes[i],axis),1.);
        }
        const auto roundtrip=document::PartDocument::from_serialized(doc.serialized());
        check(roundtrip.history.front().placement==p&&roundtrip.sketches.front().plane==plane,
            "Whole Origin lost manual rotation, sides or plane in persistence");
        near(dot(resolved.resolved_normal,resolved.resolved_x_axis),0.);
        near(dot(resolved.resolved_normal,resolved.resolved_y_axis),0.);
        near(dot(resolved.resolved_origin,resolved.resolved_normal),3.);
    }
    std::cout<<"Whole Origin: 3 local planes x auto/manual x both sides; exact frame, correction, offset and persistence PASS\n";
    for(const auto plane:{sketcher::SketchPlane::XY,sketcher::SketchPlane::XZ,sketcher::SketchPlane::YZ})
    for(int turns=0;turns<4;++turns) {
        auto doc=canonical;auto feature=document::PartDocument::create_sketch_container();
        feature.placement.references=refs;feature.placement.orientation_quarter_turns=turns;
        auto sketch=sketcher::Sketch::create_default();sketch.owner_container_id=feature.id;
        sketch.plane=plane;sketch.plane_auto=true;doc.history={feature};doc.sketches={sketch};
        doc.resolve_constructions(geometry);
        const auto axes=document::sketch_container_frame_axes(doc.sketches.front());
        const kernel::Vec3 rotation=plane==sketcher::SketchPlane::XZ?kernel::Vec3{0.,90.*turns,0.}:
            kernel::Vec3{0.,0.,90.*turns};
        for(unsigned i=0;i<3;++i)near(dot(axes[i],document::construction_direction_from_local_axis(
            std::array<std::string,3>{"x","y","z"}[i],rotation)),1.);
    }
    std::cout<<"Whole Origin: all quarter turns preserve authored XZ profiles and new XY frames PASS\n";
}
void verify(kernel::OcctKernel& kernel,fs::path directory,const std::string& kind) {
    auto doc=document::PartDocument::create_default();const auto id=doc.document_id;
    if(kind=="holes") {auto box=zima::test::rectangular_feature(doc,{40,40,40});doc.history.push_back(box);}
    auto container=document::PartDocument::create_sketch_container();auto sketch=sketcher::Sketch::create_default();
    sketch.owner_container_id=container.id;
    std::string axis;
    if(kind=="holes")static_cast<void>(sketch.add_segment(-20,0,20,0));
    else if(kind=="revolution") {
        static_cast<void>(sketch.add_rectangle(2,-3,4,3));
        axis=sketch.add_segment(0,-5,0,5);sketch.segments.back().construction=true;sketch.segments.back().centerline=true;
    } else static_cast<void>(sketch.add_circle(0,0,2));
    doc.history.push_back(container);doc.sketches.push_back(sketch);
    auto calculated=kernel.evaluate_history(doc.kernel_operations());
    workspace::Workspace live;const auto file=directory/(kind+".prtz");live.add_part(doc,std::move(calculated),file);live.activate(id);
    command_host::Host host(live,kernel,directory);
    const auto current=[&]{return workspace::document_sketch(live,id,sketch.id);};
    const auto reference=[&](const char* plane) {
        if(live.open_part(id)->session.document().find_container(container.id)->feature_kind==document::FeatureKind::Feature)
            run(host,"placement.reference.set",{{"object",container.id},{"index",0},{"reference",{{"owner",id+":origin"},{"key",plane}}}});
        else run(host,"sketch.reference.set",{{"sketch",sketch.id},{"index",0},{"reference",{{"owner",id+":origin"},{"key",plane}}}});
    };
    reference("origin:plane:xy");
    check(current().plane_auto,"New Sketch should follow first plane");near(std::abs(current().resolved_normal.z),1);
    if(kind=="holes")run(host,"holes.create",{{"sketch",sketch.id},{"diameter_mm",4}});
    if(kind=="extrusion")run(host,"feature.create",{{"sketch",sketch.id},{"parameters",zima::test::feature_profile_parameters(5)}});
    if(kind=="revolution")run(host,"feature.create",{{"sketch",sketch.id},{"parameters",zima::test::feature_profile_parameters(360,false,axis)}});
    const bool profile=kind=="extrusion"||kind=="revolution";
    const auto set=[&](const char* plane) {
        if(profile)run(host,"feature.set",{{"container",container.id},{"profile_plane",plane},{"parameters",{{"profile_plane_offset",3}}}});
        else run(host,"sketch.set",{{"sketch",sketch.id},{"plane",plane},{"plane_offset_mm",3}});
    };
    const auto automatic=current();set("XY");const auto manual=current();
    check(!manual.plane_auto&&manual.plane==sketcher::SketchPlane::XY,"Manual plane lost");
    near(dot(manual.resolved_normal,automatic.resolved_normal),0);
    near(dot(manual.resolved_origin,manual.resolved_normal),3);
    run(host,"undo");check(current().plane_auto,"Undo did not restore automatic plane");run(host,"redo");check(!current().plane_auto,"Redo did not restore manual plane");
    reference("origin:plane:yz");check(!current().plane_auto&&current().plane==sketcher::SketchPlane::XY,"Replacing reference overwrote manual plane");
    const auto saved=current();run(host,"regenerate");near(dot(current().resolved_normal,saved.resolved_normal),1);
    run(host,"save");std::vector<kernel::BodyResult> cache;auto loaded=document::PartDocument::load(file,&cache);
    const auto found=std::ranges::find(loaded.sketches,sketch.id,&sketcher::Sketch::id);
    check(found!=loaded.sketches.end()&&!found->plane_auto&&found->plane==sketcher::SketchPlane::XY,"Native data lost manual plane");
    loaded.resolve_constructions();near(dot(std::ranges::find(loaded.sketches,sketch.id,&sketcher::Sketch::id)->resolved_normal,saved.resolved_normal),1);
    if(kind!="sketch") {
        const double expected=kind=="holes"?64000-std::numbers::pi*4*40:kind=="extrusion"?std::numbers::pi*4*5:std::numbers::pi*12*6;
        near(cache.back().volume,expected);near(kernel.evaluate_history(loaded.kernel_operations()).back().volume,expected);
    }
    set("AUTO");check(current().plane_auto,"AUTO was not restored");near(std::abs(current().resolved_normal.x),1);
}
}
int main(int argc,char** argv){try {
    if(argc==3&&std::string_view(argv[1])=="--verify-library-frames") {
        std::ifstream files(argv[2]);check(bool(files),"Library frame inventory cannot open");
        std::string file;unsigned failures=0,total=0;
        while(std::getline(files,file)) {
            if(file.empty())continue;
            if(file.back()=='\r')file.pop_back();
            Json result={{"path",file},{"ok",false}};++total;
            try {
                std::vector<kernel::BodyResult> cache;
                auto part=document::PartDocument::load(fs::u8path(file),&cache);
                const auto before=part;
                part.resolve_constructions(workspace::construction_reference_source_geometry(cache));
                const auto vector_near=[](kernel::Vec3 a,kernel::Vec3 b){near(a.x,b.x);near(a.y,b.y);near(a.z,b.z);};
                const auto same_geometry=[&](const auto& self,const Json& a,const Json& b,const std::string& location)->void {
                    const auto require=[&](bool same){if(!same)throw std::runtime_error("Resolving changed Sketch data at "+location);};
                    if(a.is_number()&&b.is_number()) {
                        if(a.is_number_integer()&&b.is_number_integer())require(a==b);
                        else {
                            const double x=a.template get<double>(),y=b.template get<double>();
                            require(std::isfinite(x)&&std::isfinite(y)&&std::abs(x-y)<=1e-5);
                            if(x==0&&y==0&&(location.ends_with("/value")||location.ends_with("/offset")||location.ends_with("/plane_offset")))
                                require(std::signbit(x)==std::signbit(y));
                        }
                    } else if(a.is_object()&&b.is_object()) {
                        require(a.size()==b.size());
                        for(const auto& [key,value]:a.items()) {require(b.contains(key));self(self,value,b.at(key),location+"/"+key);}
                    } else if(a.is_array()&&b.is_array()) {
                        require(a.size()==b.size());
                        for(std::size_t i=0;i<a.size();++i)self(self,a[i],b[i],location+"/"+std::to_string(i));
                    } else require(a==b);
                };
                const auto placement_near=[&](const document::Placement& a,const document::Placement& b) {
                    check(a.references==b.references,"Resolving changed persisted placement references");
                    for(std::size_t i=0;i<a.references.size();++i)
                        check(std::signbit(a.references[i].offset)==std::signbit(b.references[i].offset),"Resolving changed a reference's signed zero side");
                    check(a.orientation_back==b.orientation_back&&a.orientation_quarter_turns==b.orientation_quarter_turns,
                        "Resolving changed an authored view orientation");
                    vector_near({a.x,a.y,a.z},{b.x,b.y,b.z});
                    const kernel::Vec3 ar{a.rotation_x,a.rotation_y,a.rotation_z},br{b.rotation_x,b.rotation_y,b.rotation_z};
                    for(const auto* axis:{"x","y","z"})vector_near(document::construction_direction_from_local_axis(axis,ar),
                        document::construction_direction_from_local_axis(axis,br));
                };
                check(part.body_history.bodies().size()==before.body_history.bodies().size(),"Resolving changed the Body inventory");
                for(const auto& body:part.body_history.bodies()) {
                    const auto* original=before.body_history.find(body.scope.id);check(original,"Resolving replaced a Body identity");
                    placement_near(body.scope.placement,original->scope.placement);
                }
                check(part.sketches.size()==before.sketches.size(),"Resolving changed the Sketch inventory");
                for(const auto& sketch:part.sketches) {
                    const auto original=std::ranges::find(before.sketches,sketch.id,&sketcher::Sketch::id);
                    check(original!=before.sketches.end(),"Resolving replaced a Sketch identity");
                    check(sketch.plane==original->plane&&sketch.plane_auto==original->plane_auto,"Resolving changed an authored work plane");
                    vector_near(sketch.resolved_origin,original->resolved_origin);
                    vector_near(sketch.resolved_x_axis,original->resolved_x_axis);
                    vector_near(sketch.resolved_y_axis,original->resolved_y_axis);
                    vector_near(sketch.resolved_normal,original->resolved_normal);
                    // Frames alone cannot detect a rebuilt attachment profile
                    // changing its points, curves, dimensions or source identity.
                    auto a=Json::parse(sketch.serialized()),b=Json::parse(original->serialized());
                    for(const auto* key:{"resolved_origin","resolved_x_axis","resolved_y_axis","resolved_normal"}){a.erase(key);b.erase(key);}
                    same_geometry(same_geometry,a,b,sketch.id);
                }
                for(const auto& feature:part.history) {
                    const auto* original=before.find_container(feature.id);check(original,"Resolving replaced a Feature identity");
                    placement_near(feature.placement,original->placement);
                }
                result["ok"]=true;result["sketches"]=part.sketches.size();
            } catch(const std::exception& error) {++failures;result["error"]=error.what();}
            std::cout<<result.dump()<<std::endl;
        }
        std::cerr<<"Library frame verification: "<<(total-failures)<<'/'<<total<<" passed\n";
        return failures?1:0;
    }
    origin_frames();
    const auto directory=fs::temp_directory_path()/("zima-work-planes-"+document::PartDocument::create_default().document_id);fs::create_directories(directory);
    kernel::OcctKernel kernel;first_face_xy(kernel,directory);for(const auto& kind:{"sketch","holes","extrusion","revolution"})verify(kernel,directory,kind);
    fs::remove_all(directory);std::cout<<"Work-plane defaults, overrides, references, geometry and native history passed\n";return 0;
}catch(const std::exception& e){std::cerr<<e.what()<<'\n';return 1;}}
