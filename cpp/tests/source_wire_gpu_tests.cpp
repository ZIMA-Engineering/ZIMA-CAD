#include <zima/viewer/mesh_view.hpp>
#include <zima/document/part_document.hpp>
#include <zima/document/viewer_packet_json.hpp>
#include <QApplication>
#include <QElapsedTimer>
#include <QMouseEvent>
#include <QCursor>
#include <QQuaternion>
#include <numbers>
#include <cmath>
#include <iostream>
#include <algorithm>

using namespace zima;
void check(bool yes,const char* message){if(!yes)throw std::runtime_error(message);}
void verify_import_batches(QApplication& application) {
    kernel::ViewerMesh mesh;
    for(int i=0;i<29000;++i)
        mesh.edges.push_back({{{-100.,i/150.-100,0},{100.,i/150.-100,0}},
            {"import","step:edge:#"+std::to_string(i),{}}});
    for(auto& edge:mesh.edges)edge.display_owner_id="import";
    viewer::MeshView view;view.resize(640,480);view.set_mesh(mesh);view.show();view.fit_all();
    view.set_display_mode(viewer::DisplayMode::Wire);view.confirm_container("import");
    const auto render=[&]{application.processEvents();check(!view.grabFramebuffer().isNull(),"Imported wire has no GPU framebuffer");};
    render();
    check(view.confirmed_candidate().has_value(),"STEP fixture has no ordinary container candidate");
    const auto uploads=view.property("gpuMeshUploadCount").toULongLong();
    const auto builds=view.property("containerColorBuildCount").toULongLong();
    check(builds==1&&view.property("gpuSourceWireUploadCount").toULongLong()==0,
        "Source-less STEP selection duplicated the display buffer");
    const auto camera=view.camera_state();
    for(int i=0;i<8;++i) {
        auto changed=camera;changed[5]+=i%3;changed[6]-=i%2;view.set_camera_state(changed);render();
    }
    check(view.property("gpuMeshUploadCount").toULongLong()==uploads&&
        view.property("containerColorBuildCount").toULongLong()==builds&&
        view.property("gpuSourceWireVertexCount").toInt()==0,
        "Imported selection rebuilt or duplicated geometry during navigation");
    std::cout<<"Source-less 29000-edge STEP selection: existing display batches, no source upload, stable navigation cache passed\n";
}
void verify_occluded_selection(QApplication& application) {
    kernel::ViewerMesh mesh;
    mesh.vertices={{-.8,0,0},{.8,0,0},{0,1,0},{-1.5,-1.5,1},{1.5,-1.5,1},{1.5,1.5,1},{-1.5,1.5,1}};
    mesh.triangles={0,1,2,3,4,5,3,5,6};
    mesh.triangle_references={{"inside","face",{}},{"occluder","face",{}},{"occluder","face",{}}};
    mesh.edges={{{{-.8,0,0},{.8,0,0}},{"inside","rim",{}}},
                {{{-.8,-.6,0},{.8,-.6,0}},{"other","rim",{}}}};
    mesh.original_references.edges={{{{-.7,-.7,0},{.7,-.7,0},{.7,.7,0},{-.7,.7,0},{-.7,-.7,0}},{"inside","start:profile",{}}}};
    viewer::MeshView view;view.resize(640,640);view.set_mesh(mesh);view.show();view.fit_all();
    unsigned preview_reads=0;
    view.set_container_preview_provider([&](const auto& owner,const auto& path) {
        check(owner=="inside"&&path.empty(),"Authored preview provider received a different occurrence");
        ++preview_reads;
        return std::vector<kernel::ViewerEdge>{{{{-1,-1,0},{1,-1,0},{1,1,0},{-1,1,0},{-1,-1,0}},{"inside","start:profile",{}}}};
    });
    view.set_selection_contract({viewer::CandidateKind::Container});application.processEvents();
    const auto azure=[&](const QImage& frame,QPointF at) {
        const double ratio=frame.width()/double(view.width());
        for(int dy=-2;dy<=2;++dy)for(int dx=-2;dx<=2;++dx) {
            const int x=qRound(at.x()*ratio)+dx,y=qRound(at.y()*ratio)+dy;
            if(!frame.rect().contains(x,y))continue;
            const auto c=frame.pixelColor(x,y);if(c.red()<25&&c.green()>180&&c.blue()>235)return true;
        }
        return false;
    };
    for(const auto mode:{viewer::DisplayMode::Wire,viewer::DisplayMode::HiddenEdges,viewer::DisplayMode::NoHiddenEdges,
                        viewer::DisplayMode::ShadedWithEdges,viewer::DisplayMode::Shaded})
    for(const auto kind:{viewer::CandidateKind::Container,viewer::CandidateKind::Face}) {
        view.set_inspected_faces({});view.set_selection_contract({kind});
        view.set_display_mode(mode);auto camera=view.camera_state();camera[0]=1;camera[1]=camera[2]=camera[3]=0;view.set_camera_state(camera);view.clear_selection();
        QCursor::setPos(view.mapToGlobal(QPoint(-20,-20)));application.processEvents();
        const auto baseline=view.grabFramebuffer();const QPointF center(view.width()/2.,view.height()/2.);
        const double pixels=1/view.world_tolerance_for_pixels(1);
        const QPointF at=center+QPointF(0,-.25*pixels),interior=center+QPointF(.2*pixels,-.25*pixels);
        QMouseEvent move(QEvent::MouseMove,at,QPointF(view.mapToGlobal(at.toPoint())),Qt::NoButton,Qt::NoButton,Qt::NoModifier);
        QApplication::sendEvent(&view,&move);application.processEvents();
        for(int cycle=0;cycle<8&&(!view.offered_candidate()||view.offered_candidate()->owner_id!="inside");++cycle) {
            for(auto type:{QEvent::MouseButtonPress,QEvent::MouseButtonRelease}) {
                QMouseEvent event(type,at,QPointF(view.mapToGlobal(at.toPoint())),Qt::RightButton,
                    type==QEvent::MouseButtonPress?Qt::RightButton:Qt::NoButton,Qt::NoModifier);
                QApplication::sendEvent(&view,&event);
            }
            application.processEvents();
        }
        check(view.offered_candidate()&&view.offered_candidate()->owner_id=="inside","Cannot cycle the ordinary mouse offer to the occluded feature");
        const auto preview_at=kind==viewer::CandidateKind::Container?center+QPointF(0,pixels):center;
        const auto hover=view.grabFramebuffer();
        const auto hover_reads=preview_reads;
        check(azure(hover,preview_at)&&!azure(hover,center+QPointF(0,.6*pixels)),"Occluded hover hid its own wire or exposed another feature's edge");
        if(kind==viewer::CandidateKind::Container)
            check(!azure(hover,center)&&!azure(hover,center+QPointF(0,.7*pixels)),
                "Authored preview also highlighted a current result or later-state reference edge");
        for(auto type:{QEvent::MouseButtonPress,QEvent::MouseButtonRelease}) {
            QMouseEvent event(type,at,QPointF(view.mapToGlobal(at.toPoint())),Qt::LeftButton,
                type==QEvent::MouseButtonPress?Qt::LeftButton:Qt::NoButton,Qt::NoModifier);
            QApplication::sendEvent(&view,&event);
        }
        application.processEvents();const auto selected=view.grabFramebuffer();
        check(preview_reads==hover_reads,"Click reread the already prepared hover preview");
        check(view.confirmed_candidate()&&view.confirmed_candidate()->owner_id=="inside"&&azure(selected,preview_at),"Click did not confirm the offered hidden wire");
        if(kind==viewer::CandidateKind::Container)
            check(!azure(selected,center),"Confirmed authored preview also highlighted a current result edge");
        const double ratio=selected.width()/double(view.width());const QPoint p(qRound(interior.x()*ratio),qRound(interior.y()*ratio));
        check(selected.pixelColor(p)==baseline.pixelColor(p),"Whole-solid hover/selection changed an interior surface pixel");
        check(view.property("gpuSourceWireVertexCount").toInt()==(kind==viewer::CandidateKind::Container?8:0),
            "Container preview and exact face inspection used the same geometry");
        if(kind==viewer::CandidateKind::Face) {
            const auto face=*view.confirmed_candidate();view.clear_selection();view.set_inspected_faces({face});
            application.processEvents();const auto inspected=view.grabFramebuffer();
            check(azure(inspected,center)&&inspected.pixelColor(p)==baseline.pixelColor(p),
                "Reference face inspection filled its interior or hid its occluded boundary");
        }
    }
    std::cout<<"Occluded Container/Face mouse hover/RMB cycling/LMB selection: all five display modes, authored Container preview without result edges, exact Face boundary, unchanged interiors passed\n";
}
void verify_curved_hover(QApplication& application) {
    // Two real curved face owners, with no persisted display edges. Any cyan
    // stroke here must come from the smooth silhouette pass itself.
    kernel::ViewerMesh mesh;constexpr int rings=24,sectors=48;
    for(int row=0;row<=rings;++row)for(int col=0;col<sectors;++col) {
        const double latitude=-std::numbers::pi/2+row*std::numbers::pi/rings;
        const double longitude=col*2*std::numbers::pi/sectors;
        mesh.vertices.push_back({std::cos(latitude)*std::cos(longitude),std::cos(latitude)*std::sin(longitude),std::sin(latitude)});
    }
    const auto triangle=[&](unsigned a,unsigned b,unsigned c) {
        const auto x=mesh.vertices[a].x+mesh.vertices[b].x+mesh.vertices[c].x;
        mesh.triangles.insert(mesh.triangles.end(),{a,b,c});
        mesh.triangle_references.push_back({x>0?"curved-selected":"curved-other","curved:surface",{}});
    };
    for(int row=0;row<rings;++row)for(int col=0;col<sectors;++col) {
        const unsigned a=row*sectors+col,b=row*sectors+(col+1)%sectors,c=(row+1)*sectors+col,d=(row+1)*sectors+(col+1)%sectors;
        if(row)triangle(a,b,c);if(row+1<rings)triangle(b,d,c);
    }
    viewer::MeshView view;view.resize(640,640);view.set_mesh(mesh);view.set_selection_contract({viewer::CandidateKind::Container});
    view.set_display_mode(viewer::DisplayMode::Shaded);view.show();view.fit_all();application.processEvents();
    for(bool flip:{false,true})for(int turn=0;turn<4;++turn) {
        auto camera=view.camera_state();const auto orientation=QQuaternion::fromAxisAndAngle(0,0,1,turn*90.F)*QQuaternion::fromAxisAndAngle(1,0,0,flip?180.F:0.F);
        camera[0]=orientation.scalar();camera[1]=orientation.x();camera[2]=orientation.y();camera[3]=orientation.z();
        view.set_camera_state(camera);view.clear_selection();
        const auto x=orientation.rotatedVector(QVector3D(1,0,0));
        const QPointF at(view.width()*.5+x.x()*50,view.height()*.5-x.y()*50);
        QMouseEvent move(QEvent::MouseMove,at,QPointF(view.mapToGlobal(at.toPoint())),Qt::NoButton,Qt::NoButton,Qt::NoModifier);
        QApplication::sendEvent(&view,&move);application.processEvents();
        check(view.offered_candidate()&&view.offered_candidate()->owner_id=="curved-selected","Curved face mouse hover selected the wrong owner");
        const auto frame=view.grabFramebuffer();unsigned selected=0,other=0;
        for(int py=0;py<frame.height();++py)for(int px=0;px<frame.width();++px) {
            const auto color=frame.pixelColor(px,py);if(color.red()>15||color.green()<180||color.blue()<235)continue;
            const double dot=(px-frame.width()*.5)*x.x()-(py-frame.height()*.5)*x.y();
            if(dot>8)++selected;if(dot<-8)++other;
        }
        if(selected<60||other>4)frame.save("build/curved-hover-owner-failure.png");
        check(selected>60&&other<=4,"Curved hover omitted its silhouette or highlighted the other feature");
    }
    std::cout<<"Curved-face hover: four quarter turns and both viewing sides passed without persisted wire edges\n";
}
int main(int argc,char** argv){QApplication application(argc,argv);try {
    viewer::MeshView view;view.resize(800,600);
    kernel::ViewerMesh mesh;std::string owner="cut";
    unsigned long long hover_uploads=1;
    if(argc>1) {
        std::vector<kernel::BodyResult> bodies;const auto part=document::PartDocument::load(std::filesystem::u8path(argv[1]),&bodies);
        check(!bodies.empty(),"User model has no calculated packet");mesh=bodies.back().mesh;
        const std::string suffix=argc>3?argv[3]:"003";
        const auto feature=std::ranges::find_if(part.history,[&](const auto& f){return f.name.ends_with(suffix);});
        check(feature!=part.history.end(),"User fixture has no requested feature");owner=feature->id;
    } else {
        mesh.vertices={{-110,-110,-1},{110,110,1},{-10,-10,0},{10,-10,0},{0,10,0}};
        mesh.triangles={2,3,4};mesh.triangle_references={{owner,"generated:profile",{}}};
        // A STEP-sized base buffer. Only the real boundaries adjacent to the
        // cutter belong to its result; another occurrence remains separate.
        for(int i=0;i<29000;++i) {
            kernel::ViewerEdge edge{{{-100.,i/150.-100,0},{100.,i/150.-100,0}},{"base","step:edge:#"+std::to_string(i),{}}};
            edge.display_owner_id="base";
            if(i==14000||i==15000||i==16000){edge.reference={owner,"generated:boundary:"+std::to_string(i),{}};edge.display_owner_id=owner;edge.edge_treatment_side_references={{"cut","generated:profile",{}}};}
            if(i==16000)edge.reference.instance_path="5:other";
            if(i==17000)edge.edge_treatment_side_references={{"cut","generated:profile",{}}}; // Adjacent face does not own this base edge.
            mesh.edges.push_back(std::move(edge));
        }
        mesh.original_references.edges.push_back({{{-50,-50,0},{50,-50,0},{50,50,0},{-50,50,0},{-50,-50,0}},{owner,"start:profile",{}}});
    }
    kernel::BodyResult snapshot;snapshot.mesh=mesh;const auto before=document::serialize_body_result(snapshot,false);
    view.set_mesh(mesh);view.set_display_mode(viewer::DisplayMode::Wire);view.show();view.fit_all();
    QCursor::setPos(view.mapToGlobal(QPoint(-20,-20)));
    const auto render=[&]{application.processEvents();auto frame=view.grabFramebuffer();check(!frame.isNull(),"No GPU framebuffer");return frame;};
    static_cast<void>(render());
    check(view.property("gpuSourceWireUploadCount").toULongLong()==0,"Idle view prepared a tool wire");
    std::vector<double> idle_times;
    for(int i=0;i<20;++i){QElapsedTimer timer;timer.start();static_cast<void>(render());if(i>=8)idle_times.push_back(timer.nsecsElapsed()/1.e6);}
    if(argc>1) {
        view.set_selection_contract({viewer::CandidateKind::Container});
        std::optional<QPointF> hit;
        const auto wanted=[&](const auto& c){return c.kind==viewer::CandidateKind::Container&&c.owner_id==owner&&c.geometry==viewer::CandidateGeometry::Display;};
        for(int y=8;y<view.height()&&!hit;y+=8)for(int x=8;x<view.width()&&!hit;x+=8)
            if(std::ranges::any_of(view.selection_candidates_at({double(x),double(y)}),wanted))hit=QPointF(x,y);
        check(hit.has_value(),"The reported real-model feature has no actual result hover candidate");
        const auto mouse=[&](QEvent::Type type,Qt::MouseButton button) {
            QMouseEvent event(type,*hit,QPointF(view.mapToGlobal(hit->toPoint())),button,
                type==QEvent::MouseButtonPress?button:Qt::NoButton,Qt::NoModifier);
            QApplication::sendEvent(&view,&event);application.processEvents();
        };
        mouse(QEvent::MouseMove,Qt::NoButton);
        for(unsigned i=0;i<32&&(!view.offered_candidate()||!wanted(*view.offered_candidate()));++i) {
            mouse(QEvent::MouseButtonPress,Qt::RightButton);mouse(QEvent::MouseButtonRelease,Qt::RightButton);
        }
        check(view.offered_candidate()&&wanted(*view.offered_candidate()),"Mouse cycling did not offer the real-model result feature");
        static_cast<void>(render());
        hover_uploads=view.property("gpuSourceWireUploadCount").toULongLong();
        check(hover_uploads>0,"Real-model hover omitted the authored preview");
        if(argc>2)render().save(QString::fromUtf8(argv[2])+".hover.png");
        mouse(QEvent::MouseButtonPress,Qt::LeftButton);mouse(QEvent::MouseButtonRelease,Qt::LeftButton);
        check(view.confirmed_candidate()&&wanted(*view.confirmed_candidate()),"Click changed the real-model offered feature identity");
    } else view.confirm_container(owner);
    static_cast<void>(render());
    check(view.confirmed_candidate().has_value(),"Cannot select the source container");
    const auto expected_vertices=argc>4?std::stoul(argv[4]):8;
    if(view.property("gpuSourceWireVertexCount").toUInt()!=expected_vertices||view.property("gpuSourceWireUploadCount").toULongLong()!=hover_uploads)
        std::cerr<<"preview vertices="<<view.property("gpuSourceWireVertexCount").toUInt()<<" expected="<<expected_vertices
            <<" uploads="<<view.property("gpuSourceWireUploadCount").toULongLong()<<" hover_uploads="<<hover_uploads<<'\n';
    check(view.property("gpuSourceWireVertexCount").toUInt()==expected_vertices&&view.property("gpuSourceWireUploadCount").toULongLong()==hover_uploads,
        "Selection omitted the authored preview or uploaded it again after hover");
    check(view.property("selectedFeatureFaceTriangleCount").toULongLong()==0,"Single-click selection filled solid faces");
    const auto selected_face_builds=view.property("containerFaceBuildCount").toULongLong();
    const auto selected_base_uploads=view.property("gpuMeshUploadCount").toULongLong();
    const auto selection_camera=view.camera_state();
    for(int i=0;i<8;++i) {
        auto changed=selection_camera;changed[5]+=i%3;changed[6]-=i%2;changed[4]*=1.F+(i%2)*.01F;
        view.set_camera_state(changed);static_cast<void>(render());
    }
    check(view.property("containerFaceBuildCount").toULongLong()==selected_face_builds&&
        view.property("gpuMeshUploadCount").toULongLong()==selected_base_uploads&&
        view.property("gpuSourceWireUploadCount").toULongLong()==hover_uploads,
        "Single-click pan/zoom rebuilt face ownership or uploaded geometry");
    view.set_camera_state(selection_camera);static_cast<void>(render());
    if(argc>2)render().save(QString::fromUtf8(argv[2])+".selected.png");
    view.set_container_inspection(owner);static_cast<void>(render());
    check(view.property("selectedFeatureFaceTriangleCount").toULongLong()==0,"Double-click inspection retained the single-click face fill");
    const auto source_uploads=view.property("gpuSourceWireUploadCount").toULongLong();
    check(source_uploads==hover_uploads&&view.property("gpuSourceWireVertexCount").toInt()>0,"Inspection reuploaded the unchanged hover/selection preview");
    const auto base_uploads=view.property("gpuMeshUploadCount").toULongLong();
    const auto color_builds=view.property("containerColorBuildCount").toULongLong();
    const auto face_builds=view.property("containerFaceBuildCount").toULongLong();
    const auto silhouette_builds=view.property("silhouetteColorBuildCount").toULongLong();
    const auto silhouette_uploads=view.property("gpuSilhouetteUploadCount").toULongLong();
    const auto camera=view.camera_state();std::vector<double> times;
    for(int i=0;i<40;++i) {
        auto changed=camera;changed[5]+=i%4;changed[6]-=i%3;changed[4]*=1.F+(i%2)*.01F;view.set_camera_state(changed);
        QElapsedTimer timer;timer.start();static_cast<void>(render());times.push_back(timer.nsecsElapsed()/1.e6);
    }
    check(view.property("gpuSourceWireUploadCount").toULongLong()==source_uploads&&
        view.property("gpuMeshUploadCount").toULongLong()==base_uploads&&
        view.property("containerColorBuildCount").toULongLong()==color_builds&&
        view.property("containerFaceBuildCount").toULongLong()==face_builds&&
        view.property("silhouetteColorBuildCount").toULongLong()==silhouette_builds&&
        view.property("gpuSilhouetteUploadCount").toULongLong()==silhouette_uploads,"Camera/repaint rebuilt source or result geometry");
    view.clear_selection();static_cast<void>(render());
    check(view.property("gpuSourceWireVertexCount").toInt()==0,"Deselection retained a source wire");
    view.confirm_container(owner);static_cast<void>(render());
    check(view.property("gpuSourceWireVertexCount").toUInt()==expected_vertices,"Reselection omitted the shared authored preview");
    check(view.property("gpuSourceWireUploadCount").toULongLong()==source_uploads,"Reselecting the same source reuploaded it");
    view.set_container_inspection(owner);static_cast<void>(render());
    check(view.property("gpuSourceWireUploadCount").toULongLong()==source_uploads,"Dimension inspection duplicated unchanged source geometry");
    view.clear_selection();static_cast<void>(render());
    auto changed=mesh;check(!changed.original_references.edges.empty(),"Source references missing");
    for(auto& edge:changed.original_references.edges)if(edge.reference.owner_id==owner)for(auto& p:edge.points)p.z+=2;
    view.set_mesh(changed,false);view.confirm_container(owner);view.set_container_inspection(owner);static_cast<void>(render());
    check(view.property("gpuSourceWireUploadCount").toULongLong()==source_uploads+1,"Changed source reused stale GPU geometry");
    kernel::BodyResult untouched;untouched.mesh=mesh;
    check(document::serialize_body_result(untouched,false)==before,"Viewer modified the authored packet");
    std::sort(times.begin(),times.end());
    std::sort(idle_times.begin(),idle_times.end());
    std::cout<<"source="<<owner<<" edges="<<mesh.edges.size()<<" source_vertices="<<view.property("gpuSourceWireVertexCount").toInt()
        <<" initial_source_uploads="<<source_uploads<<" camera_frames=40 extra_source_uploads=0 extra_base_uploads=0 extra_color_builds=0 idle_median_ms="<<idle_times[idle_times.size()/2]<<" selected_median_ms="<<times[times.size()/2]<<" p90_ms="<<times[36]<<'\n';
    if(argc>2)render().save(QString::fromUtf8(argv[2]));
    if(argc==1){verify_curved_hover(application);verify_occluded_selection(application);verify_import_batches(application);}
    return 0;
}catch(const std::exception& error){std::cerr<<error.what()<<'\n';return 1;}}
