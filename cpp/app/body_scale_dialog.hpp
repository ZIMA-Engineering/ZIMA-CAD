#pragma once
#include <zima/document/body_history.hpp>
#include <zima/kernel/scale_geometry.hpp>
#include <zima/ui/properties_subwindow.hpp>
#include <zima/ui/reference_cell.hpp>
#include <zima/ui/unit_spin_box.hpp>
#include <QDoubleSpinBox>
#include <QFormLayout>
#include <QHeaderView>
#include <QLabel>
#include <QLineEdit>
#include <QSignalBlocker>
#include <QTableWidget>
#include <QToolButton>
#include <QVBoxLayout>

namespace zima::app {
class BodyScaleDialog final : public ui::PropertiesSubWindow {
public:
    document::BodyHistory pending;
    std::function<void()> request_source,changed;
    BodyScaleDialog(document::BodyHistory value,std::function<void(document::BodyHistory)> commit,QWidget* parent)
        : PropertiesSubWindow(tr("Body scale"),parent),pending(std::move(value)),commit_(std::move(commit)) {
        setObjectName("bodyScaleDialog");setAttribute(Qt::WA_DeleteOnClose);
        auto* form=new QFormLayout;
        auto* name=new QLineEdit(QString::fromStdString(pending.name),this);name->setObjectName("bodyScaleName");
        form->addRow(tr("Název"),name);content_layout()->addLayout(form);
        connect(name,&QLineEdit::textChanged,this,[this](const auto& value){pending.name=value.toStdString();});
        table_=new QTableWidget(1,4,this);table_->setObjectName("bodyScaleSource");
        table_->horizontalHeader()->hide();table_->verticalHeader()->setDefaultSectionSize(32);
        table_->setFixedHeight(38);table_->setSelectionMode(QAbstractItemView::NoSelection);
        table_->setEditTriggers(QAbstractItemView::NoEditTriggers);
        table_->setColumnWidth(0,32);table_->setColumnWidth(1,80);table_->setColumnWidth(3,32);
        table_->horizontalHeader()->setSectionResizeMode(0,QHeaderView::Fixed);
        table_->horizontalHeader()->setSectionResizeMode(1,QHeaderView::ResizeToContents);
        table_->horizontalHeader()->setSectionResizeMode(2,QHeaderView::Stretch);
        table_->horizontalHeader()->setSectionResizeMode(3,QHeaderView::Fixed);
        ui::install_reference_cell_delegate(table_);
        // The source is required and replaceable, never removable.
        table_->setCellWidget(0,0,ui::build_reference_row_indicator([this]{if(request_source)request_source();}));
        table_->setItem(0,1,new QTableWidgetItem(tr("Zdroj")));
        field_=new ui::ReferenceCellItem;table_->setItem(0,2,field_);
        eye_=ui::build_reference_inspection_button(false,false,[this](bool on){inspected_=on;refresh();notify();});
        table_->setCellWidget(0,3,ui::centered_cell_widget(eye_));content_layout()->addWidget(table_);
        connect(table_,&QTableWidget::cellClicked,this,[this](int,int column){if(column==2&&request_source)request_source();});
        auto* dimensions=new QFormLayout;content_layout()->addLayout(dimensions);
        auto* factor=new QDoubleSpinBox(this);factor->setObjectName("bodyScaleFactor");
        factor->setDecimals(8);factor->setRange(0.00000001,1000000);factor->setValue(pending.scale->factor);
        dimensions->addRow(tr("Scale factor"),factor);
        connect(factor,&QDoubleSpinBox::valueChanged,this,[this](double value){pending.scale->factor=value;notify();});
        for(int axis=0;axis<3;++axis) {
            auto* coordinate=new ui::UnitDoubleSpinBox(ui::InputQuantity::Length,this);coordinate->setObjectName(QString("bodyScaleCenter%1").arg(axis));
            coordinate->set_display_decimals(6);coordinate->setRange(-1e9,1e9);
            const auto c=pending.scale->center;coordinate->setValue(axis==0?c.x:axis==1?c.y:c.z);
            dimensions->addRow(tr("Scale center %1").arg(QChar("XYZ"[axis])),coordinate);
            connect(coordinate,&QDoubleSpinBox::valueChanged,this,[this,axis](double value){
                auto& c=pending.scale->center;if(axis==0)c.x=value;else if(axis==1)c.y=value;else c.z=value;notify();});
        }
        status_=new QLabel(this);status_->setWordWrap(true);content_layout()->addWidget(status_);
        set_initial_size({480,320});refresh();
    }
    void arm(){active_=true;refresh();}
    void end_input(){active_=false;inspected_=false;refresh();}
    bool active_input() const{return active_;}
    bool inspected() const{return inspected_;}
    void set_source(std::string source,QString label){pending.scale->source_id=std::move(source);label_=std::move(label);active_=false;inspected_=false;refresh();notify();}
    void set_status(const QString& text){status_->setText(text);}
protected:
    bool submit() override {
        try {
            if(QString::fromStdString(pending.name).trimmed().isEmpty())throw std::invalid_argument("Specify a nonempty object name.");
            if(pending.scale->source_id.empty())throw std::invalid_argument("Select a source Body.");
            kernel::validate_body_scale(pending.scale->factor,pending.scale->center);
            commit_(pending);return true;
        }catch(const std::exception& error){set_status(tr(error.what()));return false;}
    }
private:
    std::function<void(document::BodyHistory)> commit_;
    QTableWidget* table_{};ui::ReferenceCellItem* field_{};QToolButton* eye_{};QLabel* status_{};
    QString label_;bool active_{},inspected_{};
    void notify(){if(changed)changed();}
    void refresh(){
        const bool filled=!pending.scale->source_id.empty();
        if(filled){field_->set_reference(QString::fromStdString(pending.scale->source_id));field_->setText(label_.isEmpty()?QString::fromStdString(pending.scale->source_id):label_);}
        else {field_->clear_reference();field_->setText(tr("Vyberte…"));}
        field_->set_active_input(active_);field_->set_inspected(inspected_&&filled);
        const QSignalBlocker block(eye_);eye_->setEnabled(filled);eye_->setChecked(inspected_&&filled);table_->viewport()->update();
    }
};
} // namespace zima::app
