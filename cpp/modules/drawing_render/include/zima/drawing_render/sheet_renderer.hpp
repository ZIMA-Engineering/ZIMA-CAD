#pragma once
#include <zima/drawing/drawing_document.hpp>
#include <zima/drawing/measurement_dimension.hpp>
#include <QPainter>
#include <QPainterPath>
#include <QPolygonF>
#include <QImage>
#include <set>
#include <functional>
namespace zima::drawing_render {
QString drawing_font_family();
// One painter for the interactive canvas and exported sheets. All geometry is
// already calculated ZIMA data; no QWidget, event loop or kernel is used here.
class SheetRenderer {
public:
    virtual ~SheetRenderer() = default;
    void set_render_sheet(const drawing::DrawingSheet* sheet) {sheet_=sheet;shaded_cache_.clear();}
    void set_render_context(drawing::TitleBlockContext context) {title_block_context_=std::move(context);}
    void paint_sheet(QPainter&,double zoom,QPointF origin,bool printing);
protected:
    enum class AnnotationKind { Caption, SectionLabel, Dimension, SectionEnd, Model };
    struct AnnotationKey {
        AnnotationKind kind{};std::string view,id;int end{};
        bool operator==(const AnnotationKey&)const=default;
    };
    struct AnnotationHandle {
        AnnotationKey key;QPointF point;QPainterPath hit;
        zima::drawing::Point2 direction{};double offset{},minimum{};
    };
    std::set<std::string> model_offered_;
    std::function<void(const std::string&)> model_pick_;
    std::map<std::string,zima::drawing::TitleBlockTextTarget> title_targets_;
    std::map<std::string,drawing::DrawingView> staged_model_previews_;
    std::vector<AnnotationHandle> annotation_handles_;
    std::optional<AnnotationKey> selected_annotation_,hovered_annotation_;
    struct ShadedCache {double resolution{};QRectF bounds;const zima::drawing::ProjectedTriangle* triangles{};QImage image;};
    std::map<std::string,ShadedCache> shaded_cache_;
    bool lineweights_{};
    std::string selected_;
    std::string hovered_;
    std::string selected_field_,hovered_field_;
    std::vector<std::pair<std::string,QPolygonF>> field_regions_;
    std::optional<zima::drawing::DrawingView> preview_;
    std::optional<zima::drawing::TitleBlockContext> title_block_context_;
    const drawing::DrawingSheet* sheet_{};
    virtual const drawing::DrawingDimension* pending_dimension() const {return nullptr;}
    virtual void paint_reference_overlay(QPainter&) {}
    QColor annotation_color(const AnnotationKey&,QColor,bool) const;
    QRectF view_bounds_at(const drawing::DrawingView&,double,QPointF) const;
    QString label_text(const drawing::DrawingView&,bool,bool printing=false) const;
    QRectF label_bounds(const drawing::DrawingView&,bool,double,QPointF) const;
};
}
