#include <zima/command_host/host.hpp>
#include <zima/workspace/model_calculation.hpp>
#include <zima/document/document_session.hpp>
#include <zima/document/flat.hpp>
#include <zima/workspace/sheet_state_operations.hpp>
#include <zima/workspace/drawing_sources.hpp>
#include <zima/drawing/measurement_dimension.hpp>
#include <chrono>
#include <iostream>
using namespace zima;
using commands::Json;
namespace {
void check(bool value,const char* message){if(!value)throw std::runtime_error(message);}
commands::Result run(command_host::Host& host,const char* command,Json arguments=Json::object()) {
    auto result=host.execute({{"command",command},{"arguments",std::move(arguments)}});
    if(!result.ok)throw std::runtime_error(std::string(command)+": "+result.message);
    return result;
}
}
int main() {
    auto directory=std::filesystem::temp_directory_path()/("zima-sheet-state-"+std::to_string(std::chrono::steady_clock::now().time_since_epoch().count()));
    std::filesystem::create_directory(directory);
    try {
        kernel::OcctKernel kernel;workspace::Workspace live;command_host::Options options;
        options.settings=[] {command_host::Settings s;s.templates={std::filesystem::absolute("config/templates"),"start_part.prtz","start_assembly.asmz","Body"};return s;};
        command_host::Host host(live,kernel,directory,options);
        run(host,"new",{{"type","part"},{"name","sheet-state"}});
        const auto bend=run(host,"bend.create",{{"width_mm",40.},{"radius_mm",8.},{"angle_degrees",90.},{"thickness_mm",2.},{"thickness_override",true}}).data.at("container").get<std::string>();
        const auto id=live.active_document_id();auto* part=live.open_part(id);
        const auto before=part->session.document();const auto original=part->session.calculated_boundaries().back();
        const auto unfold=run(host,"unbend.create",{{"owners",Json::array({bend})}}).data.at("container").get<std::string>();
        {
            using namespace kernel::sheet_material;
            const auto& mesh=part->session.calculated_boundaries().back().mesh;
            const auto axis=std::ranges::find_if(mesh.axes,[](const auto& a){return is_bend_line(a.reference);});
            check(axis!=mesh.axes.end(),"Calculated Unbend has no visible bend axis.");
            auto drawing_view=drawing::DrawingDocument::create_view(id,{},mesh,drawing::ViewOrientation::Top);
            const auto frame=*part->session.document().kernel_operations().front().sheet_material;
            drawing_view.camera={frame.along,frame.tangent,cross(frame.along,frame.tangent)};
            drawing_view.projected_edges=drawing::project_edges(mesh,drawing_view.camera);
            drawing::refresh_model_annotations(drawing_view,workspace::drawing_annotation_sources(&live,id,{}));
            const auto annotation=std::ranges::find_if(drawing_view.model_annotations,[&](const auto& a){return a.source.owner_id==axis->reference.owner_id&&a.source.semantic_id==axis->reference.semantic_key;});
            check(annotation!=drawing_view.model_annotations.end()&&annotation->model_axis,"Drawing Show/Erase lost the bend axis.");
            annotation->visible=true;
            const drawing::Point2 cursor{dot(axis->point,frame.along)+3,dot(axis->point,frame.tangent)};
            drawing::MeasurementPickRequest request;request.lines_only=true;
            const auto candidates=drawing::measurement_candidates(drawing_view,cursor,.2,request);
            const auto picked=std::ranges::find_if(candidates,[&](const auto& candidate){return candidate.attachment.reference.semantic_key==axis->reference.semantic_key;});
            check(picked!=candidates.end(),"Drawing dimension picker cannot select the bend line.");
            check(drawing::resolve_dimension_attachment(drawing_view,picked->attachment).has_value(),"Bend-line dimension reference cannot resolve.");
            const auto start=point(frame,{20,0,-frame.thickness});
            const auto cap=drawing::measurement_candidates(drawing_view,{dot(start,frame.along)+3,dot(start,frame.tangent)},.2,request);
            check(!cap.empty(),"Drawing has no start edge to dimension against the bend line.");
            auto dimension=drawing::make_drawing_dimension(drawing_view.id);
            dimension.attachments={cap.front().attachment,picked->attachment};
            const auto measured=drawing::evaluate_drawing_dimension(drawing_view,dimension);
            check(measured.state==drawing::MeasurementState::Resolved&&std::abs(measured.presentations.front().value-frame.angle*frame.neutral_radius/2)<1e-6,"Drawing measured the wrong distance to the bend line.");
            drawing_view.model_annotations=drawing::deserialize_model_annotations(drawing::serialize_model_annotations(drawing_view.model_annotations));
            dimension=drawing::deserialize_drawing_dimensions(drawing::serialize_drawing_dimensions({dimension})).front();
            check(drawing::evaluate_drawing_dimension(drawing_view,dimension).state==drawing::MeasurementState::Resolved,"Native Drawing lost its bend-line dimension.");
            check(drawing::resolve_dimension_attachment(drawing_view,picked->attachment).has_value(),"Saved Drawing lost the bend-line dimension reference.");
        }
        part=live.open_part(id);check(part->session.document().find_container(unfold)->feature_kind==document::FeatureKind::Unbend,"Unbend command created the wrong feature.");
        const auto rollback=part->session.rollback_boundary(unfold);
        check(rollback&&rollback->input_body&&std::abs(rollback->input_body->volume-original.volume)<1e-9,"Edit rollback did not return the real folded input.");
        check(*part->session.document().find_container(bend)==*before.find_container(bend),"Unbend changed its original source feature.");
        check(!host.execute({{"command","unbend.create"},{"arguments",{{"owners",Json::array({bend})}}}}).ok,"Already unfolded material was accepted twice.");
        run(host,"bend_back.create",{{"owners",Json::array({bend})}});
        for(const auto& source:workspace::drawing_annotation_sources(&live,id,{}))
            check(std::ranges::none_of(source.axes,[](const auto& axis){return kernel::sheet_material::is_bend_line(axis.reference);}),"Folded Drawing still offers historical flat bend lines.");
        check(std::abs(part->session.calculated_boundaries().back().volume-original.volume)<1e-7,"Bend Back changed the original material volume.");
        run(host,"undo");check(part->session.document().history.back().id==unfold,"Undo did not restore the preceding state.");
        run(host,"redo");
        const auto path=directory/"sheet-state.prtz";run(host,"save");
        std::vector<kernel::BodyResult> cache;auto loaded=document::PartDocument::load(path,&cache);
        check(loaded.history.back().feature_kind==document::FeatureKind::BendBack&&!cache.empty(),"Native save lost sheet state history or calculated data.");
        const auto& geometry=cache.back().mesh.original_references;
        check(std::ranges::any_of(geometry.edges,[&](const auto& edge){return edge.reference.owner_id==unfold;}),"Save lost Unbend original reference ownership.");
        kernel::OcctKernel cold;const auto calculated=workspace::calculate_part_with_resolved_references(cold,loaded);
        check(!calculated.empty()&&calculated.back().calculation_errors.empty(),"Cold regeneration failed.");
        check(std::abs(calculated.back().volume-original.volume)<1e-7,"Cold regeneration changed the material state.");
        run(host,"unbend.set",{{"container",unfold},{"all",true}});
        check(part->session.calculated_boundaries().back().calculation_errors.empty(),"Editing an earlier Unbend broke downstream Bend Back.");
        std::cout<<"Attach to derived state geometry"<<std::endl;
        run(host,"new",{{"type","part"},{"name","state-attachment"}});
        const auto attached_source=run(host,"bend.create",{{"width_mm",40.},{"radius_mm",8.},{"thickness_mm",2.}}).data.at("container").get<std::string>();
        const auto attachment_state=run(host,"unbend.create").data.at("container").get<std::string>();
        auto* attached=live.open_part(live.active_document_id());
        const auto& edges=attached->session.calculated_boundaries().back().mesh.original_references.edges;
        const auto end=std::ranges::find_if(edges,[&](const auto& edge) {
            if(edge.reference.owner_id!=attachment_state)return false;
            try {const auto refs=document::flat_sheet_references(edge);return refs[1].semantic_key.find("sweep:cap:end:from:")!=std::string::npos;}
            catch(const std::exception&){return false;}
        });
        check(end!=edges.end(),"Derived Unbend does not expose an attachable original end edge.");
        const auto attached_flat=run(host,"flat.create",{{"edge_owner",attachment_state},{"edge_key",end->reference.semantic_key},{"height_mm",12.}}).data.at("container").get<std::string>();
        const auto attached_before=*attached->session.document().find_container(attached_flat);
        const double uncut_volume=attached->session.calculated_boundaries().back().volume;
        const auto cut_sketch=run(host,"sketch.create",{{"name","Flat cut"},{"plane","XZ"}}).data.at("sketch").get<std::string>();
        // XZ uses local +Y along global -Z; the developed region extends in +Z.
        for(const auto& segment:std::array<std::array<double,4>,4>{{{10,-5,16,-5},{16,-5,16,-10},{16,-10,10,-10},{10,-10,10,-5}}})
            run(host,"sketch.segment.create",{{"sketch",cut_sketch},{"first",{segment[0],segment[1]}},{"second",{segment[2],segment[3]}},{"snap_mm",.000001}});
        run(host,"extrusion.create",{{"sketch",cut_sketch},{"combine","subtract"},{"extent","two_sides"},
            {"end_forward","through_all"},{"end_reverse","through_all"}});
        const double attached_volume=attached->session.calculated_boundaries().back().volume;
        check(attached_volume<uncut_volume-1,"Workspace extrusion did not cut the developed material.");
        const auto sheet_cut_sketch=run(host,"sketch.create",{{"name","Developed Sheet Cut"},{"plane","XZ"}}).data.at("sketch").get<std::string>();
        for(const auto& segment:std::array<std::array<double,4>,4>{{{24,-5,30,-5},{30,-5,30,-10},{30,-10,24,-10},{24,-10,24,-5}}})
            run(host,"sketch.segment.create",{{"sketch",sheet_cut_sketch},{"first",{segment[0],segment[1]}},{"second",{segment[2],segment[3]}},{"snap_mm",.000001}});
        run(host,"extrusion.create",{{"sketch",sheet_cut_sketch},{"sheet_cut",true},{"combine","subtract"},{"extent","two_sides"},
            {"end_forward","through_all"},{"end_reverse","through_all"}});
        const double sheet_cut_volume=attached->session.calculated_boundaries().back().volume;
        check(sheet_cut_volume<attached_volume-1,"Sheet Cut did not cut the developed material.");
        const auto material=workspace::sheet_state_regions(attached->session.document());
        check(material.back().parent_owner_id==attached_source,"Derived attachment lost its canonical source material.");
        run(host,"bend_back.create");run(host,"unbend.create");
        check(*attached->session.document().find_container(attached_flat)==attached_before,"State changes modified a Flat authored on derived geometry.");
        check(std::abs(attached->session.calculated_boundaries().back().volume-sheet_cut_volume)<1e-5,"Derived attachment did not return to its authored state.");
        run(host,"save");std::vector<kernel::BodyResult> edited_cache;
        auto edited_loaded=document::PartDocument::load(directory/"state-attachment.prtz",&edited_cache);
        kernel::OcctKernel edited_cold;const auto edited_calculated=workspace::calculate_part_with_resolved_references(edited_cold,edited_loaded);
        check(edited_calculated.back().calculation_errors.empty()&&std::abs(edited_calculated.back().volume-sheet_cut_volume)<1e-5,
            "Cold native regeneration lost cuts authored between sheet states.");
        std::cout<<"Cross-branch box"<<std::endl;
        const auto box_path=directory/"box-state.prtz";
        std::filesystem::copy_file("cpp/tests/fixtures/sheet/box-cross-branch.prtz",box_path);
        run(host,"open",{{"path",box_path.string()}});run(host,"regenerate");
        auto* box=live.open_part(live.active_document_id());
        const auto box_before=box->session.document();const double box_volume=box->session.calculated_boundaries().back().volume;
        run(host,"unbend.create");
        check(box->session.calculated_boundaries().back().calculation_errors.empty(),"Box unfolding failed.");
        run(host,"bend_back.create");
        check(std::abs(box->session.calculated_boundaries().back().volume-box_volume)<1e-5,"Box return changed its original material.");
        for(const auto& original:box_before.history)check(*box->session.document().find_container(original.id)==original,"Box state operation modified an original feature or placement.");
        std::cout<<"Tilted cone with adjacent bends: cold native regeneration"<<std::endl;
        auto cone=document::PartDocument::load("cpp/tests/fixtures/sheet/tilted-cone-with-bends.prtz");
        kernel::OcctKernel cone_kernel;
        const auto cone_flat=workspace::calculate_part_with_resolved_references(cone_kernel,cone);
        check(!cone_flat.empty()&&cone_flat.back().calculation_errors.empty(),"Cold regeneration cannot unfold the tilted cone with adjacent bends.");
        const auto cone_path=directory/"tilted-cone.prtz";cone.save(cone_path,cone_flat);
        run(host,"open",{{"path",cone_path.string()}});
        run(host,"unbend.create");
        const double cone_developed_volume=live.open_part(live.active_document_id())->session.calculated_boundaries().back().volume;
        const auto rotation=std::ranges::find_if(cone.history,[](const auto& f){return f.revolution.sheet_metal;});
        check(rotation!=cone.history.end(),"Tilted-cone fixture lost its Revolved Sheet.");
        const auto rotation_offered=[&] {
            for(const auto& source:workspace::drawing_annotation_sources(&live,live.active_document_id(),{}))
                if(std::ranges::any_of(source.axes,[&](const auto& axis){return axis.reference.owner_id==rotation->id&&axis.reference.semantic_key=="axis:primary";}))return true;
            return false;
        };
        check(!rotation_offered(),"Developed Drawing offers the old rotation axis.");
        run(host,"bend_back.create");check(rotation_offered(),"Folded Drawing did not restore the rotation axis.");run(host,"unbend.create");
        check(!rotation_offered(),"Second development did not hide the rotation axis.");
        check(std::abs(live.open_part(live.active_document_id())->session.calculated_boundaries().back().volume-cone_developed_volume)<1e-6,"Tilted cone changed on repeated development.");
        run(host,"save");
        auto cone_reopened=document::PartDocument::load(cone_path);kernel::OcctKernel cone_cold;
        const auto cone_regenerated=workspace::calculate_part_with_resolved_references(cone_cold,cone_reopened);
        check(cone_regenerated.back().calculation_errors.empty()&&std::abs(cone_regenerated.back().volume-cone_developed_volume)<1e-6,"Cold regeneration lost the developed cone state.");
        std::filesystem::remove_all(directory);
        std::cout<<"Sheet state command, rollback, Undo/Redo and native persistence tests passed.\n";return 0;
    }catch(const std::exception& error){std::cerr<<error.what()<<"\nFixture: "<<directory.string()<<'\n';return 1;}
}
