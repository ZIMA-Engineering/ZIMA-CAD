#include "rectilinear_template_solver.hpp"
#include <zima/sketcher/sketch.hpp>
#include <algorithm>
#include <cmath>
#include <numeric>
#include <unordered_map>

namespace zima::sketcher {
bool seed_rectilinear_template(Sketch& sketch,const std::vector<std::string>& anchors) {
    if(!sketch.drawing_template || !sketch.external_references.empty() || sketch.points.empty())return false;
    const std::size_t count=sketch.points.size()*2;
    std::unordered_map<std::string,std::size_t> index;
    std::vector<double> coordinates;coordinates.reserve(count);
    for(std::size_t i=0;i<sketch.points.size();++i){index.emplace(sketch.points[i].id,i);coordinates.push_back(sketch.points[i].x);coordinates.push_back(sketch.points[i].y);}
    std::vector<std::size_t> groups(count);std::iota(groups.begin(),groups.end(),0);
    const auto root=[&](std::size_t p){while(groups[p]!=p){groups[p]=groups[groups[p]];p=groups[p];}return p;};
    const auto coordinate=[&](const std::string& id,int axis)->std::optional<std::size_t>{const auto it=index.find(id);return it==index.end()?std::nullopt:std::optional{it->second*2+axis};};
    for(const auto& c:sketch.constraints)if(!c.suppressed) {
        if(c.kind!=ConstraintKind::Horizontal&&c.kind!=ConstraintKind::Vertical&&c.kind!=ConstraintKind::Coincident)continue;
        for(int axis=0;axis<2;++axis) {
            if((c.kind==ConstraintKind::Horizontal&&axis==0)||(c.kind==ConstraintKind::Vertical&&axis==1))continue;
            const auto a=coordinate(c.first_point_id,axis),b=coordinate(c.second_point_id,axis);if(!a||!b)return false;
            groups[root(*a)]=root(*b);
        }
    }
    using Row=std::vector<double>;
    std::vector<Row> rows;std::vector<double> targets;
    const auto add=[&](Row row,double target){rows.push_back(std::move(row));targets.push_back(target);};
    const auto pair=[&](const std::string& a,const std::string& b,int axis,double value)->bool {
        const auto first=coordinate(a,axis),second=coordinate(b,axis);if(!first||!second)return false;
        Row row(count);row[*first]=-1;row[*second]+=1;add(std::move(row),value);return true;
    };
    // H/V equivalence is persisted design intent. Current coordinates determine
    // only the existing positive-length branch, never whether a line is constrained.
    const auto segment_row=[&](const std::string& id)->std::optional<Row> {
        const auto line=std::ranges::find(sketch.segments,id,&SketchSegment::id);if(line==sketch.segments.end())return {};
        const auto a=index.at(line->first_point_id)*2,b=index.at(line->second_point_id)*2;
        int axis;
        if(root(a+1)==root(b+1))axis=0;else if(root(a)==root(b))axis=1;else return {};
        const double delta=coordinates[b+axis]-coordinates[a+axis];if(std::abs(delta)<1e-10)return {};
        const double sign=delta>0?1:-1;Row row(count);row[a+axis]=-sign;row[b+axis]+=sign;return row;
    };
    for(const auto& c:sketch.constraints)if(!c.suppressed) {
        if(c.kind==ConstraintKind::Horizontal||c.kind==ConstraintKind::Vertical) {
            if(!pair(c.first_point_id,c.second_point_id,c.kind==ConstraintKind::Horizontal?1:0,0))return false;
        } else if(c.kind==ConstraintKind::Coincident) {
            if(!pair(c.first_point_id,c.second_point_id,0,0)||!pair(c.first_point_id,c.second_point_id,1,0))return false;
        } else if(c.kind==ConstraintKind::PointReference&&c.second_point_id=="sketch_origin") {
            for(int axis=0;axis<2;++axis){const auto i=coordinate(c.first_point_id,axis);if(!i)return false;Row row(count);row[*i]=1;add(std::move(row),0);}
        } else if(c.kind==ConstraintKind::EqualLength) {
            auto a=segment_row(c.geometry_id),b=segment_row(c.second_geometry_id);if(!a||!b)return false;
            for(std::size_t i=0;i<count;++i)(*a)[i]-=(*b)[i];add(std::move(*a),0);
        } else return false;
    }
    for(const auto& d:sketch.dimensions)if(!d.suppressed&&d.driving) {
        if(d.kind!=DimensionKind::DistanceX&&d.kind!=DimensionKind::DistanceY)return false;
        const int axis=d.kind==DimensionKind::DistanceX?0:1;
        if(!d.second_point_id.empty()){if(!pair(d.first_point_id,d.second_point_id,axis,d.value))return false;}
        else {
            if(d.geometry_id!=(axis==0?"sketch_axis:y":"sketch_axis:x"))return false;
            const auto i=coordinate(d.first_point_id,axis);if(!i)return false;Row row(count);row[*i]=1;add(std::move(row),d.value);
        }
    }
    for(const auto& p:sketch.points)if(p.fixed||std::ranges::find(anchors,p.id)!=anchors.end())
        for(int axis=0;axis<2;++axis){Row row(count);const auto i=*coordinate(p.id,axis);row[i]=1;add(std::move(row),coordinates[i]);}
    const auto dot=[](const Row& a,const Row& b){return std::inner_product(a.begin(),a.end(),b.begin(),0.0);};
    double initial_error=0;for(std::size_t i=0;i<rows.size();++i)initial_error=std::max(initial_error,std::abs(targets[i]-dot(rows[i],coordinates)));
    if(initial_error<1e-8)return true;
    // Twice-reorthogonalized row-space QR gives the minimum coordinate change
    // even when H/V chains contain dependent equations or free coordinates.
    std::vector<Row> basis;std::vector<double> values;
    for(std::size_t i=0;i<rows.size();++i) {
        auto row=rows[i];double value=targets[i]-dot(row,coordinates);
        for(int pass=0;pass<2;++pass)for(std::size_t j=0;j<basis.size();++j) {
            const double factor=dot(row,basis[j]);if(std::abs(factor)<1e-16)continue;
            for(std::size_t k=0;k<count;++k)row[k]-=factor*basis[j][k];value-=factor*values[j];
        }
        const double norm=std::sqrt(dot(row,row));
        if(norm<1e-10){if(std::abs(value)>1e-7)return false;continue;}
        for(auto& element:row)element/=norm;basis.push_back(std::move(row));values.push_back(value/norm);
    }
    auto solved=coordinates;
    for(std::size_t j=0;j<basis.size();++j)for(std::size_t k=0;k<count;++k)solved[k]+=values[j]*basis[j][k];
    for(std::size_t i=0;i<rows.size();++i)if(!std::isfinite(dot(rows[i],solved))||std::abs(dot(rows[i],solved)-targets[i])>1e-7)return false;
    for(const auto& c:sketch.constraints)if(!c.suppressed&&c.kind==ConstraintKind::EqualLength)
        if(dot(*segment_row(c.geometry_id),solved)<=1e-8||dot(*segment_row(c.second_geometry_id),solved)<=1e-8)return false;
    for(std::size_t i=0;i<sketch.points.size();++i){sketch.points[i].x=solved[i*2];sketch.points[i].y=solved[i*2+1];}
    return true;
}
} // namespace zima::sketcher
