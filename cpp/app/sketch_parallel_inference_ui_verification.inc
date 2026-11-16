static int verify_sketch_parallel_inference_ui(
        QApplication& application, const std::filesystem::path& directory) {
    using namespace zima;
    using K = sketcher::ConstraintKind;
    const auto check = [](bool ok, const char* message) {
        if (!ok) throw std::runtime_error(message);
    };
    const auto flush = [&] {
        application.processEvents();
        QCoreApplication::sendPostedEvents(nullptr, QEvent::DeferredDelete);
        application.processEvents();
    };
    try {
        unsigned case_index{};
        for (const int scenario : {0, 1, 2, 3}) {
            const bool external = scenario != 1;
            const bool horizontal = scenario == 2;
            const bool broken = scenario == 3;
            auto part = document::PartDocument::create_default();
            auto source_feature = document::PartDocument::create_sketch_container();
            auto target_feature = document::PartDocument::create_sketch_container();
            auto source = sketcher::Sketch::create_default();
            source.owner_container_id = source_feature.id;
            const auto source_segment = horizontal
                ? source.add_segment(-40.0, -18.0, 40.0, -18.0)
                : source.add_segment(-40.0, -20.0, 40.0, 20.0);
            for (auto& point : source.points) point.fixed = true;

            auto target = sketcher::Sketch::create_default();
            target.owner_container_id = target_feature.id;
            std::string support_id;
            if (external) {
                auto reference = sketcher::Sketch::create_external_reference(
                    sketcher::ExternalReferenceKind::Edge);
                reference.source_document_id = part.document_id;
                reference.source_owner_id = broken ? "missing-source" : source.id;
                reference.source_semantic_key = "segment:" + source_segment;
                reference.infinite = true;
                reference.broken = broken;
                reference.cached_points = horizontal
                    ? std::vector<std::array<double, 2>>{{-40.0, -18.0},
                          {40.0, -18.0}}
                    : std::vector<std::array<double, 2>>{{-40.0, -20.0},
                          {40.0, 20.0}};
                support_id = reference.id;
                target.add_external_reference(reference);
            } else {
                support_id = target.add_segment(-40.0, -20.0, 40.0, 20.0);
                const auto support = std::ranges::find(
                    target.segments, support_id, &sketcher::SketchSegment::id);
                target.find_point(support->first_point_id)->fixed = true;
                target.find_point(support->second_point_id)->fixed = true;
            }
            const auto target_id = target.id;
            const auto source_definition = source.serialized();
            const auto external_definition = target.external_references;
            part.history = {source_feature, target_feature};
            part.sketches = {source, target};
            document::BodyHistoryGraph graph;
            const auto body = graph.create_body("Parallel inference");
            graph.insert({document::PartHistoryKind::Feature, source_feature.id});
            graph.insert({document::PartHistoryKind::Feature, target_feature.id});
            part.set_body_history(graph);
            part.resolve_constructions();
            const auto path = directory /
                ("parallel-inference-" + std::to_string(case_index++) + ".prtz");
            part.save(path);

            app::AssemblyWorkspaceWindow window(
                QString::fromStdString(document::path_to_utf8(directory)));
            window.setAttribute(Qt::WA_DontShowOnScreen);
            window.resize(1200, 900);
            window.show();
            check(window.open_document_path(QString::fromStdString(
                    document::path_to_utf8(path))),
                "Parallel inference fixture cannot open");
            flush();
            check(activate_test_body(application, window, body),
                "Parallel inference Body unavailable");
            flush();
            auto* tree = window.findChild<QTreeWidget*>("documentTree");
            QTreeWidgetItem* row{};
            for (QTreeWidgetItemIterator item(tree); *item; ++item) {
                if ((*item)->data(0, Qt::UserRole).toString().toStdString() ==
                        target_id &&
                    (*item)->data(0, Qt::UserRole + 3) == "part-sketch") {
                    row = *item;
                    break;
                }
            }
            check(row != nullptr, "Parallel inference Sketch row missing");
            window.show_tree_item_properties(row);
            flush();
            auto* open = window.findChild<QPushButton*>("sketchOpenButton");
            check(open != nullptr, "Parallel inference Sketch cannot open");
            open->click();
            flush();
            QEventLoop settle;
            QTimer::singleShot(950, &settle, &QEventLoop::quit);
            settle.exec();
            auto* view = dynamic_cast<viewer::MeshView*>(
                window.findChild<QOpenGLWidget*>());
            check(view != nullptr, "Parallel inference View missing");
            view->fit_points({target.world_point(-55.0, -35.0),
                target.world_point(55.0, 55.0)});
            flush();

            auto* toolbar = window.findChild<QToolBar*>("toolsToolbar");
            auto* symbol = window.findChild<QAction*>("insertSymbolAction");
            auto* image = window.findChild<QAction*>("templateImageAction");
            check(toolbar != nullptr && symbol != nullptr && image != nullptr &&
                    toolbar->actions().indexOf(image) ==
                        toolbar->actions().indexOf(symbol) + 1,
                "Sketch Image command is not immediately below Insert Symbol");

            const auto local = [&](QPointF screen_position) {
                const auto ray = view->ray_at(screen_position);
                check(ray.has_value(), "Parallel inference camera ray missing");
                const auto point = target.intersect_ray(ray->first, ray->second);
                check(point.has_value(),
                    "Parallel inference camera does not face Sketch");
                return *point;
            };
            const auto screen = [&](double x, double y) {
                const auto a = local({0.0, 0.0});
                const auto b = local({100.0, 0.0});
                const auto c = local({0.0, 100.0});
                const double ux = (b[0] - a[0]) / 100.0;
                const double uy = (b[1] - a[1]) / 100.0;
                const double vx = (c[0] - a[0]) / 100.0;
                const double vy = (c[1] - a[1]) / 100.0;
                const double determinant = ux * vy - uy * vx;
                return QPointF(
                    ((x - a[0]) * vy - (y - a[1]) * vx) / determinant,
                    (ux * (y - a[1]) - uy * (x - a[0])) / determinant);
            };
            const auto mouse = [&](QPointF position, QEvent::Type type,
                    Qt::MouseButton button,
                    Qt::MouseButtons buttons = Qt::NoButton) {
                QMouseEvent event(type, position,
                    QPointF(view->mapToGlobal(position.toPoint())), button,
                    buttons == Qt::NoButton && type == QEvent::MouseButtonPress
                        ? Qt::MouseButtons(button)
                        : buttons,
                    Qt::NoModifier);
                QApplication::sendEvent(view, &event);
                flush();
            };
            const auto click = [&](QPointF position) {
                mouse(position, QEvent::MouseMove, Qt::NoButton);
                mouse(position, QEvent::MouseButtonPress, Qt::LeftButton);
                mouse(position, QEvent::MouseButtonRelease, Qt::LeftButton);
            };
            auto* segment_action =
                window.findChild<QAction*>("sketchSegmentAction");
            check(segment_action != nullptr && segment_action->isEnabled(),
                "Parallel inference Segment command unavailable");
            segment_action->trigger();
            flush();
            const std::array start = horizontal
                ? std::array{-25.0, 18.0}
                : std::array{-25.0, 18.0};
            const std::array end = horizontal
                ? std::array{15.0, 18.0}
                : std::array{15.0, 38.0};
            click(screen(start[0], start[1]));
            const auto end_pixel = screen(end[0], end[1]);
            mouse(end_pixel, QEvent::MouseMove, Qt::NoButton);
            const bool parallel_preview = std::ranges::any_of(
                view->transient_labels(), [](const auto& label) {
                    return label.second.find("∥") != std::string::npos;
                });
            const bool horizontal_preview = std::ranges::any_of(
                view->transient_labels(), [](const auto& label) {
                    return label.second == "H";
                });
            const bool support_preview = std::ranges::any_of(
                view->transient_edges(), [](const auto& edge) {
                    return edge.reference.semantic_key == "inference:reference";
                });
            if (horizontal) {
                check(horizontal_preview && !parallel_preview && !support_preview,
                    "Horizontal external support did not retain H/V precedence");
            } else if (broken) {
                check(!parallel_preview && !support_preview,
                    "Broken external support offered a Parallel inference");
            } else {
                check(parallel_preview && support_preview,
                    "Oblique support did not show Parallel and its azure reference");
            }
            mouse(end_pixel, QEvent::MouseButtonPress, Qt::LeftButton);
            mouse(end_pixel, QEvent::MouseButtonRelease, Qt::LeftButton);
            QKeyEvent escape(QEvent::KeyPress, Qt::Key_Escape, Qt::NoModifier);
            QApplication::sendEvent(&window, &escape);
            flush();

            const auto* active = window.active_sketch_for_test();
            check(active != nullptr, "Parallel inference lost its active Sketch");
            const auto expected_segments = scenario == 1 ? 2U : 1U;
            check(active->segments.size() == expected_segments,
                "Parallel inference created the wrong segment count");
            const auto created_id = active->segments.back().id;
            const auto has_parallel = [&] {
                return std::ranges::any_of(active->constraints,
                    [&](const auto& relation) {
                        return relation.kind == K::Parallel &&
                            ((relation.geometry_id == support_id &&
                                 relation.second_geometry_id == created_id) ||
                                (relation.geometry_id == created_id &&
                                 relation.second_geometry_id == support_id));
                    });
            };
            const auto has_horizontal = [&] {
                return std::ranges::any_of(active->constraints,
                    [](const auto& relation) {
                        return relation.kind == K::Horizontal;
                    });
            };
            if (horizontal) {
                check(has_horizontal() && !has_parallel(),
                    "Horizontal support persisted external Parallel instead of H");
            } else if (broken) {
                check(!has_parallel(),
                    "Broken support persisted an automatic Parallel relation");
            } else {
                check(has_parallel(),
                    "Confirmed oblique line lost its automatic Parallel relation");
            }
            const auto parallel_equation = [&] {
                const auto* current = window.active_sketch_for_test();
                const auto segment = std::ranges::find(
                    current->segments, created_id, &sketcher::SketchSegment::id);
                const auto* first = current->find_point(segment->first_point_id);
                const auto* second = current->find_point(segment->second_point_id);
                const std::array direction{
                    second->x - first->x, second->y - first->y};
                std::array<double, 2> support_direction{80.0, 40.0};
                if (!external) {
                    const auto support = std::ranges::find(current->segments,
                        support_id, &sketcher::SketchSegment::id);
                    const auto* support_first =
                        current->find_point(support->first_point_id);
                    const auto* support_second =
                        current->find_point(support->second_point_id);
                    support_direction = {
                        support_second->x - support_first->x,
                        support_second->y - support_first->y};
                }
                return std::abs(direction[0] * support_direction[1] -
                           direction[1] * support_direction[0]) /
                    (std::hypot(direction[0], direction[1]) *
                        std::hypot(support_direction[0], support_direction[1]));
            };
            if (!horizontal && !broken) {
                check(parallel_equation() < 1.0e-8,
                    "Automatic Parallel relation does not satisfy its equation");
                for (const double step : {5.0, -3.0, 4.0}) {
                    active = window.active_sketch_for_test();
                    const auto segment = std::ranges::find(active->segments,
                        created_id, &sketcher::SketchSegment::id);
                    const auto* endpoint =
                        active->find_point(segment->second_point_id);
                    const auto before = active->serialized();
                    const auto begin = screen(endpoint->x, endpoint->y);
                    const auto finish = screen(
                        endpoint->x + step * 2.0, endpoint->y + step);
                    mouse(begin, QEvent::MouseMove, Qt::NoButton);
                    mouse(begin, QEvent::MouseButtonPress, Qt::LeftButton);
                    mouse(finish, QEvent::MouseMove, Qt::NoButton,
                        Qt::LeftButton);
                    mouse(finish, QEvent::MouseButtonRelease, Qt::LeftButton);
                    check(window.active_sketch_for_test()->serialized() != before,
                        "Underconstrained Parallel endpoint did not move in GUI");
                    check(parallel_equation() < 1.0e-8,
                        "GUI drag escaped the Parallel equation");
                    if (external) {
                        check(window.active_sketch_for_test()->external_references ==
                                external_definition,
                            "GUI drag changed the read-only external support");
                    }
                }
            }

            window.findChild<QAction*>("finishSketchAction")->trigger();
            flush();
            for (auto* dialog : window.findChildren<QDialog*>()) {
                if (!dialog->isVisible()) continue;
                auto* buttons = dialog->findChild<QDialogButtonBox*>();
                if (buttons != nullptr && buttons->button(QDialogButtonBox::Ok)) {
                    buttons->button(QDialogButtonBox::Ok)->click();
                    flush();
                    break;
                }
            }
            window.findChild<QAction*>("saveDocumentAction")->trigger();
            flush();
            const auto saved = document::PartDocument::load(path);
            const auto reopened = std::ranges::find(
                saved.sketches, target_id, &sketcher::Sketch::id);
            check(reopened != saved.sketches.end(),
                "Saved Parallel inference Sketch is missing");
            check(saved.sketches.front().serialized() == source_definition,
                "Parallel inference editing changed the source Sketch");
            if (external) {
                check(reopened->external_references == external_definition,
                    "Parallel inference changed its persisted external support");
            }
            const auto saved_definition = reopened->serialized();
            window.findChild<QAction*>("undoAction")->trigger();
            flush();
            window.findChild<QAction*>("redoAction")->trigger();
            flush();
            window.findChild<QAction*>("saveDocumentAction")->trigger();
            flush();
            const auto redone = document::PartDocument::load(path);
            const auto redone_sketch = std::ranges::find(
                redone.sketches, target_id, &sketcher::Sketch::id);
            check(redone_sketch != redone.sketches.end() &&
                    redone_sketch->serialized() == saved_definition,
                "Parallel inference Undo/Redo changed identities or geometry");
        }
        std::cout << "Automatic Parallel GUI: native/external oblique supports, "
                     "azure preview, repeated drag, H/V precedence, broken "
                     "references, toolbar order, Undo/Redo and reopen passed\n";
        return 0;
    } catch (const std::exception& error) {
        std::cerr << "Automatic Parallel GUI: " << error.what() << '\n';
        return 1;
    }
}
