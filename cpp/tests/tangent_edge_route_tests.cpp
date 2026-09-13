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
    auto empty=a;empty.points.clear();check(tangent_edge_route({empty},empty.reference).empty(),"Empty seed geometry was traversed");
    check(tangent_edge_route({a,b},{"missing","missing",{}}).empty(),"Missing seed was guessed");
    check(tangent_edge_route({a,b},a.reference,0).size()==2,"Exactly collinear route was rejected");
    std::cout<<"Shared tangent traversal: ancestry, branches, closed route, source owners, occurrences and ambiguous/empty seeds passed\n";return 0;
}catch(const std::exception& error){std::cerr<<error.what()<<'\n';return 1;}}
