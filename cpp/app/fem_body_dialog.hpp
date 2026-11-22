#pragma once
#include <zima/document/body_history.hpp>
#include <zima/ui/properties_subwindow.hpp>
#include <zima/ui/reference_cell.hpp>
#include <QFormLayout>
#include <QComboBox>
#include <QCheckBox>
#include <QDoubleSpinBox>
#include <QHeaderView>
#include <QLabel>
#include <QLineEdit>
#include <QSignalBlocker>
#include <QTableWidget>
#include <QToolButton>

namespace zima::app {
class FemBodyDialog final : public ui::PropertiesSubWindow {
public:
    document::BodyHistory pending;
    std::function<void()> changed;
    FemBodyDialog(document::BodyHistory value,std::function<void(document::BodyHistory)> commit,QWidget* parent)
        : PropertiesSubWindow(tr("FEM Body properties"),parent),pending(std::move(value)),commit_(std::move(commit)) {
        setObjectName("femBodyDialog");setAttribute(Qt::WA_DeleteOnClose);
        auto* form=new QFormLayout;
        auto* name=new QLineEdit(QString::fromStdString(pending.name),this);name->setObjectName("femBodyName");
        form->addRow(tr("Název"),name);content_layout()->addLayout(form);
        connect(name,&QLineEdit::textChanged,this,[this](const auto& text){pending.name=text.toStdString();});
        table_=new QTableWidget(1,4,this);table_->setObjectName("femBodySource");
        table_->horizontalHeader()->hide();table_->verticalHeader()->setDefaultSectionSize(32);
        table_->setFixedHeight(38);table_->setSelectionMode(QAbstractItemView::NoSelection);
        table_->setEditTriggers(QAbstractItemView::NoEditTriggers);
        table_->setColumnWidth(0,32);table_->setColumnWidth(3,32);
        table_->horizontalHeader()->setSectionResizeMode(0,QHeaderView::Fixed);
        table_->horizontalHeader()->setSectionResizeMode(1,QHeaderView::ResizeToContents);
        table_->horizontalHeader()->setSectionResizeMode(2,QHeaderView::Stretch);
        table_->horizontalHeader()->setSectionResizeMode(3,QHeaderView::Fixed);
        ui::install_reference_cell_delegate(table_);
        table_->setCellWidget(0,0,ui::build_reference_row_indicator([this]{arm();}));
        table_->setItem(0,1,new QTableWidgetItem(tr("Zdroj")));
        field_=new ui::ReferenceCellItem;table_->setItem(0,2,field_);
        eye_=ui::build_reference_inspection_button(false,false,[this](bool on){inspected_=on;refresh();notify();});
        table_->setCellWidget(0,3,ui::centered_cell_widget(eye_));content_layout()->addWidget(table_);
        connect(table_,&QTableWidget::cellClicked,this,[this](int,int column){if(column==2)arm();});
        auto* explanation=new QLabel(tr("Geometry and material are shared with the source Body. Edit the source in Modeling."),this);
        explanation->setWordWrap(true);content_layout()->addWidget(explanation);
        auto* thermal_form=new QFormLayout;
        auto* analysis=new QComboBox(this);analysis->setObjectName("femAnalysisKind");
        analysis->addItem(tr("Linear statics"),int(document::FemAnalysisKind::LinearElasticStatic));
        analysis->addItem(tr("Steady-state thermal analysis"),int(document::FemAnalysisKind::SteadyStateThermal));
        analysis->setCurrentIndex(analysis->findData(int(pending.fem->analysis)));
        thermal_form->addRow(tr("Analysis type"),analysis);
        auto* ambient=new QDoubleSpinBox(this);ambient->setObjectName("femAmbientTemperature");
        ambient->setRange(-273.15,1e6);ambient->setDecimals(3);ambient->setSuffix(" °C");
        auto* transfer=new QDoubleSpinBox(this);transfer->setObjectName("femHeatTransfer");
        transfer->setRange(0,1e9);transfer->setDecimals(6);transfer->setSuffix(" W/(m²·K)");
        const auto thermal=pending.fem->thermal.value_or(document::FemThermalSettings{});
        ambient->setValue(thermal.ambient_C);transfer->setValue(thermal.convection_W_m2K);
        thermal_form->addRow(tr("Ambient temperature"),ambient);
        thermal_form->addRow(tr("Heat transfer coefficient"),transfer);
        auto* cooling=new QLabel(tr("Heat transfer applies to the complete exterior, including heated faces. Set the coefficient for your cooling conditions."),this);
        cooling->setWordWrap(true);thermal_form->addRow(cooling);
        auto* expansion=new QCheckBox(tr("Calculate thermal expansion"),this);expansion->setObjectName("femThermalExpansion");
        expansion->setChecked(thermal.expansion);thermal_form->addRow(expansion);
        auto* expansion_help=new QLabel(tr("Uses the Part material's expansion coefficient and reference temperature, and mechanical restraints. Forces and pressure are excluded. Measurement points report temperature and displacement caused by heating."),this);
        expansion_help->setWordWrap(true);thermal_form->addRow(expansion_help);content_layout()->addLayout(thermal_form);
        const auto visibility=[=,this] {
            const bool active=pending.fem->analysis==document::FemAnalysisKind::SteadyStateThermal;
            thermal_form->setRowVisible(ambient,active);thermal_form->setRowVisible(transfer,active);thermal_form->setRowVisible(cooling,active);
            thermal_form->setRowVisible(expansion,active);thermal_form->setRowVisible(expansion_help,active&&expansion->isChecked());
        };
        connect(analysis,&QComboBox::currentIndexChanged,this,[=,this] {
            pending.fem->analysis=static_cast<document::FemAnalysisKind>(analysis->currentData().toInt());
            if(pending.fem->analysis==document::FemAnalysisKind::SteadyStateThermal&&!pending.fem->thermal)
                pending.fem->thermal=thermal;
            visibility();notify();
        });
        connect(ambient,&QDoubleSpinBox::valueChanged,this,[this](double value){if(!pending.fem->thermal)pending.fem->thermal.emplace();pending.fem->thermal->ambient_C=value;});
        connect(transfer,&QDoubleSpinBox::valueChanged,this,[this](double value){if(!pending.fem->thermal)pending.fem->thermal.emplace();pending.fem->thermal->convection_W_m2K=value;});
        connect(expansion,&QCheckBox::toggled,this,[=,this](bool value){
            if(!pending.fem->thermal)pending.fem->thermal.emplace();pending.fem->thermal->expansion=value;visibility();notify();
        });
        visibility();
        status_=new QLabel(this);status_->setObjectName("femBodyStatus");status_->setWordWrap(true);content_layout()->addWidget(status_);
        set_initial_size({520,490});refresh();
    }
    void arm(){active_=true;refresh();notify();}
    void end_input(){active_=false;inspected_=false;refresh();notify();}
    bool active_input()const{return active_;}
    bool inspected()const{return inspected_;}
    void set_source(std::string id,QString name){pending.fem->source_id=std::move(id);label_=std::move(name);active_=false;inspected_=false;refresh();notify();}
    void set_source_available(bool available){available_=available;refresh();}
    void set_status(const QString& text){status_->setText(text);}
protected:
    bool submit()override {
        try {
            if(QString::fromStdString(pending.name).trimmed().isEmpty())throw std::invalid_argument("Specify a nonempty object name.");
            if(pending.fem->source_id.empty())throw std::invalid_argument("Select a source Body.");
            commit_(pending);return true;
        }catch(const std::exception& error){set_status(tr(error.what()));return false;}
    }
private:
    std::function<void(document::BodyHistory)> commit_;
    QTableWidget* table_{};ui::ReferenceCellItem* field_{};QToolButton* eye_{};QLabel* status_{};
    QString label_;bool active_{},inspected_{},available_{};
    void notify(){if(changed)changed();}
    void refresh(){
        const bool filled=!pending.fem->source_id.empty();
        if(filled){field_->set_reference(QString::fromStdString(pending.fem->source_id));field_->setText(label_.isEmpty()?QString::fromStdString(pending.fem->source_id):label_);}
        else{field_->clear_reference();field_->setText(tr("Vyberte…"));}
        field_->set_active_input(active_);field_->set_inspected(inspected_&&available_);
        const QSignalBlocker blocked(eye_);eye_->setEnabled(filled&&available_);eye_->setChecked(inspected_&&available_);table_->viewport()->update();
    }
};
} // namespace zima::app
