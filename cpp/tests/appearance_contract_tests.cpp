#include "appearance_dialog.hpp"
#include <QApplication>
#include <QDialogButtonBox>
#include <QElapsedTimer>
#include <QLineEdit>
#include <QMouseEvent>
#include <QPushButton>
#include <QSlider>
#include <QTemporaryDir>
#include <QThread>
#include <iostream>
#include <algorithm>
#include <zima/assembly/assembly_document.hpp>
#include <zima/document/part_document.hpp>
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
int main(int argc, char **argv) {
  QApplication app(argc, argv);
  try {
    kernel::Appearance a;
    a.body = {"#C57D5C", .12, 1};
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
    require(palette.size() >= 26, "Missing existing colors or materials");
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
    occurrence.appearance = a;
    occurrence.appearance_override = a;
    assembly.components.push_back(occurrence);
    assembly.save((dir.path() + "/appearance.asmz").toStdString());
    auto loaded = assembly::AssemblyDocument::load(
        (dir.path() + "/appearance.asmz").toStdString());
    require(loaded.components.at(0).appearance == a &&
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
    app::AppearanceDialog cancel(
        a, "body", palette, [&](const auto &p) { preview = p; },
        [&](const auto &, const auto &) { ++commits; }, [](const auto &) {},
        &owner);
    cancel.show();
    cancel.findChild<QSlider *>("appearanceGloss")->setValue(50);
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
