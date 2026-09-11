#include "workspace_internal.hpp"

namespace zima::app {
using namespace workspace_detail;


void AssemblyWorkspaceWindow::create_actions() {
    const auto t = [this](const char* key, const char* fallback) {
        return application_settings_.text(
            QString::fromLatin1(key), QString::fromUtf8(fallback));
    };
    const auto make_action = [this](const QString& text, const char* icon = nullptr) {
        auto* action = new QAction(text, this);
        if (icon != nullptr) action->setIcon(resource_icon(QString::fromLatin1(icon)));
        return action;
    };

    auto* file = menuBar()->addMenu(t("menu.file", "Soubor"));
    new_document_action_ = make_action(t("menu.file.new", "Nový"), "new");
    new_document_action_->setObjectName("newDocumentAction");
    new_document_action_->setShortcut(QKeySequence::New);
    connect(new_document_action_, &QAction::triggered, this,
        [this] { new_document(); });
    file->addAction(new_document_action_);
    open_document_action_ = make_action(t("menu.file.open", "Otevřít..."), "open");
    open_document_action_->setObjectName("openDocumentAction");
    open_document_action_->setShortcut(QKeySequence::Open);
    connect(open_document_action_, &QAction::triggered, this,
        [this] { open_document(); });
    file->addAction(open_document_action_);
    auto* import_action = make_action(t("menu.file.import", "Importovat…"), "open");
    import_action->setObjectName("importDocumentAction");
    connect(import_action, &QAction::triggered, this, [this] { import_file(); });
    file->addAction(import_action);
    export_action_ = make_action(t("menu.file.export", "Exportovat…"), "save");
    export_action_->setObjectName("exportDocumentAction");
    connect(export_action_, &QAction::triggered, this, [this] { export_file(); });
    file->addAction(export_action_);
    close_document_action_ = make_action(t("menu.file.close", "Zavřít"));
    close_document_action_->setObjectName("closeDocumentAction");
    close_document_action_->setShortcut(QKeySequence(QStringLiteral("F2")));
    connect(close_document_action_, &QAction::triggered, this,
        [this] { close_document(); });
    file->addAction(close_document_action_);
    save_action_ = make_action(t("menu.file.save", "Uložit"), "save");
    save_action_->setObjectName("saveDocumentAction");
    save_action_->setShortcuts({QKeySequence::Save,
                                QKeySequence(QStringLiteral("F1"))});
    save_action_->setShortcutContext(Qt::ApplicationShortcut);
    connect(save_action_, &QAction::triggered, this,
        [this] { save_active_document(); });
    file->addAction(save_action_);
    save_as_action_ = make_action(
        t("menu.file.save_as", "Uložit jako..."), "save-as");
    save_as_action_->setObjectName("saveDocumentAsAction");
    save_as_action_->setToolTip(tr("Uložit kopii modelu včetně navázaného výkresu; původní dokument zůstane otevřený."));
    save_as_action_->setShortcut(QKeySequence::SaveAs);
    save_as_action_->setEnabled(false);
    connect(save_as_action_, &QAction::triggered, this,
        [this] { save_active_document_as(); });
    file->addAction(save_as_action_);
    rename_document_action_ = make_action(
        t("menu.file.rename", "Přejmenovat…"));
    rename_document_action_->setObjectName("renameDocumentAction");
    rename_document_action_->setEnabled(false);
    connect(rename_document_action_, &QAction::triggered, this,
        [this] { rename_document_file(); });
    file->addAction(rename_document_action_);

    delete_file_menu_ = file->addMenu(
        t("menu.file.delete", "Odstranit"));
    delete_file_menu_->setObjectName("deleteFileMenu");
    delete_current_file_action_ = delete_file_menu_->addAction(
        resource_icon("delete"),
        t("menu.file.delete.current_file", "Aktuální soubor"));
    delete_current_file_action_->setObjectName("deleteCurrentFileAction");
    connect(delete_current_file_action_, &QAction::triggered, this,
        [this] { delete_current_document_file(); });
    delete_all_versions_action_ = delete_file_menu_->addAction(
        t("menu.file.delete.current_file_and_versions",
          "Aktuální soubor a všechny verze"));
    delete_all_versions_action_->setObjectName("deleteAllVersionsAction");
    connect(delete_all_versions_action_, &QAction::triggered, this,
        [this] { delete_all_file_versions(); });
    delete_file_menu_->addSeparator();
    delete_old_versions_action_ = delete_file_menu_->addAction(
        t("menu.file.delete.old_versions", "Staré verze"));
    delete_old_versions_action_->setObjectName("deleteOldVersionsAction");
    connect(delete_old_versions_action_, &QAction::triggered, this,
        [this] { delete_old_file_versions(); });
    delete_old_versions_keep_latest_action_ = delete_file_menu_->addAction(
        t("menu.file.delete.old_versions_keep_latest",
          "Staré verze kromě nejnovější"));
    delete_old_versions_keep_latest_action_->setObjectName(
        "deleteOldVersionsKeepLatestAction");
    connect(delete_old_versions_keep_latest_action_, &QAction::triggered, this,
        [this] { delete_old_file_versions_keep_latest(); });
    delete_file_menu_->addSeparator();
    delete_working_directory_menu_ = delete_file_menu_->addMenu(
        t("menu.file.delete.working_directory", "Pracovní adresář"));
    delete_working_directory_menu_->setObjectName("deleteWorkingDirectoryMenu");
    delete_working_directory_old_versions_action_ =
        delete_working_directory_menu_->addAction(
            t("menu.file.delete.working_directory_old_versions",
              "Odstranit staré verze"));
    delete_working_directory_old_versions_action_->setObjectName(
        "deleteWorkingDirectoryOldVersionsAction");
    connect(delete_working_directory_old_versions_action_, &QAction::triggered,
        this, [this] { delete_working_directory_old_versions(); });
    delete_working_directory_keep_latest_action_ =
        delete_working_directory_menu_->addAction(
            t("menu.file.delete.working_directory_keep_latest",
              "Ponechat nejnovější verzi"));
    delete_working_directory_keep_latest_action_->setObjectName(
        "deleteWorkingDirectoryKeepLatestAction");
    connect(delete_working_directory_keep_latest_action_, &QAction::triggered,
        this, [this] { delete_working_directory_old_versions_keep_latest(); });
    for (auto* action : {delete_current_file_action_, delete_all_versions_action_,
                         delete_old_versions_action_,
                         delete_old_versions_keep_latest_action_,
                         delete_working_directory_old_versions_action_,
                         delete_working_directory_keep_latest_action_}) {
        action->setEnabled(false);
    }
    file->addSeparator();
    working_directory_action_ = make_action(
        t("menu.file.working_directory", "Nastavit pracovní adresář..."));
    working_directory_action_->setObjectName("workingDirectoryAction");
    connect(working_directory_action_, &QAction::triggered, this,
        [this] { set_working_directory(); });
    file->addAction(working_directory_action_);

    auto* edit = menuBar()->addMenu(t("menu.edit", "Upravit"));
    regenerate_document_action_ = make_action(tr("Regenerovat"));
    regenerate_document_action_->setObjectName("regenerateDocumentAction");
    regenerate_document_action_->setShortcut(QKeySequence(QStringLiteral("F5")));
    regenerate_document_action_->setToolTip(
        tr("Přepočítá aktivní dokument a jeho otevřené závislosti (F5)"));
    connect(regenerate_document_action_, &QAction::triggered, this,
        [this] { regenerate_active_document(); });
    edit->addAction(regenerate_document_action_);
    edit->addSeparator();
    undo_action_ = make_action(tr("Zpět"), "undo");
    redo_action_ = make_action(tr("Znovu"), "redo");
    undo_action_->setObjectName("undoAction");
    redo_action_->setObjectName("redoAction");
    connect(undo_action_, &QAction::triggered, this, [this] { undo(); });
    connect(redo_action_, &QAction::triggered, this, [this] { redo(); });
    edit->addAction(undo_action_);
    edit->addAction(redo_action_);

    fit_view_action_ = make_action(tr("Obnovit pohled"), "view-fit");
    fit_view_action_->setObjectName("fitViewAction");
    connect(fit_view_action_, &QAction::triggered, this, [this] {
        if(workspace_.open_drawing(workspace_.displayed_document_id())) drawing_workspace_->fit_sheet();
        else if (viewer_ != nullptr) viewer_->fit_all();
    });
    auto* normal_view_action = make_action(tr("Pohled kolmo"), "view-normal");
    normal_view_action->setObjectName("normalViewAction");
    normal_view_action->setToolTip(
        tr("Vyberte plochu ve 3D pohledu – natočí pohled kolmo k ní"));
    connect(normal_view_action, &QAction::triggered, this,
        [this] { show_orientation_dialog(); });
    selection_action_ = make_action(tr("Výběr"), "select");
    selection_action_->setObjectName("viewSelectionAction");
    selection_action_->setCheckable(true);
    selection_action_->setChecked(true);
    connect(selection_action_, &QAction::toggled, this, [this](bool enabled) {
        if (viewer_ == nullptr) return;
        if (enabled) refresh_scene();
        else viewer_->set_selection_contract({});
    });
    connect(selection_action_, &QAction::triggered, this, [this] {
        if (active_sketch_id_.empty()) return;
        // "Výběr" is a command switch, not a persistent on/off visibility
        // toggle.  Clicking it always finishes the current Sketch tool and
        // restores ordinary click and rectangle selection.
        static_cast<void>(finish_current_sketch_tool());
        const QSignalBlocker blocker(selection_action_);
        selection_action_->setChecked(true);
        sync_sketch_tool_action_checks();
        preserve_view_on_refresh_ = true;
        refresh_scene();
    });

    display_mode_group_ = new QActionGroup(this);
    display_mode_group_->setExclusive(true);
    const auto display_action = [this, &make_action](
        const QString& text, zima::viewer::DisplayMode mode) {
        auto* action = make_action(text);
        action->setCheckable(true);
        display_mode_group_->addAction(action);
        connect(action, &QAction::triggered, this, [this, mode] {
            if (viewer_ != nullptr) viewer_->set_display_mode(mode);
        });
        return action;
    };
    wire_action_ = display_action(tr("Drátový"), zima::viewer::DisplayMode::Wire);
    hidden_edges_action_ = display_action(
        tr("Skryté hrany"), zima::viewer::DisplayMode::HiddenEdges);
    no_hidden_edges_action_ = display_action(
        tr("Bez skrytých hran"), zima::viewer::DisplayMode::NoHiddenEdges);
    shaded_edges_action_ = display_action(
        tr("Stínovaný s hranami"), zima::viewer::DisplayMode::ShadedWithEdges);
    shaded_action_ = display_action(
        tr("Stínovaný"), zima::viewer::DisplayMode::Shaded);
    wire_action_->setObjectName("wireDisplayAction");
    hidden_edges_action_->setObjectName("hiddenEdgesDisplayAction");
    no_hidden_edges_action_->setObjectName("noHiddenEdgesDisplayAction");
    shaded_edges_action_->setObjectName("shadedEdgesDisplayAction");
    shaded_action_->setObjectName("shadedDisplayAction");
    shaded_edges_action_->setChecked(true);

    camera_projection_group_ = new QActionGroup(this);
    camera_projection_group_->setExclusive(true);
    const auto projection_action = [this, &make_action](const QString& text,
            const char* icon, const QString& tooltip,
            zima::viewer::ProjectionMode mode) {
        auto* action = make_action(text, icon);
        action->setCheckable(true);
        action->setToolTip(tooltip);
        camera_projection_group_->addAction(action);
        connect(action, &QAction::triggered, this, [this, mode] {
            if (viewer_ != nullptr) viewer_->set_projection_mode(mode);
        });
        return action;
    };
    orthographic_camera_action_ = projection_action(tr("Orto"),
        "view-orthographic",
        tr("Ortografický pohled – současné CAD zobrazení"),
        zima::viewer::ProjectionMode::Orthographic);
    perspective_camera_action_ = projection_action(tr("Perspektiva"),
        "view-perspective",
        tr("Perspektivní pohled s otáčením kolem modelu"),
        zima::viewer::ProjectionMode::Perspective);
    fly_camera_action_ = make_action(tr("Průlet"), "view-fly");
    fly_camera_action_->setCheckable(true);
    fly_camera_action_->setToolTip(tr(
        "Zapnout průlet nezávisle na Orto/Perspektivě: prostřední tlačítko "
        "rozhlížení, kolečko nebo W/S vpřed a vzad, A/D do stran, Q/E svisle"));
    connect(fly_camera_action_, &QAction::toggled, this, [this](bool enabled) {
        if (viewer_ != nullptr) viewer_->set_fly_navigation_enabled(enabled);
    });
    orthographic_camera_action_->setObjectName("orthographicCameraAction");
    perspective_camera_action_->setObjectName("perspectiveCameraAction");
    fly_camera_action_->setObjectName("flyCameraAction");
    orthographic_camera_action_->setChecked(true);

    const auto reference_action = [this, &make_action](
        const QString& text, const char* icon,
        zima::viewer::ReferenceVisibility reference) {
        auto* action = make_action(text, icon);
        action->setCheckable(true);
        action->setChecked(true);
        connect(action, &QAction::toggled, this, [this, reference](bool visible) {
            if (viewer_ != nullptr) {
                viewer_->set_reference_visibility(reference, visible);
            }
        });
        return action;
    };
    show_origins_action_ = reference_action(
        tr("Počátky"), "origin", zima::viewer::ReferenceVisibility::Origins);
    show_points_action_ = reference_action(
        tr("Body"), "point", zima::viewer::ReferenceVisibility::Points);
    show_axes_action_ = reference_action(
        tr("Osy"), "axis", zima::viewer::ReferenceVisibility::Axes);
    show_planes_action_ = reference_action(
        tr("Roviny"), "plane", zima::viewer::ReferenceVisibility::Planes);
    show_sketches_action_ = reference_action(
        tr("Skici"), "sketch", zima::viewer::ReferenceVisibility::Sketches);
    show_dimensions_action_ = reference_action(tr("Kóty"), "sketch-dimensions", zima::viewer::ReferenceVisibility::Dimensions);
    show_dimensions_action_->setObjectName("showDimensionsAction");
    show_dimensions_action_->setToolTip(tr("Zobrazit nebo skrýt kóty v modelovém pohledu"));
    show_origins_action_->setObjectName("showOriginsAction");
    show_points_action_->setObjectName("showPointsAction");
    show_axes_action_->setObjectName("showAxesAction");
    show_planes_action_->setObjectName("showPlanesAction");
    show_sketches_action_->setObjectName("showSketchesAction");

    auto* view = menuBar()->addMenu(t("menu.view", "Zobrazení"));
    view->setObjectName("viewMenu");
    view->addAction(fit_view_action_);
    auto* dimension_frame=view->addAction(tr("Prostorový rám kót"));
    dimension_frame->setObjectName("showDimensionFrameAction");dimension_frame->setCheckable(true);
    connect(dimension_frame,&QAction::toggled,this,[this](bool shown){if(viewer_)viewer_->set_dimension_frame_visible(shown);});
    standard_views_menu_ = view->addMenu(
        t("toolbar.standard_views", "Základní pohledy"));
    standard_views_menu_->setObjectName("standardViewsMenu");
    const auto add_standard_view = [this](
        const QString& text, zima::viewer::StandardView standard_view) {
        auto* action = standard_views_menu_->addAction(text);
        connect(action, &QAction::triggered, this, [this, standard_view] {
            if (viewer_ != nullptr) viewer_->set_standard_view(standard_view);
        });
    };
    add_standard_view(t("toolbar.view.default", "Výchozí – izometrický"), zima::viewer::StandardView::Isometric);
    add_standard_view(t("toolbar.view.front", "Front – XZ"), zima::viewer::StandardView::Front);
    add_standard_view(t("toolbar.view.back", "Back – XZ opačně"), zima::viewer::StandardView::Back);
    add_standard_view(t("toolbar.view.left", "Left – YZ"), zima::viewer::StandardView::Left);
    add_standard_view(t("toolbar.view.right", "Right – YZ opačně"), zima::viewer::StandardView::Right);
    add_standard_view(t("toolbar.view.top", "Top – XY"), zima::viewer::StandardView::Top);
    add_standard_view(t("toolbar.view.bottom", "Bottom – XY opačně"), zima::viewer::StandardView::Bottom);
    view->addSeparator();
    view->addAction(selection_action_);
    view->addSeparator();
    for (auto* action : {orthographic_camera_action_,
                         perspective_camera_action_, fly_camera_action_}) {
        view->addAction(action);
    }
    view->addSeparator();
    for (auto* action : {wire_action_, hidden_edges_action_, no_hidden_edges_action_,
                         shaded_edges_action_, shaded_action_}) {
        view->addAction(action);
    }
    view->addSeparator();
    for (auto* action : {show_origins_action_, show_points_action_, show_axes_action_,
                         show_planes_action_, show_sketches_action_, show_dimensions_action_}) {
        view->addAction(action);
    }
    view->addSeparator();
    colors_menu_ = view->addMenu(t("menu.view.colors", "Barvy"));
    colors_menu_->setObjectName("colorsMenu");
    custom_body_color_action_ = colors_menu_->addAction(tr("Barvy a vzhled…"));
    custom_body_color_action_->setObjectName("customBodyColorAction");
    custom_body_color_action_->setIcon(resource_icon("appearance"));
    connect(custom_body_color_action_, &QAction::triggered, this,
        &AssemblyWorkspaceWindow::show_body_color_dialog);
    update_body_color_actions();

    auto* applications = menuBar()->addMenu(t("menu.applications", "Aplikace"));
    application_group_ = new QActionGroup(this);
    application_group_->setExclusive(true);
    const std::array<QString, 6> application_names{
        t("application.modeling", "Modelování"),
        t("application.assembly", "Sestava"),
        t("application.sheet_metal", "Plech"),
        t("application.surface", "Plochy"),
        t("application.piping", "Potrubí"),
        t("application.drawing", "Výkres")};
    for (std::size_t index = 0; index < application_actions_.size(); ++index) {
        auto* action = applications->addAction(application_names[index]);
        action->setObjectName(
            QStringLiteral("applicationModeAction%1").arg(index));
        action->setCheckable(true);
        action->setData(static_cast<int>(index));
        application_group_->addAction(action);
        connect(action, &QAction::triggered, this, [this, index] {
            set_active_application(static_cast<ApplicationMode>(index));
        });
        application_actions_[index] = action;
    }
    application_actions_[0]->setChecked(true);

    auto* tools = menuBar()->addMenu(t("menu.tools", "Nástroje"));
    material_action_ = tools->addAction(t("menu.tools.material", "Materiál..."));
    material_action_->setObjectName("materialAction");
    connect(material_action_, &QAction::triggered, this, &AssemblyWorkspaceWindow::edit_material);
    parameters_action_ = tools->addAction(
        t("menu.tools.parameters", "Parametry..."));
    parameters_action_->setObjectName("documentParametersAction");
    parameters_action_->setIcon(resource_icon("parameters"));
    connect(parameters_action_, &QAction::triggered,
        this, &AssemblyWorkspaceWindow::edit_document_parameters);
    relations_action_ = tools->addAction(t("menu.tools.relations", "Relace..."));
    relations_action_->setObjectName("relationsAction");
    connect(relations_action_, &QAction::triggered, this, &AssemblyWorkspaceWindow::edit_relations);
    family_table_action_ = tools->addAction(t("menu.tools.family_table", "Family Table..."));
    family_table_action_->setObjectName("familyTableAction");
    connect(family_table_action_, &QAction::triggered, this, &AssemblyWorkspaceWindow::edit_family_table);
    tools->addSeparator();
    file_settings_action_ = tools->addAction(
        t("menu.tools.file_settings", "Nastavení souboru..."));
    file_settings_action_->setObjectName("fileSettingsAction");
    connect(file_settings_action_, &QAction::triggered, this, &AssemblyWorkspaceWindow::edit_file_settings);
    settings_action_ = make_action(
        t("menu.tools.global_settings", "Globální nastavení..."), "settings");
    settings_action_->setObjectName("globalSettingsAction");
    connect(settings_action_, &QAction::triggered,
        this, &AssemblyWorkspaceWindow::show_global_settings);
    tools->addAction(settings_action_);
    auto* window_menu = menuBar()->addMenu(t("menu.window", "Okno"));
    window_menu->setObjectName("windowMenu");
    connect(window_menu, &QMenu::aboutToShow, this, [this, window_menu] {
        window_menu->clear();
        auto* new_window = window_menu->addAction(application_settings_.text(
            "menu.window.new_window", tr("Nové okno")));
        new_window->setObjectName("newWindowAction");
        connect(new_window, &QAction::triggered,
            this, &AssemblyWorkspaceWindow::open_new_window);
        window_menu->addSeparator();
        if (tabs_ == nullptr || tabs_->count() == 0) {
            auto* empty = window_menu->addAction(application_settings_.text(
                "status.no_open_documents", tr("Není otevřen žádný dokument")));
            empty->setEnabled(false);
            return;
        }
        for (int index = 0; index < tabs_->count(); ++index) {
            auto* action = window_menu->addAction(tabs_->tabIcon(index), tabs_->tabText(index));
            action->setCheckable(true);
            action->setChecked(index == tabs_->currentIndex());
            connect(action, &QAction::triggered, this, [this, index] {
                if (tabs_ != nullptr && index >= 0 && index < tabs_->count()) {
                    tabs_->setCurrentIndex(index);
                }
            });
        }
    });
    auto* help = menuBar()->addMenu(t("menu.help", "Nápověda"));
    auto* about_action = help->addAction(
        t("menu.help.about", "O aplikaci ZIMA-CAD"));
    about_action->setObjectName("aboutAction");
    connect(about_action, &QAction::triggered, this, [this] { show_about(); });

    box_action_ = make_action(tr("Kvádr"), "box");
    box_action_->setObjectName("boxAction");
    cylinder_action_ = make_action(tr("Válec"), "cylinder");
    thread_action_ = make_action(tr("Otvor"), "cylinder");
    thread_action_->setObjectName("threadAction");
    shaft_thread_action_=make_action(tr("Závit"),"thread");
    shaft_thread_action_->setObjectName("shaftThreadAction");
    connect(shaft_thread_action_,&QAction::triggered,this,[this] { show_shaft_thread_properties(); });
    drill_point_action_ = make_action(tr("Vrtací špička"), "drill-point");
    drill_point_action_->setObjectName("drillPointAction");
    sphere_action_ = make_action(tr("Koule"), "sphere");
    cone_action_ = make_action(tr("Kužel"), "cone");
    pyramid_action_ = make_action(tr("Jehlan"), "pyramid");
    wedge_action_ = make_action(tr("Klín"), "wedge");
    construction_point_action_ = make_action(tr("Bod"), "point");
    curve_3d_action_ = make_action(tr("3D křivka"), "sketch-3d");
    mirror_action_=make_action(tr("Zrcadlo"),"mirror");mirror_action_->setObjectName("mirrorAction");
    mirror_action_->setToolTip(tr("Zrcadlený odkaz na vybranou komponentu nebo těleso."));
    connect(mirror_action_,&QAction::triggered,this,[this]{show_derived_copy_properties();});
    pattern_action_=make_action(tr("Pole"),"pattern");pattern_action_->setObjectName("patternAction");
    pattern_action_->setToolTip(tr("Lineární nebo kruhové kopie vybraného tělesa či komponenty."));
    connect(pattern_action_,&QAction::triggered,this,[this]{show_derived_copy_properties({},true);});
    sweep_3d_action_ = make_action(tr("3D tažení"), "sweep");
    sweep_3d_action_->setToolTip(tr("3D tažení / přechod mezi profily podél prostorové dráhy."));
    sweep2d_action_ = make_action(tr("2D tažení"), "sweep2d");
    sweep2d_action_->setObjectName("sweep2dAction");
    sweep2d_action_->setToolTip(tr("2D tažení / přechod mezi profily po rovinné dráze."));
    connect(sweep2d_action_, &QAction::triggered, this, [this] { show_sweep2d_properties(); });
    helical_sweep_action_ = make_action(t("command.helix_sweep", "H-tažení"), "helical-sweep");
    helical_sweep_action_->setObjectName("helicalSweepAction");
    helical_sweep_action_->setToolTip(tr("Šroubovicové tažení profilu."));
    connect(helical_sweep_action_, &QAction::triggered, this, [this] { show_helical_sweep_properties(); });
    construction_axis_action_ = make_action(tr("Osa"), "axis");
    construction_plane_action_ = make_action(tr("Rovina"), "plane");
    construction_point_action_->setObjectName("constructionPointAction");
    curve_3d_action_->setObjectName("curve3DAction");
    sweep_3d_action_->setObjectName("sweep3DAction");
    construction_axis_action_->setObjectName("constructionAxisAction");
    construction_plane_action_->setObjectName("constructionPlaneAction");
    extrusion_action_ = make_action(tr("Vytažení"), "protrusion");
    extrusion_action_->setObjectName("extrusionAction");
    revolution_action_ = make_action(tr("Rotace"), "revolve");
    revolution_action_->setObjectName("revolutionAction");
    fillet_action_ = make_action(tr("Zaoblení"), "fillet");
    chamfer_action_ = make_action(tr("Sražení"), "chamfer");
    shell_action_ = make_action(tr("Shell"), "shell");
    shell_action_->setObjectName("shellAction");
    sketch_action_ = make_action(tr("Skica"), "sketch");
    sketch_action_->setObjectName("sketchAction");
    sketch_normal_view_action_ = make_action(tr("Pohled kolmo"), "view-normal");
    sketch_normal_view_action_->setObjectName("sketchNormalViewAction");
    sketch_normal_view_action_->setEnabled(false);
    sketch_flip_view_action_ = make_action(
        tr("Převrátit"), "sketch-view-flip");
    sketch_flip_view_action_->setObjectName("sketchFlipViewAction");
    sketch_flip_view_action_->setEnabled(false);
    sketch_rotate_view_action_ = make_action(
        tr("Otočit"), "sketch-view-rotate");
    sketch_rotate_view_action_->setObjectName("sketchRotateViewAction");
    sketch_rotate_view_action_->setEnabled(false);
    sketch_external_reference_action_ = make_action(
        tr("Externí reference"), "sketch-reference");
    sketch_external_reference_action_->setObjectName(
        "sketchExternalReferenceAction");
    sketch_external_reference_action_->setCheckable(true);
    sketch_external_reference_action_->setEnabled(false);
    sketch_external_profile_action_ = make_action(
        tr("Reference → obrys"), "sketch-segment");
    sketch_external_profile_action_->setObjectName(
        "sketchExternalProfileAction");
    sketch_external_profile_action_->setCheckable(true);
    sketch_external_profile_action_->setEnabled(false);
    sketch_point_action_ = make_action(tr("Bod"), "point");
    sketch_point_action_->setObjectName("sketchPointAction");
    sketch_point_action_->setEnabled(false);
    sketch_construction_action_ = make_action(
        tr("Konstrukční čára"), "sketch-construction");
    sketch_construction_action_->setObjectName("sketchConstructionAction");
    sketch_construction_action_->setEnabled(false);
    sketch_segment_action_ = make_action(tr("Úsečka"), "sketch-segment");
    sketch_segment_action_->setObjectName("sketchSegmentAction");
    sketch_common_tangent_action_ = make_action(
        tr("Společná tečna"), "sketch-common-tangent");
    sketch_common_tangent_action_->setObjectName(
        "sketchCommonTangentAction");
    sketch_polyline_action_ = make_action(tr("Lomená čára"), "sketch-polyline");
    sketch_polyline_action_->setObjectName("sketchPolylineAction");
    sketch_polyline_action_->setEnabled(false);
    sketch_rectangle_action_ = make_action(tr("Obdélník"), "sketch-rectangle");
    sketch_rectangle_action_->setObjectName("sketchRectangleAction");
    // One continuous Python-parity command: after the centre is placed RMB
    // cycles 4 -> 6 -> 8 sides while the live preview stays under the cursor.
    sketch_polygon_action_ = make_action(tr("Mnohoúhelník"), "sketch-hexagon");
    sketch_polygon_action_->setIcon(resource_icon("sketch-hexagon"));
    sketch_polygon_action_->setObjectName("sketchPolygonAction");
    sketch_polygon_action_->setEnabled(false);
    connect(sketch_polygon_action_, &QAction::triggered, this,
        [this] { start_sketch_polygon(4); });
    sketch_trim_action_ = make_action(tr("Ořezat"), "sketch-trim");
    sketch_trim_action_->setObjectName("sketchTrimAction");
    sketch_trim_action_->setCheckable(true);
    sketch_trim_action_->setEnabled(false);
    sketch_corner_fillet_action_ = make_action(tr("Zaoblit roh"), "sketch-fillet");
    sketch_corner_fillet_action_->setObjectName("sketchCornerFilletAction");
    sketch_corner_fillet_action_->setEnabled(false);
    sketch_corner_fillet_action_->setVisible(false);
    sketch_offset_action_ = make_action(tr("Offset"), "sketch-offset");
    sketch_offset_action_->setObjectName("sketchOffsetAction");
    connect(sketch_offset_action_, &QAction::triggered, this, [this]{show_sketch_offset_properties();});
    sketch_mirror_action_ = make_action(tr("Zrcadlit"), "sketch-mirror");
    sketch_mirror_action_->setObjectName("sketchMirrorAction");
    sketch_mirror_action_->setEnabled(false);
    sketch_circle_action_ = make_action(tr("Kružnice"), "sketch-circle");
    sketch_circle_action_->setObjectName("sketchCircleAction");
    sketch_arc_action_ = make_action(tr("Oblouk"), "sketch-arc");
    sketch_arc_action_->setObjectName("sketchArcAction");
    sketch_ellipse_action_ = make_action(tr("Elipsa"), "sketch-ellipse");
    sketch_ellipse_action_->setObjectName("sketchEllipseAction");
    sketch_elliptical_arc_action_ = make_action(
        tr("Eliptický oblouk"), "sketch-elliptical-arc");
    sketch_elliptical_arc_action_->setObjectName("sketchEllipticalArcAction");
    sketch_bspline_action_ = make_action(
        tr("B-spline – řídicí body"), "sketch-spline");
    sketch_bspline_action_->setObjectName("sketchBSplineAction");
    sketch_interpolating_spline_action_ = make_action(
        tr("Interpolační spline"), "sketch-spline");
    sketch_interpolating_spline_action_->setObjectName(
        "sketchInterpolatingSplineAction");
    for (auto* action : {sketch_point_action_, sketch_construction_action_,
                         sketch_segment_action_, sketch_polyline_action_,
                         sketch_rectangle_action_, sketch_polygon_action_,
                         sketch_circle_action_, sketch_arc_action_,
                         sketch_ellipse_action_, sketch_elliptical_arc_action_,
                         sketch_bspline_action_,
                         sketch_interpolating_spline_action_}) {
        action->setCheckable(true);
    }
    sketch_text_action_ = make_action(tr("Text"), "sketch-text");
    sketch_text_action_->setObjectName("sketchTextAction");
    sketch_text_action_->setEnabled(false);
    sketch_horizontal_action_ = make_action(tr("Vodorovnost"));
    sketch_horizontal_action_->setObjectName("sketchHorizontalAction");
    sketch_vertical_action_ = make_action(tr("Svislost"));
    sketch_vertical_action_->setObjectName("sketchVerticalAction");
    sketch_coincident_action_ = make_action(tr("Totožnost"));
    sketch_coincident_action_->setObjectName("sketchCoincidentAction");
    sketch_midpoint_action_ = make_action(tr("Střed"));
    sketch_midpoint_action_->setObjectName("sketchMidpointAction");
    sketch_symmetric_action_ = make_action(tr("Symetrie"));
    sketch_symmetric_action_->setObjectName("sketchSymmetricAction");
    sketch_concentric_action_ = make_action(tr("Soustřednost"));
    sketch_concentric_action_->setObjectName("sketchConcentricAction");
    sketch_tangent_action_ = make_action(tr("Tečnost"));
    sketch_tangent_action_->setObjectName("sketchTangentAction");
    sketch_parallel_action_ = make_action(tr("Rovnoběžnost"));
    sketch_parallel_action_->setObjectName("sketchParallelAction");
    sketch_perpendicular_action_ = make_action(tr("Kolmost"));
    sketch_perpendicular_action_->setObjectName("sketchPerpendicularAction");
    sketch_equal_length_action_ = make_action(tr("Stejnost"));
    sketch_equal_length_action_->setObjectName("sketchEqualAction");
    const auto set_constraint_menu_icon = [](QAction* action,
            zima::sketcher::ConstraintKind kind) {
        action->setIcon(sketch_constraint_tree_icon(kind));
        action->setIconVisibleInMenu(true);
    };
    set_constraint_menu_icon(sketch_horizontal_action_,
        zima::sketcher::ConstraintKind::Horizontal);
    set_constraint_menu_icon(sketch_vertical_action_,
        zima::sketcher::ConstraintKind::Vertical);
    set_constraint_menu_icon(sketch_parallel_action_,
        zima::sketcher::ConstraintKind::Parallel);
    set_constraint_menu_icon(sketch_equal_length_action_,
        zima::sketcher::ConstraintKind::EqualLength);
    set_constraint_menu_icon(sketch_perpendicular_action_,
        zima::sketcher::ConstraintKind::Perpendicular);
    set_constraint_menu_icon(sketch_coincident_action_,
        zima::sketcher::ConstraintKind::Coincident);
    set_constraint_menu_icon(sketch_midpoint_action_,
        zima::sketcher::ConstraintKind::Midpoint);
    set_constraint_menu_icon(sketch_symmetric_action_,
        zima::sketcher::ConstraintKind::Symmetric);
    set_constraint_menu_icon(sketch_tangent_action_,
        zima::sketcher::ConstraintKind::Tangent);
    set_constraint_menu_icon(sketch_concentric_action_,
        zima::sketcher::ConstraintKind::Concentric);
    sketch_fix_point_action_ = make_action(tr("Fixovat bod"));
    sketch_fix_point_action_->setToolTip(tr(
        "Uzamkne bod na jeho současných souřadnicích X a Y."));
    sketch_fix_point_action_->setObjectName("sketchFixPointAction");
    sketch_constraints_action_ = make_action(tr("Vazby"), "sketch-constraints");
    connect(sketch_constraints_action_,&QAction::triggered,this,&AssemblyWorkspaceWindow::show_sketch_constraints);
    sketch_constraints_action_->setObjectName("sketchConstraintsAction");
    sketch_constraints_action_->setCheckable(true);
    sketch_constraints_action_->setEnabled(false);
    sketch_dimension_action_ = make_action(tr("Kóta"), "sketch-dimensions");
    sketch_dimension_action_->setObjectName("sketchDimensionAction");
    sketch_dimension_action_->setCheckable(true);
    sketch_universal_dimension_action_ =
        make_action(tr("Univerzální kóta"), "sketch-dimensions");
    sketch_universal_dimension_action_->setObjectName(
        "sketchUniversalDimensionAction");
    sketch_universal_dimension_action_->setCheckable(true);
    sketch_dimension_x_action_ = make_action(tr("Vodorovná kóta úsečky…"));
    sketch_dimension_x_action_->setObjectName("sketchDimensionXAction");
    sketch_dimension_y_action_ = make_action(tr("Svislá kóta úsečky…"));
    sketch_dimension_y_action_->setObjectName("sketchDimensionYAction");
    sketch_point_line_dimension_action_ =
        make_action(tr("Vzdálenost bodu od přímky…"));
    sketch_point_line_dimension_action_->setObjectName(
        "sketchPointLineDimensionAction");
    sketch_symmetric_dimension_action_ =
        make_action(tr("Symetrická kóta od osy…"));
    sketch_symmetric_dimension_action_->setObjectName(
        "sketchSymmetricDimensionAction");
    sketch_three_point_angle_dimension_action_ =
        make_action(tr("Tříbodová úhlová kóta…"));
    sketch_three_point_angle_dimension_action_->setObjectName(
        "sketchThreePointAngleDimensionAction");
    sketch_angle_dimension_action_ = make_action(tr("Úhel mezi přímkami…"));
    sketch_angle_dimension_action_->setObjectName("sketchAngleDimensionAction");
    sketch_parallel_distance_dimension_action_ =
        make_action(tr("Vzdálenost rovnoběžek…"));
    sketch_parallel_distance_dimension_action_->setObjectName(
        "sketchParallelDistanceDimensionAction");
    sketch_radius_dimension_action_ = make_action(tr("Kóta poloměru…"));
    sketch_radius_dimension_action_->setObjectName("sketchRadiusDimensionAction");
    sketch_diameter_dimension_action_ = make_action(tr("Kóta průměru…"));
    sketch_diameter_dimension_action_->setObjectName("sketchDiameterDimensionAction");
    sketch_ellipse_major_dimension_action_ = make_action(tr("Kóta hlavní poloosy elipsy…"));
    sketch_ellipse_major_dimension_action_->setObjectName(
        "sketchEllipseMajorDimensionAction");
    sketch_ellipse_minor_dimension_action_ = make_action(tr("Kóta vedlejší poloosy elipsy…"));
    sketch_ellipse_minor_dimension_action_->setObjectName(
        "sketchEllipseMinorDimensionAction");
    sketch_ellipse_rotation_dimension_action_ = make_action(tr("Kóta natočení elipsy…"));
    sketch_ellipse_rotation_dimension_action_->setObjectName(
        "sketchEllipseRotationDimensionAction");
    sketch_dimensions_menu_ = new QMenu(tr("Kóty"), this);
    sketch_dimensions_menu_->setObjectName("sketchDimensionsMenu");
    // Until the automatic workflow is complete there must be exactly one
    // visible dimension-entry state machine. The older individual commands
    // remain implementation details for existing documents/properties, but
    // are deliberately not exposed alongside Universal Dimension.
    sketch_dimensions_menu_->addAction(sketch_universal_dimension_action_);
    sketch_dimensions_action_ = sketch_dimensions_menu_->menuAction();
    sketch_dimensions_action_->setText(tr("Kóty"));
    sketch_dimensions_action_->setIcon(resource_icon("sketch-dimensions"));
    sketch_dimensions_action_->setObjectName("sketchDimensionsAction");
    sketch_dimensions_action_->setEnabled(false);
    finish_sketch_action_ = make_action(tr("Dokončit skicu"), "sketch");
    finish_sketch_action_->setObjectName("finishSketchAction");
    finish_sketch_action_->setEnabled(false);
    regenerate_part_action_ = make_action(tr("Regenerovat"));
    regenerate_part_action_->setObjectName("regeneratePartAction");

    connect(box_action_, &QAction::triggered, this, [this] {
        show_primitive_properties(zima::document::FeatureKind::Box); });
    connect(cylinder_action_, &QAction::triggered, this, [this] {
        show_primitive_properties(zima::document::FeatureKind::Cylinder); });
    connect(thread_action_, &QAction::triggered, this, [this] {
        show_primitive_properties(zima::document::FeatureKind::Thread); });
    connect(drill_point_action_, &QAction::triggered, this, [this] {
        show_primitive_properties(zima::document::FeatureKind::DrillPoint); });
    connect(sphere_action_, &QAction::triggered, this, [this] {
        show_primitive_properties(zima::document::FeatureKind::Sphere); });
    connect(cone_action_, &QAction::triggered, this, [this] {
        show_primitive_properties(zima::document::FeatureKind::Cone); });
    connect(pyramid_action_, &QAction::triggered, this, [this] {
        show_primitive_properties(zima::document::FeatureKind::Pyramid); });
    connect(wedge_action_, &QAction::triggered, this, [this] {
        show_primitive_properties(zima::document::FeatureKind::Wedge); });
    connect(construction_point_action_, &QAction::triggered, this, [this] {
        show_construction_properties(zima::document::ConstructionKind::Point); });
    connect(curve_3d_action_, &QAction::triggered, this, [this] {
        show_construction_properties(zima::document::ConstructionKind::Curve3D); });
    connect(sweep_3d_action_, &QAction::triggered, this, [this] {
        show_sweep3d_properties(); });
    connect(construction_axis_action_, &QAction::triggered, this, [this] {
        show_construction_properties(zima::document::ConstructionKind::Axis); });
    connect(construction_plane_action_, &QAction::triggered, this, [this] {
        show_construction_properties(zima::document::ConstructionKind::Plane); });
    connect(extrusion_action_, &QAction::triggered, this, [this] {
        show_primitive_properties(zima::document::FeatureKind::Extrusion); });
    connect(revolution_action_, &QAction::triggered, this, [this] {
        show_primitive_properties(zima::document::FeatureKind::Revolution); });
    connect(fillet_action_, &QAction::triggered, this, [this] {
        start_edge_treatment(zima::document::FeatureKind::Fillet); });
    connect(chamfer_action_, &QAction::triggered, this, [this] {
        start_edge_treatment(zima::document::FeatureKind::Chamfer); });
    connect(shell_action_, &QAction::triggered, this, [this] { start_shell(); });
    connect(sketch_action_, &QAction::triggered, this, [this] { show_sketch_properties(); });
    connect(sketch_normal_view_action_, &QAction::triggered, this,
        [this] { align_active_sketch_view(); });
    connect(sketch_flip_view_action_, &QAction::triggered, this,
        [this] { flip_active_sketch_view(); });
    connect(sketch_rotate_view_action_, &QAction::triggered, this,
        [this] { rotate_active_sketch_view(); });
    connect(sketch_external_reference_action_, &QAction::toggled, this,
        [this](bool enabled) {
            if (enabled) sketch_external_profile_active_ = false;
            set_sketch_external_reference_mode(enabled);
        });
    connect(sketch_external_profile_action_, &QAction::toggled, this,
        [this](bool enabled) {
            sketch_external_profile_active_ = enabled;
            set_sketch_external_reference_mode(enabled);
        });
    connect(sketch_point_action_, &QAction::triggered, this,
        [this] { start_sketch_point(); });
    connect(sketch_construction_action_, &QAction::triggered, this,
        [this] { start_sketch_segment(true); });
    connect(sketch_segment_action_, &QAction::triggered, this, [this] { start_sketch_segment(); });
    connect(sketch_common_tangent_action_, &QAction::triggered, this,
        [this] { start_sketch_common_tangent(); });
    connect(sketch_polyline_action_, &QAction::triggered, this,
        [this] { start_sketch_polyline(); });
    connect(sketch_rectangle_action_, &QAction::triggered, this, [this] { start_sketch_rectangle(); });
    connect(sketch_trim_action_, &QAction::triggered, this,
        [this] { start_sketch_trim(); });
    connect(sketch_corner_fillet_action_, &QAction::triggered, this,
        [this] { start_sketch_corner_fillet(); });
    connect(sketch_trim_action_, &QAction::changed,
        sketch_corner_fillet_action_, [this] {
            sketch_corner_fillet_action_->setEnabled(
                sketch_trim_action_->isEnabled());
        });
    connect(sketch_mirror_action_, &QAction::triggered, this,
        [this] { start_sketch_mirror(); });
    connect(sketch_circle_action_, &QAction::triggered, this, [this] { start_sketch_circle(); });
    connect(sketch_arc_action_, &QAction::triggered, this, [this] { start_sketch_arc(); });
    connect(sketch_ellipse_action_, &QAction::triggered, this, [this] { start_sketch_ellipse(); });
    connect(sketch_elliptical_arc_action_, &QAction::triggered, this,
        [this] { start_sketch_elliptical_arc(); });
    connect(sketch_bspline_action_, &QAction::triggered, this,
        [this] { start_sketch_bspline(false); });
    connect(sketch_interpolating_spline_action_, &QAction::triggered, this,
        [this] { start_sketch_bspline(true); });
    for (auto* action : {sketch_point_action_, sketch_construction_action_,
                         sketch_segment_action_, sketch_polyline_action_,
                         sketch_rectangle_action_, sketch_polygon_action_,
                         sketch_circle_action_, sketch_arc_action_,
                         sketch_ellipse_action_, sketch_elliptical_arc_action_,
                         sketch_bspline_action_,
                         sketch_interpolating_spline_action_}) {
        // The command starters first cancel the previous Sketch tool.  Sync
        // after that transition so the one command which actually remained
        // active owns the toolbar's green checked state.
        connect(action, &QAction::triggered, this,
            [this] { sync_sketch_tool_action_checks(); });
    }
    connect(sketch_text_action_, &QAction::triggered, this,
        [this] { show_sketch_text_properties(active_sketch_id_); });
    connect(sketch_horizontal_action_, &QAction::triggered, this, [this] {
        if (!selected_sketch_segment_id_.empty()) {
            constrain_selected_segment(zima::sketcher::ConstraintKind::Horizontal);
            if (sketch_constraints_dialog_) start_sketch_coincident(zima::sketcher::ConstraintKind::Horizontal);
        } else start_sketch_coincident(zima::sketcher::ConstraintKind::Horizontal);
    });
    connect(sketch_vertical_action_, &QAction::triggered, this, [this] {
        if (!selected_sketch_segment_id_.empty()) {
            constrain_selected_segment(zima::sketcher::ConstraintKind::Vertical);
            if (sketch_constraints_dialog_) start_sketch_coincident(zima::sketcher::ConstraintKind::Vertical);
        } else start_sketch_coincident(zima::sketcher::ConstraintKind::Vertical);
    });
    connect(sketch_coincident_action_, &QAction::triggered, this, [this] { start_sketch_coincident(); });
    connect(sketch_midpoint_action_, &QAction::triggered, this,
        [this] { start_sketch_midpoint(); });
    connect(sketch_symmetric_action_, &QAction::triggered, this,
        [this] { start_sketch_symmetric(); });
    connect(sketch_concentric_action_, &QAction::triggered, this,
        [this] { start_sketch_concentric(); });
    connect(sketch_tangent_action_, &QAction::triggered, this,
        [this] { start_sketch_tangent(); });
    connect(sketch_parallel_action_, &QAction::triggered, this, [this] {
        start_sketch_segment_pair(zima::sketcher::ConstraintKind::Parallel); });
    connect(sketch_perpendicular_action_, &QAction::triggered, this, [this] {
        start_sketch_segment_pair(zima::sketcher::ConstraintKind::Perpendicular); });
    connect(sketch_equal_length_action_, &QAction::triggered, this, [this] {
        start_sketch_segment_pair(zima::sketcher::ConstraintKind::EqualLength); });
    connect(sketch_fix_point_action_, &QAction::triggered, this,
        [this] { toggle_selected_sketch_point_fixed(); });
    connect(sketch_dimension_action_, &QAction::triggered, this,
        [this] {
            // Kóta is one contextual command. It decides from candidates
            // selected after activation, never from stale geometry latches.
            start_sketch_point_dimension(zima::sketcher::DimensionKind::Distance);
        });
    connect(sketch_universal_dimension_action_, &QAction::triggered, this,
        [this] { start_sketch_universal_dimension(); });
    connect(sketch_dimension_x_action_, &QAction::triggered, this, [this] {
        if (selected_sketch_segment_id_.empty()) {
            start_sketch_point_dimension(zima::sketcher::DimensionKind::DistanceX);
        } else show_sketch_dimension_properties(active_sketch_id_, {},
            zima::sketcher::DimensionKind::DistanceX); });
    connect(sketch_dimension_y_action_, &QAction::triggered, this, [this] {
        if (selected_sketch_segment_id_.empty()) {
            start_sketch_point_dimension(zima::sketcher::DimensionKind::DistanceY);
        } else show_sketch_dimension_properties(active_sketch_id_, {},
            zima::sketcher::DimensionKind::DistanceY); });
    connect(sketch_point_line_dimension_action_, &QAction::triggered, this,
        [this] {
            start_sketch_point_dimension(
                zima::sketcher::DimensionKind::DistancePointLine);
        });
    connect(sketch_symmetric_dimension_action_, &QAction::triggered, this,
        [this] {
            start_sketch_point_dimension(
                zima::sketcher::DimensionKind::DistanceSymmetric);
        });
    connect(sketch_three_point_angle_dimension_action_, &QAction::triggered, this,
        [this] {
            start_sketch_point_dimension(
                zima::sketcher::DimensionKind::AngleThreePoint);
        });
    connect(sketch_angle_dimension_action_, &QAction::triggered, this, [this] {
        start_sketch_line_pair_dimension(
            zima::sketcher::DimensionKind::AngleBetween); });
    connect(sketch_parallel_distance_dimension_action_, &QAction::triggered,
        this, [this] {
            start_sketch_line_pair_dimension(
                zima::sketcher::DimensionKind::DistanceLine);
        });
    connect(sketch_angle_dimension_action_, &QAction::changed,
        sketch_parallel_distance_dimension_action_,
        [this] {
            sketch_parallel_distance_dimension_action_->setEnabled(
                sketch_angle_dimension_action_->isEnabled());
        });
    sketch_parallel_distance_dimension_action_->setEnabled(
        sketch_angle_dimension_action_->isEnabled());
    connect(sketch_radius_dimension_action_, &QAction::triggered, this,
        [this] { show_sketch_dimension_properties(active_sketch_id_); });
    connect(sketch_diameter_dimension_action_, &QAction::triggered, this, [this] {
        show_sketch_dimension_properties(active_sketch_id_, {},
            zima::sketcher::DimensionKind::Diameter); });
    connect(sketch_ellipse_major_dimension_action_, &QAction::triggered, this, [this] {
        show_sketch_dimension_properties(active_sketch_id_, {},
            zima::sketcher::DimensionKind::EllipseMajorRadius); });
    connect(sketch_ellipse_minor_dimension_action_, &QAction::triggered, this, [this] {
        show_sketch_dimension_properties(active_sketch_id_, {},
            zima::sketcher::DimensionKind::EllipseMinorRadius); });
    connect(sketch_ellipse_rotation_dimension_action_, &QAction::triggered, this, [this] {
        show_sketch_dimension_properties(active_sketch_id_, {},
            zima::sketcher::DimensionKind::EllipseRotation); });
    connect(finish_sketch_action_, &QAction::triggered, this,
        [this] { finish_active_sketch(); });
    connect(regenerate_part_action_, &QAction::triggered, this,
        [this] { regenerate_active_part(); });

    insert_menu_ = new QMenu(tr("Vložit otevřený dokument"), this);
    insert_menu_->setObjectName("insertComponentMenu");
    insert_action_ = make_action(tr("Vložit komponentu"));
    insert_action_->setIcon(resource_icon("assembly"));
    insert_action_->setObjectName("insertComponentAction");
    connect(insert_action_, &QAction::triggered, this,
        [this] { insert_component_from_file(); });
    connect(insert_menu_, &QMenu::aboutToShow, this,
        [this] { rebuild_insert_menu(); });
    regenerate_action_ = make_action(tr("Regenerovat"));
    regenerate_action_->setObjectName("regenerateAssemblyAction");
    connect(regenerate_action_, &QAction::triggered, this, [this] { regenerate_assembly(); });

    main_toolbar_ = new QToolBar(tr("Dokument"), this);
    main_toolbar_->setObjectName("mainToolbar");
    main_toolbar_->setMovable(false);
    main_toolbar_->setIconSize(QSize(24, 24));
    main_toolbar_->setToolButtonStyle(Qt::ToolButtonIconOnly);
    // The document strip is the first interaction surface below the menu.
    // Give every enabled command the same immediate green offer/press
    // feedback as the View and application toolbars instead of leaving the
    // icon buttons visually inert.
    main_toolbar_->setStyleSheet(
        "QToolButton { margin:1px; padding:3px; border:1px solid transparent;"
        " border-radius:5px; }"
        "QToolButton:hover:enabled { background-color:rgba(77,216,17,72);"
        " color:#fff; border:1px solid rgba(128,170,26,190); }"
        "QToolButton:pressed:enabled { background-color:rgba(77,216,17,175);"
        " color:#fff; border:1px solid #9BCC32; padding-left:4px;"
        " padding-top:4px; padding-right:2px; padding-bottom:2px; }"
        "QToolButton:disabled { color:rgba(255,255,255,70); }");
    main_toolbar_->addAction(new_document_action_);
    main_toolbar_->addAction(open_document_action_);
    main_toolbar_->addAction(save_action_);
    main_toolbar_->addAction(save_as_action_);
    main_toolbar_->addSeparator();
    main_toolbar_->addAction(undo_action_);
    main_toolbar_->addAction(redo_action_);
    main_toolbar_->addSeparator();
    main_toolbar_->addAction(settings_action_);
    auto* toolbar_spacer = new QWidget(main_toolbar_);
    toolbar_spacer->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Preferred);
    main_toolbar_->addWidget(toolbar_spacer);
    auto* logo = new QLabel(QStringLiteral(
        "<span style=\"color:#80AA1A\">ZIMA</span>-CAD"), main_toolbar_);
    auto logo_font = logo->font();
    logo_font.setBold(true);
    logo_font.setPointSizeF(std::max(11.0, logo_font.pointSizeF()));
    logo->setFont(logo_font);
    logo->setContentsMargins(8, 2, 12, 2);
    main_toolbar_->addWidget(logo);
    addToolBar(Qt::TopToolBarArea, main_toolbar_);

