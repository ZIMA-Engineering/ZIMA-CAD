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
            form->addRow(tr("Druh Pole"),mode);content_layout()->addLayout(form);
            circular_settings_=new QWidget(this);auto* circular_form=new QFormLayout(circular_settings_);
            auto* count=new QSpinBox(this);count->setObjectName("patternCircularCount");count->setRange(2,1000);count->setValue(derived_copy.pattern->count);
            full_circle_=new QCheckBox(tr("Celý kruh"),this);full_circle_->setObjectName("patternFullCircle");full_circle_->setChecked(derived_copy.pattern->full_circle);
            angle_=new QDoubleSpinBox(this);angle_->setObjectName("patternAngle");angle_->setRange(-359.999,359.999);angle_->setDecimals(zima::ui::numeric_decimal_places(this,3));angle_->setSuffix(tr(" °"));angle_->setValue(derived_copy.pattern->angle_degrees);
            ui::bind_numeric_value_lock(angle_,"pattern:angle",derived_copy.value_locks,[this]{notify();});
            circular_form->addRow(tr("Počet včetně zdroje"),count);circular_form->addRow(full_circle_);circular_form->addRow(tr("Úhel mezi výskyty"),angle_);content_layout()->addWidget(circular_settings_);
            connect(mode,&QComboBox::currentIndexChanged,this,[this](int i){derived_copy.pattern->circular=i==1;end_input();refresh_pattern();notify();});
            connect(count,&QSpinBox::valueChanged,this,[this](int n){derived_copy.pattern->count=n;refresh_pattern();notify();});
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
        if(derived_copy.pattern) {
            setMinimumWidth(620);set_initial_size({660,850});
            linear_table_=new QTableWidget(3,7,this);linear_table_->setObjectName("patternLinearDirections");
            linear_table_->setHorizontalHeaderLabels({"",tr("Místní osa"),"",tr("Rozložení"),tr("Rozteč"),tr("Počet"),tr("Vzad")});
            linear_table_->setVerticalHeaderLabels({tr("Směr 1"),tr("Směr 2"),tr("Směr 3")});
            linear_table_->setSelectionMode(QAbstractItemView::NoSelection);linear_table_->setEditTriggers(QAbstractItemView::NoEditTriggers);
            ui::install_reference_cell_delegate(linear_table_);linear_table_->setFixedHeight(132);
            const std::array<int,7> widths{26,82,28,112,91,62,62};
            for(int column=0;column<7;++column)linear_table_->setColumnWidth(column,widths[column]);
            linear_table_->horizontalHeader()->setSectionResizeMode(1,QHeaderView::Stretch);
            for(int row=0;row<3;++row) {
                const int field=row+2;auto& d=derived_copy.pattern->linear[row];
                fields_[field]=new ui::ReferenceCellItem;linear_table_->setItem(row,1,fields_[field]);
                indicators_[field]=ui::build_reference_row_indicator([this,row,field]{derived_copy.pattern->linear[row].local_axis=-1;inspected_[field]=false;if(active_==field)active_=-1;refresh_fields();refresh_pattern();notify();});
                linear_table_->setCellWidget(row,0,indicators_[field]);
                eyes_[field]=ui::build_reference_inspection_button(false,false,[this,field](bool on){inspected_[field]=on;refresh_fields();notify();});
                linear_table_->setCellWidget(row,2,ui::centered_cell_widget(eyes_[field]));
                auto* distribution=new QComboBox(this);distribution->setObjectName(QString("patternDistribution%1").arg(row));
                distribution->addItems({tr("Vpřed"),tr("Vzad"),tr("Oboustranně"),tr("Symetricky")});distribution->setCurrentIndex(static_cast<int>(d.distribution));linear_table_->setCellWidget(row,3,distribution);
                auto* spacing=new QDoubleSpinBox(this);spacing->setObjectName(QString("patternSpacing%1").arg(row));spacing->setRange(0.001,1e6);spacing->setDecimals(zima::ui::numeric_decimal_places(this,3));spacing->setSuffix(tr(" mm"));spacing->setValue(d.spacing);linear_table_->setCellWidget(row,4,spacing);
                ui::bind_numeric_value_lock(spacing,"pattern:spacing:"+std::to_string(row),derived_copy.value_locks,[this]{notify();});
                auto* count=new QSpinBox(this);count->setObjectName(QString("patternCount%1").arg(row));count->setRange(2,1000);count->setValue(d.count);linear_table_->setCellWidget(row,5,count);
                auto* reverse=new QSpinBox(this);reverse->setObjectName(QString("patternReverseCount%1").arg(row));reverse->setRange(1,999);reverse->setValue(d.reverse_count);linear_table_->setCellWidget(row,6,reverse);
                connect(distribution,&QComboBox::currentIndexChanged,this,[this,row,count](int value){auto& d=derived_copy.pattern->linear[row];d.distribution=static_cast<kernel::PatternDistribution>(value);
                    if(d.distribution==kernel::PatternDistribution::Symmetric&&d.count%2==0)count->setValue(d.count==1000?999:d.count+1);
                    refresh_pattern();notify();});
                connect(spacing,&QDoubleSpinBox::valueChanged,this,[this,row](double value){derived_copy.pattern->linear[row].spacing=value;notify();});
                connect(count,&QSpinBox::valueChanged,this,[this,row](int value){derived_copy.pattern->linear[row].count=value;notify();});
                connect(reverse,&QSpinBox::valueChanged,this,[this,row](int value){derived_copy.pattern->linear[row].reverse_count=value;notify();});
            }
            connect(linear_table_,&QTableWidget::cellClicked,this,[this](int row,int column){if(column==1&&request_input)request_input(row+2);});
            content_layout()->addWidget(linear_table_);
            linear_help_=new QLabel(tr("Vyberte jednu až tři osy počátku Pole. Počet zahrnuje zdroj; Vzad přidává další kopie u oboustranného rozložení. Symetricky používá lichý celkový počet (3, 5, 7…)."),this);
            linear_help_->setWordWrap(true);content_layout()->addWidget(linear_help_);
        }
        auto* help=new QLabel(tr("Kopie přebírají geometrii zdroje. Rozměry a vlastnosti geometrie se upravují u zdroje."),this);
        help->setWordWrap(true);content_layout()->addWidget(help);
        status_=new QLabel(this);status_->setWordWrap(true);content_layout()->addWidget(status_);refresh_fields();refresh_pattern();
    }
    void set_status(const QString& text) override{status_->setText(text);}
    void set_sketch(unsigned,const sketcher::Sketch&) override{}
    void set_plane(document::ConstructionReference ref,QString label){derived_copy.reference=std::move(ref);labels_[0]=std::move(label);active_=-1;refresh_fields();notify();}
    void set_source(std::string id,QString label){derived_copy.source_id=std::move(id);labels_[1]=std::move(label);active_=-1;refresh_fields();notify();}
    void set_linear_axis(int row,int axis){derived_copy.pattern->linear.at(row).local_axis=axis;active_=-1;refresh_fields();refresh_pattern();notify();}
    document::ConstructionReference input_reference(int field) const {
        if(field==0)return derived_copy.reference;
        if(field<2||!derived_copy.pattern)return {};
        const auto axis=derived_copy.pattern->linear.at(field-2).local_axis;
        return axis<0?document::ConstructionReference{}:document::ConstructionReference{{},pending.container_origin.id,"origin:axis:"+std::string(1,"xyz"[axis])};
    }
    int input_count() const{return derived_copy.pattern?5:2;}
    int active_input() const{return active_;}
    bool inspected(int i) const{return inspected_.at(i);}
    void arm(int i){SweepPlacementDialog::set_active_reference_index(std::nullopt);active_=i;refresh_fields();}
    void end_input(){active_=-1;inspected_={};refresh_fields();}
    void set_active_reference_index(std::optional<std::size_t> i) override{if(i)end_input();SweepPlacementDialog::set_active_reference_index(i);}
    void clear_reference_highlights() override{inspected_={};refresh_fields();SweepPlacementDialog::clear_reference_highlights();}
