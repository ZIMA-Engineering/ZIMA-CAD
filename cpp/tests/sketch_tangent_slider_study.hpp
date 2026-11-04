#pragma once
#include "sketch_mechanism_study.hpp"
namespace zima::test::mechanism {
inline int run_tangent_slider() {
    unsigned cases{},edits{},drags{};
    for(double radius:{3.,30.,300.})for(double rotation:{0.,27.,90.})for(int side:{-1,1})
    for(bool reverse:{false,true})for(bool external_line:{false,true})for(bool external_point:{false,true})for(int state=0;state<3;++state) {
        try {
        auto s=Sketch::create_default();const double c=std::cos(rotation*radians),d=std::sin(rotation*radians);
        const auto world=[&](XY p){return XY{c*p[0]-d*p[1],d*p[0]+c*p[1]};};
        const auto anchored=world({side*radius,0});const auto anchor=s.add_point(anchored[0],anchored[1]),slider=s.add_point(0,0);
        if(external_point) {
            auto ref=Sketch::create_external_reference(ExternalReferenceKind::Point);ref.source_document_id="ground";ref.source_owner_id="pivot";
            ref.source_semantic_key="point";ref.cached_points={anchored};s.external_references.push_back(ref);
            static_cast<void>(s.add_point_reference_constraint(anchor,ref.id));
        } else s.find_point(anchor)->fixed=true;
        const auto first=world({0,-4*radius}),last=world({0,4*radius});std::string line;
        if(external_line) {
            auto ref=Sketch::create_external_reference(ExternalReferenceKind::Edge);ref.source_document_id="ground";ref.source_owner_id="guide";
            ref.source_semantic_key="line";ref.infinite=true;ref.cached_points={first,last};line=ref.id;s.external_references.push_back(ref);
        } else {
            line=s.add_segment(first[0],first[1],last[0],last[1],1e-7,true);
            const auto segment=s.segments.back();s.find_point(segment.first_point_id)->fixed=true;s.find_point(segment.second_point_id)->fixed=true;
        }
        static_cast<void>(s.add_point_on_line_constraint(slider,line));
        auto length=s.create_point_dimension(reverse?slider:anchor,reverse?anchor:slider);length.driving=state!=0;length.locked=state==2;
        s.apply_dimension(length);const auto sources=s.external_references;
        auto status=s.solve();require(status.remaining_degrees_of_freedom==(state==0?1:0),"Tangent slider confused an isolated contact with free motion");
        const auto check=[&] {
            const auto p=position(s,slider),a=position(s,anchor);
            require(std::abs(c*p[0]+d*p[1])<1e-7,"Tangent slider left its guide");
            require(std::hypot(a[0]-anchored[0],a[1]-anchored[1])<1e-8,"Tangent slider moved its pivot");
            const auto dim=std::ranges::find(s.dimensions,length.id,&SketchDimension::id);
            require(std::abs(std::hypot(p[0]-a[0],p[1]-a[1])-dim->value)<1e-7&&s.external_references==sources,"Tangent slider violated a dimension or read-only source");
        };
        check();
        if(state)for(double value:{1.2*radius,1.5*radius,radius}) {
            require(s.set_dimension_value(length.id,value),"Tangent slider rejected a feasible distance edit");check();++edits;
        }
        const auto before=s.serialized();
        if(state)require(!s.set_dimension_value(length.id,.9*radius)&&s.serialized()==before,"Tangent slider accepted an impossible distance");
        for(double along:{.2*radius,-.15*radius,.1*radius}) {
            const auto target=world({.013,along});const auto old=s.serialized();
            const bool moved=s.move_point(slider,target[0],target[1]);
            if(state==2)require(!moved&&s.serialized()==old,"An isolated locked tangent slider escaped");
            else {require(moved,"Free tangent slider rejected an ordinary off-guide drag");const auto p=position(s,slider);
                require(std::abs(-d*p[0]+c*p[1]-along)<1e-7,"Free tangent slider did not follow the permitted cursor component");check();++drags;}
        }
        const auto original=part(s);const auto file=std::filesystem::temp_directory_path()/("zima-tangent-slider-"+original.document_id+".prtz");
        original.save(file);const auto loaded=document::PartDocument::load(file);std::filesystem::remove(file);
        require(loaded.sketches[0].serialized()==original.sketches[0].serialized(),"Tangent slider changed after native reopen");++cases;
        }catch(const std::exception& e){throw std::runtime_error("Tangent slider radius="+std::to_string(radius)+" rotation="+std::to_string(rotation)+
            " side="+std::to_string(side)+" reverse="+std::to_string(reverse)+" external line="+std::to_string(external_line)+
            " external point="+std::to_string(external_point)+" state="+std::to_string(state)+": "+e.what());}
    }
    std::cout<<"Tangent slider: "<<cases<<" scale/orientation/order/support/lock cases, "<<edits<<" successive edits, "<<drags<<" moving drags passed\n";return 0;
}
}
