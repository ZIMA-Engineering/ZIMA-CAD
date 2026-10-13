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
void datum_marker_visibility(QApplication& app) {
    using namespace zima;using viewer::CandidateKind;using viewer::ReferenceVisibility;
    kernel::ViewerMesh mesh;
    mesh.axes.push_back({{-20,0,0},{1,0,0},20,{"marker-axis:entity","axis",{}},"Axis"});
    mesh.points.push_back({{-30,0,0},{"marker-axis:entity","axis:point:start",{}},{},true});
    mesh.points.push_back({{-10,0,0},{"marker-axis:entity","axis:point:end",{}},{},true});
    mesh.points.push_back({{-20,0,0},{"marker-axis:origin","point",{}},"Axis",false});
    mesh.points.push_back({{30,0,0},{"standalone:origin","point",{}},"Point",true});
    mesh.edges.push_back({{{-5,20,0},{5,20,0},{5,30,0},{-5,30,0},{-5,20,0}},
        {"marker-plane:entity","plane",{}},true,true});
    mesh.points.push_back({{0,25,0},{"marker-plane:origin","point",{}},"Plane",false});
    mesh.axes.push_back({{0,-20,0},{0,1,0},20,{"profile-feature","axis:primary",{}},"Profile axis"});
    mesh.points.push_back({{0,-30,0},{"profile-feature","profile:path-point:start:from:axis:primary",{}},{},true});
    auto part=document::PartDocument::create_default();auto sketch=sketcher::Sketch::create_default();
    auto axis=document::PartDocument::create_feature_container(sketch.id);
    axis.feature.type=document::FeatureType::Axis;axis.feature.show_point=true;
    axis.feature.show_text=false;axis.feature.sides[0].length=14;axis.feature.sides[1].length=8;
    sketch.owner_container_id=axis.id;sketch.plane=sketcher::SketchPlane::YZ;sketch.plane_auto=false;
    part.history={axis};part.sketches={sketch};part.resolve_constructions();
    auto native=part.feature_result_mesh(part.history.front());
    const auto transform=[](auto& points){for(auto& point:points){point.position.y-=45;point.reference.instance_path="assembly/nested/part";}};
    transform(native.points);transform(native.original_references.points);
    for(auto& value:native.original_references.axes){value.point.y-=45;value.reference.instance_path="assembly/nested/part";}
    mesh.points.insert(mesh.points.end(),native.points.begin(),native.points.end());
    // Match the Assembly packet after its hidden-axis line has been omitted.
    mesh.original_references.points=native.original_references.points;
    mesh.original_references.axes=native.original_references.axes;
    viewer::MeshView view;view.resize(900,600);view.show();view.set_mesh(mesh);
    view.set_projection_mode(viewer::ProjectionMode::Orthographic);view.set_standard_view(viewer::StandardView::Top);
    for(int i=0;i<10;++i)flush(app); // Let the ordinary view animation settle before comparing frames.
    view.fit_points({{-50,-50,0},{50,50,0}});view.set_selection_contract({CandidateKind::Container});flush(app);
    const auto world=[&](QPointF pixel) {
        const auto ray=view.ray_at(pixel);check(ray.has_value(),"Datum picking ray is absent");
        const auto a=ray->first,d=ray->second;check(std::abs(d.z)>1e-6,"Top view ray does not intersect the datum plane");
        const double t=-a.z/d.z;return kernel::Vec3{a.x+t*d.x,a.y+t*d.y,0};
    };
    const QPointF center{450,300};const auto origin=world(center),x=world(center+QPointF(1,0)),y=world(center+QPointF(0,1));
    const auto pixel=[&](kernel::Vec3 point) {
        const double ax=x.x-origin.x,ay=x.y-origin.y,bx=y.x-origin.x,by=y.y-origin.y;
        const double dx=point.x-origin.x,dy=point.y-origin.y,det=ax*by-ay*bx;
        return center+QPointF((dx*by-dy*bx)/det,(ax*dy-ay*dx)/det);
    };
    const auto offered=[&](std::size_t point) {
        const auto candidates=view.selection_candidates_at(pixel(mesh.points[point].position));
        return std::ranges::any_of(candidates,[&](const auto& candidate){
            const auto& reference=mesh.points[point].reference;
            const auto owner=reference.owner_id.ends_with(":origin")?reference.owner_id.substr(0,reference.owner_id.size()-7):reference.owner_id;
            return (candidate.owner_id==reference.owner_id||candidate.kind==CandidateKind::Container&&candidate.owner_id==owner)&&
                (candidate.semantic_key==reference.semantic_key||(candidate.kind==CandidateKind::Container&&candidate.semantic_key.empty()))&&candidate.instance_path==reference.instance_path;
        });
    };
    check(offered(2)&&offered(3)&&offered(4),"Visible datum/standalone marker cannot be picked");
    check(offered(6)&&offered(7)&&offered(8),"Native Feature Axis markers cannot be picked in the nested occurrence");
    const auto camera=view.camera_state();const auto visible=view.grabFramebuffer();
    view.set_reference_visibility(ReferenceVisibility::Axes,false);view.set_reference_visibility(ReferenceVisibility::Planes,false);flush(app);
    check(!offered(0)&&!offered(1)&&!offered(2)&&!offered(4)&&!offered(5)&&offered(3),"Hidden Axis/Plane retained selectable helper points or hid a standalone Point");
    check(!offered(6)&&!offered(7)&&!offered(8),"Hidden native Feature Axis retained its center/side markers");
    const auto hidden=view.grabFramebuffer();check(hidden!=visible,"Datum visibility did not change the rendered frame");
    auto stripped=mesh;stripped.axes.clear();stripped.edges.clear();stripped.points={mesh.points[3]};
    view.set_mesh(stripped,false);flush(app);const auto stripped_frame=view.grabFramebuffer();
    if(hidden!=stripped_frame){hidden.save("datum-hidden.png");stripped_frame.save("datum-stripped.png");}
    check(hidden==stripped_frame,"Hidden datum helper points remain painted");
    view.set_mesh(mesh,false);view.set_selection_contract({CandidateKind::Vertex,CandidateKind::SketchPoint});flush(app);
    check(offered(0)&&offered(2)&&offered(4)&&offered(5),"Explicit point entry cannot consume hidden datum markers");
    check(offered(6)&&offered(7)&&offered(8),"Explicit point entry lost the native Feature Axis side identities");
    view.set_selection_contract({CandidateKind::Container});view.set_constraint_reference_highlights({"marker-axis:entity"},{});flush(app);
    check(view.grabFramebuffer()!=hidden,"Independent reference inspection cannot expose the hidden Axis endpoints");
    view.set_constraint_reference_highlights({},{{"marker-plane:origin","point",{}}});flush(app);check(offered(4),"Independent inspection cannot expose a hidden Plane defining point");
    view.set_constraint_reference_highlights({},{});view.set_reference_visibility(ReferenceVisibility::Axes,true);view.set_reference_visibility(ReferenceVisibility::Planes,true);flush(app);
    check(offered(2)&&offered(4)&&view.mesh().points.size()==mesh.points.size()&&view.camera_state()==camera,"Visibility restoration changed native markers or camera");
}
}
int main(int argc,char** argv){
    QApplication app(argc,argv);
    try{
        if(!qEnvironmentVariableIsSet("ZIMA_VERIFY_FOREIGN_PREVIEW_ONLY"))datum_marker_visibility(app);
        const QString repository=QFileInfo(QString::fromUtf8(__FILE__)).absoluteDir().absoluteFilePath("../..");int metadata_count=0,logos=0;
        for(const auto& root:{"config/formats","config/templates","config/lib/02-NATIVE","config/materials","config/symbols","config/lib/01-SHEETMETAL"}){
            QDirIterator entries(QDir(repository).filePath(root),{"metadata.ini"},QDir::Files,QDirIterator::Subdirectories);
            while(entries.hasNext()){QSettings metadata(entries.next(),QSettings::IniFormat);for(const auto& language:{"cs","en","de","fr","ru"}){const auto label=metadata.value(QStringLiteral("Directory/Label/%1").arg(language));check(label.metaType().id()==QMetaType::QString&&!label.toString().trimmed().isEmpty(),"Factory library label is missing or not a scalar Qt string");}++metadata_count;
                const QString logo=QFileInfo(metadata.fileName()).absoluteDir().filePath("logo-text.svg");if(!QFileInfo::exists(logo))continue;++logos;const QIcon icon(logo);for(const auto background:{QColor("#ffffff"),QColor("#25292e")}){QImage image(64,64,QImage::Format_ARGB32);image.fill(background);QPainter painter(&image);icon.paint(&painter,image.rect());painter.end();int visible=0;for(int y=0;y<64;++y)for(int x=0;x<64;++x)visible+=image.pixelColor(x,y)!=background;check(visible>100,"Symbol category logo vanished in a theme");}
            }
        }check(metadata_count==34&&logos==9,"Factory library metadata/branding inventory differs");
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
