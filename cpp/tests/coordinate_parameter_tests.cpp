#include <zima/workspace/family_operations.hpp>
#include <zima/workspace/relation_operations.hpp>
#include <zima/workspace/engineering_metadata_operations.hpp>
#include <zima/workspace/history_operations.hpp>
#include <cmath>
#include <iostream>
using namespace zima;
namespace {
void check(bool ok,const char* message){if(!ok)throw std::runtime_error(message);}
double number(const document::RelationValue& value){return std::get<double>(value.data);}
void verify(const kernel::OcctKernel& kernel,const std::filesystem::path& directory,bool placed) {
    auto part=document::PartDocument::create_default();part.name="Coordinate parameters";
    auto s=sketcher::Sketch::create_default();s.plane=sketcher::SketchPlane::XZ;s.plane_auto=false;s.refresh_default_frame();
    static_cast<void>(s.add_circle(2,-150,5));const auto center=s.circles.front().center_point_id;
    auto d=s.create_point_line_dimension(center,"sketch_axis:x");s.apply_dimension(d);
    auto feature=document::PartDocument::create_extrusion_container(s.id);s.owner_container_id=feature.id;
    feature.extrusion.length_forward=12;
    if(placed){feature.placement.x=31;feature.placement.y=-17;feature.placement.z=8;
        feature.placement.rotation_x=feature.placement.absolute_rotation_x=35;
        feature.placement.rotation_z=feature.placement.absolute_rotation_z=22;}
    part.history={feature};part.sketches={s};document::BodyHistoryGraph graph;
    static_cast<void>(graph.create_body("Body"));graph.insert({document::PartHistoryKind::Feature,feature.id});part.set_body_history(graph);
    part.resolve_constructions();part.synchronize_dimension_identifiers();
    const auto name=part.dimension_identifiers.identifier(s.id,"dimension:"+d.id);
    check(!name.empty(),"Coordinate has no stable numeric identifier");
    const auto inputs=workspace::relation_inputs(part,{});
    check(number(inputs.at(name).value)==150,"Relations expose native Y instead of Feature Origin Z");
    // Verify against the real published Feature Origin, independently of the numeric helper.
    const auto axes=part.history_origin_reference_geometry_before({});
    const auto z=std::ranges::find_if(axes.axes,[&](const auto& a){return a.reference.owner_id==feature.container_origin.id&&a.reference.semantic_key=="origin:axis:z";});
    check(z!=axes.axes.end(),"Placed Feature Origin Z is unavailable");
    const auto& resolved=part.sketches.front();const auto* point=resolved.find_point(center);const auto world=resolved.world_point(point->x,point->y);
    const double projection=(world.x-z->point.x)*z->direction.x+(world.y-z->point.y)*z->direction.y+(world.z-z->point.z)*z->direction.z;
    check(std::abs(projection-150)<1e-7,"Coordinate does not equal projection onto the displayed Feature Origin Z");
    auto related=part;related.relations=name+" = 175\nmeasured = "+name+"\n";
    workspace::apply_relation_dimensions(related,{});workspace::apply_relation_parameters(related,{});
    check(std::abs(related.sketches.front().find_point(center)->y+175)<1e-7,"Relation assignment moves toward the wrong Origin side");
    for(const auto* unit:{"mm","in"}) {
        auto converted=part;converted.document_units["Length"]=unit;
        const double scale=std::string(unit)=="in"?25.4:1.;
        check(std::abs(number(workspace::relation_inputs(converted,{}).at(name).value)-150/scale)<1e-9,"Relations unit conversion lost the signed coordinate");
    }
    workspace::Workspace live;const auto id=part.document_id;
    const auto file=directory/(placed?"placed.prtz":"local.prtz");
    live.add_part(part,kernel.evaluate_history(part.kernel_operations()),file);
    const auto refs=workspace::family_references(live,id);
    const auto ref=std::ranges::find_if(refs,[&](const auto& r){return r.binding.owner_id==s.id&&r.binding.semantic_key=="dimension:"+d.id;});
    check(ref!=refs.end()&&std::stod(ref->value)==150,"Family catalog exposes the wrong coordinate");
    document::FamilyTable table;table.columns={"Position"};table.bindings["Position"]=ref->binding;
    table.instances={{"Positive",{{"Position","180"}}},{"Negative",{{"Position","-45"}}}};
    static_cast<void>(workspace::set_family_table(live,id,table));
    for(const auto& row:table.instances) {
        const auto member=workspace::open_family_instance(live,kernel,id,row.name,false);
        const auto& model=live.open_part(member)->session.document();
        check(std::abs(model.sketches.front().find_point(center)->y+std::stod(row.values.at("Position")))<1e-7,"Family variant moved to the wrong Origin side");
        const auto& base=live.open_part(id)->session.document();check(base.sketches.front().find_point(center)->y==-150,"Family variant changed its generic");
    }
    const auto* current=live.open_part(id);current->session.document().save(file,current->session.calculated_boundaries());
    std::vector<kernel::BodyResult> cache;auto cold=document::PartDocument::load(file,&cache);
    check(cold.serialized()==current->session.document().serialized(),"Coordinate Family save/reopen changed its native definition");
    for(const auto& [row,packet]:cold.family.evaluated){const auto member=document::PartDocument::from_serialized(*packet);
        const auto value=number(workspace::relation_inputs(member,{}).at(name).value);
        check(value==180||value==-45,"Embedded Family snapshot lost its coordinate convention");}
}
}
int main(){try{kernel::OcctKernel kernel;const auto parent=std::filesystem::canonical(std::filesystem::temp_directory_path());
    const auto directory=parent/("zima-coordinates-"+document::PartDocument::create_default().document_id);std::filesystem::create_directory(directory);
    verify(kernel,directory,false);verify(kernel,directory,true);
    check(directory.parent_path()==parent,"Unsafe test cleanup");std::filesystem::remove_all(directory);
    std::cout<<"Feature Origin projection, Relations and Family variants/units/save/reopen passed\n";return 0;
}catch(const std::exception& e){std::cerr<<e.what()<<'\n';return 1;}}
