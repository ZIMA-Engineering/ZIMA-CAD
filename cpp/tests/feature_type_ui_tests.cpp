#include "primitive_properties_dialog.hpp"
#include "application_settings.hpp"
#include "feature_naming.hpp"
#include <QPushButton>
#include <QLineEdit>
#include <QMouseEvent>
#include <QCursor>
#include <QSurfaceFormat>
#include <QDir>
#include <zima/viewer/mesh_view.hpp>
#include "../common/interaction_colors.hpp"
#include <QApplication>
#include <QComboBox>
#include <QDoubleSpinBox>
#include <QGroupBox>
#include <QLayout>
#include <QLabel>
#include <QScrollArea>
#include <QScrollBar>
#include <QDialogButtonBox>
#include <QElapsedTimer>
#include <QThread>
#include <iostream>
static void verify_rendering(QApplication& application,QWidget& parent) {
    using namespace zima;
    viewer::MeshView view(&parent);view.setGeometry(0,0,800,600);view.show();
    const auto flush=[&] {QElapsedTimer timer;timer.start();while(timer.elapsed()<60){application.processEvents();QThread::msleep(2);}};
    const auto check=[](bool ok,const char* message){if(!ok)throw std::runtime_error(message);};
    QCursor::setPos(parent.mapToGlobal(QPoint(parent.width()-10,parent.height()-10)));
    for(const auto type:{document::FeatureType::Axis,document::FeatureType::Plane,document::FeatureType::Modeling,document::FeatureType::Point}) {
        auto part=document::PartDocument::create_default();auto sketch=sketcher::Sketch::create_default();
        auto feature=document::PartDocument::create_feature_container(sketch.id);
        feature.name="";feature.feature.type=type;sketch.owner_container_id=feature.id;
        sketch.plane=type==document::FeatureType::Axis?sketcher::SketchPlane::XZ:sketcher::SketchPlane::XY;
        sketch.plane_auto=false;part.history={feature};part.sketches={sketch};part.resolve_constructions();
        auto mesh=part.feature_result_mesh(feature);
        if(type==document::FeatureType::Plane) {
            // A later coincident datum must not paint over the selected plane.
            auto other=mesh.edges.front();other.reference.owner_id="coincident-plane";
            other.display_owner_id="coincident-plane";mesh.edges.push_back(other);
        }
        view.clear_selection();view.set_mesh(std::move(mesh));
        auto camera=view.camera_state();camera[0]=1;camera[1]=camera[2]=camera[3]=0;
        // Match a normal Part camera: its Origin initializes a readable
        // screen-constant datum scale before any standalone Feature is added.
        camera[7]=10;
        view.set_camera_state(camera);view.fit_all();flush();
        const auto project=[&](kernel::Vec3 p) {
            const auto a=view.ray_at({0,0}),b=view.ray_at({double(view.width()),double(view.height())});
            check(a&&b,"Feature renderer has no camera ray");
            return QPointF((p.x-a->first.x)/(b->first.x-a->first.x)*view.width(),
                (p.y-a->first.y)/(b->first.y-a->first.y)*view.height());
        };
        const auto colored=[&](const QImage& image,const QColor& color,bool exclude_points) {
            int count=0;const double ratio=image.devicePixelRatio();
            for(int y=0;y<image.height();++y)for(int x=0;x<image.width();++x) {
                const auto c=image.pixelColor(x,y);
                if(std::abs(c.red()-color.red())>10 || std::abs(c.green()-color.green())>10 || std::abs(c.blue()-color.blue())>10)continue;
                if(exclude_points&&std::ranges::any_of(view.mesh().points,[&](const auto& p) {
                    const auto at=project(p.position)*ratio;return std::abs(x-at.x())<12*ratio&&std::abs(y-at.y())<12*ratio;
                }))continue;
                ++count;
            }
            return count;
        };
        const auto idle=view.grabFramebuffer();
        idle.save(QDir::tempPath()+"/zima-feature-idle-"+QString::number(int(type))+".png");
        std::cout<<"Render type "<<int(type)<<" axes="<<view.mesh().axes.size()<<" wire pixels="<<colored(idle,interaction::axis,true)<<std::endl;
        if(type==document::FeatureType::Axis||type==document::FeatureType::Plane)
            check(colored(idle,interaction::axis,true)>30,"Feature axis/plane has no visible idle wire");
        if(type==document::FeatureType::Modeling)
            check(colored(idle,interaction::axis,false)==0,"Modeling Feature displays its origin dot while idle");
        if(type==document::FeatureType::Axis) {
            const auto at=project({0,20,0});const auto candidates=view.selection_candidates_at(at);
            check(std::ranges::any_of(candidates,[&](const auto& c){return c.kind==viewer::CandidateKind::Container&&c.owner_id==feature.id;}),
                "Axis line does not offer its Feature container");
            QMouseEvent event(QEvent::MouseMove,at,QPointF(view.mapToGlobal(at.toPoint())),Qt::NoButton,Qt::NoButton,Qt::NoModifier);
            QApplication::sendEvent(&view,&event);flush();
            check(colored(view.grabFramebuffer(),interaction::hover,true)>30,"Hovered Axis does not highlight its line");
        }
        view.confirm_container(feature.id);flush();
        check(view.confirmed_candidate()&&view.confirmed_candidate()->owner_id==feature.id,"Tree cannot confirm a datum Feature");
        const auto selected=view.grabFramebuffer();
        check(colored(selected,interaction::selected,type==document::FeatureType::Axis||type==document::FeatureType::Plane)>10,
            "Selected Feature has no highlighted wire/origin");
        selected.save(QDir::tempPath()+"/zima-feature-render-"+QString::number(int(type))+".png");
        view.clear_selection();flush();
    }
}
int main(int argc,char** argv) {
    QSurfaceFormat format;format.setRenderableType(QSurfaceFormat::OpenGL);format.setVersion(3,3);
    format.setProfile(QSurfaceFormat::CoreProfile);format.setDepthBufferSize(24);format.setSamples(4);
    QSurfaceFormat::setDefaultFormat(format);
    QApplication application(argc,argv);
    QWidget parent;parent.resize(1600,1000);parent.show();
    auto feature=zima::document::PartDocument::create_feature_container("fixture-sketch");
    auto part=zima::document::PartDocument::create_default();
    feature.name=zima::app::next_feature_name(part,feature.feature.type,feature.id);
    feature.feature.automatic_name=feature.name;
    zima::app::PrimitivePropertiesDialog dialog(feature,true,true,[](auto){},&parent);
    dialog.set_feature_name_provider([&](auto type){return zima::app::next_feature_name(part,type,feature.id);});
    dialog.show();
    const auto flush=[&] {QElapsedTimer timer;timer.start();while(timer.elapsed()<50){application.processEvents();QThread::msleep(2);}};
    auto* type=dialog.findChild<QComboBox*>("featureType");
    try {
        for(int pass=0;pass<3;++pass)for(int i=0;i<5;++i) {
            type->setCurrentIndex(i);flush();
            const auto pending=dialog.pending_value();
            if(pending.name!=zima::app::next_feature_name(part,pending.feature.type,feature.id)||
                pending.feature.automatic_name!=pending.name)throw std::runtime_error("Type switch lost automatic naming");
            if(i>=3) {
                auto* plane=dialog.findChild<QGroupBox*>("featurePlaneGroup");
                auto* sketch=dialog.findChild<QGroupBox*>("featureSketchGroup");
                if(plane->geometry().right()>=sketch->geometry().left() || plane->y()!=sketch->y())
                    throw std::runtime_error("Sketch button is not beside the Plane section");
            }
            auto* scroll=dialog.findChild<QScrollArea*>("featureParametersScroll");
            if(i==4&&scroll->verticalScrollBar()->maximum()>0&&
                dialog.height()-scroll->mapTo(&dialog,QPoint(0,scroll->height())).y()>dialog.buttons()->height()+dialog.findChild<QLabel*>("featureValidationError")->height()+4*dialog.layout()->spacing()+dialog.layout()->contentsMargins().bottom())
                throw std::runtime_error("Unused dialog space hides Feature parameters behind a scrollbar: height="+std::to_string(dialog.height())+" scrollBottom="+std::to_string(scroll->mapTo(&dialog,QPoint(0,scroll->height())).y())+" buttons="+std::to_string(dialog.buttons()->height())+" scroll="+std::to_string(scroll->height())+" maximum="+std::to_string(scroll->maximumHeight())+" minimum="+std::to_string(scroll->minimumHeight()));
            for(const auto* name:{"featureProfilePlane","featureProfileOffset","featureSideValue0","featureSideValue1","featureThinSide"}) {
                auto* field=dialog.findChild<QWidget*>(name);
                const bool shown=QString(name)=="featureThinSide"?i==4:QString(name).startsWith("featureProfile")?i!=0:(i==1||i==4);
                if(field->isVisible()!=shown)throw std::runtime_error("Wrong Feature field visibility");
                if(shown) {
                    const auto* container=field->parentWidget();
                    std::cout<<i<<' '<<name<<" height="<<field->height()<<" hint="<<field->minimumSizeHint().height()
                        <<" group="<<container->height()<<" groupHint="<<container->minimumSizeHint().height()
                        <<" dialog="<<dialog.height()<<" dialogHint="<<dialog.minimumSizeHint().height()<<std::endl;
                    if(field->height()<field->minimumSizeHint().height()||!container->rect().contains(field->geometry()))
                        throw std::runtime_error("Feature controls are clipped after switching type");
                }
            }
        }
        auto* name=dialog.findChild<QLineEdit*>("featureName");
        name->setText("My reference");type->setCurrentIndex(1);flush();
        if(dialog.pending_value().name!="My reference"||!dialog.pending_value().feature.automatic_name.empty())
            throw std::runtime_error("Type switch overwrote a custom name");
        part.history.push_back(feature);part.history.back().name=zima::app::feature_name_prefix(zima::document::FeatureType::Axis).toStdString()+" 001";
        if(zima::app::next_feature_name(part,zima::document::FeatureType::Axis,"")!=
            zima::app::feature_name_prefix(zima::document::FeatureType::Axis).toStdString()+" 002")
            throw std::runtime_error("Automatic name collides with an existing object");
    dialog.hide();verify_rendering(application,parent);
    }catch(const std::exception& e){std::cerr<<e.what()<<std::endl;return 1;}
    return 0;
}
