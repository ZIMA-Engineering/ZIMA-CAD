#include <zima/workspace/drawing_dimension_operations.hpp>
#include <algorithm>
#include <zima/workspace/drawing_operations.hpp>
#include <zima/drawing/measurement_dimension.hpp>
#include <zima/kernel/stable_id.hpp>
namespace zima::workspace {
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
