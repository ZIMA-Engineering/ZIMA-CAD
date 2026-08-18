#include "primitive_properties_dialog.hpp"
#include "component_properties_dialog.hpp"

#include <QApplication>
#include <QDialogButtonBox>
#include <QDoubleSpinBox>
#include <QComboBox>
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

        std::cout << "C++ properties-window contracts passed\n";
        return 0;
    } catch (const std::exception& error) {
        std::cerr << error.what() << '\n';
        return 1;
    }
}
