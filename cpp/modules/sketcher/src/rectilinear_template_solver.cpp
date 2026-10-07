#include "rectilinear_template_solver.hpp"
#include <zima/sketcher/sketch.hpp>
#include <algorithm>
#include <cmath>
#include <numeric>
#include <set>
#include <unordered_map>

namespace zima::sketcher {
bool seed_rectilinear_equations(Sketch& sketch,const std::vector<std::string>& anchors,bool allow_nonlinear_distances,bool allow_constraint_only) {
    if((!allow_constraint_only&&!sketch.external_references.empty()) || sketch.points.empty() ||
       !sketch.arcs.empty() || !sketch.ellipses.empty() ||
       !sketch.elliptical_arcs.empty() || !sketch.bsplines.empty() ||
       !sketch.corner_radii.empty() || !sketch.curve_trims.empty() ||
       !sketch.offsets.empty())return false;
    // Circle centres are ordinary point coordinates and their radii are not
    // altered by this linear seed. A circle alone must not disable simultaneous
    // H/V and EqualLength solving (e.g. a title-block projection symbol).
    // Nonlinear circle constraints/drivers are rejected by the row whitelist
    // below, before any coordinates are published.
    // Undimensioned point/line commands retain their existing picked-reference
    // priority. Coupled dimension equations need the simultaneous solve.
    if(!allow_constraint_only && !sketch.drawing_template && std::ranges::none_of(sketch.dimensions,
       [](const auto& d){return d.driving&&!d.suppressed;}))return false;
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
        const auto first=coordinate(a,axis),second=coordinate(b,axis);
        if((!first&&a!="sketch_origin")||(!second&&b!="sketch_origin"))return false;
        Row row(count);if(first)row[*first]=-1;if(second)row[*second]+=1;
        add(std::move(row),value);return true;
    };
    // H/V equivalence is persisted design intent. Current coordinates determine
    // only the existing positive-length branch, never whether a line is constrained.
    const auto distance_row=[&](const std::string& first,const std::string& second)->std::optional<Row> {
        const auto ai=index.find(first),bi=index.find(second);if(ai==index.end()||bi==index.end())return {};
        const auto a=ai->second*2,b=bi->second*2;
        int axis;
        if(root(a+1)==root(b+1))axis=0;else if(root(a)==root(b))axis=1;else return {};
        const double delta=coordinates[b+axis]-coordinates[a+axis];if(std::abs(delta)<1e-10)return {};
        const double sign=delta>0?1:-1;Row row(count);row[a+axis]=-sign;row[b+axis]+=sign;return row;
    };
    const auto segment_row=[&](const std::string& id)->std::optional<Row> {
        const auto line=std::ranges::find(sketch.segments,id,&SketchSegment::id);if(line==sketch.segments.end())return {};
        return distance_row(line->first_point_id,line->second_point_id);
    };
    const auto axis_coordinate=[](const std::string& id)->int {
        return id=="sketch_axis:y"?0:id=="sketch_axis:x"?1:-1;
    };
    for(const auto& c:sketch.constraints)if(!c.suppressed) {
        if(c.kind==ConstraintKind::Horizontal||c.kind==ConstraintKind::Vertical) {
            if(!pair(c.first_point_id,c.second_point_id,c.kind==ConstraintKind::Horizontal?1:0,0))return false;
        } else if(c.kind==ConstraintKind::Coincident) {
            if(!pair(c.first_point_id,c.second_point_id,0,0)||!pair(c.first_point_id,c.second_point_id,1,0))return false;
        } else if(c.kind==ConstraintKind::PointReference) {
            std::optional<std::array<double,2>> position;
            if(c.second_point_id=="sketch_origin")position=std::array{0.,0.};
            else {
                const auto ref=std::ranges::find(sketch.external_references,c.second_point_id,&SketchExternalReference::id);
                if(ref!=sketch.external_references.end()&&!ref->broken&&ref->cached_points.size()==1)
                    position=ref->cached_points.front();
            }
            if(!position)return false;
            for(int axis=0;axis<2;++axis){const auto i=coordinate(c.first_point_id,axis);if(!i)return false;Row row(count);row[*i]=1;add(std::move(row),(*position)[axis]);}
        } else if(c.kind==ConstraintKind::PointOnLine&&axis_coordinate(c.geometry_id)>=0) {
            if(!pair("sketch_origin",c.first_point_id,axis_coordinate(c.geometry_id),0))return false;
        } else if(c.kind==ConstraintKind::PointOnLine) {
            const auto ref=std::ranges::find(sketch.external_references,c.geometry_id,&SketchExternalReference::id);
            if(ref==sketch.external_references.end())return false;
            const auto line=external_reference_line(*ref);const auto x=coordinate(c.first_point_id,0),y=coordinate(c.first_point_id,1);
            if(!line||!x||!y)return false;
            const double length=std::hypot(line->second[0],line->second[1]);
            const double nx=-line->second[1]/length,ny=line->second[0]/length;
            Row row(count);row[*x]=nx;row[*y]=ny;
            add(std::move(row),nx*line->first[0]+ny*line->first[1]);
        } else if(c.kind==ConstraintKind::EqualLength) {
            auto a=segment_row(c.geometry_id),b=segment_row(c.second_geometry_id);if(!a||!b)return false;
            for(std::size_t i=0;i<count;++i)(*a)[i]-=(*b)[i];add(std::move(*a),0);
        } else return false;
    }
    struct Distance {std::size_t a,b;double length;};
    std::vector<Distance> nonlinear_distances;
    std::vector<Row> positive_distances;
    for(const auto& d:sketch.dimensions)if(!d.suppressed&&d.driving) {
        if(d.kind==DimensionKind::DistanceX||d.kind==DimensionKind::DistanceY) {
            const int axis=d.kind==DimensionKind::DistanceX?0:1;
            if(!d.second_point_id.empty()){if(!pair(d.first_point_id,d.second_point_id,axis,d.value))return false;}
            else {
                if(axis_coordinate(d.geometry_id)!=axis ||
                   !pair("sketch_origin",d.first_point_id,axis,d.value))return false;
            }
        } else if(d.kind==DimensionKind::Distance) {
            auto row=distance_row(d.first_point_id,d.second_point_id);
            if(row){positive_distances.push_back(*row);add(std::move(*row),d.value);}
            else {
                if(!allow_nonlinear_distances)return false;
                const auto a=coordinate(d.first_point_id,0),b=coordinate(d.second_point_id,0);
                if(!a||!b||!d.geometry_id.empty()||d.value<=0)return false;
                nonlinear_distances.push_back({*a,*b,d.value});
            }
        } else if(d.kind==DimensionKind::DistancePointLine||d.kind==DimensionKind::DistanceLine) {
            const int axis=axis_coordinate(d.geometry_id);if(axis<0)return false;
            const double target=d.value*d.solution_side*(axis==0?-1:1);
            if(d.kind==DimensionKind::DistancePointLine) {
                if(!pair("sketch_origin",d.first_point_id,axis,target))return false;
            } else {
                const auto line=std::ranges::find(sketch.segments,d.second_geometry_id,&SketchSegment::id);
                if(line==sketch.segments.end() ||
                   !pair("sketch_origin",line->first_point_id,axis,target) ||
                   !pair("sketch_origin",line->second_point_id,axis,target))return false;
            }
        } else return false;
    }
    for(const auto& p:sketch.points)if(p.fixed||std::ranges::find(anchors,p.id)!=anchors.end())
        for(int axis=0;axis<2;++axis){Row row(count);const auto i=*coordinate(p.id,axis);row[i]=1;add(std::move(row),coordinates[i]);}
    const auto dot=[](const Row& a,const Row& b){return std::inner_product(a.begin(),a.end(),b.begin(),0.0);};
    const auto error=[&](const Row& position) {
        double result=0;
        for(std::size_t i=0;i<rows.size();++i)result=std::max(result,std::abs(targets[i]-dot(rows[i],position)));
        for(const auto& d:nonlinear_distances)result=std::max(result,std::abs(std::hypot(position[d.b]-position[d.a],position[d.b+1]-position[d.a+1])-d.length));
        return result;
    };
    auto solved=coordinates;
    // The original linear path needs one QR solve. Genuine distances need a
    // bounded Newton solve of the same small point graph, not sequential moves
    // that accidentally retain the initial horizontal/vertical appearance.
    const int iterations=nonlinear_distances.empty()?1:32;
    for(int iteration=0;iteration<iterations&&error(solved)>=1e-8;++iteration) {
        auto equations=rows;std::vector<double> residual;
        for(std::size_t i=0;i<rows.size();++i)residual.push_back(targets[i]-dot(rows[i],solved));
        for(const auto& d:nonlinear_distances) {
            const double dx=solved[d.b]-solved[d.a],dy=solved[d.b+1]-solved[d.a+1],length=std::hypot(dx,dy);
            if(length<1e-10)return false;
            Row row(count);row[d.a]=-dx/length;row[d.a+1]=-dy/length;row[d.b]=dx/length;row[d.b+1]=dy/length;
            equations.push_back(std::move(row));residual.push_back(d.length-length);
        }
        // Twice-reorthogonalized row-space QR preserves free coordinates and
        // detects incompatible equations before publishing any point changes.
        // Equations with disjoint coordinate support are orthogonal already.
        // Keep their original row/column order, but factor each connected block
        // independently instead of projecting every row against unrelated rows.
        // Rebuild support for each Newton step: distance derivatives can change.
        std::vector<std::size_t> parents(count);std::iota(parents.begin(),parents.end(),0);
        const auto equation_root=[&](std::size_t p) {
            while(parents[p]!=p){parents[p]=parents[parents[p]];p=parents[p];}
            return p;
        };
        std::vector<std::vector<std::size_t>> support(equations.size());
        for(std::size_t i=0;i<equations.size();++i) {
            for(std::size_t k=0;k<count;++k)if(equations[i][k]!=0.0) {
                if(!support[i].empty())parents[equation_root(k)]=equation_root(support[i].front());
                support[i].push_back(k);
            }
            if(support[i].empty()&&std::abs(residual[i])>1e-7)return false;
        }
        struct Block {std::vector<std::size_t> columns,rows;};
        std::vector<Block> blocks;std::vector<std::size_t> block_index(count,count);
        for(std::size_t k=0;k<count;++k) {
            auto& b=block_index[equation_root(k)];
            if(b==count){b=blocks.size();blocks.emplace_back();}
            blocks[b].columns.push_back(k);
        }
        for(std::size_t i=0;i<equations.size();++i)if(!support[i].empty())
            blocks[block_index[equation_root(support[i].front())]].rows.push_back(i);
        Row change(count);
        for(const auto& block:blocks) {
            std::vector<Row> basis;std::vector<double> values;
            for(const auto i:block.rows) {
                Row row;row.reserve(block.columns.size());
                for(const auto k:block.columns)row.push_back(equations[i][k]);
                double value=residual[i];
                for(int pass=0;pass<2;++pass)for(std::size_t j=0;j<basis.size();++j) {
                    const double factor=dot(row,basis[j]);if(std::abs(factor)<1e-16)continue;
                    for(std::size_t k=0;k<row.size();++k)row[k]-=factor*basis[j][k];value-=factor*values[j];
                }
                const double norm=std::sqrt(dot(row,row));
                if(norm<1e-10){if(std::abs(value)>1e-7)return false;continue;}
                for(auto& element:row)element/=norm;basis.push_back(std::move(row));values.push_back(value/norm);
            }
            for(std::size_t j=0;j<basis.size();++j)for(std::size_t k=0;k<block.columns.size();++k)
                change[block.columns[k]]+=values[j]*basis[j][k];
        }
        bool accepted=false;const double before=error(solved);
        for(double step=1;step>=1.0/1024;step*=.5) {
            auto candidate=solved;for(std::size_t k=0;k<count;++k)candidate[k]+=step*change[k];
            if(std::ranges::any_of(candidate,[](double value){return !std::isfinite(value);}))continue;
            bool same_branch=true;
            for(const auto& d:nonlinear_distances)
                if((candidate[d.b]-candidate[d.a])*(coordinates[d.b]-coordinates[d.a])+
                   (candidate[d.b+1]-candidate[d.a+1])*(coordinates[d.b+1]-coordinates[d.a+1])<=0)same_branch=false;
            if(same_branch&&error(candidate)<before){solved=std::move(candidate);accepted=true;break;}
        }
        if(!accepted)return false;
    }
    if(error(solved)>1e-8)return false;
    for(const auto& row:positive_distances)if(dot(row,solved)<-1e-8)return false;
    for(const auto& c:sketch.constraints)if(!c.suppressed&&c.kind==ConstraintKind::EqualLength)
        if(dot(*segment_row(c.geometry_id),solved)<=1e-8||dot(*segment_row(c.second_geometry_id),solved)<=1e-8)return false;
    for(std::size_t i=0;i<sketch.points.size();++i){sketch.points[i].x=solved[i*2];sketch.points[i].y=solved[i*2+1];}
    return true;
}

