#include "profile_command_fixture.hpp"
#include <zima/workspace/appearance_operations.hpp>
#include <zima/command_host/host.hpp>
#include <zima/workspace/drawing_sources.hpp>
#include <zima/workspace/drawing_projection.hpp>
#include <zima/drawing/measurement_dimension.hpp>
#include <zima/drawing/model_annotations.hpp>
#include <QCryptographicHash>
#include <QGuiApplication>
#include <zima/drawing_render/sheet_renderer.hpp>
#include <chrono>
#include <cstdlib>
#include <iostream>
#include <cmath>
using namespace zima;using commands::Json;namespace fs=std::filesystem;
namespace {
void require(bool value,const char* message){if(!value)throw std::runtime_error(message);}
std::string projection_fingerprint(const drawing::DrawingView& view) {
    QCryptographicHash hash(QCryptographicHash::Sha256);
    const auto scalar=[&](auto value){hash.addData(QByteArrayView(reinterpret_cast<const char*>(&value),sizeof(value)));};
    const auto text=[&](const std::string& value){scalar(std::uint64_t(value.size()));hash.addData(QByteArrayView(value.data(),value.size()));};
    const auto reference=[&](const auto& value){text(value.owner_id);text(value.semantic_key);text(value.instance_path);text(value.display_owner_id);};
    const auto point=[&](const auto& p){scalar(p.x);scalar(p.y);};
    scalar(std::uint64_t(view.projected_edges.size()));
    for(const auto& edge:view.projected_edges) {
        reference(edge.source);scalar(edge.hidden);scalar(edge.silhouette);scalar(edge.tangent);scalar(edge.hatch);
        scalar(edge.hatch_pattern);scalar(edge.thread);scalar(edge.thread_leadin);
        scalar(std::uint64_t(edge.points.size()));for(const auto& p:edge.points)point(p);
        scalar(std::uint64_t(edge.vertex_depths.size()));for(double d:edge.vertex_depths)scalar(d);
    }
    scalar(std::uint64_t(view.projected_triangles.size()));
    for(const auto& triangle:view.projected_triangles) {
        reference(triangle.source);scalar(triangle.depth);scalar(triangle.light);
        for(const auto& p:triangle.points)point(p);for(double d:triangle.vertex_depths)scalar(d);
    }
    text(drawing::serialize_measurement_geometry(view));
    text(drawing::serialize_model_annotations(view.model_annotations));
    return hash.result().toHex().toStdString();
}
commands::Result run(command_host::Host& host,const char* name,Json args=Json::object()) {
    auto result=host.execute({{"command",name},{"arguments",std::move(args)}});
    if(!result.ok)throw std::runtime_error(std::string(name)+": "+result.code+": "+result.message);return result;
}
void closed_nested_appearance(const fs::path& dir) {
    const auto nested_file=dir/"paint-assembly.asmz";
    const auto nested=assembly::AssemblyDocument::load(nested_file);
    const auto source_file=dir/"paint-source.prtz";
    std::vector<kernel::BodyResult> boundaries;
    const auto part=document::PartDocument::load(source_file,&boundaries);
    const auto body=part.body_history.active_body_id();
    auto root=assembly::AssemblyDocument::create_default();
    root.name="Closed nested appearance";
    std::vector<assembly::InstancePath> parents;
    for(int repeat=0;repeat<2;++repeat) {
        auto occurrence=assembly::AssemblyDocument::create_assembly_occurrence(
            "Nested",nested.document_id,nested_file,nested);
        occurrence.placement.x=repeat*100;
        parents.push_back(assembly::InstancePath{}.child(occurrence.occurrence_id));
        root.components.push_back(std::move(occurrence));
    }
    const auto file=dir/"closed-nested.asmz";
    static_cast<void>(root.save(file));
    workspace::Workspace live;
    live.add_assembly(assembly::AssemblyDocument::load(file),file);
    live.refresh_source_geometry();
    require(live.size()==1,"Appearance opened a source tab");
    const auto check=[&](const std::string& inherited) {
        const auto revision=live.open_assembly(root.document_id)->session.revision();
        const auto generation=live.open_assembly(root.document_id)->session.data_generation();
        for(const auto& parent:parents)for(std::size_t i=0;i<nested.components.size();++i) {
            const auto& child=nested.components[i];
            const auto value=live.occurrence_appearance(root.document_id,parent.child(child.occurrence_id));
            require(value.has_value(),"Closed nested Part appearance was skipped");
            require(value->bodies.at(body).color==(i==0?"#AA2200":inherited),"Closed nested Body style or occurrence override was lost");
            require(!value->owner_bodies.empty()&&!value->groups.empty(),"Closed nested face groups or Body owners were lost");
        }
        require(live.open_assembly(root.document_id)->session.revision()==revision&&
            live.open_assembly(root.document_id)->session.data_generation()==generation,
            "Appearance lookup changed the Assembly");
    };
    check("#225FC2");
    live.add_part(part,boundaries,source_file);
    auto changed=live.open_part(part.document_id)->session.document();
    changed.appearance.bodies.at(body).color="#123456";
    live.open_part(part.document_id)->session.commit_metadata(std::move(changed));
    live.refresh_source_geometry();
    check("#123456");
    require(live.remove(part.document_id),"Cannot close test Part");
    live.refresh_source_geometry();
    check("#225FC2");
    require(!live.occurrence_appearance(root.document_id,parents.front().child("missing")),"Invalid path resolved an unrelated appearance");
    std::cout<<"Closed/repeated nested Assembly: Body/face styles, overrides and unsaved open Part authority passed\n";
}
void inspect_user_assembly() {
    const auto* input=std::getenv("ZIMA_REVIEW_ASSEMBLY");
    if(!input||!*input)return;
    const auto file=fs::path(input);
    const auto start=std::chrono::steady_clock::now();
    auto stamp=[&](const char* label){std::cout<<label<<" ms="<<std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::steady_clock::now()-start).count()<<std::endl;};
    workspace::Workspace live;
    auto root=assembly::AssemblyDocument::load(file);
    const auto id=root.document_id;
    live.add_assembly(std::move(root),file);stamp("Read-only native load");
    live.refresh_source_geometry();stamp("Source display refresh");
    const auto scene=live.authoritative_viewer_mesh(id);stamp("Current Assembly viewer packet");
    std::set<std::string> paths;
    for(const auto& face:scene.triangle_references)if(!face.instance_path.empty())paths.insert(face.instance_path);
    for(const auto& edge:scene.edges)if(!edge.reference.instance_path.empty())paths.insert(edge.reference.instance_path);
    std::size_t resolved=0,bodies=0,groups=0,faces=0;
    for(const auto& path:paths)if(const auto a=live.occurrence_appearance(id,assembly::InstancePath::decode(path))) {
        ++resolved;bodies+=a->bodies.size();groups+=a->groups.size();
        for(const auto& group:a->groups)faces+=group.faces.size();
    }
    std::cout<<"Read-only appearance: paths="<<paths.size()<<" resolved="<<resolved<<" Body styles="<<bodies<<" groups="<<groups<<" authored faces="<<faces<<std::endl;
    const auto rows=workspace::build_bom_rows_for_source(id,file,&live);stamp("BOM metadata");
    std::cout<<"BOM: immediate occurrences="<<live.open_assembly(id)->session.document().components.size()<<" rows="<<rows.size()<<std::endl;
    drawing::DrawingView view;view.source_document_id=id;view.source_path=file;
    workspace::DrawingProjection projection(&live,{});
    static_cast<void>(projection.source(view));stamp("Projection source and metadata");
    for(auto orientation:{drawing::ViewOrientation::Isometric,drawing::ViewOrientation::Front,drawing::ViewOrientation::Top}) {
        view.camera=drawing::standard_camera(orientation);
        const auto camera_start=std::chrono::steady_clock::now();
        projection.project(view,{.interactive=true});
        const auto elapsed=std::chrono::duration<double,std::milli>(std::chrono::steady_clock::now()-camera_start).count();
        std::cout<<"Interactive orientation="<<int(orientation)<<" ms="<<elapsed<<" edges="<<view.projected_edges.size()<<" triangles="<<view.projected_triangles.size()<<" SHA256="<<projection_fingerprint(view)<<std::endl;
    }
    stamp("Interactive drawing views");
    std::cout<<"Projection: precise cameras="<<projection.calculated_camera_count()<<" interactive cameras="<<projection.calculated_interactive_camera_count()<<std::endl;
}
void inspect_user_drawing() {
    const auto* input=std::getenv("ZIMA_REVIEW_DRAWING");
    if(!input||!*input)return;
    const auto file=fs::path(input);
    int argc=1;char application_name[]="drawing-review";char* argv[]{application_name,nullptr};
    std::unique_ptr<QGuiApplication> gui;
    if(std::getenv("ZIMA_REVIEW_DRAWING_IMAGE"))gui=std::make_unique<QGuiApplication>(argc,argv);
    const auto start=std::chrono::steady_clock::now();
    const auto doc=drawing::DrawingDocument::load(file);
    std::cout<<"Read-only Drawing load ms="<<std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::steady_clock::now()-start).count()<<" sheets="<<doc.sheets.size()<<std::endl;
    for(const auto& sheet:doc.sheets) {
        auto path=doc.data_source_path(sheet.bom_source_document_id);
        if(path.is_relative())path=file.parent_path()/path;
        const auto context=workspace::build_title_block_context_for_source(sheet.bom_source_document_id,path,nullptr);
        const auto layout=drawing::title_block_layout(sheet,context);
        if(const auto* output=std::getenv("ZIMA_REVIEW_DRAWING_IMAGE")) {
            auto paper=sheet;paper.views.clear();
            drawing_render::SheetRenderer renderer;renderer.set_render_sheet(&paper);renderer.set_render_context(context);
            QImage image(1100,600,QImage::Format_ARGB32_Premultiplied);image.fill(Qt::black);
            {QPainter painter(&image);renderer.paint_sheet(painter,5,{20-(sheet.width_mm()-210)*5,500-sheet.height_mm()*5},false);}
            require(image.save(QString::fromUtf8(output)),"Cannot render read-only title-block inspection");
        }
        std::set<std::size_t> rendered_rows;
        for(const auto& [id,target]:layout.edit_targets)if(target.bom_row)rendered_rows.insert(*target.bom_row);
        std::cout<<"Drawing sheet: views="<<sheet.views.size()<<" BOM rows="<<sheet.bom_rows.size()<<" repeat regions="<<sheet.repeat_regions.size()
            <<" title fields="<<sheet.title_block_fields.size()<<" title texts="<<sheet.title_block_texts.size()<<" rendered BOM rows="<<rendered_rows.size()<<std::endl;
        for(const auto& row:sheet.bom_rows)std::cout<<"BOM row: item="<<row.item_number<<" quantity="<<row.quantity<<" source="<<row.source_path.filename().string()<<std::endl;
        for(const auto& row:sheet.bom_rows) {
            std::cout<<"BOM metadata: parameters="<<row.parameters.size()<<" aliases="<<row.parameter_aliases.size()<<" keys=";
            for(const auto& [key,value]:row.parameters)std::cout<<key<<',';std::cout<<std::endl;
        }
        for(const auto& text:layout.texts)if(const auto target=layout.edit_targets.find(text.field_id);
                target!=layout.edit_targets.end()&&target->second.bom_row)
            std::cout<<"BOM cell: expression="<<target->second.expression<<" text="<<text.text<<" x="<<text.position.x<<" y="<<text.position.y<<std::endl;
        for(const auto& region:sheet.repeat_regions)std::cout<<"Repeat region: x="<<region.x<<" y="<<region.y<<" width="<<region.width<<" height="<<region.height<<" step="<<region.step<<std::endl;
        for(const auto& text:layout.texts)for(const auto& region:sheet.repeat_regions)
            if(text.position.x>=region.x&&text.position.x<=region.x+region.width&&text.position.y>=region.y&&text.position.y<=region.y+region.height)
                std::cout<<"BOM visible text: "<<text.text<<" x="<<text.position.x<<" y="<<text.position.y<<std::endl;
        for(const auto& view:sheet.views)std::cout<<"View: edges="<<view.projected_edges.size()<<" triangles="<<view.projected_triangles.size()<<" measurements="<<view.measurement_geometry->curves.size()<<std::endl;
        if(std::getenv("ZIMA_REVIEW_PICKER"))for(const auto& view:sheet.views) {
            const auto prepared=std::chrono::steady_clock::now();
            drawing::MeasurementPicker picker;picker.prepare(view);
            std::cout<<"Picker prepare ms="<<std::chrono::duration<double,std::milli>(std::chrono::steady_clock::now()-prepared).count()<<std::endl;
            QCryptographicHash hash(QCryptographicHash::Sha256);
            const auto scalar=[&](auto value){hash.addData(QByteArrayView(reinterpret_cast<const char*>(&value),sizeof(value)));};
            const auto text=[&](const std::string& value){scalar(std::uint64_t(value.size()));hash.addData(QByteArrayView(value.data(),value.size()));};
            const auto reference=[&](const auto& r){text(r.owner_id);text(r.semantic_key);text(r.instance_path);};
            const auto curves=drawing::projected_measurement_curves(view);
            unsigned queries{},offered{};
            for(std::size_t i=0;i<curves.size();i+=std::max<std::size_t>(1,curves.size()/24)) {
                if(curves[i].points.empty())continue;
                for(int mode:{-1,0,1,2,3,4}) {
                    drawing::MeasurementPickRequest request;request.mode=mode;
                    const auto p=curves[i].points[curves[i].points.size()/2];
                    const auto candidates=picker.candidates(view,p,.5,request);++queries;
                    scalar(std::uint64_t(candidates.size()));offered+=unsigned(candidates.size());
                    for(const auto& candidate:candidates) {
                        const auto& a=candidate.attachment;scalar(int(a.kind));reference(a.reference);reference(a.other_reference);scalar(a.parameter);scalar(a.side);
                        scalar(candidate.position.x);scalar(candidate.position.y);scalar(candidate.distance);scalar(candidate.point_target);
                    }
                }
            }
            std::cout<<"Picker: queries="<<queries<<" candidates="<<offered<<" SHA256="<<hash.result().toHex().toStdString()<<std::endl;
        }
    }
}
void verify(const kernel::OcctKernel& kernel,fs::path dir) {
    workspace::Workspace live;command_host::Options options;
    options.settings=[] {return command_host::Settings{{fs::absolute("config/templates"),"START_PART_mm.prtz","START_ASSEMBLY_mm.asmz","Body"},{}};};
    command_host::Host host(live,kernel,dir,options);
    require(host.execute_text("appearance.get").code=="unsupported_document","Appearance query without document failed unsafely");
    require(run(host,"appearance.palette").data.at("items").size()>=26,"Built-in palette missing");
    run(host,"new",{{"type","part"},{"name","paint-source"}});const auto source=live.active_document_id();
    zima::test::rectangular_commands([&](const char* n,commands::Json a){return run(host,n,std::move(a));},{{"length_mm","10"},{"width_mm","20"},{"height_mm","30"}});
    auto* part=live.open_part(source);const auto body=part->session.document().body_history.active_body_id();
    const auto revision=part->session.revision();const auto* allocation=part->session.calculated_boundaries().data();
    const auto faces=run(host,"appearance.faces").data;
    require(faces.at("total")==10,
        "Symmetric box appearance query did not preserve its ten authored result-face identities");
    const auto original=run(host,"appearance.get").data;
    const auto original_body=part->session.calculated_boundaries().back();
    auto measured=original_body;kernel.ensure_body_properties(measured);
    require(std::abs(measured.volume-6000)<1e-8,"Appearance fixture has the wrong physical volume");
    require(part->session.revision()==revision&&part->session.calculated_boundaries().data()==allocation,"Appearance queries calculated geometry");
    const auto blue=Json{{"color","#225FC2"},{"roughness",.123456789},{"metallic",.75}};
    const auto stale=workspace::prepare_appearance_edit(live,source);
    run(host,"appearance.set",{{"style",blue}});
    require(part->session.document().appearance.bodies.at(body).color=="#225FC2","Part active Body appearance did not change");
    const auto& styled_body=part->session.calculated_boundaries().back();
    require(styled_body.kernel_shape==original_body.kernel_shape&&styled_body.volume==original_body.volume&&
        styled_body.physical_properties_current==original_body.physical_properties_current,"Appearance changed body geometry or analysis");
    bool stale_rejected=false;
    try {static_cast<void>(workspace::commit_appearance(live,stale,stale.initial));}catch(const workspace::AppearanceError& e){stale_rejected=std::string(e.code)=="stale_edit";}
    require(stale_rejected,"Stale appearance edit overwrote newer changes");
    const auto changed_revision=part->session.revision();const auto* changed_cache=part->session.calculated_boundaries().data();
    require(!run(host,"appearance.set",{{"style",blue}}).data.at("changed").get<bool>()&&part->session.revision()==changed_revision&&part->session.calculated_boundaries().data()==changed_cache,"Identical appearance committed or copied geometry");
    const auto face=faces.at("items").front();
    const auto group=Json{{"id","polished"},{"name","Leštěná plocha"},{"style",{{"color","#CCBB99"},{"roughness",.04},{"metallic",1}}},
        {"faces",Json::array({{{"owner",face.at("owner")},{"key",face.at("key")}}})}};
    run(host,"appearance.set",{{"groups",Json::array({group})}});
    const auto painted=part->session.document().appearance;const auto before_revision=part->session.revision();
    for(const auto& args:std::vector<Json>{{{"style",{{"color","red"}}}},{{"style",{{"roughness",0}}}},{{"style",{{"metallic",2}}}},
        {{"style",{{"surprise",1}}}},{{"groups",Json::array({group,group})}},{{"groups",Json::array({{{"id","bad"},{"name","Bad"},{"faces",Json::array({{{"owner","missing"},{"key","face"}}})}}})}}}) {
        require(!host.execute({{"command","appearance.set"},{"arguments",args}}).ok,"Invalid appearance accepted");
        require(part->session.revision()==before_revision&&part->session.document().appearance==painted,"Rejected appearance partially committed");
    }
    run(host,"appearance.reset");require(part->session.document().appearance.groups.empty()&&!part->session.document().appearance.bodies.contains(body),"Part reset did not remove body overrides");
    run(host,"undo");require(part->session.document().appearance==painted,"Appearance Undo lost groups");run(host,"redo");run(host,"undo");
    run(host,"body.create",{{"name","Other"}});
    require(host.execute({{"command","appearance.set"},{"arguments",{{"body",body},{"style",blue}}}}).code=="inactive_body","Inactive Body appearance was editable");
    run(host,"undo");run(host,"body.activate",{{"body",body}});
    run(host,"save");const auto native=document::PartDocument::load(dir/"paint-source.prtz");require(native.appearance==painted,"Native Part lost appearance");
    run(host,"new",{{"type","assembly"},{"name","paint-assembly"}});const auto assembly_id=live.active_document_id();
    const auto first=run(host,"component.insert",{{"source",source}}).data.at("occurrence").get<std::string>();
    const auto second=run(host,"component.insert",{{"source",source}}).data.at("occurrence").get<std::string>();
    const auto path=assembly::InstancePath{}.child(first).encoded();
    const auto source_revision=live.open_part(source)->session.revision();
    auto* owner=live.open_assembly(assembly_id);const auto snapshot=owner->session.document().find_occurrence(first)->calculated_source;
    run(host,"appearance.set",{{"instance_path",path},{"body",body},{"style",{{"color","#AA2200"}}}});
    const auto first_value=*owner->session.document().find_occurrence(first);
    require(first_value.appearance_override&&first_value.appearance_override->bodies.at(body).color=="#AA2200","Occurrence body override missing");
    require(!owner->session.document().find_occurrence(second)->appearance_override&&live.open_part(source)->session.revision()==source_revision,"Occurrence style leaked into sibling or source");
    require(first_value.calculated_source.shares_with(snapshot),"Appearance copied or recalculated component geometry");
    run(host,"save");const auto saved=assembly::AssemblyDocument::load(dir/"paint-assembly.asmz");
    require(saved.find_occurrence(first)->appearance_override==first_value.appearance_override,"Native Assembly lost override");
    require(!host.execute({{"command","appearance.set"},{"arguments",{{"instance_path",assembly::InstancePath{}.child(first).child(second).encoded()},{"style",blue}}}}).ok,"Nested occurrence bypassed its owning Assembly");
    run(host,"appearance.reset",{{"instance_path",path},{"body",body}});
    require(!owner->session.document().find_occurrence(first)->appearance_override->bodies.contains(body),"Occurrence body reset failed");
    run(host,"undo");require(owner->session.document().find_occurrence(first)->appearance_override->bodies.at(body).color=="#AA2200","Occurrence appearance Undo failed");
    run(host,"appearance.reset",{{"instance_path",path},{"inherit",true}});
    require(!owner->session.document().find_occurrence(first)->appearance_override,"Source inheritance kept an occurrence override");
    run(host,"activate",{{"document",source}});
    run(host,"appearance.set",{{"style",{{"color","#336699"}}}});
    run(host,"activate",{{"document",assembly_id}});
    require(run(host,"appearance.get",{{"instance_path",path},{"body",body}}).data.at("style").at("color")=="#336699","Open source appearance required saving or regeneration");
    auto patterned=assembly::AssemblyDocument::create_part_occurrence(
        "Paint pattern",source,dir/"paint-source.prtz",{});
    patterned.occurrence_id="paint-pattern";
    patterned.derived_copy=document::DerivedCopyParameters{
        {first},{{},patterned.occurrence_id+":origin","origin:axis:z"}};
    patterned.derived_copy->pattern=kernel::PatternRequest{};
    auto patterned_document=owner->session.document();
    patterned_document.components.push_back(std::move(patterned));
    patterned_document.calculate_derived_copies(kernel);
    owner->session.commit(std::move(patterned_document));
    std::set<std::string> pattern_paths;
    const auto pattern_prefix=assembly::InstancePath{}.child("paint-pattern").encoded();
    for(const auto& reference:owner->session.document().build_scene().triangle_references)
        if(reference.instance_path.starts_with(pattern_prefix))pattern_paths.insert(reference.instance_path);
    require(pattern_paths.size()==3,"Appearance fixture Pattern did not expose each copy path");
    const auto pattern_revision=owner->session.revision();
    const auto pattern_generation=owner->session.data_generation();
    for(const auto& copy_path:pattern_paths) {
        const auto inherited=live.occurrence_appearance(
            assembly_id,assembly::InstancePath::decode(copy_path));
        require(inherited&&inherited->bodies.at(body).color=="#336699",
            "Pattern copy did not inherit the current source Part Body style");
    }
    require(owner->session.revision()==pattern_revision&&
        owner->session.data_generation()==pattern_generation,
        "Pattern appearance lookup changed the Assembly");
    const auto pattern_source_file=dir/"paint-pattern-source.prtz";
    const auto* current_source=live.open_part(source);
    static_cast<void>(current_source->session.document().save(
        pattern_source_file,current_source->session.calculated_boundaries()));
    auto persisted_pattern=assembly::AssemblyDocument::create_default();
    auto persisted_source=assembly::AssemblyDocument::create_part_occurrence(
        "Persisted source",source,pattern_source_file,
        current_source->session.calculated_boundaries().back());
    persisted_source.source_name=current_source->session.document().name;
    persisted_source.body_color=current_source->session.document().body_color;
    persisted_source.appearance=workspace::part_appearance(
        current_source->session.document());
    const auto persisted_source_id=persisted_source.occurrence_id;
    persisted_pattern.components.push_back(std::move(persisted_source));
    auto persisted_copies=assembly::AssemblyDocument::create_part_occurrence(
        "Persisted pattern",source,pattern_source_file,{});
    persisted_copies.occurrence_id="persisted-paint-pattern";
    persisted_copies.derived_copy=document::DerivedCopyParameters{
        {persisted_source_id},{{},persisted_copies.occurrence_id+":origin","origin:axis:z"}};
    persisted_copies.derived_copy->pattern=kernel::PatternRequest{};
    persisted_pattern.components.push_back(std::move(persisted_copies));
    persisted_pattern.calculate_derived_copies(kernel);
    const auto pattern_file=dir/"paint-pattern.asmz";
    static_cast<void>(persisted_pattern.save(pattern_file));
    std::set<std::string> persisted_paths;
    const auto persisted_prefix=assembly::InstancePath{}.child(
        "persisted-paint-pattern").encoded();
    for(const auto& reference:persisted_pattern.build_scene().triangle_references)
        if(reference.instance_path.starts_with(persisted_prefix))
            persisted_paths.insert(reference.instance_path);
    require(persisted_paths.size()==3,
        "Persisted appearance Pattern did not expose each copy path");
    workspace::Workspace reopened_pattern;
    reopened_pattern.add_assembly(
        assembly::AssemblyDocument::load(pattern_file),pattern_file);
    reopened_pattern.refresh_source_geometry();
    for(const auto& copy_path:persisted_paths) {
        const auto inherited=reopened_pattern.occurrence_appearance(
            persisted_pattern.document_id,assembly::InstancePath::decode(copy_path));
        require(inherited&&inherited->bodies.at(body).color=="#336699",
            "Reopened Pattern copy lost the source Part Body style");
    }
    run(host,"new",{{"type","part"},{"name","paint-copies"}});
    zima::test::rectangular_commands([&](const char* n,commands::Json a){return run(host,n,std::move(a));},{{"length_mm","10"},{"width_mm","20"},{"height_mm","30"}});
    const auto copy_part=live.active_document_id();const auto source_body=live.open_part(copy_part)->session.document().body_history.active_body_id();
    run(host,"body.activate"); // Whole-Body sources are offered at the Part boundary.
    const auto mirrored=run(host,"mirror.create",{{"source",source_body},{"local_plane","yz"},{"placement",{{"x",30}}}}).data.at("object").get<std::string>();
    const auto copied_faces=run(host,"appearance.faces",{{"body",mirrored}}).data;
    require(copied_faces.at("total")==10,
        "Mirrored Body appearance lost its authored result-face owners");
    const auto mirrored_packet=live.open_part(copy_part)->session.calculated_boundaries().back().body_outputs.at(mirrored);
    const auto active_before_color=live.open_part(copy_part)->session.document().body_history.active_body_id();
    run(host,"appearance.set",{{"body",mirrored},{"style",blue}});
    require(live.open_part(copy_part)->session.calculated_boundaries().back().body_outputs.at(mirrored).shares_with(mirrored_packet),"Derived appearance copied or recalculated its geometry");
    require(live.open_part(copy_part)->session.document().body_history.active_body_id()==active_before_color,"Derived appearance changed the active Body");
    require(live.open_part(copy_part)->session.document().appearance.bodies.at(mirrored).color=="#225FC2","Derived Body appearance was not editable independently");
    run(host,"save");run(host,"new",{{"type","assembly"},{"name","paint-copied-source"}});
    const auto copied_owner=live.active_document_id();
    const auto copied_occurrence=run(host,"component.insert",{{"source",copy_part}}).data.at("occurrence").get<std::string>();
    const auto copied_path=assembly::InstancePath{}.child(copied_occurrence).encoded();
    run(host,"close",{{"document",copy_part}});
    require(live.active_document_id()==copied_owner,"Closing inactive source changed active Assembly");
    require(run(host,"appearance.faces",{{"instance_path",copied_path},{"body",mirrored}}).data.at("total")==10,"Closed source lost mirrored face ownership");
    require(run(host,"appearance.get",{{"instance_path",copied_path},{"body",mirrored}}).data.at("style").at("color")=="#225FC2","Closed source lost mirrored body style");


}
}
int main(){try {if(std::getenv("ZIMA_REVIEW_DRAWING")){inspect_user_drawing();return 0;}if(std::getenv("ZIMA_REVIEW_ASSEMBLY")){inspect_user_assembly();return 0;}kernel::OcctKernel kernel;const auto parent=fs::canonical(fs::temp_directory_path());
    const auto dir=parent/("zima-appearance-command-"+document::PartDocument::create_default().document_id);
    require(fs::create_directory(dir),"Cannot create test directory");verify(kernel,dir);closed_nested_appearance(dir);
    require(dir.parent_path()==parent,"Unexpected cleanup path");fs::remove_all(dir);
    std::cout<<"Appearance commands, ownership, geometry preservation and native persistence passed\n";return 0;
}catch(const std::exception& error){std::cerr<<error.what()<<'\n';return 1;}}
