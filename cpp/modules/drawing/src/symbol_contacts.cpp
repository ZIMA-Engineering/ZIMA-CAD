#include <zima/drawing/symbol_contacts.hpp>
#include <zima/drawing/measurement_dimension.hpp>
#include <zima/drawing/view_breaks.hpp>
#include <algorithm>
#include <cmath>
#include <stdexcept>
namespace zima::drawing {
namespace {
symbols::Frame paper_frame(const DrawingView& view,Point2 point) {
    const auto p=break_map(view,point);
    // Sheet X is measured from the right. Symbol-local X remains conventional.
    return {{view.x-p.x*view.scale,view.y+p.y*view.scale,0},{-1,0,0},{0,1,0}};
}
}
void validate_symbol_contacts(const DrawingSheet& sheet) {
    for(const auto& [id,contact]:sheet.symbol_contacts) {
        const auto symbol=std::ranges::find(sheet.symbol_annotations,id,[](const auto& p){return p.symbol.id;});
        if(symbol==sheet.symbol_annotations.end()||!symbol->reference||(symbol->reference->kind!=symbols::ReferenceKind::Edge&&symbol->reference->kind!=symbols::ReferenceKind::Point)||
           contact.view_id.empty()||!std::isfinite(contact.parameter)||contact.parameter<0||contact.parameter>1)
            throw std::invalid_argument("Invalid drawing symbol contact");
    }
}
void refresh_symbol_contacts(DrawingSheet& sheet) {
    validate_symbol_contacts(sheet);
    for(auto& symbol:sheet.symbol_annotations) {
        const auto contact=sheet.symbol_contacts.find(symbol.symbol.id);if(contact==sheet.symbol_contacts.end())continue;
        const auto view=std::ranges::find(sheet.views,contact->second.view_id,&DrawingView::id);
        std::optional<symbols::Frame> frame;
        if(view!=sheet.views.end()&&view->source_document_id==symbol.reference->document_id) {
            DimensionAttachment attachment;attachment.kind=symbol.reference->kind==symbols::ReferenceKind::Point?DimensionAttachmentKind::Point:DimensionAttachmentKind::CurvePoint;
            attachment.reference={symbol.reference->owner_id,symbol.reference->semantic_key,symbol.reference->instance_path};
            attachment.parameter=contact->second.parameter;
            if(const auto point=resolve_dimension_attachment(*view,attachment))frame=paper_frame(*view,*point);
        }
        symbol.refresh_reference(frame);
    }
}
void attach_symbol_to_view(symbols::Placement& symbol,const DrawingView& view,const DimensionAttachment& attachment) {
    if(attachment.kind!=DimensionAttachmentKind::CurvePoint&&attachment.kind!=DimensionAttachmentKind::Point)throw std::invalid_argument("Symbol requires a projected curve or point contact");
    const auto point=resolve_dimension_attachment(view,attachment);
    if(!point)throw std::invalid_argument("Unresolved drawing symbol contact");
    auto next=symbol;
    symbols::Reference reference;reference.document_id=view.source_document_id;
    reference.owner_id=attachment.reference.owner_id;reference.semantic_key=attachment.reference.semantic_key;
    reference.instance_path=attachment.reference.instance_path;reference.kind=attachment.kind==DimensionAttachmentKind::Point?symbols::ReferenceKind::Point:symbols::ReferenceKind::Edge;
    next.reference=reference;next.frame=paper_frame(view,*point);next.unresolved=false;next.validate();symbol=std::move(next);
}
}
