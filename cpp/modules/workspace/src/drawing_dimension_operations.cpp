#include <zima/workspace/drawing_dimension_operations.hpp>
#include <algorithm>
#include <zima/workspace/drawing_operations.hpp>
#include <zima/drawing/measurement_dimension.hpp>
#include <zima/kernel/stable_id.hpp>
#include <zima/drawing/annotation_guides.hpp>
namespace zima::workspace {
namespace {
const drawing::DrawingDimension* alignment_dimension(const drawing::DrawingSheet& sheet,const std::string& id) {
    const auto it=std::ranges::find(sheet.dimensions,id,&drawing::DrawingDimension::id);
    return it==sheet.dimensions.end()?nullptr:&*it;
}
std::optional<kernel::ViewerDimension> alignment_geometry(const drawing::DrawingSheet& sheet,const drawing::DrawingDimension& d) {
    if(d.segments.size()!=1||d.chain_datum_only||(d.kind!=drawing::DrawingDimensionKind::Linear&&d.kind!=drawing::DrawingDimensionKind::Chain))return {};
    const auto view=std::ranges::find(sheet.views,d.view_id,&drawing::DrawingView::id);if(view==sheet.views.end())return {};
    const auto evaluation=drawing::evaluate_drawing_dimension(*view,d);
    if(evaluation.state!=drawing::MeasurementState::Resolved||evaluation.presentations.size()!=1)return {};
    const auto value=evaluation.presentations.front();
    if(std::hypot(value.line_second.x-value.line_first.x,value.line_second.y-value.line_first.y)<1e-9)return {};
    return value;
}
bool guide_attached(const drawing::DrawingView& view,const drawing::DrawingDimension& d,const kernel::ViewerDimension& p) {
    if(d.segments.front().layout.envelope_offset)return true;
    const auto dx=p.line_second.x-p.line_first.x,dy=p.line_second.y-p.line_first.y;
    for(const auto& guide:drawing::annotation_guides(view)) {
        const auto gx=guide.second.x-guide.first.x,gy=guide.second.y-guide.first.y;
        const auto size=std::hypot(gx,gy);if(size<1e-9)continue;
        if(std::abs(dx*gy-dy*gx)>1e-7*size*std::hypot(dx,dy))continue;
        for(const auto point:{p.line_first,p.line_second,p.label_position.value_or(p.line_first)}) {
            const auto x=point.x*view.scale-guide.first.x,y=point.y*view.scale-guide.first.y;
            const auto t=(x*gx+y*gy)/(size*size);
            if(t>=-1e-7&&t<=1+1e-7&&std::abs(x*gy-y*gx)/size<1e-5)return true;
        }
    }
    return false;
}
}
bool free_alignment_dimension(const drawing::DrawingSheet& sheet,const std::string& id) {
    const auto* d=alignment_dimension(sheet,id);if(!d)return false;
    const auto value=alignment_geometry(sheet,*d);if(!value)return false;
    const auto view=std::ranges::find(sheet.views,d->view_id,&drawing::DrawingView::id);
    if(guide_attached(*view,*d,*value))return false;
    if(!d->chain_group.empty())for(const auto& member:sheet.dimensions)if(member.view_id==d->view_id&&member.chain_group==d->chain_group) {
        const auto geometry=alignment_geometry(sheet,member);if(!geometry||guide_attached(*view,member,*geometry))return false;
    }
    return true;
}
bool can_align_drawing_dimensions(const drawing::DrawingSheet& sheet,const std::vector<std::string>& ids) {
    if(ids.size()<2)return false;
    const auto* first=alignment_dimension(sheet,ids.front());
    if(!first||!free_alignment_dimension(sheet,first->id))return false;
    const auto a=*alignment_geometry(sheet,*first);
    const auto dx=a.line_second.x-a.line_first.x,dy=a.line_second.y-a.line_first.y;
    for(std::size_t i=1;i<ids.size();++i) {
        const auto* d=alignment_dimension(sheet,ids[i]);
        if(!d||d->id==first->id||d->view_id!=first->view_id||!free_alignment_dimension(sheet,d->id))return false;
        if(!first->chain_group.empty()&&d->chain_group==first->chain_group)return false;
        const auto b=*alignment_geometry(sheet,*d);const auto x=b.line_second.x-b.line_first.x,y=b.line_second.y-b.line_first.y;
        if(std::abs(dx*y-dy*x)>1e-7*std::hypot(dx,dy)*std::hypot(x,y))return false;
    }
    return true;
}
bool align_drawing_dimensions(drawing::DrawingSheet& sheet,const std::vector<std::string>& ids) {
    if(!can_align_drawing_dimensions(sheet,ids))return false;
    const auto* first=alignment_dimension(sheet,ids.front());const auto a=*alignment_geometry(sheet,*first);
    const double length=std::hypot(a.line_second.x-a.line_first.x,a.line_second.y-a.line_first.y);
    const drawing::Point2 normal{-(a.line_second.y-a.line_first.y)/length,(a.line_second.x-a.line_first.x)/length};
    auto next=sheet;std::set<std::string> groups;bool changed=false;
    for(std::size_t i=1;i<ids.size();++i) {
        auto& d=*std::ranges::find(next.dimensions,ids[i],&drawing::DrawingDimension::id);
        if(!d.chain_group.empty()&&!groups.insert(d.chain_group).second)continue;
        const auto before=*alignment_geometry(next,d);auto probe=d;probe.segments.front().layout.line_offset+=1;
        const auto after=*alignment_geometry(next,probe);
        const double rate=(after.line_first.x-before.line_first.x)*normal.x+(after.line_first.y-before.line_first.y)*normal.y;
        if(std::abs(rate)<1e-9)return false;
        const double offset=((a.line_first.x-before.line_first.x)*normal.x+(a.line_first.y-before.line_first.y)*normal.y)/rate;
        if(std::abs(offset)<1e-9)continue;
        d.segments.front().layout.line_offset+=offset;
        const auto view=std::ranges::find(next.views,d.view_id,&drawing::DrawingView::id);
        drawing::refresh_drawing_dimension(*view,d);synchronize_dimension_chain(next,d);changed=true;
    }
    // Preserve sheet/view storage used by the canvas; only dimension data changed.
    if(changed)sheet.dimensions=std::move(next.dimensions);
    return changed;
}
void synchronize_dimension_chain(drawing::DrawingSheet& sheet,const drawing::DrawingDimension& source) {
    if(source.chain_group.empty()||source.kind!=drawing::DrawingDimensionKind::Chain)return;
    const auto value=source;
    for(auto& d:sheet.dimensions)if(d.id!=value.id&&d.view_id==value.view_id&&d.chain_group==value.chain_group) {
        d.attachments[d.anchor_attachment]=value.attachments[value.anchor_attachment];
        d.direction=value.direction;d.parallel_reference=value.parallel_reference;d.chain_direction=value.chain_direction;
        for(auto& segment:d.segments)segment.layout.line_offset=value.segments.front().layout.line_offset;
        for(auto& segment:d.segments) {
            std::erase_if(segment.witness_edits,[](const auto& edit){return edit.side==0;});
            for(const auto& edit:value.segments.front().witness_edits)if(edit.side==0)segment.witness_edits.push_back(edit);
        }
        const auto view=std::ranges::find(sheet.views,d.view_id,&drawing::DrawingView::id);
        if(view!=sheet.views.end())drawing::refresh_drawing_dimension(*view,d);
    }
}
bool commit_drawing_chain(drawing::DrawingDocument& doc,const std::string& sheet_id,drawing::DrawingDimension value,bool creating) {
    auto next=doc;
    const auto* view=next.find_view(value.view_id);
    if(!view)throw DrawingOperationError("view_not_found","The dimension view must belong to its drawing sheet.");
    if(value.chain_group.empty())value.chain_group=value.id;
    // Freeze the established automatic direction before separating targets.
    if(!value.chain_direction) {
        const auto before=drawing::evaluate_drawing_dimension(*view,value);
        auto probe=value;for(auto& s:probe.segments)s.layout.line_offset+=1;
        const auto after=drawing::evaluate_drawing_dimension(*view,probe);
        if(!before.presentations.empty()&&!after.presentations.empty()) {
            const auto a=before.presentations.front().line_first,b=after.presentations.front().line_first;
            value.chain_direction=drawing::Point2{b.y-a.y,a.x-b.x};
        }
    }
    if(creating)if(auto* sheet=next.find_sheet(sheet_id)) {
        const auto seed=std::ranges::find(sheet->dimensions,value.chain_group,&drawing::DrawingDimension::id);
        if(seed!=sheet->dimensions.end()&&seed->chain_group.empty()) {
            auto linked=*seed;linked.chain_group=value.chain_group;
            commit_drawing_chain(next,sheet_id,std::move(linked),false);
        }
    }
    if(value.chain_datum_only)edit_drawing_dimension(next,sheet_id,value,creating);
    else for(std::size_t i=0;i<value.segments.size();++i) {
        auto member=value;
        if(i)member.id=kernel::make_stable_id();
        member.attachments={value.attachments[value.anchor_attachment],value.attachments[i<value.anchor_attachment?i:i+1]};
        member.anchor_attachment=0;member.segments={value.segments[i]};
        if(i) {
            std::erase_if(member.segments.front().witness_edits,[](const auto& edit){return edit.side==0;});
            for(const auto& edit:value.segments.front().witness_edits)if(edit.side==0)member.segments.front().witness_edits.push_back(edit);
        }
        edit_drawing_dimension(next,sheet_id,std::move(member),creating||i!=0);
    }
    doc=std::move(next);return true;
}
bool edit_drawing_dimension(drawing::DrawingDocument& doc,const std::string& sheet_id,drawing::DrawingDimension value,bool creating) {
    auto* sheet=doc.find_sheet(sheet_id);
    if(!sheet)throw DrawingOperationError("sheet_not_found","The drawing sheet does not exist.");
    auto existing=std::ranges::find(sheet->dimensions,value.id,&drawing::DrawingDimension::id);
    if(creating) {
        for(const auto& s:doc.sheets)for(const auto& d:s.dimensions)if(d.id==value.id)
            throw DrawingOperationError("invalid_arguments","The measured drawing dimension already exists.");
    } else if(existing==sheet->dimensions.end())
        throw DrawingOperationError("dimension_not_found","The measured drawing dimension does not exist.");
    const auto view=std::ranges::find(sheet->views,value.view_id,&drawing::DrawingView::id);
    if(view==sheet->views.end())throw DrawingOperationError("view_not_found","The dimension view must belong to its drawing sheet.");
    drawing::validate_drawing_dimension(value);
    const auto evaluation=drawing::evaluate_drawing_dimension(*view,value);
    if(evaluation.state==drawing::MeasurementState::Unresolved)
        throw DrawingOperationError("unresolved_reference",evaluation.message.c_str());
    if(value.kind==drawing::DrawingDimensionKind::Chain&&value.attachments.size()==2&&value.chain_group.empty())value.chain_group=value.id;
    if(!value.chain_group.empty()&&value.kind==drawing::DrawingDimensionKind::Chain&&!value.chain_direction&&!evaluation.presentations.empty()) {
        auto probe=value;for(auto& segment:probe.segments)segment.layout.line_offset+=1;
        const auto moved=drawing::evaluate_drawing_dimension(*view,probe);
        if(!moved.presentations.empty()) {
            const auto a=evaluation.presentations.front().line_first,b=moved.presentations.front().line_first;
            value.chain_direction=drawing::Point2{b.y-a.y,a.x-b.x};
        }
    }
    drawing::refresh_drawing_dimension(*view,value);
    if(value.kind!=drawing::DrawingDimensionKind::Chain)value.chain_group.clear();
    if(creating)sheet->dimensions.push_back(value);
    else {if(*existing==value)return false;*existing=value;}
    synchronize_dimension_chain(*sheet,value);
    return true;
}
bool erase_drawing_dimension(drawing::DrawingSheet& sheet,const std::string& id) {
    return std::erase_if(sheet.dimensions,[&](const auto& value){return value.id==id;})>0;
}
}
