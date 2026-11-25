#pragma once
#include <zima/document/cam_definition.hpp>
#include <zima/kernel/stable_id.hpp>
#include <zima/ui/properties_subwindow.hpp>
#include <zima/ui/reference_cell.hpp>
#include <QComboBox>
#include <QHeaderView>
#include <QLabel>
#include <QLineEdit>
#include <QPushButton>
#include <QSignalBlocker>
#include <QTableWidget>
#include <QToolButton>
#include <QEvent>
#include <QVBoxLayout>
#include <map>
#include <set>

namespace zima::app {
class CamClampingDialog final : public ui::PropertiesSubWindow {
public:
    std::function<void()> changed;
    CamClampingDialog(document::CamDefinition definition,std::map<std::string,QString> sources,
        std::function<void(std::vector<document::CamClamping>)> commit,QWidget* parent)
        :PropertiesSubWindow(tr("Clamping Sketches"),parent),pending_(std::move(definition)),sources_(std::move(sources)),commit_(std::move(commit)) {
        setObjectName("camClampingDialog");setAttribute(Qt::WA_DeleteOnClose);
        auto* row=new QHBoxLayout;content_layout()->addLayout(row);selected_=new QComboBox(this);selected_->setObjectName("camSelectedClamping");row->addWidget(selected_,1);
        add_=new QPushButton(this);add_->setObjectName("camAddClamping");row->addWidget(add_);remove_=new QPushButton(this);remove_->setObjectName("camRemoveClamping");row->addWidget(remove_);
        connect(selected_,&QComboBox::currentIndexChanged,this,[this]{active_=-1;inspected_.clear();refresh();notify();});
        connect(add_,&QPushButton::clicked,this,[this]{document::CamClamping c;c.id=kernel::make_stable_id();
            c.name=tr("Clamping %1").arg(pending_.clampings.size()+1).toStdString();pending_.clampings.push_back(std::move(c));select(int(pending_.clampings.size())-1);arm(0);});
        connect(remove_,&QPushButton::clicked,this,[this]{const auto index=selected_->currentIndex();if(index<0)return;
            pending_.clampings.erase(pending_.clampings.begin()+index);select(std::min(index,int(pending_.clampings.size())-1));notify();});
        name_=new QLineEdit(this);name_->setObjectName("camClampingName");content_layout()->addWidget(name_);
        connect(name_,&QLineEdit::textChanged,this,[this](const QString& name){if(auto* c=current()){c->name=name.toStdString();selected_->setItemText(selected_->currentIndex(),name);clear_error();}});
        table_=new QTableWidget(0,6,this);table_->setObjectName("camClampingReferences");table_->verticalHeader()->hide();table_->verticalHeader()->setDefaultSectionSize(32);
        table_->setSelectionMode(QAbstractItemView::NoSelection);table_->setEditTriggers(QAbstractItemView::NoEditTriggers);ui::install_reference_cell_delegate(table_);
        for(int col:{0,1,5}){table_->setColumnWidth(col,32);table_->horizontalHeader()->setSectionResizeMode(col,QHeaderView::Fixed);}
        for(int col:{2,4})table_->horizontalHeader()->setSectionResizeMode(col,QHeaderView::ResizeToContents);table_->horizontalHeader()->setSectionResizeMode(3,QHeaderView::Stretch);
        content_layout()->addWidget(table_);connect(table_,&QTableWidget::cellClicked,this,[this](int row,int col){if(col==3)arm(row);});
        help_=new QLabel(this);help_->setWordWrap(true);content_layout()->addWidget(help_);
        status_=new QLabel(this);status_->setWordWrap(true);content_layout()->addWidget(status_);select(pending_.clampings.empty()?-1:0);set_initial_size({780,0});
    }
    const auto& pending_value()const{return pending_.clampings;}
    bool active_input()const{return active_>=0;}
    bool accepts_source(const std::string& id)const{return active_input()&&sources_.contains(id);}
    bool set_source(const std::string& id){auto* c=current();if(!c||!accepts_source(id))return false;
        (active_==0?c->contact_sketch_id:c->upper_sketch_id)=id;inspected_.erase(active_);active_=-1;clear_error();refresh();notify();return true;}
    void arm(int row){if(!current()||row<0||row>1)return;active_=row;refresh();notify();}
    void end_input(){active_=-1;inspected_.clear();refresh();notify();}
    std::vector<std::pair<std::string,bool>> inspections()const {
        std::vector<std::pair<std::string,bool>> out;if(const auto* c=current())for(int row:inspected_)
            out.emplace_back(row==0?c->contact_sketch_id:c->upper_sketch_id,row==0?c->outward_positive:c->upper_positive);return out;
    }
protected:
    bool submit()override{try{document::validate_cam_definition(pending_);commit_(pending_.clampings);return true;}
        catch(const std::exception& e){error_=e.what();status_->setText(tr(error_.c_str()));return false;}}
    void changeEvent(QEvent* e)override{PropertiesSubWindow::changeEvent(e);if(e->type()==QEvent::LanguageChange&&status_)refresh();}
private:
    document::CamDefinition pending_;std::map<std::string,QString> sources_;std::function<void(std::vector<document::CamClamping>)> commit_;
    QComboBox* selected_{};QPushButton *add_{},*remove_{};QLineEdit* name_{};QTableWidget* table_{};QLabel *help_{},*status_{};
    int active_{-1};std::set<int> inspected_;std::string error_;
    document::CamClamping* current(){const int i=selected_->currentIndex();return i>=0&&i<int(pending_.clampings.size())?&pending_.clampings[i]:nullptr;}
    const document::CamClamping* current()const{return const_cast<CamClampingDialog*>(this)->current();}
    void notify(){if(changed)changed();}
    void clear_error(){error_.clear();status_->clear();}
    void select(int i){const QSignalBlocker block(selected_);selected_->clear();for(const auto& c:pending_.clampings)selected_->addItem(QString::fromStdString(c.name));selected_->setCurrentIndex(i);
        active_=-1;inspected_.clear();clear_error();refresh();}
    void refresh(){set_internal_title(tr("Clamping Sketches"));add_->setText(tr("Add clamping"));remove_->setText(tr("Remove clamping"));remove_->setToolTip(tr("Remove this clamping definition and its Sketch references."));
        help_->setText(tr("The contact Sketch bounds the forbidden outward space. The upper Sketch normal points above the jaws. Inspection shows the selected normal; below-jaw removal and contact loss are reported separately."));
        const auto* c=current();remove_->setEnabled(c);name_->setEnabled(c);{const QSignalBlocker block(name_);name_->setText(c?QString::fromStdString(c->name):QString{});}
        table_->setHorizontalHeaderLabels({"#",{},tr("Role"),tr("Skica"),tr("Normal side"),{}});table_->setRowCount(0);table_->setRowCount(c?2:0);
        if(c)for(int row=0;row<2;++row){const auto id=row==0?c->contact_sketch_id:c->upper_sketch_id;const bool available=sources_.contains(id);
            table_->setItem(row,0,new QTableWidgetItem(QString::number(row+1)));table_->setItem(row,2,new QTableWidgetItem(row==0?tr("Contact"):tr("Jaw top")));
            if(id.empty()){auto* indicator=ui::build_reference_row_indicator([this,row]{arm(row);});ui::set_reference_row_populated(indicator,false);table_->setCellWidget(row,1,indicator);}
            auto* field=new ui::ReferenceCellItem;table_->setItem(row,3,field);if(id.empty()){field->clear_reference();field->setText(tr("Vyberte…"));}
            else{field->set_reference(QString::fromStdString(id));field->setText(available?sources_.at(id):QString::fromStdString(id));field->set_missing(!available);}
            field->set_active_input(row==active_);field->set_inspected(inspected_.contains(row));
            auto* side=new QComboBox(table_);side->setObjectName(QString("camClampingSide%1").arg(row));side->addItems({tr("Positive normal"),tr("Negative normal")});side->setCurrentIndex((row==0?c->outward_positive:c->upper_positive)?0:1);
            connect(side,&QComboBox::currentIndexChanged,this,[this,row](int index){if(auto* c=current()){(row==0?c->outward_positive:c->upper_positive)=index==0;clear_error();notify();}});table_->setCellWidget(row,4,side);
            table_->setCellWidget(row,5,ui::centered_cell_widget(ui::build_reference_inspection_button(available,inspected_.contains(row),[this,row](bool on){if(on)inspected_.insert(row);else inspected_.erase(row);refresh();notify();})));
        }
        table_->setFixedHeight(64+table_->horizontalHeader()->height()+4);if(!error_.empty())status_->setText(tr(error_.c_str()));
    }
};
} // namespace zima::app
