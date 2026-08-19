#include "drawing_window.hpp"
#include "file_dialog.hpp"

#include <zima/assembly/assembly_document.hpp>
#include <zima/document/part_document.hpp>
#include <zima/ui/properties_subwindow.hpp>
#include <zima/workspace/workspace.hpp>

#include <QAction>
#include <QComboBox>
#include <QDoubleSpinBox>
#include <QFormLayout>
#include <QLabel>
#include <QKeyEvent>
#include <QLineEdit>
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
        orientation_->setEnabled(value_.parent_view_id.empty());
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

class ProjectionPropertiesDialog final : public zima::ui::PropertiesSubWindow {
public:
    ProjectionPropertiesDialog(QMainWindow* parent,
        std::function<void(zima::drawing::ProjectionDirection)> accepted)
        : PropertiesSubWindow(QObject::tr("Promítnutý pohled"), parent),
          accepted_(std::move(accepted)) {
        auto* content = new QWidget(this); auto* form = new QFormLayout(content);
        direction_ = new QComboBox(content);
        const std::pair<const char*, zima::drawing::ProjectionDirection> choices[] = {
            {"Vpravo", zima::drawing::ProjectionDirection::Right},
            {"Vpravo nahoře", zima::drawing::ProjectionDirection::TopRight},
            {"Nahoře", zima::drawing::ProjectionDirection::Top},
            {"Vlevo nahoře", zima::drawing::ProjectionDirection::TopLeft},
            {"Vlevo", zima::drawing::ProjectionDirection::Left},
            {"Vlevo dole", zima::drawing::ProjectionDirection::BottomLeft},
            {"Dole", zima::drawing::ProjectionDirection::Bottom},
            {"Vpravo dole", zima::drawing::ProjectionDirection::BottomRight},
        };
        for (const auto& [label, value] : choices)
            direction_->addItem(QObject::tr(label), static_cast<int>(value));
        form->addRow(QObject::tr("Směr umístění"), direction_);
        content_layout()->addWidget(content); setAttribute(Qt::WA_DeleteOnClose);
    }
private:
    std::function<void(zima::drawing::ProjectionDirection)> accepted_;
    QComboBox* direction_{};
    bool submit() override {
        accepted_(static_cast<zima::drawing::ProjectionDirection>(
            direction_->currentData().toInt()));
        return true;
    }
};

class TitleBlockPropertiesDialog final : public zima::ui::PropertiesSubWindow {
public:
    TitleBlockPropertiesDialog(QMainWindow* parent,
        std::vector<zima::drawing::TitleBlockField> fields,
        std::function<void(std::vector<zima::drawing::TitleBlockField>)> accepted)
        : PropertiesSubWindow(QObject::tr("Hodnoty razítka"),parent),
          fields_(std::move(fields)),accepted_(std::move(accepted)) {
        auto* content=new QWidget(this); auto* form=new QFormLayout(content);
        for(auto& field:fields_) if(field.editable) {
            auto* editor=new QLineEdit(QString::fromStdString(field.value),content);
            editors_.push_back({field.id,editor});
            form->addRow(QString::fromStdString(field.id),editor);
        }
        content_layout()->addWidget(content); setAttribute(Qt::WA_DeleteOnClose);
    }
private:
    std::vector<zima::drawing::TitleBlockField> fields_;
    std::vector<std::pair<std::string,QLineEdit*>> editors_;
    std::function<void(std::vector<zima::drawing::TitleBlockField>)> accepted_;
    bool submit() override {
        for(auto& field:fields_) for(const auto& [id,editor]:editors_)
            if(field.id==id) field.value=editor->text().toStdString();
        accepted_(std::move(fields_)); return true;
    }
};

class DrawingSourceDialog final : public zima::ui::PropertiesSubWindow {
public:
    DrawingSourceDialog(QMainWindow* parent,
        const std::vector<std::pair<std::string,std::string>>& sources,
        std::function<void(std::string)> accepted)
        : PropertiesSubWindow(QObject::tr("Zdroj výkresového pohledu"),parent),
          accepted_(std::move(accepted)) {
        auto* content=new QWidget(this); auto* form=new QFormLayout(content);
        source_=new QComboBox(content);
        for(const auto& [id,name]:sources)
            source_->addItem(QString::fromStdString(name),QString::fromStdString(id));
        source_->addItem(QObject::tr("Vybrat soubor…"),QString{});
        form->addRow(QObject::tr("Zdroj"),source_); content_layout()->addWidget(content);
        setAttribute(Qt::WA_DeleteOnClose);
    }
private:
    QComboBox* source_{}; std::function<void(std::string)> accepted_;
    bool submit() override { accepted_(source_->currentData().toString().toStdString()); return true; }
};

