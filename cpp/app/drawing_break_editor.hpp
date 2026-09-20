#pragma once
#include <zima/drawing/view_breaks.hpp>
#include <zima/drawing_render/sheet_renderer.hpp>
#include <zima/ui/properties_subwindow.hpp>
#include "inline_dimension_edit.hpp"
#include "numeric_expression_edit.hpp"
#include <QComboBox>
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
    int selected{-1};bool result{},adding{},vertical{};
    std::optional<drawing::Point2> first;
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
        if(adding){p.setPen(QColor("#4DD811"));p.drawText(12,22,first?tr("Klikněte na druhý konec přerušení."):tr("Klikněte na první konec přerušení."));}
    }
    void mousePressEvent(QMouseEvent* e)override {
        if(e->button()==Qt::MiddleButton){panning_=true;last_=e->position();return;}
        if(e->button()!=Qt::LeftButton||result)return;
        for(const auto& hit:dimension_hits_)if(hit.contains(e->position()))return;
        const auto q=model(e->position());
        if(adding){if(!first){first=q;update();return;}drawing::ViewBreak b;b.id=QUuid::createUuid().toString(QUuid::WithoutBraces).toStdString();b.vertical=vertical;
            const auto x=drawing::break_axis(*first,vertical),y=drawing::break_axis(q,vertical);b.start=std::min(x,y);b.length=std::abs(y-x);if(b.length<=.001)return;
            view().breaks.push_back(b);selected=int(view().breaks.size())-1;first.reset();adding=false;if(changed)changed();return;}
        selected=-1;drag_=0;
        for(int i=int(view().breaks.size())-1;i>=0;--i){const auto& b=view().breaks[i];const double t=drawing::break_axis(q,b.vertical),tolerance=8/factor_;
            if(std::abs(t-b.start)<tolerance){selected=i;drag_=1;break;}if(std::abs(t-b.start-b.length)<tolerance){selected=i;drag_=2;break;}
            if(t>b.start&&t<b.start+b.length){const double cross=b.vertical?std::abs(e->position().x()-width()/2.-pan_.x()):std::abs(e->position().y()-height()/2.-pan_.y());if(cross<10){selected=i;drag_=3;break;}}}
        if(selected>=0){initial_=view().breaks[selected];start_=q;}if(selected_changed)selected_changed(selected);update();
    }
    void mouseMoveEvent(QMouseEvent* e)override {
        if(panning_){pan_+=e->position()-last_;last_=e->position();update();return;}
        if(drag_&&selected>=0&&(e->buttons()&Qt::LeftButton)){auto& b=view().breaks[selected];const auto delta=drawing::break_axis(model(e->position()),b.vertical)-drawing::break_axis(start_,b.vertical);b=initial_;
            if(drag_==1){b.start=std::min(initial_.start+delta,initial_.start+initial_.length-.002);b.length=initial_.start+initial_.length-b.start;}else if(drag_==2)b.length=std::max(.002,initial_.length+delta);else b.start+=delta;
            // Freeze zoom while changing boundaries so the source geometry stays still.
            if(changed)changed();update();}
    }
    void mouseReleaseEvent(QMouseEvent*)override{panning_=false;drag_=0;}
    void mouseDoubleClickEvent(QMouseEvent* e)override{if(!result&&e->button()==Qt::LeftButton)for(std::size_t i=0;i<dimension_hits_.size();++i)if(dimension_hits_[i].contains(e->position())){if(edit_value)edit_value(int(i));return;}}
    void wheelEvent(QWheelEvent* e)override{zoom_=std::clamp(factor_*std::pow(1.0015,e->angleDelta().y()),.00001,10000.);pan_+=(e->position()-QPointF(width()/2.,height()/2.)-pan_)*(1-zoom_/factor_);update();e->accept();}
