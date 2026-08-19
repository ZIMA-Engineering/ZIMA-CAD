#include "drawing_window.hpp"

#include <zima/assembly/assembly_document.hpp>
#include <zima/document/part_document.hpp>
#include <zima/ui/properties_subwindow.hpp>

#include <QAction>
#include <QComboBox>
#include <QDoubleSpinBox>
#include <QFileDialog>
#include <QFormLayout>
#include <QLabel>
#include <QMenuBar>
#include <QMessageBox>
#include <QMouseEvent>
#include <QPainter>
#include <QPushButton>
#include <QTabBar>
#include <QVBoxLayout>

#include <algorithm>
#include <chrono>
#include <cmath>
#include <sstream>

namespace zima::app {
namespace {

class ViewPropertiesDialog final : public zima::ui::PropertiesSubWindow {
public:
    ViewPropertiesDialog(QMainWindow* parent, zima::drawing::DrawingView initial,
                         std::function<void(zima::drawing::DrawingView)> accepted)
        : PropertiesSubWindow(QObject::tr("Vlastnosti pohledu"), parent),
          value_(std::move(initial)), accepted_(std::move(accepted)) {
        auto* content = new QWidget(this);
        auto* form = new QFormLayout(content);
        orientation_ = new QComboBox(content);
        orientation_->addItem(QObject::tr("Přední"), static_cast<int>(zima::drawing::ViewOrientation::Front));
        orientation_->addItem(QObject::tr("Zadní"), static_cast<int>(zima::drawing::ViewOrientation::Back));
        orientation_->addItem(QObject::tr("Levý"), static_cast<int>(zima::drawing::ViewOrientation::Left));
        orientation_->addItem(QObject::tr("Pravý"), static_cast<int>(zima::drawing::ViewOrientation::Right));
        orientation_->addItem(QObject::tr("Horní"), static_cast<int>(zima::drawing::ViewOrientation::Top));
        orientation_->addItem(QObject::tr("Dolní"), static_cast<int>(zima::drawing::ViewOrientation::Bottom));
        orientation_->addItem(QObject::tr("Izometrický"), static_cast<int>(zima::drawing::ViewOrientation::Isometric));
        orientation_->setCurrentIndex(orientation_->findData(static_cast<int>(value_.orientation)));
        display_ = new QComboBox(content);
        display_->addItem(QObject::tr("Pouze viditelné hrany"),
                          static_cast<int>(zima::drawing::DisplayStyle::VisibleEdges));
        display_->addItem(QObject::tr("Viditelné a skryté hrany"),
                          static_cast<int>(zima::drawing::DisplayStyle::HiddenEdges));
        display_->addItem(QObject::tr("Stínované s hranami"),
                          static_cast<int>(zima::drawing::DisplayStyle::ShadedWithEdges));
        display_->setCurrentIndex(display_->findData(static_cast<int>(value_.display_style)));
        scale_ = new QDoubleSpinBox(content);
        scale_->setDecimals(3); scale_->setRange(0.001, 1000.0); scale_->setValue(value_.scale);
        x_ = new QDoubleSpinBox(content);
        y_ = new QDoubleSpinBox(content);
        for (auto* field : {x_, y_}) { field->setDecimals(3); field->setRange(-10000.0, 10000.0); }
        x_->setValue(value_.x); y_->setValue(value_.y);
        form->addRow(QObject::tr("Orientace"), orientation_);
        form->addRow(QObject::tr("Zobrazení"), display_);
        form->addRow(QObject::tr("Měřítko"), scale_);
        form->addRow(QObject::tr("Poloha X [mm]"), x_);
        form->addRow(QObject::tr("Poloha Y [mm]"), y_);
        content_layout()->addWidget(content);
        setAttribute(Qt::WA_DeleteOnClose);
    }
private:
    zima::drawing::DrawingView value_;
    std::function<void(zima::drawing::DrawingView)> accepted_;
    QComboBox* orientation_{};
    QComboBox* display_{};
    QDoubleSpinBox* scale_{};
    QDoubleSpinBox* x_{};
    QDoubleSpinBox* y_{};
    bool submit() override {
        value_.orientation = static_cast<zima::drawing::ViewOrientation>(
            orientation_->currentData().toInt());
        value_.display_style = static_cast<zima::drawing::DisplayStyle>(
            display_->currentData().toInt());
        value_.scale = scale_->value(); value_.x = x_->value(); value_.y = y_->value();
        accepted_(std::move(value_));
        return true;
    }
};

class SheetPropertiesDialog final : public zima::ui::PropertiesSubWindow {
public:
    SheetPropertiesDialog(QMainWindow* parent, zima::drawing::DrawingSheet initial,
                          std::function<void(zima::drawing::DrawingSheet)> accepted)
        : PropertiesSubWindow(QObject::tr("Vlastnosti listu"), parent),
          value_(std::move(initial)), accepted_(std::move(accepted)) {
        auto* content = new QWidget(this); auto* form = new QFormLayout(content);
        format_ = new QComboBox(content);
        for (const auto& name : {"A4", "A3", "A2", "A1", "A0"}) format_->addItem(name);
        format_->setCurrentIndex(static_cast<int>(value_.format));
        projection_ = new QComboBox(content);
        projection_->addItem(QObject::tr("První kvadrant"), 0);
        projection_->addItem(QObject::tr("Třetí kvadrant"), 1);
        projection_->setCurrentIndex(value_.projection_method == zima::drawing::ProjectionMethod::FirstAngle ? 0 : 1);
        scale_ = new QDoubleSpinBox(content); scale_->setRange(0.001, 1000.0);
        scale_->setDecimals(3); scale_->setValue(value_.default_scale);
        form->addRow(QObject::tr("Formát"), format_);
        form->addRow(QObject::tr("Promítání"), projection_);
        form->addRow(QObject::tr("Výchozí měřítko"), scale_);
        content_layout()->addWidget(content); setAttribute(Qt::WA_DeleteOnClose);
    }
private:
    zima::drawing::DrawingSheet value_;
    std::function<void(zima::drawing::DrawingSheet)> accepted_;
    QComboBox* format_{}; QComboBox* projection_{}; QDoubleSpinBox* scale_{};
    bool submit() override {
        value_.format = static_cast<zima::drawing::SheetFormat>(format_->currentIndex());
        value_.projection_method = projection_->currentIndex() == 0
            ? zima::drawing::ProjectionMethod::FirstAngle
            : zima::drawing::ProjectionMethod::ThirdAngle;
        value_.default_scale = scale_->value(); accepted_(std::move(value_)); return true;
    }
};

std::pair<std::string, zima::kernel::ViewerMesh> load_drawing_source(
    const std::filesystem::path& path) {
    if (path.extension() == ".prtz") {
        std::vector<zima::kernel::BodyResult> boundaries;
        const auto part = zima::document::PartDocument::load(path, &boundaries);
        if (boundaries.empty()) throw std::runtime_error(
            "Part nemá uložený vypočtený model. Nejprve jej regenerujte a uložte.");
        return {part.document_id, std::move(boundaries.back().mesh)};
    }
    if (path.extension() == ".asmz") {
        const auto assembly = zima::assembly::AssemblyDocument::load(path);
        return {assembly.document_id, assembly.build_scene()};
    }
    throw std::runtime_error("Nepodporovaný zdroj výkresového pohledu.");
}

}  // namespace

class DrawingCanvas final : public QWidget {
public:
    explicit DrawingCanvas(QWidget* parent = nullptr) : QWidget(parent) {
        setMinimumSize(640, 480); setMouseTracking(true);
    }
    void set_sheet(zima::drawing::DrawingSheet* sheet) { sheet_ = sheet; selected_.clear(); update(); }
    [[nodiscard]] const std::string& selected_view_id() const { return selected_; }
    void start_linear_dimension() { dimension_mode_ = true; first_edge_.reset(); update(); }
    [[nodiscard]] bool dimension_mode() const { return dimension_mode_; }
protected:
    void paintEvent(QPaintEvent*) override {
        QPainter painter(this);
        painter.fillRect(rect(), QColor("#53575b"));
        if (sheet_ == nullptr) return;
        const double margin = 24.0;
        const double zoom = std::min((width() - 2 * margin) / sheet_->width_mm(),
                                     (height() - 2 * margin) / sheet_->height_mm());
        const QPointF origin((width() - sheet_->width_mm() * zoom) * 0.5,
                             (height() - sheet_->height_mm() * zoom) * 0.5);
        const QRectF paper(origin.x(), origin.y(), sheet_->width_mm() * zoom,
                           sheet_->height_mm() * zoom);
        painter.fillRect(paper, Qt::white);
        painter.setRenderHint(QPainter::Antialiasing, true);
        painter.setPen(QPen(Qt::black, 1.0));
        const double frame = 10.0 * zoom;
        painter.drawRect(paper.adjusted(frame, frame, -frame, -frame));
        const QRectF title(paper.right() - 80.0 * zoom, paper.bottom() - 30.0 * zoom,
                           70.0 * zoom, 20.0 * zoom);
        painter.drawRect(title);
        painter.drawText(title.adjusted(3, 3, -3, -3), Qt::AlignLeft | Qt::AlignTop,
                         QObject::tr("ZIMA-CAD\n%1").arg(QString::fromStdString(sheet_->name)));
        for (const auto& view : sheet_->views) {
            if (view.display_style != zima::drawing::DisplayStyle::ShadedWithEdges) continue;
            painter.setPen(Qt::NoPen);
            for (const auto& triangle : view.projected_triangles) {
                const int shade = std::clamp(static_cast<int>(255.0 * triangle.light), 0, 255);
                painter.setBrush(QColor(shade, shade, shade));
                QPolygonF polygon;
                for (const auto& point : triangle.points)
                    polygon << QPointF(origin.x() + (view.x + point.x * view.scale) * zoom,
                                       origin.y() + (view.y - point.y * view.scale) * zoom);
                painter.drawPolygon(polygon);
            }
        }
        for (const auto& view : sheet_->views) {
            for (const auto& edge : view.projected_edges) {
                if (edge.hidden && view.display_style == zima::drawing::DisplayStyle::VisibleEdges)
                    continue;
                QPen pen(view.id == selected_ ? QColor("#00bcd4")
                                              : edge.hidden ? QColor("#777777") : Qt::black,
                         view.id == selected_ ? 2.0 : 1.0);
                if (edge.hidden) pen.setStyle(Qt::DashLine);
                painter.setPen(pen);
                if (edge.points.size() < 2) continue;
                QPolygonF line;
                for (const auto& point : edge.points) {
                    line << QPointF(origin.x() + (view.x + point.x * view.scale) * zoom,
                                    origin.y() + (view.y - point.y * view.scale) * zoom);
                }
                painter.drawPolyline(line);
            }
        }
        painter.setPen(QPen(QColor("#d6a600"), 1.0));
        for (const auto& dimension : sheet_->dimensions) {
            const auto* view = [&]() -> const zima::drawing::DrawingView* {
                const auto found = std::find_if(sheet_->views.begin(), sheet_->views.end(),
                    [&](const auto& item) { return item.id == dimension.view_id; });
                return found == sheet_->views.end() ? nullptr : &*found;
            }();
            if (view == nullptr) continue;
            const auto screen = [&](const zima::drawing::Point2& point) {
                return QPointF(origin.x() + (view->x + point.x * view->scale) * zoom,
                               origin.y() + (view->y - point.y * view->scale) * zoom);
            };
            const QPointF first = screen(dimension.first_point);
            const QPointF second = screen(dimension.second_point);
            const QPointF label = screen(dimension.label_position);
            painter.drawLine(first, label); painter.drawLine(second, label);
            painter.drawLine(first, second);
            painter.drawText(label + QPointF(4, -4),
                QString::number(dimension.measured_value, 'f', 3) + tr(" mm"));
        }
    }
    void mousePressEvent(QMouseEvent* event) override {
        if (sheet_ == nullptr || event->button() != Qt::LeftButton) return;
        const double margin = 24.0;
        const double zoom = std::min((width() - 2 * margin) / sheet_->width_mm(),
                                     (height() - 2 * margin) / sheet_->height_mm());
        const QPointF origin((width() - sheet_->width_mm() * zoom) * 0.5,
                             (height() - sheet_->height_mm() * zoom) * 0.5);
        const auto segment_distance = [](QPointF point, QPointF a, QPointF b) {
            const QPointF delta = b - a;
            const double length_squared = delta.x() * delta.x() + delta.y() * delta.y();
            if (length_squared <= 1e-12) return std::hypot(point.x() - a.x(), point.y() - a.y());
            const double parameter = std::clamp(
                ((point.x() - a.x()) * delta.x() + (point.y() - a.y()) * delta.y()) /
                    length_squared, 0.0, 1.0);
            const QPointF nearest = a + parameter * delta;
            return std::hypot(point.x() - nearest.x(), point.y() - nearest.y());
        };
        double best = 8.0;
        std::string hit;
        const zima::drawing::DrawingView* hit_view{};
        const zima::drawing::ProjectedEdge* hit_edge{};
        for (const auto& view : sheet_->views) for (const auto& edge : view.projected_edges) {
            if (edge.points.size() < 2 ||
                (edge.hidden && view.display_style == zima::drawing::DisplayStyle::VisibleEdges)) continue;
            for (std::size_t point = 1; point < edge.points.size(); ++point) {
                const auto screen = [&](const auto& value) {
                    return QPointF(origin.x() + (view.x + value.x * view.scale) * zoom,
                                   origin.y() + (view.y - value.y * view.scale) * zoom);
                };
                const double distance = segment_distance(event->position(),
                    screen(edge.points[point - 1]), screen(edge.points[point]));
                if (distance < best) { best = distance; hit = view.id; hit_view = &view; hit_edge = &edge; }
            }
        }
        if (dimension_mode_ && hit_view != nullptr && hit_edge != nullptr && hit_edge->points.size() >= 2) {
            if (!first_edge_) {
                first_edge_ = PickedEdge{hit_view->id, *hit_edge};
            } else if (first_edge_->view_id == hit_view->id) {
                const auto& a = first_edge_->edge.points;
                const auto& b = hit_edge->points;
                const zima::drawing::Point2 a_mid{(a.front().x + a.back().x) * 0.5,
                                                   (a.front().y + a.back().y) * 0.5};
                const zima::drawing::Point2 b_mid{(b.front().x + b.back().x) * 0.5,
                                                   (b.front().y + b.back().y) * 0.5};
                const double dx = a.back().x - a.front().x;
                const double dy = a.back().y - a.front().y;
                const double length = std::hypot(dx, dy);
                const double bx = b.back().x - b.front().x;
                const double by = b.back().y - b.front().y;
                const double b_length = std::hypot(bx, by);
                if (length > 1e-9 && b_length > 1e-9 &&
                    std::abs(dx * by - dy * bx) <= 1e-5 * length * b_length) {
                    zima::drawing::LinearDimension dimension;
                    std::ostringstream id;
                    id << "dimension-" << std::chrono::steady_clock::now().time_since_epoch().count();
                    dimension.id = id.str(); dimension.view_id = hit_view->id;
                    dimension.first = first_edge_->edge.source; dimension.second = hit_edge->source;
                    dimension.first_point = a_mid; dimension.second_point = b_mid;
                    dimension.label_position = {(a_mid.x + b_mid.x) * 0.5,
                                                (a_mid.y + b_mid.y) * 0.5};
                    dimension.measured_value = std::abs((b_mid.x - a_mid.x) * -dy / length +
                                                        (b_mid.y - a_mid.y) * dx / length);
                    sheet_->dimensions.push_back(std::move(dimension));
                    dimension_mode_ = false; first_edge_.reset();
                }
            }
            update(); return;
        }
        selected_ = std::move(hit);
        if (!selected_.empty()) {
            drag_view_id_ = selected_; drag_start_ = event->position();
            if (const auto found = std::find_if(sheet_->views.begin(), sheet_->views.end(),
                    [&](const auto& view) { return view.id == drag_view_id_; });
                found != sheet_->views.end()) drag_origin_ = {found->x, found->y};
        }
        update();
    }
    void mouseMoveEvent(QMouseEvent* event) override {
        if (sheet_ == nullptr || drag_view_id_.empty() ||
            !(event->buttons() & Qt::LeftButton)) return;
        const double margin = 24.0;
        const double zoom = std::min((width() - 2 * margin) / sheet_->width_mm(),
                                     (height() - 2 * margin) / sheet_->height_mm());
        const auto found = std::find_if(sheet_->views.begin(), sheet_->views.end(),
            [&](const auto& view) { return view.id == drag_view_id_; });
        if (found == sheet_->views.end() || zoom <= 0.0) return;
        const double next_x = drag_origin_.x + (event->position().x() - drag_start_.x()) / zoom;
        const double next_y = drag_origin_.y + (event->position().y() - drag_start_.y()) / zoom;
        const double dx = next_x - found->x; const double dy = next_y - found->y;
        found->x = next_x; found->y = next_y;
        std::function<void(const std::string&)> move_children = [&](const std::string& parent) {
            for (auto& view : sheet_->views) if (view.parent_view_id == parent) {
                view.x += dx; view.y += dy; move_children(view.id);
            }
        };
        move_children(found->id);
        update();
    }
    void mouseReleaseEvent(QMouseEvent*) override { drag_view_id_.clear(); }
private:
    zima::drawing::DrawingSheet* sheet_{};
    std::string selected_;
    struct PickedEdge { std::string view_id; zima::drawing::ProjectedEdge edge; };
    std::optional<PickedEdge> first_edge_;
    bool dimension_mode_{};
    std::string drag_view_id_;
    QPointF drag_start_;
    zima::drawing::Point2 drag_origin_;
};

DrawingWindow::DrawingWindow() {
    setWindowTitle(tr("ZIMA-CAD C++ – Výkres")); resize(1180, 760);
    create_actions(); create_layout(); new_document();
}

void DrawingWindow::create_actions() {
    auto* file = menuBar()->addMenu(tr("Soubor"));
    file->addAction(tr("Nový výkres"), this, [this] { new_document(); });
    file->addAction(tr("Otevřít výkres…"), this, [this] { open_document(); });
    file->addAction(tr("Uložit výkres…"), this, [this] { save_document(); });
    auto* drawing = menuBar()->addMenu(tr("Výkres"));
    drawing->addAction(tr("Přidat list"), this, [this] { add_sheet(); });
    drawing->addAction(tr("Odstranit list"), this, [this] { remove_sheet(); });
    drawing->addAction(tr("Vlastnosti listu…"), this, [this] { edit_sheet(); });
    drawing->addSeparator();
    drawing->addAction(tr("Vložit pohled…"), this, [this] { insert_view(); });
    drawing->addAction(tr("Vytvořit promítnutý pohled…"), this,
                       [this] { create_projected_view(); });
    drawing->addAction(tr("Vlastnosti pohledu…"), this, [this] { edit_selected_view(); });
    drawing->addAction(tr("Regenerovat pohled"), this, [this] { regenerate_selected_view(); });
    drawing->addAction(tr("Odstranit pohled"), this, [this] { delete_selected_view(); });
    drawing->addAction(tr("Lineární kóta"), this, [this] { start_linear_dimension(); });
}

void DrawingWindow::create_layout() {
    auto* central = new QWidget(this); auto* layout = new QVBoxLayout(central);
    sheets_ = new QTabBar(central); sheets_->setExpanding(false);
    canvas_ = new DrawingCanvas(central); state_ = new QLabel(central);
    layout->addWidget(sheets_); layout->addWidget(canvas_, 1); layout->addWidget(state_);
    setCentralWidget(central);
    connect(sheets_, &QTabBar::currentChanged, this, [this] { refresh(); });
}

void DrawingWindow::new_document() { document_ = zima::drawing::DrawingDocument::create_default(); path_.clear(); refresh(); }
void DrawingWindow::open_document() {
    const auto path = QFileDialog::getOpenFileName(this, tr("Otevřít výkres"), {}, tr("Výkres ZIMA-CAD (*.drwz)"));
    if (path.isEmpty()) return;
    try { document_ = zima::drawing::DrawingDocument::load(path.toStdString()); path_ = path.toStdString(); refresh(); }
    catch (const std::exception& error) { QMessageBox::warning(this, tr("Nelze otevřít výkres"), error.what()); }
}
void DrawingWindow::save_document() {
    auto path = path_.empty() ? QFileDialog::getSaveFileName(this, tr("Uložit výkres"), {}, tr("Výkres ZIMA-CAD (*.drwz)")) : QString::fromStdString(path_.string());
    if (path.isEmpty()) return; if (!path.endsWith(".drwz")) path += ".drwz";
    try { document_.save(path.toStdString()); path_ = path.toStdString(); state_->setText(tr("Výkres uložen.")); }
    catch (const std::exception& error) { QMessageBox::warning(this, tr("Nelze uložit výkres"), error.what()); }
}
void DrawingWindow::add_sheet() {
    zima::drawing::DrawingSheet sheet; sheet.id = zima::drawing::DrawingDocument::create_default().sheets.front().id;
    sheet.name = tr("List %1").arg(document_.sheets.size() + 1).toStdString(); document_.sheets.push_back(std::move(sheet)); refresh(); sheets_->setCurrentIndex(static_cast<int>(document_.sheets.size() - 1));
}
void DrawingWindow::remove_sheet() { if (document_.sheets.size() <= 1) return; document_.sheets.erase(document_.sheets.begin() + sheets_->currentIndex()); refresh(); }
void DrawingWindow::edit_sheet() {
    auto* sheet = active_sheet(); if (sheet == nullptr) return;
    auto* dialog = new SheetPropertiesDialog(this, *sheet, [this, id = sheet->id](auto accepted) {
        auto* target = document_.find_sheet(id); if (target == nullptr) return;
        accepted.id = id; *target = std::move(accepted); refresh();
    });
    dialog->show();
}
zima::drawing::DrawingSheet* DrawingWindow::active_sheet() { const auto index = sheets_->currentIndex(); return index < 0 || index >= static_cast<int>(document_.sheets.size()) ? nullptr : &document_.sheets[index]; }

void DrawingWindow::insert_view() {
    const auto path = QFileDialog::getOpenFileName(this, tr("Vybrat zdroj pohledu"), {}, tr("Model ZIMA-CAD (*.prtz *.asmz)"));
    if (path.isEmpty() || active_sheet() == nullptr) return;
    try {
        auto [source_id, mesh] = load_drawing_source(path.toStdString());
        auto view = zima::drawing::DrawingDocument::create_view(source_id, path.toStdString(), mesh);
        auto* dialog = new ViewPropertiesDialog(this, view, [this, mesh = std::move(mesh)](auto accepted) mutable {
            accepted.projected_edges = zima::drawing::project_edges(mesh, accepted.orientation);
            accepted.projected_triangles = zima::drawing::project_triangles(mesh, accepted.orientation);
            active_sheet()->views.push_back(std::move(accepted)); refresh();
        });
        dialog->show();
    } catch (const std::exception& error) { QMessageBox::warning(this, tr("Nelze vložit pohled"), error.what()); }
}
void DrawingWindow::create_projected_view() {
    const auto* parent = document_.find_view(canvas_->selected_view_id());
    if (parent == nullptr || active_sheet() == nullptr) {
        state_->setText(tr("Nejprve vyberte rodičovský pohled.")); return;
    }
    try {
        auto [source_id, mesh] = load_drawing_source(parent->source_path);
        if (source_id != parent->source_document_id)
            throw std::runtime_error("Zdrojový soubor patří jinému dokumentu.");
        auto view = zima::drawing::DrawingDocument::create_view(
            source_id, parent->source_path, mesh, zima::drawing::ViewOrientation::Right);
        view.name = tr("Promítnutý pohled").toStdString();
        view.parent_view_id = parent->id; view.x = parent->x + 70.0; view.y = parent->y;
        view.scale = parent->scale; view.display_style = parent->display_style;
        auto* dialog = new ViewPropertiesDialog(this, view,
            [this, mesh = std::move(mesh)](auto accepted) mutable {
                accepted.projected_edges = zima::drawing::project_edges(mesh, accepted.orientation);
                accepted.projected_triangles = zima::drawing::project_triangles(mesh, accepted.orientation);
                active_sheet()->views.push_back(std::move(accepted)); refresh();
            });
        dialog->show();
    } catch (const std::exception& error) {
        QMessageBox::warning(this, tr("Nelze vytvořit promítnutý pohled"), error.what());
    }
}
void DrawingWindow::regenerate_selected_view() {
    auto* view = document_.find_view(canvas_->selected_view_id()); if (view == nullptr) return;
    try {
        auto [source_id, mesh] = load_drawing_source(view->source_path);
        if (source_id != view->source_document_id)
            throw std::runtime_error("Zdrojový soubor patří jinému dokumentu.");
        document_.refresh_view(view->id, mesh); refresh(); state_->setText(tr("Pohled regenerován."));
    } catch (const std::exception& error) {
        QMessageBox::warning(this, tr("Nelze regenerovat pohled"), error.what());
    }
}
void DrawingWindow::delete_selected_view() {
    auto* sheet = active_sheet(); const std::string selected = canvas_->selected_view_id();
    if (sheet == nullptr || selected.empty()) return;
    std::vector<std::string> removed{selected};
    for (std::size_t index = 0; index < removed.size(); ++index) {
        for (const auto& view : sheet->views)
            if (view.parent_view_id == removed[index] &&
                std::find(removed.begin(), removed.end(), view.id) == removed.end())
                removed.push_back(view.id);
    }
    std::erase_if(sheet->views, [&](const auto& view) {
        return std::find(removed.begin(), removed.end(), view.id) != removed.end();
    });
    std::erase_if(sheet->dimensions, [&](const auto& dimension) {
        return std::find(removed.begin(), removed.end(), dimension.view_id) != removed.end();
    });
    refresh();
}
void DrawingWindow::edit_selected_view() {
    auto* view = document_.find_view(canvas_->selected_view_id()); if (view == nullptr) return;
    auto* dialog = new ViewPropertiesDialog(this, *view,
        [this, id = view->id, old_orientation = view->orientation](auto accepted) {
        auto* target = document_.find_view(id); if (target == nullptr) return;
        accepted.id = id;
        if (accepted.orientation == old_orientation) {
            accepted.projected_edges = target->projected_edges;
            accepted.projected_triangles = target->projected_triangles;
        } else {
            try {
                auto [source_id, mesh] = load_drawing_source(accepted.source_path);
                if (source_id != accepted.source_document_id)
                    throw std::runtime_error("Zdrojový soubor patří jinému dokumentu.");
                accepted.projected_edges = zima::drawing::project_edges(mesh, accepted.orientation);
                accepted.projected_triangles = zima::drawing::project_triangles(mesh, accepted.orientation);
            } catch (const std::exception& error) {
                QMessageBox::warning(this, tr("Nelze změnit orientaci pohledu"), error.what());
                return;
            }
        }
        *target = std::move(accepted); refresh();
    });
    dialog->show();
}
void DrawingWindow::start_linear_dimension() {
    canvas_->start_linear_dimension();
    state_->setText(tr("Lineární kóta: vyberte dvě rovnoběžné hrany stejného pohledu."));
}
void DrawingWindow::refresh() {
    const int wanted = std::clamp(sheets_ ? sheets_->currentIndex() : 0, 0, static_cast<int>(document_.sheets.size() - 1));
    sheets_->blockSignals(true); while (sheets_->count()) sheets_->removeTab(0);
    for (const auto& sheet : document_.sheets) sheets_->addTab(QString::fromStdString(sheet.name));
    sheets_->setCurrentIndex(wanted); sheets_->blockSignals(false); canvas_->set_sheet(active_sheet());
    state_->setText(tr("Výkres: %1 listů, %2 pohledů").arg(document_.sheets.size()).arg(active_sheet() ? active_sheet()->views.size() : 0));
}

}  // namespace zima::app
