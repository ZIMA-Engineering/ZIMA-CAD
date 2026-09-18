#include <zima/drawing/balloon.hpp>
#include <zima/drawing/measurement_dimension.hpp>
#include <zima/kernel/stable_id.hpp>
#include <nlohmann/json.hpp>
#include <algorithm>
#include <cmath>
#include <stdexcept>
namespace zima::drawing {
DrawingBalloon make_drawing_balloon() {DrawingBalloon result;result.id=kernel::make_stable_id();return result;}
namespace {
std::string family_root(const std::string& id) {return id.substr(0,id.find(":family:"));}
}
const BomRow* balloon_bom_row(const DrawingSheet& sheet,const DrawingView& view,const kernel::EdgeReference& ref) {
    if(!ref.valid()||sheet.bom_source_document_id.empty())return nullptr;
    const BomRow* result=nullptr;
    if(family_root(sheet.bom_source_document_id)==family_root(view.source_document_id)) {
        for(const auto& row:sheet.bom_rows)for(const auto& path:row.occurrence_paths)
            // Encoding includes each segment's length. A full immediate path can
            // only prefix itself or its descendants, never a similar sibling ID.
            if(path.empty()?ref.instance_path.empty():ref.instance_path.starts_with(path)) {
                if(result&&result!=&row)return nullptr;
                result=&row;
            }
    } else if(ref.instance_path.empty()) {
        // A separately inserted component view has no Assembly occurrence path.
        // Resolve it only when its source family identifies exactly one BOM row.
        for(const auto& row:sheet.bom_rows)
            if(family_root(row.source_document_id)==family_root(view.source_document_id)) {
                if(result&&result!=&row)return nullptr;
                result=&row;
            }
    }
    return result;
}
BalloonEvaluation evaluate_balloon(const DrawingSheet& sheet,const DrawingBalloon& value) {
    BalloonEvaluation result{value.last_anchor,value.item_number,true};
    const auto view=std::ranges::find(sheet.views,value.view_id,&DrawingView::id);
    if(view==sheet.views.end())return result;
    const auto* row=balloon_bom_row(sheet,*view,value.attachment.reference);
    const auto anchor=resolve_dimension_attachment(*view,value.attachment);
    if(anchor)result.anchor=anchor;
    if(row)result.item_number=row->item_number;
    result.unresolved=!row||row->item_number<=0||!anchor;
    return result;
}
void refresh_balloon(const DrawingSheet& sheet,DrawingBalloon& value) {
    const auto e=evaluate_balloon(sheet,value);
    value.last_anchor=e.anchor;value.item_number=e.item_number;value.unresolved=e.unresolved;
}
void refresh_balloons(DrawingSheet& sheet) {for(auto& b:sheet.balloons)refresh_balloon(sheet,b);}
void show_all_balloons(DrawingSheet& sheet,const std::string& view_id) {
    const auto view=std::ranges::find(sheet.views,view_id,&DrawingView::id);
    if(view==sheet.views.end())throw std::invalid_argument("The drawing view does not exist.");
    for(auto& b:sheet.balloons)if(b.view_id==view_id)b.visible=true;
    double right=0,top=0;
    for(const auto& edge:view->projected_edges)for(const auto& p:edge.points) {
        right=std::max(right,p.x*view->scale);top=std::max(top,p.y*view->scale);
    }
    std::set<const BomRow*> labelled;
    for(const auto& b:sheet.balloons)if(b.view_id==view_id)if(const auto* row=balloon_bom_row(sheet,*view,b.attachment.reference))labelled.insert(row);
    std::map<const BomRow*,std::vector<const ProjectedEdge*>> candidates;
    for(const auto& edge:view->projected_edges)if(drawing_edge_visible(*view,edge)&&!edge.hatch)
        if(const auto* row=balloon_bom_row(sheet,*view,edge.source);row&&!labelled.contains(row))candidates[row].push_back(&edge);
    std::size_t placed=0;
    for(const auto& row:sheet.bom_rows) {
        const auto found=candidates.find(&row);if(found==candidates.end())continue;
        for(const auto* edge:found->second) {
            auto b=make_drawing_balloon();b.view_id=view_id;b.attachment.reference=edge->source;b.attachment.parameter=.5;
            // Paper placement, independent of model/view scale. Use columns
            // when a long BOM would exceed the available sheet height.
            const double first_y=std::clamp(top+10,12-view->y,sheet.height_mm()-view->y-12);
            const auto rows=std::max(1,int((first_y-(12-view->y))/20)+1);
            do {
                b.position={std::min(right+20,view->x-12)-20*double(placed/rows),first_y-20*double(placed%rows)};
                ++placed;
            }while(std::ranges::any_of(sheet.balloons,[&](const auto& existing){return existing.visible&&existing.view_id==view_id&&std::hypot(existing.position.x-b.position.x,existing.position.y-b.position.y)<(existing.diameter+b.diameter)/2+4;}));
            refresh_balloon(sheet,b);if(b.unresolved)continue;
            sheet.balloons.push_back(std::move(b));break;
        }
    }
}
void erase_all_balloons(DrawingSheet& sheet,const std::string& view) {
    for(auto& b:sheet.balloons)if(b.view_id==view)b.visible=false;
}
void validate_balloon(const DrawingBalloon& value) {
    const auto finite_point=[](Point2 p){return std::isfinite(p.x)&&std::isfinite(p.y)&&std::abs(p.x)<=1e9&&std::abs(p.y)<=1e9;};
    if(value.id.empty()||value.view_id.empty()||!value.attachment.reference.valid()||
       value.attachment.kind!=DimensionAttachmentKind::CurvePoint||
       !std::isfinite(value.attachment.parameter)||value.attachment.parameter<0||value.attachment.parameter>1||
       !finite_point(value.position)||(value.last_anchor&&!finite_point(*value.last_anchor))||
       !std::isfinite(value.diameter)||value.diameter<2||value.diameter>100||
       !std::isfinite(value.text_height)||value.text_height<1||value.text_height>30||
       value.diameter<value.text_height*1.5||value.item_number<0)
        throw std::invalid_argument("Invalid balloon reference, position or size.");
}
std::string serialize_balloons(const std::vector<DrawingBalloon>& values) {
    auto rows=nlohmann::json::array();
    for(const auto& b:values) {
        validate_balloon(b);const auto& r=b.attachment.reference;
        rows.push_back({{"id",b.id},{"view",b.view_id},{"reference",{{"owner",r.owner_id},{"key",r.semantic_key},{"instance_path",r.instance_path}}},
            {"parameter",b.attachment.parameter},{"position",{b.position.x,b.position.y}},
            {"diameter",b.diameter},{"text_height",b.text_height},{"item_number",b.item_number},{"unresolved",b.unresolved},{"visible",b.visible},
            {"last_anchor",b.last_anchor?nlohmann::json::array({b.last_anchor->x,b.last_anchor->y}):nlohmann::json(nullptr)}});
    }
    return rows.dump();
}
std::vector<DrawingBalloon> deserialize_balloons(const std::string& data) {
    std::vector<DrawingBalloon> result;
    for(const auto& j:nlohmann::json::parse(data)) {
        DrawingBalloon b;b.id=j.at("id");b.view_id=j.at("view");const auto& r=j.at("reference");
        b.attachment.reference={r.at("owner"),r.at("key"),r.at("instance_path")};b.attachment.parameter=j.at("parameter");
        b.position={j.at("position").at(0),j.at("position").at(1)};b.diameter=j.at("diameter");b.text_height=j.at("text_height");
        b.item_number=j.at("item_number");b.unresolved=j.at("unresolved");b.visible=j.at("visible");
        if(!j.at("last_anchor").is_null())b.last_anchor=Point2{j.at("last_anchor").at(0),j.at("last_anchor").at(1)};
        validate_balloon(b);result.push_back(std::move(b));
    }
    return result;
}
}
