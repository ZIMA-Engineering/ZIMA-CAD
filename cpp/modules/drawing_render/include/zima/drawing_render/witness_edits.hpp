#pragma once
#include <zima/drawing/drawing_document.hpp>
#include <zima/viewer/dimension_presentation.hpp>
#include <algorithm>

namespace zima::drawing_render {
struct WitnessGeometry {
    int side{};
    QPointF first,last;
    std::vector<QPolygonF> curves;
    struct Grip {std::string id;int end;QPointF point;};
    std::vector<Grip> grips;
};
inline WitnessGeometry witness_geometry(QPointF first,QPointF last,int side,
    const std::vector<drawing::WitnessEdit>& edits,double paper_scale) {
    WitnessGeometry result{side,first,last};
    const auto delta=last-first,unit=viewer::dimension_screen_unit(delta);
    const QPointF normal(-unit.y(),unit.x());
    std::vector<drawing::WitnessEdit> ordered;
    for(const auto& edit:edits)if(edit.side==side)ordered.push_back(edit);
    std::ranges::sort(ordered,{},&drawing::WitnessEdit::first);
    double offset=0;
    QPolygonF curve{first};
    for(const auto& edit:ordered) {
        const auto a=first+delta*edit.first+normal*offset;
        curve<<a;
        if(edit.kind==drawing::WitnessEditKind::Break) {
            result.curves.push_back(curve);curve.clear();
        }else offset+=edit.offset*paper_scale;
        const auto b=first+delta*edit.last+normal*offset;
        curve<<b;
        result.grips.push_back({edit.id,0,a});result.grips.push_back({edit.id,1,b});
    }
    curve<<last+normal*offset;result.curves.push_back(curve);
    return result;
}
// Only witness curves are editable. Dimension lines and arrow identities are
// preserved; a jog displaces the far end of its witness and the attached line.
inline std::vector<WitnessGeometry> apply_witness_edits(viewer::DimensionPresentation& layout,
    const std::vector<drawing::WitnessEdit>& edits,double scale,bool chain,bool datum,
    std::array<std::pair<QPointF,QPointF>,2> baselines={}) {
    std::vector<WitnessGeometry> result;
    const int count=chain?1:2;
    if(layout.curves.size()<std::size_t(count))return result;
    std::array<QPointF,2> shifts{};
    if(chain)for(int side=0;side<2;++side) {
        const auto geometry=witness_geometry(baselines[side].first,baselines[side].second,side,edits,scale);
        shifts[side]=geometry.curves.back().back()-baselines[side].second;
    }
    const auto a=layout.handles[1],b=layout.handles[2];
    for(int i=0;i<count;++i) {
        const auto line=layout.curves[i];if(line.size()!=2)continue;
        const int side=chain?(datum?0:1):i;
        if(QLineF(line.front(),line.back()).length()<1e-5)continue;
        auto geometry=witness_geometry(line.front(),line.back(),side,edits,scale);
        shifts[side]=geometry.curves.back().back()-line.back();result.push_back(std::move(geometry));
    }
    const auto translate=[&](QPointF p) {
        const auto delta=b-a;const auto squared=viewer::dimension_screen_dot(delta,delta);
        const double t=squared>1e-12?std::clamp(viewer::dimension_screen_dot(p-a,delta)/squared,0.,1.):(datum?0.:1.);
        return p+shifts[0]*(1-t)+shifts[1]*t;
    };
    for(std::size_t i=count;i<layout.curves.size();++i)for(auto& p:layout.curves[i])p=translate(p);
    for(auto& arrow:layout.arrows)arrow.first=translate(arrow.first);
    const auto text_shift=chain?shifts[datum?0:1]:(shifts[0]+shifts[1])/2;
    layout.text_baseline+=text_shift;layout.handles[0]+=text_shift;
    layout.handles[1]+=shifts[0];layout.handles[2]+=shifts[1];
    layout.curves.erase(layout.curves.begin(),layout.curves.begin()+count);
    for(const auto& geometry:result)layout.curves.insert(layout.curves.end(),geometry.curves.begin(),geometry.curves.end());
    return result;
}
} // namespace zima::drawing_render
