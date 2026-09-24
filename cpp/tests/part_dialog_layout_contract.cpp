#include "application_settings.hpp"
#include "primitive_properties_dialog.hpp"
#include "construction_properties_dialog.hpp"
#include "sketch_properties_dialog.hpp"
#include "sweep2d_dialog.hpp"
#include "helical_sweep_dialog.hpp"
#include "shaft_thread_dialog.hpp"
#include "body_properties_dialog.hpp"
#include "section_properties_dialog.hpp"
#include "derived_copy_dialog.hpp"
#include "sheet_state_dialog.hpp"
#include <QApplication>
#include <QSettings>
#include <QTemporaryDir>
#include <QScrollArea>
#include <QDialogButtonBox>
#include <QDir>
#include <filesystem>
#include <iostream>
#include <memory>

int verify_part_dialog_layout(QApplication& application, QWidget& parent) {
    using namespace zima;
    const auto root=std::filesystem::path(__FILE__).parent_path().parent_path().parent_path();
    QTemporaryDir temporary;
    const auto flush=[&]{for(int i=0;i<12;++i)application.processEvents();};
    int failures=0, count=0;
    for(const QString language:{"cs","en","de","fr","ru"}) {
        QSettings config(temporary.filePath("config.ini"),QSettings::IniFormat);
        config.setValue("Application/Language",language);
        config.setValue("Paths/Localization",QString::fromStdString((root/"config/localization").generic_string()));config.sync();
        auto settings=app::ApplicationSettings::load(temporary.path());
        app::apply_application_translations(application,settings);app::apply_application_font(application,settings);
        for(const QSize available:{QSize(1366,768),QSize(1920,1080)}) {
            parent.resize(available);flush();
            const auto check=[&](ui::PropertiesSubWindow* raw,const QString& name) {
                std::unique_ptr<ui::PropertiesSubWindow> dialog(raw);dialog->setAttribute(Qt::WA_DeleteOnClose,false);
                dialog->show();flush();++count;
                QStringList errors;
                std::vector<std::pair<QWidget*,int>> anchored;
                for(auto* field:dialog->findChildren<QTableWidget*>())if(field->isVisible())
                    anchored.emplace_back(field,field->mapTo(dialog.get(),QPoint{}).y());
                const auto original_size=dialog->size();
                std::vector<std::pair<QAbstractItemView*,int>> expanding;
                if(dialog->property("expandBottomTable").toBool())
                    for(int i=0;i<dialog->content_layout()->count();++i)
                        if(auto* view=qobject_cast<QAbstractItemView*>(dialog->content_layout()->itemAt(i)->widget());view&&view->isVisible())
                            expanding.emplace_back(view,view->height());
                dialog->resize(dialog->width(),std::min(available.height(),dialog->height()+100));flush();
                for(const auto& [field,y]:anchored)if(field->mapTo(dialog.get(),QPoint{}).y()!=y)
                    errors<<field->objectName()+": fields moved vertically on resize";
                const auto growth=dialog->height()-original_size.height();
                if(growth>10&&!expanding.empty()) {
                    int gained=0;for(const auto& [view,height]:expanding)gained+=view->height()-height;
                    if(gained<growth/2)errors<<"extra window height did not reach the reference table";
                }
                dialog->resize(original_size);flush();
                auto* status=dialog->findChild<QLabel*>("containerPlacementStatusLabel");
                auto* dof=dialog->findChild<QLabel*>("containerPlacementDofLabel");
                if(status&&dof&&status->isVisible()&&dof->isVisible()&&
                    std::abs(status->mapTo(dialog.get(),status->rect().center()).y()-dof->mapTo(dialog.get(),dof->rect().center()).y())>1)
                    errors<<"placement status is not beside degrees of freedom";
                if(!parent.rect().contains(dialog->geometry()))errors<<"window exceeds parent";
                for(auto* widget:dialog->findChildren<QWidget*>()) {
                    if(!widget->isVisible() || !widget->parentWidget())continue;
                    const bool editor=qobject_cast<QAbstractSpinBox*>(widget)||qobject_cast<QLineEdit*>(widget)||
                        qobject_cast<QComboBox*>(widget)||qobject_cast<QAbstractButton*>(widget);
                    auto* owner=widget->parentWidget();
                    const bool viewport=owner->parentWidget()&&qobject_cast<QAbstractScrollArea*>(owner->parentWidget());
                    if(editor&&!viewport&&!owner->rect().contains(widget->geometry()))
                        errors<<widget->objectName()+": editor exceeds its container";
                }
                for(auto* table:dialog->findChildren<QTableWidget*>())if(table->isVisible())
                    for(int row=0;row<table->rowCount();++row)for(int col=0;col<table->columnCount();++col)
                        if(auto* widget=table->cellWidget(row,col);widget&&widget->isVisible()) {
                            const auto cell=table->visualRect(table->model()->index(row,col));
                            if(widget->width()>cell.width()+1||widget->height()>cell.height()+1)
                                errors<<table->objectName()+": cell overflow";
                        }
                if(!dialog->rect().contains(QRect(dialog->buttons()->mapTo(dialog.get(),QPoint()),dialog->buttons()->size())))
                    errors<<"confirmation buttons outside window";
                errors.removeDuplicates();
                if(!errors.empty()) {
                    ++failures;std::cerr<<language.toStdString()<<" "<<available.width()<<" "<<name.toStdString()<<": "<<errors.join(", ").toStdString()<<'\n';
                }
                if(!errors.empty()||name=="sweep2d"||name=="shaft-thread"||name=="fillet"||name=="chamfer"||name=="shell"||dialog->findChild<QTableWidget*>("curve3DPoints")) {
                    const auto output=root/"build/dialog-layout";std::filesystem::create_directories(output);
                    dialog->grab().save(QString::fromStdString(output.generic_string())+"/"+language+"-"+name+"-"+QString::number(available.width())+".png");
                }
                dialog->hide();
            };
            using Part=document::PartDocument;
            const std::array factories{&Part::create_box_container,&Part::create_cylinder_container,&Part::create_sphere_container,
                &Part::create_cone_container,&Part::create_pyramid_container,&Part::create_wedge_container,
                &Part::create_hole_container,&Part::create_thread_container,&Part::create_drill_point_container,&Part::create_twisted_sheet_container};
            for(const auto factory:factories) {
                auto feature=factory();check(new app::PrimitivePropertiesDialog(feature,false,true,[](auto){},&parent),QString::number(static_cast<int>(feature.feature_kind)));
            }
            for(bool revolve:{false,true}) {
                auto feature=revolve?Part::create_revolution_container("profile"):Part::create_extrusion_container("profile");
                check(new app::PrimitivePropertiesDialog(feature,false,true,[](auto){},&parent),revolve?"revolution":"extrusion");
            }
            check(new app::PrimitivePropertiesDialog(Part::create_shell_container(),false,true,[](auto){},&parent),"shell");
            for(auto kind:{document::FeatureKind::Fillet,document::FeatureKind::Chamfer}) {
                auto feature=Part::create_box_container();feature.feature_kind=kind;
                check(new app::PrimitivePropertiesDialog(feature,false,true,[](auto){},&parent),kind==document::FeatureKind::Fillet?"fillet":"chamfer");
            }
            for(auto kind:{document::ConstructionKind::Point,document::ConstructionKind::Axis,document::ConstructionKind::Plane,document::ConstructionKind::Curve3D}) {
                auto feature=Part::create_construction(kind);
                if(kind==document::ConstructionKind::Curve3D) {
                    feature.curve_type=document::Curve3DType::Polyline;
                    for(int i=0;i<3;++i) {
                        auto point=Part::create_construction(document::ConstructionKind::Point);
                        point.origin={double(i)*10,0,0};point.curve_tangent_enabled=true;
                        point.curve_tangent=document::Curve3DTangentMode::PositiveX;
                        feature.curve_points.push_back(point);
                    }
                }
                check(new app::ConstructionPropertiesDialog(feature,false,[](auto){},&parent),"construction"+QString::number(static_cast<int>(kind)));
            }
            check(new app::ConstructionPropertiesDialog(Part::create_sweep3d_container(),false,true,[](auto){},&parent),"sweep3d");
            auto* sweep=new app::Sweep2DDialog(Part::create_sweep2d_container(),[](auto){},&parent);
            auto path=sketcher::Sketch::from_serialized(sweep->pending.sweep2d.path_sketch);
            auto invalid_path=path;static_cast<void>(invalid_path.add_segment(10,0,40,0));
            sweep->set_sketch(0,invalid_path);sweep->set_status("preview status");
            const auto error_prefix=settings.qt_translations.value("Dráha není platná: %1").section("%1",0,0);
            bool explained=false;for(auto* label:sweep->findChildren<QLabel*>())
                if(!error_prefix.isEmpty()&&label->text().startsWith(error_prefix))explained=true;
            if(!explained){++failures;std::cerr<<"Invalid Sweep path did not retain its localized error\n";}
            static_cast<void>(path.add_segment(0,0,40,0));sweep->set_sketch(0,path);
            for(auto* label:sweep->findChildren<QLabel*>())if(!error_prefix.isEmpty()&&label->text().startsWith(error_prefix)) {
                ++failures;std::cerr<<"Valid Sweep path retained its previous error\n";
            }
            check(sweep,"sweep2d");
            check(new app::HelicalSweepDialog(Part::create_helical_sweep_container(),[](auto){},&parent),"helix");
            check(new app::ShaftThreadDialog(Part::create_shaft_thread_container(),[](auto){},&parent),"shaft-thread");
            check(new app::SketchPropertiesDialog(sketcher::Sketch::create_default(),{},false,{},[](auto,auto,auto){},&parent),"sketch");
            check(new app::BodyPropertiesDialog({},true,[](auto,auto){},&parent),"body");
            check(new app::BodyBooleanPropertiesDialog({},{{"first","First"},{"second","Second"}},[](auto){},&parent),"boolean");
            auto revolved_sheet=Part::create_revolution_container("profile");revolved_sheet.revolution.sheet_metal=true;
            check(new app::PrimitivePropertiesDialog(revolved_sheet,false,true,[](auto){},&parent),"revolved-sheet");
            auto section=document::create_section();
            check(new app::SectionPropertiesDialog(&parent,section,[](auto){return true;},[]{}),"section");
            for(int mode=0;mode<3;++mode) {
                const bool pattern=mode>0;
                document::DerivedCopyParameters parameters;
                if(pattern){parameters.pattern.emplace();parameters.pattern->circular=mode==2;}
                check(new app::DerivedCopyDialog(Part::create_box_container(),parameters,[](auto,auto){},&parent),mode==0?"mirror":mode==1?"linear-pattern":"circular-pattern");
            }
            for(int mode=0;mode<3;++mode) {
                std::set<std::string> locks;
                auto* dialog=new app::SketchPropertiesDialog(sketcher::Sketch::create_default(),{},false,{},[](auto,auto,auto){},&parent);
                if(mode==0)dialog->set_holes_mode(5,locks,[](double){},[]{});
                if(mode==1)dialog->set_bend_mode({}, {},locks,[](auto){},[]{});
                if(mode==2)dialog->set_flat_mode({}, {},locks,[](auto){},[]{});
                check(dialog,mode==0?"holes":mode==1?"bend":"flat");
            }
            for(bool unbend:{false,true}) {
                auto feature=Part::create_sketch_container();
                feature.feature_kind=unbend?document::FeatureKind::Unbend:document::FeatureKind::BendBack;
                check(new app::SheetStateDialog(feature,{},[](auto){},&parent),unbend?"unbend":"bend-back");
            }
        }
    }
    std::cout<<count<<" Part dialog/language/window-size combinations; failures="<<failures<<'\n';
    return failures?1:0;
}
