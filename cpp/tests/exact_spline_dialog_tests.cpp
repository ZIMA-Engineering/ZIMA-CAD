#include "../app/sketch_offset_dialog.hpp"
#include "../app/import_options_dialog.hpp"
#include <QTemporaryDir>
#include <QFile>
#include <QLabel>
#include "../app/sketch_bspline_properties_dialog.hpp"
#include <QApplication>
#include <QDialogButtonBox>
#include <QPushButton>
#include <QDoubleSpinBox>
#include <QSpinBox>
#include <QCheckBox>
#include <iostream>
#include <stdexcept>
int main(int argc,char** argv){QApplication app(argc,argv);try{
 QWidget owner;owner.resize(1000,800);
 const std::vector<std::array<double,2>> original{{0.1234567890123,0.0000000123},{0.1234567890123,0.0000000123},{1.1234567890123,0.0000000123}};
 for(bool linked:{false,true}) {
  bool committed=false;
  auto* dialog=new zima::app::SketchBSplinePropertiesDialog(2,false,original,
    [&](unsigned degree,bool closed,const auto& points){
      if(degree!=2 || closed || points!=original)throw std::runtime_error("Dialog rounded exact curve poles");
      committed=true;
    },&owner);
  dialog->set_exact_geometry(linked);
  if(dialog->findChild<QSpinBox*>("bsplineDegree")->isEnabled() ||
     dialog->findChild<QCheckBox*>("bsplineClosed")->isEnabled())throw std::runtime_error("Exact parameterization is editable");
  for(auto* field:dialog->findChildren<QDoubleSpinBox*>())
    if(field->isReadOnly()!=linked)throw std::runtime_error("Incorrect linked curve editing state");
  dialog->buttons()->button(QDialogButtonBox::Ok)->click();
  if(!committed)throw std::runtime_error("Exact spline dialog did not confirm unchanged curve");
 }
 bool offset_committed=false;
 auto* offset=new zima::app::SketchOffsetDialog({},[&](auto value,bool free){
  if(value.source_id!="own-curve"||value.distance!=2.5||!value.flipped||free)throw std::runtime_error("Wrong offset parameters");offset_committed=true;
 },&owner);
 offset->set_source("own-curve");offset->findChild<QDoubleSpinBox*>("sketchOffsetDistance")->setValue(2.5);
 offset->findChild<QPushButton*>("sketchOffsetFlip")->click();
 owner.show();offset->show();app.processEvents();
 if(!(offset->windowFlags()&Qt::SubWindow))throw std::runtime_error("Offset must stay an internal properties window");
 if(!offset->grab().save("sketch-offset-dialog.png"))throw std::runtime_error("Cannot capture offset dialog");
 offset->buttons()->button(QDialogButtonBox::Ok)->click();
 if(!offset_committed)throw std::runtime_error("Offset OK did not commit");
 bool offset_cancel_commit=false;
 auto* canceled_offset=new zima::app::SketchOffsetDialog({},[&](auto,bool){offset_cancel_commit=true;},&owner);
 canceled_offset->buttons()->button(QDialogButtonBox::Cancel)->click();
 if(offset_cancel_commit)throw std::runtime_error("Offset Cancel committed");
 QTemporaryDir temp;
 QFile source(temp.path()+"/sample.stp");
 if(!source.open(QIODevice::WriteOnly))throw std::runtime_error("Cannot create metadata fixture");
 source.write("ISO-10303-21;HEADER;FILE_SCHEMA(('AUTOMOTIVE_DESIGN'));ENDSEC;");source.close();
 auto* settings=new zima::app::ImportOptionsDialog(source.fileName(),0.1,&owner);
 if(settings->mesh_deflection()!=0.1 || settings->findChild<QLabel*>("importFileName")->text()!="sample.stp" ||
    settings->findChild<QLabel*>("importSchema")->text()!="AUTOMOTIVE_DESIGN")throw std::runtime_error("Import metadata/default missing");
 owner.show();settings->show();app.processEvents();
 if(!settings->grab().save("import-options-dialog.png"))throw std::runtime_error("Cannot capture import dialog");
 auto* choice=settings->findChild<QDoubleSpinBox*>("importMeshDeflection");choice->setValue(2.5);
 bool accepted=false;QObject::connect(settings,&QDialog::accepted,[&]{accepted=true;});
 settings->buttons()->button(QDialogButtonBox::Ok)->click();
 if(!accepted || settings->mesh_deflection()!=2.5)throw std::runtime_error("Coarse import value rejected");
 auto* canceled=new zima::app::ImportOptionsDialog(source.fileName(),0.1,&owner);
 bool imported=false;QObject::connect(canceled,&QDialog::accepted,[&]{imported=true;});
 canceled->buttons()->button(QDialogButtonBox::Cancel)->click();
 if(imported)throw std::runtime_error("Cancel accepted import settings");
 std::cout<<"Exact spline dialog retains precision and linked ownership\n";return 0;
}catch(const std::exception& e){std::cerr<<e.what()<<'\n';return 1;}}
