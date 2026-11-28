#include <zima/viewer/mesh_view.hpp>
#include <zima/kernel/calculation_diagnostics.hpp>
#include <QApplication>
#include <QWheelEvent>
#include <QQuaternion>
#include <cmath>
#include <iostream>
#include <limits>
#include <stdexcept>

static void check(bool ok,const char* message){if(!ok)throw std::runtime_error(message);}
int main(int argc,char** argv) {
    QApplication application(argc,argv);
    try {
        zima::viewer::MeshView view;view.resize(1000,700);
        zima::kernel::ViewerMesh mesh;mesh.vertices={{-10,-10,-10},{10,10,10}};view.set_mesh(mesh);
        const auto calls=zima::kernel::body_calculation_count.load();
        const std::array<float,11> baseline{1,0,0,0,20,12,-7,20,3,1,-2};
        const auto rotation=[](const auto& c){return QQuaternion(c[0],c[1],c[2],c[3]);};
        const auto center=[](const auto& c){return QVector3D(c[8],c[9],c[10]);};
        for(const auto mode:{zima::viewer::ProjectionMode::Orthographic,zima::viewer::ProjectionMode::Perspective}) {
            view.set_projection_mode(mode);view.set_camera_state(baseline);
            view.navigate_camera({31,-18},{},0,view.rect().center());
            auto after=view.camera_state();check(after[5]==43&&after[6]==-25,"Camera pan has wrong pixel displacement");
            for(int i:{0,1,2,3,4,7,8,9,10})check(after[i]==baseline[i],"Pan changed another camera component");
            const QVector3D pivot(5,-3,2);const auto before=after;
            for(int i=0;i<20;++i)view.navigate_camera({},QQuaternion::fromAxisAndAngle(QVector3D(1,2,3).normalized(),2),0,
                view.rect().center(),zima::kernel::Vec3{5,-3,2});
            after=view.camera_state();
            check((rotation(after).rotatedVector(pivot-center(after))-rotation(before).rotatedVector(pivot-center(before))).length()<1e-4,
                "Repeated rotation moved its world pivot");
            const auto expected=QQuaternion::fromAxisAndAngle(QVector3D(1,2,3).normalized(),40);
            check(std::abs(QQuaternion::dotProduct(rotation(after),expected))>1-1e-6,"Simultaneous-axis rotation is wrong");
            for(int i=4;i<8;++i)check(after[i]==before[i],"Rotation changed framing");
            view.set_camera_state(baseline);const QPointF anchor(723,156);
            view.navigate_camera({}, {},std::log(1.15),anchor);after=view.camera_state();
            check(std::abs(after[4]-23)<1e-5,"Zoom scale is wrong");
            const QPointF expected_pan=anchor-QPointF(500,350)-(anchor-QPointF(500,350)-QPointF(12,-7))/1.15;
            check(std::abs(after[5]-expected_pan.x())<1e-4&&std::abs(after[6]-expected_pan.y())<1e-4,"Zoom anchor drifted");
            view.set_camera_state(baseline);
            QWheelEvent wheel(anchor,anchor,{},QPoint(0,120),Qt::NoButton,Qt::NoModifier,Qt::NoScrollPhase,false);
            QApplication::sendEvent(&view,&wheel);
            for(int i=0;i<11;++i)check(std::abs(view.camera_state()[i]-after[i])<1e-5,"Mouse wheel differs from controller camera path");
            const auto valid=view.camera_state();
            view.navigate_camera({std::numeric_limits<double>::quiet_NaN(),0},{},0,anchor);
            view.navigate_camera({},QQuaternion(0,0,0,0),1,anchor);
            check(view.camera_state()==valid,"Invalid input damaged camera");
            for(int rate:{30,60,120}) {
                view.set_camera_state(baseline);
                for(int i=0;i<rate;++i)view.navigate_camera({},QQuaternion::fromAxisAndAngle(QVector3D(1,2,3).normalized(),40.F/rate),0,anchor);
                check(std::abs(QQuaternion::dotProduct(rotation(view.camera_state()),expected))>1-1e-6,"Camera rotation depends on frame rate");
            }
        }
        view.set_projection_mode(zima::viewer::ProjectionMode::Perspective);view.set_camera_state(baseline);view.set_fly_navigation_enabled(true);
        const QPointF pixel(500,350);auto ray=view.ray_at(pixel);check(ray.has_value(),"Fly camera ray missing");
        const auto start=ray->first;view.navigate_camera({}, {},std::log(1.15),pixel);ray=view.ray_at(pixel);
        const double distance=std::sqrt(std::pow(ray->first.x-start.x,2)+std::pow(ray->first.y-start.y,2)+std::pow(ray->first.z-start.z,2));
        check(distance>0&&view.camera_state()[4]==baseline[4],"Fly zoom failed to translate eye");
        check(zima::kernel::body_calculation_count.load()==calls,"Camera navigation invoked geometry calculation");
        std::cout<<"Camera pan, quaternion/pivot, zoom anchor, mouse wheel, finite input, 30/60/120 Hz and fly contracts passed\n";
        return 0;
    }catch(const std::exception& e){std::cerr<<e.what()<<'\n';return 1;}
}
