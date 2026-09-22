#include <zima/workspace/drawing_operations.hpp>
#include <zima/workspace/drawing_projection.hpp>
#include <chrono>
#include <iostream>
int main(int argc,char** argv) {
    if(argc!=2){std::cerr<<"Usage: drawing_projection_benchmark source.asmz\n";return 2;}
    try {
        const auto start=std::chrono::steady_clock::now();
        const auto seconds=[&]{return std::chrono::duration<double>(std::chrono::steady_clock::now()-start).count();};
        auto [id,mesh]=zima::workspace::read_drawing_source(nullptr,std::filesystem::u8path(argv[1]),{});
        std::size_t samples=0;for(const auto& edge:mesh.edges)samples+=edge.points.size();
        std::cout<<"Source: "<<seconds()<<" s; triangles="<<mesh.triangles.size()/3<<" edges="<<mesh.edges.size()<<" samples="<<samples<<std::endl;
        for(const auto orientation:{zima::drawing::ViewOrientation::Isometric,zima::drawing::ViewOrientation::Front}) {
            const auto before=seconds();
            const auto edges=zima::drawing::project_edges(mesh,orientation);
            std::cout<<"Projection: "<<seconds()-before<<" s; output edges="<<edges.size()<<std::endl;
        }
        auto view=zima::drawing::DrawingDocument::create_view(id,std::filesystem::u8path(argv[1]),mesh,zima::drawing::ViewOrientation::Isometric);
        zima::workspace::DrawingProjection projection(nullptr,{});
        const auto before=seconds();projection.project(view,{});
        std::cout<<"Complete view projection with source annotations: "<<seconds()-before<<" s"<<std::endl;
        return 0;
    } catch(const std::exception& e){std::cerr<<e.what()<<'\n';return 1;}
}