zima::drawing::Point2 projection_placement(
    zima::drawing::ProjectionDirection direction, double distance) {
    constexpr double diagonal = 0.7071067811865475244;
    switch (direction) {
        case zima::drawing::ProjectionDirection::Right: return {distance,0};
        case zima::drawing::ProjectionDirection::TopRight: return {distance*diagonal,-distance*diagonal};
        case zima::drawing::ProjectionDirection::Top: return {0,-distance};
        case zima::drawing::ProjectionDirection::TopLeft: return {-distance*diagonal,-distance*diagonal};
        case zima::drawing::ProjectionDirection::Left: return {-distance,0};
        case zima::drawing::ProjectionDirection::BottomLeft: return {-distance*diagonal,distance*diagonal};
        case zima::drawing::ProjectionDirection::Bottom: return {0,distance};
        case zima::drawing::ProjectionDirection::BottomRight: return {distance*diagonal,distance*diagonal};
        case zima::drawing::ProjectionDirection::None: return {};
    }
    return {};
}

std::pair<std::string, zima::kernel::ViewerMesh> load_drawing_source(
    const std::filesystem::path& path, zima::workspace::Workspace* workspace = nullptr,
    const std::string& expected_document_id = {}) {
    if (workspace != nullptr) {
        std::optional<std::string> open_id;
        if (!expected_document_id.empty() && workspace->find(expected_document_id) != nullptr)
            open_id = expected_document_id;
        else open_id = workspace->document_id_for_path(path);
        if (open_id && (workspace->open_part(*open_id) != nullptr ||
                        workspace->open_assembly(*open_id) != nullptr))
            return {*open_id, workspace->authoritative_viewer_mesh(*open_id)};
    }
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
        setMinimumSize(640, 480); setMouseTracking(true); setFocusPolicy(Qt::StrongFocus);
    }
    void set_sheet(zima::drawing::DrawingSheet* sheet) {
        sheet_ = sheet;
        selected_.clear();
        selected_dimension_id_.clear();
        dragged_dimension_id_.clear();
        drag_view_id_.clear();
        first_edge_.reset();
        dimension_mode_ = false;
        update();
    }
    [[nodiscard]] const std::string& selected_view_id() const { return selected_; }
    void set_changed_callback(std::function<void()> callback) { changed_=std::move(callback); }
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
        const auto screen=[&](const zima::drawing::Point2& point) {
            return QPointF(origin.x()+point.x*zoom,origin.y()+point.y*zoom);
        };
        const auto pen_color=[](zima::drawing::DrawingPen pen) {
            return pen == zima::drawing::DrawingPen::Yellow ? QColor("#b58b00")
                : pen == zima::drawing::DrawingPen::Green ? QColor("#277a3d") : QColor("#111111");
        };
        if (sheet_->frame_lines.empty()) {
            const double frame = 10.0 * zoom;
            painter.drawRect(paper.adjusted(frame, frame, -frame, -frame));
        }
        const auto draw_template=[&](const auto& lines,const auto& texts) {
            for(const auto& line:lines) { painter.setPen(QPen(pen_color(line.pen),1.0));
                painter.drawLine(screen(line.first),screen(line.second)); }
            for(const auto& text:texts) { painter.setPen(pen_color(text.pen)); QFont font=painter.font();
                font.setPixelSize(std::max(1,static_cast<int>(text.height*zoom))); painter.setFont(font);
                painter.drawText(screen(text.position),QString::fromStdString(text.text)); }
        };
        draw_template(sheet_->frame_lines,sheet_->frame_texts);
        draw_template(sheet_->title_block_lines,sheet_->title_block_texts);
        for(const auto& field:sheet_->title_block_fields) {
            painter.setPen(QColor("#111111")); QFont font=painter.font();
            font.setPixelSize(std::max(1,static_cast<int>(field.height*zoom))); painter.setFont(font);
            QString value=QString::fromStdString(field.value);
            if(field.expression=="&sheet.format") value=QString::fromStdString(
                sheet_->format==zima::drawing::SheetFormat::A4?"A4":sheet_->format==zima::drawing::SheetFormat::A3?"A3":
                sheet_->format==zima::drawing::SheetFormat::A2?"A2":sheet_->format==zima::drawing::SheetFormat::A1?"A1":"A0");
            else if(field.expression=="&sheet.scale") value=tr("M1:%1").arg(1.0/sheet_->default_scale,0,'g',4);
            painter.drawText(screen(field.position),value);
        }
        for(std::size_t index=0;index<sheet_->bom_rows.size();++index) {
            const auto& row=sheet_->bom_rows[index]; const double y=sheet_->height_mm()-75.0-index*6.0;
            painter.setPen(QColor("#111111"));
            painter.drawText(screen({sheet_->width_mm()-18.0,y}),QString::number(row.item_number));
            painter.drawText(screen({sheet_->width_mm()-35.0,y}),QString::number(row.quantity));
            painter.drawText(screen({sheet_->width_mm()-100.0,y}),QString::fromStdString(row.name));
        }
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
        for (const auto& dimension : sheet_->dimensions) {
            const QColor dimension_color=dimension.id==selected_dimension_id_ ? QColor("#00bcd4")
                : dimension.unresolved ? QColor("#c62828") : QColor("#d6a600");
            painter.setPen(QPen(dimension_color,dimension.id==selected_dimension_id_?2.0:1.0));
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
            const double mx=dimension.second_point.x-dimension.first_point.x;
            const double my=dimension.second_point.y-dimension.first_point.y;
            const double measured_length=std::hypot(mx,my);
            if(measured_length<=1e-9) continue;
            const double nx=mx/measured_length,ny=my/measured_length;
            const auto on_dimension_line=[&](const zima::drawing::Point2& witness) {
                const double projection=(witness.x-dimension.label_position.x)*nx+
                                        (witness.y-dimension.label_position.y)*ny;
                return zima::drawing::Point2{dimension.label_position.x+projection*nx,
                                            dimension.label_position.y+projection*ny};
            };
            const QPointF line_first=screen(on_dimension_line(dimension.first_point));
            const QPointF line_second=screen(on_dimension_line(dimension.second_point));
            painter.drawLine(first,line_first); painter.drawLine(second,line_second);
            painter.drawLine(line_first,line_second);
            const QPointF arrow_delta=line_second-line_first;
            const double arrow_length=std::hypot(arrow_delta.x(),arrow_delta.y());
            if(arrow_length>1e-9) {
                const QPointF direction=arrow_delta/arrow_length;
                const QPointF normal(-direction.y(),direction.x());
                painter.setBrush(dimension_color); painter.setPen(Qt::NoPen);
                painter.drawPolygon(QPolygonF{line_first,line_first+direction*8.0+normal*3.0,
                    line_first+direction*8.0-normal*3.0});
                painter.drawPolygon(QPolygonF{line_second,line_second-direction*8.0+normal*3.0,
                    line_second-direction*8.0-normal*3.0});
                painter.setPen(QPen(dimension_color,dimension.id==selected_dimension_id_?2.0:1.0));
            }
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
        setFocus();
        if(!dimension_mode_) for(auto& dimension:sheet_->dimensions) {
            const auto view=std::find_if(sheet_->views.begin(),sheet_->views.end(),[&](const auto& item) {
                return item.id==dimension.view_id; });
            if(view==sheet_->views.end()) continue;
            const auto screen_point=[&](const zima::drawing::Point2& point) {
                return QPointF(origin.x()+(view->x+point.x*view->scale)*zoom,
                    origin.y()+(view->y-point.y*view->scale)*zoom); };
            const QPointF label=screen_point(dimension.label_position);
            const double label_distance=std::hypot(label.x()-event->position().x(),
                                                    label.y()-event->position().y());
            const double mx=dimension.second_point.x-dimension.first_point.x;
            const double my=dimension.second_point.y-dimension.first_point.y;
            const double length=std::hypot(mx,my); if(length<=1e-9) continue;
            const double nx=mx/length,ny=my/length;
            const auto line_point=[&](const zima::drawing::Point2& witness) {
                const double along=(witness.x-dimension.label_position.x)*nx+
                    (witness.y-dimension.label_position.y)*ny;
                return zima::drawing::Point2{dimension.label_position.x+along*nx,
                    dimension.label_position.y+along*ny}; };
            const double line_distance=segment_distance(event->position(),
                screen_point(line_point(dimension.first_point)),
                screen_point(line_point(dimension.second_point)));
            if(label_distance<=14.0 || line_distance<=7.0) {
                selected_dimension_id_=dimension.id; selected_.clear(); drag_view_id_.clear();
                if(label_distance<=14.0) { dragged_dimension_id_=dimension.id;
                    dimension_drag_start_=event->position();
                    dimension_label_start_=dimension.label_position; }
                update(); return;
            }
        }
        selected_dimension_id_.clear();
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
            if (!hit_edge->source.valid()) { update(); return; }
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
                    if(changed_) changed_();
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
        if(sheet_==nullptr) return;
        const double margin = 24.0;
        const double zoom = std::min((width() - 2 * margin) / sheet_->width_mm(),
                                     (height() - 2 * margin) / sheet_->height_mm());
        if(!dragged_dimension_id_.empty() && (event->buttons()&Qt::LeftButton)) {
            const auto dimension=std::find_if(sheet_->dimensions.begin(),sheet_->dimensions.end(),
                [&](const auto& item){return item.id==dragged_dimension_id_;});
            if(dimension==sheet_->dimensions.end()) return;
            const auto view=std::find_if(sheet_->views.begin(),sheet_->views.end(),
                [&](const auto& item){return item.id==dimension->view_id;});
            if(view==sheet_->views.end() || zoom<=0.0 || view->scale<=0.0) return;
            dimension->label_position={dimension_label_start_.x+
                (event->position().x()-dimension_drag_start_.x())/(zoom*view->scale),
                dimension_label_start_.y-
                (event->position().y()-dimension_drag_start_.y())/(zoom*view->scale)};
            update(); return;
        }
        if(drag_view_id_.empty() || !(event->buttons()&Qt::LeftButton)) return;
        const auto found = std::find_if(sheet_->views.begin(), sheet_->views.end(),
            [&](const auto& view) { return view.id == drag_view_id_; });
        if (found == sheet_->views.end() || zoom <= 0.0) return;
        double next_x = drag_origin_.x + (event->position().x() - drag_start_.x()) / zoom;
        double next_y = drag_origin_.y + (event->position().y() - drag_start_.y()) / zoom;
        if (!found->parent_view_id.empty() &&
            found->projection_direction != zima::drawing::ProjectionDirection::None) {
            const auto parent = std::find_if(sheet_->views.begin(), sheet_->views.end(),
                [&](const auto& view) { return view.id == found->parent_view_id; });
            if (parent != sheet_->views.end()) {
                const auto ray = projection_placement(found->projection_direction, 1.0);
                const double distance = (next_x-parent->x)*ray.x + (next_y-parent->y)*ray.y;
                next_x = parent->x + distance*ray.x; next_y = parent->y + distance*ray.y;
            }
        }
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
    void mouseReleaseEvent(QMouseEvent*) override {
        if((!drag_view_id_.empty() || !dragged_dimension_id_.empty()) && changed_) changed_();
        drag_view_id_.clear();
        dragged_dimension_id_.clear();
    }
    void keyPressEvent(QKeyEvent* event) override {
        if(event->key()==Qt::Key_Escape) {
            if(dimension_mode_ && first_edge_) first_edge_.reset();
            else if(dimension_mode_) dimension_mode_=false;
            else { selected_dimension_id_.clear(); selected_.clear(); }
            update(); event->accept(); return;
        }
        if(event->key()==Qt::Key_Delete && sheet_!=nullptr &&
           !selected_dimension_id_.empty()) {
            std::erase_if(sheet_->dimensions,[&](const auto& dimension) {
                return dimension.id==selected_dimension_id_; });
            selected_dimension_id_.clear(); if(changed_) changed_(); update();
            event->accept(); return;
        }
        QWidget::keyPressEvent(event);
    }
private:
    zima::drawing::DrawingSheet* sheet_{};
    std::string selected_;
    struct PickedEdge { std::string view_id; zima::drawing::ProjectedEdge edge; };
    std::optional<PickedEdge> first_edge_;
    bool dimension_mode_{};
    std::string drag_view_id_;
    QPointF drag_start_;
    zima::drawing::Point2 drag_origin_;
    std::function<void()> changed_;
    std::string selected_dimension_id_;
    std::string dragged_dimension_id_;
    QPointF dimension_drag_start_;
    zima::drawing::Point2 dimension_label_start_;
};

