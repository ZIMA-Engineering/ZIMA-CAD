#include "profile_request_fixture.hpp"
#include <zima/assembly/assembly_document.hpp>
#include <zima/kernel/occt_kernel.hpp>
#include <zima/viewer/mesh_view.hpp>
#include <zima/viewer/shading.hpp>
#include <zima/document/part_document.hpp>
#include <QApplication>
#include <QCryptographicHash>
#include <QMouseEvent>
#include <QQuaternion>
#include <QVariantAnimation>
#include <algorithm>
#include <array>
#include <chrono>
#include <iomanip>
#include <iostream>
#include <stdexcept>

namespace {
void require(bool ok,const char* message) { if(!ok)throw std::runtime_error(message); }
QByteArray frame_hash(zima::viewer::MeshView& view) {
    const auto pixels=view.grabFramebuffer().convertToFormat(QImage::Format_RGBA8888);
    return QCryptographicHash::hash(QByteArrayView(reinterpret_cast<const char*>(pixels.constBits()),
        pixels.sizeInBytes()),QCryptographicHash::Sha256).toHex();
}
void exercise(QApplication& app,const std::string& name,const zima::kernel::ViewerMesh& mesh) {
    using namespace zima;
    QByteArray first_shading;
    for(int trial=0;trial<4;++trial) {
        const auto begin=std::chrono::steady_clock::now();
        const auto vertices=viewer::shaded_triangle_vertices(mesh);
        const auto elapsed=std::chrono::duration<double,std::milli>(std::chrono::steady_clock::now()-begin).count();
        const auto hash=QCryptographicHash::hash(QByteArrayView(reinterpret_cast<const char*>(vertices.data()),
            vertices.size()*sizeof(float)),QCryptographicHash::Sha256).toHex();
        if(trial==0)first_shading=hash;
        require(hash==first_shading,"Repeated shading changed the exact GPU vertex buffer");
        std::cout<<"shading case="<<name<<" trial="<<trial<<" ms="<<elapsed<<" sha256="<<hash.constData()<<std::endl;
    }
    viewer::MeshView view;view.resize(1200,800);view.show();app.processEvents();
    require(view.isValid(),"Benchmark requires desktop OpenGL");
    view.set_mesh(mesh);view.set_standard_view(viewer::StandardView::Isometric);
    for(auto* animation:view.findChildren<QVariantAnimation*>())animation->setCurrentTime(animation->duration());
    view.repaint();app.processEvents();
    const auto original_camera=view.camera_state();
    const auto revision=view.base_mesh_revision();
    std::cout<<"case="<<name<<" triangles="<<mesh.triangles.size()/3<<" edges="<<mesh.edges.size()<<std::endl;
    const auto snapshot=[&](const std::string& state) {
        std::cout<<"frame case="<<name<<" state="<<state<<" sha256="<<frame_hash(view).constData()<<std::endl;
    };
    // Actual common-picker hover and confirmation, rather than a synthetic highlight flag.
    view.set_selection_contract({viewer::CandidateKind::Container});
    QPointF hit;std::optional<viewer::ViewerCandidate> candidate;
    for(int y=20;y<view.height()-20&&!candidate;y+=12)for(int x=20;x<view.width()-20;x+=12) {
        const auto offered=view.selection_candidates_at({double(x),double(y)});
        if(!offered.empty()){candidate=offered.front();hit={double(x),double(y)};break;}
    }
    require(candidate.has_value(),"Curved fixture has no selectable container");
    for(int state=0;state<3;++state) {
        if(state==1) {
            QMouseEvent move(QEvent::MouseMove,hit,view.mapToGlobal(hit),Qt::NoButton,Qt::NoButton,Qt::NoModifier);
            QApplication::sendEvent(&view,&move);
            require(view.hovered_candidate()==candidate,"Hover changed candidate identity");
        } else if(state==2) {
            QMouseEvent press(QEvent::MouseButtonPress,hit,view.mapToGlobal(hit),Qt::LeftButton,Qt::LeftButton,Qt::NoModifier);
            QApplication::sendEvent(&view,&press);
            QMouseEvent release(QEvent::MouseButtonRelease,hit,view.mapToGlobal(hit),Qt::LeftButton,Qt::NoButton,Qt::NoModifier);
            QApplication::sendEvent(&view,&release);
            require(view.confirmed_candidate()==candidate,"Confirmation changed candidate identity");
        }
        for(const auto mode:{viewer::DisplayMode::Wire,viewer::DisplayMode::HiddenEdges,
                viewer::DisplayMode::NoHiddenEdges,viewer::DisplayMode::ShadedWithEdges,viewer::DisplayMode::Shaded}) {
            view.set_display_mode(mode);
            for(int alpha:{255,120}) {
                view.set_body_surface_colors(QColor(185,194,204,alpha));
                const auto hash=frame_hash(view);
                std::vector<double> samples;
                for(int i=0;i<7;++i) {
                    const auto begin=std::chrono::steady_clock::now();view.grabFramebuffer();
                    samples.push_back(std::chrono::duration<double,std::milli>(std::chrono::steady_clock::now()-begin).count());
                }
                std::sort(samples.begin(),samples.end());
                require(frame_hash(view)==hash,"Unchanged frame is unstable");
                require(view.base_mesh_revision()==revision,"Highlight changed base geometry");
                std::cout<<"warm case="<<name<<" state="<<state<<" mode="<<int(mode)<<" alpha="<<alpha
                    <<" median_ms="<<samples[3]<<" max_ms="<<samples.back()<<" sha256="<<hash.constData()<<std::endl;
            }
        }
    }
    view.clear_selection();view.set_body_surface_colors(QColor(185,194,204));
    view.set_display_mode(viewer::DisplayMode::ShadedWithEdges);
    // A cached empty silhouette set must not prevent later nonempty views.
    for(const auto standard:{viewer::StandardView::Top,viewer::StandardView::Front,viewer::StandardView::Isometric}) {
        view.set_standard_view(standard);
        for(auto* animation:view.findChildren<QVariantAnimation*>())animation->setCurrentTime(animation->duration());
        snapshot("orientation-"+std::to_string(int(standard)));
    }
    view.set_camera_state(original_camera);
    auto camera=original_camera;camera[5]+=31;camera[6]-=23;view.set_camera_state(camera);snapshot("pan");
    camera[4]*=1.3F;view.set_camera_state(camera);snapshot("zoom");
    auto q=QQuaternion::fromAxisAndAngle(0,1,0,.00001F)*QQuaternion(camera[0],camera[1],camera[2],camera[3]);
    camera[0]=q.scalar();camera[1]=q.x();camera[2]=q.y();camera[3]=q.z();
    view.set_camera_state(camera);snapshot("tiny-rotation");
    view.set_projection_mode(viewer::ProjectionMode::Perspective);snapshot("perspective");
    view.set_projection_mode(viewer::ProjectionMode::Orthographic);view.set_camera_state(original_camera);
    // Same topology/array sizes, different vertex positions and normals.
    auto changed=mesh;
    for(auto& p:changed.vertices){p.x+=.13*p.z;p.z*=.83;}
    for(auto& edge:changed.edges)for(auto& p:edge.points){p.x+=.13*p.z;p.z*=.83;}
    view.set_mesh(changed,false);snapshot("changed-mesh");
    view.set_mesh({},false);snapshot("empty-mesh");
    view.set_mesh(mesh,false);snapshot("restored-mesh");
}
}
int main(int argc,char** argv) {
    QApplication app(argc,argv);
    try {
        std::cout<<std::fixed<<std::setprecision(3);
        zima::kernel::OcctKernel kernel;
        for(bool sphere:{false,true}) {
            const auto body=kernel.evaluate_history({{"curved",sphere?
                zima::kernel::PrimitiveRequest(static_cast<zima::kernel::RevolutionRequest>(zima::test::SphericalRevolution{5})):
                zima::kernel::PrimitiveRequest(static_cast<zima::kernel::ExtrusionRequest>(zima::test::CircularExtrusion{5,12}))}}).back();
            for(int count:{64,256}) {
                auto assembly=zima::assembly::AssemblyDocument::create_default();assembly.document_id="silhouette-assembly";
                for(int i=0;i<count;++i) {
                    auto item=zima::assembly::AssemblyDocument::create_part_occurrence("Curved","source",{},body);
                    item.occurrence_id="occ-"+std::to_string(i);item.placement.x=(i%16)*14.;item.placement.y=(i/16)*14.;
                    assembly.components.push_back(std::move(item));
                }
                exercise(app,std::string(sphere?"sphere-":"cylinder-")+std::to_string(count),assembly.build_scene());
            }
        }
        {
            // Non-manifold adjacency must remain excluded, even with more
            // than three incidents. Invalid triangles must not create records.
            auto mesh=kernel.evaluate_history({{"nonmanifold",
                static_cast<zima::kernel::ExtrusionRequest>(zima::test::CircularExtrusion{5,12})}}).back().mesh;
            const std::array<std::uint32_t,3> first{mesh.triangles[0],mesh.triangles[1],mesh.triangles[2]};
            const auto face=mesh.triangle_references.front();
            for(int duplicate=0;duplicate<3;++duplicate) {
                mesh.triangles.insert(mesh.triangles.end(),first.begin(),first.end());
                mesh.triangle_references.push_back(face);
            }
            mesh.triangles.insert(mesh.triangles.end(),{0,0,0,static_cast<std::uint32_t>(mesh.vertices.size()),0,1});
            mesh.triangle_references.insert(mesh.triangle_references.end(),{face,face});
            exercise(app,"nonmanifold",mesh);
        }
        for(int i=1;i<argc;++i) {
            const auto path=std::filesystem::u8path(argv[i]);
            require(path.extension()==".prtz","Native benchmark inputs must be Parts");
            auto part=zima::document::PartDocument::load(path);
            const auto calculated=kernel.evaluate_history(part.kernel_operations());
            require(!calculated.empty(),"Native fixture has no calculated body");
            // Explicit one-time preparation, excluded from all repaint timings.
            // No source document is changed or saved.
            exercise(app,path.stem().string(),calculated.back().mesh);
        }
        return 0;
    } catch(const std::exception& e){std::cerr<<e.what()<<std::endl;return 1;}
}
