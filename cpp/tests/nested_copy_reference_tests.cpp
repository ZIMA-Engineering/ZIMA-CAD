#include "profile_solid_fixture.hpp"
#include <zima/command_host/host.hpp>
#include <zima/workspace/reference_sources.hpp>
#include <cmath>
#include <iostream>
#include <numbers>
using namespace zima;namespace fs=std::filesystem;using commands::Json;
namespace {
void require(bool value,const char* message){if(!value)throw std::runtime_error(message);}
void near(double a,double b){if(std::abs(a-b)>1e-7)throw std::runtime_error("Expected "+std::to_string(b)+", got "+std::to_string(a));}
commands::Result run(command_host::Host& host,const std::string& command,Json args=Json::object()) {
    auto r=host.execute({{"command",command},{"arguments",std::move(args)}});if(!r.ok)throw std::runtime_error(r.code+": "+r.message);return r;
}
struct Copy {std::string label,id;unsigned count{1};};
struct Bounds {kernel::Vec3 lo{1e30,1e30,1e30},hi{-1e30,-1e30,-1e30};};
Bounds bounds(const kernel::ViewerReferenceGeometry& data,const std::string& root) {
    Bounds result;const auto prefix=assembly::InstancePath{}.child(root).encoded();
    for(std::size_t i=0;i<data.triangle_references.size();++i)if(data.triangle_references[i].instance_path.starts_with(prefix))for(unsigned j=0;j<3;++j) {
        const auto p=data.vertices.at(data.triangles.at(i*3+j));
        result.lo={std::min(result.lo.x,p.x),std::min(result.lo.y,p.y),std::min(result.lo.z,p.z)};
        result.hi={std::max(result.hi.x,p.x),std::max(result.hi.y,p.y),std::max(result.hi.z,p.z)};
    }
    return result;
}
void check_geometry(const workspace::Workspace& live,const std::string& top,const kernel::ViewerReferenceGeometry& data,
    const std::string& root,bool local) {
    const auto prefix=assembly::InstancePath{}.child(root).encoded();double maximum=0;std::size_t planes=0,cylinders=0;
    for(std::size_t i=0;i<data.triangle_references.size();++i) {
        const auto& face=data.triangle_references[i];if(!face.instance_path.starts_with(prefix)||!face.surface)continue;
        const auto& surface=*face.surface;const auto path=assembly::InstancePath::decode(face.instance_path);
        const auto origin=local?surface.origin:live.occurrence_point_to_scene(top,path,surface.origin);
        const auto axis=local?surface.axis:live.occurrence_direction_to_scene(top,path,surface.axis);
        near(axis.x*axis.x+axis.y*axis.y+axis.z*axis.z,1);
        for(unsigned j=0;j<3;++j) {
            const auto p=data.vertices.at(data.triangles.at(i*3+j));const kernel::Vec3 v{p.x-origin.x,p.y-origin.y,p.z-origin.z};
            const double axial=v.x*axis.x+v.y*axis.y+v.z*axis.z;double error=0;
            if(surface.kind==kernel::SurfaceGeometry::Kind::Plane){error=std::abs(axial);++planes;}
            else if(surface.kind==kernel::SurfaceGeometry::Kind::Cylinder) {
                error=std::abs(std::hypot(v.x-axial*axis.x,v.y-axial*axis.y,v.z-axial*axis.z)-surface.radius);++cylinders;
            }
            maximum=std::max(maximum,error);
        }
    }
    require(planes>0&&cylinders>0,"Plane and cylinder references were not both checked");
    if(maximum>1e-7)throw std::runtime_error("Analytic surface/triangle disagreement: "+std::to_string(maximum)+" mm");
    std::cout<<(local?"context":"scene")<<" max analytic surface/triangle error "<<maximum<<" mm\n";
}
void verify(const fs::path& dir) {
    kernel::OcctKernel kernel;
    auto part=document::PartDocument::create_default();auto box=zima::test::rectangular_feature(part,{10,12,14});
    auto cylinder=zima::test::circular_feature(part,3,18);cylinder.placement.x=18;
    part.history={box,cylinder};document::BodyHistoryGraph graph;static_cast<void>(graph.create_body("Box"));graph.insert({document::PartHistoryKind::Feature,box.id});
    static_cast<void>(graph.create_body("Cylinder"));graph.insert({document::PartHistoryKind::Feature,cylinder.id});part.set_body_history(std::move(graph));
    const auto bodies=kernel.evaluate_history(part.kernel_operations());part.save(dir/"source.prtz",bodies);
    const double volume=10*12*14+std::numbers::pi*3*3*18;near(bodies.back().volume,volume);
    auto inner=assembly::AssemblyDocument::create_default();auto leaf=assembly::AssemblyDocument::create_part_occurrence("Leaf",part.document_id,"source.prtz",bodies.back());
    leaf.placement={10,20,5,15,20,70};inner.components={leaf};inner.save(dir/"inner.asmz");
    auto top=assembly::AssemblyDocument::create_default();auto group=assembly::AssemblyDocument::create_assembly_occurrence("Group",inner.document_id,"inner.asmz",inner);
    group.placement={25,-15,7,10,20,30};top.components={group};
    auto target=document::PartDocument::create_default();target.save(dir/"target.prtz",{});
    auto target_occurrence=assembly::AssemblyDocument::create_part_occurrence("Target",target.document_id,"target.prtz",{});
    target_occurrence.placement={2,3,4,5,6,7};top.components.push_back(target_occurrence);const auto target_path=assembly::InstancePath{}.child(target_occurrence.occurrence_id);
    workspace::Workspace live;live.add_part(part,bodies,dir/"source.prtz");live.add_part(target,{},dir/"target.prtz");live.add_assembly(inner,dir/"inner.asmz");live.add_assembly(top,dir/"top.asmz");live.activate(top.document_id);live.display_top_level(top.document_id);
    auto working_directory=dir;command_host::Host host(live,kernel,working_directory);std::vector<Copy> copies{{"source",group.occurrence_id}};
    const auto mirror=[&](const std::string& source,const char* plane,const char* label,unsigned count=1) {
        const auto id=run(host,"mirror.create",{{"source",source},{"local_plane",plane}}).data.at("object").get<std::string>();copies.push_back({label,id,count});return id;
    };
    const auto yz=mirror(group.occurrence_id,"yz","mirror yz");mirror(group.occurrence_id,"xz","mirror xz");mirror(group.occurrence_id,"xy","mirror xy");
    const auto twice=mirror(yz,"yz","double mirror");
    const auto ring=run(host,"pattern.create",{{"source",group.occurrence_id},{"mode","circular"},{"count",3}}).data.at("object").get<std::string>();copies.push_back({"circular pattern",ring,2});
    const auto grid=run(host,"pattern.create",{{"source",group.occurrence_id},{"linear",Json::array({{{"axis","y"},{"spacing_mm",26},{"count",3}}})}}).data.at("object").get<std::string>();copies.push_back({"linear pattern",grid,2});
    mirror(ring,"xz","mirror of pattern",2);
    const auto nested=run(host,"pattern.create",{{"source",ring},{"mode","circular"},{"count",3}}).data.at("object").get<std::string>();copies.push_back({"pattern of pattern",nested,4});
    const auto check_all=[&](const workspace::Workspace& workspace) {
        const auto* owner=workspace.open_assembly(top.document_id);const auto revision=owner->session.revision(),generation=owner->session.data_generation();
        const auto scene=owner->session.document().build_scene();const auto& data=scene.original_references;
        for(const auto& copy:copies) {
            std::cout<<copy.label<<": ";check_geometry(workspace,top.document_id,data,copy.id,false);
            near(owner->session.document().find_occurrence(copy.id)->calculated_source->volume,volume*copy.count);
            if(copy.id!=group.occurrence_id) {
                const auto prefix=assembly::InstancePath{}.child(copy.id).encoded();
                const auto local=workspace::context_original_reference_geometry(workspace,top.document_id,target_path,part.document_id,
                    [&](auto kind,const auto&,const auto&,const auto& path){return kind==workspace::OriginalReferenceKind::Face&&path.starts_with(prefix);});
                check_geometry(workspace,top.document_id,local,copy.id,true);
            }
        }
        const auto a=bounds(data,group.occurrence_id),b=bounds(data,yz),c=bounds(data,twice),linear=bounds(data,grid);
        near(b.lo.x,-a.hi.x);near(b.hi.x,-a.lo.x);near(b.lo.y,a.lo.y);near(b.hi.z,a.hi.z);
        near(c.lo.x,a.lo.x);near(c.hi.x,a.hi.x);near(c.lo.y,a.lo.y);near(c.hi.y,a.hi.y);near(c.lo.z,a.lo.z);near(c.hi.z,a.hi.z);
        near(linear.lo.x,a.lo.x);near(linear.lo.y,a.lo.y+26);near(linear.hi.y,a.hi.y+52);
        require(owner->session.revision()==revision&&owner->session.data_generation()==generation,"Reading copy references mutated Assembly state");
    };
    check_all(live);
    const auto& ring_snapshot=live.open_assembly(top.document_id)->session.document().find_occurrence(ring)->nested_snapshot;
    const auto virtual_path=assembly::InstancePath{}.child(ring).child(ring_snapshot.front().occurrence_id);
    const auto virtual_owner=live.resolve_occurrence(top.document_id,virtual_path),leaf_owner=live.resolve_occurrence(top.document_id,virtual_path.child(leaf.occurrence_id));
    require(virtual_owner&&virtual_owner->owner_assembly_document_id==top.document_id&&virtual_owner->source_document_id==inner.document_id&&
        leaf_owner&&leaf_owner->owner_assembly_document_id==inner.document_id,"Pattern virtual nodes reported their source as their owning Assembly");
    const auto before=live.open_assembly(top.document_id)->session.revision();
    const auto scene=live.open_assembly(top.document_id)->session.document().build_scene();const auto prefix=assembly::InstancePath{}.child(nested).encoded();
    const auto face=std::ranges::find_if(scene.original_references.triangle_references,[&](const auto& f){return f.instance_path.starts_with(prefix)&&f.surface;});
    require(face!=scene.original_references.triangle_references.end(),"Nested Pattern reference is missing");
    run(host,"reference.get",{{"kind","face"},{"owner",face->owner_id},{"key",face->semantic_key},{"instance_path",face->instance_path}});
    require(live.open_assembly(top.document_id)->session.revision()==before,"Copy reference query created history");
    run(host,"component.activate",{{"instance_path",face->instance_path}});
    require(live.active_document_id()==part.document_id&&live.active_occurrence_path()==assembly::InstancePath{{group.occurrence_id,leaf.occurrence_id}}.encoded()&&
        live.displayed_document_id()==top.document_id,"Nested Pattern activation lost the canonical source context");
    run(host,"component.deactivate");
    require(live.open_part(part.document_id)->session.revision()==0&&live.open_part(part.document_id)->session.calculated_boundaries().back().mesh.vertices==bodies.back().mesh.vertices,
        "Copy calculation modified the source Part");
    run(host,"save");workspace::Workspace reopened;reopened.add_assembly(assembly::AssemblyDocument::load(dir/"top.asmz"),dir/"top.asmz");check_all(reopened);
}
}
int main(){try{const auto parent=fs::canonical(fs::temp_directory_path());const auto dir=parent/("zima-copy-reference-"+document::PartDocument::create_default().document_id);
    require(fs::create_directory(dir),"Cannot create test folder");verify(dir);require(dir.parent_path()==parent,"Unsafe cleanup");fs::remove_all(dir);return 0;
}catch(const std::exception& e){std::cerr<<e.what()<<'\n';return 1;}}
