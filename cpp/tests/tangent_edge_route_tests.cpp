#include <zima/kernel/tangent_edge_route.hpp>
#include <zima/kernel/edge_treatment_route_plan.hpp>
#include <iostream>
#include <numbers>
using namespace zima::kernel;
void check(bool ok,const char* message){if(!ok)throw std::runtime_error(message);}
ViewerEdge edge(std::string id,Vec3 from,Vec3 to,std::string first,std::string last,std::string path={}) {
    ViewerEdge result;result.reference={"feature-"+id,id,path};result.points={from,to};
    result.edge_treatment_endpoint_references={{"points",first,path},{"points",last,path}};
    BSplineGeometry exact;exact.degree=1;exact.poles={from,to};
    exact.knots={0,0,1,1};exact.weights={1,1};exact.validate();
    result.exact_spline=std::move(exact);return result;
}
int main(){try {
    auto a=edge("a",{0,0,0},{1,0,0},"v0","v1");auto b=edge("b",{1,0,0},{2,0,0},"v1","v2");
    auto route=tangent_edge_route({a,b},a.reference);check(route==std::vector<EdgeReference>{a.reference,b.reference},"Tangent route did not cross source owners at a shared native point");
    auto separate=b;separate.edge_treatment_endpoint_references.front().semantic_key="different-point";
    check(tangent_edge_route({a,separate},a.reference).size()==1,"Spatial coincidence replaced exact endpoint identity");
    auto branch=edge("branch",{1,0,0},{2,.1,0},"v1","branch-end");
    check(tangent_edge_route({a,b,branch},a.reference)==
        std::vector<EdgeReference>{a.reference,b.reference},
        "The unique exact tangent continuation stopped at an unrelated branch");
    auto tee=edge("tee",{1,0,0},{1,1,0},"v1","tee-end");
    check(tangent_edge_route({a,b,tee},a.reference)==
        std::vector<EdgeReference>{a.reference,b.reference},
        "The unique exact tangent continuation stopped at a T junction");
    auto ambiguous_tangent=edge("ambiguous-tangent",{1,0,0},{3,0,0},
        "v1","ambiguous-end");
    check(tangent_edge_route({a,b,ambiguous_tangent},a.reference).size()==1,
        "A genuinely ambiguous tangent junction was traversed");
    {
        auto left=edge("left",{-1,1,0},{0,1,0},"left-end","arc-start");
        auto arc=edge("radius",{0,1,0},{1,0,0},"arc-start","arc-end");
        arc.reference={"previous-fillet","fillet:edge:from:radius",{}};
        const double weight=std::sqrt(.5);
        BSplineGeometry exact;exact.degree=2;
        exact.poles={{0,1,0},{1,1,0},{1,0,0}};
        exact.knots={0,0,0,1,1,1};exact.weights={1,weight,1};
        exact.validate();arc.exact_spline=std::move(exact);
        auto right=edge("right",{1,0,0},{1,-1,0},"arc-end","right-end");
        const auto mixed=tangent_edge_route({right,arc,left,tee},arc.reference);
        check(mixed==std::vector<EdgeReference>{left.reference,arc.reference,right.reference},
            "A line-radius-line route was not returned as three ordered segments");
    }
    auto back=edge("back",{1,0,0},{.2,.1,0},"v1","back-end");
    check(tangent_edge_route({a,back},a.reference).size()==1,"Route doubled back on itself");
    auto tangent_return=edge("tangent-return",{1,0,0},{.25,0,0},
        "v1","return-end");
    check(tangent_edge_route({a,tangent_return},a.reference).size()==1,
        "A sharp tangent return was mistaken for forward continuation");
    auto beyond_return=edge("beyond-return",{.25,0,0},{-.5,0,0},
        "return-end","beyond-end");
    check(tangent_edge_route({a,tangent_return,beyond_return},a.reference).size()==1,
        "A sharp tangent return propagated into a different smooth chain");
    auto reversed_storage=edge("reversed-storage",{2,0,0},{1,0,0},
        "v2","v1");
    check(tangent_edge_route({a,reversed_storage},a.reference)==
            std::vector<EdgeReference>{a.reference,reversed_storage.reference},
        "A forward continuation depended on stored point order");
    const auto shallow_angle=.5*std::numbers::pi/180.;
    auto shallow=edge("shallow",{1,0,0},
        {1+std::cos(shallow_angle),std::sin(shallow_angle),0},
        "v1","shallow-end");
    check(tangent_edge_route({a,shallow},a.reference).size()==1,
        "A real shallow corner was mistaken for exact tangent continuation");
    auto missing_exact=b;missing_exact.exact_spline.reset();
    check(tangent_edge_route({a,missing_exact},a.reference).size()==1,
        "Display chords replaced missing exact tangent geometry");
    auto copy_a=a,copy_b=b;copy_a.reference.instance_path="copy";copy_b.reference.instance_path="copy";
    check(tangent_edge_route({a,b,copy_a,copy_b},a.reference).size()==2&&tangent_edge_route({a,b,copy_a,copy_b},copy_a.reference).size()==2,"Tangent route crossed repeated occurrences");
    std::vector<ViewerEdge> circle;
    for(int i=0;i<12;++i) {
        const auto angle=i*std::numbers::pi/6,next=(i+1)*std::numbers::pi/6;
        auto arc=edge("circle-"+std::to_string(i),
            {std::cos(angle),std::sin(angle),0},
            {std::cos(next),std::sin(next),0},
            "circle-point-"+std::to_string(i),
            "circle-point-"+std::to_string((i+1)%12));
        const double middle=(angle+next)/2.;
        const double weight=std::cos((next-angle)/2.);
        BSplineGeometry exact;exact.degree=2;
        exact.poles={{std::cos(angle),std::sin(angle),0},
            {std::cos(middle)/weight,std::sin(middle)/weight,0},
            {std::cos(next),std::sin(next),0}};
        exact.knots={0,0,0,1,1,1};exact.weights={1,weight,1};
        exact.validate();arc.exact_spline=std::move(exact);
        circle.push_back(std::move(arc));
    }
    check(tangent_edge_route(circle,circle.front().reference).size()==12,
        "Exact smooth closed route followed its coarse display chords");
    auto duplicate=a;duplicate.points={{0,2,0},{1,2,0}};
    check(tangent_edge_route({a,duplicate},a.reference).empty(),"An ambiguous seed identity was guessed");
    check(tangent_edge_route({a,duplicate},a.reference,1e-6,0).size()==1,"Exact offered seed index was ignored");
    {
        auto first=a,second=b;second.reference=first.reference;
        for(bool reverse_order:{false,true})for(bool reverse_direction:{false,true}) {
            auto x=first,y=second;
            if(reverse_direction) {
                std::ranges::reverse(x.points);std::ranges::reverse(x.edge_treatment_endpoint_references);
                std::ranges::reverse(y.points);std::ranges::reverse(y.edge_treatment_endpoint_references);
            }
            const std::vector<const ViewerEdge*> segments=reverse_order?
                std::vector<const ViewerEdge*>{&y,&x}:std::vector<const ViewerEdge*>{&x,&y};
            const auto chain=edge_treatment_chain(segments);
            check(chain&&chain->endpoints_complete&&chain->endpoints.size()==2,
                "A periodic split lost its two original route ends");
            check(chain->endpoints.front().reference.semantic_key=="v0"&&
                chain->endpoints.back().reference.semantic_key=="v2",
                "An internal seam replaced an original route endpoint");
            const std::vector<ViewerEdge> values=reverse_order?std::vector{y,x}:std::vector{x,y};
            for(const auto picked:{std::optional<std::size_t>{},std::optional<std::size_t>{0},std::optional<std::size_t>{1}})
                check(tangent_edge_route(values,first.reference,1e-6,picked)==std::vector{first.reference},
                    "One split curve produced duplicate persisted route entries");
        }
        auto disconnected=second;disconnected.edge_treatment_endpoint_references.front().semantic_key="other-v1";
        check(!edge_treatment_chain({&first,&disconnected}),"Coordinate proximity joined disconnected persisted segments");
        auto branching=branch;branching.reference=first.reference;
        check(!edge_treatment_chain({&first,&second,&branching}),"Branching fragments were treated as one curve");
        check(!edge_treatment_chain({&first,&duplicate}),"Inconsistent copies of one persisted vertex were accepted");
        auto repeated=first;
        check(!edge_treatment_chain({&first,&repeated}),"Duplicate geometry was mistaken for a closed curve");
        std::ranges::reverse(repeated.points);std::ranges::reverse(repeated.edge_treatment_endpoint_references);
        check(!edge_treatment_chain({&first,&repeated}),"Reversed duplicate geometry was mistaken for a closed curve");
        auto foreign=second;foreign.reference.instance_path="other";
        check(!edge_treatment_chain({&first,&foreign}),"Connected-curve classification crossed occurrences");
        auto missing=second;missing.edge_treatment_endpoint_references.clear();
        check(!edge_treatment_chain({&first,&missing}),"An unproven split curve was accepted");
        auto closing=edge("close",{2,0,0},{0,0,0},"v2","v0");closing.reference=first.reference;
        const auto closed=edge_treatment_chain({&first,&second,&closing});
        check(closed&&closed->endpoints_complete&&closed->endpoints.empty(),"A connected closed curve invented free ends");
        auto upper=first,lower=first;upper.points={{0,0,0},{.5,.5,0},{1,0,0}};lower.points={{0,0,0},{.5,-.5,0},{1,0,0}};
        const auto two_arcs=edge_treatment_chain({&upper,&lower});
        check(two_arcs&&two_arcs->endpoints_complete&&two_arcs->endpoints.empty(),"Distinct halves of a closed curve were rejected as duplicates");
        auto unknown=first;unknown.edge_treatment_endpoint_references.clear();
        const auto incomplete=edge_treatment_chain({&unknown});
        check(incomplete&&!incomplete->endpoints_complete&&incomplete->endpoints.empty(),"A standalone circle invented endpoints");
    }
    auto empty=a;empty.points.clear();check(tangent_edge_route({empty},empty.reference).empty(),"Empty seed geometry was traversed");
    check(tangent_edge_route({a,b},{"missing","missing",{}}).empty(),"Missing seed was guessed");
    check(tangent_edge_route({a,b},a.reference,0).size()==2,"Exactly collinear route was rejected");
    {
        auto first=a,continuation=b,turned=b;
        turned.reference={"feature-turned","turned",{}};
        const auto publish_material=[](ViewerEdge& value,
                Vec3 first,Vec3 second) {
            value.edge_treatment_side_directions={
                std::vector(value.points.size(),first),
                std::vector(value.points.size(),second)};
        };
        publish_material(first,{0,1,0},{0,0,1});
        publish_material(continuation,{0,1,0},{0,0,1});
        publish_material(turned,{1,0,0},{0,0,1});
        check(tangent_edge_route({first,continuation},first.reference)==
                std::vector<EdgeReference>{first.reference,
                    continuation.reference},
            "A tangent route with the same material sides was stopped");
        check(tangent_edge_route({first,turned},first.reference)==
                std::vector<EdgeReference>{first.reference},
            "A tangent edge crossed a same-angle material wedge turned onto another side");
        auto fillet_boundary=first;
        fillet_boundary.reference={"previous-fillet",
            "fillet:edge:from:source",{}};
        check(tangent_edge_route({fillet_boundary,continuation},
                fillet_boundary.reference)==
                std::vector<EdgeReference>{fillet_boundary.reference,
                    continuation.reference},
            "A forward current Fillet edge stopped a compatible tangent route");
        check(tangent_edge_route({fillet_boundary,continuation},
                continuation.reference)==
                std::vector<EdgeReference>{fillet_boundary.reference,
                    continuation.reference},
            "A compatible route could not enter a current Fillet edge");
        auto chamfer_boundary=first;
        chamfer_boundary.reference={"previous-chamfer",
            "chamfer:edge:from:source",{}};
        check(tangent_edge_route({chamfer_boundary,continuation},
                chamfer_boundary.reference)==
                std::vector<EdgeReference>{chamfer_boundary.reference,
                    continuation.reference},
            "A forward current Chamfer edge stopped a compatible tangent route");
        auto fillet_return=tangent_return;
        fillet_return.reference={"previous-returning-fillet",
            "fillet:edge:from:returning-source",{}};
        publish_material(fillet_return,{0,1,0},{0,0,1});
        check(tangent_edge_route({first,fillet_return},first.reference)==
                std::vector<EdgeReference>{first.reference},
            "A tangent but reversing current Fillet arc was selected");
        auto inherited_key=first;
        inherited_key.reference={"later-boolean",
            "boolean:add:split-edge:from:fillet:edge:from:source",{}};
        check(tangent_edge_route({inherited_key,continuation},
                inherited_key.reference)==
                std::vector<EdgeReference>{inherited_key.reference,
                    continuation.reference},
            "A later Boolean edge was mistaken for a Fillet boundary from nested ancestry");
    }
    {
        const std::vector body{a,b,tee};
        const std::array<std::size_t,2> selected{1,0};
        const auto plan=build_edge_treatment_route_plan(body,selected);
        check(plan&& !plan->closed&&plan->segments.size()==2&&
            plan->terminals.size()==2,
            "Open treatment route did not produce two stable terminals");
        check(edge_treatment_route_supports_linear_fillet(*plan),
            "An open route with two stable terminals rejected R1/R2");
        check(plan->segments[0].edge_index==0&&
            plan->segments[0].start.semantic_key=="v0"&&
            plan->segments[1].edge_index==1&&
            plan->segments[1].end.semantic_key=="v2",
            "Treatment route depended on selected edge order");
        check(plan->terminals[0].reference.semantic_key=="v0"&&
            plan->terminals[1].reference.semantic_key=="v2"&&
            plan->terminals[0].body_incidence==1&&
            plan->terminals[1].body_incidence==1,
            "Treatment route terminal incidence is incorrect");
        const auto reversed_plan=build_edge_treatment_route_plan(body,
            selected,b.edge_treatment_endpoint_references.back());
        check(reversed_plan&&reversed_plan->segments[0].edge_index==1&&
            reversed_plan->segments[0].reversed&&
            reversed_plan->terminals[0].reference.semantic_key=="v2"&&
            reversed_plan->terminals[1].reference.semantic_key=="v0",
            "Authored R1 endpoint did not reverse the complete route");
        const std::array<std::size_t,3> branched{0,1,2};
        check(!build_edge_treatment_route_plan(body,branched),
            "A branched selection was accepted as one treatment route");
        const std::array<std::size_t,2> duplicate_selected{0,0};
        check(!build_edge_treatment_route_plan(body,duplicate_selected),
            "A duplicate selected edge was accepted");
    }
    {
        const std::array<std::size_t,12> selected{
            11,10,9,8,7,6,5,4,3,2,1,0};
        const auto plan=build_edge_treatment_route_plan(circle,selected);
        check(plan&&plan->closed&&plan->terminals.empty()&&
            plan->segments.size()==circle.size()&&
            plan->segments.front().start.semantic_key=="circle-point-0",
            "Closed treatment route invented caps or used input order");
        check(!edge_treatment_route_supports_linear_fillet(*plan),
            "A closed route accepted two different Fillet radii");
    }
    {
        auto inconsistent=b;
        inconsistent.points.front()={1.1,0,0};
        const std::vector body{a,inconsistent};
        const std::array<std::size_t,2> selected{0,1};
        check(!build_edge_treatment_route_plan(body,selected),
            "Inconsistent coordinates for one persisted endpoint were accepted");
        auto missing=b;
        missing.edge_treatment_endpoint_references.clear();
        const std::vector incomplete{a,missing};
        check(!build_edge_treatment_route_plan(incomplete,selected),
            "A treatment route without stable endpoints was accepted");
    }
    std::cout<<"Shared tangent traversal: exact derivatives, shallow corners, ancestry, branches, closed route, source owners, occurrences and ambiguous/empty seeds passed\n";return 0;
}catch(const std::exception& error){std::cerr<<error.what()<<'\n';return 1;}}
