#include "assembly_workspace_window.hpp"

#include <QAction>
#include <QApplication>
#include <QDialog>
#include <QDialogButtonBox>
#include <QEvent>
#include <QMenu>
#include <QPushButton>
#include <QStackedWidget>
#include <QSurfaceFormat>
#include <QTabBar>
#include <QToolBar>
#include <QTreeWidget>

#include <iostream>

namespace {

bool verify(bool condition, const char* message) {
    if (!condition) std::cerr << "Startup contract failed: " << message << '\n';
    return condition;
}

int verify_startup_contract(
    QApplication& application, zima::app::AssemblyWorkspaceWindow& window) {
    window.show();
    application.processEvents();

    auto* tabs = window.findChild<QTabBar*>("documentTabs");
    auto* tree = window.findChild<QTreeWidget*>("documentTree");
    auto* part_toolbar = window.findChild<QToolBar*>("partToolbar");
    auto* assembly_toolbar = window.findChild<QToolBar*>("assemblyToolbar");
    auto* box = window.findChild<QAction*>("boxAction");
    auto* new_assembly = window.findChild<QAction*>("newAssemblyAction");
    auto* new_drawing = window.findChild<QAction*>("newDrawingAction");
    if (!verify(tabs != nullptr && tree != nullptr, "document navigation is missing") ||
        !verify(part_toolbar != nullptr && assembly_toolbar != nullptr,
                "workspace toolbars are missing") ||
        !verify(box != nullptr && new_assembly != nullptr && new_drawing != nullptr,
                "primary actions are missing") ||
        !verify(tabs->count() == 1 && tabs->tabText(0) == QStringLiteral("Nový díl"),
                "application must start with one new Part") ||
        !verify(box->isEnabled() && !part_toolbar->isHidden() &&
                    assembly_toolbar->isHidden(),
                "Part commands must be visible and enabled at startup")) {
        return 1;
    }

    box->trigger();
    application.processEvents();
    auto* properties = window.findChild<QDialog*>("zimaPropertiesSubWindow");
    auto* buttons = properties == nullptr
        ? nullptr : properties->findChild<QDialogButtonBox*>();
    if (!verify(buttons != nullptr, "Box must open the shared Properties window")) {
        return 1;
    }
    buttons->button(QDialogButtonBox::Ok)->click();
    application.processEvents();
    QCoreApplication::sendPostedEvents(nullptr, QEvent::DeferredDelete);
    application.processEvents();
    if (!verify(tree->topLevelItemCount() == 1 &&
                    tree->topLevelItem(0)->childCount() == 1,
                "confirming Box must create the first Part history item")) {
        return 1;
    }

    new_assembly->trigger();
    application.processEvents();
    auto* insert = window.findChild<QAction*>("insertComponentAction");
    auto* insert_menu = window.findChild<QMenu*>("insertComponentMenu");
    if (!verify(tabs->count() == 2 && !assembly_toolbar->isHidden(),
                "New Assembly must become a visible second document") ||
        !verify(insert != nullptr && insert->isEnabled() && insert_menu != nullptr,
                "calculated open Part must be insertable into the Assembly")) {
        return 1;
    }
    QAction* source_action{};
    for (auto* action : insert_menu->actions()) {
        if (action->objectName() == QStringLiteral("insertSourceAction") &&
            action->isEnabled()) {
            source_action = action;
            break;
        }
    }
    if (!verify(source_action != nullptr, "Assembly insertion has no Part source")) {
        return 1;
    }
    source_action->trigger();
    application.processEvents();
    if (!verify(tree->topLevelItemCount() == 1 &&
                    tree->topLevelItem(0)->childCount() == 1,
                "inserting the open Part must create an Assembly occurrence")) {
        return 1;
    }

    new_drawing->trigger();
    application.processEvents();
    auto* stack = window.findChild<QStackedWidget*>("workspaceStack");
    auto* drawing_toolbar = window.findChild<QToolBar*>("drawingToolbar");
    auto* insert_view = window.findChild<QAction*>("insertDrawingViewAction");
    if (!verify(tabs->count() == 3 && stack != nullptr &&
                    stack->currentWidget()->objectName() == QStringLiteral("drawingWorkspace"),
                "New Drawing must open inside the common workspace") ||
        !verify(drawing_toolbar != nullptr && !drawing_toolbar->isHidden() &&
                    insert_view != nullptr && insert_view->isEnabled(),
                "Drawing commands must be visible and ready for a model view")) {
        return 1;
    }
    return 0;
}

}  // namespace

int main(int argc, char* argv[]) {
    QSurfaceFormat format;
    format.setVersion(3, 3);
    format.setProfile(QSurfaceFormat::CoreProfile);
    QSurfaceFormat::setDefaultFormat(format);
    QApplication application(argc, argv);
    application.setApplicationName("ZIMA-CAD");
    zima::app::AssemblyWorkspaceWindow window;
    if (application.arguments().contains("--verify-startup")) {
        return verify_startup_contract(application, window);
    }
    window.show();
    return application.exec();
}
