#include <zima/document/bend.hpp>
#include <zima/document/sheet_state.hpp>
#include <zima/kernel/occt_kernel.hpp>
#include <iostream>
using namespace zima;
int main(int argc,char** argv) {
    int failures=0;
    const auto check=[&](document::PartDocument part,const std::string& label,std::string selected=std::string{},bool cut=false) {
        try {
            kernel::OcctKernel kernel;auto operations=part.kernel_operations();
            const auto before=kernel.evaluate_history(operations).back();
            kernel::HistoryOperation state;state.owner_id="cone-unbend";state.body=operations.back().body;
            kernel::SheetStateRequest request;if(!selected.empty()){request.all=false;request.owners={selected};}
            state.primitive=request;operations.push_back(state);
            const auto flat=kernel.evaluate_history(operations).back();
            if(flat.volume<=0)throw std::runtime_error("Empty development.");
            if(operations.front().sheet_material&&operations.size()==2) {
                using namespace kernel::sheet_material;
                const auto source=*operations.front().sheet_material;auto developed=source;developed.unfolded=true;
                const auto line=std::ranges::find_if(flat.mesh.axes,[](const auto& axis){return is_bend_line(axis.reference);});
                if(line==flat.mesh.axes.end())throw std::runtime_error("Revolved Sheet has no developed bend line.");
                const auto at=coordinates(developed,line->point);
                if(std::abs(at.depth-(source.thickness_sign<0?-source.thickness:0))>1e-8||
                   std::abs(at.length-source.angle*source.neutral_radius/2)>1e-8||std::abs(line->display_length-40)>1e-8)
                    throw std::runtime_error("Revolved Sheet bend line misses the inner-skin angular midpoint.");
                if(std::ranges::any_of(flat.mesh.axes,[&](const auto& axis){return axis.reference.owner_id==source.owner_id&&axis.reference.semantic_key=="axis:primary";}))
                    throw std::runtime_error("Unbend left the source rotation axis visible.");
            }
            state.owner_id="cone-back";state.primitive=kernel::SheetStateRequest{false,true,{}};operations.push_back(state);
            const auto back=kernel.evaluate_history(operations).back();
            if(std::abs(back.volume-before.volume)>1e-6)throw std::runtime_error("Original-state volume changed.");
            for(const auto& original:before.mesh.axes)if(original.reference.semantic_key=="axis:primary") {
                const auto restored=std::ranges::find_if(back.mesh.axes,[&](const auto& a){return a.reference==original.reference;});
                if(restored==back.mesh.axes.end()||restored->point!=original.point||restored->direction!=original.direction)
                    throw std::runtime_error("Bend Back did not restore the exact source rotation axis.");
            }
            if(cut) {
                using namespace kernel::sheet_material;
                const auto frame=*operations.front().sheet_material;auto developed=frame;developed.unfolded=true;
                const auto& sketch=part.sketches.front();
                const auto& segment=*std::ranges::find_if(sketch.segments,[](const auto& s){return !s.construction;});
                const auto& a=*sketch.find_point(segment.first_point_id);const auto& b=*sketch.find_point(segment.second_point_id);
                auto station=coordinates(frame,sketch.world_point((a.x+b.x)/2,(a.y+b.y)/2));
                station.length=frame.angle*frame.neutral_radius*.45;station.depth=0;
                const auto center=point(developed,station);const auto directions=basis(developed,station);
                kernel::ExtrusionRequest cutter;kernel::ExtrusionRequest::PolygonProfile rectangle;
                for(auto xy:{std::pair{-2.,-3.},std::pair{2.,-3.},std::pair{2.,3.},std::pair{-2.,3.}})
                    rectangle.vertices.push_back(add(center,add(mul(directions[0],xy.first),mul(directions[1],xy.second))));
                cutter.outer_profile=rectangle;cutter.direction=mul(directions[2],8);cutter.start_offset=-4;
                cutter.profile_region_id="cut-profile";cutter.outer_boundary_id="cut-boundary";
                cutter.outer_edge_source_ids={"a","b","c","d"};cutter.outer_vertex_source_ids={"p","q","r","s"};
                kernel::HistoryOperation removal;removal.owner_id="flat-cut";removal.body=state.body;
                removal.primitive=cutter;removal.operation=kernel::BooleanOperation::Subtract;
                operations.pop_back();operations.push_back(removal);const auto cut_flat=kernel.evaluate_history(operations).back();
                if(cut_flat.volume>=flat.volume-1)throw std::runtime_error("Flat cut missed the cone.");
                operations.push_back(state);
                if(kernel.evaluate_history(operations).back().volume>=before.volume-1)throw std::runtime_error("Bend Back lost the flat cone cut.");
                state.owner_id="cone-flat-again";state.primitive=kernel::SheetStateRequest{};operations.push_back(state);
                const auto again=kernel.evaluate_history(operations).back();
                if(std::abs(again.volume-cut_flat.volume)>1e-6)throw std::runtime_error("Repeated cone development changed an authored flat cut.");
                for(const auto& p:cut_flat.mesh.points) {
                    double distance=INFINITY;
                    for(const auto& q:again.mesh.points)distance=std::min(distance,std::sqrt(dot(sub(p.position,q.position),sub(p.position,q.position))));
                    if(distance>1e-6)throw std::runtime_error("Flat cone vertex accumulated round-trip error.");
                }
            }
            std::cout<<label<<" passed"<<std::endl;
        }catch(const std::exception& error){++failures;std::cerr<<label<<": "<<error.what()<<std::endl;}
    };
    {
        auto part=document::PartDocument::load(argc>1?argv[1]:"cpp/tests/fixtures/sheet/tilted-cone-with-bends.prtz");
        for(auto& feature:part.history)if(document::is_sheet_state(feature.feature_kind))feature.suppressed=true;
        check(part,"Native source");
        for(const auto& operation:part.kernel_operations())if(operation.sheet_material&&operation.sheet_material->kind!=kernel::SheetMaterialDefinition::Kind::Plane)
            check(part,"Native region "+operation.owner_id,operation.owner_id);
    }
    if(argc==1)for(double slope:{-20.,-10.,0.,10.,20.})for(bool reverse_axis:{false,true})for(bool reverse:{false,true}) {
        auto part=document::PartDocument::create_default();auto sketch=sketcher::Sketch::create_default();
        auto feature=document::PartDocument::create_revolution_container(sketch.id);document::initialize_sheet_revolution(feature,sketch,{});
        feature.revolution.thickness_override=true;feature.revolution.thin_thickness=2;feature.revolution.angle_degrees=90;
        feature.revolution.direction=reverse?document::ExtrusionDirection::Reverse:document::ExtrusionDirection::Forward;
        auto& axis=*std::ranges::find(sketch.segments,feature.revolution.axis_segment_id,&sketcher::SketchSegment::id);
        sketch.find_point(axis.first_point_id)->y=30+slope;sketch.find_point(axis.second_point_id)->y=30;
        if(reverse_axis)std::swap(axis.first_point_id,axis.second_point_id);
        part.history={feature};part.sketches={sketch};part.resolve_constructions();
        check(part,"Slope "+std::to_string(slope)+" axis "+std::to_string(reverse_axis)+" direction "+std::to_string(reverse),{},std::abs(slope)==10);
    }
    return failures?1:0;
}
