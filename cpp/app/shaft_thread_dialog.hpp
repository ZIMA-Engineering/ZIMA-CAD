#pragma once
#include "thread_catalog.hpp"
#include <zima/document/part_document.hpp>
#include <zima/ui/properties_subwindow.hpp>
#include <zima/ui/reference_cell.hpp>
#include <QCheckBox>
#include <QComboBox>
#include <QDoubleSpinBox>
#include <QFormLayout>
#include <QHeaderView>
#include <QLineEdit>
#include <QSignalBlocker>
#include <QTableWidget>
#include <QToolButton>
#include <QVBoxLayout>
#include <array>
namespace zima::app {
class ShaftThreadDialog final : public ui::PropertiesSubWindow {
public:
    using Container=document::HistoryContainer;
    std::function<void()> changed;
    ShaftThreadDialog(Container initial,std::function<void(Container)> commit,QWidget* parent)
        : PropertiesSubWindow(tr("Vlastnosti závitu"),parent),pending_(std::move(initial)),commit_(std::move(commit)) {
        setObjectName("shaftThreadDialog");setAttribute(Qt::WA_DeleteOnClose);
        setMinimumWidth(340);
        auto* form=new QFormLayout;
        name_=new QLineEdit(QString::fromStdString(pending_.name),this);
        form->addRow(tr("Název"),name_);
        refs_=new QTableWidget(4,3,this);refs_->setObjectName("shaftThreadReferences");
        refs_->setHorizontalHeaderLabels({tr("Reference"),tr("Plocha"),QString()});
        refs_->verticalHeader()->hide();refs_->setSelectionMode(QAbstractItemView::NoSelection);
        refs_->setEditTriggers(QAbstractItemView::NoEditTriggers);
        refs_->horizontalHeader()->setSectionResizeMode(0,QHeaderView::ResizeToContents);
        refs_->horizontalHeader()->setSectionResizeMode(1,QHeaderView::Stretch);
        refs_->setColumnWidth(2,32);refs_->setFixedHeight(152);
        ui::install_reference_cell_delegate(refs_);
        const std::array<QString,4> labels{tr("Válcová plocha"),tr("Počáteční plocha"),tr("Vstupní sražení"),tr("Až k")};
        for (int row=0;row<4;++row) {
            refs_->setItem(row,0,new QTableWidgetItem(labels[row]));
            fields_[row]=new ui::ReferenceCellItem;refs_->setItem(row,1,fields_[row]);
            eyes_[row]=ui::build_reference_inspection_button(false,false,[this,row](bool checked) {
                inspected_[row]=checked;refresh_fields();notify();
            });
            refs_->setCellWidget(row,2,ui::centered_cell_widget(eyes_[row]));
        }
        connect(refs_,&QTableWidget::cellClicked,this,[this](int row,int column) {
            if (column!=1) return;
            if (row==2) {
                const QSignalBlocker blocker(chamfer_);
                chamfer_->setChecked(true);
            }
            active_=row;refresh_fields();notify();
        });
        form->addRow(refs_);
        chamfer_=new QCheckBox(tr("Použít vstupní sražení"),this);
        chamfer_->setObjectName("shaftThreadChamfer");
        chamfer_->setChecked(pending_.shaft_thread.chamfer.has_value());form->addRow(chamfer_);
        standard_=new QComboBox(this);standard_->setObjectName("shaftThreadStandard");
        standard_->addItem(tr("Metrický závit ISO"),"metric");
        standard_->addItem(tr("Whitworth BSW"),"whitworth");
        standard_->addItem(tr("Trubkový G (BSPP)"),"pipe");
        standard_->setCurrentIndex(static_cast<int>(pending_.shaft_thread.standard));
        size_=new QComboBox(this);size_->setObjectName("shaftThreadSize");
        form->addRow(tr("Typ závitu"),standard_);form->addRow(tr("Rozměr závitu"),size_);
        root_=number(pending_.shaft_thread.root_diameter,"shaftThreadRootDiameter");
        length_=number(pending_.shaft_thread.length,"shaftThreadLength");
        factor_=number(pending_.shaft_thread.runout_pitch_factor,"shaftThreadRunoutFactor");
        factor_->setMinimum(0);factor_->setSuffix(tr(" × stoupání"));
        form->addRow(tr("Patní průměr"),root_);
        end_=new QComboBox(this);end_->setObjectName("shaftThreadEnd");
        end_->addItem(tr("Délka"),static_cast<int>(document::EndCondition::Length));
        end_->addItem(tr("Až k"),static_cast<int>(document::EndCondition::UpTo));
        end_->addItem(tr("Skrz vše"),static_cast<int>(document::EndCondition::ThroughAll));
        end_->setCurrentIndex(end_->findData(static_cast<int>(pending_.shaft_thread.end_condition)));
        form->addRow(tr("Zakončení"),end_);form->addRow(tr("Délka od počáteční plochy"),length_);
        runout_=new QCheckBox(tr("Výběh závitu"),this);
        runout_->setObjectName("shaftThreadRunout");
        runout_->setChecked(pending_.shaft_thread.runout_enabled);
        form->addRow(runout_);form->addRow(tr("Délka výběhu"),factor_);
        content_layout()->addLayout(form);
        refill(false);
        connect(standard_,&QComboBox::currentIndexChanged,this,[this] { refill(true);notify(); });
        connect(size_,&QComboBox::activated,this,[this] { select_size();notify(); });
        for (auto* field : {root_,length_,factor_})
            connect(field,&QDoubleSpinBox::valueChanged,this,[this] { notify(); });
        connect(name_,&QLineEdit::textChanged,this,[this] { notify(); });
        connect(end_,&QComboBox::currentIndexChanged,this,[this] {
            active_=end_->currentData().toInt()==static_cast<int>(document::EndCondition::UpTo) ? 3 : -1;
            if (active_<0) inspected_[3]=false;
            refresh_fields();notify();
        });
        connect(chamfer_,&QCheckBox::toggled,this,[this](bool checked) {
            if (!checked) pending_.shaft_thread.chamfer.reset();
            active_=checked ? 2 : -1;refresh_fields();notify();
        });
        connect(runout_,&QCheckBox::toggled,this,[this] { refresh_fields();notify(); });
        if (!pending_.shaft_thread.cylinder.valid()) active_=0;
        else if (!pending_.shaft_thread.start.valid()) active_=1;
        else if (pending_.shaft_thread.end_condition==document::EndCondition::UpTo &&
            !pending_.shaft_thread.end) active_=3;
        refresh_fields();
    }
    Container pending() const {
        auto value=pending_;auto& p=value.shaft_thread;
        value.name=name_->text().toStdString();
        p.standard=static_cast<document::ThreadStandard>(standard_->currentIndex());
        p.root_diameter=root_->value();p.length=length_->value();
        p.runout_pitch_factor=factor_->value();p.runout_enabled=end_->currentIndex()==0 && runout_->isChecked();
        p.end_condition=static_cast<document::EndCondition>(end_->currentData().toInt());
        if (!chamfer_->isChecked()) p.chamfer.reset();
        return value;
    }
    int active_reference() const { return active_; }
    bool inspected(int row) const { return inspected_[row]; }
    std::optional<kernel::FaceReference> reference(int row) const {
        const auto& p=pending_.shaft_thread;
        if (row==0) return p.cylinder.valid() ? std::optional{p.cylinder} : std::nullopt;
        if (row==1) return p.start.valid() ? std::optional{p.start} : std::nullopt;
        return row==2 ? p.chamfer : p.end;
    }
    void set_reference(kernel::FaceReference ref,const QString& label) {
        if (active_<0) return;
        const int row=active_;auto& p=pending_.shaft_thread;
        if (row==0) p.cylinder=std::move(ref);
        if (row==1) p.start=std::move(ref);
        if (row==2) p.chamfer=std::move(ref);
        if (row==3) p.end=std::move(ref);
        labels_[row]=label;
        active_=-1;refresh_fields();notify();
    }
    void end_reference_entry() { active_=-1;inspected_.fill(false);refresh_fields();notify(); }
    bool set_numeric(std::string_view key,double value) {
        if (value<=0) throw std::runtime_error("Rozměr musí být kladný.");
        if (key=="root_diameter") root_->setValue(value);
        else if (key=="length" && end_->currentData().toInt()==static_cast<int>(document::EndCondition::Length))
            length_->setValue(value);
        else return false;
        return true;
    }
protected:
    bool submit() override { commit_(pending());return true; }
private:
    QDoubleSpinBox* number(double value,const char* name) {
        auto* result=new QDoubleSpinBox(this);result->setObjectName(name);
        result->setDecimals(3);result->setRange(0.001,1e6);result->setSuffix(tr(" mm"));result->setValue(value);
        result->setKeyboardTracking(false);return result;
    }
    void refill(bool select) {
        const QSignalBlocker blocker(size_);
        sizes_=load_thread_catalog(standard_->currentData().toString());
        size_->clear();int selected=0;
        for (const auto& value : sizes_) {
            if (value.designation.toStdString()==pending_.shaft_thread.designation) selected=size_->count();
            size_->addItem(value.designation);
        }
        size_->setCurrentIndex(selected);
        if (select) select_size();
    }
    void select_size() {
        const int index=size_->currentIndex();
        if (index<0 || index>=static_cast<int>(sizes_.size())) return;
        const auto& value=sizes_[index];auto& p=pending_.shaft_thread;
        p.designation=value.designation.toStdString();p.nominal_diameter=value.nominal_diameter;p.pitch=value.pitch;
        const QSignalBlocker blocker(root_);
        root_->setValue(value.external_root_diameter);
    }
    void refresh_fields() {
        const bool up_to=end_->currentData().toInt()==static_cast<int>(document::EndCondition::UpTo);
        refs_->setRowHidden(2,false);refs_->setRowHidden(3,!up_to);
        if (end_->currentIndex()!=0) {
            const QSignalBlocker blocker(runout_);
            runout_->setChecked(false);
        }
        length_->setEnabled(end_->currentIndex()==0);runout_->setEnabled(end_->currentIndex()==0);
        factor_->setEnabled(end_->currentIndex()==0 && runout_->isChecked());
        for (int row=0;row<4;++row) {
            const auto ref=reference(row);
            fields_[row]->setText(ref ? (labels_[row].isEmpty() ? QString::fromStdString(ref->semantic_key) : labels_[row]) : tr("Vyberte plochu…"));
            if (ref) fields_[row]->set_reference(QString::fromStdString(ref->semantic_key));
            else fields_[row]->clear_reference();
            fields_[row]->set_active_input(active_==row);fields_[row]->set_inspected(inspected_[row] && ref.has_value());
            const QSignalBlocker blocker(eyes_[row]);eyes_[row]->setEnabled(ref.has_value());eyes_[row]->setChecked(inspected_[row]);
        }
        refs_->viewport()->update();
    }
    void notify() { if (changed) changed(); }
    Container pending_;std::function<void(Container)> commit_;
    QLineEdit* name_{};QTableWidget* refs_{};
    std::array<ui::ReferenceCellItem*,4> fields_{};
    std::array<QToolButton*,4> eyes_{};
    std::array<QString,4> labels_{};
    std::array<bool,4> inspected_{};
    int active_{-1};
    QComboBox *standard_{},*size_{},*end_{};
    QDoubleSpinBox *root_{},*length_{},*factor_{};
    QCheckBox *chamfer_{},*runout_{};
    std::vector<ThreadCatalogSize> sizes_;
};
}