bool seed_equal_length_components(Sketch& sketch,const std::vector<std::string>& anchors) {
    std::set<std::string> visited;
    std::set<std::string> local_geometry;
    for(const auto& line:sketch.segments)local_geometry.insert(line.id);
    for(const auto& circle:sketch.circles)local_geometry.insert(circle.id);
    for(const auto& arc:sketch.arcs)local_geometry.insert(arc.id);
    for(const auto& ellipse:sketch.ellipses)local_geometry.insert(ellipse.id);
    for(const auto& arc:sketch.elliptical_arcs)local_geometry.insert(arc.id);
    for(const auto& spline:sketch.bsplines)local_geometry.insert(spline.id);
    bool changed=false;
    for(const auto& equal:sketch.constraints) {
        if(equal.suppressed||equal.kind!=ConstraintKind::EqualLength||visited.contains(equal.geometry_id))continue;
        std::set<std::string> points,geometry;
        geometry.insert(equal.geometry_id);geometry.insert(equal.second_geometry_id);
        bool expanded=true;
        while(expanded) {
            const auto size=points.size()+geometry.size();
            for(const auto& line:sketch.segments)
                if(geometry.contains(line.id)||points.contains(line.first_point_id)||points.contains(line.second_point_id)) {
                    geometry.insert(line.id);points.insert(line.first_point_id);points.insert(line.second_point_id);
                }
            for(const auto& c:sketch.constraints)if(!c.suppressed&&
                (points.contains(c.first_point_id)||points.contains(c.second_point_id)||geometry.contains(c.geometry_id)||geometry.contains(c.second_geometry_id))) {
                for(const auto& id:{c.first_point_id,c.second_point_id})if(sketch.find_point(id))points.insert(id);
                for(const auto& id:{c.geometry_id,c.second_geometry_id})if(local_geometry.contains(id))geometry.insert(id);
            }
            for(const auto& d:sketch.dimensions)if(!d.suppressed&&d.driving&&
                (points.contains(d.first_point_id)||points.contains(d.second_point_id)||geometry.contains(d.geometry_id)||geometry.contains(d.second_geometry_id))) {
                for(const auto& id:{d.first_point_id,d.second_point_id})if(sketch.find_point(id))points.insert(id);
                for(const auto& id:{d.geometry_id,d.second_geometry_id})if(local_geometry.contains(id))geometry.insert(id);
            }
            expanded=size!=points.size()+geometry.size();
        }
        visited.insert(geometry.begin(),geometry.end());
        auto component=sketch;
        std::erase_if(component.points,[&](const auto& p){return !points.contains(p.id);});
        std::erase_if(component.segments,[&](const auto& line){return !geometry.contains(line.id);});
        const auto unused_curve=[&](const auto& c){return !geometry.contains(c.id)&&!points.contains(c.center_point_id);};
        std::erase_if(component.circles,unused_curve);std::erase_if(component.arcs,unused_curve);
        std::erase_if(component.ellipses,unused_curve);std::erase_if(component.elliptical_arcs,unused_curve);
        // Nonlinear and derived geometry remains an unsupported component.
        std::erase_if(component.constraints,[&](const auto& c){return c.suppressed||
            (!points.contains(c.first_point_id)&&!points.contains(c.second_point_id)&&!geometry.contains(c.geometry_id)&&!geometry.contains(c.second_geometry_id));});
        std::erase_if(component.dimensions,[&](const auto& d){return d.suppressed||!d.driving||
            (!points.contains(d.first_point_id)&&!points.contains(d.second_point_id)&&!geometry.contains(d.geometry_id)&&!geometry.contains(d.second_geometry_id));});
        if(std::ranges::none_of(component.constraints,[](const auto& c){return c.kind==ConstraintKind::PointOnLine;}))continue;
        if(!seed_rectilinear_equations(component,anchors,false,true)) {
            if(!component.circles.empty()||!component.arcs.empty()||!component.ellipses.empty()||
                !component.elliptical_arcs.empty()||!component.bsplines.empty()||
                !component.corner_radii.empty()||!component.curve_trims.empty()||!component.offsets.empty())continue;
            // Reuse the bounded simultaneous equation solver for oblique line
            // components. A fresh scratch Sketch has no copied point-index cache.
            auto nonlinear=Sketch::create_default();
            nonlinear.points=component.points;nonlinear.segments=component.segments;
            nonlinear.constraints=component.constraints;nonlinear.dimensions=component.dimensions;
            nonlinear.external_references=component.external_references;
            if(!seed_circular_equations(nonlinear,anchors,true))continue;
            component.points=std::move(nonlinear.points);
        }
        for(const auto& p:component.points)if(auto* target=sketch.find_point(p.id)) {
            changed=changed||target->x!=p.x||target->y!=p.y;target->x=p.x;target->y=p.y;
        }
    }
    return changed;
}
// A failed circular drag needs simultaneous point, radius and tangent updates.
// This bounded seed covers native line/circle/arc graphs; unsupported equations
// leave the transaction untouched and the ordinary solver verifies the result.
bool seed_circular_equations(Sketch& sketch,const std::vector<std::string>& anchors,bool line_component) {
    if((!line_component&&(sketch.arcs.empty()&&sketch.circles.empty()))||sketch.points.size()>32||
       !sketch.ellipses.empty()||!sketch.elliptical_arcs.empty()||
       !sketch.bsplines.empty()||!sketch.corner_radii.empty()||!sketch.offsets.empty()||!sketch.curve_trims.empty())return false;
    auto next=sketch;
    const auto point=[&](const std::string& id)->std::optional<std::array<double,2>> {
        if(id=="sketch_origin")return std::array{0.,0.};
        const auto* p=next.find_point(id);if(p)return std::array{p->x,p->y};
        const auto ref=std::ranges::find(next.external_references,id,&SketchExternalReference::id);
        if(ref!=next.external_references.end()&&!ref->broken&&ref->cached_points.size()==1)return ref->cached_points.front();
        return {};
    };
    const auto line=[&](const std::string& id)->std::optional<std::array<double,4>> {
        if(id=="sketch_axis:x")return std::array{0.,0.,1.,0.};
        if(id=="sketch_axis:y")return std::array{0.,0.,0.,1.};
        const auto s=std::ranges::find(next.segments,id,&SketchSegment::id);
        if(s==next.segments.end()) {
            const auto ref=std::ranges::find(next.external_references,id,&SketchExternalReference::id);
            if(ref==next.external_references.end())return {};
            const auto value=external_reference_line(*ref);if(!value)return {};
            const double length=std::hypot(value->second[0],value->second[1]);
            return std::array{value->first[0],value->first[1],value->second[0]/length,value->second[1]/length};
        }
        const auto a=point(s->first_point_id),b=point(s->second_point_id);if(!a||!b)return {};
        const double length=std::hypot((*b)[0]-(*a)[0],(*b)[1]-(*a)[1]);if(length<1e-10)return {};
        return std::array{(*a)[0],(*a)[1],((*b)[0]-(*a)[0])/length,((*b)[1]-(*a)[1])/length};
    };
    const auto circular=[&](const std::string& id)->std::optional<std::array<double,3>> {
        for(const auto& c:next.circles)if(c.id==id) {
            const auto p=point(c.center_point_id);return std::array{(*p)[0],(*p)[1],c.radius};
        }
        for(const auto& c:next.arcs)if(c.id==id) {
            const auto p=point(c.center_point_id);return std::array{(*p)[0],(*p)[1],c.radius};
        }
        return {};
    };
    // External supports are immutable inputs. Only native point coordinates and
    // radii below enter the simultaneous fit. Preserve the angular ray branch
    // from its input geometry while fitting the requested dimension value.
    std::unordered_map<std::string,double> angular_targets;
    for(const auto& d:next.dimensions)if(!d.suppressed&&d.driving&&d.kind==DimensionKind::AngleBetween) {
        const auto a=line(d.geometry_id),b=line(d.second_geometry_id);if(!a||!b)return false;
        const double measured=std::acos(std::clamp((*a)[2]*(*b)[2]+(*a)[3]*(*b)[3],-1.,1.))*180./std::acos(-1.);
        const double magnitude=std::abs(d.value),supplement=180.-magnitude;
        const double target=d.angle_sector==1&&std::abs(measured-supplement)<std::abs(measured-magnitude)?supplement:magnitude;
        const double cross=(*a)[2]*(*b)[3]-(*a)[3]*(*b)[2];
        angular_targets.emplace(d.id,std::copysign(target,cross));
    }
    std::set<std::string> derived_endpoints;
    for(const auto& arc:next.arcs)for(const auto& id:{arc.start_point_id,arc.end_point_id}) {
        const auto* p=next.find_point(id);
        if(!p || p->fixed || std::ranges::find(anchors,id)!=anchors.end())continue;
        const auto owners=std::ranges::count_if(next.arcs,[&](const auto& a){return a.start_point_id==id || a.end_point_id==id;});
        if(owners!=1 || std::ranges::any_of(next.segments,[&](const auto& l){return l.first_point_id==id || l.second_point_id==id;}))continue;
        if(std::ranges::any_of(next.constraints,[&](const auto& c){return !c.suppressed && (c.first_point_id==id || c.second_point_id==id);}) ||
           std::ranges::any_of(next.dimensions,[&](const auto& d){return !d.suppressed && d.driving && (d.first_point_id==id || d.second_point_id==id || d.third_geometry_id==id);}))continue;
        derived_endpoints.insert(id);
    }
    const auto equations=[&]()->std::optional<std::vector<double>> {
        std::vector<double> r;
        // A free arc end carries no independent equation. Preserve its angle
        // during radial fitting instead of rotating it outside a valid contact
        // domain merely to minimize displacement of its old coordinates.
        for(const auto& arc:next.arcs)for(const auto& [id,angle]:{
                std::pair{arc.start_point_id,arc.start_angle},std::pair{arc.end_point_id,arc.end_angle}}) {
            if(!derived_endpoints.contains(id))continue;
            auto* p=next.find_point(id);const auto c=point(arc.center_point_id);
            p->x=(*c)[0]+arc.radius*std::cos(angle);p->y=(*c)[1]+arc.radius*std::sin(angle);
        }
        for(const auto& c:next.circles)if(c.radius<=1e-9)return {};
        for(const auto& a:next.arcs) {
            if(a.radius<=1e-9)return {};
            const auto c=point(a.center_point_id);
            for(const auto& id:{a.start_point_id,a.end_point_id}) {
                const auto p=point(id);r.push_back(std::hypot((*p)[0]-(*c)[0],(*p)[1]-(*c)[1])-a.radius);
            }
        }
        for(const auto& c:next.constraints) {
            if(c.suppressed)continue;
            if(c.kind==ConstraintKind::PointReference||c.kind==ConstraintKind::Coincident) {
                const auto a=point(c.first_point_id),b=point(c.second_point_id);if(!a||!b)return {};
                r.push_back((*a)[0]-(*b)[0]);r.push_back((*a)[1]-(*b)[1]);
            } else if(c.kind==ConstraintKind::PointOnLine) {
                const auto p=point(c.first_point_id);const auto l=line(c.geometry_id);if(!p||!l)return {};
                r.push_back(((*p)[0]-(*l)[0])*(*l)[3]-((*p)[1]-(*l)[1])*(*l)[2]);
            } else if(c.kind==ConstraintKind::PointOnCircle) {
                const auto p=point(c.first_point_id);const auto circle=circular(c.geometry_id);if(!p||!circle)return {};
                r.push_back(std::hypot((*p)[0]-(*circle)[0],(*p)[1]-(*circle)[1])-(*circle)[2]);
            } else if(c.kind==ConstraintKind::Horizontal||c.kind==ConstraintKind::Vertical) {
                const auto a=point(c.first_point_id),b=point(c.second_point_id);if(!a||!b)return {};
                const int axis=c.kind==ConstraintKind::Horizontal?1:0;r.push_back((*b)[axis]-(*a)[axis]);
            } else if(c.kind==ConstraintKind::Parallel||c.kind==ConstraintKind::Perpendicular||c.kind==ConstraintKind::EqualLength) {
                const auto a=line(c.geometry_id),b=line(c.second_geometry_id);if(!a||!b)return {};
                if(c.kind==ConstraintKind::EqualLength) {
                    const auto sa=std::ranges::find(next.segments,c.geometry_id,&SketchSegment::id);
                    const auto sb=std::ranges::find(next.segments,c.second_geometry_id,&SketchSegment::id);
                    if(sa==next.segments.end()||sb==next.segments.end())return {};
                    const auto length=[&](const auto& s){const auto p=point(s.first_point_id),q=point(s.second_point_id);return std::hypot((*p)[0]-(*q)[0],(*p)[1]-(*q)[1]);};
                    r.push_back(length(*sa)-length(*sb));
                } else r.push_back(c.kind==ConstraintKind::Parallel?(*a)[2]*(*b)[3]-(*a)[3]*(*b)[2]:(*a)[2]*(*b)[2]+(*a)[3]*(*b)[3]);
            } else if(c.kind==ConstraintKind::Tangent) {
                const auto first=circular(c.geometry_id),second=circular(c.second_geometry_id);
                if(first && second) {
                    // Keep the persisted internal/external side; this is only
                    // a seed and the ordinary solver checks arc domains too.
                    const double target=c.tangent_internal?std::abs((*first)[2]-(*second)[2]):(*first)[2]+(*second)[2];
                    const double distance=std::hypot((*first)[0]-(*second)[0],(*first)[1]-(*second)[1]);
                    r.push_back(distance-target);
                    const auto a=std::ranges::find(next.arcs,c.geometry_id,&SketchArc::id);
                    const auto b=std::ranges::find(next.arcs,c.second_geometry_id,&SketchArc::id);
                    if(a!=next.arcs.end() && b!=next.arcs.end()) {
                        if(distance<1e-9)return {};
                        for(const auto& id:{a->start_point_id,a->end_point_id}) {
                            if(id!=b->start_point_id && id!=b->end_point_id)continue;
                            const auto p=point(id);
                            const double factor=(*first)[2]/distance*(c.tangent_internal&&(*first)[2]<(*second)[2]?-1.:1.);
                            r.push_back((*p)[0]-(*first)[0]-((*second)[0]-(*first)[0])*factor);
                            r.push_back((*p)[1]-(*first)[1]-((*second)[1]-(*first)[1])*factor);
                        }
                    }
                    continue;
                }
                const auto circle=first?first:second;
                const auto& circle_id=first?c.geometry_id:c.second_geometry_id;
                const auto& line_id=first?c.second_geometry_id:c.geometry_id;
                const auto l=line(line_id);const auto p=point(c.first_point_id);
                if(!circle||!l||!p)return {};
                const std::array center{(*circle)[0],(*circle)[1]};
                r.push_back(std::hypot((*p)[0]-center[0],(*p)[1]-center[1])-(*circle)[2]);
                r.push_back(((*p)[0]-(*l)[0])*(*l)[3]-((*p)[1]-(*l)[1])*(*l)[2]);
                r.push_back(((*p)[0]-center[0])*(*l)[2]+((*p)[1]-center[1])*(*l)[3]);
                // An arc endpoint on its tangent line is the unique tangent
                // contact, even when entry retained a separate contact point.
                // The circle-distance equation alone is singular there and
                // admits a tiny tangential drift outside the finite arc domain.
                const auto a=std::ranges::find(next.arcs,circle_id,&SketchArc::id);
                if(a!=next.arcs.end())for(const auto& id:{a->start_point_id,a->end_point_id})if(std::ranges::any_of(next.constraints,[&](const auto& support){
                    return !support.suppressed&&support.kind==ConstraintKind::PointOnLine&&
                        support.first_point_id==id&&support.geometry_id==line_id;
                })) {
                    const auto endpoint=point(id);
                    r.push_back(((*endpoint)[0]-center[0])*(*l)[2]+((*endpoint)[1]-center[1])*(*l)[3]);
                }
            } else if(c.kind==ConstraintKind::EqualRadius) {
                const auto a=std::ranges::find(next.arcs,c.geometry_id,&SketchArc::id),b=std::ranges::find(next.arcs,c.second_geometry_id,&SketchArc::id);
                if(a==next.arcs.end()||b==next.arcs.end())return {};r.push_back(a->radius-b->radius);
            } else if(c.kind==ConstraintKind::Symmetric) {
                const auto a=point(c.first_point_id),b=point(c.second_point_id);const auto l=line(c.geometry_id);if(!a||!b||!l)return {};
                const double projection=((*a)[0]-(*l)[0])*(*l)[2]+((*a)[1]-(*l)[1])*(*l)[3];
                r.push_back((*a)[0]+(*b)[0]-2*((*l)[0]+projection*(*l)[2]));
                r.push_back((*a)[1]+(*b)[1]-2*((*l)[1]+projection*(*l)[3]));
            } else return {};
        }
        for(const auto& d:next.dimensions) {
            if(d.suppressed||!d.driving)continue;
            if(d.kind==DimensionKind::Radius||d.kind==DimensionKind::Diameter) {
                const auto c=circular(d.geometry_id);if(!c)return {};
                r.push_back((*c)[2]-d.value*(d.kind==DimensionKind::Diameter?.5:1.));
            } else if(d.kind==DimensionKind::AngleBetween) {
                const auto a=line(d.geometry_id),b=line(d.second_geometry_id);if(!a||!b)return {};
                const double dot=(*a)[2]*(*b)[2]+(*a)[3]*(*b)[3],cross=(*a)[2]*(*b)[3]-(*a)[3]*(*b)[2];
                double residual=std::atan2(cross,dot)*180./std::acos(-1.)-angular_targets.at(d.id);
                while(residual>180.)residual-=360.;while(residual<-180.)residual+=360.;
                r.push_back(residual);
            } else if(d.kind==DimensionKind::Angle) {
                const auto a=point(d.first_point_id),b=point(d.second_point_id);if(!a||!b)return {};
                double residual=std::atan2((*b)[1]-(*a)[1],(*b)[0]-(*a)[0])*180./std::acos(-1.)-d.value;
                while(residual>180.)residual-=360.;while(residual<-180.)residual+=360.;r.push_back(residual);
            } else if(d.kind==DimensionKind::Distance||d.kind==DimensionKind::DistanceX||d.kind==DimensionKind::DistanceY) {
                const auto a=point(d.first_point_id),b=point(d.second_point_id);if(!a||!b)return {};
                const double x=(*b)[0]-(*a)[0],y=(*b)[1]-(*a)[1];
                r.push_back((d.kind==DimensionKind::Distance?std::hypot(x,y):d.kind==DimensionKind::DistanceX?x:y)-d.value);
            } else return {};
        }
        return r;
    };
    std::vector<double*> variables;
    for(auto& p:next.points)if(!p.fixed&&!derived_endpoints.contains(p.id)&&std::ranges::find(anchors,p.id)==anchors.end()){variables.push_back(&p.x);variables.push_back(&p.y);}
    for(auto& a:next.arcs)variables.push_back(&a.radius);
    for(auto& c:next.circles)variables.push_back(&c.radius);
    if(variables.empty())return false;
    const auto norm=[](const auto& r){double value=0;for(double v:r)value+=v*v;return value;};
    for(int iteration=0;iteration<48;++iteration) {
        const auto residual=equations();if(!residual)return false;
        if(std::ranges::all_of(*residual,[](double v){return std::isfinite(v)&&std::abs(v)<1e-9;})) {
            if(line_component)for(const auto& segment:sketch.segments) {
                const auto* a=sketch.find_point(segment.first_point_id);const auto* b=sketch.find_point(segment.second_point_id);
                const auto p=point(segment.first_point_id),q=point(segment.second_point_id);
                if(!a||!b||((*q)[0]-(*p)[0])*(b->x-a->x)+((*q)[1]-(*p)[1])*(b->y-a->y)<=1e-12)return false;
            }
            for(auto& a:next.arcs) {
                const auto c=point(a.center_point_id),p=point(a.start_point_id),q=point(a.end_point_id);
                const double old_start=a.start_angle,old_sweep=a.end_angle-a.start_angle;
                a.start_angle=std::atan2((*p)[1]-(*c)[1],(*p)[0]-(*c)[0]);
                while(a.start_angle-old_start>3.141592653589793)a.start_angle-=6.283185307179586;
                while(a.start_angle-old_start<-3.141592653589793)a.start_angle+=6.283185307179586;
                a.end_angle=std::atan2((*q)[1]-(*c)[1],(*q)[0]-(*c)[0]);
                while(a.end_angle<=a.start_angle)a.end_angle+=6.283185307179586;
                if(std::abs(a.end_angle-a.start_angle-old_sweep)>3.141592653589793)return false;
            }
            sketch=std::move(next);return true;
        }
        const auto n=variables.size();std::vector<std::vector<double>> jacobian(residual->size(),std::vector<double>(n));
        for(std::size_t j=0;j<n;++j) {
            const double original=*variables[j],step=1e-5;*variables[j]+=step;
            const auto shifted=equations();*variables[j]=original;if(!shifted)return false;
            for(std::size_t i=0;i<residual->size();++i)jacobian[i][j]=((*shifted)[i]-(*residual)[i])/step;
        }
        bool accepted=false;
        for(double damping:{1e-8,1e-6,1e-4,.01,1.}) {
            std::vector<std::vector<double>> matrix(n,std::vector<double>(n+1));
            for(std::size_t j=0;j<n;++j) {
                matrix[j][j]=damping;
                for(std::size_t i=0;i<residual->size();++i){matrix[j][n]-=jacobian[i][j]*(*residual)[i];for(std::size_t k=0;k<n;++k)matrix[j][k]+=jacobian[i][j]*jacobian[i][k];}
            }
            for(std::size_t j=0;j<n;++j) {
                std::size_t pivot=j;for(std::size_t k=j+1;k<n;++k)if(std::abs(matrix[k][j])>std::abs(matrix[pivot][j]))pivot=k;
                std::swap(matrix[j],matrix[pivot]);const double divisor=matrix[j][j];
                for(std::size_t k=j;k<=n;++k)matrix[j][k]/=divisor;
                for(std::size_t i=0;i<n;++i)if(i!=j){const double factor=matrix[i][j];for(std::size_t k=j;k<=n;++k)matrix[i][k]-=factor*matrix[j][k];}
            }
            std::vector<double> original;for(const auto v:variables)original.push_back(*v);
            for(double step=1;step>=1./128;step*=.5) {
                for(std::size_t j=0;j<n;++j)*variables[j]=original[j]+step*matrix[j][n];
                const auto candidate=equations();if(candidate&&norm(*candidate)<norm(*residual)){accepted=true;break;}
            }
            if(accepted)break;
            for(std::size_t j=0;j<n;++j)*variables[j]=original[j];
        }
        if(!accepted)return false;
    }
    return false;
}
} // namespace zima::sketcher
