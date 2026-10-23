#include "appearance_dialog.hpp"
#include <zima/document/component_source.hpp>
#include <QApplication>
#include <QComboBox>
#include <QDialogButtonBox>
#include <QElapsedTimer>
#include <QLineEdit>
#include <QListWidget>
#include <QMouseEvent>
#include <QPushButton>
#include <QSlider>
#include <QSpinBox>
#include <QTemporaryDir>
#include <QThread>
#include <QVariantAnimation>
#include <iostream>
#include <algorithm>
#include <set>
#include <zima/assembly/assembly_document.hpp>
#include <zima/document/part_document.hpp>
#include <zima/kernel/pattern_geometry.hpp>
#include <zima/kernel/axis_display.hpp>
using namespace zima;
void require(bool ok, const char *message) {
  if (!ok)
    throw std::runtime_error(message);
}
void render() {
  QElapsedTimer t;
  t.start();
  while (t.elapsed() < 250) {
    QApplication::processEvents();
    QThread::msleep(5);
  }
}
void copied_axis_presentation() {
  kernel::ViewerMesh source;
  source.axes.push_back({{0,0,0},{1,0,0},12,{"source","axis:primary",{}}});
  source.axes.push_back({{0,4,0},{1,0,0},12,{"source","centerline:from:centroid:profile",{}}});
  kernel::ViewerEdge curve;curve.reference={"source","centerline:from:centroid:curve",{}};
  curve.construction=curve.overlay=curve.dash_dot=true;
  curve.points={{-5,-4,0},{0,-2,0},{5,-4,0}};source.edges.push_back(curve);
  for(const bool mirror:{false,true}) {
    kernel::PatternRequest pattern;pattern.linear[0].count=2;pattern.linear[0].spacing=20;
    auto copied=mirror?kernel::mirrored_viewer_mesh(source,{{10,0,0},{1,0,0}},"copy"):
      kernel::pattern_copy_mesh(source,pattern,1,"copy");
    auto control=copied;
    for(auto& axis:control.axes) {std::string storage;axis.reference.semantic_key=kernel::construction_source_key(axis.reference.semantic_key,storage);}
    for(auto& edge:control.edges) {std::string storage;edge.reference.semantic_key=kernel::construction_source_key(edge.reference.semantic_key,storage);}
    viewer::MeshView view;view.resize(600,450);view.set_candidate_filter([](const auto&){return false;});view.show();view.set_standard_view(viewer::StandardView::Top);
    view.fit_points({{10,-10,0},{30,10,0}});
    // Standard views animate for 850 ms; compare only the settled camera.
    for(int frame=0;frame<4;++frame)render();
    view.set_mesh(control,false);render();const auto expected=view.grabFramebuffer();
    view.set_mesh(copied,false);render();
    if(view.grabFramebuffer()!=expected) {
      expected.save("pattern-axis-expected.png");view.grabFramebuffer().save("pattern-axis-actual.png");
      std::cerr<<"Copied presentation mirror="<<mirror<<'\n';
      throw std::runtime_error("Copied construction presentation differs from its source types");
    }
    view.set_reference_visibility(viewer::ReferenceVisibility::Axes,false);render();
    require(view.grabFramebuffer()!=expected,"Copied construction axes ignore visibility");
    view.set_reference_visibility(viewer::ReferenceVisibility::Axes,true);render();
    require(view.grabFramebuffer()==expected,"Copied axes do not restore after visibility toggle");
  }
  std::cout<<"Pattern/Mirror axes and curved centerlines: identical presentation and visibility toggles passed\n";
}
int main(int argc, char **argv) {
  QApplication app(argc, argv);
  try {
    copied_axis_presentation();
    kernel::Appearance a;
    a.body = {"#4DC57D5C", .12, 1};
    a.bodies["body"] = {"#225FC2", .3, 0};
    a.owner_bodies["result"] = "body";
    a.groups.push_back({"group",
                        "Polished faces",
                        "body",
                        {"#C1C5C8", .1, 1},
                        {"result::cap"}});
    require(document::deserialize_appearance(
                document::serialize_appearance(a)) == a,
            "Appearance roundtrip");
    auto bad = a;
    bad.groups.push_back(
        {"other", "Duplicate face", "body", {}, {"result::cap"}});
    bool rejected = false;
    try {
      document::validate_appearance(bad);
    } catch (...) {
      rejected = true;
    }
    require(rejected, "Multiple groups claimed one face");
    auto palette = document::default_surface_palette();
    const auto skeleton=std::find_if(palette.begin(),palette.end(),[](const auto& color){return color.id=="skeleton";});
    require(skeleton!=palette.end() && QColor(QString::fromStdString(skeleton->style.color)).alpha()==77,"Skeleton palette transparency");
    require(palette.size() >= 26, "Missing existing colors or materials");
    std::set<std::string> ral_codes;
    for(const auto& swatch:palette)if(swatch.category=="RAL") {
      require(ral_codes.insert(swatch.name).second,"Duplicate RAL code");
      require(swatch.style.metallic==0&&QColor(QString::fromStdString(swatch.style.color)).isValid(),"Invalid RAL paint style");
    }
    require(ral_codes.size()==23&&ral_codes.contains("RAL 1016")&&ral_codes.contains("RAL 3024"),"Incomplete selected RAL palette");
    require(document::deserialize_palette(
                document::serialize_palette(palette)) == palette,
            "Palette roundtrip");
    kernel::ViewerMesh result;
    result.vertices={{-1,-1,0},{1,-1,0},{0,1,0}};
    result.triangles={0,1,2};
    result.triangle_references={{"result","cap","instance"}};
    auto references=result;
    references.triangle_references={{"original","cap","instance"}};
    const auto ordinary=viewer::ordered_viewer_candidates(result,references,{0,0,2},{0,0,-1},.01);
    const auto painting=viewer::ordered_viewer_candidates(result,references,{0,0,2},{0,0,-1},.01,true);
    auto final_face=[](const auto& c){return c.kind==viewer::CandidateKind::Face&&c.geometry==viewer::CandidateGeometry::Display;};
    require(std::none_of(ordinary.begin(),ordinary.end(),final_face)&&std::any_of(painting.begin(),painting.end(),final_face),"Appearance must explicitly offer final occurrence faces");
    QTemporaryDir dir;
    auto part = document::PartDocument::create_default();
    part.appearance = a;
    part.save((dir.path() + "/appearance.prtz").toStdString());
    require(document::PartDocument::load(
                (dir.path() + "/appearance.prtz").toStdString())
                    .appearance == a,
            "Part persistence");
    assembly::AssemblyDocument assembly;
    assembly::PartOccurrence occurrence;
    occurrence.occurrence_id = "instance";
    occurrence.name = "Part";
    occurrence.source_document_id=part.document_id;
    occurrence.source_path="appearance.prtz";
    occurrence.appearance = a;
    occurrence.appearance_override = a;
    assembly.components.push_back(occurrence);
    assembly.save((dir.path() + "/appearance.asmz").toStdString());
    auto loaded = assembly::AssemblyDocument::load(
        (dir.path() + "/appearance.asmz").toStdString());
    require(loaded.components.at(0).appearance == document::component_appearance(part) &&
                loaded.components.at(0).appearance_override == a,
            "Occurrence persistence");
    QWidget owner;
    owner.resize(1100, 850);
    owner.show();
    QWidget view(&owner);
    view.setGeometry(0, 0, 200, 200);
    view.show();
    int commits = 0;
    kernel::Appearance preview;
    std::vector<kernel::NamedStyle> saved;
    app::AppearanceDialog dialog(
        {}, "body", palette, [&](const auto &p) { preview = p; },
        [&](const auto &p, const auto &colors) {
          ++commits;
          preview = p;
          saved = colors;
        },
        [](const auto &) {}, &owner);
    dialog.show();
    render();
    auto* categories=dialog.findChild<QComboBox*>("appearanceCategory");
    auto* swatches=dialog.findChild<QListWidget*>("appearancePalette");
    require(categories&&swatches,"Missing appearance category controls");
    categories->setCurrentIndex(categories->findData("RAL"));render();
    require(swatches->count()==23,"RAL category does not filter its swatches");
    auto* ral=swatches->item(0);const auto local=swatches->visualItemRect(ral).center();
    require(ral->icon().pixmap(44,QIcon::Normal).toImage()==ral->icon().pixmap(44,QIcon::Selected).toImage(),"Selecting a paint swatch recolors its preview icon");
    const auto global=swatches->viewport()->mapToGlobal(local);
    QMouseEvent press(QEvent::MouseButtonPress,QPointF(local),QPointF(global),Qt::LeftButton,Qt::LeftButton,Qt::NoModifier);
    QMouseEvent release(QEvent::MouseButtonRelease,QPointF(local),QPointF(global),Qt::LeftButton,Qt::NoButton,Qt::NoModifier);
    QApplication::sendEvent(swatches->viewport(),&press);QApplication::sendEvent(swatches->viewport(),&release);
    require(preview.bodies.at("body").color=="#E6D2B5"&&commits==0,"RAL selection did not preview without committing");
    require(dialog.grab().save("appearance-ral-dialog.png"),"RAL screenshot failed");
    auto click = [&](const char *name) {
      auto *b = dialog.findChild<QPushButton *>(name);
      require(b, "Missing control");
      b->click();
    };
    click("appearanceAddGroup");
    viewer::ViewerCandidate face{
        viewer::CandidateKind::Face,       0, 0, "result", "cap", {},
        viewer::CandidateGeometry::Display};
    dialog.select_face(face);
    require(dialog.pending().groups.at(0).faces.size() == 1,
            "Result face assignment");
    click("appearanceAddGroup");
    dialog.select_face(face);
    require(dialog.pending().groups.at(0).faces.empty() &&
                dialog.pending().groups.at(1).faces.size() == 1,
            "Move face to second group");
    dialog.end_entry();
    face.semantic_key = "side";
    dialog.select_face(face);
    require(dialog.pending().groups.at(1).faces.size() == 1,
            "Disarmed entry accepted a face");
    click("appearanceClearFaces");
    require(dialog.pending().groups.size() == 2 &&
                dialog.pending().groups.at(1).faces.empty(),
            "Clear should keep groups");
    click("appearanceDefaults");
    require(dialog.pending().groups.empty(), "Reset should remove groups");
    dialog.findChild<QLineEdit *>("appearanceName")->setText("My bronze");
    dialog.findChild<QSlider *>("appearanceMetallic")->setValue(100);
    dialog.findChild<QSlider *>("appearanceGloss")->setValue(88);
    auto* transparency=dialog.findChild<QSlider*>("appearanceTransparency");
    auto* percent=dialog.findChild<QSpinBox*>("appearanceTransparencyValue");
    require(transparency && percent,"Missing transparency controls");
    percent->setValue(70);
    require(transparency->value()==70 && QColor(QString::fromStdString(preview.bodies.at("body").color)).alpha()==77,"Transparency preview or slider synchronization");
    click("appearanceAddColor");
    require(commits == 0, "Palette committed before OK");
    render();
    require(dialog.grab().save("appearance-dialog.png"), "Screenshot failed");
    auto *sphere =
        dynamic_cast<viewer::MeshView *>(dialog.findChild<QWidget *>("appearanceSpherePreview"));
    require(sphere && sphere->isValid(), "OpenGL sphere unavailable");
    sphere->set_body_surface_styles({"#AD7945", .8, 0});
    render();
    auto matte = sphere->grabFramebuffer();
    sphere->set_body_surface_styles({"#AD7945", .1, 1});
    render();
    auto metal = sphere->grabFramebuffer();
    require(!matte.isNull() && matte != metal,
            "Material shader did not change pixels");
    matte.save("appearance-matte.png");
    metal.save("appearance-metal.png");
    kernel::ViewerMesh layers;
    layers.vertices={{-2,-2,0},{2,-2,0},{0,2,0},{-2,-2,1},{2,-2,1},{0,2,1}};
    layers.triangles={0,1,2,3,4,5};
    layers.triangle_references={{"rear","face",{}},{"front","face",{}}};
    sphere->set_standard_view(viewer::StandardView::Top);
    sphere->set_mesh(layers);
    sphere->set_body_surface_styles({"#FFFFFF",.5,0},{},{{"\x1f" "rear",{"#802244CC",.5,0}},{"\x1f" "front",{"#80CC4422",.5,0}}});
    for(int frame=0;frame<4;++frame)render();
    const auto ordered=sphere->grabFramebuffer();
    layers.triangles={3,4,5,0,1,2};
    std::reverse(layers.triangle_references.begin(),layers.triangle_references.end());
    sphere->set_mesh(layers,false);render();
    require(ordered==sphere->grabFramebuffer(),"Transparent rendering depends on mesh order");
    sphere->set_body_surface_styles({"#FFFFFF",.5,0},{},{{"\x1f" "rear",{"#2244CC",.5,0}},{"\x1f" "front",{"#00CC4422",.5,0}}});
    render();const auto invisible_front=sphere->grabFramebuffer();
    layers.triangles={0,1,2};layers.triangle_references={{"rear","face",{}}};
    sphere->set_mesh(layers,false);render();
    require(invisible_front==sphere->grabFramebuffer(),"Fully transparent surface occludes opaque geometry");
    // Fill transparency must not change which native edges are occluded.
    layers.triangles={0,1,2,3,4,5};
    layers.triangle_references={{"rear","face",{}},{"front","face",{}}};
    layers.edges={{{{-.5,0,0},{.5,0,0}},{"rear","edge",{}}},
                  {{{-.5,-.4,1},{.5,-.4,1}},{"front","edge",{}}}};
    sphere->set_mesh(layers,false);sphere->set_edge_color_override(QColor(255,0,255));
    const auto edge_pixels=[](const QImage& image){
        std::size_t count{};for(int y=0;y<image.height();++y)for(int x=0;x<image.width();++x){
            const auto c=image.pixelColor(x,y);if(c.red()>180&&c.blue()>180&&c.green()<80)++count;
        }return count;
    };
    std::map<viewer::DisplayMode,QImage> opaque_modes;
    for(const int alpha:{255,77,0}) {
        const auto color=QColor(90,110,140,alpha).name(QColor::HexArgb).toStdString();
        sphere->set_body_surface_styles({color,.5,0});
        for(const auto mode:{viewer::DisplayMode::Wire,viewer::DisplayMode::HiddenEdges,
                viewer::DisplayMode::NoHiddenEdges,viewer::DisplayMode::ShadedWithEdges}) {
            sphere->set_display_mode(mode);render();const auto image=sphere->grabFramebuffer();
            if(alpha==255)opaque_modes.emplace(mode,image);
            else if(mode!=viewer::DisplayMode::ShadedWithEdges)
                require(image==opaque_modes.at(mode),"Surface alpha changed an edge-only display mode");
            else require(edge_pixels(image)==edge_pixels(opaque_modes.at(mode)),"Transparent shaded fill exposed rear edges");
        }
    }
    require(edge_pixels(opaque_modes.at(viewer::DisplayMode::Wire))>
            edge_pixels(opaque_modes.at(viewer::DisplayMode::NoHiddenEdges))*1.5,
        "No-hidden mode did not occlude the rear test edge");
    require(edge_pixels(opaque_modes.at(viewer::DisplayMode::NoHiddenEdges))>5,
        "No-hidden mode removed the visible front edge");
    opaque_modes.at(viewer::DisplayMode::NoHiddenEdges).save("appearance-visible-edges.png");
    sphere->set_edge_color_override({});sphere->set_display_mode(viewer::DisplayMode::Shaded);
    if(const auto source=qEnvironmentVariable("ZIMA_VERIFY_TRANSPARENT_PART_FILE");!source.isEmpty()) {
        std::vector<kernel::BodyResult> boundaries;
        const auto part=document::PartDocument::load(std::filesystem::path(source.toStdWString()),&boundaries);
        require(!boundaries.empty()&&!boundaries.back().mesh.triangles.empty(),"Supplied transparent Part has no cached faces");
        viewer::MeshView actual;actual.resize(1000,800);actual.show();
        actual.set_mesh(boundaries.back().mesh);actual.set_standard_view(viewer::StandardView::Isometric);
        for(auto* animation:actual.findChildren<QVariantAnimation*>())animation->setCurrentTime(animation->duration());
        actual.set_body_surface_styles(part.appearance.body);
        actual.set_display_mode(viewer::DisplayMode::NoHiddenEdges);render();
        const auto transparent=actual.grabFramebuffer();transparent.save("supplied-part-visible-edges.png");
        auto opaque=part.appearance.body;const QColor color(QString::fromStdString(opaque.color));
        opaque.color=QColor(color.red(),color.green(),color.blue(),255).name(QColor::HexArgb).toStdString();
        actual.set_body_surface_styles(opaque);render();
        require(transparent==actual.grabFramebuffer(),"Supplied Part transparency changed visible edges");
        actual.set_display_mode(viewer::DisplayMode::Wire);render();
        require(transparent!=actual.grabFramebuffer(),"Supplied Part still shows every hidden edge");
        std::cout<<"Supplied transparent Part: native cached faces, alpha-independent visible edges and distinct wire mode passed\n";
    }
    QMouseEvent shortclick(QEvent::MouseButtonRelease, QPointF(20, 20),
                           QPointF(view.mapToGlobal(QPoint(20, 20))),
                           Qt::MiddleButton, Qt::NoButton, Qt::NoModifier);
    QApplication::sendEvent(&view, &shortclick);
    require(commits == 0, "Short MMB committed");
    QMouseEvent dbl(QEvent::MouseButtonDblClick, QPointF(20, 20),
                    QPointF(view.mapToGlobal(QPoint(20, 20))), Qt::MiddleButton,
                    Qt::MiddleButton, Qt::NoModifier);
    QApplication::sendEvent(&view, &dbl);
    require(commits == 1 && saved.size() == palette.size() + 1,
            "MMB double click outside dialog did not commit palette");
    require(QColor(QString::fromStdString(saved.back().style.color)).alpha()==77,"Custom palette lost transparency");
    app::AppearanceDialog cancel(
        a, "body", palette, [&](const auto &p) { preview = p; },
        [&](const auto &, const auto &) { ++commits; }, [](const auto &) {},
        &owner);
    cancel.show();
    cancel.findChild<QSlider *>("appearanceGloss")->setValue(50);
    cancel.findChild<QSpinBox*>("appearanceTransparencyValue")->setValue(100);
    cancel.reject();
    require(preview == a && commits == 1, "Cancel did not restore appearance");
    std::cout << "Appearance persistence, result-face groups, palette "
                 "transaction, MMB and material rendering passed\n";
    return 0;
  } catch (const std::exception &e) {
    std::cerr << e.what() << '\n';
    return 1;
  }
}
