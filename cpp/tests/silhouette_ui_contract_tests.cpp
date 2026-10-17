#include <zima/viewer/mesh_view.hpp>
#include <zima/workspace/native_documents.hpp>
#include <QApplication>
#include <QEventLoop>
#include <QTimer>
#include <QElapsedTimer>
#include <QPalette>
#include <QCryptographicHash>
#include <QFile>
#include <QWheelEvent>
#include <QMouseEvent>
#include <QQuaternion>
#include <fstream>
#include <iostream>
#include <numbers>

using namespace zima;
namespace fs=std::filesystem;
static void check(bool value,const char* text){if(!value)throw std::runtime_error(text);}
static void flush(QApplication& app) {
    app.processEvents();QEventLoop wait;QTimer::singleShot(25,&wait,&QEventLoop::quit);wait.exec();app.processEvents();
}
static QByteArray hash_file(const fs::path& path) {
    QFile file(QString::fromStdString(path.string()));check(file.open(QIODevice::ReadOnly),"Cannot read source hash");
    QCryptographicHash hash(QCryptographicHash::Sha256);check(hash.addData(&file),"Cannot hash source");return hash.result();
}
static kernel::ViewerMesh curved_mesh(double radius,bool sphere) {
    kernel::ViewerMesh mesh;constexpr unsigned count=48,rows=16;
    for(unsigned j=0;j<=rows;++j)for(unsigned i=0;i<=count;++i) {
        const double u=2*std::numbers::pi*i/count;
        const double v=std::numbers::pi*j/rows;
        if(sphere)mesh.vertices.push_back({radius*std::sin(v)*std::cos(u),radius*std::sin(v)*std::sin(u),radius*std::cos(v)});
        else {
            const double minor=radius*std::cos(v);
            mesh.vertices.push_back({(5+2*radius+minor)*std::cos(u),(5+2*radius+minor)*std::sin(u),radius*std::sin(v)});
        }
    }
    for(unsigned j=0;j<rows;++j)for(unsigned i=0;i<count;++i) {
        const unsigned a=j*(count+1)+i,b=a+1,c=a+count+1,d=c+1;
        for(auto triangle:{std::array{a,b,d},std::array{a,d,c}}) {
            mesh.triangles.insert(mesh.triangles.end(),triangle.begin(),triangle.end());
            mesh.triangle_references.push_back({"curved",sphere?"sphere":"fillet",{}});
        }
    }
    return mesh;
}
int main(int argc,char** argv) {
    QApplication app(argc,argv);
    try {
        check(argc>=2,"Usage: silhouette-ui-contract output-directory [native-parts...]");
        const fs::path output=fs::absolute(argv[1]);fs::create_directories(output);
        QPalette dark;dark.setColor(QPalette::Window,QColor(40,45,52));dark.setColor(QPalette::WindowText,Qt::white);
        dark.setColor(QPalette::Text,Qt::white);dark.setColor(QPalette::Base,QColor(30,34,40));app.setPalette(dark);
        viewer::MeshView view;view.resize(1000,800);view.show();flush(app);
        std::vector<std::pair<std::string,kernel::ViewerMesh>> scenes;
        if(!qEnvironmentVariableIsSet("ZIMA_SILHOUETTE_NATIVE_ONLY"))for(double r:{.02,.1,1.,10.})for(bool sphere:{false,true})
            scenes.emplace_back(std::string(sphere?"sphere-":"fillet-")+std::to_string(r),curved_mesh(r,sphere));
        std::vector<std::pair<fs::path,QByteArray>> sources;
        for(int i=2;i<argc;++i) {
            const auto path=fs::absolute(fs::u8path(argv[i]));sources.emplace_back(path,hash_file(path));
            workspace::Workspace live;const auto id=workspace::insert_native_document(live,workspace::read_native_document(path));
            const auto* state=live.open_part(id);check(state,"Native source is not a Part");
            const auto& boundaries=state->session.calculated_boundaries();check(!boundaries.empty(),"Source has no calculated geometry");
            for(const auto& [body,value]:boundaries.back().body_outputs)
                scenes.emplace_back(path.stem().string()+"-"+body,value.get().mesh);
        }
        std::size_t frames=0;double render_ms=0;
        for(auto& [name,mesh]:scenes) {
            mesh.dimensions.clear();mesh.axes.clear();mesh.points.clear();view.set_mesh(mesh);view.fit_all();flush(app);
            for(auto projection:{viewer::ProjectionMode::Orthographic,viewer::ProjectionMode::Perspective}) {
                view.set_projection_mode(projection);
                int direction=0;
                for(const kernel::Vec3 camera:std::array{kernel::Vec3{0,-1,0},kernel::Vec3{1,-2,.3},kernel::Vec3{-1,-1,1},kernel::Vec3{1,1,-1},kernel::Vec3{1,-2,-.05},kernel::Vec3{1,-2,-.25}}) {
                    view.set_view_direction(camera,qEnvironmentVariableIsSet("ZIMA_SILHOUETTE_CLOSEUP")?35.F:0.F);
                    {QEventLoop animation;QTimer::singleShot(900,&animation,&QEventLoop::quit);animation.exec();}
                    view.fit_all();flush(app);
                    if(name.starts_with("HHS-")||name.starts_with("HHB-")) {
                        view.fit_points({{-3,-3,-1},{3,3,1}});flush(app);
                        if(qEnvironmentVariableIsSet("ZIMA_SILHOUETTE_CLOSEUP")) {
                            const auto state=view.camera_state();
                            auto radial=QQuaternion(state[0],state[1],state[2],state[3]).inverted().rotatedVector({-1,0,0});
                            radial.setZ(0);radial.normalize();const auto center=radial*1.55F;
                            view.fit_points({{center.x()-.25,center.y()-.25,-.2},{center.x()+.25,center.y()+.25,.3}});flush(app);
                        }
                    }
                    int mode=0;
                    for(auto style:{viewer::DisplayMode::ShadedWithEdges,viewer::DisplayMode::Shaded,viewer::DisplayMode::HiddenEdges}) {
                        view.set_display_mode(style);flush(app);QElapsedTimer timer;timer.start();const auto image=view.grabFramebuffer();render_ms+=timer.nsecsElapsed()/1e6;
                        check(!image.isNull(),"Native OpenGL frame is empty");
                        const auto file=output/(name+"-p"+std::to_string(int(projection))+"-v"+std::to_string(direction)+"-m"+std::to_string(mode++)+".png");
                        check(image.save(QString::fromStdString(file.string())),"Cannot save silhouette evidence");++frames;
                        const auto builds=view.property("gpuSilhouetteBuildCount").toULongLong();
                        const QPointF center(view.width()/2.,view.height()/2.);
                        QWheelEvent wheel(center,QPointF(view.mapToGlobal(center.toPoint())),{},QPoint(0,120),Qt::NoButton,Qt::NoModifier,Qt::NoScrollPhase,false);
                        QApplication::sendEvent(&view,&wheel);
                        const auto mouse=[&](QEvent::Type type,QPointF p,Qt::MouseButton button,Qt::MouseButtons buttons) {
                            QMouseEvent event(type,p,QPointF(view.mapToGlobal(p.toPoint())),button,buttons,Qt::NoModifier);
                            QApplication::sendEvent(&view,&event);
                        };
                        mouse(QEvent::MouseButtonPress,center,Qt::MiddleButton,Qt::MiddleButton|Qt::RightButton);
                        mouse(QEvent::MouseMove,center+QPointF(8,4),Qt::NoButton,Qt::MiddleButton|Qt::RightButton);
                        mouse(QEvent::MouseButtonRelease,center+QPointF(8,4),Qt::MiddleButton,Qt::NoButton);flush(app);
                        static_cast<void>(view.grabFramebuffer());
                        check(view.property("gpuSilhouetteBuildCount").toULongLong()==builds,"Pan/zoom rebuilt a camera-independent silhouette");
                    }
                    ++direction;
                }
            }
            if(view.property("gpuSilhouetteBuildCount").isValid()) {
                const auto before=view.property("gpuSilhouetteBuildCount").toULongLong();
                auto moved=mesh;for(auto& point:moved.vertices)point.x+=.01;
                view.set_mesh(std::move(moved),false);flush(app);static_cast<void>(view.grabFramebuffer());
                check(view.property("gpuSilhouetteBuildCount").toULongLong()>before,
                    "Changed source geometry reused an obsolete silhouette");
            }
        }
        for(const auto& [path,before]:sources)check(hash_file(path)==before,"Read-only silhouette inspection changed a native source");
        std::ofstream(output/"result.json")<<"{\"scenes\":"<<scenes.size()<<",\"frames\":"<<frames<<",\"capture_ms\":"<<render_ms<<",\"sources_unchanged\":true}";
        std::cout<<scenes.size()<<" scenes, "<<frames<<" frames: projections, radii, view sides, display modes and pan/zoom cache passed\n";
        return 0;
    }catch(const std::exception& error){std::cerr<<error.what()<<'\n';return 1;}
}