DrawingWindow::DrawingWindow(
    zima::workspace::Workspace* workspace, bool create_initial_document)
    : workspace_(workspace) {
    setWindowTitle(tr("ZIMA-CAD – Výkres")); resize(1180, 760);
    create_actions(); create_layout();
    if(create_initial_document) new_document();
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
    drawing->addAction(tr("Načíst formát…"), this, [this] { load_frame(); });
    drawing->addAction(tr("Načíst razítko…"), this, [this] { load_title_block(); });
    drawing->addAction(tr("Hodnoty razítka…"), this, [this] { edit_title_block(); });
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
    canvas_->set_changed_callback([this] { sync_workspace_document(); });
    layout->addWidget(sheets_); layout->addWidget(canvas_, 1); layout->addWidget(state_);
    setCentralWidget(central);
    connect(sheets_, &QTabBar::currentChanged, this, [this] { refresh(); });
}

void DrawingWindow::new_document() {
    document_ = zima::drawing::DrawingDocument::create_default(); path_.clear();
    workspace_document_id_.clear();
    if(workspace_!=nullptr) {
        workspace_->add_drawing(document_); workspace_document_id_=document_.document_id;
        workspace_->activate(workspace_document_id_); workspace_->display_top_level(workspace_document_id_);
    }
    refresh();
}
void DrawingWindow::edit_workspace_document(const std::string& document_id) {
    if(workspace_==nullptr) return;
    auto* state=workspace_->open_drawing(document_id); if(state==nullptr) return;
    workspace_document_id_=document_id; document_=state->document; path_=state->path; refresh();
}
void DrawingWindow::open_document() {
    const auto path = open_file(this, tr("Otevřít výkres"), {}, tr("Výkres ZIMA-CAD (*.drwz)"));
    if (path.isEmpty()) return;
    try {
        document_ = zima::drawing::DrawingDocument::load(path.toStdString()); path_ = path.toStdString();
        workspace_document_id_.clear();
        if(workspace_!=nullptr) {
            if(auto* existing=workspace_->open_drawing(document_.document_id)) {
                existing->document=document_; existing->path=path_; }
            else workspace_->add_drawing(document_,path_);
            workspace_document_id_=document_.document_id;
            workspace_->activate(workspace_document_id_); workspace_->display_top_level(workspace_document_id_);
        }
        refresh();
    }
    catch (const std::exception& error) { QMessageBox::warning(this, tr("Nelze otevřít výkres"), error.what()); }
}
void DrawingWindow::save_document() {
    auto path = path_.empty() ? save_file(this, tr("Uložit výkres"), "drawing.drwz", tr("Výkres ZIMA-CAD (*.drwz)"), "drwz") : QString::fromStdString(path_.string());
    if (path.isEmpty()) return;
    if (!path.endsWith(".drwz", Qt::CaseInsensitive)) path += ".drwz";
    try { document_.save(path.toStdString()); path_ = path.toStdString();
        sync_workspace_document(); state_->setText(tr("Výkres uložen.")); }
    catch (const std::exception& error) { QMessageBox::warning(this, tr("Nelze uložit výkres"), error.what()); }
}
void DrawingWindow::add_sheet() {
    zima::drawing::DrawingSheet sheet; sheet.id = zima::drawing::DrawingDocument::create_default().sheets.front().id;
    sheet.name = tr("List %1").arg(document_.sheets.size() + 1).toStdString(); document_.sheets.push_back(std::move(sheet)); refresh(); sheets_->setCurrentIndex(static_cast<int>(document_.sheets.size() - 1));
}
void DrawingWindow::remove_sheet() { if (document_.sheets.size() <= 1) return; document_.sheets.erase(document_.sheets.begin() + sheets_->currentIndex()); refresh(); }
void DrawingWindow::edit_sheet() {
    auto* sheet = active_sheet(); if (sheet == nullptr) return;
    auto* dialog = new SheetPropertiesDialog(this, *sheet,
        [this, id = sheet->id, old_format = sheet->format](auto accepted) {
        auto* target = document_.find_sheet(id); if (target == nullptr) return;
        if(accepted.format!=old_format) {
            accepted.frame_lines.clear(); accepted.frame_texts.clear();
            accepted.title_block_lines.clear(); accepted.title_block_texts.clear();
            accepted.title_block_fields.clear();
        }
        accepted.id = id; *target = std::move(accepted); refresh();
    });
    dialog->show();
}
void DrawingWindow::load_frame() {
    auto* sheet=active_sheet(); if(sheet==nullptr) return;
    const auto path=open_file(this,tr("Načíst formát"),"config/formats",
                                                 tr("Formát výkresu (*.frmz)"));
    if(path.isEmpty()) return;
    try { zima::drawing::load_frame_template(*sheet,path.toStdString()); refresh(); }
    catch(const std::exception& error) { QMessageBox::warning(this,tr("Nelze načíst formát"),error.what()); }
}
void DrawingWindow::load_title_block() {
    auto* sheet=active_sheet(); if(sheet==nullptr) return;
    const auto path=open_file(this,tr("Načíst razítko"),"config/formats",
                                                 tr("Razítko výkresu (*.tblz)"));
    if(path.isEmpty()) return;
    try { zima::drawing::load_title_block_template(*sheet,path.toStdString()); refresh(); }
    catch(const std::exception& error) { QMessageBox::warning(this,tr("Nelze načíst razítko"),error.what()); }
}
void DrawingWindow::edit_title_block() {
    auto* sheet=active_sheet(); if(sheet==nullptr || sheet->title_block_fields.empty()) return;
    auto* dialog=new TitleBlockPropertiesDialog(this,sheet->title_block_fields,
        [this,id=sheet->id](auto fields) {
            auto* target=document_.find_sheet(id); if(target==nullptr) return;
            target->title_block_fields=std::move(fields); refresh();
        });
    dialog->show();
}
zima::drawing::DrawingSheet* DrawingWindow::active_sheet() { const auto index = sheets_->currentIndex(); return index < 0 || index >= static_cast<int>(document_.sheets.size()) ? nullptr : &document_.sheets[index]; }

