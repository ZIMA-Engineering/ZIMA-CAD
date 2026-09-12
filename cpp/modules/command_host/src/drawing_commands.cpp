#include <zima/workspace/drawing_view_operations.hpp>
#include <zima/command_host/host.hpp>
#include <zima/workspace/drawing_operations.hpp>
#include <algorithm>
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
    return {{"view",v.id},{"sheet",sheet},{"name",v.name},{"source_document",v.source_document_id},{"source_path",document::path_to_utf8(v.source_path)},
        {"parent_view",v.parent_view_id},{"orientation",orientations.at(static_cast<std::size_t>(v.orientation))},{"projection_direction",directions.at(static_cast<std::size_t>(v.projection_direction))},
        {"camera",{{"horizontal",vector_json(v.camera.horizontal)},{"vertical",vector_json(v.camera.vertical)},{"depth",vector_json(v.camera.depth)}}},
        {"x_mm",v.x},{"y_mm",v.y},{"scale",v.scale},{"use_sheet_scale",v.use_sheet_scale},{"display_style",styles.at(static_cast<std::size_t>(v.display_style))},
        {"hidden_edge_style",v.hidden_edge_style==drawing::HiddenEdgeStyle::Dashed?"dashed":"gray"},{"tangent_edge_style",v.tangent_edge_style==drawing::TangentEdgeStyle::Visible?"visible":v.tangent_edge_style==drawing::TangentEdgeStyle::Thin?"thin":"hidden"},
        {"section",v.section_id},{"section_parent_view",v.section_parent_id},{"show_caption",v.show_caption},{"show_section_label",v.show_section_label},
        {"show_dimension_guides",v.show_dimension_guides},{"guide_offset_mm",v.dimension_guide_offset},{"guide_spacing_mm",v.dimension_guide_spacing},
        {"projected_edges",v.projected_edges.size()},{"projected_triangles",v.projected_triangles.size()},{"model_annotations",v.model_annotations.size()},
        {"measurement_curves",v.measurement_geometry->curves.size()},{"measurement_points",v.measurement_geometry->points.size()},{"value_locks",v.value_locks}};
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
                auto next=state->document();auto result=action(next,args,state->path);
                if(result.value("changed",true)){state->commit(std::move(next));change_=Change{ChangeKind::Model,id,true};}
                result["document"]=id;result["revision"]=state->revision();return Result::success(std::move(result));
            }catch(const workspace::DrawingOperationError& e){return Result::failure(e.code,tr(e.what()));}
             catch(const std::exception& e){return Result::failure("drawing_failed",tr(e.what()));}
        });
    };
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
    add({"drawing.view.delete",tr("Delete a drawing view, its projected descendants and their dimensions."),{{"view",true},{"document",false}},true},[](auto& doc,const Json& args,const auto&){
        const auto ids=workspace::delete_drawing_view(doc,args["view"].get<std::string>());return Json{{"removed",ids},{"changed",true}};
    });
    std::vector<commands::Argument> parameters={{"name",false},{"format",false},{"projection",false},{"scale",false,Type::Number},{"thick_line_mm",false,Type::Number},{"thin_line_mm",false,Type::Number},{"red_line_mm",false,Type::Number},{"locale",false},{"document",false}};
    add({"drawing.sheet.create",tr("Create a drawing sheet with explicit paper settings."),parameters,true},[](auto& doc,const Json& args,const auto&){workspace::SheetSettings initial;initial.name="List "+std::to_string(doc.sheets.size()+1);const auto id=workspace::create_drawing_sheet(doc,settings(args,initial));auto result=sheet_json(*doc.find_sheet(id));result["changed"]=true;return result;});
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
        add({name,remove?tr("Remove the embedded drawing template geometry."):tr("Load and embed a native drawing template."),std::move(args),true},[this,title,remove](auto& doc,const Json& args,const auto&){
            const auto id=args["sheet"].get<std::string>();bool changed=true;
            if(remove)changed=workspace::clear_drawing_template(doc,id,title);
            else{auto path=std::filesystem::u8path(args["path"].get<std::string>());if(path.is_relative())path=directory_/path;workspace::load_drawing_template(doc,id,path,title);}
            auto result=sheet_json(*doc.find_sheet(id));result["changed"]=changed;return result;
        });
    }
}
}
