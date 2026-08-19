#include "primitive_properties_dialog.hpp"
#include "component_properties_dialog.hpp"
#include "mate_properties_dialog.hpp"
#include "sketch_properties_dialog.hpp"
#include "sketch_dimension_properties_dialog.hpp"

#include <QApplication>
#include <QDialogButtonBox>
#include <QDoubleSpinBox>
#include <QComboBox>
#include <QCheckBox>
#include <QMouseEvent>
#include <QPushButton>
#include <QWidget>

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
        require(extrusion_height != nullptr,
                "Extrusion dialog does not expose its height");
        require(extrusion_direction != nullptr,
                "Extrusion dialog does not expose its direction");
        require(extrusion_dialog->findChildren<QDoubleSpinBox*>(
                    "primitiveTranslation").empty(),
                "Extrusion dialog exposes an ignored placement");
        extrusion_height->setValue(48.0);
        extrusion_direction->setCurrentIndex(
            extrusion_direction->findData("symmetric"));
        extrusion_dialog->buttons()->button(QDialogButtonBox::Ok)->click();
        application.processEvents();
        require(extrusion_commits == 1 &&
                    committed_extrusion.feature_kind ==
                        zima::document::FeatureKind::Extrusion &&
                    committed_extrusion.extrusion.sketch_id == "sketch-profile" &&
                    committed_extrusion.extrusion.height == 48.0 &&
                    committed_extrusion.extrusion.direction ==
                        zima::document::ExtrusionDirection::Symmetric,
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
            "Komponenta", "source-part", "source.zcp.json", component_body);
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

        std::cout << "C++ properties-window contracts passed\n";
        return 0;
    } catch (const std::exception& error) {
        std::cerr << error.what() << '\n';
        return 1;
    }
}
