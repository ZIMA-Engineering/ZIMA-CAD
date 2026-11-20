#pragma once
#include <zima/sketcher/curve_fillet.hpp>
#include <zima/ui/properties_subwindow.hpp>
#include <zima/ui/reference_cell.hpp>
#include <zima/ui/unit_spin_box.hpp>
#include <QDialogButtonBox>
#include <QFormLayout>
#include <QHeaderView>
#include <QPushButton>
#include <QTableWidget>
#include <QToolButton>
#include <QVBoxLayout>
#include <array>
#include <optional>

namespace zima::app {
class SketchCurveFilletDialog final : public ui::PropertiesSubWindow {
public:
    using Commit=std::function<void(const std::array<std::string,2>&,double)>;
    std::function<void()> changed;
    const sketcher::Sketch original;
    std::optional<sketcher::Sketch> preview;
    std::array<std::string,2> curves;
    std::array<bool,2> inspected{};
    SketchCurveFilletDialog(sketcher::Sketch sketch,std::array<std::string,2> selected,
            Commit commit,QWidget* parent)
        :PropertiesSubWindow(tr("Zaoblení"),parent),original(std::move(sketch)),
         curves(std::move(selected)),commit_(std::move(commit)) {
        setObjectName("sketchCurveFilletDialog");setAttribute(Qt::WA_DeleteOnClose);
        auto* fields=new QWidget(this);auto* form=new QFormLayout(fields);
        form->setContentsMargins(0,0,0,0);form->setFormAlignment(Qt::AlignTop);
        radius_=new ui::UnitDoubleSpinBox(ui::InputQuantity::Length,this);
        radius_->setObjectName("sketchFilletRadius");radius_->setRange(.000001,1000000);radius_->setValue(1);
        form->addRow(tr("Poloměr R"),radius_);
        table_=new QTableWidget(2,3,this);table_->setObjectName("sketchFilletCurves");
        table_->setHorizontalHeaderLabels({QString{},tr("Křivky"),QString{}});
        table_->setSelectionMode(QAbstractItemView::NoSelection);table_->setEditTriggers(QAbstractItemView::NoEditTriggers);
        table_->horizontalHeader()->setSectionResizeMode(1,QHeaderView::Stretch);
        for(int column:{0,2}) {table_->horizontalHeader()->setSectionResizeMode(column,QHeaderView::Fixed);table_->setColumnWidth(column,32);}
        table_->setFixedHeight(100);ui::install_reference_cell_delegate(table_);
        for(int i=0;i<2;++i) {
            indicators_[i]=ui::build_reference_row_indicator([this,i]{curves[i].clear();inspected[i]=false;active_=i;refresh();});
            table_->setCellWidget(i,0,indicators_[i]);
            items_[i]=new ui::ReferenceCellItem;table_->setItem(i,1,items_[i]);
            eyes_[i]=ui::build_reference_inspection_button(false,false,[this,i](bool value){inspected[i]=value;refresh(false);});
            table_->setCellWidget(i,2,ui::centered_cell_widget(eyes_[i]));
        }
        form->addRow(table_);content_layout()->addWidget(fields);content_layout()->addStretch(1);
        connect(table_,&QTableWidget::cellClicked,this,[this](int row,int column){if(column==1){active_=row;refresh(false);}});
        connect(radius_,&QDoubleSpinBox::valueChanged,this,[this]{refresh();});
        active_=curves[0].empty()?0:curves[1].empty()?1:-1;
        set_initial_size({430,235});refresh();
    }
    [[nodiscard]] bool accepts(const std::string& id)const {
        return active_>=0&&!id.empty()&&id!=curves[1-active_]&&sketcher::fillet_curve_endpoints(original,id).has_value();
    }
    [[nodiscard]] bool active()const{return active_>=0;}
    void accept(const std::string& id) {
        if(!accepts(id))return;curves[active_]=id;inspected[active_]=false;
        active_=curves[0].empty()?0:curves[1].empty()?1:-1;refresh();
    }
    void end_entry(){active_=-1;inspected={};refresh(false);}
    void refresh(bool calculate=true) {
        if(calculate) {
            preview.reset();show_submission_error({});
            if(!curves[0].empty()&&!curves[1].empty())try {
                auto candidate=original;
                static_cast<void>(sketcher::fillet_sketch_curves(candidate,curves[0],curves[1],radius_->value()));
                preview=std::move(candidate);
            }catch(const std::exception& error){show_submission_error(tr(error.what()));}
        }
        for(int i=0;i<2;++i) {
            items_[i]->setText(curves[i].empty()?tr("Vyberte křivku ve View."):tr("Křivka %1").arg(i+1));
            if(curves[i].empty())items_[i]->clear_reference();else items_[i]->set_reference(QString::fromStdString(curves[i]));
            items_[i]->set_active_input(active_==i);items_[i]->set_inspected(inspected[i]);
            ui::set_reference_row_populated(indicators_[i],!curves[i].empty());
            eyes_[i]->setEnabled(!curves[i].empty());eyes_[i]->setChecked(inspected[i]);
        }
        buttons()->button(QDialogButtonBox::Ok)->setEnabled(preview.has_value());
        table_->viewport()->update();if(changed)changed();
    }
private:
    bool submit()override {if(!preview)return false;commit_(curves,radius_->value());return true;}
    Commit commit_;int active_{};
    ui::UnitDoubleSpinBox* radius_{};QTableWidget* table_{};
    std::array<ui::ReferenceCellItem*,2> items_{};
    std::array<QWidget*,2> indicators_{};std::array<QToolButton*,2> eyes_{};
};
}
