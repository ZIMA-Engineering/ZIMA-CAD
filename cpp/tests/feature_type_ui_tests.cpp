#include "primitive_properties_dialog.hpp"
#include "application_settings.hpp"
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
int main(int argc,char** argv) {
    QApplication application(argc,argv);
    QWidget parent;parent.resize(1600,1000);parent.show();
    auto feature=zima::document::PartDocument::create_feature_container("fixture-sketch");
    zima::app::PrimitivePropertiesDialog dialog(feature,true,true,[](auto){},&parent);
    dialog.show();
    const auto flush=[&] {QElapsedTimer timer;timer.start();while(timer.elapsed()<50){application.processEvents();QThread::msleep(2);}};
    auto* type=dialog.findChild<QComboBox*>("featureType");
    try {
        for(int pass=0;pass<3;++pass)for(int i=0;i<5;++i) {
            type->setCurrentIndex(i);flush();
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
    }catch(const std::exception& e){std::cerr<<e.what()<<std::endl;return 1;}
    return 0;
}
