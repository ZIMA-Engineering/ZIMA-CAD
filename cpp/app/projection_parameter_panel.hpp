#pragma once
#include <zima/document/projection_parameters.hpp>
#include <zima/ui/reference_cell.hpp>
#include <QComboBox>
#include <QCheckBox>
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
    enum class Pick { None, Target, Direction, Region };
    std::function<void(Pick)> pick_requested;
    std::function<void()> changed,inspection_changed,layout_changed;
    std::function<QString(const kernel::FaceReference&)> face_label;
    std::function<QString(const kernel::EdgeReference&)> direction_label;
    ProjectionParameterPanel(document::ProjectionParameters initial,QWidget* parent)
        :QWidget(parent),pending_(std::move(initial)) {
        setObjectName("projectionParameters");
        auto* layout=new QVBoxLayout(this);layout->setContentsMargins(0,0,0,0);
        auto* form=new QFormLayout;form_=form;layout->addLayout(form);
        output_=new QComboBox(this);output_->setObjectName("projectionOutput");
        output_->addItems({tr("Skica"),tr("Rozdělit"),tr("Plocha")});
        output_->setCurrentIndex(static_cast<int>(pending_.output));form->addRow(tr("Výsledek"),output_);
        direction_=new QComboBox(this);direction_->setObjectName("projectionDirection");
        direction_->addItems({tr("Normála skici"),tr("Reference")});
        direction_->setCurrentIndex(static_cast<int>(pending_.direction));form->addRow(tr("Směr promítnutí"),direction_);
        reverse_=new QCheckBox(tr("Obrátit směr"),this);reverse_->setObjectName("projectionReverse");
        reverse_->setChecked(pending_.reverse);form->addRow(reverse_);
        direction_table_=table("projectionDirectionReference",tr("Osa nebo přímá hrana"));layout->addWidget(direction_table_);
        targets_=table("projectionTargets",tr("Cílové plochy"));layout->addWidget(targets_,1);
        regions_=table("projectionRegions",tr("Zachované oblasti"));layout->addWidget(regions_,1);
        auto* notice=new QLabel(tr("Promítá se na první z vybraných ploch ve zvoleném směru. Pořadí ploch výsledek nemění."),this);notice_=notice;
        notice->setWordWrap(true);layout->addWidget(notice);
        connect(output_,&QComboBox::currentIndexChanged,this,[this](int value){pending_.output=static_cast<document::ProjectionOutput>(value);finish_entry();refresh();notify();});
        connect(direction_,&QComboBox::currentIndexChanged,this,[this](int value){pending_.direction=static_cast<document::ProjectionDirection>(value);finish_entry();refresh();notify();});
        connect(reverse_,&QCheckBox::toggled,this,[this](bool value){pending_.reverse=value;notify();});
        for(const auto& [view,kind]:{std::pair{targets_,Pick::Target},std::pair{direction_table_,Pick::Direction},std::pair{regions_,Pick::Region}})
            connect(view,&QTableWidget::cellClicked,this,[this,kind](int row,int column){if(column==2)begin_entry(kind,row);});
        refresh();
    }
    const document::ProjectionParameters& parameters()const{return pending_;}
    Pick active_pick()const{return active_;}
    void begin_entry(Pick kind,int row) {active_=kind;row_=row;refresh();if(pick_requested)pick_requested(kind);}
    void finish_entry(bool clear_inspection=false) {
        active_=Pick::None;row_=-1;
        if(clear_inspection){inspected_faces_.clear();inspect_direction_=false;}
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
    void set_direction(kernel::EdgeReference reference) {
        pending_.direction_reference=std::move(reference);active_=Pick::None;row_=-1;inspect_direction_=false;refresh();notify();
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
    bool direction_inspected()const{return inspect_direction_;}
    void refresh() {
        const auto previous=layout_state();
        direction_table_->setVisible(pending_.direction==document::ProjectionDirection::Reference);
        regions_->setVisible(pending_.output==document::ProjectionOutput::Surface);
        fill(targets_,Pick::Target,static_cast<int>(pending_.targets.size())+1);
        fill(direction_table_,Pick::Direction,1);
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
        direction_->setItemText(0,tr("Normála skici"));direction_->setItemText(1,tr("Reference"));
        static_cast<QLabel*>(form_->labelForField(output_))->setText(tr("Výsledek"));
        static_cast<QLabel*>(form_->labelForField(direction_))->setText(tr("Směr promítnutí"));
        reverse_->setText(tr("Obrátit směr"));
        notice_->setText(tr("Promítá se na první z vybraných ploch ve zvoleném směru. Pořadí ploch výsledek nemění."));
        direction_table_->setHorizontalHeaderLabels({tr("Č."),{},tr("Osa nebo přímá hrana"),{}});
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
    std::tuple<bool,bool,int,int,int> layout_state()const {
        return std::tuple{direction_table_->isHidden(),regions_->isHidden(),
            direction_table_->minimumHeight(),targets_->minimumHeight(),regions_->minimumHeight()};
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
            if(kind==Pick::Direction) {
                const auto& direction=pending_.direction_reference;
                ref={direction.owner_id,direction.semantic_key,direction.instance_path};populated=ref.valid();inspected=inspect_direction_;
                label=direction_label?direction_label(direction):QString::fromStdString(direction.owner_id);
            }else if(kind==Pick::Target&&row<static_cast<int>(pending_.targets.size())) {
                const auto& target=pending_.targets[row];ref=target.reference;populated=true;
                label=face_label?face_label(ref):QString::fromStdString(target.label);
            }else if(kind==Pick::Region&&row<static_cast<int>(pending_.regions.size())) {
                ref=pending_.regions[row].target;populated=true;
                label=tr("Oblast %1 — %2").arg(row+1).arg(face_label?face_label(ref):QString::fromStdString(ref.owner_id));
            }
            if(kind!=Pick::Direction)inspected=inspected_faces_.contains(key(ref));
            if(!populated)label=kind==Pick::Target?tr("Vyberte cílovou plochu…"):
                kind==Pick::Direction?tr("Vyberte osu nebo přímou hranu…"):tr("Vyberte zachovanou oblast na cílové ploše…");
            view->setRowHeight(row,34);view->setItem(row,0,new QTableWidgetItem(QString::number(row+1)));
            auto* indicator=ui::build_reference_row_indicator([this,kind,row,ref]{
                if(kind==Pick::Target&&row<static_cast<int>(pending_.targets.size())) {
                    pending_.targets.erase(pending_.targets.begin()+row);inspected_faces_.erase(key(ref));
                    std::erase_if(pending_.regions,[&](const auto& region){return region.target==ref;});
                    row_=std::min(row,static_cast<int>(pending_.targets.size()));
                }else if(kind==Pick::Region&&row<static_cast<int>(pending_.regions.size())) {
                    pending_.regions.erase(pending_.regions.begin()+row);row_=std::min(row,static_cast<int>(pending_.regions.size()));
                }else if(kind==Pick::Direction){pending_.direction_reference={};inspect_direction_=false;row_=0;}
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
                [this,kind,ref](bool on){if(kind==Pick::Direction)inspect_direction_=on;else if(on)inspected_faces_.insert(key(ref));else inspected_faces_.erase(key(ref));
                    refresh();if(inspection_changed)inspection_changed();})));
        }
        view->setMinimumHeight(view->horizontalHeader()->sizeHint().height()+2*view->frameWidth()+std::min(count,3)*34);
        if(kind==Pick::Direction)view->setMaximumHeight(view->minimumHeight());
    }
    void notify(){if(changed)changed();}
    document::ProjectionParameters pending_;
    QComboBox *output_{},*direction_{};QCheckBox* reverse_{};
    QFormLayout* form_{};QLabel* notice_{};
    QTableWidget *targets_{},*direction_table_{},*regions_{};
    Pick active_{Pick::None};int row_{-1};std::set<Key> inspected_faces_;bool inspect_direction_{};
};
}
