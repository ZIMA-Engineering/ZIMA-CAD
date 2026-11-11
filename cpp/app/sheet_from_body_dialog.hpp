#pragma once
#include <zima/kernel/geometry_kernel.hpp>
#include <zima/ui/properties_subwindow.hpp>
#include <zima/ui/reference_cell.hpp>
#include <zima/ui/unit_spin_box.hpp>
#include <QCheckBox>
#include <QDoubleSpinBox>
#include <QFormLayout>
#include <QHeaderView>
#include <QLabel>
#include <QTableWidget>
#include <QToolButton>
#include <QVBoxLayout>
#include <functional>
#include <optional>
#include <stdexcept>
#include <vector>

namespace zima::app {
class SheetFromBodyDialog final : public ui::PropertiesSubWindow {
public:
    enum class EntryKind { None, SourceFace, IncludedFace, RipSketch };
    using Commit=std::function<void(kernel::FaceReference,double,bool,
        std::vector<kernel::FaceReference>,std::vector<std::string>)>;
    std::function<void()> changed;
    SheetFromBodyDialog(double thickness,Commit commit,QWidget* parent)
        :PropertiesSubWindow(tr("Plech z tělesa"),parent),commit_(std::move(commit)) {
        setObjectName("sheetFromBodyDialog");setAttribute(Qt::WA_DeleteOnClose);
        setProperty("originSelectionBound",true);
        if(auto* outer=qobject_cast<QVBoxLayout*>(layout()))
            outer->setStretch(outer->indexOf(content_layout()),1);
        auto* fields=new QWidget(this);
        fields->setSizePolicy(QSizePolicy::Preferred,QSizePolicy::Fixed);
        auto* form=new QFormLayout(fields);form->setContentsMargins(0,0,0,0);
        form->setFormAlignment(Qt::AlignTop);

        source_table_=make_table("sheetSourceFace",1,tr("Výchozí rovinná plocha"),65);
        source_indicator_=ui::build_reference_row_indicator([this]{
            selected_.reset();source_inspected_=false;
            arm(EntryKind::SourceFace,0);refresh();});
        source_table_->setCellWidget(0,0,source_indicator_);
        source_field_=new ui::ReferenceCellItem;source_table_->setItem(0,1,source_field_);
        source_eye_=ui::build_reference_inspection_button(false,false,
            [this](bool value){source_inspected_=value;refresh();});
        source_table_->setCellWidget(0,2,ui::centered_cell_widget(source_eye_));
        form->addRow(source_table_);

        included_table_=make_table("sheetIncludedFaces",0,tr("Zahrnuté plochy"),230);
        append_included_rows(12);form->addRow(included_table_);
        rip_table_=make_table("sheetRipSketches",0,tr("RIP skici"),125);
        append_rip_rows(3);form->addRow(rip_table_);

        thickness_=new ui::UnitDoubleSpinBox(ui::InputQuantity::Length,this);
        thickness_->setObjectName("sheetSourceThickness");
        thickness_->setRange(.001,1000000);thickness_->setValue(thickness);
        thickness_override_=new QCheckBox(tr("Nedržet se tloušťky plechu"),this);
        thickness_override_->setObjectName("sheetSourceThicknessOverride");
        thickness_->setEnabled(false);form->addRow(thickness_override_);
        form->addRow(tr("Tloušťka"),thickness_);content_layout()->addWidget(fields);
        auto* note=new QLabel(tr("The first face defines the material side. An empty included-face list converts the complete connected sheet branch. RIP consumes all ordinary geometry of each selected Sketch without removing material."),this);
        note->setWordWrap(true);note->setSizePolicy(QSizePolicy::Preferred,QSizePolicy::Fixed);
        content_layout()->addWidget(note);content_layout()->addStretch(1);
        set_initial_size({455,690});

        connect(source_table_,&QTableWidget::cellClicked,this,[this](int,int column){
            if(column==1){arm(EntryKind::SourceFace,0);refresh();}});
        connect(included_table_,&QTableWidget::cellClicked,this,[this](int row,int column){
            if(column==1){arm(EntryKind::IncludedFace,row);refresh();}});
        connect(rip_table_,&QTableWidget::cellClicked,this,[this](int row,int column){
            if(column==1){arm(EntryKind::RipSketch,row);refresh();}});
        connect(thickness_override_,&QCheckBox::toggled,this,
            [this](bool enabled){thickness_->setEnabled(enabled);});
        refresh();
    }

