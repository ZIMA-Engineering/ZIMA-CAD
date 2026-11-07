static int verify_sketch_combined_endpoint_ui(
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
        std::filesystem::create_directories(directory);
        unsigned case_index{};
        for (const double sign : {1.0, -1.0}) {
            for (const auto expected_relation : {K::Parallel, K::Perpendicular}) {
                auto part = document::PartDocument::create_default();
                auto feature = document::PartDocument::create_sketch_container();
                auto sketch = sketcher::Sketch::create_default();
                sketch.owner_container_id = feature.id;
                feature.feature.sketch_id = sketch.id;
                const double start_x = -15.0 * sign;
                const double end_x = start_x + 40.0 * sign;
                constexpr double contact_y = 7.0;
                const auto start_support = sketch.add_segment(
                    start_x, -30.0, start_x, 30.0);
                const auto end_support = sketch.add_segment(
                    end_x, -30.0, end_x, 30.0);
                const auto parallel_support = sketch.add_segment(
                    -8.5, -20.0, 8.5, -20.0);
                for (auto& point : sketch.points) point.fixed = true;
                part.history = {feature};
                part.sketches = {sketch};
                document::BodyHistoryGraph graph;
                const auto body = graph.create_body("Combined endpoint inference");
                graph.insert({document::PartHistoryKind::Feature, feature.id});
                part.set_body_history(graph);
                part.resolve_constructions();
                const auto file = directory /
                    ("combined-endpoint-" + std::to_string(case_index++) + ".prtz");
                part.save(file);

                app::AssemblyWorkspaceWindow window(
                    QString::fromStdString(document::path_to_utf8(directory)));
                window.resize(1200, 900);
                window.show();
                check(window.open_document_path(QString::fromStdString(
                    document::path_to_utf8(file))),
                    "Combined endpoint fixture cannot open");
                flush();
                check(activate_test_body(application, window, body),
                    "Combined endpoint Body unavailable");
                flush();
                auto* tree = window.findChild<QTreeWidget*>("documentTree");
                QTreeWidgetItem* row{};
                for (QTreeWidgetItemIterator item(tree); *item; ++item) {
                    if ((*item)->data(0, Qt::UserRole).toString().toStdString() ==
                            sketch.id &&
                        (*item)->data(0, Qt::UserRole + 3) == "part-sketch") {
                        row = *item;
                        break;
                    }
                }
                check(row != nullptr, "Combined endpoint Sketch row missing");
                window.show_tree_item_properties(row);
                flush();
                auto* open = window.findChild<QPushButton*>("sketchOpenButton");
                check(open != nullptr, "Combined endpoint open button missing");
                open->click();
                flush();
                QEventLoop settle;
                QTimer::singleShot(950, &settle, &QEventLoop::quit);
                settle.exec();
                auto* view = dynamic_cast<viewer::MeshView*>(
                    window.findChild<QOpenGLWidget*>());
                check(view != nullptr, "Combined endpoint View missing");
                view->fit_points({sketch.world_point(-65.0, -40.0),
                    sketch.world_point(65.0, 40.0)});
                flush();
                const auto local = [&](QPointF screen_position) {
                    const auto ray = view->ray_at(screen_position);
                    check(ray.has_value(), "Combined endpoint camera ray missing");
                    const auto point = sketch.intersect_ray(ray->first, ray->second);
                    check(point.has_value(), "Combined endpoint camera misses Sketch");
                    return *point;
                };
                const auto a = local({0.0, 0.0});
                const auto b = local({100.0, 0.0});
                const auto c = local({0.0, 100.0});
                const double ux = (b[0] - a[0]) / 100.0;
                const double uy = (b[1] - a[1]) / 100.0;
                const double vx = (c[0] - a[0]) / 100.0;
                const double vy = (c[1] - a[1]) / 100.0;
                const double determinant = ux * vy - uy * vx;
                const auto screen = [&](double x, double y) {
                    return QPointF(((x - a[0]) * vy - (y - a[1]) * vx) /
                            determinant,
                        (ux * (y - a[1]) - uy * (x - a[0])) /
                            determinant);
                };
                const auto mouse = [&](QPointF position, QEvent::Type type,
                        Qt::MouseButton button,
                        Qt::MouseButtons buttons = Qt::NoButton) {
                    QMouseEvent event(type, position,
                        QPointF(view->mapToGlobal(position.toPoint())), button,
                        buttons == Qt::NoButton &&
                                type == QEvent::MouseButtonPress
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
                    "Combined endpoint Segment command unavailable");
                segment_action->trigger();
                flush();
                click(screen(start_x, contact_y));
                const auto endpoint = screen(end_x, contact_y);
                mouse(endpoint, QEvent::MouseMove, Qt::NoButton);
                check(view->hovered_candidate().has_value() &&
                        view->hovered_candidate()->semantic_key ==
                            "segment:" + end_support,
                    "Second endpoint did not retain its C support");
                const auto offers_expected = [&] {
                    const auto& current_labels = view->transient_labels();
                    const bool has_contact = std::ranges::any_of(
                        current_labels, [](const auto& label) {
                            return label.second == "C";
                        });
                    const bool has_relation = std::ranges::any_of(
                        current_labels, [&](const auto& label) {
                            return expected_relation == K::Parallel
                                ? label.second.find("∥") != std::string::npos
                                : label.second.find("⊥") != std::string::npos;
                        });
                    return has_contact && has_relation;
                };
                std::vector<std::string> observed_variants;
                const auto remember_variant = [&] {
                    std::string value;
                    for (const auto& label : view->transient_labels())
                        value += '[' + label.second + ']';
                    observed_variants.push_back(std::move(value));
                };
                remember_variant();
                for (unsigned cycle = 0;
                     cycle < 32 && !offers_expected(); ++cycle) {
                    mouse(endpoint, QEvent::MouseButtonPress, Qt::RightButton);
                    mouse(endpoint, QEvent::MouseButtonRelease, Qt::RightButton);
                    remember_variant();
                }
                if (!offers_expected()) {
                    std::cerr << "Combined endpoint preview failed: sign="
                              << sign << " relation="
                              << (expected_relation == K::Parallel
                                      ? "Parallel" : "Perpendicular")
                              << " variants=";
                    for (const auto& value : observed_variants)
                        std::cerr << '{' << value << '}';
                    std::cerr << '\n';
                    throw std::runtime_error(
                        "RMB did not expose the expected combined relation");
                }
                mouse(endpoint, QEvent::MouseButtonPress, Qt::LeftButton);
                mouse(endpoint, QEvent::MouseButtonRelease, Qt::LeftButton);
                QKeyEvent escape(QEvent::KeyPress, Qt::Key_Escape,
                    Qt::NoModifier);
                QApplication::sendEvent(view, &escape);
                flush();
                window.findChild<QAction*>("finishSketchAction")->trigger();
                flush();
                app::SketchPropertiesDialog* properties{};
                for (auto* dialog : window.findChildren<QDialog*>()) {
                    if (auto* candidate =
                            dynamic_cast<app::SketchPropertiesDialog*>(dialog);
                        candidate != nullptr && candidate->isVisible()) {
                        properties = candidate;
                    }
                }
                check(properties != nullptr,
                    "Combined endpoint did not return to Sketch Properties");
                const auto pending = properties->pending_value().first;
                check(pending.segments.size() == sketch.segments.size() + 1,
                    "Combined endpoint did not create exactly one segment");
                const auto& created = pending.segments.back();
                const auto owns_pair = [&](const auto& relation,
                        const std::string& support) {
                    return relation.kind == expected_relation &&
                        ((relation.geometry_id == support &&
                             relation.second_geometry_id == created.id) ||
                            (relation.geometry_id == created.id &&
                             relation.second_geometry_id == support));
                };
                const auto expected_support = expected_relation == K::Parallel
                    ? parallel_support : start_support;
                check(std::ranges::any_of(pending.constraints,
                          [&](const auto& relation) {
                              return owns_pair(relation, expected_support);
                          }),
                    "Confirmed combined relation used the wrong support");
                check(std::ranges::any_of(pending.constraints,
                          [&](const auto& relation) {
                              return relation.kind == K::PointOnLine &&
                                  relation.first_point_id ==
                                      created.first_point_id &&
                                  relation.geometry_id == start_support;
                          }) &&
                        std::ranges::any_of(pending.constraints,
                            [&](const auto& relation) {
                                return relation.kind == K::PointOnLine &&
                                    relation.first_point_id ==
                                        created.second_point_id &&
                                    relation.geometry_id == end_support;
                            }),
                    "Confirmed direction relation discarded an endpoint C");
                const auto verify_equations = [&](const sketcher::Sketch& value) {
                    const auto& line = *std::ranges::find(
                        value.segments, created.id, &sketcher::SketchSegment::id);
                    const auto* first = value.find_point(line.first_point_id);
                    const auto* second = value.find_point(line.second_point_id);
                    check(first != nullptr && second != nullptr,
                        "Combined endpoint point identity disappeared");
                    check(std::abs(first->x - start_x) < 1.0e-7 &&
                            std::abs(second->x - end_x) < 1.0e-7 &&
                            std::abs(first->y - second->y) < 1.0e-7,
                        "Combined endpoint equations are not satisfied");
                };
                verify_equations(pending);
                properties->buttons()->button(QDialogButtonBox::Ok)->click();
                flush();
                window.findChild<QAction*>("saveDocumentAction")->trigger();
                flush();
                verify_equations(document::PartDocument::load(file).sketches.front());

                row = nullptr;
                for (QTreeWidgetItemIterator item(tree); *item; ++item) {
                    if ((*item)->data(0, Qt::UserRole).toString().toStdString() ==
                            sketch.id &&
                        (*item)->data(0, Qt::UserRole + 3) == "part-sketch") {
                        row = *item;
                        break;
                    }
                }
                check(row != nullptr,
                    "Committed combined endpoint Sketch row disappeared");
                window.show_tree_item_properties(row);
                flush();
                window.findChild<QPushButton*>("sketchOpenButton")->click();
                flush();
                QEventLoop reopen_settle;
                QTimer::singleShot(950, &reopen_settle, &QEventLoop::quit);
                reopen_settle.exec();
                view->fit_points({sketch.world_point(-65.0, -40.0),
                    sketch.world_point(65.0, 40.0)});
                flush();
                const auto drag_a = local({0.0, 0.0});
                const auto drag_b = local({100.0, 0.0});
                const auto drag_c = local({0.0, 100.0});
                const double drag_ux = (drag_b[0] - drag_a[0]) / 100.0;
                const double drag_uy = (drag_b[1] - drag_a[1]) / 100.0;
                const double drag_vx = (drag_c[0] - drag_a[0]) / 100.0;
                const double drag_vy = (drag_c[1] - drag_a[1]) / 100.0;
                const double drag_determinant =
                    drag_ux * drag_vy - drag_uy * drag_vx;
                const auto drag_screen = [&](double x, double y) {
                    return QPointF(((x - drag_a[0]) * drag_vy -
                                       (y - drag_a[1]) * drag_vx) /
                            drag_determinant,
                        (drag_ux * (y - drag_a[1]) -
                            drag_uy * (x - drag_a[0])) /
                            drag_determinant);
                };
                const auto view_point = [&](const std::string& point_id) {
                    const auto found = std::ranges::find_if(view->mesh().points,
                        [&](const auto& point) {
                            return point.reference.owner_id == sketch.id &&
                                point.reference.semantic_key ==
                                    "point:" + point_id;
                        });
                    check(found != view->mesh().points.end(),
                        "Combined endpoint View point disappeared");
                    return sketch.local_point(found->position);
                };
                view->clear_selection();
                tree->clearSelection();
                flush();
                view->set_selection_filter(viewer::SelectionFilter::Points);
                for (const double delta : {3.0, -5.0, 4.0}) {
                    const auto before = view_point(created.second_point_id);
                    const auto approximate = drag_screen(before[0], before[1]);
                    std::optional<QPointF> picked_point;
                    std::size_t picked_candidate_index{};
                    for (int y = -8; y <= 8 && !picked_point; ++y) {
                        for (int x = -8; x <= 8; ++x) {
                            const QPointF candidate_position =
                                approximate + QPointF(x, y);
                            const auto candidates =
                                view->selection_candidates_at(candidate_position);
                            const auto found = std::ranges::find_if(candidates,
                                [&](const auto& candidate) {
                                    return candidate.kind ==
                                                viewer::CandidateKind::SketchPoint &&
                                        candidate.owner_id == sketch.id &&
                                        candidate.semantic_key ==
                                            "point:" + created.second_point_id;
                                });
                            if (found != candidates.end()) {
                                picked_point = candidate_position;
                                picked_candidate_index =
                                    std::size_t(found - candidates.begin());
                                break;
                            }
                        }
                    }
                    check(picked_point.has_value(),
                        "Combined endpoint point is not pickable after reopen");
                    const auto from = *picked_point;
                    const auto to = drag_screen(before[0], before[1] + delta);
                    view->clear_selection();
                    tree->clearSelection();
                    view->reset_candidate_cycle();
                    mouse(from, QEvent::MouseMove, Qt::NoButton);
                    for (std::size_t index = 0;
                         index < picked_candidate_index; ++index) {
                        mouse(from, QEvent::MouseButtonPress, Qt::RightButton);
                        mouse(from, QEvent::MouseButtonRelease, Qt::RightButton);
                    }
                    check(view->hovered_candidate().has_value() &&
                            view->hovered_candidate()->kind ==
                                viewer::CandidateKind::SketchPoint,
                        "Combined endpoint drag did not select its point");
                    mouse(from, QEvent::MouseButtonPress, Qt::LeftButton);
                    mouse(to, QEvent::MouseMove, Qt::NoButton,
                        Qt::LeftButton);
                    mouse(to, QEvent::MouseButtonRelease, Qt::LeftButton);
                    const auto after = view_point(created.second_point_id);
                    check(std::abs(after[1] - before[1]) > 1.0e-5,
                        "Combined endpoint free degree of freedom did not move");
                    const auto first = view_point(created.first_point_id);
                    check(std::abs(first[0] - start_x) < 1.0e-7 &&
                            std::abs(after[0] - end_x) < 1.0e-7 &&
                            std::abs(first[1] - after[1]) < 1.0e-7,
                        "Repeated drag broke C plus direction");
                }
                view->set_selection_filter(viewer::SelectionFilter::All);
                window.findChild<QAction*>("finishSketchAction")->trigger();
                flush();
                for (auto* dialog : window.findChildren<QDialog*>()) {
                    if (auto* candidate =
                            dynamic_cast<app::SketchPropertiesDialog*>(dialog);
                        candidate != nullptr && candidate->isVisible()) {
                        candidate->buttons()->button(QDialogButtonBox::Ok)->click();
                        break;
                    }
                }
                flush();
                window.findChild<QAction*>("undoAction")->trigger();
                flush();
                window.findChild<QAction*>("redoAction")->trigger();
                flush();
                window.findChild<QAction*>("saveDocumentAction")->trigger();
                flush();
                verify_equations(document::PartDocument::load(file).sketches.front());
            }
        }
        std::cout << "C plus Parallel/Perpendicular endpoint inference: both "
                     "directions, RMB order, repeated GUI drag, Undo/Redo and "
                     "save/reopen passed\n";
        return 0;
    } catch (const std::exception& error) {
        std::cerr << error.what() << '\n';
        return 1;
    }
}
