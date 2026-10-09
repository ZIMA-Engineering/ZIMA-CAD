#pragma once
#include <zima/document/placement_types.hpp>
#include <zima/ui/reference_cell.hpp>
#include <zima/ui/unit_spin_box.hpp>
#include <QHeaderView>
#include <QLabel>
#include <QSignalBlocker>
#include <QTableWidget>
#include <QToolButton>
#include <QVBoxLayout>
#include <array>
#include <cmath>
#include <optional>

namespace zima::app {
// A feature-specific form assembled from the common reference-entry controls.
// The native reference list remains the single source for preview and commit.
class SheetFormPlacementSection final {
public:
    using Reference=zima::document::ConstructionReference;
    std::function<void(std::size_t)> request;
    std::function<void()> changed,highlights_changed;
    SheetFormPlacementSection(QWidget* parent,QVBoxLayout* layout,const std::vector<Reference>& initial,
            std::function<QString(const std::string&)> readable_kind):readable_kind_(std::move(readable_kind)) {
        auto* heading=new QLabel(QObject::tr("Form placement"),parent);
        auto font=heading->font();font.setBold(true);heading->setFont(font);layout->addWidget(heading);
        table_=new QTableWidget(3,4,parent);table_->setObjectName("primitiveReferenceTable");
        table_->setHorizontalHeaderLabels({{},QObject::tr("Reference"),QObject::tr("Odsazení"),{}});
        table_->setVerticalHeaderLabels({QStringLiteral("1"),QStringLiteral("2"),QStringLiteral("3")});
        table_->verticalHeader()->setDefaultSectionSize(34);table_->verticalHeader()->setMinimumSectionSize(34);
        table_->horizontalHeader()->setSectionResizeMode(1,QHeaderView::Stretch);
        for(int c:{0,2,3})table_->horizontalHeader()->setSectionResizeMode(c,QHeaderView::ResizeToContents);
        zima::ui::install_reference_cell_delegate(table_);
        table_->setFixedHeight(table_->horizontalHeader()->sizeHint().height()+102+2*table_->frameWidth());
        layout->addWidget(table_);
        for(std::size_t i=0;i<3;++i) {
            indicators_[i]=zima::ui::build_reference_row_indicator([this,i] {
                rows_[i]={};labels_[i]={};inspected_[i]=false;active_=i;refresh();
                if(highlights_changed)highlights_changed();if(changed)changed();if(request)request(i);
            });
            table_->setCellWidget(int(i),0,zima::ui::centered_cell_widget(indicators_[i]));
            items_[i]=new zima::ui::ReferenceCellItem;table_->setItem(int(i),1,items_[i]);
            const auto role=i==0?QObject::tr("Sheet face"):i==1?QObject::tr("Position 1"):QObject::tr("Position 2");
            items_[i]->setToolTip(role);
            {
                offsets_[i]=new zima::ui::UnitDoubleSpinBox(zima::ui::InputQuantity::Length,parent);
                offsets_[i]->setObjectName(i==0?"sheetFormOffset0":i==1?"sheetFormOffset1":"sheetFormOffset2");
                offsets_[i]->setRange(i==0?0.:-1e6,i==0?0.:1e6);
                offsets_[i]->set_display_decimals(zima::ui::numeric_decimal_places(parent,3));
                if(i>0)offsets_[i]->setToolTip(QObject::tr("Signed distance to a line or plane. A point sets sheet-plane X in row 2 or Z in row 3."));
                table_->setCellWidget(int(i),2,offsets_[i]);
                QObject::connect(offsets_[i],&QDoubleSpinBox::valueChanged,parent,[this,i](double v){rows_[i].offset=v;if(changed)changed();});
            }
            eyes_[i]=zima::ui::build_reference_inspection_button(false,false,[this,i](bool value){set_inspected(i,value);if(highlights_changed)highlights_changed();});
            table_->setCellWidget(int(i),3,zima::ui::centered_cell_widget(eyes_[i]));
        }
        QObject::connect(table_,&QTableWidget::cellClicked,parent,[this](int row,int column){if(column==1&&request)request(std::size_t(row));});
        initialize(initial);
    }
    void initialize(const std::vector<Reference>& refs) {
        rows_={};labels_={};std::size_t i=0;for(const auto& r:refs)if(!r.orientation_only&&i<3) {
            rows_[i]=r;labels_[i]=readable_kind_(r.semantic_key);++i;
        }
        inspected_={};refresh();
    }
    void set_reference(std::size_t index,Reference reference,const QString& label) {
        if(index>=3)return;reference.orientation_only=false;reference.orientation_drives_rotation=false;
        reference.orientation_role="none";reference.supports_offset=true;
        reference.offset=index==0?0.:rows_[index].offset;
        reference.measured_offset.reset();reference.picked_position.reset();
        rows_[index]=std::move(reference);labels_[index]=label;inspected_[index]=false;refresh();
    }
    [[nodiscard]] std::vector<Reference> references() const {
        if(rows_[0].owner_id.empty())return {};
        auto result=std::vector<Reference>(rows_.begin(),rows_.end());auto front=rows_[0];
        front.offset=0;front.orientation_only=true;front.orientation_drives_rotation=true;front.orientation_role="front";
        result.push_back(std::move(front));return result;
    }
    [[nodiscard]] const Reference& position_reference(std::size_t index) const {return rows_.at(index);}
    bool set_reference_offset(std::size_t populated_index,double value) {
        if(!std::isfinite(value))return false;
        for(std::size_t i=0;i<rows_.size();++i) {
            if(rows_[i].owner_id.empty()&&rows_[i].semantic_key.empty())continue;
            if(populated_index--!=0)continue;
            auto* field=offsets_[i];
            if(!field||!field->isEnabled()||field->isReadOnly()||rows_[i].offset_locked||
               !rows_[i].supports_offset||value<field->minimum()||value>field->maximum())return false;
            field->setValue(value);return true;
        }
        return false;
    }
    [[nodiscard]] std::size_t first_empty() const {for(std::size_t i=0;i<3;++i)if(rows_[i].owner_id.empty())return i;return 3;}
    void set_active(std::optional<std::size_t> index) {active_=index;refresh();}
    void set_corner(bool corner) {corner_=corner;if(corner_)rows_[1].offset=0.;refresh();}
    void set_inspected(std::size_t index,bool value) {if(index<3){inspected_[index]=value;refresh();}}
    void clear_highlights() {inspected_={};refresh();}
    [[nodiscard]] std::vector<Reference> highlighted() const {
        std::vector<Reference> result;for(std::size_t i=0;i<3;++i)if(inspected_[i]&&!rows_[i].owner_id.empty())result.push_back(rows_[i]);return result;
    }
private:
    void refresh() {
        for(std::size_t i=0;i<3;++i) {
            const bool populated=!rows_[i].owner_id.empty();
            items_[i]->setText(populated?(labels_[i].isEmpty()?QString::fromStdString(rows_[i].semantic_key):labels_[i]):QObject::tr("Zadejte referenci"));
            if(populated)items_[i]->set_reference(QString::fromStdString(rows_[i].semantic_key));
            else items_[i]->clear_reference();
            items_[i]->set_active_input(active_&&*active_==i);items_[i]->set_inspected(inspected_[i]);
            zima::ui::set_reference_row_populated(indicators_[i],populated);
            eyes_[i]->setEnabled(populated);{const QSignalBlocker guard(eyes_[i]);eyes_[i]->setChecked(inspected_[i]);}
            if(offsets_[i]){const QSignalBlocker guard(offsets_[i]);offsets_[i]->setValue(i==0?0.:rows_[i].offset);offsets_[i]->setEnabled(i!=0&&populated&&(!corner_||i!=1));}
            if(i==0)items_[i]->setToolTip(corner_?QObject::tr("First outer sheet face"):QObject::tr("Sheet face"));
            if(i==1)items_[i]->setToolTip(corner_?QObject::tr("Second outer sheet face"):QObject::tr("Position 1"));
            if(i==2)items_[i]->setToolTip(corner_?QObject::tr("Position along bend"):QObject::tr("Position 2"));
            if(i==2&&offsets_[i])offsets_[i]->setToolTip(corner_?
                QObject::tr("Signed distance along the bend axis. Select a point or a transverse line or plane."):
                QObject::tr("Signed distance to a line or plane. A point sets sheet-plane X in row 2 or Z in row 3."));
        }
    }
    QTableWidget* table_{};
    std::function<QString(const std::string&)> readable_kind_;
    std::array<Reference,3> rows_;
    std::array<QString,3> labels_;
    std::array<QWidget*,3> indicators_{};
    std::array<zima::ui::ReferenceCellItem*,3> items_{};
    std::array<zima::ui::UnitDoubleSpinBox*,3> offsets_{};
    std::array<QToolButton*,3> eyes_{};
    std::array<bool,3> inspected_{};
    std::optional<std::size_t> active_;
    bool corner_{};
};
}