protected:
    bool submit() override{try{read_placement();if(pending.name.empty())throw std::invalid_argument("Zadejte název kontejneru.");
        if(derived_copy.source_id.empty())throw std::invalid_argument("Vyberte zdrojové těleso nebo komponentu.");
        if((!derived_copy.pattern||derived_copy.pattern->circular)&&derived_copy.reference.owner_id.empty())throw std::invalid_argument(derived_copy.pattern?"Vyberte osu Pole.":"Vyberte rovinu zrcadlení.");
        commit_(pending,derived_copy);return true;}catch(const std::exception& error){set_status(QString::fromUtf8(error.what()));return false;}}
private:
    std::function<void(document::HistoryContainer,document::DerivedCopyParameters)> commit_;
    QWidget* plane_buttons_{};QWidget* circular_settings_{};QDoubleSpinBox* angle_{};QCheckBox* full_circle_{};
    QTableWidget* linear_table_{};QLabel* linear_help_{};
    void refresh_pattern(){if(!derived_copy.pattern)return;const bool circular=derived_copy.pattern->circular;
        const QSignalBlocker angle_block(angle_);angle_->setValue(derived_copy.pattern->full_circle?360.0/derived_copy.pattern->count:derived_copy.pattern->angle_degrees);
        table_->setRowHidden(0,!circular);table_->setFixedHeight(circular?74:38);plane_buttons_->setVisible(circular);
        circular_settings_->setVisible(circular);linear_table_->setVisible(!circular);linear_help_->setVisible(!circular);
        angle_->setEnabled(!derived_copy.pattern->full_circle);
        for(int row=0;row<3;++row){const auto& d=derived_copy.pattern->linear[row];const bool enabled=d.local_axis>=0;
            for(int column=3;column<7;++column)linear_table_->cellWidget(row,column)->setEnabled(enabled&&(column!=6||d.distribution==kernel::PatternDistribution::Both));
            auto* count=qobject_cast<QSpinBox*>(linear_table_->cellWidget(row,5));count->setSingleStep(d.distribution==kernel::PatternDistribution::Symmetric?2:1);
            count->setToolTip(d.distribution==kernel::PatternDistribution::Both?tr("Počet vpřed včetně zdroje; další kopie vzad určuje sousední pole."):tr("Celkový počet v tomto směru včetně zdroje."));
        }
    }
    QTableWidget* table_{};QLabel* status_{};std::array<ui::ReferenceCellItem*,5> fields_{};
    std::array<QWidget*,5> indicators_{};std::array<QToolButton*,5> eyes_{};std::array<QString,5> labels_{};
    std::array<bool,5> inspected_{};int active_{-1};
    void notify(){if(changed)changed();}
    void refresh_fields(){for(int i=0;i<input_count();++i){if(!fields_[i])continue;const auto reference=input_reference(i);
        const auto value=i==1?derived_copy.source_id:reference.owner_id+reference.semantic_key;
        if(value.empty()){fields_[i]->clear_reference();fields_[i]->setText(tr("Vyberte…"));}
        else {fields_[i]->set_reference(QString::fromStdString(value));fields_[i]->setText(i>=2?QString("Osa %1").arg(QChar("XYZ"[derived_copy.pattern->linear[i-2].local_axis])):
            labels_[i].isEmpty()?QString::fromStdString(i==1?derived_copy.source_id:derived_copy.reference.semantic_key):labels_[i]);}
        fields_[i]->set_active_input(active_==i);fields_[i]->set_inspected(inspected_[i]&&!value.empty());
        const QSignalBlocker block(eyes_[i]);eyes_[i]->setEnabled(!value.empty());eyes_[i]->setChecked(inspected_[i]&&!value.empty());
        ui::set_reference_row_populated(indicators_[i],!value.empty());}table_->viewport()->update();if(linear_table_)linear_table_->viewport()->update();}

};
} // namespace zima::app