    [[nodiscard]] bool active()const{return entry_kind_!=EntryKind::None;}
    [[nodiscard]] EntryKind entry_kind()const{return entry_kind_;}
    [[nodiscard]] int active_row()const{return active_row_;}
    [[nodiscard]] bool face_entry()const{return entry_kind_==EntryKind::SourceFace||entry_kind_==EntryKind::IncludedFace;}
    [[nodiscard]] bool rip_entry()const{return entry_kind_==EntryKind::RipSketch;}
    [[nodiscard]] bool inspected()const{return source_inspected_;}
    [[nodiscard]] const std::optional<kernel::FaceReference>& selected()const{return selected_;}
    [[nodiscard]] std::vector<kernel::FaceReference> included_faces()const {
        std::vector<kernel::FaceReference> result;
        for(const auto& row:included_)if(row.reference)result.push_back(*row.reference);
        return result;
    }
    [[nodiscard]] std::vector<std::string> rip_sketches()const {
        std::vector<std::string> result;
        for(const auto& row:rips_)if(!row.sketch_id.empty())result.push_back(row.sketch_id);
        return result;
    }
    [[nodiscard]] std::vector<kernel::FaceReference> inspected_faces()const {
        std::vector<kernel::FaceReference> result;
        if(source_inspected_&&selected_)result.push_back(*selected_);
        for(const auto& row:included_)if(row.inspected&&row.reference)result.push_back(*row.reference);
        return result;
    }
    [[nodiscard]] std::vector<std::string> inspected_rips()const {
        std::vector<std::string> result;
        for(const auto& row:rips_)if(row.inspected&&!row.sketch_id.empty())result.push_back(row.sketch_id);
        return result;
    }
    void end_entry(){entry_kind_=EntryKind::None;active_row_=-1;refresh();}
    void set_reference(kernel::FaceReference face,double inferred_thickness) {
        selected_=std::move(face);update_thickness_hint(inferred_thickness);
        entry_kind_=EntryKind::None;active_row_=-1;refresh();
    }
    void set_face_reference(kernel::FaceReference face,double inferred_thickness) {
        if(entry_kind_==EntryKind::SourceFace){set_reference(std::move(face),inferred_thickness);return;}
        if(entry_kind_!=EntryKind::IncludedFace||active_row_<0||static_cast<std::size_t>(active_row_)>=included_.size())return;
        auto& row=included_[static_cast<std::size_t>(active_row_)];
        row.reference=std::move(face);row.inspected=false;
        if(static_cast<std::size_t>(active_row_)+1==included_.size())append_included_rows(1);
        entry_kind_=EntryKind::None;active_row_=-1;refresh();
    }
    void set_rip_reference(std::string sketch_id,QString label) {
        if(entry_kind_!=EntryKind::RipSketch||active_row_<0||static_cast<std::size_t>(active_row_)>=rips_.size())return;
        auto& row=rips_[static_cast<std::size_t>(active_row_)];
        row.sketch_id=std::move(sketch_id);row.label=std::move(label);row.inspected=false;
        if(static_cast<std::size_t>(active_row_)+1==rips_.size())append_rip_rows(1);
        entry_kind_=EntryKind::None;active_row_=-1;refresh();
    }

private:
    struct FaceRow {std::optional<kernel::FaceReference> reference;ui::ReferenceCellItem* field{};QWidget* indicator{};QToolButton* eye{};bool inspected{};};
    struct RipRow {std::string sketch_id;QString label;ui::ReferenceCellItem* field{};QWidget* indicator{};QToolButton* eye{};bool inspected{};};
    QTableWidget* make_table(const char* object_name,int rows,const QString& heading,int height) {
        auto* table=new QTableWidget(rows,3,this);table->setObjectName(object_name);
        table->setHorizontalHeaderLabels({QString{},heading,QString{}});table->verticalHeader()->show();
        table->setSelectionMode(QAbstractItemView::NoSelection);table->setEditTriggers(QAbstractItemView::NoEditTriggers);
        table->horizontalHeader()->setSectionResizeMode(1,QHeaderView::Stretch);
        table->horizontalHeader()->setSectionResizeMode(0,QHeaderView::Fixed);
        table->horizontalHeader()->setSectionResizeMode(2,QHeaderView::Fixed);
        table->setColumnWidth(0,32);table->setColumnWidth(2,32);table->setFixedHeight(height);
        ui::install_reference_cell_delegate(table);return table;
    }
    void append_included_rows(std::size_t count) {
        for(std::size_t item=0;item<count;++item) {
            const auto index=included_.size();included_table_->insertRow(static_cast<int>(index));FaceRow row;
            row.indicator=ui::build_reference_row_indicator([this,index]{
                if(index>=included_.size())return;included_[index].reference.reset();included_[index].inspected=false;
                arm(EntryKind::IncludedFace,static_cast<int>(index));refresh();});
            included_table_->setCellWidget(static_cast<int>(index),0,row.indicator);
            row.field=new ui::ReferenceCellItem;included_table_->setItem(static_cast<int>(index),1,row.field);
            row.eye=ui::build_reference_inspection_button(false,false,[this,index](bool value){
                if(index<included_.size()){included_[index].inspected=value;refresh();}});
            included_table_->setCellWidget(static_cast<int>(index),2,ui::centered_cell_widget(row.eye));
            included_.push_back(std::move(row));
        }
    }
    void append_rip_rows(std::size_t count) {
        for(std::size_t item=0;item<count;++item) {
            const auto index=rips_.size();rip_table_->insertRow(static_cast<int>(index));RipRow row;
            row.indicator=ui::build_reference_row_indicator([this,index]{
                if(index>=rips_.size())return;rips_[index].sketch_id.clear();rips_[index].label.clear();rips_[index].inspected=false;
                arm(EntryKind::RipSketch,static_cast<int>(index));refresh();});
            rip_table_->setCellWidget(static_cast<int>(index),0,row.indicator);
            row.field=new ui::ReferenceCellItem;rip_table_->setItem(static_cast<int>(index),1,row.field);
            row.eye=ui::build_reference_inspection_button(false,false,[this,index](bool value){
                if(index<rips_.size()){rips_[index].inspected=value;refresh();}});
            rip_table_->setCellWidget(static_cast<int>(index),2,ui::centered_cell_widget(row.eye));
            rips_.push_back(std::move(row));
        }
    }
    void arm(EntryKind kind,int row){entry_kind_=kind;active_row_=row;}
    void update_thickness_hint(double inferred_thickness) {
        const auto* unit=static_cast<const ui::UnitDoubleSpinBox*>(thickness_);
        thickness_->setToolTip(inferred_thickness>=.001?
            tr("Odhad tloušťky zdroje: %1. Nastavení plechu zůstává zachováno.").arg(
                thickness_->locale().toString(inferred_thickness/unit->native_per_unit(),'f',unit->display_decimals())+unit->suffix()):QString{});
    }
    bool submit()override {
        if(!selected_)throw std::invalid_argument(tr("Select a calculated planar source face.").toStdString());
        commit_(*selected_,thickness_->value(),thickness_override_->isChecked(),included_faces(),rip_sketches());return true;
    }
    void refresh() {
        if(!selected_)thickness_->setToolTip({});
        source_field_->setText(selected_?tr("Výchozí rovinná plocha"):tr("Vyberte rovinnou plochu ve View."));
        if(selected_)source_field_->set_reference(QString::fromStdString(selected_->semantic_key));else source_field_->clear_reference();
        source_field_->set_active_input(entry_kind_==EntryKind::SourceFace);source_field_->set_inspected(source_inspected_);
        ui::set_reference_row_populated(source_indicator_,selected_.has_value());source_eye_->setEnabled(selected_.has_value());source_eye_->setChecked(source_inspected_);
        for(std::size_t index=0;index<included_.size();++index) {
            auto& row=included_[index];row.field->setText(row.reference?tr("Zahrnutá plocha"):tr("Vyberte plochu ve View."));
            if(row.reference)row.field->set_reference(QString::fromStdString(row.reference->semantic_key));else row.field->clear_reference();
            row.field->set_active_input(entry_kind_==EntryKind::IncludedFace&&active_row_==static_cast<int>(index));row.field->set_inspected(row.inspected);
            ui::set_reference_row_populated(row.indicator,row.reference.has_value());row.eye->setEnabled(row.reference.has_value());row.eye->setChecked(row.inspected);
        }
        for(std::size_t index=0;index<rips_.size();++index) {
            auto& row=rips_[index];row.field->setText(row.sketch_id.empty()?tr("Vyberte RIP skicu ve View."):row.label);
            if(row.sketch_id.empty())row.field->clear_reference();else row.field->set_reference(QString::fromStdString(row.sketch_id));
            row.field->set_active_input(entry_kind_==EntryKind::RipSketch&&active_row_==static_cast<int>(index));row.field->set_inspected(row.inspected);
            ui::set_reference_row_populated(row.indicator,!row.sketch_id.empty());row.eye->setEnabled(!row.sketch_id.empty());row.eye->setChecked(row.inspected);
        }
        source_table_->viewport()->update();included_table_->viewport()->update();rip_table_->viewport()->update();if(changed)changed();
    }
    Commit commit_;std::optional<kernel::FaceReference> selected_;std::vector<FaceRow> included_;std::vector<RipRow> rips_;
    QTableWidget* source_table_{};QTableWidget* included_table_{};QTableWidget* rip_table_{};
    ui::ReferenceCellItem* source_field_{};QWidget* source_indicator_{};QToolButton* source_eye_{};
    QDoubleSpinBox* thickness_{};QCheckBox* thickness_override_{};
    EntryKind entry_kind_{EntryKind::SourceFace};int active_row_{};bool source_inspected_{};
};
}
