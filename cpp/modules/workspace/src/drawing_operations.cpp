#include <zima/workspace/drawing_operations.hpp>
#include <zima/drawing/measurement_dimension.hpp>
#include <algorithm>
#include <cmath>
#include <cctype>
namespace zima::workspace {
namespace {
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
void load_drawing_template(drawing::DrawingDocument& doc,const std::string& id,const std::filesystem::path& path,bool title){
    auto& target=sheet(doc,id);auto next=target;
    auto ext=path.extension().string();std::ranges::transform(ext,ext.begin(),[](unsigned char c){return static_cast<char>(std::tolower(c));});
    if(ext!=(title?".tblz":".frmz"))throw DrawingOperationError("unsupported_format","Use a native frmz frame or tblz title-block template.");
    if(title)drawing::load_title_block_template(next,path);else drawing::load_frame_template(next,path);
    validate_sheet_settings(sheet_settings(next));target=std::move(next);
}
std::pair<std::string,kernel::ViewerMesh> read_drawing_source(const Workspace* live,const std::filesystem::path& path,const std::string& expected){
    const auto checked=[&](std::string id,kernel::ViewerMesh mesh){if(!expected.empty()&&id!=expected)throw DrawingOperationError("source_identity","The drawing source file belongs to a different document.");return std::pair{std::move(id),std::move(mesh)};};
    if(live){
        auto open=!expected.empty()&&live->find(expected)?std::optional<std::string>(expected):live->document_id_for_path(path);
        if(open){if(!live->open_part(*open)&&!live->open_assembly(*open))throw DrawingOperationError("unsupported_document","Drawing sources must be Parts or Assemblies.");return checked(*open,live->authoritative_viewer_mesh(*open));}
    }
    auto ext=path.extension().string();std::ranges::transform(ext,ext.begin(),[](unsigned char c){return static_cast<char>(std::tolower(c));});
    if(ext==".prtz"){
        std::vector<kernel::BodyResult> boundaries;const auto part=document::PartDocument::load(path,&boundaries);
        if(boundaries.empty()&&!part.kernel_operations().empty())throw DrawingOperationError("uncalculated_source","The Part has no saved calculated model. Regenerate and save it first.");
        return checked(part.document_id,boundaries.empty()?kernel::ViewerMesh{}:std::move(boundaries.back().mesh));
    }
    if(ext==".asmz"){const auto assembly=assembly::AssemblyDocument::load(path);return checked(assembly.document_id,assembly.build_scene());}
    throw DrawingOperationError("unsupported_format","Drawing sources must be native prtz or asmz files.");
}
}
