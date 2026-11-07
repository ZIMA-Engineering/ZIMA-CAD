#include <zima/viewer/mesh_view.hpp>
#include <zima/sketcher/sketch.hpp>
#include "../app/sketch_point_pick_priority.hpp"
#include <QApplication>
#include <QMouseEvent>
#include <QEventLoop>
#include <QTimer>
#include <QImage>
#include <QDir>
#include <algorithm>
#include <cmath>
#include <iostream>
#include <stdexcept>

int main(int argc,char** argv) {
    QApplication application(argc,argv);
    try {
        using namespace zima;
        const auto check=[](bool ok,const char* text){if(!ok)throw std::runtime_error(text);};
        const auto flush=[&]{application.processEvents();};
        auto sketch=sketcher::Sketch::create_default();
        const auto segment=sketch.add_segment(-30,12,70,12);
        auto reference=sketcher::Sketch::create_external_reference(sketcher::ExternalReferenceKind::Edge);
        reference.source_document_id="source";reference.source_owner_id="read-only";reference.source_semantic_key="segment";reference.cached_points={{-30,-12},{70,-12}};
        const auto reference_id=reference.id;sketch.add_external_reference(reference);
        static_cast<void>(sketch.add_external_edge_endpoints(reference_id));
        const auto midpoint=std::ranges::find(sketch.external_references,sketcher::ExternalReferenceKind::EdgeMidpoint,&sketcher::SketchExternalReference::kind);
        check(midpoint!=sketch.external_references.end(),"Reference midpoint is missing");
        const std::string native_key="sketch_midpoint:"+segment,external_key="external_point:"+midpoint->id;
        const auto mesh=sketch.viewer_mesh();
        viewer::MeshView view;view.setAttribute(Qt::WA_DontShowOnScreen);view.resize(900,700);view.show();flush();
        view.set_active_sketch_owner(sketch.id);
        view.set_selection_contract({viewer::CandidateKind::SketchPoint,viewer::CandidateKind::SketchExternalReference,viewer::CandidateKind::SketchSegment});
        view.set_candidate_priority(app::sketch_placement_pick_priority);
        view.set_mesh(mesh,false);
        unsigned cases{};
        for(bool back:{false,true})for(int sector=0;sector<4;++sector)
        for(bool transient:{false,true})for(float zoom:{.75F,1.5F}) {
            auto palette=view.palette();const bool dark=zoom>1.F;
            palette.setColor(QPalette::Window,dark?QColor(32,32,32):QColor(245,245,245));
            palette.setColor(QPalette::Text,dark?QColor(Qt::white):QColor(Qt::black));view.setPalette(palette);
            view.set_view_direction({0,0,back?1.:-1.},float(90*sector));
            view.fit_points({{-60,-60,0},{100,60,0}});flush();
            auto camera=view.camera_state();camera[4]*=zoom;view.set_camera_state(camera);flush();
            QEventLoop animation;QTimer::singleShot(950,&animation,&QEventLoop::quit);animation.exec();
            const auto local=[&](QPointF pixel) {
                const auto ray=view.ray_at(pixel);check(ray.has_value(),"Midpoint view ray missing");
                const auto p=sketch.intersect_ray(ray->first,ray->second);check(p.has_value(),"Midpoint view plane missing");return *p;
            };
            const auto a=local({0,0}),b=local({100,0}),c=local({0,100});
            const double ux=(b[0]-a[0])/100,uy=(b[1]-a[1])/100,vx=(c[0]-a[0])/100,vy=(c[1]-a[1])/100,det=ux*vy-uy*vx;
            const auto screen=[&](double x,double y){return QPointF(((x-a[0])*vy-(y-a[1])*vx)/det,(ux*(y-a[1])-uy*(x-a[0]))/det);};
            const auto move=[&](QPointF p){QMouseEvent event(QEvent::MouseMove,p,QPointF(view.mapToGlobal(p.toPoint())),Qt::NoButton,Qt::NoButton,Qt::NoModifier);QApplication::sendEvent(&view,&event);flush();};
            for(bool external:{false,true}) {
                const auto key=external?external_key:native_key;
                const double y=external?-12:12;
                auto publish=[&](const kernel::ViewerMesh& value) {
                    if(transient) {
                        // As in a live Sketch drag, immutable reference crosses
                        // remain in the pickable base while native draft points
                        // are replaced by the transient presentation.
                        kernel::ViewerMesh supports;
                        for(const auto& point:value.points)if(point.reference.semantic_key.starts_with("external_point:"))supports.points.push_back(point);
                        view.set_mesh(std::move(supports),false);view.set_transient_context_mesh(value);
                    }
                    else {view.set_transient_context_mesh({});view.set_mesh(value,false);}
                    flush();
                };
                publish(mesh);move(screen(-30,y));
                const auto image=view.grabFramebuffer();
                check(!image.isNull()&&view.isValid(),"Hidden native OpenGL test context is unavailable");
                const auto center=screen(20,y);const double dpi=view.devicePixelRatioF();
                const QRect crop(int((center.x()-9)*dpi),int((center.y()-9)*dpi),int(18*dpi),int(18*dpi));
                auto hidden=mesh;std::erase_if(hidden.points,[&](const auto& p){return p.reference.semantic_key==key;});
                publish(hidden);move(screen(-30,y));
                const auto control=view.grabFramebuffer();
                if(image.copy(crop)!=control.copy(crop)) {
                    image.copy(crop).save(QDir::tempPath()+"/midpoint-offered.png");
                    control.copy(crop).save(QDir::tempPath()+"/midpoint-absent.png");
                    std::cerr<<"Midpoint pixel case back="<<back<<" sector="<<sector<<" transient="<<transient<<" zoom="<<zoom<<" external="<<external<<'\n';
                }
                check(image.copy(crop)==control.copy(crop),"Endpoint hover or profile preview exposed a distant midpoint marker");
                publish(mesh);move(center);
                const auto near=view.grabFramebuffer();
                if(!transient) {
                    const auto candidate=view.hovered_candidate();
                    check(candidate&&candidate->semantic_key==key,"Cursor at midpoint does not offer that exact point");
                }
                publish(hidden);move(center);
                if(near.copy(crop)==view.grabFramebuffer().copy(crop))
                    throw std::runtime_error("Cursor near midpoint has no visible feedback: back="+
                        std::to_string(back)+" sector="+std::to_string(sector)+
                        " transient="+std::to_string(transient)+" zoom="+
                        std::to_string(zoom)+" external="+std::to_string(external));
                if(!external&&!transient) {
                    // Ordinary Sketch selection still offers point/reference
                    // kinds while its active candidate is the complete
                    // Segment. Crossing the middle must not paint M until the
                    // exact midpoint candidate itself wins the picker.
                    view.set_candidate_priority([](const auto& candidate) {
                        return candidate.kind ==
                                viewer::CandidateKind::SketchSegment
                            ? -1 : 1;
                    });
                    publish(mesh);move(center);
                    const auto segment_only=view.grabFramebuffer();
                    check(view.hovered_candidate() &&
                            view.hovered_candidate()->kind ==
                                viewer::CandidateKind::SketchSegment,
                        "Segment-priority midpoint fixture did not select the segment");
                    publish(hidden);move(center);
                    check(segment_only.copy(crop)==view.grabFramebuffer().copy(crop),
                        "Selecting a segment also exposed its midpoint marker");
                    view.set_candidate_priority(app::sketch_placement_pick_priority);
                }
                ++cases;
            }
        }
        std::cout<<"Midpoint presentation: "<<cases<<" native/external, front/back, sectors, Light/Dark and base/transient cases; distant-marker absence, near feedback and exact picking passed\n";
        return 0;
    }catch(const std::exception& e){std::cerr<<e.what()<<'\n';return 1;}
}
