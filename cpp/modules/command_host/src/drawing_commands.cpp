#include <zima/workspace/drawing_label_operations.hpp>
#include <zima/workspace/drawing_view_operations.hpp>
#include <zima/workspace/drawing_hatch_operations.hpp>
#include "section_component_input.hpp"
#include <zima/command_host/host.hpp>
#include <zima/workspace/drawing_operations.hpp>
#include <algorithm>
#include <cmath>
#include <zima/workspace/drawing_projection.hpp>
#include <zima/document/file_path.hpp>
namespace zima::command_host {
namespace {
constexpr std::array formats{"A4","A3","A2","A1","A0"};
Json sheet_json(const drawing::DrawingSheet& s){return {{"sheet",s.id},{"name",s.name},{"format",formats.at(static_cast<std::size_t>(s.format))},{"width_mm",s.width_mm()},{"height_mm",s.height_mm()},
    {"projection",s.projection_method==drawing::ProjectionMethod::FirstAngle?"first_angle":"third_angle"},{"scale",s.default_scale},{"thick_line_mm",s.thick_line_mm},{"thin_line_mm",s.thin_line_mm},{"red_line_mm",s.red_line_mm},{"locale",s.title_block_locale},
    {"views",s.views.size()},{"dimensions",s.dimensions.size()},{"frame_lines",s.frame_lines.size()},{"frame_circles",s.frame_circles.size()},{"frame_texts",s.frame_texts.size()},
    {"title_lines",s.title_block_lines.size()},{"title_circles",s.title_block_circles.size()},{"title_texts",s.title_block_texts.size()},{"title_fields",s.title_block_fields.size()},{"title_images",s.title_block_images.size()},{"repeat_regions",s.repeat_regions.size()}};}
Json vector_json(const kernel::Vec3& p){return Json::array({p.x,p.y,p.z});}
Json view_json(const drawing::DrawingView& v,const std::string& sheet){
    constexpr std::array orientations{"front","back","left","right","top","bottom","isometric"};
    constexpr std::array styles{"visible_edges","hidden_edges","shaded_with_edges","shaded"};
    constexpr std::array directions{"none","right","top_right","top","top_left","left","bottom_left","bottom","bottom_right"};
    Json breaks=Json::array();for(const auto& b:v.breaks)breaks.push_back({{"id",b.id},{"vertical",b.vertical},{"start",b.start},{"length",b.length},{"gap",b.gap},{"mark",int(b.mark)}});
    Json markers=Json::array();for(const auto& section:v.section_markers)markers.push_back(section.id);
    return {{"view",v.id},{"sheet",sheet},{"name",v.name},{"source_document",v.source_document_id},{"source_path",document::path_to_utf8(v.source_path)},
        {"parent_view",v.parent_view_id},{"orientation",orientations.at(static_cast<std::size_t>(v.orientation))},{"projection_direction",directions.at(static_cast<std::size_t>(v.projection_direction))},
        {"camera",{{"horizontal",vector_json(v.camera.horizontal)},{"vertical",vector_json(v.camera.vertical)},{"depth",vector_json(v.camera.depth)}}},
        {"x_mm",v.x},{"y_mm",v.y},{"scale",v.scale},{"use_sheet_scale",v.use_sheet_scale},{"display_style",styles.at(static_cast<std::size_t>(v.display_style))},
        {"hidden_edge_style",v.hidden_edge_style==drawing::HiddenEdgeStyle::Dashed?"dashed":"gray"},{"tangent_edge_style",v.tangent_edge_style==drawing::TangentEdgeStyle::Visible?"visible":v.tangent_edge_style==drawing::TangentEdgeStyle::Thin?"thin":"hidden"},
        {"section",v.section_id},{"section_markers",std::move(markers)},{"hidden_hatch_components",v.hidden_hatch_components},{"section_parent_view",v.section_parent_id},{"show_caption",v.show_caption},{"show_section_label",v.show_section_label},
        {"show_dimension_guides",v.show_dimension_guides},{"guide_offset_mm",v.dimension_guide_offset},{"guide_spacing_mm",v.dimension_guide_spacing},{"guide_count",v.dimension_guide_count},{"breaks",breaks},
        {"projected_edges",v.projected_edges.size()},{"projected_triangles",v.projected_triangles.size()},{"model_annotations",v.model_annotations.size()},
        {"measurement_curves",v.measurement_geometry->curves.size()},{"measurement_points",v.measurement_geometry->points.size()},{"value_locks",v.value_locks}};
}
Json hatch_json(const drawing::DrawingView& view,const document::SectionDefinition& source,std::size_t limit=10000) {
    constexpr std::array modes{"cut_hatch","cut_only","uncut"},patterns{"parallel","cross","dashed"};
    const auto components=workspace::drawing_section_components(view,source);auto items=Json::array();
    for(const auto& [key,value]:components) {
        if(items.size()>=limit)break;
        const auto stored=source.components.find(key);const auto hatch=document::section_component_hatch(source,key);
        items.push_back({{"component",key},{"name",source.component_names.contains(key)?source.component_names.at(key):""},
            {"available",source.component_names.contains(key)},{"mode",modes.at(value.mode)},
            {"source_mode",modes.at(stored==source.components.end()?0:stored->second.mode)},
            {"hidden_in_view",view.hidden_hatch_components.contains(key)},{"custom_hatch",value.custom_hatch},
            {"hatch",{{"pattern",patterns.at(hatch.pattern)},{"angle_degrees",hatch.angle},{"spacing_mm",hatch.spacing_mm},{"offset_mm",hatch.offset_mm}}}});
    }
    return {{"view",view.id},{"source_document",view.source_document_id},{"section",source.id},{"items",std::move(items)},
        {"total",components.size()},{"length_unit","mm"},{"angle_unit","degrees"},{"body_calculated",false}};
}
const drawing::DrawingSheet& label_sheet(const drawing::DrawingDocument& doc,const std::string& id) {
    const drawing::DrawingSheet* found=nullptr;
    for(const auto& sheet:doc.sheets)for(const auto& view:sheet.views)if(view.id==id) {
        if(found)throw workspace::DrawingOperationError("ambiguous_reference","The drawing view reference is ambiguous.");found=&sheet;
    }
    if(!found)throw workspace::DrawingOperationError("view_not_found","The drawing view does not exist.");return *found;
}
std::vector<const drawing::DrawingView*> label_views(const drawing::DrawingSheet& sheet) {
    std::vector<const drawing::DrawingView*> views;for(const auto& view:sheet.views)views.push_back(&view);return views;
}
Json label_position(const std::optional<drawing::Point2>& p){return p?Json::array({p->x,p->y}):Json(nullptr);}
Json labels_json(const drawing::DrawingDocument& doc,const std::string& id) {
    const auto& sheet=label_sheet(doc,id);const auto& view=*doc.find_view(id);const auto views=label_views(sheet);Json markers=Json::array();
    for(const auto& marker:view.section_markers) {
        workspace::drawing_section_marker(view,marker.id);
        const auto layout=drawing::section_trace_layout(view,marker,views);const auto stored=view.section_marker_offsets.find(marker.id);
        markers.push_back({{"section",marker.id},{"offsets_mm",stored==view.section_marker_offsets.end()?Json(nullptr):Json(stored->second)},
            {"displayable",layout.has_value()},{"effective_offsets_mm",layout?Json(layout->end_offsets):Json(nullptr)},
            {"minimum_offsets_mm",layout?Json(layout->minimum_offsets):Json(nullptr)}});
    }
    return {{"view",id},{"sheet",sheet.id},{"caption_position_mm",label_position(view.caption_position)},
        {"section_label_position_mm",label_position(view.section_label_position)},{"show_caption",view.show_caption},{"show_section_label",view.show_section_label},
        {"markers",std::move(markers)},{"length_unit","mm"},{"coordinates","view_origin_right_up"},{"body_calculated",false}};
}
void invalid_label_arguments(){throw workspace::DrawingOperationError("invalid_arguments","Invalid drawing label arguments.");}
std::optional<drawing::Point2> parse_label_position(const Json& value) {
    if(value.is_null())return {};if(!value.is_array()||value.size()!=2)invalid_label_arguments();
    for(const auto& n:value)if(!n.is_number()||!std::isfinite(n.get<double>()))invalid_label_arguments();
    return drawing::Point2{value[0].get<double>(),value[1].get<double>()};
}
void invalid_view_arguments(){throw workspace::DrawingOperationError("invalid_arguments","Invalid drawing view parameters.");}
template<class Enum,std::size_t N> void enum_argument(const Json& args,const char* key,Enum& target,const std::array<const char*,N>& names) {
    if(!args.contains(key))return;const auto text=args[key].get<std::string>();const auto found=std::ranges::find(names,text);
    if(found==names.end())invalid_view_arguments();target=static_cast<Enum>(found-names.begin());
}
void view_settings(drawing::DrawingView& value,const Json& args) {
    value.name=args.value("name",value.name);value.x=args.value("x_mm",value.x);value.y=args.value("y_mm",value.y);
    value.scale=args.value("scale",value.scale);value.use_sheet_scale=args.value("use_sheet_scale",args.contains("scale")?false:value.use_sheet_scale);
    if(args.contains("scale")&&value.use_sheet_scale)invalid_view_arguments();
    value.show_caption=args.value("show_caption",value.show_caption);value.show_section_label=args.value("show_section_label",value.show_section_label);
    if(args.contains("breaks")){value.breaks.clear();for(const auto& b:args.at("breaks"))value.breaks.push_back({b.at("id"),b.at("vertical"),b.at("start"),b.at("length"),b.at("gap"),static_cast<drawing::BreakMark>(b.at("mark").get<int>())});}
    value.show_dimension_guides=args.value("show_dimension_guides",value.show_dimension_guides);
    value.dimension_guide_count=args.value("guide_count",value.dimension_guide_count);value.dimension_guide_offset=args.value("guide_offset_mm",value.dimension_guide_offset);value.dimension_guide_spacing=args.value("guide_spacing_mm",value.dimension_guide_spacing);
    enum_argument(args,"orientation",value.orientation,std::array{"front","back","left","right","top","bottom","isometric"});
    enum_argument(args,"display_style",value.display_style,std::array{"visible_edges","hidden_edges","shaded_with_edges","shaded"});
    enum_argument(args,"hidden_edge_style",value.hidden_edge_style,std::array{"dashed","gray"});
    enum_argument(args,"tangent_edge_style",value.tangent_edge_style,std::array{"visible","thin","hidden"});
    enum_argument(args,"projection_direction",value.projection_direction,std::array{"none","right","top_right","top","top_left","left","bottom_left","bottom","bottom_right"});
    if(args.contains("orientation"))value.camera=drawing::standard_camera(value.orientation);
    if(args.contains("camera")) {
        if(args.contains("orientation"))invalid_view_arguments();const auto& camera=args["camera"];
        if(camera.size()!=3||!camera.contains("horizontal")||!camera.contains("vertical")||!camera.contains("depth"))invalid_view_arguments();
        const auto vector=[](const Json& row){if(!row.is_array()||row.size()!=3)invalid_view_arguments();for(const auto& n:row)if(!n.is_number())invalid_view_arguments();return kernel::Vec3{row[0].get<double>(),row[1].get<double>(),row[2].get<double>()};};
        value.camera={vector(camera["horizontal"]),vector(camera["vertical"]),vector(camera["depth"])};
    }
    if(args.contains("value_locks")) {
        value.value_locks.clear();for(const auto& key:args["value_locks"]){if(!key.is_string()||!value.value_locks.insert(key.get<std::string>()).second)invalid_view_arguments();}
    }
    if(args.contains("hidden_hatch_components")) {
        value.hidden_hatch_components.clear();for(const auto& key:args["hidden_hatch_components"]){if(!key.is_string()||key.get<std::string>().empty()||!value.hidden_hatch_components.insert(key.get<std::string>()).second)invalid_view_arguments();}
    }
    if(args.contains("section")){value.section_id=args["section"].get<std::string>();value.section_snapshot.reset();value.section_parent_id.clear();}
}
workspace::SheetSettings settings(const Json& args,workspace::SheetSettings value){
    if(args.contains("name"))value.name=args["name"].get<std::string>();
    if(args.contains("format")){const auto name=args["format"].get<std::string>();const auto found=std::ranges::find(formats,name);if(found==formats.end())throw workspace::DrawingOperationError("invalid_arguments","Invalid drawing sheet format or projection method.");value.format=static_cast<drawing::SheetFormat>(found-formats.begin());}
    if(args.contains("projection")){const auto name=args["projection"].get<std::string>();if(name!="first_angle"&&name!="third_angle")throw workspace::DrawingOperationError("invalid_arguments","Invalid drawing sheet format or projection method.");value.projection=name=="first_angle"?drawing::ProjectionMethod::FirstAngle:drawing::ProjectionMethod::ThirdAngle;}
    value.scale=args.value("scale",value.scale);value.thick_line_mm=args.value("thick_line_mm",value.thick_line_mm);value.thin_line_mm=args.value("thin_line_mm",value.thin_line_mm);value.red_line_mm=args.value("red_line_mm",value.red_line_mm);value.locale=args.value("locale",value.locale);return value;
}
}
void Host::register_drawing_commands(){
    using Type=commands::ArgumentType;
    const auto add=[this](commands::Command command,std::function<Json(drawing::DrawingDocument&,const Json&,const std::filesystem::path&)> action){
        const bool changes=command.changes_state;
        dispatcher_.add(std::move(command),[this,changes,action](const Json& args){
            if(changes){const auto checked=target(args);if(!checked.ok)return checked;}
            const auto id=args.value("document",workspace_.active_document_id());auto* state=workspace_.open_drawing(id);
            if(!state)return Result::failure("unsupported_document",tr("Drawing commands require an open Drawing."));
            try{
                // Queries are registered separately below and never copy projected geometry.
                auto next=state->document();const auto path=state->path;auto result=action(next,args,path);
                // Source hatch edits may open a source and reallocate Workspace storage.
                state=workspace_.open_drawing(id);
                if(result.value("changed",true)){state->commit(std::move(next));change_=Change{ChangeKind::Model,id,true};}
                result["document"]=id;result["revision"]=state->revision();return Result::success(std::move(result));
            }catch(const workspace::DrawingOperationError& e){return Result::failure(e.code,tr(e.what()));}
             catch(const std::exception& e){return Result::failure("drawing_failed",tr(e.what()));}
        });
    };
    dispatcher_.add({"drawing.view.labels.get",tr("Read stored view labels and Section trace end positions without loading sources."),
        {{"view",true},{"document",false}},false},[this](const Json& args) {
        const auto id=args.value("document",workspace_.active_document_id());const auto* state=workspace_.open_drawing(id);
        if(!state)return Result::failure("unsupported_document",tr("Drawing commands require an open Drawing."));
        try {auto result=labels_json(state->document(),args.at("view").get<std::string>());
            result["document"]=id;result["revision"]=state->revision();return Result::success(std::move(result));
        }catch(const workspace::DrawingOperationError& e){return Result::failure(e.code,tr(e.what()));}
         catch(const std::exception& e){return Result::failure("drawing_failed",tr(e.what()));}
    });
    add({"drawing.view.labels.set",tr("Move view labels and Section trace ends in paper millimetres without projection."),
        {{"view",true},{"values",true,Type::Object},{"document",false}},true},[](auto& doc,const Json& args,const auto&) {
        const auto id=args.at("view").template get<std::string>();const auto& sheet=label_sheet(doc,id);auto& view=*doc.find_view(id);
        const auto& values=args.at("values");
        for(auto it=values.begin();it!=values.end();++it)if(it.key()!="caption_position_mm"&&it.key()!="section_label_position_mm"&&it.key()!="markers")invalid_label_arguments();
        const auto caption=view.caption_position,section_label=view.section_label_position;const auto offsets=view.section_marker_offsets;
        if(values.contains("caption_position_mm"))workspace::set_drawing_label_position(view,workspace::DrawingLabel::Caption,parse_label_position(values["caption_position_mm"]));
        if(values.contains("section_label_position_mm"))workspace::set_drawing_label_position(view,workspace::DrawingLabel::Section,parse_label_position(values["section_label_position_mm"]));
        if(values.contains("markers")) {
            const auto& markers=values["markers"];if(!markers.is_array()||markers.size()>10000)invalid_label_arguments();std::set<std::string> seen;
            const auto views=label_views(sheet);
            for(const auto& row:markers) {
                if(!row.is_object()||row.size()!=2||!row.contains("section")||!row["section"].is_string()||!row.contains("offsets_mm"))invalid_label_arguments();
                const auto key=row["section"].template get<std::string>();if(!seen.insert(key).second)invalid_label_arguments();
                const auto& marker=workspace::drawing_section_marker(view,key);const auto requested=parse_label_position(row["offsets_mm"]);
                if(!requested){workspace::reset_drawing_section_ends(view,key);continue;}
                // Both ends can share one segment. The second must respect the
                // first end's new position, using the same paper layout as the GUI.
                for(std::size_t end=0;end<2;++end) {
                    const auto layout=drawing::section_trace_layout(view,marker,views);
                    if(!layout)throw workspace::DrawingOperationError("trace_unavailable","The Section trace cannot be displayed in this view.");
                    workspace::set_drawing_section_end(view,key,end,end?requested->y:requested->x,layout->minimum_offsets[end]);
                }
            }
        }
        auto result=labels_json(doc,id);result["changed"]=caption!=view.caption_position||section_label!=view.section_label_position||offsets!=view.section_marker_offsets;return result;
    });
    dispatcher_.add({"drawing.view.hatch.get",tr("Read current source hatch styles and local drawing visibility."),
        {{"view",true},{"limit",false,Type::Integer},{"document",false}},false},[this](const Json& args) {
        const auto id=args.value("document",workspace_.active_document_id());const auto* state=workspace_.open_drawing(id);
        if(!state)return Result::failure("unsupported_document",tr("Drawing commands require an open Drawing."));
        try {
            const auto limit=args.value("limit",2000LL);if(limit<1||limit>10000)invalid_view_arguments();
            const auto* view=state->document().find_view(args.at("view").get<std::string>());
            if(!view)throw workspace::DrawingOperationError("view_not_found","The drawing view does not exist.");
            auto result=hatch_json(*view,workspace::drawing_source_section(&workspace_,*view,state->path),static_cast<std::size_t>(limit));
            result["document"]=id;result["revision"]=state->revision();return Result::success(std::move(result));
        }catch(const workspace::DrawingOperationError& e){return Result::failure(e.code,tr(e.what()));}
         catch(const std::exception& e){return Result::failure("drawing_failed",tr(e.what()));}
    });
    add({"drawing.view.hatch.set",tr("Change source hatch styles and local drawing visibility in one confirmed edit."),
        {{"view",true},{"components",true,Type::Array},{"document",false}},true},[this](auto& doc,const Json& args,const auto& path) {
        const auto id=args.at("view").get<std::string>();const auto* original=doc.find_view(id);
        if(!original)throw workspace::DrawingOperationError("view_not_found","The drawing view does not exist.");
        if(args.at("components").empty())throw workspace::DrawingOperationError("invalid_arguments","Specify at least one Section component.");
        const auto source=workspace::drawing_source_section(&workspace_,*original,path);
        auto table=source;table.components=workspace::drawing_section_components(*original,source);
        patch_section_components<workspace::DrawingOperationError>(table,args.at("components"));
        workspace::SectionComponents patch;
        for(const auto& row:args.at("components")){const auto key=row.at("component").get<std::string>();patch.emplace(key,table.components.at(key));}
        auto value=*original;workspace::set_drawing_section_components(value,source,patch);
        auto result=hatch_json(value,*value.section_snapshot);
        const bool source_changed=value.section_snapshot->components!=source.components;
        const bool changed=source_changed||value.hidden_hatch_components!=original->hidden_hatch_components;
        result["source_changed"]=source_changed;result["changed"]=changed;
        if(!changed)return result;
        std::string sheet_id;for(const auto& sheet:doc.sheets)if(std::ranges::any_of(sheet.views,[&](const auto& view){return view.id==id;})){sheet_id=sheet.id;break;}
        workspace::DrawingProjection projection(&workspace_,path);
        workspace::edit_drawing_view(doc,sheet_id,value,false,projection,true);
        const auto* accepted=doc.find_view(id);
        auto commit=workspace::prepare_section_component_commit(&workspace_,value.source_document_id,projection.source(*accepted).path,*accepted->section_snapshot,&source);
        commit();return result;
    });
    for(bool single:{false,true})dispatcher_.add({single?"drawing.sheet.get":"drawing.sheet.list",single?tr("Read the stored properties of one drawing sheet."):tr("List stored drawing sheets without projection or source loading."),single?std::vector<commands::Argument>{{"sheet",true},{"document",false}}:std::vector<commands::Argument>{{"document",false}},false},[this,single](const Json& args){
        const auto id=args.value("document",workspace_.active_document_id());const auto* state=workspace_.open_drawing(id);if(!state)return Result::failure("unsupported_document",tr("Drawing commands require an open Drawing."));
        if(single){const auto* sheet=state->document().find_sheet(args["sheet"].get<std::string>());if(!sheet)return Result::failure("sheet_not_found",tr("The drawing sheet does not exist."));auto result=sheet_json(*sheet);result["document"]=id;result["revision"]=state->revision();return Result::success(std::move(result));}
        Json items=Json::array();for(const auto& sheet:state->document().sheets)items.push_back(sheet_json(sheet));return Result::success({{"document",id},{"revision",state->revision()},{"items",std::move(items)}});
    });
    for(const auto& mode:{std::string("list"),std::string("get"),std::string("references")}) {
        std::vector<commands::Argument> parameters;
        if(mode=="list")parameters.push_back({"sheet",false});else parameters.push_back({"view",true});
        if(mode=="references")parameters.push_back({"kind",false});
        if(mode!="get")parameters.push_back({"limit",false,Type::Integer});parameters.push_back({"document",false});
        dispatcher_.add({"drawing.view."+mode,mode=="references"?tr("List original measuring references stored with a drawing view."):tr("Read stored drawing views without loading source models."),std::move(parameters),false},[this,mode](const Json& args){
            const auto id=args.value("document",workspace_.active_document_id());const auto* state=workspace_.open_drawing(id);
            if(!state)return Result::failure("unsupported_document",tr("Drawing commands require an open Drawing."));
            const auto& doc=state->document();const auto limit=args.value("limit",2000.0);
            if(limit<1||limit>10000)return Result::failure("invalid_arguments",tr("Drawing query limit must be from 1 to 10000."));
            if(args.contains("sheet")&&!doc.find_sheet(args["sheet"].get<std::string>()))return Result::failure("sheet_not_found",tr("The drawing sheet does not exist."));
            const drawing::DrawingView* selected=nullptr;std::string selected_sheet;
            if(mode!="list"){
                for(const auto& sheet:doc.sheets)for(const auto& view:sheet.views)if(view.id==args["view"].get<std::string>()){selected=&view;selected_sheet=sheet.id;}
                if(!selected)return Result::failure("view_not_found",tr("The drawing view does not exist."));
            }
            if(mode=="get"){auto result=view_json(*selected,selected_sheet);result["document"]=id;result["revision"]=state->revision();return Result::success(std::move(result));}
            Json items=Json::array();std::size_t total=0;const auto append=[&](Json row){++total;if(items.size()<static_cast<std::size_t>(limit))items.push_back(std::move(row));};
            if(mode=="list"){
                for(const auto& sheet:doc.sheets)if(!args.contains("sheet")||sheet.id==args["sheet"].get<std::string>())for(const auto& view:sheet.views)append(view_json(view,sheet.id));
            }else{
                const auto kind=args.value("kind",std::string("all"));if(kind!="all"&&kind!="curve"&&kind!="point")return Result::failure("invalid_arguments",tr("Drawing reference kind must be all, curve or point."));
                const auto reference=[](const auto& ref){return Json{{"owner",ref.owner_id},{"key",ref.semantic_key},{"instance_path",ref.instance_path}};};
                if(kind!="point")for(const auto& curve:selected->measurement_geometry->curves)if(curve.source.valid()){
                    auto row=reference(curve.source);row["kind"]="curve";row["line"]=curve.line;row["sample_count"]=curve.points.size();
                    row["first"]=curve.points.empty()?Json(nullptr):vector_json(curve.points.front());row["last"]=curve.points.empty()?Json(nullptr):vector_json(curve.points.back());
                    if(curve.circle)row["circle"]={{"center",vector_json(curve.circle->center)},{"normal",vector_json(curve.circle->normal)},{"radial",vector_json(curve.circle->radial)},{"radius_mm",curve.circle->radius}};else row["circle"]=nullptr;
                    append(std::move(row));
                }
                if(kind!="curve")for(const auto& point:selected->measurement_geometry->points)if(point.source.valid()){
                    auto row=reference(point.source);row["kind"]="point";row["position"]=vector_json(point.position);append(std::move(row));
                }
            }
            Json result={{"document",id},{"revision",state->revision()},{"items",std::move(items)},{"total",total}};
            if(selected){result["view"]=selected->id;result["source_document"]=selected->source_document_id;result["coordinate_system"]="source_model_mm";}
            return Result::success(std::move(result));
        });
    }
    for(bool creating:{true,false}) {
        std::vector<commands::Argument> parameters={{creating?"sheet":"view",true},{"source",false},{"name",false},{"orientation",false},{"camera",false,Type::Object},
            {"x_mm",false,Type::Number},{"y_mm",false,Type::Number},{"scale",false,Type::Number},{"use_sheet_scale",false,Type::Boolean},
            {"display_style",false},{"hidden_edge_style",false},{"tangent_edge_style",false},{"show_caption",false,Type::Boolean},{"show_section_label",false,Type::Boolean},
            {"show_dimension_guides",false,Type::Boolean},{"guide_offset_mm",false,Type::Number},{"guide_spacing_mm",false,Type::Number},{"guide_count",false,Type::Integer},{"breaks",false,Type::Array},
            {"section",false},{"section_markers",false,Type::Array},{"hidden_hatch_components",false,Type::Array},{"value_locks",false,Type::Array},{"distance_mm",false,Type::Number},{"document",false}};
        if(creating){parameters.push_back({"parent_view",false});parameters.push_back({"projection_direction",false});}
        add({creating?"drawing.view.create":"drawing.view.set",creating?tr("Create a drawing view from a calculated source or a parent view."):tr("Edit a drawing view and update its projected descendants."),std::move(parameters),true},[this,creating](auto& doc,const Json& args,const auto& document_path){
            std::string sheet_id;drawing::DrawingView value;
            if(creating) {
                sheet_id=args["sheet"].get<std::string>();
                if(!doc.find_sheet(sheet_id))throw workspace::DrawingOperationError("sheet_not_found","The drawing sheet does not exist.");
                std::string source=args.value("source",std::string{});std::filesystem::path source_path;
                const auto parent_id=args.value("parent_view",std::string{});const drawing::DrawingView* parent=nullptr;
                if(!parent_id.empty()) {
                    const auto& views=doc.find_sheet(sheet_id)->views;const auto found=std::ranges::find(views,parent_id,&drawing::DrawingView::id);
                    if(found==views.end())throw workspace::DrawingOperationError("view_not_found","The parent drawing view is unavailable.");parent=&*found;
                    source=parent->source_document_id;source_path=parent->source_path;
                }
                if(source.empty())throw workspace::DrawingOperationError("invalid_arguments","A source document or parent view is required.");
                value=drawing::DrawingDocument::create_view(source,source_path,{});
                value.name=workspace::next_drawing_view_name(doc);
                if(parent){value.parent_view_id=parent_id;value.scale=parent->scale;value.use_sheet_scale=parent->use_sheet_scale;value.display_style=parent->display_style;}
            }else{
                const auto* current=doc.find_view(args["view"].get<std::string>());if(!current)throw workspace::DrawingOperationError("view_not_found","The drawing view does not exist.");value=*current;
                for(const auto& sheet:doc.sheets)if(std::ranges::any_of(sheet.views,[&](const auto& v){return v.id==value.id;}))sheet_id=sheet.id;
            }
            if(!value.parent_view_id.empty()) {
                for(const auto* key:{"source","orientation","camera","x_mm","y_mm"})if(args.contains(key))throw workspace::DrawingOperationError("invalid_arguments","A projected view inherits its source and camera; use distance_mm for its position.");
            }else{
                if(args.contains("distance_mm")||args.contains("projection_direction"))invalid_view_arguments();
                if(creating||args.contains("source")) {
                    const auto source=args.value("source",value.source_document_id);std::filesystem::path path;
                    if(const auto* part=workspace_.open_part(source))path=part->path;
                    else if(const auto* assembly=workspace_.open_assembly(source))path=assembly->path;
                    else throw workspace::DrawingOperationError("source_unavailable","The drawing source must be an open Part or Assembly.");
                    if(value.source_document_id!=source){value.section_id.clear();value.section_snapshot.reset();value.section_markers.clear();value.section_parent_id.clear();value.hidden_hatch_components.clear();}
                    value.source_document_id=source;value.source_path=std::move(path);
                }
            }
            view_settings(value,args);
            if(!value.parent_view_id.empty()) {
                const auto* parent=doc.find_view(value.parent_view_id);
                if(!parent||value.projection_direction==drawing::ProjectionDirection::None)invalid_view_arguments();
                if(creating||args.contains("distance_mm")) {
                    if(!args.contains("distance_mm"))invalid_view_arguments();const auto distance=args["distance_mm"].get<double>();
                    if(!std::isfinite(distance)||distance<.001||distance>10000)invalid_view_arguments();
                    const auto offset=workspace::projection_placement(value.projection_direction,distance);value.x=parent->x+offset.x;value.y=parent->y+offset.y;
                }
            }
            workspace::DrawingProjection projection(&workspace_,document_path);
            if(args.contains("section_markers")) {
                value.section_markers.clear();const auto& sections=projection.source(value).sections;std::set<std::string> seen;
                for(const auto& key:args["section_markers"]){if(!key.is_string()||!seen.insert(key.get<std::string>()).second)invalid_view_arguments();const auto found=std::ranges::find(sections,key.get<std::string>(),&document::SectionDefinition::id);
                    if(found==sections.end())throw workspace::DrawingOperationError("section_not_found","A source section trace no longer exists. Edit the drawing view first.");value.section_markers.push_back(*found);}
            }
            workspace::edit_drawing_view(doc,sheet_id,value,creating,projection);
            auto result=view_json(*doc.find_view(value.id),sheet_id);result["changed"]=true;return result;
        });
    }
    add({"drawing.view.delete",tr("Delete a drawing view, its projected descendants and their dimensions."),{{"view",true},{"document",false}},true},[](auto& doc,const Json& args,const auto&){
        const auto ids=workspace::delete_drawing_view(doc,args["view"].get<std::string>());return Json{{"removed",ids},{"changed",true}};
    });
    std::vector<commands::Argument> parameters={{"name",false},{"format",false},{"projection",false},{"scale",false,Type::Number},{"thick_line_mm",false,Type::Number},{"thin_line_mm",false,Type::Number},{"red_line_mm",false,Type::Number},{"locale",false},{"document",false}};
    add({"drawing.sheet.create",tr("Create a drawing sheet with explicit paper settings."),parameters,true},[this](auto& doc,const Json& args,const auto&){workspace::SheetSettings initial;initial.name=tr("List")+" "+std::to_string(doc.sheets.size()+1);const auto id=workspace::create_drawing_sheet(doc,settings(args,initial));auto result=sheet_json(*doc.find_sheet(id));result["changed"]=true;return result;});
    parameters.insert(parameters.begin(),{"sheet",true});
    add({"drawing.sheet.set",tr("Edit drawing sheet settings through the shared sheet operation."),parameters,true},[this](auto& doc,const Json& args,const auto& document_path){
        const auto id=args["sheet"].get<std::string>();const auto* sheet=doc.find_sheet(id);if(!sheet)throw workspace::DrawingOperationError("sheet_not_found","The drawing sheet does not exist.");
        const bool changed=workspace::set_drawing_sheet(doc,id,settings(args,workspace::sheet_settings(*sheet)),[&](const auto& view){auto path=view.source_path;if(path.is_relative()&&!document_path.empty())path=document_path.parent_path()/path;return workspace::read_drawing_source(&workspace_,path,view.source_document_id).second;});
        auto result=sheet_json(*doc.find_sheet(id));result["changed"]=changed;return result;
    });
    add({"drawing.sheet.delete",tr("Delete one drawing sheet and its contained views and dimensions."),{{"sheet",true},{"document",false}},true},[](auto& doc,const Json& args,const auto&){const auto id=args["sheet"].get<std::string>();workspace::delete_drawing_sheet(doc,id);return Json{{"sheet",id},{"changed",true}};});
    for(bool title:{false,true})for(bool remove:{false,true}){
        const auto name=std::string("drawing.")+(title?"title_block.":"frame.")+(remove?"clear":"load");
        std::vector<commands::Argument> args={{"sheet",true}};if(!remove)args.push_back({"path",true});args.push_back({"document",false});
        add({name,remove?tr("Remove the embedded drawing template geometry."):tr("Load and embed a native drawing template."),std::move(args),true},[this,title,remove](auto& doc,const Json& args,const auto& document_path){
            const auto id=args["sheet"].get<std::string>();bool changed=true;
            if(remove)changed=workspace::clear_drawing_template(doc,id,title);
            else{auto path=std::filesystem::u8path(args["path"].get<std::string>());if(path.is_relative())path=directory_/path;workspace::load_drawing_template(doc,id,path,title,&workspace_,document_path);}
            auto result=sheet_json(*doc.find_sheet(id));result["changed"]=changed;return result;
        });
    }
}
}
