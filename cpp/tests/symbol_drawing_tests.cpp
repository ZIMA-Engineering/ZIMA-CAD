#include <zima/workspace/drawing_sources.hpp>
#include <zima/document/component_source.hpp>
#include <zima/symbols/definition.hpp>
#include <zima/kernel/stable_id.hpp>
#include <iostream>
#include <zima/drawing/symbol_contacts.hpp>
#include <zima/drawing/measurement_dimension.hpp>
using namespace zima;
namespace {
void check(bool ok,const char* message){if(!ok)throw std::runtime_error(message);}
std::array<double,2> paper_extent(const drawing::DrawingView& view,const drawing::ModelAnnotation& annotation) {
    const auto projected=drawing::project_model_annotation(view,annotation);
    double xmin=1e100,xmax=-1e100,ymin=1e100,ymax=-1e100;
    for(const auto& curve:projected.curves)for(auto p:curve){xmin=std::min(xmin,p.x);xmax=std::max(xmax,p.x);ymin=std::min(ymin,p.y);ymax=std::max(ymax,p.y);}
    return {(xmax-xmin)*view.scale,(ymax-ymin)*view.scale};
}
}
int main(){try {
    workspace::Workspace live;auto part=document::PartDocument::create_default();
    symbols::Placement symbol;const auto definition=symbols::projection_method();symbol.symbol.id="drawing-source-symbol";
    symbol.symbol.definition=definition.serialized();symbol.symbol.variant=definition.default_variant;symbol.symbol.x=15;symbol.symbol.y=8;
    symbol.frame.origin={20,30,40};symbol.leader=true;part.symbol_annotations={symbol};live.add_part(part);
    auto packets=workspace::drawing_annotation_sources(&live,part.document_id,{});
    drawing::DrawingView view;view.camera={{1,0,0},{0,1,0},{0,0,1}};view.scale=.2;
    drawing::refresh_model_annotations(view,packets);
    auto found=std::ranges::find_if(view.model_annotations,[](const auto& item){return item.kind==drawing::ModelAnnotationKind::Symbol;});
    check(found!=view.model_annotations.end()&&found->model_symbol==symbol,"Model symbol not inherited");
    check(std::ranges::count_if(view.model_annotations,[](const auto& item){return item.source.semantic_id.starts_with("symbol:");})==1,"Symbol construction strokes duplicated annotation");
    const auto candidates=drawing::show_erase_candidates(view.model_annotations,drawing::ShowEraseMode::Show,{drawing::ModelAnnotationKind::Symbol});
    check(candidates.size()==1&&!found->visible,"Show/Erase did not offer hidden symbol");found->visible=true;
    const auto small=paper_extent(view,*found);view.scale=2;const auto large=paper_extent(view,*found);
    check(std::abs(small[0]-large[0])<1e-8&&std::abs(small[1]-large[1])<1e-8,"Drawing view scale changed nominal symbol size");
    found->paper_handles["text"]={63,29};drawing::refresh_model_annotations(view,packets);
    found=std::ranges::find_if(view.model_annotations,[](const auto& item){return item.kind==drawing::ModelAnnotationKind::Symbol;});
    check(found->visible&&found->paper_handles.at("text")==drawing::Point2{63,29},"Source refresh lost independent Drawing placement");
    const auto shown=drawing::project_model_annotation(view,*found);check(std::abs(shown.text_anchor.x*view.scale-63)<1e-8,"Drawing grip override was ignored");
    const auto saved=drawing::serialize_model_annotations(view.model_annotations);
    check(drawing::deserialize_model_annotations(saved)==view.model_annotations,"Drawing lost embedded source definition");
    auto missing=packets;for(auto& packet:missing)packet.symbols.clear();drawing::refresh_model_annotations(view,missing);
    found=std::ranges::find_if(view.model_annotations,[](const auto& item){return item.kind==drawing::ModelAnnotationKind::Symbol;});
    check(found->unresolved&&found->visible&&found->model_symbol==symbol,"Missing model source deleted or moved Drawing symbol");
    auto assembly=assembly::AssemblyDocument::create_default();
    for(int i=0;i<2;++i){assembly::PartOccurrence occurrence;occurrence.occurrence_id="symbol-occurrence-"+std::to_string(i);
        occurrence.source_document_id=part.document_id;occurrence.placement.x=i*100;occurrence.calculated_source=document::component_source(part,{});assembly.components.push_back(std::move(occurrence));}
    live.add_assembly(assembly);const auto repeated=workspace::drawing_annotation_sources(&live,assembly.document_id,{});
    std::set<std::string> paths;std::set<double> x;
    for(const auto& packet:repeated)for(const auto& item:packet.symbols){paths.insert(packet.instance_path);x.insert(item.frame.origin.x);}
    check(paths.size()==2&&x==std::set<double>{20,120},"Repeated Assembly symbols lost occurrence placement");
    auto sheet_document=drawing::DrawingDocument::create_default();auto& sheet=sheet_document.sheets.front();
    drawing::DrawingView attached_view;attached_view.id="contact-view";attached_view.source_document_id=part.document_id;
    attached_view.camera={{1,0,0},{0,1,0},{0,0,1}};attached_view.x=100;attached_view.y=80;attached_view.scale=2;
    kernel::ViewerMesh geometry;kernel::ViewerEdge edge;edge.reference={"profile","original:curve","occurrence"};edge.points={{0,0,0},{10,0,0}};geometry.edges={edge};
    drawing::capture_measurement_geometry(attached_view,geometry);sheet.views={attached_view};
    drawing::DimensionAttachment attachment;attachment.reference=edge.reference;attachment.parameter=.25;
    {
        auto split=edge;split.points={{8,0,0},{6,0,0}};
        const auto encode=[](const std::string& v){return std::to_string(v.size())+":"+v;};
        split.reference={"cut","boolean:cut:split-edge:from:"+encode(edge.reference.owner_id)+encode(edge.reference.semantic_key)+encode("")+":between:faces:ends:vertices",edge.reference.instance_path};
        auto split_geometry=geometry;split_geometry.original_references.edges={edge};split_geometry.edges={split};
        auto split_view=attached_view;drawing::capture_measurement_geometry(split_view,split_geometry);split_view.projected_edges=drawing::project_edges(split_geometry,split_view.camera);
        drawing::MeasurementPickRequest request;request.mode=int(drawing::DimensionAttachmentKind::CurvePoint);
        const auto candidates=drawing::measurement_candidates(split_view,{7.3,0},.01,request);
        check(!candidates.empty(),"Symbol split contact not offered");
        auto attached=symbol;drawing::attach_symbol_to_view(attached,split_view,candidates.front().attachment);
        check(attached.reference->owner_id==edge.reference.owner_id&&attached.reference->semantic_key==edge.reference.semantic_key&&attached.reference->instance_path==edge.reference.instance_path,"Symbol did not inherit original edge identity");
        check(std::abs(candidates.front().attachment.parameter-.73)<1e-8,"Symbol contact parameter changed position");
    }
    symbols::Placement direct=symbol;direct.symbol.id="sheet-symbol";
    drawing::attach_symbol_to_view(direct,attached_view,attachment);sheet.symbol_annotations={direct};sheet.symbol_contacts[direct.symbol.id]={attached_view.id,.25};
    check(direct.frame.origin==kernel::Vec3{95,80,0}&&direct.frame.x==kernel::Vec3{-1,0,0},"Sheet contact mirrored local symbol geometry");
    sheet.views.front().x+=10;drawing::refresh_symbol_contacts(sheet);
    check(sheet.symbol_annotations.front().frame.origin==kernel::Vec3{105,80,0},"Sheet symbol did not follow its view");
    const auto path=std::filesystem::temp_directory_path()/("zima-symbol-drawing-"+kernel::make_stable_id()+".drwz");
    sheet_document.save(path);const auto reopened=drawing::DrawingDocument::load(path);std::filesystem::remove(path);
    check(reopened.sheets.front().symbol_contacts==sheet.symbol_contacts&&reopened.sheets.front().symbol_annotations==sheet.symbol_annotations,"Sheet symbol lost projected reference or pose on reopen");
    const auto last=sheet.symbol_annotations.front().frame;sheet.views.clear();drawing::refresh_symbol_contacts(sheet);
    check(sheet.symbol_annotations.front().unresolved&&sheet.symbol_annotations.front().frame==last,"Deleted view moved or deleted attached symbol");
    sheet.views={attached_view};drawing::refresh_symbol_contacts(sheet);
    check(!sheet.symbol_annotations.front().unresolved,"Restored view did not repair symbol reference");
    {
        auto point_view=attached_view;auto points=geometry;
        points.original_references.points.push_back({{2,3,0},{"profile","original-point","occurrence"}});
        drawing::capture_measurement_geometry(point_view,points);
        drawing::DimensionAttachment target;target.kind=drawing::DimensionAttachmentKind::Point;target.reference={"profile","original-point","occurrence"};
        auto note=symbol;note.symbol.id="point-note";drawing::attach_symbol_to_view(note,point_view,target);
        check(note.reference->kind==symbols::ReferenceKind::Point,"Symbol point contact became an edge");
        auto document=drawing::DrawingDocument::create_default();auto& page=document.sheets.front();page.views={point_view};page.symbol_annotations={note};page.symbol_contacts[note.symbol.id]={point_view.id,0};
        drawing::refresh_symbol_contacts(page);check(!page.symbol_annotations.front().unresolved,"Point symbol did not resolve");
        document.save(path);const auto copy=drawing::DrawingDocument::load(path);std::filesystem::remove(path);
        check(copy.sheets.front().symbol_annotations==page.symbol_annotations,"Point symbol lost identity on save/reopen");
        points.original_references.points.clear();drawing::capture_measurement_geometry(page.views.front(),points);const auto previous=page.symbol_annotations.front().frame;
        drawing::refresh_symbol_contacts(page);check(page.symbol_annotations.front().unresolved&&page.symbol_annotations.front().frame==previous,"Missing point moved or deleted symbol");
    }
    std::cout<<"Drawing symbols: Show/Erase, paper size, source retention, layout and repeated occurrences passed\n";return 0;
}catch(const std::exception& error){std::cerr<<error.what()<<'\n';return 1;}}
