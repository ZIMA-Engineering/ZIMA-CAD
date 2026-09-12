#include <zima/drawing_render/sheet_renderer.hpp>
#include <zima/viewer/dimension_text_layer.hpp>
#include <zima/viewer/embedded_image.hpp>
#include <zima/viewer/annotation_arrow.hpp>
#include "drawing_annotation_layout.hpp"
#include "drawing_shading.hpp"
#include <QCoreApplication>
#include <QDir>
#include <QFileInfo>
#include <QFontDatabase>
#include <QRegularExpression>
#include <QPainterPathStroker>
namespace zima::drawing_render {
using app::model_annotation_key;
using app::model_annotation_layout;
using app::drawing_shaded_fill;
QString drawing_font_family() {
    static const QString family = [] {
        const QString relative = QStringLiteral("config/fonts/osifont-lgpl3fe.ttf");
        QString path = relative;
        if (!QFileInfo::exists(path)) {
            path = QDir(QCoreApplication::applicationDirPath())
                .absoluteFilePath(QStringLiteral("../../config/fonts/osifont-lgpl3fe.ttf"));
        }
        const int id = QFontDatabase::addApplicationFont(path);
        const auto families = id >= 0
            ? QFontDatabase::applicationFontFamilies(id) : QStringList{};
        return families.empty() ? QStringLiteral("sans-serif") : families.front();
    }();
    return family;
}

QColor SheetRenderer::annotation_color(const AnnotationKey& key,QColor normal,bool printing)const{
        if(printing)return normal;
        const auto same=[&](const auto& candidate){return candidate&&candidate->kind==key.kind&&candidate->view==key.view&&candidate->id==key.id;};
        if(same(selected_annotation_))return QColor("#00D1FF");if(same(hovered_annotation_))return QColor("#FF9300");return normal;
    }
QRectF SheetRenderer::view_bounds_at(const zima::drawing::DrawingView& view,double zoom,QPointF origin) const {
        bool first = true; double xmin{}, xmax{}, ymin{}, ymax{};
        const auto include = [&](const zima::drawing::Point2& point) {
            const auto screen = QPointF(origin.x()+(sheet_->width_mm()-view.x+point.x*view.scale)*zoom,origin.y()+(sheet_->height_mm()-view.y-point.y*view.scale)*zoom);
            if (first) { xmin=xmax=screen.x(); ymin=ymax=screen.y(); first=false; }
            else { xmin=std::min(xmin,screen.x()); xmax=std::max(xmax,screen.x());
                   ymin=std::min(ymin,screen.y()); ymax=std::max(ymax,screen.y()); }
        };
        for (const auto& edge : view.projected_edges) for (const auto& point : edge.points) include(point);
        for (const auto& triangle : view.projected_triangles) for (const auto& point : triangle.points) include(point);
        if (first) include({});
        return QRectF(QPointF(xmin,ymin),QPointF(xmax,ymax));
    }
QString SheetRenderer::label_text(const zima::drawing::DrawingView& view,bool section,bool printing)const{
        if(section?(!view.show_section_label||view.section_id.empty()||!view.section_snapshot):!view.show_caption)return {};
        const auto value=QString::fromStdString(section?view.section_snapshot->name:view.name);
        return !printing&&value.trimmed().isEmpty()?QStringLiteral("-"):value;
    }
QRectF SheetRenderer::label_bounds(const zima::drawing::DrawingView& view,bool section,double zoom,QPointF origin)const{
        const auto text=label_text(view,section);if(text.isEmpty())return {};
        QFont font(drawing_font_family());font.setPixelSize(1000);const QFontMetricsF metrics(font);
        const auto bounds=view_bounds_at(view,zoom,origin);const auto& position=section?view.section_label_position:view.caption_position;
        const bool both=view.show_caption&&view.show_section_label&&!view.section_id.empty()&&view.section_snapshot;
        const QPointF center=position?QPointF(origin.x()+(sheet_->width_mm()-view.x+position->x)*zoom,origin.y()+(sheet_->height_mm()-view.y-position->y)*zoom)
            :QPointF(bounds.center().x(),bounds.top()-((section?6.5:5.75)+(!section&&both?8:0))*zoom);
        const double height=section?5.0:3.5;
        const double width=std::max(1.0,metrics.horizontalAdvance(text))*height*zoom/metrics.capHeight();
        return QRectF(center.x()-width/2-zoom,center.y()-(height+2)*zoom/2,width+2*zoom,(height+2)*zoom);
    }
void SheetRenderer::paint_sheet(QPainter& painter,double zoom,QPointF origin,bool printing) {
        if(!sheet_)return;
        const auto* dimension_preview=pending_dimension();
        if(!printing)annotation_handles_.clear();
        const auto width=[&](bool thick){return printing||lineweights_?zoom*(thick?sheet_->thick_line_mm:sheet_->thin_line_mm):1.0;};
        const auto ink=printing?QColor(Qt::black):QColor(Qt::white);
        std::vector<viewer::DimensionTextLabel> dimension_texts;
        std::vector<const zima::drawing::DrawingView*> views;
        for(const auto& view:sheet_->views)if(printing||!preview_||preview_->id!=view.id) {
            const auto staged=staged_model_previews_.find(view.id);
            views.push_back(!printing&&staged!=staged_model_previews_.end()?&staged->second:&view);
        }
        if(!printing&&preview_)views.push_back(&*preview_);
        const QRectF paper(origin.x(), origin.y(), sheet_->width_mm() * zoom,
                           sheet_->height_mm() * zoom);
        painter.setRenderHint(QPainter::Antialiasing, true);
        QFont annotation_font(drawing_font_family());annotation_font.setPixelSize(std::max(1,static_cast<int>(3.5*zoom)));painter.setFont(annotation_font);
        painter.setPen(QPen(QColor("#808080"), 1.0));
        if(!printing)painter.drawRect(paper);
        painter.setPen(QPen(ink,width(false)));
        const auto screen=[&](const zima::drawing::Point2& point) {
            return QPointF(origin.x()+sheet_->width_mm()*zoom-point.x*zoom,
                           origin.y()+sheet_->height_mm()*zoom-point.y*zoom);
        };
        const auto pen_color=[&](zima::drawing::DrawingPen pen) {
            if(printing)return QColor(Qt::black);
            return pen==zima::drawing::DrawingPen::Red?QColor("#FF0000"):pen == zima::drawing::DrawingPen::Yellow ? QColor("#E6C85C")
                : pen == zima::drawing::DrawingPen::Green ? QColor("#4DD811") : QColor("#FFFFFF");
        };
        if (sheet_->frame_lines.empty()) {
            const double frame = 10.0 * zoom;
            painter.drawRect(paper.adjusted(frame, frame, -frame, -frame));
        }
        if(!printing)field_regions_.clear();
        const auto draw_handle=[&](QPointF point,bool selected) {
            painter.save();painter.setPen(Qt::NoPen);
            painter.setBrush(selected?QColor("#D05CFF"):QColor("#FF9300"));
            // Same 4.5 logical-pixel radius as ordinary Sketcher point markers.
            painter.drawEllipse(point,4.5,4.5);painter.restore();
        };
        const auto draw_text=[&](const zima::drawing::TemplateText& text) {
            auto value=QString::fromStdString(text.text);
            if(value.trimmed().isEmpty()){if(printing)return;value=QStringLiteral("-");}
            const bool selected=!printing&&!text.field_id.empty()&&selected_field_==text.field_id;
            const bool hovered=!printing&&!text.field_id.empty()&&hovered_field_==text.field_id;
            painter.save();painter.setPen(selected?QColor("#00D1FF"):hovered?QColor("#FF9300"):pen_color(text.pen));
            QFont font(QString::fromStdString(text.font));font.setPixelSize(1000);painter.setFont(font);
            const QFontMetricsF metrics(font);
            const auto ink=metrics.tightBoundingRect(value);const auto anchor=screen(text.position);
            const double scale=text.height/std::max(1.0,metrics.capHeight());
            const double angle=text.angle*3.141592653589793/180.0,flip=text.flipped?-1:1;
            const QPointF x=screen({text.position.x+flip*std::cos(angle)*scale,text.position.y+flip*std::sin(angle)*scale})-anchor;
            const QPointF y=screen({text.position.x+std::sin(angle)*scale,text.position.y-std::cos(angle)*scale})-anchor;
            const QTransform transform(x.x(),x.y(),y.x(),y.y(),anchor.x(),anchor.y());
            painter.setTransform(transform,true);
            auto alignment=QString::fromStdString(text.alignment).toLower();
            const double dx=alignment=="center"?-ink.center().x():alignment=="right"?-ink.right():-ink.left();
            const double dy=text.vertical_alignment=="top"?-ink.top():text.vertical_alignment=="middle"||text.vertical_alignment=="center"?-ink.center().y():text.vertical_alignment=="baseline"?0:-ink.bottom();
            painter.drawText(QPointF(dx,dy),value);painter.restore();
            if(!printing&&!text.field_id.empty()) {
                const auto polygon=transform.map(QPolygonF(ink.translated(dx,dy).adjusted(-60,-60,60,60)));
                field_regions_.push_back({text.field_id,polygon});
                // Fixed title-block fields remain ordinary text selection.
            }
        };
        const auto pen_width=[&](zima::drawing::DrawingPen pen){return printing||lineweights_?zoom*zima::drawing::drawing_pen_width_mm(*sheet_,pen):1.0;};
        const auto draw_template=[&](const auto& lines,const auto& texts,const auto& circles) {
            for(const auto& line:lines){painter.setPen(QPen(pen_color(line.pen),pen_width(line.pen)));painter.drawLine(screen(line.first),screen(line.second));}
            for(const auto& circle:circles){painter.setPen(QPen(pen_color(circle.pen),pen_width(circle.pen)));painter.setBrush(Qt::NoBrush);painter.drawEllipse(screen(circle.center),circle.radius*zoom,circle.radius*zoom);}
            for(const auto& text:texts)draw_text(text);
        };
        draw_template(sheet_->frame_lines,sheet_->frame_texts,sheet_->frame_circles);
        const auto layout=zima::drawing::title_block_layout(*sheet_,title_block_context_.value_or(zima::drawing::TitleBlockContext{}));
        if(!printing)title_targets_=layout.edit_targets;
        for(const auto& image:layout.images) {
            QPolygonF target;for(const auto& point:image.corners())target<<screen({point[0],point[1]});
            zima::viewer::paint_embedded_image(painter,image.data_base64,image.format,target);
        }
        draw_template(layout.lines,layout.texts,layout.circles);
        for (const auto* rendered_view : views) {
            const auto& view = *rendered_view;
            if (view.display_style != zima::drawing::DisplayStyle::ShadedWithEdges && view.display_style != zima::drawing::DisplayStyle::Shaded) continue;
            bool first=true;QRectF model_bounds;
            for(const auto& triangle:view.projected_triangles)for(const auto& point:triangle.points) {
                const QPointF p(point.x,point.y);
                if(first){model_bounds=QRectF(p,p);first=false;}
                else model_bounds=QRectF(QPointF(std::min(model_bounds.left(),p.x()),std::min(model_bounds.top(),p.y())),
                    QPointF(std::max(model_bounds.right(),p.x()),std::max(model_bounds.bottom(),p.y())));
            }
            const QRectF target(origin.x()+(sheet_->width_mm()-view.x+model_bounds.left()*view.scale)*zoom,
                origin.y()+(sheet_->height_mm()-view.y-model_bounds.bottom()*view.scale)*zoom,
                model_bounds.width()*view.scale*zoom,model_bounds.height()*view.scale*zoom);
            const double resolution=zoom*view.scale*(printing?1.0:2.0);
            auto& cached=shaded_cache_[view.id];
            if(cached.image.isNull()||cached.resolution!=resolution||cached.bounds!=model_bounds||cached.triangles!=view.projected_triangles.data()) {
                cached.image=drawing_shaded_fill(view,model_bounds,resolution);
                cached.resolution=resolution;cached.bounds=model_bounds;cached.triangles=view.projected_triangles.data();
            }
            painter.setRenderHint(QPainter::SmoothPixmapTransform,true);
            painter.drawImage(target,cached.image);

        }
        painter.setBrush(Qt::NoBrush);
        for (const auto* rendered_view : views) {
            const auto& view = *rendered_view;
            for(bool hidden_pass:{true,false})for (const auto& edge : view.projected_edges) {
                if(edge.hidden!=hidden_pass)continue;
                if(!zima::drawing::drawing_edge_visible(view,edge))continue;
                const bool gray=edge.hidden&&view.hidden_edge_style==zima::drawing::HiddenEdgeStyle::Gray;
                const QColor edge_color=!printing&&!model_pick_&&!selected_annotation_&&view.id==selected_?QColor("#00D1FF"):
                    edge.hatch&&!printing?QColor("#55BB77"):(edge.hidden||edge.tangent)&&!printing?QColor("#666666"):gray?QColor("#808080"):ink;
                QPen pen(edge_color,width(!edge.hatch&&!edge.hidden&&!(edge.tangent&&view.tangent_edge_style==zima::drawing::TangentEdgeStyle::Thin)));
                pen.setCapStyle(Qt::FlatCap);pen.setJoinStyle(Qt::RoundJoin);
                if((edge.hidden&&!gray)||(edge.hatch&&edge.hatch_pattern==2)){pen.setDashPattern({3.0*zoom/pen.widthF(),1.5*zoom/pen.widthF()});}
                painter.setPen(pen);
                if (edge.points.size() < 2) continue;
                QPolygonF line;
                for (const auto& point : edge.points) {
                    line << QPointF(origin.x() + sheet_->width_mm()*zoom -
                                        (view.x - point.x * view.scale) * zoom,
                                    origin.y() + sheet_->height_mm()*zoom -
                                        (view.y + point.y * view.scale) * zoom);
                }
                painter.drawPolyline(line);
            }
        }
        painter.setBrush(Qt::NoBrush);
        for (const auto* view : views) {
            const auto bounds = printing?view_bounds_at(*view,zoom,origin):view_bounds_at(*view,zoom,origin).adjusted(-8,-8,8,8);
            if (!printing&&((!selected_annotation_&&view->id==selected_) || view->id==hovered_ || (preview_ && preview_->id==view->id))) {
                painter.setPen(QPen(view->id==hovered_ && view->id!=selected_ ? QColor("#FF9300")
                    : QColor("#00D1FF"), 1, Qt::DashLine));
                painter.drawRect(bounds);
            }
            for(bool section:{false,true}){
                const auto text=label_text(*view,section,printing);if(text.isEmpty())continue;
                const auto rect=label_bounds(*view,section,zoom,origin);
                const AnnotationKey key{section?AnnotationKind::SectionLabel:AnnotationKind::Caption,view->id,{},0};
                if(!printing){QPainterPath hit;hit.addRect(rect);annotation_handles_.push_back({key,rect.center(),hit});}
                painter.save();painter.setPen(annotation_color(key,printing||section?ink:QColor("#4DD811"),printing));
                QFont font(drawing_font_family());font.setPixelSize(1000);painter.setFont(font);const QFontMetricsF metrics(font);
                painter.translate(rect.left()+zoom,rect.bottom()-zoom);const double scale=(section?5.0:3.5)*zoom/metrics.capHeight();painter.scale(scale,scale);painter.drawText(QPointF(0,0),text);painter.restore();
            }
        }
        // Model annotation strokes are shared by drawing, hit testing and PDF.
        for(const auto* view:views){
            const auto screen=[&](QPointF p){return QPointF(origin.x()+(sheet_->width_mm()-view->x+p.x())*zoom,origin.y()+(sheet_->height_mm()-view->y-p.y())*zoom);};
            const auto b=view_bounds_at(*view,zoom,origin);const auto o=screen({});
            const QRectF paper_bounds((b.left()-o.x())/zoom,(o.y()-b.bottom())/zoom,b.width()/zoom,b.height()/zoom);
            if(!printing&&view->show_dimension_guides){
                painter.save();painter.setPen(QPen(QColor("#666666"),1,Qt::DashLine));
                std::set<std::pair<std::string,std::string>> drawn;
                for(const auto& item:view->model_annotations)if(item.visible&&item.model_envelope.valid&&drawn.emplace(item.source.owner_id,item.source.instance_path).second){
                    for(int level=-1;level<4;++level){auto frame=item.model_envelope;const double offset=level<0?0:(view->dimension_guide_offset+level*view->dimension_guide_spacing)/view->scale;
                        frame.minimum=kernel::dimension_sub(frame.minimum,{offset,offset,offset});frame.maximum=kernel::dimension_add(frame.maximum,{offset,offset,offset});
                        const auto corners=frame.corners();const auto projected=[&](auto p){return screen({kernel::dimension_dot(p,view->camera.horizontal)*view->scale,kernel::dimension_dot(p,view->camera.vertical)*view->scale});};
                        for(unsigned i=0;i<8;++i)for(unsigned bit:{1u,2u,4u})if(!(i&bit))painter.drawLine(projected(corners[i]),projected(corners[i|bit]));
                    }
                }painter.restore();
            }
            for(const auto& stored:view->model_annotations){
                const auto item=drawing::project_model_annotation(*view,stored);
                const auto id=model_annotation_key(item.source);const bool offered=!printing&&model_pick_&&preview_&&view->id==preview_->id&&model_offered_.contains(id);
                if(!item.visible&&!offered)continue;
                const auto layout=model_annotation_layout(*view,item,paper_bounds.adjusted(-5,-5,5,5),QFontMetricsF(painter.font()).horizontalAdvance(QString::fromStdString(item.text))/zoom);
                if(item.model_dimension && layout.curves.empty())continue;
                const AnnotationKey key{AnnotationKind::Model,view->id,id,0};
                QColor color=annotation_color(key,printing?ink:item.unresolved?QColor("#E05050"):!item.visible?QColor("#777777"):item.kind==drawing::ModelAnnotationKind::Dimension?QColor("#FFD400"):QColor("#E6C85C"),printing);
                painter.save();QPen pen(color,width(false));if(item.kind!=drawing::ModelAnnotationKind::Dimension)pen.setDashPattern({8*zoom/pen.widthF(),1.5*zoom/pen.widthF(),.5*zoom/pen.widthF(),1.5*zoom/pen.widthF()});painter.setPen(pen);painter.setBrush(Qt::NoBrush);QPainterPath stroke;
                for(const auto& line:layout.curves){if(line.empty())continue;QPolygonF polygon;for(auto p:line)polygon<<screen(p);painter.drawPolyline(polygon);stroke.moveTo(polygon.front());for(qsizetype i=1;i<polygon.size();++i)stroke.lineTo(polygon[i]);}
                for(const auto& center:layout.centers){painter.save();painter.setPen(Qt::NoPen);painter.setBrush(color);painter.drawEllipse(screen(center),.35*zoom,.35*zoom);painter.restore();}
                for(const auto& [tip,direction]:layout.arrows){painter.save();painter.setPen(Qt::NoPen);painter.setBrush(color);painter.drawPolygon(viewer::annotation_arrow(screen(tip),{direction.x(),-direction.y()},2.5*zoom));painter.restore();}
                const auto text=QString::fromStdString(item.text);const auto text_point=screen(layout.text);
                QTransform text_transform;text_transform.translate(text_point.x(),text_point.y());text_transform.rotate(layout.text_angle);
                if(item.kind==drawing::ModelAnnotationKind::Dimension)
                    dimension_texts.push_back({text,text_point,layout.text_angle,painter.font(),color});
                else {painter.save();painter.translate(text_point);painter.rotate(layout.text_angle);painter.drawText(QPointF{},text);painter.restore();}
                if(!printing){QPainterPathStroker picker;picker.setWidth(10);auto hit=picker.createStroke(stroke);if(!text.isEmpty())hit.addRect(text_transform.mapRect(QFontMetricsF(painter.font()).boundingRect(text)));
                    if(layout.handles.empty()){if(!layout.curves.empty()&&!layout.curves[0].empty())annotation_handles_.push_back({key,screen(layout.curves[0].front()),hit});}
                    else for(const auto& [name,p]:layout.handles){auto handle=key;handle.end=name=="text"?0:name=="arrow_first"?1:2;annotation_handles_.push_back({handle,screen(p),hit});}
                }painter.restore();
            }
        }
        // Traces are independent of model topology. One paper-space layout
        // supplies View/PDF strokes and collision-free upright end letters.
        for(const auto* view:views){
            using Point=zima::drawing::Point2;
            std::vector<std::pair<const zima::document::SectionDefinition*,zima::drawing::SectionTraceLayout>> traces;
            std::vector<std::array<Point,2>> obstacles;
            for(const auto& edge:view->projected_edges)if(zima::drawing::drawing_edge_visible(*view,edge))for(std::size_t i=1;i<edge.points.size();++i)
                obstacles.push_back({Point{edge.points[i-1].x*view->scale,edge.points[i-1].y*view->scale},Point{edge.points[i].x*view->scale,edge.points[i].y*view->scale}});
            for(const auto& section:view->section_markers)if(auto layout=zima::drawing::section_trace_layout(*view,section,views)){
                obstacles.insert(obstacles.end(),layout->chain.begin(),layout->chain.end());obstacles.insert(obstacles.end(),layout->accents.begin(),layout->accents.end());
                for(int end=0;end<2;++end){const auto tip=layout->arrow_tips[end],d=layout->arrow_directions[end];
                    obstacles.push_back({Point{tip.x-d.x*8,tip.y-d.y*8},tip});
                    obstacles.push_back({Point{tip.x-d.x*3-d.y*zima::viewer::annotation_arrow_half_width(3),tip.y-d.y*3+d.x*zima::viewer::annotation_arrow_half_width(3)},Point{tip.x-d.x*3+d.y*zima::viewer::annotation_arrow_half_width(3),tip.y-d.y*3-d.x*zima::viewer::annotation_arrow_half_width(3)}});
                }
                traces.emplace_back(&section,std::move(*layout));
            }
            const auto screen=[&](Point p){return QPointF(origin.x()+(sheet_->width_mm()-view->x+p.x)*zoom,origin.y()+(sheet_->height_mm()-view->y-p.y)*zoom);};
            for(const auto& [section,layout]:traces){
                const AnnotationKey trace_key{AnnotationKind::SectionEnd,view->id,section->id,0};
                const auto trace_color=annotation_color(trace_key,ink,printing);
                QPen thin(annotation_color(trace_key,printing?ink:QColor("#E6C85C"),printing),width(false));thin.setCapStyle(Qt::FlatCap);thin.setDashPattern({8*zoom/thin.widthF(),1.5*zoom/thin.widthF(),.5*zoom/thin.widthF(),1.5*zoom/thin.widthF()});painter.setPen(thin);
                for(const auto& line:layout.chain)painter.drawLine(screen(line[0]),screen(line[1]));
                QPen thick(trace_color,width(true));thick.setCapStyle(Qt::FlatCap);painter.setPen(thick);
                for(const auto& line:layout.accents)painter.drawLine(screen(line[0]),screen(line[1]));
                auto letter=QString::fromStdString(section->name);const auto separator=letter.indexOf(QRegularExpression("[-–—]"));if(separator>0)letter=letter.left(separator).trimmed();
                QFont font(drawing_font_family());font.setPixelSize(1000);const QFontMetricsF metrics(font);const auto bounds=metrics.tightBoundingRect(letter);const double text_scale=5/metrics.capHeight();
                const Point text_size{bounds.width()*text_scale,bounds.height()*text_scale};
                for(int end=0;end<2;++end){const auto tip=screen(layout.arrow_tips[end]);const auto d=layout.arrow_directions[end];const QPointF direction(d.x,-d.y);
                    const auto tail=tip-direction*8*zoom;painter.drawLine(tail,tip);
                    painter.save();painter.setPen(Qt::NoPen);painter.setBrush(trace_color);painter.drawPolygon(zima::viewer::annotation_arrow(tip,direction,3*zoom));painter.restore();
                    const auto position=zima::drawing::section_letter_position(layout,end,text_size,obstacles,.75+sheet_->thick_line_mm/2);
                    obstacles.push_back({Point{position.x-text_size.x/2,position.y-text_size.y/2},Point{position.x+text_size.x/2,position.y+text_size.y/2}});
                    painter.save();painter.setFont(font);painter.translate(screen(position));painter.scale(text_scale*zoom,text_scale*zoom);
                    // Only translation and positive uniform scale: letters never
                    // rotate with the trace, mirror or turn upside down.
                    painter.drawText(QPointF(-bounds.center().x(),-bounds.center().y()),letter);painter.restore();
                    if(!printing){
                        QPainterPath stroke;for(auto line:layout.chain){stroke.moveTo(screen(line[0]));stroke.lineTo(screen(line[1]));}stroke.moveTo(tail);stroke.lineTo(tip);
                        QPainterPathStroker picker;picker.setWidth(10);auto hit=picker.createStroke(stroke);const auto center=screen(position);hit.addRect(QRectF(center.x()-text_size.x*zoom/2,center.y()-text_size.y*zoom/2,text_size.x*zoom,text_size.y*zoom));
                        const auto& line=end?layout.chain.back():layout.chain.front();auto outward=zima::drawing::Point2{line[end?1:0].x-line[end?0:1].x,line[end?1:0].y-line[end?0:1].y};const auto n=std::hypot(outward.x,outward.y);outward.x/=n;outward.y/=n;
                        annotation_handles_.push_back({{AnnotationKind::SectionEnd,view->id,section->id,end},tip,hit,outward,layout.end_offsets[end],layout.minimum_offsets[end]});
                    }
                }
            }
        }

        std::vector<const drawing::DrawingDimension*> dimensions;
        for(const auto& d:sheet_->dimensions)if(printing||!dimension_preview||dimension_preview->id!=d.id)dimensions.push_back(&d);
        if(!printing&&dimension_preview)dimensions.push_back(dimension_preview);
        for(const auto* entry:dimensions){
            const auto& dimension=*entry;
            const auto view_it=std::ranges::find_if(views,[&](const auto* view){return view->id==dimension.view_id;});
            if(view_it==views.end())continue;const auto* view=*view_it;
            const auto evaluation=drawing::evaluate_drawing_dimension(*view,dimension);
            if(evaluation.state==drawing::MeasurementState::Hidden)continue;
            const auto screen=[&](kernel::Vec3 p){return QPointF(origin.x()+(sheet_->width_mm()-view->x+p.x*view->scale)*zoom,origin.y()+(sheet_->height_mm()-view->y-p.y*view->scale)*zoom);};
            for(std::size_t index=0;index<evaluation.presentations.size();++index){
                const auto& source=evaluation.presentations[index];const AnnotationKey key{AnnotationKind::Dimension,dimension.view_id,dimension.id,int(evaluation.cached_segment_indices.empty()?index:evaluation.cached_segment_indices[index])*3};
                const auto color=annotation_color(key,evaluation.state==drawing::MeasurementState::Unresolved?QColor("#C62828"):printing?ink:QColor("#FFD400"),printing);
                const auto text=QString::fromStdString(drawing::drawing_dimension_text(dimension,source,evaluation.state==drawing::MeasurementState::Unresolved));
                const auto layout=viewer::dimension_presentation(source,screen,QFontMetricsF(painter.font()).horizontalAdvance(text),2.5*zoom,.75*zoom,index<evaluation.angular_leaders.size()&&evaluation.angular_leaders[index]);
                if(!layout.valid)continue;
                painter.save();painter.setPen(QPen(color,width(false)));painter.setBrush(color);QPainterPath stroke;
                for(const auto& curve:layout.curves){if(curve.empty())continue;painter.drawPolyline(curve);stroke.moveTo(curve.front());for(qsizetype i=1;i<curve.size();++i)stroke.lineTo(curve[i]);}
                for(const auto& [tip,direction]:layout.arrows)painter.drawPolygon(viewer::annotation_arrow(tip,direction,2.5*zoom));
                QTransform transform;transform.translate(layout.text_baseline.x(),layout.text_baseline.y());transform.rotate(layout.text_angle);
                dimension_texts.push_back({text,layout.text_baseline,layout.text_angle,painter.font(),color});
                if(!printing){
                    QPainterPathStroker picker;picker.setWidth(10);auto hit=picker.createStroke(stroke);hit.addRect(transform.mapRect(QFontMetricsF(painter.font()).boundingRect(text)));
                    for(int end=0;end<3;++end){auto grip=key;grip.end+=end;annotation_handles_.push_back({grip,layout.handles[end],hit});}
                }painter.restore();
            }
        }
        if(!printing)paint_reference_overlay(painter);
        viewer::paint_dimension_text_layer(painter,dimension_texts,.5*zoom,
            [printing](QPainter& text_painter,const QPainterPath& mask){
                text_painter.fillPath(mask,printing?QColor(Qt::white):QColor(Qt::black));
            });
        if(!printing&&!preview_)for(const auto& handle:annotation_handles_){
            const bool selected=(selected_annotation_&&selected_annotation_->kind==handle.key.kind&&selected_annotation_->view==handle.key.view&&selected_annotation_->id==handle.key.id)||(dimension_preview&&handle.key.kind==AnnotationKind::Dimension&&dimension_preview->id==handle.key.id),hovered=hovered_annotation_&&*hovered_annotation_==handle.key;
            const bool movable=handle.key.kind==AnnotationKind::Caption||handle.key.kind==AnnotationKind::SectionLabel||handle.key.kind==AnnotationKind::SectionEnd||handle.key.kind==AnnotationKind::Dimension||
                (handle.key.kind==AnnotationKind::Model&&std::ranges::any_of(sheet_->views,[&](const auto& view){return view.id==handle.key.view&&std::ranges::any_of(view.model_annotations,[&](const auto& item){return item.kind==drawing::ModelAnnotationKind::Dimension&&model_annotation_key(item.source)==handle.key.id;});}));
            if(movable&&(selected||hovered))draw_handle(handle.point,selected);
        }
    }

}
