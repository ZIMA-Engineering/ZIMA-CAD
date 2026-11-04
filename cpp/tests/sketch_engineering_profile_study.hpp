#pragma once
#include "sketch_mechanism_study.hpp"

namespace zima::test::engineering {
using namespace mechanism;
struct Bracket {
    Sketch sketch=Sketch::create_default();
    std::vector<std::string> points,edges,dimensions;
    double rotation{};
    int side=1;
    XY world(XY p) const {
        const double c=std::cos(rotation*radians),s=std::sin(rotation*radians);
        return {c*p[0]-s*side*p[1],s*p[0]+c*side*p[1]};
    }
};
// MIT ESP Training, pp. 96 and 99: the same U section dimensioned by
// overall length or slot width. Coordinates below use a uniform scale of 10.
inline Bracket bracket(double rotation,int side,bool reverse,bool external,bool reverse_creation,bool slot_width) {
    Bracket f;f.rotation=rotation;f.side=side;auto& s=f.sketch;
    const double length=slot_width?30:40,height=30,thickness=5;
    for(const XY p:{XY{0,0},{length,0},{length,height},{length-thickness,height},
        {length-thickness,thickness},{thickness,thickness},{thickness,height},{0,height}}) {
        const auto q=f.world(p);f.points.push_back(s.add_point(q[0],q[1]));
    }
    s.find_point(f.points[0])->fixed=true;
    for(unsigned i=0;i<8;++i) {
        const auto a=position(s,f.points[i]),b=position(s,f.points[(i+1)%8]);
        f.edges.push_back(s.add_segment(a[0],a[1],b[0],b[1]));
    }
    auto direction=s.create_segment_dimension(f.edges[0],DimensionKind::Angle);direction.locked=true;s.apply_dimension(direction);
    std::string reference=f.edges[0];
    if(external) {
        auto ref=Sketch::create_external_reference(ExternalReferenceKind::Edge);
        ref.source_document_id="immutable-datum";ref.source_owner_id="datum";ref.source_semantic_key="direction";
        ref.cached_points={f.world({-10,0}),f.world({50,0})};ref.infinite=true;
        reference=ref.id;s.external_references.push_back(ref);
    }
    std::vector<unsigned> order{1,2,3,4,5,6,7};if(reverse_creation)std::ranges::reverse(order);
    for(const auto i:order) {
        // Native pair order is retained. An external line is explicitly the
        // immutable reference; GUI selection order is tested separately.
        static_cast<void>(s.add_segment_pair_constraint(reverse&&!external?f.edges[i]:reference,
            reverse&&!external?reference:f.edges[i],i%2?ConstraintKind::Perpendicular:ConstraintKind::Parallel));
    }
    const auto a=position(s,f.points[2]),b=position(s,f.points[7]);
    const auto top=s.add_segment(a[0],a[1],b[0],b[1],1e-6,true);
    static_cast<void>(s.add_segment_pair_constraint(reverse&&!external?top:reference,
        reverse&&!external?reference:top,ConstraintKind::Parallel));
    auto width=slot_width?s.create_line_pair_dimension(reverse?f.edges[5]:f.edges[3],reverse?f.edges[3]:f.edges[5],DimensionKind::DistanceLine):s.create_segment_dimension(f.edges[0]);
    std::vector<SketchDimension> dimensions{width,s.create_segment_dimension(f.edges[1]),s.create_segment_dimension(f.edges[2]),
        s.create_segment_dimension(f.edges[6]),s.create_line_pair_dimension(reverse?f.edges[4]:f.edges[0],reverse?f.edges[0]:f.edges[4],DimensionKind::DistanceLine)};
    for(auto& dim:dimensions){dim.locked=true;f.dimensions.push_back(dim.id);s.apply_dimension(dim);}
    return f;
}
inline void equations(const Sketch& s) {
    const auto line=[&](const std::string& id)->std::array<double,4> {
        XY a{},b{};
        const auto found=std::ranges::find(s.segments,id,&SketchSegment::id);
        if(found!=s.segments.end()){a=position(s,found->first_point_id);b=position(s,found->second_point_id);}
        else {const auto ref=std::ranges::find(s.external_references,id,&SketchExternalReference::id);
            require(ref!=s.external_references.end()&&ref->cached_points.size()==2,"Missing independent line support");a=ref->cached_points[0];b=ref->cached_points[1];}
        const double l=std::hypot(b[0]-a[0],b[1]-a[1]);require(l>1e-8,"Collapsed engineering profile edge");
        return {a[0],a[1],(b[0]-a[0])/l,(b[1]-a[1])/l};
    };
    for(const auto& c:s.constraints)if(!c.suppressed) {
        require(c.kind==ConstraintKind::Parallel||c.kind==ConstraintKind::Perpendicular,"Unverified engineering relation");
        const auto a=line(c.geometry_id),b=line(c.second_geometry_id);
        const double residual=c.kind==ConstraintKind::Parallel?a[2]*b[3]-a[3]*b[2]:a[2]*b[2]+a[3]*b[3];
        require(std::abs(residual)<1e-8,"Engineering parallel/perpendicular equation failed");
    }
    for(const auto& d:s.dimensions)if(!d.suppressed) {
        double actual{};
        if(d.kind==DimensionKind::DistanceLine) {
            const auto a=line(d.geometry_id),b=line(d.second_geometry_id);
            actual=(a[2]*(b[1]-a[1])-a[3]*(b[0]-a[0]))*d.solution_side;
        } else {
            const auto a=position(s,d.first_point_id),b=position(s,d.second_point_id);
            actual=d.kind==DimensionKind::Distance?std::hypot(b[0]-a[0],b[1]-a[1]):
                d.value+std::remainder(std::atan2(b[1]-a[1],b[0]-a[0])/radians-d.value,360.);
        }
        require(std::abs(actual-d.value)<1e-7,"Engineering dimension equation failed");
    }
}
inline int run() {
    unsigned passed{},failed{},edits{},drags{},rejected{};
    for(bool slot_width:{false,true})for(double rotation:{0.,90.,27.})for(int side:{-1,1})
    for(bool reverse:{false,true})for(bool external:{false,true})for(bool reverse_creation:{false,true})
    for(unsigned active=0;active<5;++active)for(int state=0;state<3;++state) {
        const auto label="U"+std::to_string(slot_width)+" rotation="+std::to_string(rotation)+" side="+std::to_string(side)+
            " reverse="+std::to_string(reverse)+" external="+std::to_string(external)+" order="+std::to_string(reverse_creation)+
            " dimension="+std::to_string(active)+" state="+std::to_string(state);
        if(const auto* filter=std::getenv("ZIMA_ENGINEERING_FILTER");filter&&label.find(filter)==std::string::npos)continue;
        try {
            auto f=bracket(rotation,side,reverse,external,reverse_creation,slot_width);auto& s=f.sketch;
            const auto sources=s.external_references;
            const auto contacts=s.constraints;
            auto dim=*std::ranges::find(s.dimensions,f.dimensions[active],&SketchDimension::id);
            dim.driving=state!=0;dim.locked=state==2;s.apply_dimension(dim);equations(s);
            const auto status=s.solve();require(status.remaining_degrees_of_freedom==(state==0?1:0),"Unexpected U profile DOF");
            if(state)for(double value:{dim.value*1.1,dim.value*.9,dim.value}) {
                require(s.set_dimension_value(dim.id,value),"Rejected reachable U profile dimension edit");equations(s);++edits;
            }
            const std::array<unsigned,5> handles{slot_width?4u:1u,2,slot_width?1u:3u,6,5};const auto handle=f.points[handles[active]];
            for(double delta:{1.,-1.5,.7}) {
                const auto old=position(s,handle);const auto movement=f.world(active==1||active==4?XY{.13,delta}:XY{delta,.13});
                const auto before=s.serialized();const bool moved=s.move_point(handle,old[0]+movement[0],old[1]+movement[1]);
                if(state==2){require(!moved&&s.serialized()==before,"Fully locked U profile escaped");++rejected;}
                else {require(moved,"Rejected free U profile drag");const auto changed=position(s,handle);
                    require(std::hypot(changed[0]-old[0],changed[1]-old[1])>1e-4,"U drag reported no movement");equations(s);++drags;}
            }
            require(s.external_references==sources&&s.constraints==contacts,"Changed U references or ordered relation identities");
            const auto before=s.serialized();bool rejected_duplicate=false;
            try {static_cast<void>(s.add_segment_pair_constraint(contacts.front().second_geometry_id,contacts.front().geometry_id,contacts.front().kind));}
            catch(const std::exception&){rejected_duplicate=true;}
            require(rejected_duplicate&&s.serialized()==before,"Redundant reverse U relation changed the document");
            auto fixed=s;for(auto& p:fixed.points)p.fixed=true;
            const auto frozen=fixed.serialized();require(!fixed.set_dimension_value(fixed.dimensions.front().id,1234)&&fixed.serialized()==frozen,"Impossible fixed U dimension was committed");
            auto initial=part(s);document::DocumentSession session(initial);auto edited=initial;
            auto changed_dim=*std::ranges::find(edited.sketches[0].dimensions,f.dimensions[1],&SketchDimension::id);
            changed_dim.driving=true;changed_dim.value+=1;edited.sketches[0].apply_dimension(changed_dim);equations(edited.sketches[0]);
            session.commit_metadata(edited);require(session.undo()&&session.document().sketches[0].serialized()==initial.sketches[0].serialized()&&
                session.redo()&&session.document().sketches[0].serialized()==edited.sketches[0].serialized(),"U profile Undo/Redo failed");
            const auto file=std::filesystem::temp_directory_path()/("zima-engineering-"+initial.document_id+".prtz");
            edited.save(file);const auto loaded=document::PartDocument::load(file);std::filesystem::remove(file);
            require(loaded.sketches[0].serialized()==edited.sketches[0].serialized(),"U native reopen changed its definition");equations(loaded.sketches[0]);
            ++passed;
        } catch(const std::exception& e){++failed;std::cerr<<"FAIL "<<label<<": "<<e.what()<<'\n';}
    }
    std::cout<<"Engineering U profile study: "<<passed<<" passed, "<<failed<<" failed; "<<edits<<" numeric edits, "<<drags<<" moving drags, "<<rejected<<" locked rejections\n";
    return failed?1:0;
}
}