void DrawingWindow::insert_view() {
    if(active_sheet()==nullptr) return;
    std::vector<std::pair<std::string,std::string>> sources;
    if(workspace_!=nullptr) for(const auto& state:workspace_->documents())
        std::visit([&](const auto& item) {
            using State=std::decay_t<decltype(item)>;
            if constexpr(std::is_same_v<State,zima::workspace::PartState> ||
                         std::is_same_v<State,zima::workspace::AssemblyState>)
                sources.push_back({item.session.document().document_id,item.session.document().name});
        },state);
    if(!sources.empty()) {
        auto* dialog=new DrawingSourceDialog(this,sources,[this](std::string id) {
            if(id.empty()) { insert_view_from_file(); return; }
            try {
                std::filesystem::path path;
                if(const auto* part=workspace_->open_part(id)) path=part->path;
                else if(const auto* assembly=workspace_->open_assembly(id)) path=assembly->path;
                begin_view_insertion(id,path,workspace_->authoritative_viewer_mesh(id));
            } catch(const std::exception& error) {
                QMessageBox::warning(this,tr("Nelze vložit pohled"),error.what()); }
        });
        dialog->show(); return;
    }
    insert_view_from_file();
}

void DrawingWindow::insert_view_from_file() {
    const auto path = open_file(this, tr("Vybrat zdroj pohledu"), {},
        tr("Model ZIMA-CAD (*.prtz *.asmz)"));
    if (path.isEmpty() || active_sheet() == nullptr) return;
    try {
        auto [source_id, mesh] = load_drawing_source(path.toStdString(),workspace_);
        begin_view_insertion(std::move(source_id),path.toStdString(),std::move(mesh));
    } catch (const std::exception& error) { QMessageBox::warning(this, tr("Nelze vložit pohled"), error.what()); }
}

