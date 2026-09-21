#include <zima/document/part_document.hpp>
#include <zima/kernel/occt_kernel.hpp>
#include <cmath>
#include <iostream>
#include <stdexcept>
using namespace zima;
int main() {
    try {
        kernel::OcctKernel kernel;
        for (bool periodic : {false, true}) for (bool interpolating : {false, true}) {
            std::cout << "periodic=" << periodic << " interpolating=" << interpolating << std::endl;
            auto sketch=sketcher::Sketch::create_default();
            const std::vector<std::array<double,2>> poles=periodic
                ? std::vector<std::array<double,2>>{{-20,0},{-15,15},{0,22},{15,15},{20,0},{10,-18},{-10,-18}}
                : std::vector<std::array<double,2>>{{0,0},{10,0},{20,15},{0,30},{-20,15},{-10,0},{0,0}};
            const auto spline=sketch.add_bspline(poles,3,periodic,false,1e-6,interpolating);
            if (!periodic) static_cast<void>(sketch.add_tangent_constraint(spline,spline));
            sketch=sketcher::Sketch::from_serialized(sketch.serialized());
            auto part=document::PartDocument::create_default();
            auto feature=document::PartDocument::create_extrusion_container(sketch.id);
            feature.extrusion.height=10;feature.extrusion.length_forward=10;
            sketch.owner_container_id=feature.id;part.sketches={sketch};part.history={feature};
            document::BodyHistoryGraph graph;static_cast<void>(graph.create_body("Spline"));
            graph.insert({document::PartHistoryKind::Feature,feature.id});part.set_body_history(graph);
            part.resolve_constructions();
            const auto bodies=kernel.evaluate_history(part.kernel_operations());
            if(bodies.empty()||!std::isfinite(bodies.back().volume)||bodies.back().volume<=1)
                throw std::runtime_error("Closed spline did not produce a solid");
            // Independent sampled Green's theorem area, compared with kernel volume.
            const auto mesh=sketch.viewer_mesh();
            const auto edge=std::ranges::find_if(mesh.edges,[&](const auto& e){return e.reference.semantic_key=="bspline:"+spline;});
            if(edge==mesh.edges.end())throw std::runtime_error("Spline display curve is missing");
            double area=0;
            for(std::size_t i=0;i+1<edge->points.size();++i)area+=edge->points[i].x*edge->points[i+1].y-edge->points[i+1].x*edge->points[i].y;
            const double expected=std::abs(area)*5;
            if(std::abs(bodies.back().volume-expected)>expected*.01)
                throw std::runtime_error("Spline extrusion volume differs from its displayed profile");
        }
        std::cout << "Closed spline extrusion passed\n";return 0;
    } catch(const std::exception& e) {std::cerr<<e.what()<<'\n';return 1;}
}
