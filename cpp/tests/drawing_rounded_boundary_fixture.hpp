#pragma once
#include <zima/drawing/measurement_dimension.hpp>
#include <numbers>

namespace zima::test {
// Two independent semicylindrical end faces, with unequal radii. Deliberately
// omit extrema from the coarse display mesh: measurements must use the surface.
inline drawing::DrawingView rounded_boundary_view(double left=3,double right=5,bool horizontal=false) {
    kernel::ViewerMesh mesh;
    for(int end=0;end<2;++end) {
        const double center=end?30:-20,radius=end?right:left;
        auto surface=std::make_shared<kernel::SurfaceGeometry>();
        surface->kind=kernel::SurfaceGeometry::Kind::Cylinder;surface->radius=radius;
        surface->origin={center,0,0};surface->axis={0,0,1};surface->radial={1,0,0};
        surface->axial_min=-10;surface->axial_max=10;
        surface->u_min=end?-std::numbers::pi/2:std::numbers::pi/2;
        surface->u_max=surface->u_min+std::numbers::pi;
        kernel::FaceReference face{end?"right-round":"left-round","rounded-face",end?"assembly/right":"assembly/left"};face.surface=surface;
        const auto start=static_cast<unsigned>(mesh.vertices.size());
        for(unsigned i=0;i<=25;++i) {
            const double angle=surface->u_min+std::numbers::pi*i/25;
            for(double z:{-10.,10.})mesh.vertices.push_back({center+radius*std::cos(angle),radius*std::sin(angle),z});
            if(i) {
                for(const auto index:{2*i-2,2*i,2*i-1,2*i,2*i+1,2*i-1})mesh.triangles.push_back(start+index);
                mesh.triangle_references.push_back(face);mesh.triangle_references.push_back(face);
            }
        }
        // The radial closing face makes the end generators real topology edges,
        // so they cannot be confused with the smooth silhouette in the middle.
        kernel::FaceReference closing{face.owner_id,"closing-plane",face.instance_path};
        for(const auto index:{0u,1u,50u,50u,1u,51u})mesh.triangles.push_back(start+index);
        mesh.triangle_references.push_back(closing);mesh.triangle_references.push_back(closing);
    }
    mesh.original_references.vertices=mesh.vertices;mesh.original_references.triangles=mesh.triangles;
    mesh.original_references.triangle_references=mesh.triangle_references;
    drawing::DrawingView view;view.id="independent-rounds";view.source_document_id="boundary-fixture";
    view.source_path="unavailable-boundary-fixture.prtz";view.scale=2;
    view.display_style=drawing::DisplayStyle::HiddenEdges;
    view.camera=horizontal?drawing::ProjectionCamera{{0,0,1},{1,0,0},{0,-1,0}}:
        drawing::ProjectionCamera{{1,0,0},{0,0,1},{0,1,0}};
    drawing::prepare_interactive_view(view,std::make_shared<const kernel::ViewerMesh>(mesh));
    drawing::ModelAnnotation axis;axis.kind=drawing::ModelAnnotationKind::Axis;axis.visible=true;
    axis.source={view.source_document_id,"datum","axis:datum","assembly/datum"};
    axis.model_axis=std::array<kernel::Vec3,2>{{{0,0,-10},{0,0,10}}};
    axis.model_envelope.include({-1,-1,-10});axis.model_envelope.include({1,1,10});
    view.model_annotations.push_back(std::move(axis));
    return view;
}
}
