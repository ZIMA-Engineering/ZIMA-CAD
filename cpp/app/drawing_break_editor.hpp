#pragma once
#include <zima/drawing/view_breaks.hpp>
#include <zima/drawing_render/sheet_renderer.hpp>
#include <zima/ui/properties_subwindow.hpp>
#include <zima/ui/reference_cell.hpp>
#include "inline_dimension_edit.hpp"
#include "numeric_expression_edit.hpp"
#include <QComboBox>
#include <QCursor>
#include <QKeyEvent>
#include <QDialogButtonBox>
#include <QHeaderView>
#include <QLabel>
#include <QMouseEvent>
#include <QPainter>
#include <QPushButton>
#include <QTableWidget>
#include <QSignalBlocker>
#include <QUuid>
#include <QVBoxLayout>
#include <QWheelEvent>
namespace zima::app {
class BreakEditorCanvas final:public QWidget,public drawing_render::SheetRenderer {
public:
    drawing::DrawingSheet sheet;
    int selected{-1};bool result{},picking{},vertical{},draft_vertical{};
    std::optional<double> draft_first,draft_second;
    std::function<void(drawing::Point2)> boundary_picked;
    std::function<void()> entry_ended;
    std::function<void()> changed;
    std::function<void(int)> selected_changed;
    std::function<void(int)> edit_value;
    explicit BreakEditorCanvas(drawing::DrawingSheet value,QWidget* parent):QWidget(parent),sheet(std::move(value)) {
        show_paper_border_=false;setObjectName("drawingBreakCanvas");setMinimumSize(300,240);setMouseTracking(true);setFocusPolicy(Qt::StrongFocus);
        sheet.frame_lines={{{0,0},{0,0},drawing::DrawingPen::White}};sheet.frame_texts.clear();sheet.frame_circles.clear();sheet.title_block_circles.clear();sheet.title_block_lines.clear();sheet.title_block_texts.clear();sheet.title_block_fields.clear();sheet.title_block_images.clear();sheet.texts.clear();sheet.repeat_regions.clear();
    }
    drawing::DrawingView& view(){return sheet.views.front();}
    const drawing::DrawingView& view()const{return sheet.views.front();}
    void fit(){zoom_=0;pan_={};update();}
    QPointF screen(drawing::Point2 p)const{return origin_+QPointF(p.x*factor_,-p.y*factor_);}
    drawing::Point2 model(QPointF p)const{return {(p.x()-origin_.x())/factor_,(origin_.y()-p.y())/factor_};}
    void start_pick(bool along_vertical) {vertical=along_vertical;picking=true;pointer_=mapFromGlobal(QCursor::pos());pointer_inside_=rect().contains(pointer_.toPoint());setFocus(Qt::OtherFocusReason);update();}
    void end_pick() {picking=false;pointer_inside_=false;update();}
protected:
    void paintEvent(QPaintEvent*)override {
        QPainter p(this);p.fillRect(rect(),Qt::black);
        auto copy=sheet;auto& v=copy.views.front();if(!result)v.breaks.clear();
        bool valid=true;try{drawing::validate_view_breaks(v);}catch(...){valid=false;v.breaks.clear();}
        const auto edges=drawing::broken_edges(v);QRectF bounds;bool empty=true;
        for(const auto& e:edges)for(auto point:e.points){QPointF q(point.x,-point.y);if(empty){bounds=QRectF(q,q);empty=false;}else bounds=bounds.united(QRectF(q-QPointF(.00001,.00001),QSizeF(.00002,.00002)));}
        if(empty)bounds=QRectF(-50,-30,100,60);
        factor_=zoom_>0?zoom_:std::min((width()-100)/std::max(bounds.width(),1.),(height()-100)/std::max(bounds.height(),1.));
        factor_=std::clamp(factor_,.00001,10000.);origin_=QPointF(width()/2.,height()/2.)-bounds.center()*factor_+pan_;
        v.x=copy.width_mm();v.y=copy.height_mm();set_render_sheet(&copy);paint_sheet(p,factor_/v.scale,origin_,false);set_render_sheet(nullptr);
        if(result){if(!valid){p.setPen(Qt::red);p.drawText(15,25,tr("Neplatné přerušení — upravte hranice."));}return;}
        p.setRenderHint(QPainter::Antialiasing);p.setPen(QPen(QColor("#9B7546"),1,Qt::DashLine));
        p.drawLine(screen({0,0})-QPointF(12,0),screen({0,0})+QPointF(12,0));p.drawLine(screen({0,0})-QPointF(0,12),screen({0,0})+QPointF(0,12));
        dimension_hits_.clear();
        for(std::size_t i=0;i<view().breaks.size();++i){const auto& b=view().breaks[i];const bool active=int(i)==selected;
            const double cross=b.vertical?-bounds.center().y():bounds.center().x();
            auto a=screen(b.vertical?drawing::Point2{cross,b.start}:drawing::Point2{b.start,-bounds.center().y()});
            auto z=screen(b.vertical?drawing::Point2{cross,b.start+b.length}:drawing::Point2{b.start+b.length,-bounds.center().y()});
            // The segment is perpendicular to both construction boundaries.
            if(b.vertical){a.setX(width()/2.+pan_.x());z.setX(a.x());}
            const auto color=active?QColor("#4DD811"):QColor("#9B7546");
            QRectF strip=b.vertical?QRectF(0,std::min(a.y(),z.y()),width(),std::abs(a.y()-z.y())):QRectF(std::min(a.x(),z.x()),0,std::abs(a.x()-z.x()),height());
            p.fillRect(strip,QColor(80,130,60,35));p.setPen(QPen(color,1,Qt::DashLine));
            for(auto q:{a,z})if(b.vertical)p.drawLine(QPointF(0,q.y()),QPointF(width(),q.y()));else p.drawLine(QPointF(q.x(),0),QPointF(q.x(),height()));
            p.setPen(QPen(color,2));p.drawLine(a,z);p.setBrush(color);p.drawEllipse(a,4,4);p.drawEllipse(z,4,4);p.drawText(a+QPointF(6,-8),QString::number(i+1)+" A");p.drawText(z+QPointF(6,-8),"B");
            if(active){
                const auto position=QString::number(b.start,'f',3)+" mm",length=QString::number(b.length,'f',3)+" mm";
                const QPointF shift=b.vertical?QPointF(35,0):QPointF(0,35);
                auto zero=screen({0,0});if(b.vertical)zero.setX(a.x());else zero.setY(a.y());
                p.setPen(QPen(QColor("#FFD400"),1));p.drawLine(zero-shift,a-shift);p.drawLine(a+shift,z+shift);
                for(const auto& item:std::array<std::pair<QPointF,QString>,2>{{{(zero+a)/2-shift,position},{(a+z)/2+shift,length}}}){
                    const auto rect=QFontMetricsF(p.font()).boundingRect(item.second).adjusted(-5,-4,5,4);QRectF box(item.first-QPointF(rect.width()/2,rect.height()/2),rect.size());p.fillRect(box,Qt::black);p.drawText(box,Qt::AlignCenter,item.second);dimension_hits_.push_back(box);
                }
            }
        }
        const auto boundary=[&](double value,bool along_vertical,QColor color) {
            const auto q=screen(along_vertical?drawing::Point2{0,value}:drawing::Point2{value,0});
            p.setPen(QPen(color,1,Qt::DashLine));
            if(along_vertical)p.drawLine(QPointF(0,q.y()),QPointF(width(),q.y()));
            else p.drawLine(QPointF(q.x(),0),QPointF(q.x(),height()));
        };
        for(const auto& value:{draft_first,draft_second})if(value)boundary(*value,draft_vertical,QColor("#9B7546"));
        if(picking&&pointer_inside_) {
            const QColor color("#4DD811");boundary(drawing::break_axis(model(pointer_),vertical),vertical,color);
            p.setBrush(color);p.setPen(QPen(color,1));p.drawEllipse(pointer_,4,4);
        }
    }
    void mousePressEvent(QMouseEvent* e)override {
        if(e->button()==Qt::MiddleButton){if(picking){end_pick();if(entry_ended)entry_ended();}panning_=true;last_=e->position();return;}
        if(e->button()!=Qt::LeftButton||result)return;
        if(picking){if(boundary_picked)boundary_picked(model(e->position()));e->accept();return;}
        for(const auto& hit:dimension_hits_)if(hit.contains(e->position()))return;
        const auto q=model(e->position());
        selected=-1;drag_=0;
        for(int i=int(view().breaks.size())-1;i>=0;--i){const auto& b=view().breaks[i];const double t=drawing::break_axis(q,b.vertical),tolerance=8/factor_;
            if(std::abs(t-b.start)<tolerance){selected=i;drag_=1;break;}if(std::abs(t-b.start-b.length)<tolerance){selected=i;drag_=2;break;}
            if(t>b.start&&t<b.start+b.length){const double cross=b.vertical?std::abs(e->position().x()-width()/2.-pan_.x()):std::abs(e->position().y()-height()/2.-pan_.y());if(cross<10){selected=i;drag_=3;break;}}}
        if(selected>=0){initial_=view().breaks[selected];start_=q;}if(selected_changed)selected_changed(selected);update();
    }
    void mouseMoveEvent(QMouseEvent* e)override {
        pointer_=e->position();pointer_inside_=rect().contains(pointer_.toPoint());if(picking)update();
        if(panning_){pan_+=e->position()-last_;last_=e->position();update();return;}
        if(drag_&&selected>=0&&(e->buttons()&Qt::LeftButton)){auto& b=view().breaks[selected];const auto delta=drawing::break_axis(model(e->position()),b.vertical)-drawing::break_axis(start_,b.vertical);b=initial_;
            if(drag_==1){b.start=std::min(initial_.start+delta,initial_.start+initial_.length-.002);b.length=initial_.start+initial_.length-b.start;}else if(drag_==2)b.length=std::max(.002,initial_.length+delta);else b.start+=delta;
            // Freeze zoom while changing boundaries so the source geometry stays still.
            if(changed)changed();update();}
    }
    void mouseReleaseEvent(QMouseEvent*)override{panning_=false;drag_=0;}
    void leaveEvent(QEvent*)override{pointer_inside_=false;update();}
    void keyPressEvent(QKeyEvent* e)override{if(e->key()==Qt::Key_Escape&&picking){end_pick();if(entry_ended)entry_ended();e->accept();return;}QWidget::keyPressEvent(e);}
    void mouseDoubleClickEvent(QMouseEvent* e)override{if(!result&&e->button()==Qt::LeftButton)for(std::size_t i=0;i<dimension_hits_.size();++i)if(dimension_hits_[i].contains(e->position())){if(edit_value)edit_value(int(i));return;}}
    void wheelEvent(QWheelEvent* e)override{zoom_=std::clamp(factor_*std::pow(1.0015,e->angleDelta().y()),.00001,10000.);pan_+=(e->position()-QPointF(width()/2.,height()/2.)-pan_)*(1-zoom_/factor_);update();e->accept();}
private:
    double zoom_{},factor_{1};QPointF origin_,pan_,last_,pointer_;bool panning_{},pointer_inside_{};int drag_{};drawing::ViewBreak initial_;drawing::Point2 start_;
    std::vector<QRectF> dimension_hits_;
};
class DrawingBreakEditor final:public ui::PropertiesSubWindow {
public:
    DrawingBreakEditor(QWidget* owner,drawing::DrawingSheet sheet,const drawing::DrawingView& value,std::function<void(std::vector<drawing::ViewBreak>)> accepted)
        :PropertiesSubWindow(tr("Přerušení pohledu"),owner),accepted_(std::move(accepted)) {
        setObjectName("drawingBreakEditor");set_initial_size({1160,780});set_centered_on_show();
        sheet.views={value};std::erase_if(sheet.dimensions,[&](const auto& d){return d.view_id!=value.id;});std::erase_if(sheet.balloons,[&](const auto& b){return b.view_id!=value.id;});
        canvas_=new BreakEditorCanvas(std::move(sheet),this);content_layout()->addWidget(canvas_,1);
        table_=new QTableWidget(this);table_->setObjectName("drawingBreakTable");table_->setColumnCount(8);
        auto reference_palette=table_->palette();reference_palette.setColor(QPalette::Base,QColor("#20252b"));
        reference_palette.setColor(QPalette::Text,QColor("#e6edf3"));table_->setPalette(reference_palette);
        table_->setHorizontalHeaderLabels({QString{},tr("Směr"),tr("První poloha"),tr("Druhá poloha"),tr("Poloha od počátku [mm]"),tr("Délka vynechání [mm]"),tr("Mezera na papíře [mm]"),tr("Značka")});
        for(int column=4;column<7;++column){auto* heading=table_->horizontalHeaderItem(column);QStringList lines;QString line;
            for(const auto& word:heading->text().split(' ')){if(!line.isEmpty()&&table_->fontMetrics().horizontalAdvance(line+" "+word)>125){lines.push_back(line);line.clear();}if(!line.isEmpty())line+=' ';line+=word;}
            if(!line.isEmpty())lines.push_back(line);heading->setText(lines.join('\n'));}
        table_->horizontalHeader()->setSectionResizeMode(QHeaderView::Stretch);table_->horizontalHeader()->setSectionResizeMode(0,QHeaderView::Fixed);table_->setColumnWidth(0,34);
        table_->verticalHeader()->hide();table_->verticalHeader()->setDefaultSectionSize(34);table_->setMinimumHeight(118);table_->setMaximumHeight(190);
        table_->setSelectionMode(QAbstractItemView::NoSelection);table_->setEditTriggers(QAbstractItemView::NoEditTriggers);ui::install_reference_cell_delegate(table_);content_layout()->addWidget(table_);
        error_=new QLabel(this);error_->setObjectName("drawingBreakError");error_->setWordWrap(true);content_layout()->addWidget(error_);
        connect(table_,&QTableWidget::cellClicked,this,[this](int row,int column){if(column==2||column==3)arm(row,column-2);});
        canvas_->changed=[this]{refresh();};canvas_->selected_changed=[this](int){end_entry();};canvas_->edit_value=[this](int column){edit_dimension(column);};
        canvas_->boundary_picked=[this](auto point){pick(point);};canvas_->entry_ended=[this]{end_entry();};
        if(!value.breaks.empty())draft_.vertical=value.breaks.front().vertical;refresh();
    }
private:
    BreakEditorCanvas* canvas_;QTableWidget* table_;QLabel* error_;bool refreshing_{};
    drawing::ViewBreak draft_;std::optional<double> first_,second_;int active_row_{-1},active_end_{};
    std::function<void(std::vector<drawing::ViewBreak>)> accepted_;
    void validate(){try{drawing::validate_view_breaks(canvas_->view());
        if(first_||second_)throw std::invalid_argument(QT_TR_NOOP("Zadejte obě polohy přerušení."));
        error_->clear();buttons()->button(QDialogButtonBox::Ok)->setEnabled(true);
    }catch(const std::exception& e){error_->setText(tr(e.what()));buttons()->button(QDialogButtonBox::Ok)->setEnabled(false);}}
    void end_entry(){active_row_=-1;canvas_->end_pick();refresh();}
    void arm(int row,int end){active_row_=row;active_end_=end;canvas_->selected=row<int(canvas_->view().breaks.size())?row:-1;
        const auto& value=row<int(canvas_->view().breaks.size())?canvas_->view().breaks[row]:draft_;
        canvas_->start_pick(value.vertical);refresh();}
    void pick(drawing::Point2 point){if(active_row_<0)return;
        const int row=active_row_;auto& rows=canvas_->view().breaks;const bool existing=row<int(rows.size());
        auto value=existing?rows[row]:draft_;auto first=existing?std::optional(value.start):first_;auto second=existing?std::optional(value.start+value.length):second_;
        (active_end_==0?first:second)=drawing::break_axis(point,value.vertical);
        if(first&&second){value.start=std::min(*first,*second);value.length=std::abs(*second-*first);
            if(value.length<=.001){error_->setText(tr("Polohy přerušení musí být různé."));return;}
            if(existing)rows[row]=value;
            else {value.id=QUuid::createUuid().toString(QUuid::WithoutBraces).toStdString();rows.push_back(value);first_.reset();second_.reset();draft_=drawing::ViewBreak{};draft_.vertical=value.vertical;}
            canvas_->selected=row;
        }else {first_=first;second_=second;}
        end_entry();
    }
    void refresh(){refreshing_=true;QSignalBlocker block(table_);const auto& rows=canvas_->view().breaks;
        const int count=int(rows.size())+(rows.size()<32?1:0);
        if(table_->rowCount()!=count){table_->setRowCount(0);table_->setRowCount(count);
            for(int row=0;row<count;++row){
                auto* indicator=ui::build_reference_row_indicator([this,row]{if(row>=int(canvas_->view().breaks.size()))return;
                    auto& rows=canvas_->view().breaks;rows.erase(rows.begin()+row);canvas_->selected=-1;active_row_=-1;canvas_->end_pick();refresh();});
                indicator->setObjectName("drawingBreakRowIndicator");
                table_->setCellWidget(row,0,ui::centered_cell_widget(indicator));
                auto* direction=new QComboBox(table_);direction->setObjectName("drawingBreakDirection");direction->addItems({tr("Vodorovně"),tr("Svisle")});table_->setCellWidget(row,1,direction);
                connect(direction,&QComboBox::currentIndexChanged,this,[this,row](int i){if(refreshing_)return;const bool stored=row<int(canvas_->view().breaks.size());
                    (stored?canvas_->view().breaks[row]:draft_).vertical=i==1;if(!stored){first_.reset();second_.reset();}
                    if(active_row_==row)canvas_->start_pick(i==1);refresh();});
                for(int col:{2,3})table_->setItem(row,col,new ui::ReferenceCellItem);
                for(int col=4;col<7;++col){auto* field=new ExpressionDoubleSpinBox(table_);field->setDecimals(3);field->setRange(col==4?-1e9:col==5?.002:.1,col==6?100:1e9);table_->setCellWidget(row,col,field);
                    connect(field,&QDoubleSpinBox::valueChanged,this,[this,row,col](double x){if(refreshing_)return;auto& rows=canvas_->view().breaks;const bool stored=row<int(rows.size());auto& b=stored?rows[row]:draft_;
                        (col==4?b.start:col==5?b.length:b.gap)=x;canvas_->selected=stored?row:-1;refresh();});}
                auto* mark=new QComboBox(table_);mark->addItems({tr("Bez čáry"),tr("Rovná čára"),tr("Cikcak")});table_->setCellWidget(row,7,mark);
                connect(mark,&QComboBox::currentIndexChanged,this,[this,row](int i){if(refreshing_)return;(row<int(canvas_->view().breaks.size())?canvas_->view().breaks[row]:draft_).mark=static_cast<drawing::BreakMark>(i);validate();canvas_->update();});
            }
        }
        for(int row=0;row<count;++row){const bool stored=row<int(rows.size());const auto& b=stored?rows[row]:draft_;
            ui::set_reference_row_populated(table_->cellWidget(row,0)->findChild<QWidget*>("drawingBreakRowIndicator"),stored);
            static_cast<QComboBox*>(table_->cellWidget(row,1))->setCurrentIndex(b.vertical?1:0);
            for(int col:{2,3}){auto* item=static_cast<ui::ReferenceCellItem*>(table_->item(row,col));const auto position=stored?std::optional(col==2?b.start:b.start+b.length):(col==2?first_:second_);
                item->setText(position?QString::number(*position,'f',3)+" mm":col==2?tr("První poloha"):tr("Druhá poloha"));
                if(position)item->set_reference(QString::number(*position,'g',16));else item->clear_reference();item->set_active_input(active_row_==row&&active_end_==col-2);}
            for(int col=4;col<7;++col){auto* field=static_cast<QDoubleSpinBox*>(table_->cellWidget(row,col));field->setValue(col==4?b.start:col==5?b.length:b.gap);field->setEnabled(stored||col==6);}
            static_cast<QComboBox*>(table_->cellWidget(row,7))->setCurrentIndex(int(b.mark));
        }
        canvas_->draft_first=first_;canvas_->draft_second=second_;canvas_->draft_vertical=draft_.vertical;
        refreshing_=false;validate();table_->viewport()->update();canvas_->update();
    }
    void edit_dimension(int col){const int row=canvas_->selected;if(row<0)return;auto* field=new InlineDimensionEdit(canvas_);const auto& b=canvas_->view().breaks[row];field->setText(QString::number(col?b.length:b.start,'f',3));field->move(canvas_->width()/2-52,canvas_->height()/2-14);field->show();field->setFocus();field->selectAll();
        connect(field,&QLineEdit::returnPressed,this,[this,field,row,col]{try{double value=numeric_expression_value(field->text());auto candidate=canvas_->view();(col?candidate.breaks[row].length:candidate.breaks[row].start)=value;drawing::validate_view_breaks(candidate);canvas_->view().breaks=std::move(candidate.breaks);field->deleteLater();refresh();}catch(const std::exception& e){error_->setText(tr(e.what()));}});
    }
    bool submit()override{validate();if(!buttons()->button(QDialogButtonBox::Ok)->isEnabled())return false;accepted_(canvas_->view().breaks);return true;}
};
}
