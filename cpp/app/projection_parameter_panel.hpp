#pragma once
#include <zima/document/projection_parameters.hpp>
#include <zima/ui/reference_cell.hpp>
#include <zima/ui/unit_spin_box.hpp>
#include <QSignalBlocker>
#include <QComboBox>
#include <QFormLayout>
#include <QHeaderView>
#include <QLabel>
#include <QPushButton>
#include <QTableWidget>
#include <QToolButton>
#include <QVBoxLayout>
#include <QEvent>
#include <algorithm>
#include <functional>
#include <set>
#include <tuple>

namespace zima::app {
// Embedded by the ordinary Feature properties panel. Placement, Sketch editing,
// confirmation and transactions remain owned by the shared host dialog.
class ProjectionParameterPanel final:public QWidget {
public:
    enum class Pick { None, Target, Region };
    std::function<void(Pick)> pick_requested;
    std::function<void()> changed,inspection_changed,layout_changed;
    std::function<QString(const kernel::FaceReference&)> face_label;
    ProjectionParameterPanel(document::ProjectionParameters initial,QWidget* parent)
        :QWidget(parent),pending_(std::move(initial)) {
        setObjectName("projectionParameters");
        auto* layout=new QVBoxLayout(this);layout->setContentsMargins(0,0,0,0);
        auto* form=new QFormLayout;form_=form;layout->addLayout(form);
        output_=new QComboBox(this);output_->setObjectName("projectionOutput");
        output_->addItems({tr("Skica"),tr("Rozdělit"),tr("Plocha")});
        output_->setCurrentIndex(static_cast<int>(pending_.output));form->addRow(tr("Výsledek"),output_);
        targets_=table("projectionTargets",tr("Cílové plochy"));layout->addWidget(targets_,1);
        regions_=table("projectionRegions",tr("Zachované oblasti"));layout->addWidget(regions_,1);
        details_=new QFormLayout;layout->addLayout(details_);
        relief_direction_=new QComboBox(this);relief_direction_->setObjectName("projectionReliefDirection");
        relief_direction_->addItems({tr("Along projection direction"),tr("Normal to target surface")});
        relief_direction_->setCurrentIndex(static_cast<int>(pending_.relief_direction));
        details_->addRow(tr("Relief direction"),relief_direction_);
        connect(relief_direction_,&QComboBox::currentIndexChanged,this,[this](int value) {
            pending_.relief_direction=static_cast<document::ProjectionReliefDirection>(value);notify();
        });
        height_=new ui::UnitDoubleSpinBox(ui::InputQuantity::Length,this);height_->setObjectName("projectionHeight");
        height_->setRange(-1000000,1000000);height_->setValue(pending_.height);
        displayed_height_=height_->value();initial_height_=pending_.height;
        details_->addRow(tr("Korekce"),height_);
        height_->setToolTip(tr("Positive correction adds material outward; negative correction removes material inward."));
        connect(height_,&QDoubleSpinBox::valueChanged,this,[this](double value){
            pending_.height=std::abs(value)==std::abs(displayed_height_)?initial_height_:std::abs(value);notify();});
        auto* notice=new QLabel(tr("Promítá se kolmo ke skicové rovině na nejbližší vybranou plochu. Pořadí ploch výsledek nemění."),this);notice_=notice;
        notice->setObjectName("projectionNotice");
        notice->setWordWrap(true);layout->addWidget(notice);
        connect(output_,&QComboBox::currentIndexChanged,this,[this](int value){pending_.output=static_cast<document::ProjectionOutput>(value);finish_entry();refresh();notify();});
        for(const auto& [view,kind]:{std::pair{targets_,Pick::Target},std::pair{regions_,Pick::Region}})
            connect(view,&QTableWidget::cellClicked,this,[this,kind](int row,int column){if(column==2)begin_entry(kind,row);});
        refresh();
    }
    const document::ProjectionParameters& parameters()const{return pending_;}
    void set_relief_mode(bool relief) {
        if(relief) {
            if(pending_.output!=document::ProjectionOutput::Relief) {
                pending_.relief_direction=document::ProjectionReliefDirection::Projection;
                const QSignalBlocker blocker(relief_direction_);relief_direction_->setCurrentIndex(0);
            }
            pending_.output=document::ProjectionOutput::Relief;
        }
        else if(pending_.output==document::ProjectionOutput::Relief)pending_.output=document::ProjectionOutput::Curves;
        {const QSignalBlocker blocker(output_);output_->setCurrentIndex(relief?0:int(pending_.output));}
        refresh();
    }
    bool subtract()const{return pending_.output==document::ProjectionOutput::Relief&&height_->value()<0;}
    void set_subtract(bool on){const QSignalBlocker blocker(height_);height_->setValue((on?-1.:1.)*pending_.height);displayed_height_=height_->value();}
    Pick active_pick()const{return active_;}
    void begin_entry(Pick kind,int row) {active_=kind;row_=row;refresh();if(pick_requested)pick_requested(kind);}
    void finish_entry(bool clear_inspection=false) {
        active_=Pick::None;row_=-1;
        if(clear_inspection)inspected_faces_.clear();
        refresh();
    }
    bool set_target(document::ExtrusionParameters::EndTarget target) {
        if(active_!=Pick::Target||target.kind!=document::EndTargetKind::Face)return false;
        const auto duplicate=std::ranges::find_if(pending_.targets,[&](const auto& value){return value.reference==target.reference;});
        if(duplicate!=pending_.targets.end()&&duplicate-pending_.targets.begin()!=row_)return false;
        if(duplicate!=pending_.targets.end()) {
            row_=static_cast<int>(pending_.targets.size());refresh();return true;
        }
        if(row_>=0&&row_<static_cast<int>(pending_.targets.size())) {
            const auto old=pending_.targets[row_].reference;inspected_faces_.erase(key(old));
            std::erase_if(pending_.regions,[&](const auto& region){return region.target==old;});
            pending_.targets[row_]=std::move(target);
        }else pending_.targets.push_back(std::move(target));
        row_=static_cast<int>(pending_.targets.size());refresh();notify();return true;
    }
    bool set_region(kernel::FaceReference target,kernel::Vec3 local_seed) {
        if(active_!=Pick::Region||std::ranges::none_of(pending_.targets,[&](const auto& value){return value.reference==target;}))return false;
        document::ProjectionRegion region{std::move(target),local_seed,{}};
        if(row_>=0&&row_<static_cast<int>(pending_.regions.size()))pending_.regions[row_]=std::move(region);
        else pending_.regions.push_back(std::move(region));
        active_=Pick::None;row_=-1;refresh();notify();return true;
    }
    std::vector<kernel::FaceReference> inspected_faces()const {
        std::vector<kernel::FaceReference> result;
        for(const auto& target:pending_.targets)if(inspected_faces_.contains(key(target.reference)))result.push_back(target.reference);
        return result;
    }
    void refresh() {
        const auto previous=layout_state();
        const bool relief=pending_.output==document::ProjectionOutput::Relief;
        form_->setRowVisible(output_,!relief);details_->setRowVisible(height_,relief);
        details_->setRowVisible(relief_direction_,relief);
        regions_->setVisible(pending_.output==document::ProjectionOutput::Surface);
        fill(targets_,Pick::Target,static_cast<int>(pending_.targets.size())+1);
        fill(regions_,Pick::Region,static_cast<int>(pending_.regions.size())+1);
        if(previous!=layout_state()) {
            layout()->invalidate();updateGeometry();
            if(layout_changed)layout_changed();
        }
    }
protected:
    void changeEvent(QEvent* event)override {
        QWidget::changeEvent(event);
        if(event->type()!=QEvent::LanguageChange)return;
        output_->setItemText(0,tr("Skica"));output_->setItemText(1,tr("Rozdělit"));output_->setItemText(2,tr("Plocha"));
        static_cast<QLabel*>(form_->labelForField(output_))->setText(tr("Výsledek"));
        static_cast<QLabel*>(details_->labelForField(height_))->setText(tr("Korekce"));
        static_cast<QLabel*>(details_->labelForField(relief_direction_))->setText(tr("Relief direction"));
        relief_direction_->setItemText(0,tr("Along projection direction"));
        relief_direction_->setItemText(1,tr("Normal to target surface"));
        height_->setToolTip(tr("Positive correction adds material outward; negative correction removes material inward."));
        notice_->setText(tr("Promítá se kolmo ke skicové rovině na nejbližší vybranou plochu. Pořadí ploch výsledek nemění."));
        targets_->setHorizontalHeaderLabels({tr("Č."),{},tr("Cílové plochy"),{}});
        regions_->setHorizontalHeaderLabels({tr("Č."),{},tr("Zachované oblasti"),{}});
        refresh();
    }
private:
    // QTableWidget's default preferred height reserves a mostly empty viewport
    // for each list. Keep a compact natural form; extra dialog height can still
    // expand both lists without pushing the second list out of the initial view.
    class ReferenceTable final:public QTableWidget {
    public:
        explicit ReferenceTable(QWidget* parent):QTableWidget(0,4,parent){}
        QSize sizeHint()const override {
            auto size=QTableWidget::sizeHint();size.setHeight(minimumHeight());return size;
        }
    };
    std::tuple<bool,int,int> layout_state()const {
        return std::tuple{regions_->isHidden(),targets_->minimumHeight(),regions_->minimumHeight()};
    }
    using Key=std::tuple<std::string,std::string,std::string>;
    template<class R>static Key key(const R& r){return {r.owner_id,r.semantic_key,r.instance_path};}
    QTableWidget* table(const char* name,const QString& label) {
        auto* value=new ReferenceTable(this);value->setObjectName(name);
        value->setHorizontalHeaderLabels({tr("Č."),{},label,{}});value->verticalHeader()->hide();
        value->setSelectionMode(QAbstractItemView::NoSelection);value->setEditTriggers(QAbstractItemView::NoEditTriggers);
        ui::install_reference_cell_delegate(value);
        for(int column:{0,1,3}){value->horizontalHeader()->setSectionResizeMode(column,QHeaderView::Fixed);value->setColumnWidth(column,34);}
        value->horizontalHeader()->setSectionResizeMode(2,QHeaderView::Stretch);return value;
    }
    void fill(QTableWidget* view,Pick kind,int count) {
        view->setRowCount(0);view->setRowCount(count);
        for(int row=0;row<count;++row) {
            kernel::FaceReference ref;QString label;bool populated=false,inspected=false;
            if(kind==Pick::Target&&row<static_cast<int>(pending_.targets.size())) {
                const auto& target=pending_.targets[row];ref=target.reference;populated=true;
                label=face_label?face_label(ref):QString::fromStdString(target.label);
            }else if(kind==Pick::Region&&row<static_cast<int>(pending_.regions.size())) {
                ref=pending_.regions[row].target;populated=true;
                label=tr("Oblast %1 — %2").arg(row+1).arg(face_label?face_label(ref):QString::fromStdString(ref.owner_id));
            }
            inspected=inspected_faces_.contains(key(ref));
            if(!populated)label=kind==Pick::Target?tr("Vyberte cílovou plochu…"):
                tr("Vyberte zachovanou oblast na cílové ploše…");
            view->setRowHeight(row,34);view->setItem(row,0,new QTableWidgetItem(QString::number(row+1)));
            auto* indicator=ui::build_reference_row_indicator([this,kind,row,ref]{
                if(kind==Pick::Target&&row<static_cast<int>(pending_.targets.size())) {
                    pending_.targets.erase(pending_.targets.begin()+row);inspected_faces_.erase(key(ref));
                    std::erase_if(pending_.regions,[&](const auto& region){return region.target==ref;});
                    row_=std::min(row,static_cast<int>(pending_.targets.size()));
                }else if(kind==Pick::Region&&row<static_cast<int>(pending_.regions.size())) {
                    pending_.regions.erase(pending_.regions.begin()+row);row_=std::min(row,static_cast<int>(pending_.regions.size()));
                }
                active_=kind;refresh();notify();if(inspection_changed)inspection_changed();if(pick_requested)pick_requested(kind);
            });
            indicator->findChild<QPushButton*>()->setToolTip(kind==Pick::Target?tr("Odstranit cílovou plochu ze seznamu"):
                kind==Pick::Region?tr("Odstranit výběr oblasti"):tr("Vymazat referenci"));
            ui::set_reference_row_populated(indicator,populated);view->setCellWidget(row,1,indicator);
            auto* item=new ui::ReferenceCellItem(label);if(populated)item->set_reference(QString::fromStdString(ref.owner_id+":"+ref.semantic_key));
            item->set_active_input(active_==kind&&row_==row);item->set_inspected(inspected);view->setItem(row,2,item);
            // A region has no independently calculated geometry until OK.
            // Only its real target is inspectable here; no fake region eye.
            if(kind!=Pick::Region)view->setCellWidget(row,3,ui::centered_cell_widget(ui::build_reference_inspection_button(populated,inspected,
                [this,ref](bool on){if(on)inspected_faces_.insert(key(ref));else inspected_faces_.erase(key(ref));
                    refresh();if(inspection_changed)inspection_changed();})));
        }
        view->setMinimumHeight(view->horizontalHeader()->sizeHint().height()+2*view->frameWidth()+std::min(count,3)*34);
    }
    void notify(){if(changed)changed();}
    document::ProjectionParameters pending_;
    QComboBox *output_{},*relief_direction_{};
    QFormLayout* form_{};QLabel* notice_{};
    QFormLayout* details_{};ui::UnitDoubleSpinBox* height_{};double displayed_height_{},initial_height_{};
    QTableWidget *targets_{},*regions_{};
    Pick active_{Pick::None};int row_{-1};std::set<Key> inspected_faces_;
};
}