void DrawingWindow::begin_view_insertion(
    std::string source_id,std::filesystem::path source_path,zima::kernel::ViewerMesh mesh) {
    std::vector<zima::drawing::BomRow> bom;
    const zima::assembly::AssemblyDocument* assembly{};
    std::optional<zima::assembly::AssemblyDocument> loaded;
    if(workspace_!=nullptr) if(const auto* open=workspace_->open_assembly(source_id))
        assembly=&open->session.document();
    if(assembly==nullptr && (!source_path.empty()) &&
       source_path.extension()==".asmz") {
        loaded=zima::assembly::AssemblyDocument::load(source_path); assembly=&*loaded;
    }
    if(assembly!=nullptr) for(const auto& component:assembly->components) {
        const auto existing=std::find_if(bom.begin(),bom.end(),[&](const auto& row) {
            return row.designation==component.source_document_id; });
        if(existing==bom.end()) bom.push_back({static_cast<int>(bom.size()+1),1,
            component.name,component.source_document_id,{}});
        else ++existing->quantity;
    }
    auto view=zima::drawing::DrawingDocument::create_view(source_id,source_path,mesh);
        auto* dialog = new ViewPropertiesDialog(this, view,
            [this, mesh = std::move(mesh), bom = std::move(bom)](auto accepted) mutable {
            accepted.camera = zima::drawing::standard_camera(accepted.orientation);
            accepted.projected_edges = zima::drawing::project_edges(mesh, accepted.camera);
            accepted.projected_triangles = zima::drawing::project_triangles(mesh, accepted.camera);
            if(!bom.empty() && active_sheet()->bom_rows.empty()) active_sheet()->bom_rows=std::move(bom);
            active_sheet()->views.push_back(std::move(accepted)); refresh();
        });
    dialog->show();
}
void DrawingWindow::create_projected_view() {
    const auto* parent = document_.find_view(canvas_->selected_view_id());
    if (parent == nullptr || active_sheet() == nullptr) {
        state_->setText(tr("Nejprve vyberte rodičovský pohled.")); return;
    }
    const auto parent_copy = *parent;
    auto* dialog = new ProjectionPropertiesDialog(this,
        [this, parent_copy](zima::drawing::ProjectionDirection direction) {
            try {
                auto [source_id, mesh] = load_drawing_source(
                    parent_copy.source_path,workspace_,parent_copy.source_document_id);
                if (source_id != parent_copy.source_document_id)
                    throw std::runtime_error("Zdrojový soubor patří jinému dokumentu.");
                auto view = zima::drawing::DrawingDocument::create_view(
                    source_id, parent_copy.source_path, mesh,
                    zima::drawing::ViewOrientation::Right);
                view.name = tr("Promítnutý pohled").toStdString();
                view.parent_view_id = parent_copy.id;
                view.projection_direction = direction;
                view.camera = zima::drawing::projected_camera(
                    parent_copy.camera, direction, active_sheet()->projection_method);
                const auto placement = projection_placement(direction, 70.0);
                view.x = parent_copy.x + placement.x; view.y = parent_copy.y + placement.y;
                view.scale = parent_copy.scale; view.display_style = parent_copy.display_style;
                view.projected_edges = zima::drawing::project_edges(mesh, view.camera);
                view.projected_triangles = zima::drawing::project_triangles(mesh, view.camera);
                active_sheet()->views.push_back(std::move(view)); refresh();
            } catch (const std::exception& error) {
                QMessageBox::warning(this, tr("Nelze vytvořit promítnutý pohled"), error.what());
            }
        });
    dialog->show();
}
void DrawingWindow::regenerate_selected_view() {
    auto* view = document_.find_view(canvas_->selected_view_id()); if (view == nullptr) return;
    try {
        auto [source_id, mesh] = load_drawing_source(
            view->source_path,workspace_,view->source_document_id);
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
                auto [source_id, mesh] = load_drawing_source(
                    accepted.source_path,workspace_,accepted.source_document_id);
                if (source_id != accepted.source_document_id)
                    throw std::runtime_error("Zdrojový soubor patří jinému dokumentu.");
                accepted.camera = zima::drawing::standard_camera(accepted.orientation);
                accepted.projected_edges = zima::drawing::project_edges(mesh, accepted.camera);
                accepted.projected_triangles = zima::drawing::project_triangles(mesh, accepted.camera);
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
    sync_workspace_document();
}

void DrawingWindow::sync_workspace_document() {
    if(workspace_!=nullptr) for(auto& sheet:document_.sheets) for(auto& view:sheet.views)
        if(view.source_path.empty()) {
            if(const auto* part=workspace_->open_part(view.source_document_id)) view.source_path=part->path;
            else if(const auto* assembly=workspace_->open_assembly(view.source_document_id))
                view.source_path=assembly->path;
        }
    if(workspace_!=nullptr && !workspace_document_id_.empty())
        if(auto* state=workspace_->open_drawing(workspace_document_id_)) {
            state->document=document_; state->path=path_;
        }
}

}  // namespace zima::app
