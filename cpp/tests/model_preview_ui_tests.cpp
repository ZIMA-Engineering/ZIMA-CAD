#include "model_preview_tab.hpp"
#include "profile_solid_fixture.hpp"
#include <dxfinterface.h>
#include <dxfsceneview.h>
#include <zima/viewer/mesh_view.hpp>
#include <zima/kernel/occt_kernel.hpp>
#include <QApplication>
#include <QTemporaryDir>
#include <QFile>
#include <QTreeWidget>
#include <QLabel>
#include <QGraphicsItem>
#include <QThread>
#include <QCryptographicHash>
#include <QSettings>
#include <QDirIterator>
#include <QIcon>
#include <QPainter>
#include <QGraphicsPathItem>
#include <iostream>
#include <cmath>

namespace {
void check(bool value,const char* message){if(!value)throw std::runtime_error(message);}
QByteArray digest(const QString& path){QFile file(path);check(file.open(QIODevice::ReadOnly),"Cannot open fixture");return QCryptographicHash::hash(file.readAll(),QCryptographicHash::Sha256);}
void flush(QApplication& app){for(int i=0;i<4;++i){app.processEvents();QThread::msleep(15);}}
double distance(zima::kernel::Vec3 a,zima::kernel::Vec3 b){return std::hypot(std::hypot(a.x-b.x,a.y-b.y),a.z-b.z);}
}
int main(int argc,char** argv){
    QApplication app(argc,argv);
    try{
        const QString repository=QFileInfo(QString::fromUtf8(__FILE__)).absoluteDir().absoluteFilePath("../..");int metadata_count=0,logos=0;
        for(const auto& root:{"config/formats","config/templates","config/lib/02-NATIVE","config/materials","config/symbols","config/lib/01-SHEETMETAL"}){
            QDirIterator entries(QDir(repository).filePath(root),{"metadata.ini"},QDir::Files,QDirIterator::Subdirectories);
            while(entries.hasNext()){QSettings metadata(entries.next(),QSettings::IniFormat);for(const auto& language:{"cs","en","de","fr","ru"}){const auto label=metadata.value(QStringLiteral("Directory/Label/%1").arg(language));check(label.metaType().id()==QMetaType::QString&&!label.toString().trimmed().isEmpty(),"Factory library label is missing or not a scalar Qt string");}++metadata_count;
                const QString logo=QFileInfo(metadata.fileName()).absoluteDir().filePath("logo-text.svg");if(!QFileInfo::exists(logo))continue;++logos;const QIcon icon(logo);for(const auto background:{QColor("#ffffff"),QColor("#25292e")}){QImage image(64,64,QImage::Format_ARGB32);image.fill(background);QPainter painter(&image);icon.paint(&painter,image.rect());painter.end();int visible=0;for(int y=0;y<64;++y)for(int x=0;x<64;++x)visible+=image.pixelColor(x,y)!=background;check(visible>100,"Symbol category logo vanished in a theme");}
            }
        }check(metadata_count==34&&logos==8,"Factory library metadata/branding inventory differs");
        QTemporaryDir fixture;check(fixture.isValid(),"Cannot create preview fixture");
        const QString independent_title=fixture.filePath("independent.tblz");check(QFile::copy(QDir(repository).filePath("config/formats/ZE-TITLE-BLOCK.tblz"),independent_title),"Cannot isolate the title block");
        const auto title=zima::document::PartDocument::load(std::filesystem::u8path(independent_title.toStdString()));int embedded_logos=0;
        for(const auto& sketch:title.sketches)if(sketch.drawing_template)for(const auto& image:sketch.drawing_template->images){if(image.name!="ZIMA-Engineering.svg")continue;++embedded_logos;const auto bytes=QByteArray::fromBase64(QByteArray::fromStdString(image.data_base64));QImage rendered;check(image.format=="svg"&&bytes.size()>5000&&rendered.loadFromData(bytes,"SVG")&&!rendered.isNull(),"Title-block logo requires an external SVG or cannot render from its native data");}
        check(embedded_logos==5&&!QFileInfo::exists(fixture.filePath("ZIMA-Engineering.svg")),"Title-block language variants do not keep independent embedded logos");
        const QString dxf=fixture.filePath("řezání-проверка.dxf");
        QFile file(dxf);check(file.open(QIODevice::WriteOnly),"Cannot create DXF fixture");
        file.write("0\nSECTION\n2\nHEADER\n9\n$INSUNITS\n70\n4\n0\nENDSEC\n0\nSECTION\n2\nTABLES\n0\nTABLE\n2\nLAYER\n70\n2\n0\nLAYER\n2\n0\n70\n0\n62\n7\n6\nCONTINUOUS\n0\nLAYER\n2\nCUT\n70\n0\n62\n1\n6\nCONTINUOUS\n0\nENDTAB\n0\nENDSEC\n0\nSECTION\n2\nBLOCKS\n0\nBLOCK\n2\nHOLE\n70\n0\n10\n0\n20\n0\n30\n0\n0\nCIRCLE\n8\n0\n10\n0\n20\n0\n40\n2\n0\nENDBLK\n0\nENDSEC\n0\nSECTION\n2\nENTITIES\n0\nLWPOLYLINE\n8\nCUT\n90\n4\n70\n1\n10\n0\n20\n0\n42\n1\n10\n10\n20\n0\n10\n10\n20\n10\n10\n0\n20\n10\n0\nINSERT\n8\nCUT\n2\nHOLE\n10\n5\n20\n5\n41\n1\n42\n1\n43\n1\n50\n0\n0\nENDSEC\n0\nEOF\n");file.close();const auto dxf_hash=digest(dxf);
        DXFInterface parsed(dxf);check(parsed.loaded()&&parsed.units()==4,"DXF load or units failed");check(parsed.unsupported().isEmpty(),"Supported DXF fixture was reported incomplete");check(parsed.scene()->items().size()==2,"DXF block definition leaked or INSERT was omitted");
        const auto bounds=parsed.scene()->itemsBoundingRect();check(bounds.bottom()>=10&&bounds.top()<=-4.9,"Closed/bulged polyline was rendered as straight open lines");for(auto* item:parsed.scene()->items())check(item->data(0).toString()=="CUT","Block layer inheritance failed");
        const QString spline=fixture.filePath("rational.dxf");{QFile file(spline);check(file.open(QIODevice::WriteOnly),"Cannot write spline fixture");file.write("0\nSECTION\n2\nENTITIES\n0\nSPLINE\n8\n0\n70\n4\n71\n2\n72\n6\n73\n3\n40\n0\n40\n0\n40\n0\n40\n1\n40\n1\n40\n1\n41\n1\n41\n0.7071067811865476\n41\n1\n10\n1\n20\n0\n30\n0\n10\n1\n20\n1\n30\n0\n10\n0\n20\n1\n30\n0\n0\nENDSEC\n0\nEOF\n");}DXFInterface rational(spline);check(rational.loaded()&&rational.unsupported().isEmpty()&&rational.scene()->items().size()==1,"Rational spline preview failed");auto* curve=dynamic_cast<QGraphicsPathItem*>(rational.scene()->items().front());check(curve&&curve->path().elementCount()==257,"Spline path is absent");const auto midpoint=curve->path().elementAt(128);check(std::abs(midpoint.x-std::sqrt(.5))<1e-10&&std::abs(midpoint.y-std::sqrt(.5))<1e-10,"DXF rational weights were lost");
        const QString unsupported=fixture.filePath("unsupported.dxf");{QFile file(unsupported);check(file.open(QIODevice::WriteOnly),"Cannot write unsupported fixture");file.write("0\nSECTION\n2\nENTITIES\n0\nXLINE\n8\n0\n10\n0\n20\n0\n11\n1\n21\n0\n0\nENDSEC\n0\nEOF\n");}DXFInterface partial(unsupported);check(partial.loaded()&&partial.unsupported().contains("XLINE"),"Unsupported DXF entity was silently omitted");
        zima::app::ModelPreviewTab drawing(dxf);drawing.resize(1000,700);drawing.load_dxf();drawing.show();flush(app);auto* tree=drawing.findChild<QTreeWidget*>("previewTree");auto* view=drawing.findChild<DXFSceneView*>("dxfPreviewView");check(tree&&view&&drawing.findChild<QLabel*>("previewDetails")->text().contains("mm"),"DXF tree or unit information is absent");auto* layers=tree->topLevelItem(0)->child(0);QTreeWidgetItem* cut{};for(int i=0;i<layers->childCount();++i)if(layers->child(i)->text(0)=="CUT")cut=layers->child(i);check(cut,"DXF layer is absent");cut->setCheckState(0,Qt::Unchecked);for(auto* item:view->scene()->items())check(!item->isVisible(),"DXF layer visibility failed");cut->setCheckState(0,Qt::Checked);for(auto* item:view->scene()->items())check(item->isVisible(),"DXF layer restore failed");check(digest(dxf)==dxf_hash,"Preview modified the original DXF");
        auto document=zima::document::PartDocument::create_default();document.history.push_back(zima::test::rectangular_feature(document,{20,10,5}));zima::kernel::OcctKernel kernel;const auto bodies=kernel.evaluate_history(document.kernel_operations());check(!bodies.empty(),"STEP fixture calculation failed");
        const QString step=fixture.filePath("box.step");kernel.export_step({{bodies.back(),{},{}}},step.toUtf8().toStdString());const auto step_hash=digest(step);auto imported=zima::app::load_step_preview(step);check(!imported.nodes.empty(),"STEP preview did not load its hierarchy");
        zima::app::ModelPreviewTab model(step);model.resize(1000,700);model.set_step(std::move(imported));model.show();flush(app);auto* mesh=dynamic_cast<zima::viewer::MeshView*>(model.findChild<QWidget*>("stepPreviewView"));check(mesh&&!mesh->mesh().triangles.empty(),"STEP preview mesh is absent");check(digest(step)==step_hash,"STEP preview modified the source");
        zima::viewer::MeshView viewport;viewport.resize(900,600);viewport.show();viewport.set_mesh(bodies.back().mesh);viewport.set_preserve_screen_position_on_resize(true);flush(app);
        for(const auto projection:{zima::viewer::ProjectionMode::Orthographic,zima::viewer::ProjectionMode::Perspective}){
            viewport.set_projection_mode(projection);viewport.resize(900,600);flush(app);viewport.fit_all();flush(app);const auto camera=viewport.camera_state();const auto before=viewport.ray_at({260,210});const auto second=viewport.ray_at({540,360});const auto uploads=viewport.property("gpuMeshUploadCount").toULongLong();
            for(const int width:{750,610,810,900}){viewport.resize(width,600);flush(app);const auto after=viewport.ray_at({260,210}),other=viewport.ray_at({540,360});check(before&&second&&after&&other,"Resize camera ray is absent");check(distance(before->first,after->first)<1e-3&&distance(before->second,after->second)<1e-5&&distance(second->second,other->second)<1e-5,"Panel resize moved the model/picking ray");check(viewport.camera_state()==camera,"Panel resize changed the stored camera");check(viewport.property("gpuMeshUploadCount").toULongLong()==uploads,"Panel resize uploaded geometry again");}
        }
        std::cout<<"Read-only DXF blocks/layers/bulges, STEP mesh and repeated orthographic/perspective width stability passed\n";return 0;
    }catch(const std::exception& error){std::cerr<<error.what()<<'\n';return 1;}
}
