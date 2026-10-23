#include "primitive_properties_dialog.hpp"
#include "application_settings.hpp"
#include "feature_naming.hpp"
#include "feature_limit_table.hpp"
#include "feature_limit_diagnostic.hpp"
#include <QFile>
#include <QJsonDocument>
#include <QJsonObject>
#include <QPainter>
#include <QStyleOptionViewItem>
#include <QFormLayout>
#include <filesystem>
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
#include <QCheckBox>
#include <QDoubleSpinBox>
#include <QGroupBox>
#include <QLayout>
#include <QLabel>
#include <QScrollArea>
#include <QScrollBar>
#include <QDialogButtonBox>
#include <QElapsedTimer>
#include <QThread>
#include <QToolButton>
#include <iostream>
static void verify_limit_localization(QApplication& application,QWidget& parent) {
    using namespace zima;
    const auto check=[](bool ok,const char* text){if(!ok)throw std::runtime_error(text);};
    auto authored=document::PartDocument::create_feature_container("localized-limits");
    authored.feature.sides[0].extrusion_extent=document::EndCondition::UpTo;
    for(const auto* owner:{"source-a","source-b"}) {
        document::ExtrusionParameters::EndTarget target;target.reference={owner,"plane",{}};target.label=owner;
        authored.feature.sides[0].targets.push_back(target);
    }
    auto failure=kernel::OrderedUpToFailure::AmbiguousAdjacency;
    app::PrimitivePropertiesDialog dialog(authored,false,true,[&](auto){throw kernel::OrderedUpToError(failure,0,1);},&parent);
    dialog.show();application.processEvents();
    auto* limits=static_cast<app::FeatureLimitTable*>(dialog.findChild<QWidget*>("featureSideLimits0"));
    limits->begin_entry(1);
    auto* eye=limits->table()->cellWidget(0,6)->findChild<QToolButton*>();eye->click();
    const auto before=dialog.pending_value();int previews=0;dialog.set_preview_callback([&](const auto&){++previews;});
    previews=0;
    const auto palette=application.palette();
    const auto root=std::filesystem::path(__FILE__).parent_path().parent_path().parent_path()/"config/localization";
    for(const auto* language:{"cs","en","de","fr","ru"}) {
        QFile file(QString::fromStdString((root/(std::string(language)+".qt.json")).generic_string()));
        check(file.open(QIODevice::ReadOnly),"Cannot read the ordered-limit translation catalog");
        app::ApplicationSettings settings;settings.language=language;
        const auto messages=QJsonDocument::fromJson(file.readAll()).object();
        for(auto item=messages.begin();item!=messages.end();++item)settings.qt_translations[item.key()]=item.value().toString();
        app::apply_application_translations(application,settings);application.processEvents();
        for(const auto* theme:{"light","dark"}) {
            settings.theme=theme;app::apply_application_appearance(application,settings);application.processEvents();
            check(limits->findChild<QPushButton*>("featureLimitDown")->text()==QObject::tr("Dolů"),"Live language change missed the target ordering controls");
            check(limits->active_row()==1&&dialog.highlighted_reference_entries().size()==1,"Language/theme change lost entry or inspection identity");
            check(dialog.findChild<QLabel*>("featureUpToDraftNotice")->text()==QObject::tr(
                "Draft preview: profile. Inspect target faces with the eyes; OK calculates the result."),"Draft preview notice did not follow the live language");
            auto* correction=dialog.findChild<QDoubleSpinBox*>("featureSideCorrection0");
            auto* form=qobject_cast<QFormLayout*>(correction->parentWidget()->layout());
            check(form&&qobject_cast<QLabel*>(form->labelForField(correction))->text()==QObject::tr("Korekce"),
                "Correction label did not follow the live language");
            for(int row:{0,1}) {
                QImage cell(240,36,QImage::Format_ARGB32_Premultiplied);cell.fill(application.palette().color(QPalette::Base));
                QPainter painter(&cell);QStyleOptionViewItem option;option.rect=cell.rect();
                option.palette=application.palette();option.font=limits->table()->font();option.state=QStyle::State_Enabled;
                option.widget=limits->table();limits->table()->itemDelegate()->paint(&painter,option,limits->table()->model()->index(row,3));painter.end();
                check(row==0?cell.pixelColor(230,18)==QColor("#00D1FF"):cell.pixelColor(1,18)==QColor("#4DD811"),
                    "Reference inspection/input colors violate the shared azure/green contract");
            }
            for(const auto code:{kernel::OrderedUpToFailure::InvalidInput,kernel::OrderedUpToFailure::AmbiguousAdjacency,
                    kernel::OrderedUpToFailure::AmbiguousRetainedPatch,kernel::OrderedUpToFailure::EmptyPatch,
                    kernel::OrderedUpToFailure::DisconnectedChain,kernel::OrderedUpToFailure::SelfIntersection}) {
                failure=code;dialog.buttons()->button(QDialogButtonBox::Ok)->click();application.processEvents();
                const auto expected=app::feature_limit_diagnostic(kernel::OrderedUpToError(code,0,1));
                const auto actual=dialog.findChild<QLabel*>("featureValidationError")->text();
                check(actual==expected&&actual.contains('1')&&actual.contains('2')&&!actual.contains("%1"),
                    "Rejected limit did not show localized affected rows and reason");
            }
            for(auto* ancestor=limits->parentWidget();ancestor;ancestor=ancestor->parentWidget())
                if(auto* scroll=qobject_cast<QScrollArea*>(ancestor))scroll->ensureWidgetVisible(limits);
            application.processEvents();application.processEvents();
            check(dialog.findChild<QScrollArea*>("featureParametersScroll")->horizontalScrollBar()->maximum()==0,
                "Feature target columns overflow a sufficiently wide parent View");
            dialog.grab().save(QDir::tempPath()+"/zima-ordered-limits-"+language+"-"+theme+".png");
        }
    }
    check(previews==0&&dialog.pending_value().feature==before.feature,"Language/theme changes calculated or altered a Feature draft");
    dialog.reject();app::apply_application_translations(application,app::ApplicationSettings{});application.setPalette(palette);
    std::cout<<"Ordered Up To: five languages, two themes, live state preservation and six localized row diagnostics passed"<<std::endl;
}
static void verify_ordered_limits(QApplication& application,QWidget& parent) {
    using namespace zima;
    const auto check=[](bool ok,const char* message){if(!ok)throw std::runtime_error(message);};
    const auto flush=[&]{QElapsedTimer timer;timer.start();while(timer.elapsed()<50){application.processEvents();QThread::msleep(2);}};
    auto authored=document::PartDocument::create_feature_container("ordered-ui-sketch");
    authored.feature.sides[0].extrusion_extent=document::EndCondition::UpTo;
    authored.feature.sides[0].up_to.length_correction=-0.;
    authored.feature.sides[0].up_to.angle_correction_degrees=7.123456789;
    int commits=0,picks=0,inspections=0,previews=0;
    app::PrimitivePropertiesDialog dialog(authored,true,true,[&](auto){++commits;},&parent);
    dialog.set_extrusion_target_request([&]{++picks;});
    dialog.set_reference_highlights_changed_callback([&]{++inspections;});
    dialog.set_preview_callback([&](const auto&){++previews;});
    dialog.show();flush();
    auto* limits=static_cast<app::FeatureLimitTable*>(dialog.findChild<QWidget*>("featureSideLimits0"));
    auto* table=limits->table();
    const auto click=[&](QWidget* widget,QPoint point){
        for(const auto type:{QEvent::MouseButtonPress,QEvent::MouseButtonRelease}) {
            QMouseEvent event(type,QPointF(point),QPointF(widget->mapToGlobal(point)),Qt::LeftButton,
                type==QEvent::MouseButtonPress?Qt::LeftButton:Qt::NoButton,Qt::NoModifier);
            QApplication::sendEvent(widget,&event);
        }
        flush();
    };
    const auto field=[&](int row){
        table->scrollToItem(table->item(row,3));
        for(auto* ancestor=table->parentWidget();ancestor;ancestor=ancestor->parentWidget())
            if(auto* area=qobject_cast<QScrollArea*>(ancestor))area->ensureWidgetVisible(table);
        flush();click(table->viewport(),table->visualItemRect(table->item(row,3)).center());
        if(limits->active_row()!=static_cast<std::size_t>(row)) {
            dialog.grab().save(QDir::tempPath()+"/zima-ordered-entry-failure.png");
            const auto box=table->visualItemRect(table->item(row,3));
            std::cerr<<"Entry row="<<row<<" rect="<<box.x()<<','<<box.y()<<','<<box.width()<<','<<box.height()
                <<" viewport="<<table->viewport()->width()<<'x'<<table->viewport()->height()<<" active="
                <<(limits->active_row()?int(*limits->active_row()):-1)<<'\n';
        }
    };
    const auto assign=[&](const char* owner){dialog.set_extrusion_target({owner,"plane",{}},{0,0,12},{0,0,1},owner);flush();};
    check(table->isVisible()&&table->rowCount()>=3,"Ordered Up To table is absent or shorter than three rows");
    check(std::signbit(dialog.pending_value().feature.sides[0].up_to.length_correction),"Opening the table lost signed zero");
    for(int row=0;row<3;++row) {
        field(row);check(limits->active_row()==row,"Reference field did not arm the selected row");
        check(dialog.allows_point_end_target()==(row==0),"Point picking was offered for a multi-face chain");
        assign(row==0?"source-a":row==1?"source-b":"source-c");
    }
    check(picks==3&&dialog.pending_value().feature.sides[0].targets.size()==3,"Appending references replaced an earlier row");
    auto* eye=table->cellWidget(0,6)->findChild<QToolButton*>();
    click(eye,eye->rect().center());
    check(dialog.highlighted_reference_entries().size()==1,"Eye did not inspect the exact reference");
    const auto before_entry=previews;
    field(1);check(dialog.highlighted_reference_entries().size()==1,"Arming another row cleared independent inspection");
    check(previews==before_entry,"Reference entry calculated a new preview");
    const auto before_rejected=dialog.pending_value();bool rejected=false;
    try {assign("source-a");}catch(const std::invalid_argument&){rejected=true;}
    check(rejected&&dialog.pending_value().feature==before_rejected.feature&&limits->active_row()==1,
        "Duplicate reference rejection changed the draft or ended input");
    assign("source-d");check(dialog.pending_value().feature.sides[0].targets[1].reference.owner_id=="source-d",
        "Replacing a middle row changed another reference");
    // Ordering owns one checkbox, independently of field input and inspection.
    table->item(0,0)->setCheckState(Qt::Checked);flush();
    auto* down=limits->findChild<QPushButton*>("featureLimitDown");
    check(down->isEnabled(),"Selected row cannot move down");click(down,down->rect().center());
    check(dialog.pending_value().feature.sides[0].targets[1].reference.owner_id=="source-a"&&
        table->item(1,0)->checkState()==Qt::Checked&&dialog.highlighted_reference_entries().size()==1,
        "Reordering lost the selected or inspected reference identity");
    auto* choose=table->cellWidget(1,4)->findChild<QComboBox*>();choose->setCurrentIndex(2);flush();
    const auto graph=dialog.pending_value().feature.sides[0];
    check(graph.up_to.retained_side(graph.targets[1].reference,graph.targets[0].reference)==false,
        "Retained patch selection is not attached to its directed source pair");
    dialog.finish_extrusion_target_entry(true);check(dialog.highlighted_reference_entries().empty(),"Ending input retained temporary inspection");
    for(const double correction:{-2.5,0.,3.25}) {
        check(dialog.set_inline_parameter_value("side0_up_to_length_correction",correction),"Length correction is not editable");
        check(dialog.pending_value().feature.sides[0].up_to.length_correction==correction,"Length correction was altered");
    }
    auto* mode=dialog.findChild<QComboBox*>("featureSideMode0");mode->setCurrentIndex(2);flush();
    dialog.findChild<QComboBox*>("featureSideEnd0")->setCurrentIndex(2);flush();
    check(dialog.pending_value().feature.sides[0].up_to.angle_correction_degrees==7.123456789,
        "Switching operation rounded the authored angular correction");
    check(dialog.set_inline_parameter_value("side0_up_to_angle_correction",-4.5),"Angular correction is not editable");
    mode->setCurrentIndex(1);flush();
    check(dialog.pending_value().feature.sides[0].up_to.length_correction==3.25&&
        dialog.pending_value().feature.sides[0].up_to.angle_correction_degrees==-4.5,"Switching operations lost independent corrections");
    auto* type=dialog.findChild<QComboBox*>("featureType");type->setCurrentIndex(1);flush();
    dialog.set_extrusion_target({"axis-source","plane",{}},{0,0,20},{0,0,1},"axis-source");
    const auto single=dialog.pending_value().feature.sides[0];
    check(single.targets.size()==1&&single.up_to.retained_patches.empty()&&!single.up_to.primary_cap_source,
        "Replacing an Axis's single limit retained obsolete multi-face adjacency");
    type->setCurrentIndex(4);flush();
    check(dialog.pending_value().feature.sides[0].up_to.length_correction==3.25&&
        dialog.pending_value().feature.sides[0].up_to.angle_correction_degrees==-4.5,"Axis transition lost inactive Modeling corrections");
    check(commits==0&&inspections>0,"Draft interaction committed a document transaction");
    dialog.reject();check(commits==0&&authored.feature.sides[0].targets.empty(),"Cancel changed the source definition");
    std::cout<<"Ordered Up To: append/replace/reorder/retained sides/inspection/corrections/Cancel passed"<<std::endl;
}
static void verify_rendering(QApplication& application,QWidget& parent) {
    using namespace zima;
    viewer::MeshView view(&parent);view.setGeometry(0,0,800,600);view.show();
    const auto flush=[&] {QElapsedTimer timer;timer.start();while(timer.elapsed()<60){application.processEvents();QThread::msleep(2);}};
    const auto check=[](bool ok,const char* message){if(!ok)throw std::runtime_error(message);};
    QCursor::setPos(parent.mapToGlobal(QPoint(parent.width()-10,parent.height()-10)));
    for(const auto type:{document::FeatureType::Axis,document::FeatureType::Plane,document::FeatureType::Modeling,document::FeatureType::Point}) {
        auto part=document::PartDocument::create_default();auto sketch=sketcher::Sketch::create_default();
        auto feature=document::PartDocument::create_feature_container(sketch.id);
        feature.name="";feature.feature.type=type;feature.feature.show_point=true;
        sketch.owner_container_id=feature.id;
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
            check(colored(idle,interaction::axis,false)>0,"Enabled Modeling origin dot is not visible while idle");
        if(type==document::FeatureType::Modeling) {
            for(bool point:{false,true})for(bool text:{false,true}) {
                auto changed=feature;changed.name="Visible name";
                changed.feature.show_point=point;changed.feature.show_text=text;
                view.set_mesh(part.feature_result_mesh(changed));flush();
                check((colored(view.grabFramebuffer(),interaction::axis,false)>0)==(point||text),
                    "Idle Feature visibility switches do not control rendering");
            }
            view.set_mesh(part.feature_result_mesh(feature));flush();
        }
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
    feature.feature.sides[0].draft_angle_degrees=5.123456789;
    feature.feature.sides[1].draft_angle_degrees=-3.987654321;
    zima::app::PrimitivePropertiesDialog dialog(feature,true,true,[](auto){},&parent);
    dialog.set_feature_name_provider([&](auto type){return zima::app::next_feature_name(part,type,feature.id);});
    dialog.show();
    const auto flush=[&] {QElapsedTimer timer;timer.start();while(timer.elapsed()<50){application.processEvents();QThread::msleep(2);}};
    auto* type=dialog.findChild<QComboBox*>("featureType");
    try {
        flush();
        auto* initial_scroll=dialog.findChild<QScrollArea*>("propertiesContentScrollArea");
        if(initial_scroll->verticalScrollBar()->maximum()>0&&dialog.height()<parent.height()-30)
            throw std::runtime_error("Initial Feature properties unnecessarily hide fields: height="+
                std::to_string(dialog.height())+" overflow="+std::to_string(initial_scroll->verticalScrollBar()->maximum()));
        if(dialog.pending_value().feature.sides!=feature.feature.sides)
            throw std::runtime_error("Opening Feature properties rounded an authored side parameter");
        const auto initial_type=type->currentIndex();type->setCurrentIndex(4);flush();
        QCursor::setPos(parent.mapToGlobal(QPoint(5,parent.height()-5)));flush();
        auto* result_group=dialog.findChild<QGroupBox*>("featureResultGroup");
        auto* add=dialog.findChild<QPushButton*>("featureAdd");
        auto* subtract=dialog.findChild<QPushButton*>("featureSubtract");
        const auto result_rect=result_group->geometry(),add_rect=add->geometry(),subtract_rect=subtract->geometry();
        const auto dialog_rect=dialog.geometry();
        const auto initial_parameters=dialog.pending_value().feature;
        for(int pass=0;pass<3;++pass)for(auto* button:{subtract,add}) {
            const auto icon_key=button->icon().cacheKey();
            QCursor::setPos(button->mapToGlobal(button->rect().center()));flush();
            QEnterEvent enter(button->rect().center(),button->rect().center(),button->mapToGlobal(button->rect().center()));
            QApplication::sendEvent(button,&enter);flush();
            if(button->icon().cacheKey()!=icon_key)
                throw std::runtime_error("Operation hover replaces its unchanged icon and invalidates layout");
            if(result_group->geometry()!=result_rect||add->geometry()!=add_rect||subtract->geometry()!=subtract_rect||dialog.geometry()!=dialog_rect)
                throw std::runtime_error("Hovering a Feature operation changes the parameter block geometry");
            QCursor::setPos(parent.mapToGlobal(QPoint(5,parent.height()-5)));flush();
            if(result_group->geometry()!=result_rect||dialog.pending_value().feature!=initial_parameters)
                throw std::runtime_error("Leaving a Feature operation changes geometry or parameters");
        }
        type->setCurrentIndex(initial_type);flush();
        auto* first=dialog.findChild<QComboBox*>("featureSideMode0");
        auto* second=dialog.findChild<QComboBox*>("featureSideMode1");
        auto* first_end=dialog.findChild<QComboBox*>("featureSideEnd0");
        auto* second_end=dialog.findChild<QComboBox*>("featureSideEnd1");
        auto* first_angle=dialog.findChild<QDoubleSpinBox*>("featureSideValue0");
        auto* second_angle=dialog.findChild<QDoubleSpinBox*>("featureSideValue1");
        {
        const auto original_palette=application.palette();
        for(const auto& theme:{QStringLiteral("light"),QStringLiteral("dark"),QStringLiteral("light")}) {
        zima::app::ApplicationSettings appearance;appearance.theme=theme;
        zima::app::apply_application_appearance(application,appearance);
        zima::app::PrimitivePropertiesDialog probe(feature,true,true,[](auto){},&parent);
        probe.show();flush();
        auto* probe_mode=probe.findChild<QComboBox*>("featureSideMode0");
        auto* probe_end=probe.findChild<QComboBox*>("featureSideEnd0");
        // The fixed-size inspection eye must fit its reference row after a
        // compact operation becomes an Extrusion, including a short viewport.
        for(const int available_height:{1400,720}) {
            parent.resize(1600,available_height);
            for(const int operation:{2,1,3,1}) {
                probe_mode->setCurrentIndex(operation);flush();
                probe_end->setCurrentIndex(operation==2?2:1);flush();
                if(operation==3)continue;
                auto* area=probe.findChild<QScrollArea*>("featureParametersScroll");
                if(area->verticalScrollBar()->maximum()>0&&probe.height()<parent.height()-30)
                    throw std::runtime_error("Feature operation change leaves unused height while scrolling its rows");
                for(auto* eye:probe.findChildren<QToolButton*>("referenceInspectionButton")) {
                    if(!eye->isVisible())continue;
                    auto* row=eye->parentWidget();
                    std::cout<<"UpTo eye="<<eye->width()<<'x'<<eye->height()
                        <<" row="<<row->width()<<'x'<<row->height()
                        <<" dialog="<<probe.height()<<std::endl;
                    if(eye->size()!=QSize(30,30)||!row->rect().contains(eye->geometry()))
                        throw std::runtime_error("Feature UpTo inspection eye is clipped by its reference row");
                    // A short View legitimately needs scrolling. Its complete
                    // fixed-size row must remain reachable in both viewports.
                    for(auto* ancestor=row;ancestor;ancestor=ancestor->parentWidget())
                        if(auto* area=qobject_cast<QScrollArea*>(ancestor))area->ensureWidgetVisible(eye);
                    flush();
                    for(auto* ancestor=row->parentWidget();ancestor&&!qobject_cast<QScrollArea*>(ancestor);ancestor=ancestor->parentWidget()) {
                        const QRect bounds(eye->mapTo(ancestor,QPoint()),eye->size());
                        if(!ancestor->rect().contains(bounds)) {
                            probe.grab().save(QDir::tempPath()+"/zima-up-to-eye-clipping.png");
                            std::cout<<"clip="<<ancestor->objectName().toStdString()
                                <<" bounds="<<bounds.x()<<','<<bounds.y()<<','<<bounds.width()<<','<<bounds.height()
                                <<" ancestor="<<ancestor->width()<<'x'<<ancestor->height()<<std::endl;
                            throw std::runtime_error("Feature UpTo inspection eye is clipped by "+ancestor->objectName().toStdString());
                        }
                    }
                }
            }
        }
        parent.resize(1600,1000);flush();
        }
        application.setPalette(original_palette);
        }
        const auto verify_rotation=[&](bool ok){if(!ok)throw std::runtime_error("Feature rotation controls allow overlap or change an extrusion");};
        first->setCurrentIndex(2);second->setCurrentIndex(2);
        first_angle->setValue(300);verify_rotation(second_angle->value()==60);
        second_angle->setValue(200);verify_rotation(first_angle->value()==160);
        second_end->setCurrentIndex(1);verify_rotation(first->currentIndex()==0);
        first->setCurrentIndex(1);verify_rotation(second->currentIndex()==2&&second_end->currentIndex()==1);
        first->setCurrentIndex(2);first_end->setCurrentIndex(0);first_angle->setValue(90);
        auto* symmetry=dialog.findChild<QCheckBox*>("featureSymmetric");symmetry->setChecked(true);
        first_angle->setValue(250);verify_rotation(first_angle->value()==180);
        first_end->setCurrentIndex(1);verify_rotation(!symmetry->isChecked()&&second->currentIndex()==0);
        first->setCurrentIndex(1);first_end->setCurrentIndex(0);
        if(dialog.pending_value().feature.sides[0].draft_angle_degrees!=feature.feature.sides[0].draft_angle_degrees||
           dialog.pending_value().feature.sides[1].draft_angle_degrees!=feature.feature.sides[1].draft_angle_degrees)
            throw std::runtime_error("Switching side operations changed stored draft angles");
        auto* first_draft=dialog.findChild<QDoubleSpinBox*>("featureSideDraft0");
        auto* second_draft=dialog.findChild<QDoubleSpinBox*>("featureSideDraft1");
        if(!first_draft||!second_draft)throw std::runtime_error("Missing Feature draft fields");
        first_draft->setValue(-7.5);second_draft->setValue(2.25);
        for(int pass=0;pass<3;++pass)for(int i=0;i<5;++i) {
            type->setCurrentIndex(i);flush();
            const auto* point_toggle=dialog.findChild<QCheckBox*>("featureShowPoint");
            const auto* text_toggle=dialog.findChild<QCheckBox*>("featureShowText");
            if(type->geometry().right()>=point_toggle->geometry().left()||
                point_toggle->geometry().right()>=text_toggle->geometry().left())
                throw std::runtime_error("Feature type and visibility switches overlap");
            if(pass==0&&i==4)dialog.grab().save(QDir::tempPath()+"/zima-feature-visibility-properties.png");
            const auto pending=dialog.pending_value();
            if(pending.feature.sides[0].draft_angle_degrees!=-7.5||pending.feature.sides[1].draft_angle_degrees!=2.25)
                throw std::runtime_error("Type switch lost independently authored draft angles");
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
            for(const auto* name:{"featureProfilePlane","featureProfileOffset","featureSideValue0","featureSideValue1","featureThinSide","featureSideDraft0","featureSideDraft1"}) {
                auto* field=dialog.findChild<QWidget*>(name);
                const bool draft_field=QString(name).startsWith("featureSideDraft");
                const bool shown=draft_field?i==4&&(QString(name).endsWith('0')?first:second)->currentIndex()==1:
                    QString(name)=="featureThinSide"?i==4:QString(name).startsWith("featureProfile")?(i!=0&&i!=1):(i==1||i==4);
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
        type->setCurrentIndex(1);flush();
        const auto check_axis=[&](bool ok){if(!ok)throw std::runtime_error("Ordinary Feature Axis end controls are inconsistent");};
        check_axis(first_end->isVisible()&&second_end->isVisible()&&
            !dialog.findChild<QGroupBox*>("featurePlaneGroup")->isVisible());
        for(auto* ending:{first_end,second_end}) {
            ending->setCurrentIndex(1);flush();
            const std::size_t side=ending==first_end?0:1;
            check_axis(dialog.pending_value().feature.sides[side].extrusion_extent==zima::document::EndCondition::UpTo);
            ending->setCurrentIndex(2);flush();
            check_axis(!dialog.pending_value().feature.sides[side].axis_enabled);
            ending->setCurrentIndex(0);flush();
            check_axis(dialog.pending_value().feature.sides[side].length>0);
        }
        name->setText("My reference");type->setCurrentIndex(1);flush();
        if(dialog.pending_value().name!="My reference"||!dialog.pending_value().feature.automatic_name.empty())
            throw std::runtime_error("Type switch overwrote a custom name");
        part.history.push_back(feature);part.history.back().name=zima::app::feature_name_prefix(zima::document::FeatureType::Axis).toStdString()+" 001";
        if(zima::app::next_feature_name(part,zima::document::FeatureType::Axis,"")!=
            zima::app::feature_name_prefix(zima::document::FeatureType::Axis).toStdString()+" 002")
            throw std::runtime_error("Automatic name collides with an existing object");
    dialog.hide();verify_ordered_limits(application,parent);verify_limit_localization(application,parent);verify_rendering(application,parent);
    }catch(const std::exception& e){std::cerr<<e.what()<<std::endl;return 1;}
    return 0;
}
