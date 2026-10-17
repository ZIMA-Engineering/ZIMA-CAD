#pragma once

#include <zima/kernel/geometry_kernel.hpp>
#include <QVector3D>
#include <array>
#include <optional>
#include <span>
#include <vector>

namespace zima::viewer {
// Presentation only: use the same corner normals as the shaded surface.
// Selecting complete triangulation edges instead produces small transverse
// strokes where the outline crosses an irregular fillet triangulation.
struct SmoothSilhouetteTriangle {
    std::array<QVector3D,3> points;
    std::array<QVector3D,3> normals;
    bool operator==(const SmoothSilhouetteTriangle&)const=default;
};

inline std::vector<SmoothSilhouetteTriangle> smooth_silhouette_triangles(
    const kernel::ViewerMesh& mesh,std::span<const float> shaded) {
    std::vector<SmoothSilhouetteTriangle> result;
    result.reserve(mesh.triangles.size()/3);
    std::size_t offset=0;
    for(std::size_t triangle=0;triangle<mesh.triangles.size()/3;++triangle) {
        bool valid=true;
        for(std::size_t corner=0;corner<3;++corner)
            valid&=mesh.triangles[triangle*3+corner]<mesh.vertices.size();
        if(!valid)continue; // The shading buffer omits the same invalid triangle.
        if(offset+18>shaded.size())break;
        if(triangle<mesh.triangle_references.size()&&mesh.triangle_references[triangle].valid()) {
            SmoothSilhouetteTriangle value;
            for(std::size_t corner=0;corner<3;++corner) {
                const auto i=offset+corner*6;
                value.points[corner]={shaded[i],shaded[i+1],shaded[i+2]};
                value.normals[corner]={shaded[i+3],shaded[i+4],shaded[i+5]};
            }
            result.push_back(value);
        }
        offset+=18;
    }
    return result;
}

inline std::optional<std::array<QVector3D,2>> smooth_silhouette_segment(
    const SmoothSilhouetteTriangle& triangle,const QVector3D& direction) {
    std::array<double,3> side;
    for(std::size_t i=0;i<3;++i) {
        const auto& normal=triangle.normals[i];
        side[i]=double(normal.x())*direction.x()+double(normal.y())*direction.y()+
            double(normal.z())*direction.z();
    }
    // A whole tangent planar patch has no internal outline. Its real boundary
    // remains an ordinary persisted edge, including at an exact axial view.
    if(side[0]==0&&side[1]==0&&side[2]==0)return std::nullopt;
    std::array<QVector3D,3> contacts;
    std::size_t count=0;
    const auto add=[&](QVector3D point) {
        for(std::size_t i=0;i<count;++i)if(contacts[i]==point)return;
        if(count<contacts.size())contacts[count++]=point;
    };
    for(std::size_t i=0;i<3;++i) {
        const auto j=(i+1)%3;
        if(side[i]==0)add(triangle.points[i]);
        if(side[j]==0)add(triangle.points[j]);
        if((side[i]<0&&side[j]>0)||(side[i]>0&&side[j]<0)) {
            const double fraction=side[i]/(side[i]-side[j]);
            const auto& a=triangle.points[i];const auto& b=triangle.points[j];
            add({float(double(a.x())+(double(b.x())-a.x())*fraction),
                float(double(a.y())+(double(b.y())-a.y())*fraction),
                float(double(a.z())+(double(b.z())-a.z())*fraction)});
        }
    }
    if(count!=2||contacts[0]==contacts[1])return std::nullopt;
    return std::array{contacts[0],contacts[1]};
}
} // namespace zima::viewer
