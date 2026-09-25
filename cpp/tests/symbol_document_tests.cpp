#include <zima/workspace/symbol_operations.hpp>
#include <zima/workspace/document_operations.hpp>
#include <zima/workspace/sketch_operations.hpp>
#include <zima/kernel/stable_id.hpp>
#include <zima/sketcher/text_geometry.hpp>
#include <iostream>
using namespace zima;
namespace {void check(bool ok,const char* text){if(!ok)throw std::runtime_error(text);}}
namespace {
void native_annotations(const std::filesystem::path& root) {
    workspace::Workspace live;
    auto part=document::PartDocument::create_default();const auto part_id=part.document_id;live.add_part(part);
    auto assembly=assembly::AssemblyDocument::create_default();const auto assembly_id=assembly.document_id;live.add_assembly(assembly);
    auto drawing=drawing::DrawingDocument::create_default();const auto drawing_id=drawing.document_id;
    const auto sheet=drawing.sheets.front().id;live.add_drawing(drawing);
    auto definition=symbols::projection_method();symbols::Placement annotation;
    annotation.symbol.id="native-annotation";annotation.symbol.definition=definition.serialized();annotation.symbol.variant=definition.default_variant;
    annotation.symbol.x=12;annotation.symbol.y=8;annotation.leader=true;annotation.frame.origin={20,30,40};
    for(const auto& id:{part_id,assembly_id,drawing_id}) {
        live.activate(id);const auto target=id==drawing_id?sheet:std::string{};
        annotation.reference=symbols::Reference{id,id+":origin","plane:xy","",symbols::ReferenceKind::Plane,true};
        annotation.unresolved=true;
        check(workspace::store_symbol_annotation(live,id,annotation,target),"Annotation insertion was not committed");
        check(!workspace::store_symbol_annotation(live,id,annotation,target),"Unchanged annotation created an Undo record");
        check(workspace::step_document_history(live,id,workspace::HistoryDirection::Undo),"Annotation Undo failed");
        check(workspace::symbol_annotations(live,id,target).empty(),"Annotation Undo retained inserted symbol");
        check(workspace::step_document_history(live,id,workspace::HistoryDirection::Redo),"Annotation Redo failed");
        check(workspace::symbol_annotations(live,id,target)==std::vector{annotation},"Annotation Redo lost placement or identity");
        if(id!=drawing_id) {
            const auto mesh=id==part_id?live.open_part(id)->session.document().construction_viewer_mesh():live.open_assembly(id)->session.document().construction_viewer_mesh();
            const auto count=std::ranges::count_if(mesh.edges,[&](const auto& edge){return edge.reference.owner_id==annotation.symbol.id;});
            check(count==annotation.viewer_mesh().edges.size(),"Model display omitted or duplicated symbol strokes");
            check(std::ranges::none_of(mesh.original_references.edges,[&](const auto& edge){return edge.reference.owner_id==annotation.symbol.id;}),"Annotation became a model topology reference");
        }
        auto invalid=annotation;invalid.frame.x={0,0,0};bool rejected=false;
        try{workspace::store_symbol_annotation(live,id,invalid,target);}catch(const std::exception&){rejected=true;}
        check(rejected&&workspace::symbol_annotations(live,id,target)==std::vector{annotation},"Invalid edit changed annotation document");
        const auto extension=id==part_id?".prtz":id==assembly_id?".asmz":".drwz";
        const auto path=root/(id+extension);auto pending=workspace::prepare_document_save(live,id,path);
        check(workspace::complete_document_save(live,pending.write()),"Native annotation save failed");
        const auto restored=id==part_id?document::PartDocument::load(path).symbol_annotations:
            id==assembly_id?assembly::AssemblyDocument::load(path).symbol_annotations:drawing::DrawingDocument::load(path).sheets.front().symbol_annotations;
        check(restored==std::vector{annotation},"Native file lost unresolved symbol or leader");
        const auto copy_path=root/(id+"-copy"+extension);static_cast<void>(live.save_copy(id,copy_path));
        symbols::Placement copied;std::string copied_id;
        if(id==part_id){auto copy=document::PartDocument::load(copy_path);copied=copy.symbol_annotations.front();copied_id=copy.document_id;}
        else if(id==assembly_id){auto copy=assembly::AssemblyDocument::load(copy_path);copied=copy.symbol_annotations.front();copied_id=copy.document_id;}
        else {auto copy=drawing::DrawingDocument::load(copy_path);copied=copy.sheets.front().symbol_annotations.front();copied_id=copy.document_id;}
        check(copied_id!=id&&copied.reference->document_id==copied_id&&copied.reference->owner_id==copied_id+":origin","Copy retained source document annotation identity");
        check(copied.frame==annotation.frame&&copied.symbol==annotation.symbol&&copied.unresolved,"Copy changed embedded definition or unresolved pose");
        check(workspace::remove_symbol_annotation(live,id,annotation.symbol.id,target),"Annotation removal failed");
        check(workspace::step_document_history(live,id,workspace::HistoryDirection::Undo),"Annotation deletion Undo failed");
        check(workspace::symbol_annotations(live,id,target)==std::vector{annotation},"Annotation deletion Undo lost data");
    }
}
void contact_transactions() {
    auto part=document::PartDocument::create_default();document::DocumentSession session(part,{});
    auto surface=std::make_shared<kernel::SurfaceGeometry>();surface->kind=kernel::SurfaceGeometry::Kind::Cylinder;surface->radius=5;
    kernel::FaceReference face{"authored-cylinder","face:wall","",surface};kernel::BodyResult body;body.mesh.original_references.triangle_references={face};
    const auto definition=symbols::projection_method();symbols::Placement value;value.symbol.id="contact-symbol";
    value.symbol.definition=definition.serialized();value.symbol.variant=definition.default_variant;
    symbols::attach_to_surface(value,face,part.document_id,{5,0,2});part.symbol_annotations={value};session.commit(part,{body});
    surface=std::make_shared<kernel::SurfaceGeometry>(*surface);surface->radius=8;body.mesh.original_references.triangle_references.front().surface=surface;
    session.commit(session.document(),{body});check(session.document().symbol_annotations.front().frame.origin==kernel::Vec3{8,0,2},"Model transaction failed to move surface attachment");
    session.commit(session.document(),{});const auto lost=session.document().symbol_annotations.front();
    check(lost.unresolved&&lost.frame.origin==kernel::Vec3{8,0,2},"Deleting source did not retain last symbol pose");
    check(session.undo()&&!session.document().symbol_annotations.front().unresolved,"Undo source removal did not restore attachment");
    check(session.redo()&&session.document().symbol_annotations.front()==lost,"Redo source removal changed retained annotation");
    auto replaced=session.document();replaced.symbol_annotations.front().reference->document_id="different-source";
    session.commit(replaced,{body});
    check(session.document().symbol_annotations.front().unresolved&&session.document().symbol_annotations.front().frame==lost.frame,
          "Same topology IDs in a different source rebound the symbol");
}
}
int main(){try {
    const auto root=std::filesystem::temp_directory_path()/("zima-symbol-editor-"+kernel::make_stable_id());
    std::filesystem::create_directory(root);
    native_annotations(root);
    contact_transactions();
    workspace::Workspace live;const auto id=workspace::create_symbol_document(live,"Example",root/"example.symz");
    check(workspace::document_needs_save(live,id),"New symbol incorrectly clean");
    const auto sketch_id=live.open_part(id)->session.document().sketches.front().id;
    workspace::mutate_document_sketch(live,id,sketch_id,[](auto& sketch){static_cast<void>(sketch.add_rectangle(0,0,10,5));});
    auto pending=workspace::prepare_document_save(live,id,root/"example.symz");
    check(workspace::complete_document_save(live,pending.write()),"Symbol save receipt failed");
    auto saved=symbols::Definition::load(root/"example.symz");check(saved.sketches.front().segments.size()==4,"Symbol saved wrong geometry");
    check(!workspace::document_needs_save(live,id),"Saved symbol dirty");
    check(workspace::step_document_history(live,id,workspace::HistoryDirection::Undo),"Symbol Undo failed");
    check(workspace::edited_symbol_definition(live,id).sketches.front().segments.empty(),"Undo retained rectangle");
    check(workspace::step_document_history(live,id,workspace::HistoryDirection::Redo),"Symbol Redo failed");
    workspace::save_symbol_document(live,id,root/"copy.symz",true);
    check(live.open_part(id)->path.filename()=="example.symz","Copy retargeted document");
    check(symbols::Definition::load(root/"copy.symz").serialized()==saved.serialized(),"Copy lost definition");
    workspace::Workspace reopened;const auto reopened_id=workspace::open_symbol_document(reopened,root/"example.symz");
    check(workspace::edited_symbol_definition(reopened,reopened_id).serialized()==saved.serialized(),"Reopen changed definition");
    check(workspace::open_symbol_document(reopened,root/"example.symz")==reopened_id&&reopened.size()==1,"Open duplicated tab");
    auto multivariant=symbols::projection_method();multivariant.save(root/"variants.symz");
    const auto multi=workspace::open_symbol_document(live,root/"variants.symz");
    workspace::save_symbol_document(live,multi,root/"variants.symz");
    check(symbols::Definition::load(root/"variants.symz").serialized()==multivariant.serialized(),"Editing carrier lost variants, identities or pens");
    auto label=sketcher::Sketch::create_text();label.value="Ra 3.2";label.modeling_geometry=false;
    sketcher::rebuild_text_contours(label,true);multivariant.sketches.front().texts.push_back(label);
    multivariant.fields["Specification"]={multivariant.sketches.front().id,label.id,{"Ra 3.2","Ra 6.3"},true};
    multivariant.variants.at(multivariant.default_variant).text_values["Specification"]="Ra 3.2";
    multivariant.save(root/"fields.symz");const auto fields=workspace::open_symbol_document(live,root/"fields.symz");
    workspace::mutate_document_sketch(live,fields,multivariant.sketches.front().id,[](auto& s){s.texts.clear();});
    check(workspace::edited_symbol_definition(live,fields).fields.empty(),"Deleting text retained dangling editable field");
    check(workspace::step_document_history(live,fields,workspace::HistoryDirection::Undo),"Field deletion Undo failed");
    check(workspace::edited_symbol_definition(live,fields).serialized()==multivariant.serialized(),"Undo failed to restore field metadata");
    std::filesystem::remove_all(root);std::cout<<"Symbol document creation, edit, save, copy, reopen, variants and Undo/Redo passed\n";return 0;
}catch(const std::exception& e){std::cerr<<e.what()<<'\n';return 1;}}
