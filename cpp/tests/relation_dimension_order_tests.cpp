#include <zima/workspace/relation_operations.hpp>
#include <zima/workspace/family_operations.hpp>
#include <zima/document/part_document.hpp>
#include <zima/assembly/assembly_document.hpp>
#include <iostream>
#include <stdexcept>

namespace {
void check(bool ok,const char* message){if(!ok)throw std::runtime_error(message);}
template<class Doc> void verify(bool reverse,bool vertical,double sign) {
    using namespace zima;
    auto s=sketcher::Sketch::create_default();
    const auto pos=[&](double value){return vertical?std::array{0.,sign*value}:std::array{sign*value,0.};};
    const auto a=pos(0),b=pos(10);const auto line=s.add_segment(a[0],a[1],b[0],b[1]);const auto segment=s.segments.front();
    static_cast<void>(s.add_point_reference_constraint(segment.first_point_id,"sketch_origin"));
    static_cast<void>(s.add_point_pair_constraint(segment.first_point_id,segment.second_point_id,vertical?sketcher::ConstraintKind::Vertical:sketcher::ConstraintKind::Horizontal));
    const auto p=pos(9);const auto point=s.add_point(p[0],p[1],1e-8,true);static_cast<void>(s.add_point_on_line_constraint(point,line));
    auto contact=s.create_axis_dimension(point,vertical?"sketch_axis:x":"sketch_axis:y");s.apply_dimension(contact);
    auto extent=s.create_axis_dimension(segment.second_point_id,vertical?"sketch_axis:x":"sketch_axis:y");s.apply_dimension(extent);
    Doc doc=Doc::create_default();doc.sketches={s};
    const auto parameter=[&](const auto& d){return document::DimensionParameter{s.id,"dimension:"+d.id,s.name,document::DimensionQuantity::Length};};
    doc.dimension_identifiers.synchronize(reverse?std::vector{parameter(extent),parameter(contact)}:std::vector{parameter(contact),parameter(extent)});
    const auto symbol=[&](const auto& d){return doc.dimension_identifiers.identifier(s.id,"dimension:"+d.id);};
    doc.relations=symbol(contact)+" = travel\n"+symbol(extent)+" = travel + "+std::to_string(sign)+"\n";
    for(double target:{12.,18.,8.}) {
        doc.user_parameters["travel"]=std::to_string(sign*target);
        workspace::apply_relation_dimensions(doc,{});
        const auto& current=doc.sketches.front();const auto* q=current.find_point(point);const auto* end=current.find_point(segment.second_point_id);
        check(std::abs((vertical?q->y:q->x)-sign*target)<1e-8,"Relation contact did not reach final target");
        check(std::abs((vertical?end->y:end->x)-sign*(target+1))<1e-8,"Relation support did not reach final extent");
        check(std::abs(vertical?q->x:q->y)<1e-8,"Relation contact left its native support");
        const auto saved=current.serialized();workspace::apply_relation_dimensions(doc,{});check(doc.sketches.front().serialized()==saved,"Unchanged Relations changed solved geometry");
        doc.sketches.front()=sketcher::Sketch::from_serialized(saved);
    }
    auto invalid=doc;invalid.relations=symbol(contact)+" = "+std::to_string(sign*20)+"\n"+symbol(extent)+" = "+std::to_string(sign*9)+"\n";
    bool rejected=false;try{workspace::apply_relation_dimensions(invalid,{});}catch(const std::invalid_argument&){rejected=true;}
    check(rejected,"Impossible final relation contact was accepted");
}
}
int main() {
    try {
        for(bool reverse:{false,true})for(bool vertical:{false,true})for(double sign:{-1.,1.}) {
            verify<zima::document::PartDocument>(reverse,vertical,sign);
            verify<zima::assembly::AssemblyDocument>(reverse,vertical,sign);
        }
        std::cout<<"16 Part/Assembly relation-order cases: growing/shrinking finite supports, both allocation orders, axes and signs, repeated edits, unchanged no-op and impossible final contacts passed\n";return 0;
    }catch(const std::exception& error){std::cerr<<error.what()<<'\n';return 1;}
}
