#include <zima/drawing/symbol_contacts.hpp>
#include <zima/drawing/measurement_dimension.hpp>
#include <zima/drawing/view_breaks.hpp>
#include <algorithm>
#include <cmath>
#include <stdexcept>
#include <limits>
namespace zima::drawing {
namespace {
symbols::Frame paper_frame(const DrawingView& view,Point2 point) {
    const auto p=break_map(view,point);
    // Sheet X is measured from the right. Symbol-local X remains conventional.
    return {{view.x-p.x*view.scale,view.y+p.y*view.scale,0},{-1,0,0},{0,1,0}};
}
bool resolve_contact(symbols::Placement& symbol,const DrawingView& view,double parameter) {
    const auto& ref=*symbol.reference;
    DimensionAttachment a;a.kind=ref.kind==symbols::ReferenceKind::Point?DimensionAttachmentKind::Point:DimensionAttachmentKind::CurvePoint;
    a.reference={ref.owner_id,ref.semantic_key,ref.instance_path};a.parameter=parameter;
    symbol.paper_tangent.reset();symbol.paper_extension_start.reset();
    if(ref.kind==symbols::ReferenceKind::Edge)for(const auto& curve:projected_measurement_curves(view))if(curve.source==a.reference&&curve.line&&curve.points.size()>1) {
        const auto first=curve.points.front(),last=curve.points.back();
        const auto start=paper_frame(view,first).origin,end=paper_frame(view,last).origin;
        const auto tangent=kernel::Vec3{end.x-start.x,end.y-start.y,0};const double length=std::hypot(tangent.x,tangent.y);
        if(length<1e-9)return false;
        symbol.paper_tangent=kernel::Vec3{tangent.x/length,tangent.y/length,0};
        symbol.refresh_reference(paper_frame(view,{first.x+(last.x-first.x)*parameter,first.y+(last.y-first.y)*parameter}));
        if(parameter<0||parameter>1)symbol.paper_extension_start=parameter<0?start:end;
        return true;
    }
    if(parameter<0||parameter>1)return false;
    if(const auto point=resolve_dimension_attachment(view,a)){
        symbol.refresh_reference(paper_frame(view,*point));
        if(ref.kind==symbols::ReferenceKind::Edge) {
            auto before=a,after=a;before.parameter=std::max(0.,parameter-1e-5);after.parameter=std::min(1.,parameter+1e-5);
            const auto p=resolve_dimension_attachment(view,before),q=resolve_dimension_attachment(view,after);
            if(p&&q){const auto u=paper_frame(view,*p).origin,v=paper_frame(view,*q).origin;
                const double length=std::hypot(v.x-u.x,v.y-u.y);
                if(length>1e-12)symbol.paper_tangent=kernel::Vec3{(v.x-u.x)/length,(v.y-u.y)/length,0};}
        }
        return true;
    }return false;
}

}
void validate_symbol_contacts(const DrawingSheet& sheet) {
    for(const auto& [id,contact]:sheet.symbol_contacts) {
        const auto symbol=std::ranges::find(sheet.symbol_annotations,id,[](const auto& p){return p.symbol.id;});
        if(symbol==sheet.symbol_annotations.end()||!symbol->reference||(symbol->reference->kind!=symbols::ReferenceKind::Edge&&symbol->reference->kind!=symbols::ReferenceKind::Point)||
           contact.view_id.empty()||!std::isfinite(contact.parameter)||std::abs(contact.parameter)>1e9)
            throw std::invalid_argument("Invalid drawing symbol contact");
    }
}
void refresh_symbol_contacts(DrawingSheet& sheet) {
    validate_symbol_contacts(sheet);
    for(auto& symbol:sheet.symbol_annotations) {
        const auto contact=sheet.symbol_contacts.find(symbol.symbol.id);if(contact==sheet.symbol_contacts.end())continue;
        const auto view=std::ranges::find(sheet.views,contact->second.view_id,&DrawingView::id);
        auto next=symbol;
        if(view==sheet.views.end()||view->source_document_id!=symbol.reference->document_id||!resolve_contact(next,*view,contact->second.parameter))symbol.refresh_reference({});
        else {if(!next.leader)next.symbol.x=next.symbol.y=next.offset_z=0;symbol=std::move(next);}
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
    next.reference=reference;next.frame=paper_frame(view,*point);next.unresolved=false;
    resolve_contact(next,view,attachment.parameter);if(!next.leader)next.symbol.x=next.symbol.y=next.offset_z=0;
    next.validate();symbol=std::move(next);
}
void translate_symbol_contacts(DrawingSheet& sheet,const std::string& view_id,Point2 delta) {
    for(auto& symbol:sheet.symbol_annotations) {
        const auto contact=sheet.symbol_contacts.find(symbol.symbol.id);
        if(contact==sheet.symbol_contacts.end()||contact->second.view_id!=view_id)continue;
        symbol.frame.origin.x+=delta.x;symbol.frame.origin.y+=delta.y;
        if(symbol.paper_extension_start){symbol.paper_extension_start->x+=delta.x;symbol.paper_extension_start->y+=delta.y;}
    }
}
bool move_symbol_contact(DrawingSheet& sheet,const std::string& id,Point2 paper_point) {
    const auto symbol=std::ranges::find(sheet.symbol_annotations,id,[](const auto& p){return p.symbol.id;});
    const auto contact=sheet.symbol_contacts.find(id);
    if(symbol==sheet.symbol_annotations.end()||contact==sheet.symbol_contacts.end()||!symbol->reference||symbol->reference->kind!=symbols::ReferenceKind::Edge)return false;
    const auto view=std::ranges::find(sheet.views,contact->second.view_id,&DrawingView::id);if(view==sheet.views.end())return false;
    return move_symbol_contact(*symbol,*view,contact->second,paper_point);
}
bool move_symbol_contact(symbols::Placement& value,const DrawingView& source,SymbolContact& contact,Point2 paper_point) {
    if(!value.reference||value.reference->kind!=symbols::ReferenceKind::Edge||contact.view_id!=source.id)return false;
    auto* symbol=&value;const auto* view=&source;
    const kernel::EdgeReference ref{symbol->reference->owner_id,symbol->reference->semantic_key,symbol->reference->instance_path};
    for(const auto& curve:projected_measurement_curves(*view))if(curve.source==ref&&curve.points.size()>1) {
        const auto a=paper_frame(*view,curve.points.front()).origin,b=paper_frame(*view,curve.points.back()).origin;
        const double dx=b.x-a.x,dy=b.y-a.y,length2=dx*dx+dy*dy;if(curve.line&&length2<1e-12)return false;
        double t{};
        if(curve.line)t=((paper_point.x-a.x)*dx+(paper_point.y-a.y)*dy)/length2;
        else {
            double nearest=std::numeric_limits<double>::infinity();
            for(std::size_t i=1;i<curve.points.size();++i) {
                const auto p=paper_frame(*view,curve.points[i-1]).origin,q=paper_frame(*view,curve.points[i]).origin;
                const double x=q.x-p.x,y=q.y-p.y,square=x*x+y*y;if(square<1e-18)continue;
                const double fraction=std::clamp(((paper_point.x-p.x)*x+(paper_point.y-p.y)*y)/square,0.,1.);
                const double distance=std::hypot(paper_point.x-p.x-fraction*x,paper_point.y-p.y-fraction*y);
                if(distance<nearest){nearest=distance;t=curve.parameters[i-1]+fraction*(curve.parameters[i]-curve.parameters[i-1]);}
            }
            if(!std::isfinite(nearest))return false;
        }
        const auto old_grip=symbol->frame.world({symbol->symbol.x,symbol->symbol.y,0});
        auto next=*symbol;if(!resolve_contact(next,*view,t))return false;
        if(next.leader){const auto grip=next.frame.local(old_grip);next.symbol.x=grip.x;next.symbol.y=grip.y;}
        else next.symbol.x=next.symbol.y=next.offset_z=0;
        contact.parameter=t;*symbol=std::move(next);return true;
    }
    return false;
}
}
