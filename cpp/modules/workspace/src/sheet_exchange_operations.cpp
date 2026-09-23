#include <zima/workspace/sheet_exchange_operations.hpp>
#include <zima/workspace/sheet_state_operations.hpp>
#include <zima/kernel/sheet_exchange.hpp>
#include <zima/document/precision.hpp>

namespace zima::workspace {
SheetDxfResult prepare_sheet_dxf(const document::PartDocument& source,
        const std::vector<kernel::BodyResult>& cache,const kernel::OcctKernel& kernel) {
    const auto* body=source.body_history.find(source.body_history.active_body_id());
    const auto regions=sheet_state_regions(source);
    if(!body||regions.empty()||cache.empty())throw std::invalid_argument("Sheet DXF requires an active calculated sheet body.");
    const double thickness=regions.front().thickness;
    for(const auto& region:regions)if(std::abs(region.thickness-thickness)>1e-6)
        throw std::invalid_argument("Sheet DXF requires one material thickness.");
    auto copy=source;auto calculated=cache;
    if(std::ranges::any_of(regions,[](const auto& region){return region.kind!=kernel::SheetMaterialDefinition::Kind::Plane&&!region.unfolded;})) {
        auto feature=document::PartDocument::create_sketch_container();feature.feature_kind=document::FeatureKind::Unbend;
        feature.name="DXF";feature.sheet_state.all=true;
        copy.insert_history_entry(document::PartHistoryKind::Feature,feature.id);copy.history.push_back(std::move(feature));
        calculated=kernel.evaluate_history_incremental(copy.kernel_operations(),cache);
    }
    if(calculated.empty()||!calculated.back().calculation_errors.empty())
        throw std::invalid_argument("Sheet DXF requires a calculated sheet body.");
    const auto found=calculated.back().body_outputs.find(body->scope.id);
    if(found==calculated.back().body_outputs.end())throw std::invalid_argument("Sheet DXF requires a calculated sheet body.");
    const auto outline=kernel::sheet_flat_contour(found->second.get(),thickness,document::sheet_cut_tolerance(source.document_precision));
    SheetDxfResult result;result.thickness=thickness;result.volume=found->second->volume;
    result.contour=sketcher::Sketch::create_default();result.contour.name="DXF";
    for(const auto& arc:outline.arcs) {
        if(arc.full)static_cast<void>(result.contour.add_circle(arc.center.x,arc.center.y,arc.radius));
        else static_cast<void>(result.contour.add_arc(arc.center.x,arc.center.y,arc.start.x,arc.start.y,arc.end.x,arc.end.y,false,1e-7,arc.clockwise));
    }
    for(const auto& curve:outline.curves) {
        if(curve.degree==1&&curve.poles.size()==2) {
            const auto& a=curve.poles.front();const auto& b=curve.poles.back();
            static_cast<void>(result.contour.add_segment(a.x,a.y,b.x,b.y));continue;
        }
        std::vector<std::array<double,2>> poles;for(const auto p:curve.poles)poles.push_back({p.x,p.y});
        const auto id=result.contour.add_bspline(poles,curve.degree,false,false,1e-7,false);
        auto& spline=*std::ranges::find(result.contour.bsplines,id,&sketcher::SketchBSpline::id);
        spline.knots=curve.knots;spline.weights=curve.weights;
    }
    result.contour.validate();return result;
}
}
