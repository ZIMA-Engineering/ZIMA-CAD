#include <zima/workspace/drawing_operations.hpp>
#include <zima/workspace/family_operations.hpp>
#include <zima/workspace/drawing_sources.hpp>
#include <zima/drawing/balloon.hpp>
#include <zima/drawing/measurement_dimension.hpp>
#include <algorithm>
#include <cmath>
#include <cctype>
namespace zima::workspace {
namespace {
kernel::ViewerMesh drawing_assembly_mesh(const assembly::AssemblyDocument& document) {
    auto mesh=document.build_drawing_scene();
    std::set<std::string> skeletons;
    const auto visit=[&](auto&& self,const auto& nodes,assembly::InstancePath parent)->void {
        for(const auto& node:nodes){const auto path=parent.child(node.occurrence_id);
            if(assembly::is_skeleton(node))skeletons.insert(path.encoded());
            self(self,node.children,path);
        }
    };
    visit(visit,document.occurrence_snapshot(),{});
    if(skeletons.empty())return mesh;
    const auto hidden=[&](const auto& reference){return skeletons.contains(reference.instance_path);};
    const auto filter=[&](auto& geometry) {
        std::vector<std::uint32_t> triangles;
        std::vector<kernel::FaceReference> references;
        for(std::size_t i=0;i<geometry.triangles.size()/3;++i) {
            const auto reference=i<geometry.triangle_references.size()?geometry.triangle_references[i]:kernel::FaceReference{};
            if(hidden(reference))continue;
            references.push_back(reference);
            triangles.insert(triangles.end(),geometry.triangles.begin()+3*i,geometry.triangles.begin()+3*i+3);
        }
        std::vector<kernel::Vec3> vertices;std::map<std::uint32_t,std::uint32_t> remap;
        for(auto& index:triangles){auto [it,inserted]=remap.emplace(index,static_cast<std::uint32_t>(vertices.size()));if(inserted)vertices.push_back(geometry.vertices.at(index));index=it->second;}
        geometry.vertices=std::move(vertices);geometry.triangles=std::move(triangles);geometry.triangle_references=std::move(references);
        std::erase_if(geometry.edges,[&](const auto& item){return hidden(item.reference);});
        std::erase_if(geometry.points,[&](const auto& item){return hidden(item.reference);});
        std::erase_if(geometry.axes,[&](const auto& item){return hidden(item.reference);});
    };
    filter(mesh);filter(mesh.original_references);
    std::erase_if(mesh.dimensions,[&](const auto& item){return hidden(item.reference);});
    std::erase_if(mesh.constraint_markers,[&](const auto& item){return hidden(item.reference);});
    std::erase_if(mesh.images,[&](const auto& item){return hidden(item.reference);});
    std::erase_if(mesh.annotation_frames,[&](const auto& item){return skeletons.contains(item.first.second);});
    return mesh;
}
drawing::DrawingSheet& sheet(drawing::DrawingDocument& doc,const std::string& id){auto* s=doc.find_sheet(id);if(!s)throw DrawingOperationError("sheet_not_found","The drawing sheet does not exist.");return *s;}
void text(const std::string& s,std::size_t max){if(s.empty()||s.size()>max||std::ranges::any_of(s,[](unsigned char c){return c<32||c==127;}))throw DrawingOperationError("invalid_arguments","Drawing names and language codes must be non-empty bounded single-line text.");}
void assign(drawing::DrawingSheet& s,const SheetSettings& v){s.name=v.name;s.format=v.format;s.projection_method=v.projection;s.default_scale=v.scale;s.thick_line_mm=v.thick_line_mm;s.thin_line_mm=v.thin_line_mm;s.red_line_mm=v.red_line_mm;s.title_block_locale=v.locale;}
bool clear(drawing::DrawingSheet& s,bool title){
    if(!title){const bool changed=!s.frame_lines.empty()||!s.frame_texts.empty()||!s.frame_circles.empty();s.frame_lines.clear();s.frame_texts.clear();s.frame_circles.clear();return changed;}
    const bool changed=!s.title_block_lines.empty()||!s.title_block_texts.empty()||!s.title_block_fields.empty()||!s.title_block_circles.empty()||!s.title_block_images.empty()||!s.repeat_regions.empty();
    s.title_block_lines.clear();s.title_block_texts.clear();s.title_block_fields.clear();s.title_block_circles.clear();s.title_block_images.clear();s.repeat_regions.clear();return changed;
}
}
SheetSettings sheet_settings(const drawing::DrawingSheet& s){return {s.name,s.format,s.projection_method,s.default_scale,s.thick_line_mm,s.thin_line_mm,s.red_line_mm,s.title_block_locale};}
void validate_sheet_settings(const SheetSettings& v){
    text(v.name,256);text(v.locale,32);
    if(v.format<drawing::SheetFormat::A4||v.format>drawing::SheetFormat::A0||v.projection<drawing::ProjectionMethod::FirstAngle||v.projection>drawing::ProjectionMethod::ThirdAngle)
        throw DrawingOperationError("invalid_arguments","Invalid drawing sheet format or projection method.");
    if(!std::isfinite(v.scale)||v.scale<.001||v.scale>1000)throw DrawingOperationError("invalid_arguments","Drawing sheet scale must be from 0.001 to 1000.");
    for(double width:{v.thick_line_mm,v.thin_line_mm,v.red_line_mm})if(!std::isfinite(width)||width<.05||width>2)throw DrawingOperationError("invalid_arguments","Drawing line widths must be from 0.05 to 2 mm.");
}
std::string create_drawing_sheet(drawing::DrawingDocument& doc,const SheetSettings& settings){
    validate_sheet_settings(settings);auto value=drawing::DrawingDocument::create_default().sheets.front();assign(value,settings);const auto id=value.id;doc.sheets.push_back(std::move(value));return id;
}
void delete_drawing_sheet(drawing::DrawingDocument& doc,const std::string& id){
    const auto& target=sheet(doc,id);if(doc.sheets.size()<=1)throw DrawingOperationError("last_sheet","The last drawing sheet cannot be removed.");
    std::set<std::string> views;for(const auto& view:target.views)views.insert(view.id);
    for(const auto& other:doc.sheets)if(other.id!=id)for(const auto& view:other.views)if(views.contains(view.parent_view_id)||views.contains(view.section_parent_id))
        throw DrawingOperationError("dependent_view","Another drawing sheet contains a dependent view.");
    doc.sources=doc.data_sources();
    std::erase_if(doc.sheets,[&](const auto& s){return s.id==id;});
}
bool set_drawing_sheet(drawing::DrawingDocument& doc,const std::string& id,const SheetSettings& settings,const DrawingSourceReader& source){
    validate_sheet_settings(settings);auto& target=sheet(doc,id);if(sheet_settings(target)==settings)return false;
    auto next=target;if(next.format!=settings.format){clear(next,false);clear(next,true);}assign(next,settings);
    for(auto& view:next.views)if(view.use_sheet_scale&&view.scale!=settings.scale){
        view.scale=settings.scale;
        if(!view.section_id.empty()){
            if(!source)throw DrawingOperationError("source_unavailable","A stored source model is required to update section hatching.");
            drawing::refresh_view_geometry(view,source(view));
            for(auto& dimension:next.dimensions)if(dimension.view_id==view.id)drawing::refresh_drawing_dimension(view,dimension);
        }
    }
    target=std::move(next);return true;
}
bool clear_drawing_template(drawing::DrawingDocument& doc,const std::string& id,bool title){return clear(sheet(doc,id),title);}
void load_drawing_template(drawing::DrawingDocument& doc,const std::string& id,const std::filesystem::path& path,bool title,const Workspace* live,const std::filesystem::path& drawing_path){
    auto& target=sheet(doc,id);auto next=target;
    auto ext=path.extension().string();std::ranges::transform(ext,ext.begin(),[](unsigned char c){return static_cast<char>(std::tolower(c));});
    if(ext!=(title?".tblz":".frmz"))throw DrawingOperationError("unsupported_format","Use a native frmz frame or tblz title-block template.");
    if(title) {
        drawing::load_title_block_template(next,path);
        next.bom_source_document_id=next.selected_source_document_id.empty()?doc.source_document_id:next.selected_source_document_id;
        auto source_path=doc.data_source_path(next.bom_source_document_id);
        if(source_path.is_relative()&&!drawing_path.empty())source_path=drawing_path.parent_path()/source_path;
        next.bom_rows=build_bom_rows_for_source(next.bom_source_document_id,source_path,live);
        drawing::refresh_balloons(next);
    } else drawing::load_frame_template(next,path);
    validate_sheet_settings(sheet_settings(next));target=std::move(next);
}
std::pair<std::string,kernel::ViewerMesh> read_drawing_source(const Workspace* live,const std::filesystem::path& path,const std::string& expected){
    const auto checked=[&](std::string id,kernel::ViewerMesh mesh){if(!expected.empty()&&id!=expected)throw DrawingOperationError("source_identity","The drawing source file belongs to a different document.");return std::pair{std::move(id),std::move(mesh)};};
    if(live){
        auto open=!expected.empty()&&live->find(expected)?std::optional<std::string>(expected):live->document_id_for_path(path);
        if(open && !expected.empty() && *open!=expected) {
            if(const auto* base=live->open_part(*open)) {
                std::vector<kernel::BodyResult> cache;auto member=family_part_source(base->session.document(),cache,expected);
                Workspace source;source.add_part(std::move(member),std::move(cache),base->path);
                return checked(expected,source.authoritative_viewer_mesh(expected));
            }
            if(const auto* base=live->open_assembly(*open))return checked(expected,drawing_assembly_mesh(family_assembly_source(base->session.document(),expected)));
        }
        if(open && (expected.empty()||*open==expected)){if(!live->open_part(*open)&&!live->open_assembly(*open))throw DrawingOperationError("unsupported_document","Drawing sources must be Parts or Assemblies.");if(const auto* assembly=live->open_assembly(*open))return checked(*open,drawing_assembly_mesh(assembly->session.document()));return checked(*open,live->authoritative_viewer_mesh(*open));}
    }
    auto ext=path.extension().string();std::ranges::transform(ext,ext.begin(),[](unsigned char c){return static_cast<char>(std::tolower(c));});
    if(ext==".prtz"){
        std::vector<kernel::BodyResult> boundaries;auto part=family_part_source(document::PartDocument::load(path,&boundaries),boundaries,expected);
        if(boundaries.empty()&&!part.kernel_operations().empty())throw DrawingOperationError("uncalculated_source","The Part has no saved calculated model. Regenerate and save it first.");
        const auto id=part.document_id;
        if(!expected.empty()&&id!=expected)throw DrawingOperationError("source_identity","The drawing source file belongs to a different document.");
        // Use the same persisted Sketch, construction and datum packet as an
        // open source. This temporary workspace never creates a user tab.
        Workspace source;source.add_part(std::move(part),std::move(boundaries),path);
        return checked(id,source.authoritative_viewer_mesh(id));
    }
    if(ext==".asmz"){const auto assembly=read_family_assembly(live,path,expected);return checked(assembly.document_id,drawing_assembly_mesh(assembly));}
    throw DrawingOperationError("unsupported_format","Drawing sources must be native prtz or asmz files.");
}
}
