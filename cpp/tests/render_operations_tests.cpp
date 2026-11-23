#include "profile_solid_fixture.hpp"
#include <zima/workspace/render_operations.hpp>
#include <zima/kernel/calculation_diagnostics.hpp>
#include <iostream>

using namespace zima;
namespace {
void require(bool value,const char* message) { if(!value)throw std::runtime_error(message); }
template<class F> void rejects(F action) {
    bool rejected=false;try {action();}catch(const std::exception&) {rejected=true;}
    require(rejected,"Invalid Render transaction accepted");
}
void verify_boolean_and_consumed_source() {
    auto part=document::PartDocument::create_default();
    auto first=test::rectangular_feature(part,{20,10,5});
    auto second=test::rectangular_feature(part,{10,6,3});part.history={first,second};
    document::BodyHistoryGraph graph;const auto a=graph.create_body("A");
    graph.insert({document::PartHistoryKind::Feature,first.id});const auto b=graph.create_body("B");
    graph.insert({document::PartHistoryKind::Feature,second.id});
    const auto joined=graph.create_boolean("Union",kernel::BodyCombination::Add,a,b);part.set_body_history(graph);
    kernel::OcctKernel kernel;auto calculated=kernel.evaluate_history(part.kernel_operations());
    const auto source=calculated.back().body_outputs.at(a),result=calculated.back().body_outputs.at(joined);
    workspace::Workspace live;live.add_part(part,std::move(calculated));auto& session=live.open_part(part.document_id)->session;
    for(const auto& id:{a,joined}) {
        const auto edit=workspace::prepare_render_edit(session.document());
        require(workspace::render_source_candidates(session,edit)==std::vector<std::string>{a,b,joined},
            "Render did not offer a consumed Body and its Boolean result");
        document::BodyHistory value;value.scope.id=edit.object_id;value.name="Analysis";value.render=document::RenderDefinition{{id},false};
        const auto count=kernel::body_calculation_count.load();
        require(workspace::commit_render(live,edit,value),"Render source selection failed");
        require(workspace::read_render_sources(session,value.scope.id).front().second.shares_with(id==a?source:result),
            "Render resolved another material history boundary");
        require(kernel::body_calculation_count.load()==count,"Selecting a Boolean or consumed Body recalculated CAD");
    }
    auto measured=session.calculated_boundaries().back();kernel.ensure_body_properties(measured);
    require(std::abs(measured.volume-1000.)<1e-7,"Analyses duplicated Boolean material volume");
}
}
int main() {
    try {
        verify_boolean_and_consumed_source();
        auto part=document::PartDocument::create_default();
        auto feature=test::rectangular_feature(part,{20,10,5});part.history={feature};
        document::BodyHistoryGraph graph;const auto source=graph.create_body("Stock");
        graph.insert({document::PartHistoryKind::Feature,feature.id});graph.activate({});
        const auto empty=graph.create_body("Empty");graph.activate({});part.set_body_history(graph);
        kernel::OcctKernel kernel;auto calculated=kernel.evaluate_history(part.kernel_operations());
        const auto original=calculated.back().body_outputs.at(source);
        workspace::Workspace live;live.add_part(part,std::move(calculated));
        auto& session=live.open_part(part.document_id)->session;
        const auto* shared=&session.calculated_boundaries();
        const auto before=session.revision();const auto generation=session.data_generation();
        const auto calculations=kernel::body_calculation_count.load();
        auto edit=workspace::prepare_render_edit(session.document());
        require(workspace::render_source_candidates(session,edit)==std::vector<std::string>{source},
            "Render offered an empty or invalid material source");
        require(session.revision()==before&&session.data_generation()==generation&&!session.can_undo(),
            "Opening/cancelling Render draft changed the document");
        document::BodyHistory value;value.scope.id=edit.object_id;value.name="Static analysis";
        value.render=document::RenderDefinition{{empty},false};
        rejects([&]{static_cast<void>(workspace::commit_render(live,edit,value));});
        require(session.revision()==before,"Failed Render creation committed history");
        value.render->source_ids.front()=source;
        require(workspace::commit_render(live,edit,value),"Render creation did not commit");
        const auto render=value.scope.id;
        const auto scene_before=*session.document().body_history.find(render)->render;
        auto image_data=std::make_shared<const document::RenderImageData>(document::RenderImageData{"aW1hZ2U=","dGh1bWI="});
        std::vector<document::RenderImage> images{{"image-a","Front","2026-11-23T12:00:00Z",320,200,image_data},
            {"image-b","Back","2026-11-23T12:01:00Z",640,400,image_data}};
        require(workspace::commit_render_images(live,part.document_id,render,images),"Retaining Render images failed");
        require(document::same_render_scene(scene_before,*session.document().body_history.find(render)->render),"Gallery changed render inputs");
        const auto image_revision=session.revision();
        require(!workspace::commit_render_images(live,part.document_id,render,images)&&session.revision()==image_revision,"Unchanged gallery added Undo");
        images[0].name="Renamed";require(workspace::commit_render_images(live,part.document_id,render,images),"Gallery rename failed");
        require(session.document().body_history.find(render)->render->images[0].data==image_data,"Gallery edit copied compressed pixels");
        images.erase(images.begin());require(workspace::commit_render_images(live,part.document_id,render,images),"Gallery delete failed");
        require(session.undo()&&session.document().body_history.find(render)->render->images.size()==2&&
            session.document().body_history.find(render)->render->images[0].data==image_data,"Gallery Undo lost immutable data");
        require(session.redo()&&session.document().body_history.find(render)->render->images.size()==1,"Gallery Redo failed");
        const auto before_bad_gallery=session.revision();images[0].width=0;
        rejects([&]{static_cast<void>(workspace::commit_render_images(live,part.document_id,render,images));});
        require(session.revision()==before_bad_gallery,"Invalid gallery committed");
        require(workspace::commit_render_images(live,part.document_id,render,{}),"Gallery clear failed");
        require(workspace::read_render_sources(session,render).front().second.shares_with(original),"Render source was copied");
        require(&session.calculated_boundaries()==shared,"Render creation replaced calculated CAD");
        const auto revision=session.revision();const auto new_generation=session.data_generation();
        edit=workspace::prepare_render_edit(session.document(),render);
        require(!workspace::commit_render(live,edit,*session.document().body_history.find(render)),"Unchanged Render OK committed");
        require(session.revision()==revision&&session.data_generation()==new_generation,"Unchanged Render changed generation");
        value=*session.document().body_history.find(render);value.render->settings.samples=16;
        require(workspace::commit_render(live,edit,value),"Render controls did not commit");
        rejects([&]{static_cast<void>(workspace::commit_render(live,edit,value));});
        require(session.undo()&&session.document().body_history.find(render)->render->settings.samples==64,
            "Render controls Undo failed");
        require(session.redo()&&session.document().body_history.find(render)->render->settings.samples==16,
            "Render controls Redo failed");
        for(const auto kind:{document::RenderObjectKind::Camera,document::RenderObjectKind::PointLight,document::RenderObjectKind::Sun}) {
            document::RenderObject object;object.kind=kind;object.id="object-"+std::to_string(int(kind));object.name=object.id;
            object.placement.x=30;object.placement.y=40;object.placement.z=50;
            object.placement.references={{"",source+":origin","origin:point",0,false}};
            edit=workspace::prepare_render_edit(session.document(),render);
            require(workspace::commit_render_object(live,edit,object),"Render object creation failed");
            const auto* placed=workspace::find_render_object(session.document(),render,object.id);
            require(placed&&placed->placement.reference_valid&&std::hypot(placed->placement.x,placed->placement.y,placed->placement.z)<1e-8,"Render reference placement did not resolve");
            edit=workspace::prepare_render_edit(session.document(),render);const auto unchanged=session.revision();
            require(!workspace::commit_render_object(live,edit,*placed)&&session.revision()==unchanged,"Unchanged Render object created Undo");
            auto damaged=session.document();auto body=*damaged.body_history.find(render);
            auto& last=body.render->objects.back().placement;last.x=3;last.y=4;last.z=5;
            last.rotation_x=last.absolute_rotation_x=27;last.references={{"","removed","origin:point",0,false}};
            const auto frame=last;damaged.body_history.update_body(body);
            for(int i=0;i<3;++i)damaged.resolve_constructions({});
            const auto broken=workspace::find_render_object(damaged,render,object.id)->placement;
            require(!broken.reference_valid&&broken.x==frame.x&&broken.y==frame.y&&broken.z==frame.z&&broken.rotation_x==frame.rotation_x,"Reference loss moved a Render object");
            session.commit_metadata(damaged);require(session.undo()&&session.redo(),"Broken frame Undo/Redo failed");
            const auto path=std::filesystem::temp_directory_path()/("zima-render-"+part.document_id+".prtz");
            session.document().save(path,session.calculated_boundaries());auto reopened=document::PartDocument::load(path);std::filesystem::remove(path);
            require(workspace::find_render_object(reopened,render,object.id)->placement==broken,"Native reopen changed the retained frame");
            body=*reopened.body_history.find(render);body.render->objects.back().placement.references={{"",source+":origin","origin:point",0,false}};
            reopened.body_history.update_body(body);reopened.resolve_constructions({});
            placed=workspace::find_render_object(reopened,render,object.id);
            require(placed->placement.reference_valid&&std::hypot(placed->placement.x,placed->placement.y,placed->placement.z)<1e-8,"Replacing a Render reference did not repair the frame");
            session.commit_metadata(reopened);edit=workspace::prepare_render_edit(session.document(),render);
            require(workspace::erase_render_object(live,edit,object.id)&&!workspace::find_render_object(session.document(),render,object.id),"Render object deletion failed");
            require(session.undo()&&workspace::find_render_object(session.document(),render,object.id)&&session.redo(),"Render object deletion Undo/Redo failed");
        }
        require(&session.calculated_boundaries()==shared&&workspace::read_render_sources(session,render).front().second.shares_with(original),
            "Render editing/Undo/Redo replaced material data");
        require(kernel::body_calculation_count.load()==calculations,"Render metadata evaluated CAD geometry");
        auto next=session.document();auto suppressed=next.body_history;auto source_body=*suppressed.find(source);
        source_body.suppressed=true;suppressed.update_body(source_body);next.set_body_history(suppressed);
        session.commit_metadata(std::move(next));
        rejects([&]{static_cast<void>(workspace::read_render_sources(session,render));});
        require(session.undo(),"Source suppression Undo failed");
        require(workspace::read_render_sources(session,render).front().second.shares_with(original),"Source did not return after Undo");
        auto stale=session.calculated_boundaries();stale.back().body_outputs.erase(source);
        session.update_calculated_boundaries(std::move(stale));
        rejects([&]{static_cast<void>(workspace::read_render_sources(session,render));});
        std::cout<<"Render draft, source selection, shared snapshot, metadata commit, stale draft and Undo/Redo passed\n";
        return 0;
    } catch(const std::exception& error) {std::cerr<<error.what()<<'\n';return 1;}
}
