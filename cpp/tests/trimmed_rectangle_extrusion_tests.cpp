#include "trimmed_rectangle_arc_study.hpp"
#include <zima/document/document_session.hpp>
#include <zima/document/viewer_packet_json.hpp>
#include <zima/kernel/occt_kernel.hpp>
#include <iostream>

int main() {
    using namespace zima;namespace fs=std::filesystem;
    try {
        const auto fixture=fs::path(__FILE__).parent_path()/"fixtures/sketch/trimmed-rectangle-circle.json";
        std::ifstream input(fixture);trimmed_rectangle_study::check(bool(input),"Extrusion fixture missing");
        const auto source=sketcher::Sketch::from_serialized(std::string(std::istreambuf_iterator<char>(input),{}));
        kernel::OcctKernel kernel;kernel::AggregatePropertyScope properties(kernel::AggregatePropertyDemand::All);
        const auto check=trimmed_rectangle_study::check;
        const auto parent=fs::canonical(fs::temp_directory_path());
        const auto directory=parent/("zima-rectangle-extrusion-"+document::PartDocument::create_default().document_id);fs::create_directory(directory);
        for(unsigned variant=0;variant<3;++variant) {
            auto sketch=variant?trimmed_rectangle_study::retrim(source,variant==2):source;
            if(variant) {
                sketch.apply_dimension(sketch.create_arc_radius_dimension(sketch.arcs.front().id));
                const auto point=std::ranges::find_if(sketch.constraints,[](const auto& c){return c.kind==sketcher::ConstraintKind::Tangent;})->first_point_id;
                sketch.apply_dimension(sketch.create_point_dimension(point,source.dimensions.back().second_point_id,sketcher::DimensionKind::Distance));
            }
            auto model=document::PartDocument::create_default();auto feature=document::PartDocument::create_extrusion_container(sketch.id);
            sketch.owner_container_id=feature.id;feature.extrusion.length_forward=50;
            model.history={feature};model.sketches={sketch};document::BodyHistoryGraph graph;graph.create_body("Trim extrusion");
            graph.insert({document::PartHistoryKind::Feature,feature.id});model.set_body_history(graph);model.resolve_constructions();
            const auto calculate=[&](const auto& definition) {
                auto bodies=kernel.evaluate_history(definition.kernel_operations());
                check(!bodies.empty()&&std::ranges::all_of(bodies,[](const auto& b){return b.calculation_errors.empty();}),"Trimmed profile extrusion failed");
                const auto& s=definition.sketches.front();trimmed_rectangle_study::equations(s);
                const auto& arc=s.arcs.front();const double r=arc.radius;
                const auto& width=s.dimensions.back();const auto* p=s.find_point(width.first_point_id);const auto* q=s.find_point(width.second_point_id);
                const double w=std::hypot(p->x-q->x,p->y-q->y);
                const auto* start=s.find_point(arc.start_point_id);const auto* end=s.find_point(arc.end_point_id);
                const double height=std::hypot(start->x-end->x,start->y-end->y);
                const double sweep=arc.end_angle-arc.start_angle;
                const double expected=(height*w+r*r*(sweep-std::sin(sweep))/2)*50;
                kernel.ensure_body_properties(bodies.back());
                if(std::abs(bodies.back().volume-expected)>=expected*1e-10)
                    std::cerr.precision(17),std::cerr<<"Extrusion expected="<<expected<<" actual="<<bodies.back().volume<<" width="<<w<<" height="<<height<<" radius="<<r<<" sweep="<<sweep<<std::endl;
                check(std::abs(bodies.back().volume-expected)<expected*1e-10,"Trimmed extrusion violates independent analytic volume");
                return bodies;
            };
            document::DocumentSession session(model,calculate(model));
            for(double factor:{1.04,.97,1.02})for(unsigned dimension=0;dimension<2;++dimension) {
                const auto previous_definition=session.document().serialized();const auto previous_packet=document::serialize_body_result(session.calculated_boundaries().back());
                auto changed=session.document();auto& s=changed.sketches.front();
                check(s.set_dimension_value(s.dimensions[dimension].id,s.dimensions[dimension].value*factor),"Trim extrusion rejected dimension edit");
                session.commit(changed,calculate(changed));
                const auto definition=session.document().serialized();const auto packet=document::serialize_body_result(session.calculated_boundaries().back());
                check(session.undo()&&session.document().serialized()==previous_definition&&document::serialize_body_result(session.calculated_boundaries().back())==previous_packet,"Trim extrusion Undo changed exact input packet");
                check(session.redo()&&session.document().serialized()==definition&&document::serialize_body_result(session.calculated_boundaries().back())==packet,"Trim extrusion Redo changed exact output packet");
                const auto path=directory/(std::to_string(variant)+".prtz");session.document().save(path,session.calculated_boundaries());
                std::vector<kernel::BodyResult> reopened;const auto restored=document::PartDocument::load(path,&reopened);
                check(restored.serialized()==definition&&document::serialize_body_result(reopened.back())==packet,"Trim extrusion native reopen changed definition or exact packet");
            }
        }
        check(fs::canonical(directory).parent_path()==parent,"Unsafe extrusion fixture cleanup");fs::remove_all(directory);
        std::cout<<"Trimmed rectangle Extrusion: 18 successive edits, analytic volume, exact packet Undo/Redo and native save/reopen passed"<<std::endl;return 0;
    }catch(const std::exception& e){std::cerr<<e.what()<<std::endl;return 1;}
}
