#pragma once
#include <zima/ui/properties_subwindow.hpp>
#include <zima/ui/reference_cell.hpp>
#include <zima/drawing/balloon.hpp>
#include <zima/drawing/measurement_dimension.hpp>
#include <QApplication>
#include <QDialogButtonBox>
#include <QDoubleSpinBox>
#include <QFormLayout>
#include <QHeaderView>
#include <QLabel>
#include <QMouseEvent>
#include <QPushButton>
#include <QSignalBlocker>
#include <QTableWidget>
#include <QToolButton>
#include <QVBoxLayout>
#include <algorithm>
namespace zima::app {
// One transaction and interaction contract for managing and editing balloons.
class DrawingBalloonDialog final : public ui::PropertiesSubWindow {
public:
    DrawingBalloonDialog(drawing::DrawingSheet sheet,std::string view,std::string balloon,
        std::function<void(const std::vector<drawing::DrawingBalloon>&)> commit,QWidget* parent)
        :PropertiesSubWindow(tr("Pozice"),parent),sheet_(std::move(sheet)),view_(std::move(view)),selected_(std::move(balloon)),commit_(std::move(commit)) {
        setObjectName("drawingBalloonProperties");set_initial_size({500,445});setAttribute(Qt::WA_DeleteOnClose);
        if(auto* b=selected_balloon())view_=b->view_id;
        references_=new QTableWidget(2,2,this);references_->setObjectName("drawingBalloonReferences");
        references_->setHorizontalHeaderLabels({tr("Reference"),QString()});references_->setVerticalHeaderLabels({tr("Pohled"),tr("Uchycení")});
        references_->setSelectionMode(QAbstractItemView::NoSelection);references_->setEditTriggers(QAbstractItemView::NoEditTriggers);
        references_->horizontalHeader()->setSectionResizeMode(0,QHeaderView::Stretch);references_->horizontalHeader()->setSectionResizeMode(1,QHeaderView::ResizeToContents);
        references_->setMaximumHeight(110);ui::install_reference_cell_delegate(references_);
        for(int row=0;row<2;++row) {
            cells_[row]=new ui::ReferenceCellItem;references_->setItem(row,0,cells_[row]);
            eyes_[row]=ui::build_reference_inspection_button(false,false,[this,row](bool on){inspected_[row]=on;publish();});
            references_->setCellWidget(row,1,ui::centered_cell_widget(eyes_[row]));
        }
        content_layout()->addWidget(references_);
        connect(references_,&QTableWidget::cellClicked,this,[this](int row,int col){if(col==0){active_=row;placing_=false;new_balloon_=row==1&&!selected_balloon();publish();}});
        auto* actions=new QHBoxLayout;content_layout()->addLayout(actions);
        show_=new QPushButton(tr("Show all"),this);show_->setObjectName("drawingBalloonShowAll");
        erase_=new QPushButton(tr("Erase all"),this);erase_->setObjectName("drawingBalloonEraseAll");
        add_=new QPushButton(tr("Přidat pozici"),this);add_->setObjectName("drawingBalloonAdd");
        for(auto* button:{show_,erase_,add_}){button->setAutoDefault(false);actions->addWidget(button);}
        connect(show_,&QPushButton::clicked,this,[this]{drawing::show_all_balloons(sheet_,view_);active_=-1;placing_=false;publish();});
        connect(erase_,&QPushButton::clicked,this,[this]{drawing::erase_all_balloons(sheet_,view_);selected_.clear();end_entry();});
        connect(add_,&QPushButton::clicked,this,[this]{selected_.clear();active_=1;new_balloon_=true;placing_=false;publish();});
        auto* form=new QFormLayout;content_layout()->addLayout(form);
        item_=new QLabel(this);item_->setObjectName("drawingBalloonItem");form->addRow(tr("Číslo položky"),item_);
        diameter_=number("drawingBalloonDiameter",2,100,16);form->addRow(tr("Průměr balónku"),diameter_);
        height_=number("drawingBalloonTextHeight",1,30,5);form->addRow(tr("Výška textu"),height_);
        x_=number("drawingBalloonX",-10000,10000,0);y_=number("drawingBalloonY",-10000,10000,0);
        form->addRow(tr("Poloha X"),x_);form->addRow(tr("Poloha Y"),y_);
        for(auto* field:{diameter_,height_,x_,y_})connect(field,qOverload<double>(&QDoubleSpinBox::valueChanged),this,[this]{
            if(auto* b=selected_balloon()){b->diameter=diameter_->value();b->text_height=height_->value();b->position={x_->value(),y_->value()};}publish(false);
        });
        status_=new QLabel(this);status_->setObjectName("drawingBalloonStatus");status_->setWordWrap(true);content_layout()->addWidget(status_);
        auto* hint=new QLabel(tr("Vyberte pohled. Show all zobrazí pozice první úrovně kusovníku. Přidat pozici: vyberte hranu dílu nebo podsestavy a umístěte balónek. Fialové body přesouvají balónek a jeho uchycení."),this);
        hint->setWordWrap(true);content_layout()->addWidget(hint);
        active_=view_.empty()?0:-1;publish();
    }
    const drawing::DrawingSheet& staged_sheet()const{return sheet_;}
    const std::string& view_id()const{return view_;}
    const std::string& selected_id()const{return selected_;}
    bool choosing_view()const{return active_==0;}
    bool entering()const{return active_==1;}
    bool placing()const{return placing_;}
    bool inspecting_view()const{return inspected_[0];}
    bool inspecting_attachment()const{return inspected_[1];}
    drawing::DrawingBalloon* balloon(const std::string& id){auto i=std::ranges::find(sheet_.balloons,id,&drawing::DrawingBalloon::id);return i==sheet_.balloons.end()?nullptr:&*i;}
    drawing::DrawingBalloon* selected_balloon(){return balloon(selected_);}
    void set_changed(std::function<void()> changed){changed_=std::move(changed);publish();}
    void select_view(const std::string& id){view_=id;selected_.clear();active_=-1;placing_=false;publish();}
    void select_balloon(const std::string& id){selected_=id;if(const auto* b=selected_balloon())view_=b->view_id;active_=-1;placing_=false;publish();}
    void end_entry(){active_=-1;inspected_={false,false};publish();}
    void changed(){publish();}
    void accept_candidate(const std::string& view,const drawing::MeasurementCandidate& candidate){
        if(view!=view_)return;
        auto* b=new_balloon_?nullptr:selected_balloon();
        if(!b){sheet_.balloons.push_back(drawing::make_drawing_balloon());b=&sheet_.balloons.back();b->view_id=view_;selected_=b->id;b->diameter=diameter_->value();b->text_height=height_->value();}
        b->attachment=candidate.attachment;b->visible=true;drawing::refresh_balloon(sheet_,*b);
        if(new_balloon_){const auto* v=find_view();b->position={candidate.position.x*v->scale+25,candidate.position.y*v->scale+25};placing_=true;}
        active_=-1;new_balloon_=false;inspected_[1]=true;publish();
    }
    void position(drawing::Point2 point,bool final){if(auto* b=selected_balloon())b->position=point;if(final)placing_=false;publish();}
protected:
    bool submit()override {commit_(sheet_.balloons);return true;}
    bool eventFilter(QObject* watched,QEvent* event)override {
        const auto* widget=qobject_cast<QWidget*>(watched);
        if(isVisible()&&widget&&parentWidget()&&(widget==parentWidget()||parentWidget()->isAncestorOf(widget))) {
            if(event->type()==QEvent::MouseButtonPress) {
                const auto* e=static_cast<QMouseEvent*>(event);
                if(e->button()==Qt::MiddleButton){middle_origin_=e->globalPosition();middle_pending_=true;}
                if((e->buttons()&Qt::MiddleButton)&&(e->buttons()&Qt::RightButton))middle_pending_=false;
            }else if(event->type()==QEvent::MouseMove&&middle_pending_) {
                if((static_cast<QMouseEvent*>(event)->globalPosition()-middle_origin_).manhattanLength()>=QApplication::startDragDistance())middle_pending_=false;
            }else if(event->type()==QEvent::MouseButtonRelease) {
                if(static_cast<QMouseEvent*>(event)->button()==Qt::MiddleButton&&middle_pending_){middle_pending_=false;end_entry();}
            }
        }
        return PropertiesSubWindow::eventFilter(watched,event);
    }
private:
    const drawing::DrawingView* find_view()const {const auto it=std::ranges::find(sheet_.views,view_,&drawing::DrawingView::id);return it==sheet_.views.end()?nullptr:&*it;}
    QDoubleSpinBox* number(const char* name,double low,double high,double initial){auto* f=new QDoubleSpinBox(this);f->setObjectName(name);f->setDecimals(ui::numeric_decimal_places(this));f->setRange(low,high);f->setValue(initial);f->setSuffix(" mm");return f;}
    void publish(bool fields=true){
        const auto* v=find_view();const auto* b=selected_balloon();
        for(int row=0;row<2;++row){
            const bool present=row==0?v!=nullptr:b!=nullptr;
            cells_[row]->set_reference(present?QString::fromStdString(row==0?v->id:b->attachment.reference.semantic_key):QString());
            cells_[row]->setText(present?QString::fromStdString(row==0?v->name:b->attachment.reference.owner_id+" / "+b->attachment.reference.semantic_key):tr("Vyberte referenci"));
            cells_[row]->set_active_input(active_==row);cells_[row]->set_inspected(present&&inspected_[row]);
            QSignalBlocker block(eyes_[row]);eyes_[row]->setEnabled(present);eyes_[row]->setChecked(present&&inspected_[row]);
        }
        item_->setText(b?QString::number(drawing::evaluate_balloon(sheet_,*b).item_number):QStringLiteral("—"));
        const bool source=v&&v->source_document_id==sheet_.bom_source_document_id;
        show_->setEnabled(source);erase_->setEnabled(v);add_->setEnabled(source);
        diameter_->setEnabled(b||(new_balloon_&&entering()));height_->setEnabled(b||(new_balloon_&&entering()));
        if(b&&fields){for(auto [field,value]:{std::pair{diameter_,b->diameter},std::pair{height_,b->text_height},std::pair{x_,b->position.x},std::pair{y_,b->position.y}}){QSignalBlocker block(field);field->setValue(value);}}
        x_->setEnabled(b);y_->setEnabled(b);
        status_->setText(!v?tr("Vyberte pohled"):!source?tr("Pohled nepatří ke kusovníku tohoto listu."):b&&drawing::evaluate_balloon(sheet_,*b).unresolved?tr("Uchycení pozice chybí. Vyberte novou referenci."):placing_?tr("Kliknutím umístěte balónek."):entering()?tr("Vyberte hranu dílu nebo podsestavy."):tr("Pozice: %1").arg(std::count_if(sheet_.balloons.begin(),sheet_.balloons.end(),[&](const auto& item){return item.view_id==view_&&item.visible;})));
        references_->viewport()->update();if(changed_)changed_();
    }
    drawing::DrawingSheet sheet_;
    std::string view_,selected_;
    std::function<void(const std::vector<drawing::DrawingBalloon>&)> commit_;
    std::function<void()> changed_;
    int active_{-1};bool new_balloon_{},placing_{},middle_pending_{};QPointF middle_origin_;
    std::array<bool,2> inspected_{};
    QTableWidget* references_{};std::array<ui::ReferenceCellItem*,2> cells_{};std::array<QToolButton*,2> eyes_{};
    QPushButton *show_{},*erase_{},*add_{};QDoubleSpinBox *diameter_{},*height_{},*x_{},*y_{};QLabel *item_{},*status_{};
};
}
