#include <zima/kernel/tangent_edge_route.hpp>
#include <iostream>
using namespace zima::kernel;
void check(bool ok,const char* message){if(!ok)throw std::runtime_error(message);}
ViewerEdge edge(std::string id,Vec3 from,Vec3 to,std::string first,std::string last,std::string path={}) {
    ViewerEdge result;result.reference={"feature-"+id,id,path};result.points={from,to};
    result.edge_treatment_endpoint_references={{"points",first,path},{"points",last,path}};return result;
}
int main(){try {
    auto a=edge("a",{0,0,0},{1,0,0},"v0","v1");auto b=edge("b",{1,0,0},{2,0,0},"v1","v2");
    auto route=tangent_edge_route({a,b},a.reference);check(route==std::vector<EdgeReference>{a.reference,b.reference},"Tangent route did not cross source owners at a shared native point");
    auto separate=b;separate.edge_treatment_endpoint_references.front().semantic_key="different-point";
    check(tangent_edge_route({a,separate},a.reference).size()==1,"Spatial coincidence replaced exact endpoint identity");
    auto branch=edge("branch",{1,0,0},{2,.1,0},"v1","branch-end");
    check(tangent_edge_route({a,b,branch},a.reference).size()==1,"Ambiguous junction was traversed");
    auto back=edge("back",{1,0,0},{.2,.1,0},"v1","back-end");
    check(tangent_edge_route({a,back},a.reference).size()==1,"Route doubled back on itself");
    auto copy_a=a,copy_b=b;copy_a.reference.instance_path="copy";copy_b.reference.instance_path="copy";
    check(tangent_edge_route({a,b,copy_a,copy_b},a.reference).size()==2&&tangent_edge_route({a,b,copy_a,copy_b},copy_a.reference).size()==2,"Tangent route crossed repeated occurrences");
    std::vector<ViewerEdge> circle;
    for(int i=0;i<12;++i) {
        const auto angle=i*std::numbers::pi/6,next=(i+1)*std::numbers::pi/6;
        circle.push_back(edge("circle-"+std::to_string(i),{std::cos(angle),std::sin(angle),0},{std::cos(next),std::sin(next),0},"circle-point-"+std::to_string(i),"circle-point-"+std::to_string((i+1)%12)));
    }
    check(tangent_edge_route(circle,circle.front().reference).size()==12,"Closed route terminated incorrectly");
    auto duplicate=a;duplicate.points={{0,2,0},{1,2,0}};
    check(tangent_edge_route({a,duplicate},a.reference).empty(),"An ambiguous seed identity was guessed");
    check(tangent_edge_route({a,duplicate},a.reference,35,1e-6,0).size()==1,"Exact offered seed index was ignored");
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
                check(tangent_edge_route(values,first.reference,35,1e-6,picked)==std::vector{first.reference},
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
    std::cout<<"Shared tangent traversal: ancestry, branches, closed route, source owners, occurrences and ambiguous/empty seeds passed\n";return 0;
}catch(const std::exception& error){std::cerr<<error.what()<<'\n';return 1;}}