private:
    double zoom_{},factor_{1};QPointF origin_,pan_,last_;bool panning_{};int drag_{};drawing::ViewBreak initial_;drawing::Point2 start_;
    std::vector<QRectF> dimension_hits_;
};
class DrawingBreakEditor final:public ui::PropertiesSubWindow {
public:
    DrawingBreakEditor(QWidget* owner,drawing::DrawingSheet sheet,const drawing::DrawingView& value,std::function<void(std::vector<drawing::ViewBreak>)> accepted)
        :PropertiesSubWindow(tr("Přerušení pohledu"),owner),accepted_(std::move(accepted)) {
        setObjectName("drawingBreakEditor");set_initial_size({1000,780});set_centered_on_show();
        sheet.views={value};std::erase_if(sheet.dimensions,[&](const auto& d){return d.view_id!=value.id;});std::erase_if(sheet.balloons,[&](const auto& b){return b.view_id!=value.id;});
        auto* bar=new QHBoxLayout;direction_=new QComboBox(this);direction_->setObjectName("drawingBreakDirection");direction_->addItems({tr("Vodorovné zkrácení"),tr("Svislé zkrácení")});if(!value.breaks.empty())direction_->setCurrentIndex(value.breaks.front().vertical?1:0);
        auto* add=new QPushButton(tr("Přidat přerušení"),this);add->setObjectName("drawingBreakAdd");auto* remove=new QPushButton(tr("Odstranit"),this);remove->setObjectName("drawingBreakRemove");mode_=new QComboBox(this);mode_->setObjectName("drawingBreakPreviewMode");mode_->addItems({tr("Celý pohled"),tr("Výsledný náhled")});auto* fit=new QPushButton(tr("Přizpůsobit"),this);
        bar->addWidget(direction_);bar->addWidget(add);bar->addWidget(remove);bar->addWidget(mode_);bar->addWidget(fit);content_layout()->addLayout(bar);
        canvas_=new BreakEditorCanvas(std::move(sheet),this);content_layout()->addWidget(canvas_,1);
        table_=new QTableWidget(this);table_->setObjectName("drawingBreakTable");table_->setColumnCount(4);table_->setHorizontalHeaderLabels({tr("Poloha od počátku [mm]"),tr("Délka vynechání [mm]"),tr("Mezera na papíře [mm]"),tr("Značka")});table_->horizontalHeader()->setSectionResizeMode(QHeaderView::Stretch);table_->setMaximumHeight(165);table_->setSelectionBehavior(QAbstractItemView::SelectRows);content_layout()->addWidget(table_);
        error_=new QLabel(this);error_->setWordWrap(true);content_layout()->addWidget(error_);
        connect(add,&QPushButton::clicked,this,[this]{if(canvas_->view().breaks.size()>=32)return;mode_->setCurrentIndex(0);canvas_->adding=true;canvas_->vertical=direction_->currentIndex()==1;canvas_->first.reset();canvas_->update();});
        connect(remove,&QPushButton::clicked,this,[this]{auto& b=canvas_->view().breaks;if(canvas_->selected>=0&&canvas_->selected<int(b.size()))b.erase(b.begin()+canvas_->selected);canvas_->selected=-1;refresh();});
        connect(mode_,&QComboBox::currentIndexChanged,this,[this](int i){canvas_->result=i==1;canvas_->adding=false;canvas_->first.reset();canvas_->fit();});
        connect(fit,&QPushButton::clicked,canvas_,&BreakEditorCanvas::fit);
        connect(table_,&QTableWidget::currentCellChanged,this,[this](int row,int,int,int){canvas_->selected=row;canvas_->update();});
        canvas_->changed=[this]{refresh();};canvas_->selected_changed=[this](int row){table_->setCurrentCell(row,0);};canvas_->edit_value=[this](int column){edit_dimension(column);};refresh();
    }
private:
    BreakEditorCanvas* canvas_;QTableWidget* table_;QComboBox *direction_,*mode_;QLabel* error_;bool refreshing_{};
    std::function<void(std::vector<drawing::ViewBreak>)> accepted_;
    void validate(){try{drawing::validate_view_breaks(canvas_->view());error_->clear();buttons()->button(QDialogButtonBox::Ok)->setEnabled(true);}catch(const std::exception& e){error_->setText(QString::fromUtf8(e.what()));buttons()->button(QDialogButtonBox::Ok)->setEnabled(false);}}
    void refresh(){refreshing_=true;QSignalBlocker block(table_);const auto& rows=canvas_->view().breaks;direction_->setEnabled(rows.empty());if(!rows.empty())direction_->setCurrentIndex(rows.front().vertical?1:0);
        if(table_->rowCount()!=int(rows.size())){table_->setRowCount(int(rows.size()));for(int row=0;row<int(rows.size());++row)for(int col=0;col<4;++col){
            if(col==3){auto* combo=new QComboBox(table_);combo->addItems({tr("Bez čáry"),tr("Rovná čára"),tr("Cikcak")});table_->setCellWidget(row,col,combo);connect(combo,&QComboBox::currentIndexChanged,this,[this,row](int i){if(refreshing_)return;canvas_->view().breaks[row].mark=static_cast<drawing::BreakMark>(i);validate();canvas_->update();});}
            else {auto* field=new ExpressionDoubleSpinBox(table_);field->setDecimals(3);field->setRange(col==0?-1e9:col==1?.002:.1,col==2?100:1e9);table_->setCellWidget(row,col,field);connect(field,&QDoubleSpinBox::valueChanged,this,[this,row,col](double x){if(refreshing_)return;auto& b=canvas_->view().breaks[row];(col==0?b.start:col==1?b.length:b.gap)=x;canvas_->selected=row;validate();canvas_->update();});}
        }}
        for(int row=0;row<int(rows.size());++row){for(int col=0;col<3;++col)static_cast<QDoubleSpinBox*>(table_->cellWidget(row,col))->setValue(col==0?rows[row].start:col==1?rows[row].length:rows[row].gap);static_cast<QComboBox*>(table_->cellWidget(row,3))->setCurrentIndex(int(rows[row].mark));}
        table_->setCurrentCell(canvas_->selected,0);refreshing_=false;validate();canvas_->update();
    }
    void edit_dimension(int col){const int row=canvas_->selected;if(row<0)return;auto* field=new InlineDimensionEdit(canvas_);const auto& b=canvas_->view().breaks[row];field->setText(QString::number(col?b.length:b.start,'f',3));field->move(canvas_->width()/2-52,canvas_->height()/2-14);field->show();field->setFocus();field->selectAll();
        connect(field,&QLineEdit::returnPressed,this,[this,field,row,col]{try{double value=numeric_expression_value(field->text());auto candidate=canvas_->view();(col?candidate.breaks[row].length:candidate.breaks[row].start)=value;drawing::validate_view_breaks(candidate);canvas_->view().breaks=std::move(candidate.breaks);field->deleteLater();refresh();}catch(const std::exception& e){error_->setText(QString::fromUtf8(e.what()));}});
    }
    bool submit()override{try{drawing::validate_view_breaks(canvas_->view());accepted_(canvas_->view().breaks);return true;}catch(const std::exception& e){error_->setText(QString::fromUtf8(e.what()));return false;}}
};
}
