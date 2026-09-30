#include "profile_request_fixture.hpp"
#include <zima/assembly/assembly_document.hpp>
#include <zima/kernel/occt_kernel.hpp>
#include <zima/kernel/dimension_layout.hpp>
#include <zima/viewer/mesh_view.hpp>

#include <QApplication>
#include <QEventLoop>
#include <QMouseEvent>
#include <QTimer>
#include <chrono>
#include <fstream>
#include <iomanip>
#include <iostream>
#include <stdexcept>

namespace {
template<class Function>
double milliseconds(Function&& function, int repetitions) {
    const auto start = std::chrono::steady_clock::now();
    for (int i = 0; i < repetitions; ++i) function();
    return std::chrono::duration<double, std::milli>(
        std::chrono::steady_clock::now() - start).count() / repetitions;
}
void require(bool condition, const char* message) {
    if (!condition) throw std::runtime_error(message);
}
}

// Run on a real desktop to include MeshView preparation and mouse dispatch.
// Paint/GPU time is deliberately excluded from the mouse-dispatch measurement.
int main(int argc, char** argv) {
    QApplication application(argc, argv);
    try {
        zima::kernel::OcctKernel kernel;
        const zima::kernel::BodySnapshot body = zima::test::profile_body(kernel, {10,10,10});
        zima::viewer::MeshView view;
        view.resize(1200,800);
        view.set_selection_contract({zima::viewer::CandidateKind::Occurrence});
        view.show(); application.processEvents();
        require(view.isValid(), "Benchmark requires a working desktop OpenGL context");
        std::cout << std::fixed << std::setprecision(3);
        for (int count : {256,1024}) {
            auto assembly = zima::assembly::AssemblyDocument::create_default();
            assembly.document_id = "benchmark-assembly";
            for (int i = 0; i < count; ++i) {
                auto item = zima::assembly::AssemblyDocument::create_part_occurrence(
                    "Part", "source", {}, body);
                item.occurrence_id = "occ-" + std::to_string(i);
                item.placement.x = (i % 32) * 14.; item.placement.y = (i / 32) * 14.;
                assembly.components.push_back(std::move(item));
            }
            auto scene = assembly.build_scene();
            const auto scene_ms = milliseconds([&] { scene = assembly.build_scene(); }, 10);
            std::map<zima::kernel::ObjectEnvelopeKey,zima::kernel::ModelEnvelope> bounds;
            const auto bounds_ms = milliseconds([&] { bounds = zima::kernel::object_envelopes(scene); }, 5);
            require(!bounds.empty(), "Benchmark object bounds are missing");
            if (argc > 1) {
                std::ofstream snapshot(std::string(argv[1]) + "-" + std::to_string(count) + ".txt");
                snapshot << std::hexfloat;
                for (const auto& [key, frame] : bounds) {
                    snapshot << std::quoted(key.first) << ' ' << std::quoted(key.second) << ' ' << frame.valid;
                    for (const auto p : {frame.minimum, frame.maximum, frame.origin,
                                         frame.axes[0], frame.axes[1], frame.axes[2]})
                        snapshot << ' ' << p.x << ' ' << p.y << ' ' << p.z;
                    snapshot << '\n';
                }
                require(bool(snapshot), "Cannot write bounds snapshot");
            }
            const auto set_ms = milliseconds([&] { view.set_mesh(scene); }, 5);
            view.set_standard_view(zima::viewer::StandardView::Top);
            // Allow the normal 850 ms camera transition to finish before picking.
            QEventLoop settle;
            QTimer::singleShot(1100, &settle, &QEventLoop::quit); settle.exec();
            view.repaint(); application.processEvents();
            // The middle screen row can pass through an empty gap. Project
            // actual occurrence centres through the settled orthographic rays.
            std::vector<QPointF> positions;
            const auto base = view.ray_at({0,0})->first;
            const auto horizontal = view.ray_at({1,0})->first;
            const auto vertical = view.ray_at({0,1})->first;
            for (int i = 0; i < 20; ++i)
                positions.push_back({(14.*(6+i)+5-base.x)/(horizontal.x-base.x),
                    (14.*(count/64)+5-base.y)/(vertical.y-base.y)});
            std::size_t candidates = 0;
            const auto pick_ms = milliseconds([&] {
                for (int i = 0; i < 20; ++i)
                    candidates += view.selection_candidates_at(positions[i]).size();
            }, 5) / 20.;
            require(candidates > 0, "Benchmark did not hit any occurrence");
            const auto mouse_ms = milliseconds([&] {
                for (int i = 0; i < 20; ++i) {
                    const auto position = positions[i];
                    QMouseEvent event(QEvent::MouseMove, position, view.mapToGlobal(position),
                        Qt::NoButton, Qt::NoButton, Qt::NoModifier);
                    QApplication::sendEvent(&view, &event);
                }
            }, 5) / 20.;
            // Confirmation must consume the exact occurrence offered on hover.
            bool confirmed = false;
            for (int i = 0; i < 20 && !confirmed; ++i) {
                const auto position = positions[i];
                const auto offered = view.selection_candidates_at(position);
                if (offered.empty()) continue;
                QMouseEvent move(QEvent::MouseMove, position, view.mapToGlobal(position),
                    Qt::NoButton, Qt::NoButton, Qt::NoModifier);
                QApplication::sendEvent(&view, &move);
                require(view.hovered_candidate() == offered.front(), "Hover changed the offered occurrence");
                QMouseEvent press(QEvent::MouseButtonPress, position, view.mapToGlobal(position),
                    Qt::LeftButton, Qt::LeftButton, Qt::NoModifier);
                QApplication::sendEvent(&view, &press);
                QMouseEvent release(QEvent::MouseButtonRelease, position, view.mapToGlobal(position),
                    Qt::LeftButton, Qt::NoButton, Qt::NoModifier);
                QApplication::sendEvent(&view, &release);
                require(view.confirmed_candidate() == offered.front(), "Click changed the offered occurrence");
                confirmed = true;
            }
            require(confirmed, "No occurrence was confirmed");
            std::cout << "occurrences=" << count << " triangles=" << scene.triangles.size()/3
                << " scene_ms=" << scene_ms << " set_mesh_ms=" << set_ms
                << " object_bounds_ms=" << bounds_ms
                << " pick_ms=" << pick_ms << " mouse_dispatch_ms=" << mouse_ms
                << " candidates=" << candidates << " confirmation=passed\n" << std::flush;
        }
        return 0;
    } catch (const std::exception& error) {
        std::cerr << error.what() << '\n'; return 1;
    }
}
