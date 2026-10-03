#include "../app/solid_state_dialog.hpp"
#include "../app/application_settings.hpp"
#include <QApplication>
#include <QDialogButtonBox>
#include <QMouseEvent>
#include <QJsonObject>
#include <QLabel>
#include <QPixmap>
#include <QFontDatabase>
#include <QSettings>
#include <QTemporaryDir>
#include <filesystem>
#include <iostream>
using namespace zima;
void check(bool value,const char* message){if(!value)throw std::runtime_error(message);}
void translations(QApplication& application,QWidget& parent) {
    QTemporaryDir temporary;check(temporary.isValid(),"Cannot create translation settings");
    for(const auto* language:{"cs","en","de","fr","ru"}) {
        const auto path=std::filesystem::path(__FILE__).parent_path().parent_path().parent_path()/"config/localization";
        QSettings config(temporary.filePath("config.ini"),QSettings::IniFormat);
        config.setValue("Application/Language",language);
        config.setValue("Paths/Localization",QString::fromStdString(path.generic_string()));config.sync();
        const auto settings=app::ApplicationSettings::load(temporary.path());
        QJsonObject catalog;for(auto it=settings.qt_translations.begin();it!=settings.qt_translations.end();++it)catalog[it.key()]=it.value();
        app::apply_application_translations(application,settings);
        application.processEvents();
        for(bool restore:{false,true}) {
            auto* dialog=new app::SolidStateDialog({"Test",restore,false,1,{}},{{"a","A"}},[](auto){},&parent);
            dialog->show();application.processEvents();
            check(dialog->windowTitle()==catalog[restore?"Restore shape":"Straighten"].toString(),"Dialog title not localized");
            check(dialog->buttons()->button(QDialogButtonBox::Cancel)->text()==catalog[QString::fromUtf8("Zrušit")].toString(),"Cancel not localized");
            check(dialog->findChild<QCheckBox*>("solidStateAll")->text()==catalog["All eligible elements"].toString(),"Mode label not localized");
            const auto* factor=dialog->findChild<QDoubleSpinBox*>("solidStateCoefficient");
            check(factor->toolTip()==catalog["Straight length equals the section centroid path length multiplied by this coefficient. Cross-section dimensions stay unchanged."].toString(),"Coefficient help not localized");
            dialog->buttons()->button(QDialogButtonBox::Ok)->click();application.processEvents();
            bool error=false;for(const auto* label:dialog->findChildren<QLabel*>())
                error=error||label->text().contains(catalog["Select at least one eligible solid element."].toString());
            check(error&&dialog->isVisible(),"Validation error not localized");
            if(QString::fromLatin1(language)=="cs") {
                const auto capture=qEnvironmentVariable("ZIMA_SOLID_STATE_DIALOG_CAPTURE");
                if(!capture.isEmpty())check(dialog->grab().save(capture+(restore?"-restore.png":"-straighten.png")),"Cannot save dialog capture");
            }
            dialog->reject();application.processEvents();
        }
        const char* continuity="Straightening requires coincident profile centroids and tangent-continuous directions.";
        auto* invalid_join=new app::SolidStateDialog({"Test",true,true,1,{}},{{"a","A"}},
            [continuity](auto){throw std::invalid_argument(continuity);},&parent);
        invalid_join->show();application.processEvents();
        invalid_join->buttons()->button(QDialogButtonBox::Ok)->click();application.processEvents();
        bool localized=false;
        const auto expected=catalog[continuity].toString();check(!expected.isEmpty(),"Missing continuity translation");
        for(const auto* label:invalid_join->findChildren<QLabel*>())localized=localized||label->text().contains(expected);
        check(localized&&invalid_join->isVisible(),"Continuity error was not localized after changing language");
        invalid_join->reject();application.processEvents();
    }
}
int main(int argc,char** argv) {
    QApplication application(argc,argv);app::apply_application_appearance(application,{});
#if defined(_WIN32)
    // Qt's offscreen plugin does not enumerate Windows system fonts.
    if(QGuiApplication::platformName()=="offscreen") {
        const auto font=QFontDatabase::addApplicationFont(qEnvironmentVariable("WINDIR")+"/Fonts/segoeui.ttf");
        check(font>=0,"Cannot load the GUI test font");
        application.setFont(QFont(QFontDatabase::applicationFontFamilies(font).front(),9));
    }
#endif
    QWidget parent;parent.resize(1100,760);parent.show();
    QWidget view(&parent);view.setGeometry(0,0,1100,760);view.show();
    try {
        for(bool restore:{false,true}) {
            app::SolidStateEdit initial{"State",restore,true,.98765432115,{}};
            int commits=0;app::SolidStateEdit committed;
            const auto make=[&](auto value){return new app::SolidStateDialog(value,{{"a","A"},{"b","B"},{"c","C"}},
                [&](auto next){++commits;committed=std::move(next);},&parent);};
            auto* dialog=make(initial);dialog->show();application.processEvents();
            check(dialog->windowFlags().testFlag(Qt::SubWindow)&&dialog->parentWidget()==&parent,"Not an internal properties window");
            check(!dialog->buttons()->button(QDialogButtonBox::Apply),"Unexpected Apply transaction");
            auto* table=dialog->findChild<QTableWidget*>("solidStateElements");
            auto* factor=dialog->findChild<QDoubleSpinBox*>("solidStateCoefficient");
            auto* name=dialog->findChild<QLineEdit*>("solidStateName");
            check(factor->isVisible()!=restore,"Restore exposes a coefficient");
            check(dialog->pending_value()==initial,"Opening rounded or changed the definition");
            check(!dialog->selecting()&&dialog->selected().size()==3,"All mode lost eligible elements");
            table->cellWidget(0,3)->findChild<QToolButton*>()->click();
            dialog->findChild<QCheckBox*>("solidStateIndividual")->click();
            check(dialog->highlighted().empty(),"Mode change retained an unrelated inspection");
            check(dialog->selecting()&&table->rowCount()==1,"Individual input did not arm");
            dialog->set_source("a");dialog->set_source("b");dialog->set_source("b");dialog->set_source("unknown");
            check(dialog->selected()==std::vector<std::string>{"a","b"},"Invalid or duplicate source accepted");
            auto* eye=table->cellWidget(0,3)->findChild<QToolButton*>();eye->click();
            check(dialog->highlighted()==std::vector<std::string>{"a"}&&dialog->selecting(),"Inspection changed input ownership");
            table->cellClicked(1,2);dialog->set_source("c");
            check(dialog->selected()==std::vector<std::string>{"a","c"}&&dialog->highlighted()==std::vector<std::string>{"a"},"Replacement affected independent inspection");
            const int y=name->mapTo(dialog,QPoint{}).y(),height=table->height(),factor_width=factor->width();
            dialog->resize(700,600);application.processEvents();
            check(name->mapTo(dialog,QPoint{}).y()==y&&table->height()>height&&table->rowHeight(0)==34,"Resize changed row spacing or did not expand the table");
            if(!restore)check(factor->width()==factor_width,"Resize stretched the compact coefficient field");
            check(parent.rect().contains(dialog->geometry()),"Dialog escaped application bounds");
            table->cellWidget(0,1)->findChild<QPushButton*>()->click();
            check(dialog->selected()==std::vector<std::string>{"c"}&&dialog->highlighted().empty()&&dialog->selecting(),"Remove retained the row, inspection or disabled entry");
            dialog->end_entry();dialog->set_source("a");
            check(!dialog->selecting()&&dialog->selected()==std::vector<std::string>{"c"},"Ended entry accepted a pick");
            if(!restore)factor->setValue(.9);
            name->setText("Edited state");
            QMouseEvent short_click(QEvent::MouseButtonPress,QPointF(900,650),QPointF(900,650),Qt::MiddleButton,Qt::MiddleButton,Qt::NoModifier);
            QApplication::sendEvent(&view,&short_click);check(commits==0,"Single middle click committed");
            QMouseEvent confirm(QEvent::MouseButtonDblClick,QPointF(900,650),QPointF(900,650),Qt::MiddleButton,Qt::MiddleButton,Qt::NoModifier);
            QApplication::sendEvent(&view,&confirm);application.processEvents();
            check(commits==1&&committed.name=="Edited state"&&committed.owners==std::vector<std::string>{"c"},"View middle double click did not confirm pending data");
            check(committed.coefficient==(restore?initial.coefficient:.9),"Coefficient was lost or rounded");
            auto* edit=make(committed);edit->show();application.processEvents();
            check(edit->pending_value()==committed,"Reopening changed pending data");
            edit->findChild<QTableWidget*>("solidStateElements")->cellClicked(0,2);edit->set_source("a");
            edit->reject();application.processEvents();check(commits==1&&committed.owners==std::vector<std::string>{"c"},"Cancel committed a replacement");
            auto invalid=initial;invalid.all=false;invalid.owners={"missing"};
            auto* missing=make(invalid);missing->show();application.processEvents();
            missing->buttons()->button(QDialogButtonBox::Ok)->click();
            check(commits==1&&missing->isVisible(),"Missing source was committed");missing->reject();application.processEvents();
        }
        translations(application,parent);
        std::cout<<"Solid state dialog: shared presentation, entry, inspection, replacement, resize, confirmation and Cancel passed\n";
        return 0;
    }catch(const std::exception& error){std::cerr<<error.what()<<'\n';return 1;}
}