    view_toolbar_ = new QToolBar(tr("Pohled"), this);
    view_toolbar_->setObjectName("viewToolbar");
    view_toolbar_->setMovable(false);
    view_toolbar_->setIconSize(QSize(16, 16));
    view_toolbar_->setStyleSheet(
        "QToolButton:hover:enabled { background-color:rgba(77,216,17,72);"
        " color:#fff; border:1px solid rgba(128,170,26,190); border-radius:4px; }"
        "QToolButton:checked { background-color:rgba(77,216,17,125);"
        " color:#fff; border:1px solid #80AA1A; border-radius:4px; }"
        "QToolButton:pressed { background-color:rgba(77,216,17,165);"
        " color:#fff; border:1px solid #9BCC32; border-radius:4px; }");
    view_toolbar_->addAction(custom_body_color_action_);
    view_toolbar_->addAction(parameters_action_);
    measure_action_=view_toolbar_->addAction(resource_icon("measure"),tr("Měření…"));
    measure_action_->setObjectName("measureAction");
    connect(measure_action_,&QAction::triggered,this,[this]{show_measurement();});
    view_toolbar_->addAction(regenerate_document_action_);
    section_action_=view_toolbar_->addAction(tr("Řezy…"));
    section_action_->setObjectName("createSectionAction");
    connect(section_action_,&QAction::triggered,this,[this]{show_section_properties();});
    cancel_section_sketch_action_=view_toolbar_->addAction(tr("Zrušit skicu řezu"));
    cancel_section_sketch_action_->setObjectName("cancelSectionSketchAction");cancel_section_sketch_action_->setVisible(false);
    connect(cancel_section_sketch_action_,&QAction::triggered,this,[this]{cancel_section_sketch();});
    view_toolbar_->addSeparator();
    view_toolbar_->addAction(fit_view_action_);
    view_toolbar_->addAction(normal_view_action);
    standard_view_combo_ = new QComboBox(view_toolbar_);
    standard_view_combo_->setObjectName("standardViewCombo");
    const std::array<std::pair<QString, zima::viewer::StandardView>, 8> standard_views_data{{
        {tr("Základní pohledy"), zima::viewer::StandardView::Isometric},
        {tr("Výchozí – izometrický"), zima::viewer::StandardView::Isometric},
        {tr("Front – XZ"), zima::viewer::StandardView::Front},
        {tr("Back – XZ opačně"), zima::viewer::StandardView::Back},
        {tr("Left – YZ"), zima::viewer::StandardView::Left},
        {tr("Right – YZ opačně"), zima::viewer::StandardView::Right},
        {tr("Top – XY"), zima::viewer::StandardView::Top},
        {tr("Bottom – XY opačně"), zima::viewer::StandardView::Bottom},
    }};
    for (const auto& [text, mode] : standard_views_data) {
        standard_view_combo_->addItem(text, static_cast<int>(mode));
    }
    connect(standard_view_combo_, &QComboBox::currentIndexChanged, this,
        [this](int index) {
            if (index <= 0 || viewer_ == nullptr) return;
            viewer_->set_standard_view(static_cast<zima::viewer::StandardView>(
                standard_view_combo_->itemData(index).toInt()));
            standard_view_combo_->setCurrentIndex(0);
        });
    view_toolbar_->addWidget(standard_view_combo_);
    view_toolbar_->addAction(selection_action_);
    if (auto* selection_button = qobject_cast<QToolButton*>(
            view_toolbar_->widgetForAction(selection_action_))) {
        selection_button->setToolButtonStyle(Qt::ToolButtonTextOnly);
    }
    selection_filter_combo_ = new QComboBox(view_toolbar_);
    selection_filter_combo_->setObjectName("selectionFilterCombo");
    selection_filter_combo_->setToolTip(tr("Filtr prvků vybíraných ve 3D pohledu"));
    for (const auto& filter : {tr("Vše"), tr("Plochy"), tr("Body"),
                               tr("Osy"), tr("Roviny")}) {
        selection_filter_combo_->addItem(filter);
    }
    connect(selection_filter_combo_, &QComboBox::currentIndexChanged, this,
        [this] { if (viewer_ != nullptr && workspace_.size() != 0) refresh_scene(); });
    view_toolbar_->addWidget(selection_filter_combo_);
    view_toolbar_->addSeparator();
    for (auto* action : {orthographic_camera_action_,
                         perspective_camera_action_, fly_camera_action_}) {
        view_toolbar_->addAction(action);
        if (auto* button = qobject_cast<QToolButton*>(
                view_toolbar_->widgetForAction(action))) {
            button->setToolButtonStyle(Qt::ToolButtonTextBesideIcon);
        }
    }
    view_toolbar_->addSeparator();
    for (auto* action : {wire_action_, hidden_edges_action_, no_hidden_edges_action_,
                         shaded_edges_action_, shaded_action_}) {
        view_toolbar_->addAction(action);
    }
    view_toolbar_->addSeparator();
    for (auto* action : {show_origins_action_, show_points_action_, show_axes_action_,
                         show_planes_action_, show_sketches_action_, show_dimensions_action_}) {
        view_toolbar_->addAction(action);
    }

    tools_toolbar_ = new QToolBar(tr("Nástroje"), this);
    tools_toolbar_->setObjectName("toolsToolbar");
    tools_toolbar_->setMovable(false);
    tools_toolbar_->setOrientation(Qt::Vertical);
    tools_toolbar_->setToolButtonStyle(Qt::ToolButtonTextBesideIcon);
    tools_toolbar_->setMinimumWidth(158);
    tools_toolbar_->setIconSize(QSize(16, 16));
    tools_toolbar_->setStyleSheet(
        "QToolButton { padding:3px 6px; text-align:left; }"
        "QToolButton:checked { background-color:rgba(77,216,17,125);"
        " color:#fff; border:none; border-radius:4px; }"
        "QToolButton#applicationCommandButton:hover:enabled {"
        " background-color:rgba(77,216,17,90); color:#fff; border:none;"
        " border-radius:4px; }"
        "QToolButton#applicationCommandButton:pressed:enabled {"
        " background-color:rgba(77,216,17,165); color:#fff; border:none;"
        " border-radius:4px; }");
}

} // namespace zima::app
