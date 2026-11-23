#include "../app/cam_machine_dialog.hpp"
#include "../app/application_settings.hpp"
#include <QApplication>
#include <QDialogButtonBox>
#include <QFontDatabase>
#include <QMouseEvent>
#include <QPushButton>
#include <QSettings>
#include <QTemporaryDir>
#include <filesystem>
#include <iostream>

using namespace zima;
namespace {
int checks{};
void check(bool value,const char* message){++checks;if(!value)throw std::runtime_error(message);}
template<class T> T* field(QObject* object,const char* name) {
    auto* result=object->findChild<T*>(name);check(result!=nullptr,"Missing CAM machine field");return result;
}
void flush(QApplication& application){for(int i=0;i<5;++i)application.processEvents();}
}
int main(int argc,char** argv) {
    QApplication application(argc,argv);
    try {
#if defined(_WIN32)
        if(QGuiApplication::platformName()=="offscreen") {
            const auto font=QFontDatabase::addApplicationFont(qEnvironmentVariable("WINDIR")+"/Fonts/segoeui.ttf");
            check(font>=0,"Cannot load GUI test font");
            application.setFont(QFont(QFontDatabase::applicationFontFamilies(font).front(),9));
        }
#endif
        QWidget owner;owner.resize(1200,900);owner.show();
        QWidget view(&owner);view.setGeometry(owner.rect());view.show();
        cam::Machine initial{"machine","Authored machine"};initial.minimum={-123.123456789,-20.000000123,-350};
        initial.maximum_feed_mm_min=1000.123456789;initial.postprocessor_id="authored-postprocessor";
        int commits=0;auto committed=initial;
        auto make=[&](auto value) {
            return new app::CamMachineDialog(value,[&](auto next){++commits;committed=std::move(next);},&owner);
        };
        auto* dialog=make(initial);dialog->show();flush(application);
        check(dialog->parentWidget()==&owner&&(dialog->windowFlags()&Qt::SubWindow),"Machine dialog must be internal");
        check(!dialog->buttons()->button(QDialogButtonBox::Apply),"Machine dialog exposes Apply");
        check(dialog->pending_value()==initial,"Opening rounded machine settings");
        auto* table=field<QTableWidget>(dialog,"camMachineTravel");
        const auto top=table->mapTo(dialog,QPoint{}).y();const auto height=table->height();
        const auto feed_width=field<QDoubleSpinBox>(dialog,"camMaximumFeed")->width();
        dialog->resize(750,780);flush(application);
        check(table->mapTo(dialog,QPoint{}).y()==top&&table->height()==height,"Resize stretched the fixed travel rows");
        check(owner.rect().contains(dialog->geometry()),"Machine dialog escaped its owner");
        check(table->width()-table->horizontalHeader()->length()<=6,"Machine travel table has a redundant empty viewport");
        check(field<QDoubleSpinBox>(dialog,"camMaximumFeed")->width()==feed_width,"Resize stretched a compact machine speed field");
        dialog->buttons()->button(QDialogButtonBox::Ok)->click();flush(application);
        check(commits==1&&committed==initial,"Unchanged OK rounded or altered the machine");

        dialog=make(committed);dialog->show();flush(application);
        field<QLineEdit>(dialog,"camMachineName")->setText(" ");
        dialog->buttons()->button(QDialogButtonBox::Ok)->click();
        check(commits==1&&dialog->isVisible()&&!field<QLabel>(dialog,"camMachineStatus")->text().isEmpty(),"Empty name was accepted");
        field<QLineEdit>(dialog,"camMachineName")->setText("Edited machine");
        field<QDoubleSpinBox>(dialog,"camMinimumX")->setValue(600);
        dialog->buttons()->button(QDialogButtonBox::Ok)->click();
        check(commits==1&&dialog->isVisible(),"Reversed machine travel was accepted");
        field<QDoubleSpinBox>(dialog,"camMaximumX")->setValue(600);
        dialog->buttons()->button(QDialogButtonBox::Ok)->click();
        check(commits==1&&dialog->isVisible(),"Zero machine travel was accepted");
        field<QDoubleSpinBox>(dialog,"camMinimumX")->setValue(-600);
        field<QDoubleSpinBox>(dialog,"camMaximumFeed")->setValue(1500);
        field<QDoubleSpinBox>(dialog,"camRapidFeed")->setValue(20000);
        field<QDoubleSpinBox>(dialog,"camMaximumSpindle")->setValue(18000);
        QMouseEvent single(QEvent::MouseButtonPress,QPointF(10,10),QPointF(10,10),Qt::MiddleButton,Qt::MiddleButton,Qt::NoModifier);
        QApplication::sendEvent(&view,&single);check(commits==1,"Short middle click committed machine settings");
        QMouseEvent confirm(QEvent::MouseButtonDblClick,QPointF(10,10),QPointF(10,10),Qt::MiddleButton,Qt::MiddleButton,Qt::NoModifier);
        QApplication::sendEvent(&view,&confirm);flush(application);
        check(commits==2&&committed.name=="Edited machine"&&committed.minimum.x==-600&&committed.maximum.x==600&&
            committed.maximum_feed_mm_min==1500&&committed.rapid_mm_min==20000&&committed.maximum_spindle_rpm==18000,
            "Middle double click did not commit edited limits");
        check(committed.id==initial.id&&committed.minimum.y==initial.minimum.y&&committed.postprocessor_id==initial.postprocessor_id,
            "Machine edit changed unrelated authored settings");
        dialog=make(committed);dialog->show();flush(application);
        check(dialog->pending_value()==committed,"Reopening changed machine settings");
        field<QComboBox>(dialog,"camMachineKind")->setCurrentIndex(1);
        field<QSpinBox>(dialog,"camMachineAxes")->setValue(2);
        check(dialog->pending_value().kind==cam::MachineKind::Turning&&dialog->pending_value().axes==2,"Machine capabilities cannot be authored");
        dialog->buttons()->button(QDialogButtonBox::Cancel)->click();flush(application);
        check(commits==2&&committed.kind==cam::MachineKind::Milling&&committed.axes==3,"Cancel committed machine capabilities");

        // Change language and appearance while the same window remains open.
        QTemporaryDir temporary;check(temporary.isValid(),"Missing temporary settings directory");
        const auto localization=std::filesystem::path(__FILE__).parent_path().parent_path().parent_path()/"config/localization";
        dialog=make(committed);dialog->show();flush(application);
        field<QDoubleSpinBox>(dialog,"camMinimumX")->setValue(700);
        dialog->buttons()->button(QDialogButtonBox::Ok)->click();
        for(const auto* language:{"cs","en","de","fr","ru"}) {
            QSettings config(temporary.filePath("config.ini"),QSettings::IniFormat);
            config.setValue("Application/Language",language);
            config.setValue("Paths/Localization",QString::fromStdString(localization.generic_string()));config.sync();
            auto settings=app::ApplicationSettings::load(temporary.path());
            app::apply_application_translations(application,settings);flush(application);
            const auto translated=[&](const char* key){const auto value=settings.qt_translations.value(QString::fromUtf8(key));check(!value.isEmpty(),"Missing machine translation");return value;};
            for(const auto* theme:{"light","dark","light"}) {
                settings.theme=theme;app::apply_application_appearance(application,settings);flush(application);
                check(dialog->windowTitle()==translated("CAM machine properties"),"Machine title did not follow language");
                check(field<QComboBox>(dialog,"camMachineKind")->itemText(0)==translated("Milling")&&
                    field<QComboBox>(dialog,"camMachineKind")->itemText(1)==translated("Turning"),"Machine kinds not translated");
                check(field<QLabel>(dialog,"camMachineCapability")->text()==translated("Toolpath generation currently supports 3-axis milling."),"Capability note not translated");
                check(field<QLabel>(dialog,"camMachineStatus")->text()==translated("Each axis minimum must be smaller than its maximum."),"Existing error not retranslated");
                check(field<QTableWidget>(dialog,"camMachineTravel")->horizontalHeaderItem(1)->text()==translated("Minimum travel"),"Travel headings not translated");
                for(const auto* key:{"Machining type","Axis count","Maximum cutting feed","Rapid feed","Maximum spindle speed"}) {
                    bool found=false;const auto expected=translated(key);
                    for(const auto* label:dialog->findChildren<QLabel*>())found=found||label->text()==expected;
                    check(found,"Machine parameter label did not follow language");
                }
                check(field<QLineEdit>(dialog,"camMachineName")->text()=="Edited machine","Translation changed authored name");
                check(dialog->palette().color(QPalette::Text)!=dialog->palette().color(QPalette::Base),"Machine text is invisible");
                const auto capture=qEnvironmentVariable("ZIMA_CAM_MACHINE_CAPTURE");
                if(!capture.isEmpty())check(dialog->grab().save(capture+"-"+language+"-"+theme+".png"),"Cannot capture machine dialog");
            }
        }
        dialog->reject();flush(application);check(commits==2,"Language/theme inspection committed settings");
        std::cout<<"CAM machine dialog: "<<checks<<" checks passed\n";return 0;
    }catch(const std::exception& error){std::cerr<<error.what()<<'\n';return 1;}
}
