#include "primitive_properties_dialog.hpp"
#include "construction_properties_dialog.hpp"
#include "component_properties_dialog.hpp"
#include "mate_properties_dialog.hpp"
#include "sketch_properties_dialog.hpp"
#include "sketch_dimension_properties_dialog.hpp"
#include "sketch_text_properties_dialog.hpp"
#include "file_dialog.hpp"

#include <QApplication>
#include <QAbstractProxyModel>
#include <QDialogButtonBox>
#include <QDoubleSpinBox>
#include <QComboBox>
#include <QCheckBox>
#include <QFileDialog>
#include <QFileSystemModel>
#include <QMouseEvent>
#include <QPushButton>
#include <QPlainTextEdit>
#include <QTimer>
#include <QWidget>

#include <chrono>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <stdexcept>

namespace {

void require(bool condition, const char* message) {
    if (!condition) throw std::runtime_error(message);
}

}  // namespace

int main(int argc, char* argv[]) {
    QApplication application(argc, argv);
    QWidget parent;
    parent.resize(900, 650);
    parent.show();
    const auto initial = zima::document::PartDocument::create_box_container();

    try {
        bool cancel_committed = false;
        auto* cancel_dialog = new zima::app::PrimitivePropertiesDialog(
            initial, false, false,
            [&](zima::document::HistoryContainer) { cancel_committed = true; },
            &parent);
        cancel_dialog->show();
        application.processEvents();
        cancel_dialog->buttons()->button(QDialogButtonBox::Cancel)->click();
        application.processEvents();
        require(!cancel_committed, "Cancel committed pending Box changes");

        int ok_commits = 0;
        zima::document::HistoryContainer committed;
        auto* ok_dialog = new zima::app::PrimitivePropertiesDialog(
            initial, false, false,
            [&](zima::document::HistoryContainer value) {
                ++ok_commits;
                committed = std::move(value);
            },
            &parent);
        ok_dialog->show();
        application.processEvents();
        auto* length = ok_dialog->findChild<QDoubleSpinBox*>("boxLength");
        require(length != nullptr, "Box dialog must expose its length");
        length->setValue(125.0);
        const auto translations =
            ok_dialog->findChildren<QDoubleSpinBox*>("primitiveTranslation");
        require(translations.size() == 3,
                "Box dialog must expose three translation coordinates");
        translations.front()->setValue(42.0);
        ok_dialog->buttons()->button(QDialogButtonBox::Ok)->click();
        application.processEvents();
        require(ok_commits == 1, "OK must commit exactly once");
        require(committed.box.length == 125.0, "OK did not commit edited length");
        require(committed.placement.x == 42.0,
                "OK did not commit container placement");

        int middle_commits = 0;
        zima::document::CombineMode middle_operation =
            zima::document::CombineMode::Add;
        auto* middle_dialog = new zima::app::PrimitivePropertiesDialog(
            initial, true, true,
            [&](zima::document::HistoryContainer value) {
                ++middle_commits;
                middle_operation = value.combine_mode;
            },
            &parent);
        middle_dialog->show();
        application.processEvents();
        auto* operation = middle_dialog->findChild<QComboBox*>();
        require(operation != nullptr, "Box dialog has no operation selector");
        operation->setCurrentIndex(operation->findData("subtract"));
        QMouseEvent middle_double_click(
            QEvent::MouseButtonDblClick,
            QPointF(50.0, 50.0),
            QPointF(50.0, 50.0),
            QPointF(50.0, 50.0),
            Qt::MiddleButton,
            Qt::MiddleButton,
            Qt::NoModifier);
        QApplication::sendEvent(&parent, &middle_double_click);
        application.processEvents();
        require(middle_commits == 1,
                "Middle-button double-click outside dialog did not invoke OK");
        require(middle_operation == zima::document::CombineMode::Subtract,
                "Properties dialog did not commit subtract mode");

        auto cylinder_initial =
            zima::document::PartDocument::create_cylinder_container();
        int cylinder_commits = 0;
        zima::document::HistoryContainer committed_cylinder;
        auto* cylinder_dialog = new zima::app::PrimitivePropertiesDialog(
            cylinder_initial, false, false,
            [&](zima::document::HistoryContainer value) {
                ++cylinder_commits;
                committed_cylinder = std::move(value);
            }, &parent);
        cylinder_dialog->show();
        application.processEvents();
        auto* radius = cylinder_dialog->findChild<QDoubleSpinBox*>("cylinderRadius");
        auto* height = cylinder_dialog->findChild<QDoubleSpinBox*>("cylinderHeight");
        require(radius != nullptr && height != nullptr,
                "Cylinder dialog does not expose its parameters");
        radius->setValue(17.0);
        height->setValue(63.0);
        cylinder_dialog->buttons()->button(QDialogButtonBox::Ok)->click();
        application.processEvents();
        require(cylinder_commits == 1 &&
                    committed_cylinder.feature_kind ==
                        zima::document::FeatureKind::Cylinder &&
                    committed_cylinder.cylinder.radius == 17.0 &&
                    committed_cylinder.cylinder.height == 63.0,
                "Cylinder Properties did not commit exact parameters");

        auto sphere_initial = zima::document::PartDocument::create_sphere_container();
        zima::document::HistoryContainer committed_sphere;
        auto* sphere_dialog = new zima::app::PrimitivePropertiesDialog(
            sphere_initial, false, false,
            [&](zima::document::HistoryContainer value) {
                committed_sphere = std::move(value);
            }, &parent);
        sphere_dialog->show();
        application.processEvents();
        auto* sphere_radius = sphere_dialog->findChild<QDoubleSpinBox*>("sphereRadius");
        require(sphere_radius != nullptr, "Sphere dialog does not expose its radius");
        sphere_radius->setValue(27.0);
        sphere_dialog->buttons()->button(QDialogButtonBox::Ok)->click();
        application.processEvents();
        require(committed_sphere.feature_kind == zima::document::FeatureKind::Sphere &&
                    committed_sphere.sphere.radius == 27.0,
                "Sphere Properties did not commit its radius");
        const std::vector<zima::kernel::EdgeReference> treatment_edges{
            {"source-feature", "generated:source-edge", {}}};
        for (const auto kind : {zima::document::FeatureKind::Fillet,
                                zima::document::FeatureKind::Chamfer}) {
            auto treatment_initial = kind == zima::document::FeatureKind::Fillet
                ? zima::document::PartDocument::create_fillet_container(treatment_edges)
                : zima::document::PartDocument::create_chamfer_container(treatment_edges);
            zima::document::HistoryContainer committed_treatment;
            auto* treatment_dialog = new zima::app::PrimitivePropertiesDialog(
                treatment_initial, true, false,
                [&](zima::document::HistoryContainer value) {
                    committed_treatment = std::move(value);
                }, &parent);
            treatment_dialog->show();
            application.processEvents();
            auto* treatment_size = treatment_dialog->findChild<QDoubleSpinBox*>(
                "edgeTreatmentSize");
            require(treatment_size != nullptr,
                    "Fillet/Chamfer edit did not reuse Primitive Properties");
            treatment_size->setValue(kind == zima::document::FeatureKind::Fillet
                ? 2.5 : 3.5);
            treatment_dialog->buttons()->button(QDialogButtonBox::Ok)->click();
            application.processEvents();
            require(committed_treatment.feature_kind == kind &&
                        committed_treatment.edge_treatment.edges == treatment_edges &&
                        committed_treatment.edge_treatment.size ==
                            (kind == zima::document::FeatureKind::Fillet ? 2.5 : 3.5),
                    "Fillet/Chamfer Properties lost its input edge identity or size");
        }
        auto cone_initial = zima::document::PartDocument::create_cone_container();
        zima::document::HistoryContainer committed_cone;
        auto* cone_dialog = new zima::app::PrimitivePropertiesDialog(
            cone_initial, false, false,
            [&](zima::document::HistoryContainer value) {
                committed_cone = std::move(value);
            }, &parent);
        cone_dialog->show();
        application.processEvents();
        cone_dialog->findChild<QDoubleSpinBox*>("coneBottomRadius")->setValue(25.0);
        cone_dialog->findChild<QDoubleSpinBox*>("coneTopRadius")->setValue(5.0);
        cone_dialog->findChild<QDoubleSpinBox*>("coneHeight")->setValue(70.0);
        cone_dialog->buttons()->button(QDialogButtonBox::Ok)->click();
        require(committed_cone.feature_kind == zima::document::FeatureKind::Cone &&
                    committed_cone.cone.bottom_radius == 25.0 &&
                    committed_cone.cone.top_radius == 5.0 &&
                    committed_cone.cone.height == 70.0,
                "Cone Properties did not commit exact parameters");

        auto pyramid_initial = zima::document::PartDocument::create_pyramid_container();
        zima::document::HistoryContainer committed_pyramid;
        auto* pyramid_dialog = new zima::app::PrimitivePropertiesDialog(
            pyramid_initial, false, false,
            [&](zima::document::HistoryContainer value) {
                committed_pyramid = std::move(value);
            }, &parent);
        pyramid_dialog->show();
        application.processEvents();
        pyramid_dialog->findChild<QDoubleSpinBox*>("pyramidLength")->setValue(30.0);
        pyramid_dialog->findChild<QDoubleSpinBox*>("pyramidWidth")->setValue(20.0);
        pyramid_dialog->findChild<QDoubleSpinBox*>("pyramidHeight")->setValue(40.0);
        pyramid_dialog->buttons()->button(QDialogButtonBox::Ok)->click();
        require(committed_pyramid.feature_kind == zima::document::FeatureKind::Pyramid &&
                    committed_pyramid.pyramid.length == 30.0 &&
                    committed_pyramid.pyramid.width == 20.0 &&
                    committed_pyramid.pyramid.height == 40.0,
                "Pyramid Properties did not commit exact parameters");

        auto wedge_initial = zima::document::PartDocument::create_wedge_container();
        zima::document::HistoryContainer committed_wedge;
        auto* wedge_dialog = new zima::app::PrimitivePropertiesDialog(
            wedge_initial, false, false,
            [&](zima::document::HistoryContainer value) {
                committed_wedge = std::move(value);
            }, &parent);
        wedge_dialog->show();
        application.processEvents();
        wedge_dialog->findChild<QDoubleSpinBox*>("wedgeLength")->setValue(60.0);
        wedge_dialog->findChild<QDoubleSpinBox*>("wedgeWidth")->setValue(25.0);
        wedge_dialog->findChild<QDoubleSpinBox*>("wedgeHeight")->setValue(35.0);
        wedge_dialog->findChild<QDoubleSpinBox*>("wedgeTopOffset")->setValue(18.0);
        wedge_dialog->buttons()->button(QDialogButtonBox::Ok)->click();
        require(committed_wedge.feature_kind == zima::document::FeatureKind::Wedge &&
                    committed_wedge.wedge.length == 60.0 &&
                    committed_wedge.wedge.width == 25.0 &&
                    committed_wedge.wedge.height == 35.0 &&
                    committed_wedge.wedge.top_offset == 18.0,
                "Wedge Properties did not commit exact parameters");

        auto axis_initial = zima::document::PartDocument::create_construction(
            zima::document::ConstructionKind::Axis);
        zima::document::ConstructionObject committed_axis;
        auto* construction_axis_dialog = new zima::app::ConstructionPropertiesDialog(
            axis_initial, false,
            [&](zima::document::ConstructionObject value) {
                committed_axis = std::move(value);
            }, &parent);
        construction_axis_dialog->show();
        application.processEvents();
        construction_axis_dialog->findChild<QDoubleSpinBox*>("constructionX")->setValue(12.0);
        construction_axis_dialog->findChild<QDoubleSpinBox*>("constructionDirectionX")->setValue(1.0);
        construction_axis_dialog->findChild<QDoubleSpinBox*>("constructionDirectionZ")->setValue(0.0);
        construction_axis_dialog->findChild<QDoubleSpinBox*>("constructionDisplaySize")->setValue(80.0);
        construction_axis_dialog->buttons()->button(QDialogButtonBox::Ok)->click();
        require(committed_axis.kind == zima::document::ConstructionKind::Axis &&
                    committed_axis.origin.x == 12.0 &&
                    committed_axis.direction.x == 1.0 &&
                    committed_axis.display_size == 80.0,
                "Construction Properties did not commit exact parameters");

        auto extrusion_initial =
            zima::document::PartDocument::create_extrusion_container("sketch-profile");
        int extrusion_commits = 0;
        zima::document::HistoryContainer committed_extrusion;
        auto* extrusion_dialog = new zima::app::PrimitivePropertiesDialog(
            extrusion_initial, false, false,
            [&](zima::document::HistoryContainer value) {
                ++extrusion_commits;
                committed_extrusion = std::move(value);
            }, &parent);
        extrusion_dialog->show();
        application.processEvents();
        auto* extrusion_height =
            extrusion_dialog->findChild<QDoubleSpinBox*>("extrusionHeight");
        auto* extrusion_direction =
            extrusion_dialog->findChild<QComboBox*>("extrusionDirection");
        auto* extrusion_extent =
            extrusion_dialog->findChild<QComboBox*>("extrusionExtent");
        require(extrusion_height != nullptr,
                "Extrusion dialog does not expose its height");
        require(extrusion_direction != nullptr,
                "Extrusion dialog does not expose its direction");
        require(extrusion_extent != nullptr,
                "Extrusion dialog does not expose its extent");
        require(extrusion_dialog->findChildren<QDoubleSpinBox*>(
                    "primitiveTranslation").empty(),
                "Extrusion dialog exposes an ignored placement");
        extrusion_height->setValue(48.0);
        extrusion_direction->setCurrentIndex(
            extrusion_direction->findData("symmetric"));
        int preview_updates = 0;
        extrusion_dialog->set_preview_callback(
            [&](const auto&) { ++preview_updates; });
        extrusion_extent->setCurrentIndex(
            extrusion_extent->findData("up_to"));
        extrusion_dialog->set_extrusion_target(
            {"datum-plane", "plane", {}}, {0.0, 0.0, 30.0}, {0.0, 0.0, 1.0});
        extrusion_dialog->buttons()->button(QDialogButtonBox::Ok)->click();
        application.processEvents();
        require(extrusion_commits == 1 &&
                    committed_extrusion.feature_kind ==
                        zima::document::FeatureKind::Extrusion &&
                    committed_extrusion.extrusion.sketch_id == "sketch-profile" &&
                    committed_extrusion.extrusion.height == 48.0 &&
                    committed_extrusion.extrusion.direction ==
                        zima::document::ExtrusionDirection::Forward &&
                    committed_extrusion.extrusion.extent ==
                        zima::document::ExtrusionExtent::UpToPlane &&
                    committed_extrusion.extrusion.target_face.owner_id ==
                        "datum-plane" && preview_updates >= 2,
                "Extrusion Properties did not preserve its Sketch or height");

        auto revolution_initial =
            zima::document::PartDocument::create_revolution_container(
                "sketch-revolution");
        int revolution_commits = 0;
        zima::document::HistoryContainer committed_revolution;
        auto* revolution_dialog = new zima::app::PrimitivePropertiesDialog(
            revolution_initial, false, false,
            [&](zima::document::HistoryContainer value) {
                ++revolution_commits;
                committed_revolution = std::move(value);
            }, &parent);
        revolution_dialog->show();
        application.processEvents();
        auto* revolution_axis =
            revolution_dialog->findChild<QComboBox*>("revolutionAxis");
        auto* revolution_angle =
            revolution_dialog->findChild<QDoubleSpinBox*>("revolutionAngle");
        require(revolution_axis != nullptr && revolution_angle != nullptr,
                "Revolution dialog does not expose its axis and angle");
        require(revolution_dialog->findChildren<QDoubleSpinBox*>(
                    "primitiveTranslation").empty(),
                "Revolution dialog exposes an ignored placement");
        revolution_axis->setCurrentIndex(
            revolution_axis->findData("sketch_y"));
        revolution_angle->setValue(225.0);
        revolution_dialog->buttons()->button(QDialogButtonBox::Ok)->click();
        application.processEvents();
        require(revolution_commits == 1 &&
                    committed_revolution.feature_kind ==
                        zima::document::FeatureKind::Revolution &&
                    committed_revolution.revolution.sketch_id ==
                        "sketch-revolution" &&
                    committed_revolution.revolution.axis ==
                        zima::document::RevolutionAxis::SketchY &&
                    committed_revolution.revolution.angle_degrees == 225.0,
                "Revolution Properties did not preserve exact parameters");

        zima::kernel::BodyResult component_body;
        auto component = zima::assembly::AssemblyDocument::create_part_occurrence(
            "Komponenta", "source-part", "source.prtz", component_body);
        const std::string source_id = component.source_document_id;
        int component_commits = 0;
        zima::assembly::PartOccurrence committed_component;
        auto* component_dialog = new zima::app::ComponentPropertiesDialog(
            component,
            [&](zima::assembly::PartOccurrence value) {
                ++component_commits;
                committed_component = std::move(value);
            }, &parent);
        component_dialog->show();
        application.processEvents();
        const auto component_translations =
            component_dialog->findChildren<QDoubleSpinBox*>("componentTranslation");
        require(component_translations.size() == 3,
                "Component Properties does not expose Assembly-owned placement");
        component_translations.front()->setValue(88.0);
        component_dialog->buttons()->button(QDialogButtonBox::Ok)->click();
        application.processEvents();
        require(component_commits == 1 && committed_component.placement.x == 88.0 &&
                    committed_component.source_document_id == source_id,
                "Component Properties changed source ownership or lost placement");

        auto mate = zima::assembly::AssemblyDocument::create_mate(
            "Vazba", zima::assembly::MateKind::PlaneCoincident,
            {zima::assembly::MateReferenceKind::Face,
             zima::assembly::InstancePath{}.child("dependent"), "box-a", "z_min"},
            {zima::assembly::MateReferenceKind::Face,
             zima::assembly::InstancePath{}.child("prerequisite"), "box-b", "z_max"});
        int mate_commits = 0;
        zima::assembly::AssemblyMate committed_mate;
        auto* mate_dialog = new zima::app::MatePropertiesDialog(
            mate,
            [&](zima::assembly::AssemblyMate value) {
                ++mate_commits;
                committed_mate = std::move(value);
            }, &parent);
        mate_dialog->show();
        application.processEvents();
        auto* mate_offset = mate_dialog->findChild<QDoubleSpinBox*>("mateOffset");
        require(mate_offset != nullptr,
                "Mate Properties does not expose its offset");
        mate_offset->setValue(12.5);
        mate_dialog->buttons()->button(QDialogButtonBox::Ok)->click();
        application.processEvents();
        require(mate_commits == 1 && committed_mate.offset == 12.5 &&
                    committed_mate.dependent == mate.dependent &&
                    committed_mate.prerequisite == mate.prerequisite,
                "Mate Properties changed references or lost its offset");
        auto axis_mate = zima::assembly::AssemblyDocument::create_mate(
            "Osy", zima::assembly::MateKind::AxisCoincident,
            {zima::assembly::MateReferenceKind::Axis,
             zima::assembly::InstancePath{}.child("dependent"), "box-a", "axis:z"},
            {zima::assembly::MateReferenceKind::Axis,
             zima::assembly::InstancePath{}.child("prerequisite"), "box-b", "axis:z"});
        auto* axis_dialog = new zima::app::MatePropertiesDialog(
            axis_mate, [](zima::assembly::AssemblyMate) {}, &parent);
        axis_dialog->show();
        application.processEvents();
        auto* axis_offset = axis_dialog->findChild<QDoubleSpinBox*>("mateOffset");
        require(axis_offset != nullptr && !axis_offset->isEnabled(),
                "Axis mate incorrectly exposed a meaningless axial offset");
        axis_dialog->buttons()->button(QDialogButtonBox::Cancel)->click();
        auto point_mate = zima::assembly::AssemblyDocument::create_mate(
            "Body", zima::assembly::MateKind::PointCoincident,
            {zima::assembly::MateReferenceKind::Point,
             zima::assembly::InstancePath{}.child("dependent"), "box-a", "corner:0"},
            {zima::assembly::MateReferenceKind::Point,
             zima::assembly::InstancePath{}.child("prerequisite"), "box-b", "corner:0"});
        auto* point_dialog = new zima::app::MatePropertiesDialog(
            point_mate, [](zima::assembly::AssemblyMate) {}, &parent);
        point_dialog->show();
        application.processEvents();
        auto* point_offset = point_dialog->findChild<QDoubleSpinBox*>("mateOffset");
        auto* point_flip = point_dialog->findChild<QCheckBox*>("mateFlipped");
        require(point_offset != nullptr && !point_offset->isEnabled() &&
                    point_flip != nullptr && !point_flip->isEnabled(),
                "Point mate exposed meaningless offset or orientation controls");
        point_dialog->buttons()->button(QDialogButtonBox::Cancel)->click();
        auto angle_mate = zima::assembly::AssemblyDocument::create_mate(
            "Úhel", zima::assembly::MateKind::AxisAngle,
            {zima::assembly::MateReferenceKind::Axis,
             zima::assembly::InstancePath{}.child("dependent"), "box-a", "axis:z"},
            {zima::assembly::MateReferenceKind::Axis,
             zima::assembly::InstancePath{}.child("prerequisite"), "box-b", "axis:z"});
        auto* angle_dialog = new zima::app::MatePropertiesDialog(
            angle_mate, [](zima::assembly::AssemblyMate) {}, &parent);
        angle_dialog->show();
        application.processEvents();
        auto* angle_value = angle_dialog->findChild<QDoubleSpinBox*>("mateAngle");
        auto* mate_lower_enabled =
            angle_dialog->findChild<QCheckBox*>("mateLowerEnabled");
        auto* mate_upper_enabled =
            angle_dialog->findChild<QCheckBox*>("mateUpperEnabled");
        auto* mate_lower = angle_dialog->findChild<QDoubleSpinBox*>("mateLowerLimit");
        auto* mate_upper = angle_dialog->findChild<QDoubleSpinBox*>("mateUpperLimit");
        require(angle_value != nullptr && angle_value->isEnabled() &&
                    angle_value->suffix().contains("°") && mate_lower_enabled &&
                    mate_upper_enabled && mate_lower && mate_upper,
                "Axis angle mate does not expose a degree-valued editor");
        angle_value->setValue(60.0);
        mate_lower_enabled->setChecked(true);
        mate_upper_enabled->setChecked(true);
        application.processEvents();
        require(mate_lower->value() == 0.0 && mate_upper->value() == 60.0,
                "New Assembly mate limits did not default to zero and current value");
        angle_dialog->buttons()->button(QDialogButtonBox::Cancel)->click();
        auto plane_angle_mate = zima::assembly::AssemblyDocument::create_mate(
            "Úhel ploch", zima::assembly::MateKind::PlaneAngle,
            {zima::assembly::MateReferenceKind::Face,
             zima::assembly::InstancePath{}.child("dependent"), "box-a", "z_min"},
            {zima::assembly::MateReferenceKind::Face,
             zima::assembly::InstancePath{}.child("prerequisite"), "box-b", "z_max"});
        auto* plane_angle_dialog = new zima::app::MatePropertiesDialog(
            plane_angle_mate, [](zima::assembly::AssemblyMate) {}, &parent);
        plane_angle_dialog->show();
        application.processEvents();
        auto* plane_angle_value =
            plane_angle_dialog->findChild<QDoubleSpinBox*>("mateAngle");
        require(plane_angle_value != nullptr && plane_angle_value->isEnabled(),
                "Plane angle mate does not expose its angle editor");
        plane_angle_dialog->buttons()->button(QDialogButtonBox::Cancel)->click();
        auto sketch = zima::sketcher::Sketch::create_default();
        int sketch_commits = 0;
        auto* sketch_dialog = new zima::app::SketchPropertiesDialog(
            sketch, false, [&](zima::sketcher::Sketch committed) {
                ++sketch_commits;
                sketch = std::move(committed);
            }, &parent);
        sketch_dialog->show();
        require(sketch_dialog->windowFlags().testFlag(Qt::SubWindow),
                "Sketch Properties is not an internal SubWindow");
        auto* sketch_offset =
            sketch_dialog->findChild<QDoubleSpinBox*>("sketchPlaneOffset");
        require(sketch_offset != nullptr,
                "Sketch Properties does not expose its plane offset");
        sketch_offset->setValue(12.5);
        sketch_dialog->buttons()->button(QDialogButtonBox::Ok)->click();
        require(sketch_commits == 1 && sketch.plane_offset == 12.5,
                "Sketch Properties did not commit exactly once on OK");
        application.processEvents();

        auto initial_text = zima::sketcher::Sketch::create_text();
        initial_text.value = "O";
        int text_commits = 0;
        zima::sketcher::SketchText committed_text;
        auto* text_dialog = new zima::app::SketchTextPropertiesDialog(
            initial_text, std::array{5.0, 7.0},
            [](const std::optional<zima::sketcher::SketchText>&) {},
            [&](zima::sketcher::SketchText value) {
                ++text_commits;
                committed_text = std::move(value);
            }, &parent);
        text_dialog->show();
        application.processEvents();
        auto* text_value =
            text_dialog->findChild<QPlainTextEdit*>("sketchTextValue");
        require(text_value && text_value->toPlainText() == QStringLiteral("O") &&
                    text_dialog->windowFlags().testFlag(Qt::SubWindow),
                "Editable Sketch Text did not reopen as a semantic internal dialog");
        text_value->setPlainText(QStringLiteral("OI"));
        text_dialog->buttons()->button(QDialogButtonBox::Ok)->click();
        application.processEvents();
        require(text_commits == 1 && committed_text.value == "OI" &&
                    !committed_text.contours.empty(),
                "Sketch Text OK did not retain text and calculate profile contours");

        auto* cancel_text_dialog = new zima::app::SketchTextPropertiesDialog(
            committed_text, std::nullopt,
            [](const std::optional<zima::sketcher::SketchText>&) {},
            [&](zima::sketcher::SketchText) { ++text_commits; }, &parent);
        cancel_text_dialog->show();
        application.processEvents();
        cancel_text_dialog->findChild<QPlainTextEdit*>("sketchTextValue")
            ->setPlainText(QStringLiteral("discarded"));
        cancel_text_dialog->buttons()->button(QDialogButtonBox::Cancel)->click();
        application.processEvents();
        require(text_commits == 1,
                "Cancel committed an edited semantic Sketch Text");

        zima::sketcher::SketchDimension dimension{
            "dimension", zima::sketcher::DimensionKind::Distance,
            "first", "second", 20.0};
        int dimension_commits = 0;
        zima::sketcher::SketchDimension committed_dimension;
        auto* dimension_dialog = new zima::app::SketchDimensionPropertiesDialog(
            dimension, false, [&](zima::sketcher::SketchDimension committed) {
                ++dimension_commits;
                committed_dimension = std::move(committed);
            }, &parent);
        dimension_dialog->show();
        auto* lower_enabled =
            dimension_dialog->findChild<QCheckBox*>("sketchLowerEnabled");
        auto* upper_enabled =
            dimension_dialog->findChild<QCheckBox*>("sketchUpperEnabled");
        auto* lower_limit =
            dimension_dialog->findChild<QDoubleSpinBox*>("sketchLowerLimit");
        auto* upper_limit =
            dimension_dialog->findChild<QDoubleSpinBox*>("sketchUpperLimit");
        require(lower_enabled && upper_enabled && lower_limit && upper_limit,
                "Sketch dimension Properties does not expose independent limits");
        lower_enabled->setChecked(true);
        upper_enabled->setChecked(true);
        require(lower_limit->value() == 0.0 && upper_limit->value() == 20.0,
                "New dimension limits did not default to zero and nominal value");
        dimension_dialog->buttons()->button(QDialogButtonBox::Ok)->click();
        require(dimension_commits == 1 && committed_dimension.lower_limit == 0.0 &&
                    committed_dimension.upper_limit == 20.0,
                "Sketch dimension Properties did not commit its absolute limits");

        const auto file_dialog_directory = std::filesystem::temp_directory_path() /
            ("zima-cad-file-dialog-contract-" + std::to_string(
                std::chrono::steady_clock::now().time_since_epoch().count()));
        std::filesystem::create_directories(file_dialog_directory / "0000-index");
        std::filesystem::create_directories(file_dialog_directory / "visible-folder");
        std::ofstream(file_dialog_directory / "visible.prtz") << "{}";
        std::ofstream(file_dialog_directory / "0000-index.prtz") << "{}";
        bool file_dialog_inspected = false;
        bool file_dialog_contents_valid = false;
        int file_dialog_attempts = 0;
        QTimer file_dialog_probe;
        file_dialog_probe.setInterval(10);
        QObject::connect(&file_dialog_probe, &QTimer::timeout, [&] {
            auto* dialog = qobject_cast<QFileDialog*>(QApplication::activeModalWidget());
            if (dialog == nullptr) return;
            if (++file_dialog_attempts > 200) {
                file_dialog_inspected = true;
                dialog->reject();
                return;
            }
            auto* proxy = dialog->proxyModel();
            auto* files = proxy == nullptr
                ? nullptr : qobject_cast<QFileSystemModel*>(proxy->sourceModel());
            if (files == nullptr) return;
            const auto source_root = files->index(
                QString::fromStdString(file_dialog_directory.string()));
            if (!source_root.isValid()) return;
            const auto root = proxy->mapFromSource(source_root);
            QStringList names;
            for (int row = 0; row < proxy->rowCount(root); ++row)
                names.push_back(proxy->index(row, 0, root).data().toString());
            if (!names.contains("visible-folder") || !names.contains("visible.prtz") ||
                !names.contains("0000-index.prtz")) return;
            const bool hidden_index = !names.contains("0000-index");
            proxy->sort(0, Qt::DescendingOrder);
            const auto sorted_root = proxy->mapFromSource(source_root);
            bool file_seen = false;
            bool directories_first = true;
            for (int row = 0; row < proxy->rowCount(sorted_root); ++row) {
                const bool directory = files->fileInfo(
                    proxy->mapToSource(proxy->index(row, 0, sorted_root))).isDir();
                if (!directory) file_seen = true;
                else if (file_seen) directories_first = false;
            }
            file_dialog_contents_valid = hidden_index && directories_first;
            file_dialog_inspected = true;
            dialog->reject();
        });
        file_dialog_probe.start();
        QTimer file_dialog_timeout;
        file_dialog_timeout.setSingleShot(true);
        QObject::connect(&file_dialog_timeout, &QTimer::timeout, [] {
            if (auto* dialog = qobject_cast<QFileDialog*>(
                    QApplication::activeModalWidget())) dialog->reject();
        });
        file_dialog_timeout.start(3000);
        const auto selected_file = zima::app::open_file(
            &parent, "File dialog contract",
            QString::fromStdString(file_dialog_directory.string()),
            "ZIMA-CAD Part (*.prtz)");
        file_dialog_probe.stop();
        file_dialog_timeout.stop();
        std::filesystem::remove_all(file_dialog_directory);
        require(selected_file.isEmpty(),
                "Rejected file dialog unexpectedly returned a selection");
        require(file_dialog_inspected && file_dialog_contents_valid,
                "File dialog did not hide 0000-index or keep directories first");

        std::cout << "C++ properties-window contracts passed\n";
        return 0;
    } catch (const std::exception& error) {
        std::cerr << error.what() << '\n';
        return 1;
    }
}
