#include "primitive_properties_dialog.hpp"
#include "mech_keyway_parameter_panel.hpp"
#include "mech_keyway_view_cues.hpp"
#include "application_settings.hpp"
#include <QApplication>
#include <QDialogButtonBox>
#include <QPushButton>
#include <QTemporaryDir>
#include <QSettings>
#include <QMouseEvent>
#include <QDir>
#include <QScrollArea>
#include <QScrollBar>
#include <iostream>
#include <stdexcept>

namespace {
void check(bool yes,const char* message){if(!yes)throw std::runtime_error(message);}
}
int verify_mech_keyway_ui(QApplication& application,QWidget& parent){
    using namespace zima;
    parent.resize(1300,1150);parent.show();application.processEvents();
    auto owner=workspace::create_mech_keyway();owner.mech_keyway.table_id="din6885-1-maedler";
    owner.mech_keyway.length=45.123456789;
    int commits=0;
    {
        app::PrimitivePropertiesDialog dialog(owner,true,true,[&](auto){++commits;},&parent);
        dialog.show();application.processEvents();
        check(dialog.windowFlags().testFlag(Qt::SubWindow)&&dialog.parentWidget()==&parent,"MECH does not use the shared internal properties window");
        check(!dialog.buttons()->button(QDialogButtonBox::Apply),"MECH exposes an Apply transaction");
        check(dialog.pending_value()==owner,"Opening MECH rounded or rewrote untouched parameters");
        check(!dialog.findChild<QDoubleSpinBox*>("mechKeyWidth"),"Catalogue width became independently editable");
        check(!dialog.findChild<QWidget*>("edgeTreatmentEdges"),"MECH exposes unrelated edge-treatment controls");
        check(dialog.mech_keyway_panel()!=nullptr&&app::uses_container_placement(document::FeatureKind::MechKeyway),"MECH bypasses shared placement");
        dialog.buttons()->button(QDialogButtonBox::Ok)->click();application.processEvents();
        check(commits==0,"Unchanged MECH OK committed a transaction");
    }
    {
        app::PrimitivePropertiesDialog dialog(owner,true,true,[&](auto value){++commits;check(value.mech_keyway.length==63,"MECH lost the edited length");},&parent);
        dialog.show();application.processEvents();
        check(dialog.set_inline_parameter_value("mech_length",63),"MECH View length editor is disconnected");
        check(!dialog.set_inline_parameter_value("mech_width",12),"MECH View permits overriding catalogue width");
        dialog.buttons()->button(QDialogButtonBox::Ok)->click();application.processEvents();check(commits==1,"MECH changed OK did not commit once");
    }
    {
        const auto root=std::filesystem::path(__FILE__).parent_path().parent_path().parent_path();
        app::PrimitivePropertiesDialog dialog(owner,true,true,[&](auto){++commits;},&parent);
        auto* panel=dialog.mech_keyway_panel();
        panel->initialize_roughness({symbols::Definition::load(root/"config/symbols/surface-texture/ZE-SURFACE-TEXTURE-ISO21920.symz")},{});
        dialog.show();application.processEvents();application.processEvents();
        auto* toggle=panel->findChild<QCheckBox*>("mechKeyRoughness");
        auto* scroll=dialog.findChild<QScrollArea*>("propertiesContentScrollArea");
        const auto original_size=dialog.size();
        dialog.move(dialog.x(),parent.height()-dialog.height());
        toggle->click();application.processEvents();application.processEvents();
        check(dialog.height()>original_size.height()&&dialog.width()==original_size.width(),"Enabling MECH roughness did not grow only the properties height");
        check(parent.rect().contains(dialog.geometry()),"Expanded MECH properties escaped the parent window");
        check(scroll&&scroll->verticalScrollBar()->maximum()==0,"MECH roughness fields need scrolling despite sufficient parent height");
        auto* floor=panel->findChild<QPushButton*>("mechKeyFloorTexture");
        check(floor->isVisible()&&scroll->viewport()->rect().contains(QRect(floor->mapTo(scroll->viewport(),QPoint{}),floor->size())),"Expanded MECH properties clipped the floor texture editor");
        const auto expanded=dialog.size();
        toggle->click();application.processEvents();toggle->click();application.processEvents();application.processEvents();
        check(dialog.size()==expanded,"Repeated MECH roughness toggles kept growing the window");
        parent.resize(1300,700);application.processEvents();dialog.resize(dialog.width(),700);
        toggle->click();application.processEvents();toggle->click();application.processEvents();application.processEvents();
        check(parent.rect().contains(dialog.geometry())&&scroll->verticalScrollBar()->maximum()>0,"MECH roughness did not retain bounded scrolling in a small parent");
        check(dialog.rect().contains(QRect(dialog.buttons()->mapTo(&dialog,QPoint{}),dialog.buttons()->size())),"MECH roughness hid OK/Cancel in a small parent");
        dialog.buttons()->button(QDialogButtonBox::Cancel)->click();application.processEvents();
        check(commits==1,"MECH roughness resize or Cancel committed pending changes");
        parent.resize(1300,1150);application.processEvents();
    }
    {
        app::PrimitivePropertiesDialog dialog(owner,true,true,[&](auto){++commits;},&parent);
        dialog.show();application.processEvents();auto* panel=dialog.mech_keyway_panel();
        kernel::SurfaceGeometry cone;cone.kind=kernel::SurfaceGeometry::Kind::Cone;cone.radius=17.5;cone.semi_angle=.1;
        panel->set_support({"shaft","cone",{}},cone);
        check(panel->parameters().type==document::KeywayType::Woodruff&&!panel->findChild<QComboBox*>("mechKeyType")->isEnabled(),"A cone shaft offers a parallel key");
        check(!dialog.set_inline_parameter_value("mech_length",50),"A Woodruff disc accepts an independent length");
        auto selected=panel->parameters();document::select_keyway_size(selected,20);
        panel->update_derived(selected,true,20);
        auto* sizes=panel->findChild<QComboBox*>("mechKeySize");
        check(sizes&&sizes->count()==3&&sizes->isEnabled()&&!sizes->isEditable(),"Woodruff catalogue alternatives are unavailable or allow arbitrary values");
        sizes->setCurrentIndex(1);selected=panel->parameters();document::select_keyway_size(selected,20);
        check(selected.woodruff_diameter==22&&selected.width==6,"Woodruff row selection did not select its authored catalogue size");
        panel->update_derived(selected,true,20);
        check(sizes->currentData().toString()=="6x9x22","Refreshing the preview lost the selected Woodruff row");
        document::select_keyway_size(selected,25);panel->update_derived(selected,true,25);
        check(sizes->count()==2&&selected.width==8,"A source diameter change retained an inapplicable key size");
        cone.reversed=true;panel->set_support({"hub","cone",{}},cone);application.processEvents();
        check(panel->parameters().position==document::KeywayPosition::TwoSided,"A cone hub lacks two independent sides");
        auto* refs=panel->findChild<QTableWidget*>("mechKeywayReferences");
        check(!refs->isRowHidden(1)&&!refs->isRowHidden(2),"Hub does not always expose both end reference rows");
        check(panel->parameters().limits[0].termination==document::KeywayTermination::UpTo&&
            panel->parameters().limits[1].termination==document::KeywayTermination::UpTo,"Hub defaults to an authored blind length");
        check(!panel->findChild<QComboBox*>("mechKeyPosition")->isVisible()&&
            !panel->findChild<QComboBox*>("mechKeyLimit0")->isVisible()&&
            !panel->findChild<QComboBox*>("mechKeyLimit1")->isVisible(),"Hub exposes an alternate blind termination");
        check(!dialog.set_inline_parameter_value("mech_length",12)&&!dialog.set_inline_parameter_value("mech_forward_length",12)&&
            !dialog.set_inline_parameter_value("mech_reverse_length",12),"Hub exposes an authored length");
        check(dialog.set_inline_parameter_value("mech_forward_correction",2),"MECH Up To correction is not editable");
        panel->begin_entry(1);panel->set_limit(0,{"hub","front",{}});
        panel->begin_entry(0);panel->finish_entry();check(panel->parameters().limits[0].reference.valid(),"Ending entry deleted its target");
        check(!dialog.set_inline_parameter_value("mech_reverse_length",14)&&dialog.set_inline_parameter_value("mech_reverse_correction",1),"Reverse Up To is not independent");
        check(!dialog.set_inline_parameter_value("mech_reverse_correction",-1),"Hub permits a blind inward end correction");
        panel->begin_entry(2);panel->set_limit(1,{"hub","back",{}});
        check(panel->parameters().limits[0].reference.semantic_key=="front"&&panel->parameters().limits[1].reference.semantic_key=="back","Hub end replacement changed the other end");
        cone.kind=kernel::SurfaceGeometry::Kind::Cylinder;panel->set_support({"hub","cylinder",{}},cone);application.processEvents();
        check(!refs->isRowHidden(1)&&!refs->isRowHidden(2),"Cylindrical hub hides a required end row");
        cone.reversed=false;panel->set_support({"shaft","cylinder",{}},cone);application.processEvents();
        check(refs->isRowHidden(1)&&refs->isRowHidden(2),"Shaft exposes hub end reference rows");
        dialog.resize(dialog.width()+120,dialog.height()+90);application.processEvents();
        dialog.buttons()->button(QDialogButtonBox::Cancel)->click();application.processEvents();check(commits==1,"Cancel committed MECH changes");
    }
    QTemporaryDir config;
    check(config.isValid(),"Cannot create isolated MECH language config");
    const auto catalogs=std::filesystem::path(__FILE__).parent_path().parent_path().parent_path()/"config/localization";
    app::PrimitivePropertiesDialog translated(owner,true,true,[&](auto){++commits;},&parent);
    translated.show();
    translated.mech_keyway_panel()->set_draft_error("No keyway size is available for this support diameter");
    for(const auto language:{"cs","en","de","fr","ru"}){
        QSettings settings(config.filePath("config.ini"),QSettings::IniFormat);
        settings.setValue("Application/Language",language);settings.setValue("Paths/Localization",QString::fromStdString(catalogs.generic_string()));settings.sync();
        auto loaded=app::ApplicationSettings::load(config.path());app::apply_application_translations(application,loaded);application.processEvents();
        check(translated.findChild<QComboBox*>("mechKeyType")->itemText(1)==loaded.qt_translations.value("Woodruff key"),"MECH type did not follow the language change");
        // New property windows take their feature-specific title from the active catalogue.
        app::PrimitivePropertiesDialog titled(owner,true,true,[](auto){},&parent);
        check(titled.windowTitle()==loaded.qt_translations.value("Keyway properties"),"MECH properties title did not identify the keyway in the selected language");
        check(translated.findChild<QComboBox*>("mechKeyPosition")->itemText(2)==loaded.qt_translations.value("Two directions"),"MECH axial choices are untranslated");
        auto* catalogue=translated.findChild<QComboBox*>("mechKeyCatalogue");
        check(catalogue->itemText(catalogue->findData("zima-csn-parallel"))==loaded.qt_translations.value("CSN 02 2507 / 02 2562 (historical table)"),"CSN catalogue standard is missing or untranslated");
        auto* preview_error=translated.findChild<QLabel*>("mechKeyPreviewError");
        check(preview_error&&preview_error->isVisible()&&preview_error->text()==loaded.qt_translations.value("No keyway size is available for this support diameter"),"Draft failure did not follow the language change");
        check(translated.findChild<QComboBox*>("mechKeySize")->count()==0,"Failed draft displays stale catalogue dimensions");
        for(const auto theme:{"light","dark","light"}){loaded.theme=theme;app::apply_application_appearance(application,loaded);application.processEvents();}
    }
    translated.buttons()->button(QDialogButtonBox::Cancel)->click();
    check(commits==1,"Language/theme change committed MECH data");
    std::cout<<"MECH dialog: shared placement, unchanged/changed OK, Cancel, shaft/hub modes, independent limits, View field routing, five languages and live themes passed\n";
    return 0;
}
