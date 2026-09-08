#pragma once
#include <zima/kernel/geometry_kernel.hpp>
#include <cmath>
inline zima::kernel::ViewerMesh drawing_cylinder_fixture() {
    zima::kernel::ViewerMesh mesh;
    constexpr unsigned count=64;
    for(unsigned level=0;level<2;++level)for(unsigned i=0;i<count;++i) {
        const double angle=i*2*3.141592653589793/count;
        mesh.vertices.push_back({10*std::cos(angle),10*std::sin(angle),20.0*level});
    }
    mesh.vertices.push_back({0,0,0});mesh.vertices.push_back({0,0,20});
    for(unsigned i=0;i<count;++i) {
        const unsigned j=(i+1)%count;
        for(const auto index:{i,j,j+count,i,j+count,i+count,2*count,j,i,2*count+1,i+count,j+count})mesh.triangles.push_back(index);
        for(int t=0;t<4;++t)mesh.triangle_references.push_back({"cylinder",t<2?"wall":t==2?"bottom":"top",{}});
    }
    for(unsigned level=0;level<2;++level) {
        zima::kernel::ViewerEdge edge;edge.reference={"cylinder",level?"rim-top":"rim-bottom",{}};
        for(unsigned i=0;i<=count;++i)edge.points.push_back(mesh.vertices[level*count+i%count]);
        mesh.edges.push_back(edge);
    }
    zima::kernel::ViewerEdge seam;seam.reference={"cylinder","seam",{}};seam.parameter_seam=true;
    seam.points={mesh.vertices[0],mesh.vertices[count]};mesh.edges.push_back(seam);
    return mesh;
}
