#pragma once
#include "sweep_placement_dialog.hpp"
#include <zima/ui/reference_cell.hpp>
#include <QHeaderView>
#include <QLineEdit>
#include <QFormLayout>
#include <QPushButton>
#include <QToolButton>
#include <QSignalBlocker>
#include <QSpinBox>
#include <QCheckBox>
#include <QComboBox>
namespace zima::app {
class DerivedCopyDialog final : public SweepPlacementDialog {
public:
    document::DerivedCopyParameters derived_copy;
    std::function<void(int)> request_input;
    DerivedCopyDialog(document::HistoryContainer value,document::DerivedCopyParameters parameters,
        std::function<void(document::HistoryContainer,document::DerivedCopyParameters)> commit,QWidget* parent)
        : SweepPlacementDialog(parameters.pattern?tr("Vlastnosti Pole"):tr("Vlastnosti Zrcadla"),std::move(value),parent),derived_copy(std::move(parameters)),commit_(std::move(commit)) {
        setObjectName(derived_copy.pattern?"patternDialog":"mirrorDialog");setAttribute(Qt::WA_DeleteOnClose);setMinimumWidth(440);set_initial_size({510,820});
        auto* form=new QFormLayout;auto* name=new QLineEdit(QString::fromStdString(pending.name),this);name->setObjectName("mirrorName");
        form->addRow(tr("Název"),name);content_layout()->addLayout(form);
        connect(name,&QLineEdit::textChanged,this,[this](const auto& text){pending.name=text.toStdString();});
        install_placement();
        if(derived_copy.pattern) {
            auto* form=new QFormLayout;auto* mode=new QComboBox(this);mode->setObjectName("patternMode");mode->addItems({tr("Lineární"),tr("Kruhové")});mode->setCurrentIndex(derived_copy.pattern->circular?1:0);
            auto* count=new QSpinBox(this);count->setObjectName("patternCount");count->setRange(2,1000);count->setValue(derived_copy.pattern->count);
            direction_=new QComboBox(this);direction_->setObjectName("patternDirection");direction_->addItems({"X","Y","Z"});direction_->setCurrentIndex(derived_copy.linear_axis);
            spacing_=new QDoubleSpinBox(this);spacing_->setObjectName("patternSpacing");spacing_->setRange(-1e6,1e6);spacing_->setDecimals(3);spacing_->setSuffix(tr(" mm"));spacing_->setValue(derived_copy.pattern->spacing);
            full_circle_=new QCheckBox(tr("Celý kruh"),this);full_circle_->setObjectName("patternFullCircle");full_circle_->setChecked(derived_copy.pattern->full_circle);
            angle_=new QDoubleSpinBox(this);angle_->setObjectName("patternAngle");angle_->setRange(-359.999,359.999);angle_->setDecimals(3);angle_->setSuffix(tr(" °"));angle_->setValue(derived_copy.pattern->angle_degrees);
            form->addRow(tr("Druh Pole"),mode);form->addRow(tr("Počet včetně zdroje"),count);form->addRow(tr("Místní směr"),direction_);form->addRow(tr("Rozteč"),spacing_);
            form->addRow(full_circle_);form->addRow(tr("Úhel mezi výskyty"),angle_);content_layout()->addLayout(form);
            connect(mode,&QComboBox::currentIndexChanged,this,[this](int i){derived_copy.pattern->circular=i==1;end_input();refresh_pattern();notify();});
            connect(count,&QSpinBox::valueChanged,this,[this](int n){derived_copy.pattern->count=n;refresh_pattern();notify();});
            connect(direction_,&QComboBox::currentIndexChanged,this,[this](int i){derived_copy.linear_axis=i;notify();});
            connect(spacing_,&QDoubleSpinBox::valueChanged,this,[this](double v){derived_copy.pattern->spacing=v;notify();});
            connect(angle_,&QDoubleSpinBox::valueChanged,this,[this](double v){derived_copy.pattern->angle_degrees=v;notify();});
            connect(full_circle_,&QCheckBox::toggled,this,[this](bool on){derived_copy.pattern->full_circle=on;refresh_pattern();notify();});
        }
        table_=new QTableWidget(2,4,this);table_->setObjectName("mirrorReferences");
        table_->horizontalHeader()->hide();table_->verticalHeader()->hide();table_->setFixedHeight(74);
        table_->setSelectionMode(QAbstractItemView::NoSelection);table_->setEditTriggers(QAbstractItemView::NoEditTriggers);
        table_->setColumnWidth(0,26);table_->setColumnWidth(1,120);table_->setColumnWidth(3,28);
        table_->horizontalHeader()->setSectionResizeMode(2,QHeaderView::Stretch);ui::install_reference_cell_delegate(table_);
        for(int row=0;row<2;++row){
            table_->setItem(row,1,new QTableWidgetItem(row?tr("Zdroj"):derived_copy.pattern?tr("Osa Pole"):tr("Rovina zrcadlení")));
            fields_[row]=new ui::ReferenceCellItem;table_->setItem(row,2,fields_[row]);
            indicators_[row]=ui::build_reference_row_indicator([this,row]{if(row)derived_copy.source_id.clear();else derived_copy.reference={};
                labels_[row].clear();inspected_[row]=false;refresh_fields();notify();});table_->setCellWidget(row,0,indicators_[row]);
            eyes_[row]=ui::build_reference_inspection_button(false,false,[this,row](bool on){inspected_[row]=on;refresh_fields();notify();});
            table_->setCellWidget(row,3,ui::centered_cell_widget(eyes_[row]));
        }
        connect(table_,&QTableWidget::cellClicked,this,[this](int row,int column){if(column==2&&request_input)request_input(row);});
        content_layout()->addWidget(table_);
        plane_buttons_=new QWidget(this);auto* planes=new QHBoxLayout(plane_buttons_);planes->setContentsMargins(0,0,0,0);
        planes->addWidget(new QLabel(derived_copy.pattern?tr("Osy vlastního počátku:"):tr("Roviny vlastního počátku:"),this));
        for(const auto* key:derived_copy.pattern?std::array<const char*,3>{"x","y","z"}:std::array<const char*,3>{"xy","yz","xz"}){auto* button=new QPushButton(QString::fromLatin1(key).toUpper(),this);
            button->setObjectName(QString("mirrorPlane_%1").arg(key));planes->addWidget(button);
            connect(button,&QPushButton::clicked,this,[this,key]{if(request_input)request_input(0);set_plane({{},pending.container_origin.id,(derived_copy.pattern?"origin:axis:":"origin:plane:")+std::string(key)},QString::fromLatin1(key).toUpper());});}
        content_layout()->addWidget(plane_buttons_);
        auto* help=new QLabel(tr("Kopie přebírají geometrii zdroje. Rozměry a vlastnosti geometrie se upravují u zdroje."),this);
        help->setWordWrap(true);content_layout()->addWidget(help);
        status_=new QLabel(this);status_->setWordWrap(true);content_layout()->addWidget(status_);refresh_fields();refresh_pattern();
    }
    void set_status(const QString& text) override{status_->setText(text);}
    void set_sketch(unsigned,const sketcher::Sketch&) override{}
    void set_plane(document::ConstructionReference ref,QString label){derived_copy.reference=std::move(ref);labels_[0]=std::move(label);active_=-1;refresh_fields();notify();}
    void set_source(std::string id,QString label){derived_copy.source_id=std::move(id);labels_[1]=std::move(label);active_=-1;refresh_fields();notify();}
    int active_input() const{return active_;}
    bool inspected(int i) const{return inspected_.at(i);}
    void arm(int i){SweepPlacementDialog::set_active_reference_index(std::nullopt);active_=i;refresh_fields();}
    void end_input(){active_=-1;inspected_={};refresh_fields();}
    void set_active_reference_index(std::optional<std::size_t> i) override{if(i)end_input();SweepPlacementDialog::set_active_reference_index(i);}
    void clear_reference_highlights() override{inspected_={};refresh_fields();SweepPlacementDialog::clear_reference_highlights();}
protected:
    bool submit() override{try{read_placement();if(pending.name.empty())throw std::invalid_argument("Zadejte název Zrcadla.");
        if(derived_copy.source_id.empty())throw std::invalid_argument("Vyberte zdrojové těleso nebo komponentu.");
        if((!derived_copy.pattern||derived_copy.pattern->circular)&&derived_copy.reference.owner_id.empty())throw std::invalid_argument(derived_copy.pattern?"Vyberte osu Pole.":"Vyberte rovinu zrcadlení.");
        commit_(pending,derived_copy);return true;}catch(const std::exception& error){set_status(QString::fromUtf8(error.what()));return false;}}
private:
    std::function<void(document::HistoryContainer,document::DerivedCopyParameters)> commit_;
    QWidget* plane_buttons_{};QComboBox* direction_{};QDoubleSpinBox* spacing_{};QDoubleSpinBox* angle_{};QCheckBox* full_circle_{};
    void refresh_pattern(){if(!derived_copy.pattern)return;const bool circular=derived_copy.pattern->circular;
        const QSignalBlocker angle_block(angle_);angle_->setValue(derived_copy.pattern->full_circle?360.0/derived_copy.pattern->count:derived_copy.pattern->angle_degrees);
        table_->setRowHidden(0,!circular);table_->setFixedHeight(circular?74:38);plane_buttons_->setVisible(circular);
        direction_->setEnabled(!circular);spacing_->setEnabled(!circular);full_circle_->setEnabled(circular);angle_->setEnabled(circular&&!derived_copy.pattern->full_circle);}
    QTableWidget* table_{};QLabel* status_{};std::array<ui::ReferenceCellItem*,2> fields_{};
    std::array<QWidget*,2> indicators_{};std::array<QToolButton*,2> eyes_{};std::array<QString,2> labels_{};
    std::array<bool,2> inspected_{};int active_{-1};
    void notify(){if(changed)changed();}
    void refresh_fields(){for(int i=0;i<2;++i){const auto value=i?derived_copy.source_id:derived_copy.reference.owner_id+derived_copy.reference.semantic_key;
        if(value.empty()){fields_[i]->clear_reference();fields_[i]->setText(tr("Vyberte…"));}
        else {fields_[i]->set_reference(QString::fromStdString(value));fields_[i]->setText(labels_[i].isEmpty()?QString::fromStdString(i?derived_copy.source_id:derived_copy.reference.semantic_key):labels_[i]);}
        fields_[i]->set_active_input(active_==i);fields_[i]->set_inspected(inspected_[i]&&!value.empty());
        const QSignalBlocker block(eyes_[i]);eyes_[i]->setEnabled(!value.empty());eyes_[i]->setChecked(inspected_[i]&&!value.empty());
        ui::set_reference_row_populated(indicators_[i],!value.empty());}table_->viewport()->update();}
};
} // namespace zima::app
