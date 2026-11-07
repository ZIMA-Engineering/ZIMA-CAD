#pragma once
#include "sweep_placement_dialog.hpp"
#include "tool_button_style.hpp"
#include <zima/ui/unit_spin_box.hpp>
#include <zima/ui/reference_cell.hpp>
#include <QButtonGroup>
#include <QHeaderView>
#include <QLineEdit>
#include <QFormLayout>
#include <QPushButton>
#include <QToolButton>
#include <QSignalBlocker>
#include <QSpinBox>
#include <QCheckBox>
#include <QComboBox>
#include <array>
#include <map>
#include <set>
#include <vector>
namespace zima::app {
class DerivedCopyDialog final : public SweepPlacementDialog {
public:
    static constexpr int plane_field=0;
    static constexpr int first_source_field=1;
    static constexpr int first_axis_field=100;
    document::DerivedCopyParameters derived_copy;
    std::function<void(int)> request_input;
    DerivedCopyDialog(document::HistoryContainer value,
        document::DerivedCopyParameters parameters,
        std::function<void(document::HistoryContainer,
            document::DerivedCopyParameters)> commit,QWidget* parent)
        : SweepPlacementDialog(parameters.pattern?tr("Vlastnosti Pole"):
              tr("Vlastnosti Zrcadla"),std::move(value),parent),
          derived_copy(std::move(parameters)),commit_(std::move(commit)) {
        setObjectName(derived_copy.pattern?"patternDialog":"mirrorDialog");
        setAttribute(Qt::WA_DeleteOnClose);setMinimumWidth(520);
        set_initial_size({640,760});
        auto* form=new QFormLayout;
        auto* name=new QLineEdit(QString::fromStdString(pending.name),this);
        name->setObjectName("mirrorName");form->addRow(tr("Název"),name);
        content_layout()->addLayout(form);
        connect(name,&QLineEdit::textChanged,this,[this](const auto& text) {
            pending.name=text.toStdString();
        });
        install_placement();
        if(derived_copy.pattern)install_pattern_mode();
        install_reference_selector();
        install_source_table();
        if(derived_copy.pattern)install_linear_directions();
        auto* help=new QLabel(tr("Kopie přebírají geometrii zdrojů. Rozměry a vlastnosti geometrie se upravují u zdrojových prvků."),this);
        help->setWordWrap(true);content_layout()->addWidget(help);
        status_=new QLabel(this);status_->setWordWrap(true);
        content_layout()->addWidget(status_);
        refresh_fields();refresh_pattern();
    }
    void set_status(const QString& text) override{status_->setText(text);}
    void set_sketch(unsigned,const sketcher::Sketch&) override{}
    void set_plane(document::ConstructionReference ref,QString label) {
        derived_copy.reference=std::move(ref);reference_label_=std::move(label);
        active_=-1;refresh_fields();notify();
    }
    void set_source(std::string id,QString label) {
        if(id.empty())return;
        int target=is_source_input(active_)?source_row(active_):
            static_cast<int>(derived_copy.source_ids.size());
        const auto existing=std::ranges::find(derived_copy.source_ids,id);
        if(existing!=derived_copy.source_ids.end()) {
            const auto old=static_cast<int>(existing-derived_copy.source_ids.begin());
            if(!is_source_input(active_))target=old;
            else if(old!=target) {
                derived_copy.source_ids.erase(existing);
                if(old<target)--target;
            }
        }
        if(target<static_cast<int>(derived_copy.source_ids.size()))
            derived_copy.source_ids[target]=id;
        else derived_copy.source_ids.push_back(id);
        source_labels_[id]=std::move(label);active_=-1;
        ensure_source_rows();refresh_fields();notify();
    }
    void set_source_label(const std::string& id,QString label) {
        if(!id.empty())source_labels_[id]=std::move(label);
        refresh_fields();
    }
    void set_linear_axis(int row,int axis) {
        derived_copy.pattern->linear.at(row).local_axis=axis;active_=-1;
        refresh_fields();refresh_pattern();notify();
    }
    [[nodiscard]] bool is_source_input(int field) const {
        return field>=first_source_field&&
            field<first_source_field+source_table_->rowCount();
    }
    [[nodiscard]] bool is_axis_input(int field) const {
        return derived_copy.pattern&&field>=first_axis_field&&
            field<first_axis_field+3;
    }
    [[nodiscard]] int source_row(int field) const {
        return field-first_source_field;
    }
    [[nodiscard]] int axis_row(int field) const {
        return field-first_axis_field;
    }
    [[nodiscard]] std::vector<int> input_fields() const {
        std::vector<int> result{plane_field};
        for(int row=0;row<source_table_->rowCount();++row)
            result.push_back(first_source_field+row);
        if(derived_copy.pattern)for(int row=0;row<3;++row)
            result.push_back(first_axis_field+row);
        return result;
    }
    document::ConstructionReference input_reference(int field) const {
        if(field==plane_field)return derived_copy.reference;
        if(!is_axis_input(field))return {};
        const auto axis=derived_copy.pattern->linear.at(axis_row(field)).local_axis;
        return axis<0?document::ConstructionReference{}:
            document::ConstructionReference{{},pending.container_origin.id,
                "origin:axis:"+std::string(1,"xyz"[axis])};
    }
    int active_input() const{return active_;}
    bool inspected(int field) const {
        if(field==plane_field)return false;
        if(is_axis_input(field))return axis_inspected_.at(axis_row(field));
        if(is_source_input(field)) {
            const int row=source_row(field);
            return row<static_cast<int>(derived_copy.source_ids.size())&&
                inspected_sources_.contains(derived_copy.source_ids[row]);
        }
        return false;
    }
    void arm(int field) {
        SweepPlacementDialog::set_active_reference_index(std::nullopt);
        active_=field;refresh_fields();
    }
    void end_input() {
        active_=-1;axis_inspected_={};
        inspected_sources_.clear();refresh_fields();
    }
    void set_active_reference_index(std::optional<std::size_t> i) override {
        if(i)end_input();SweepPlacementDialog::set_active_reference_index(i);
    }
    void clear_reference_highlights() override {
        axis_inspected_={};
        inspected_sources_.clear();refresh_fields();
        SweepPlacementDialog::clear_reference_highlights();
    }
protected:
    bool submit() override {
        try {
            read_placement();
            if(pending.name.empty())
                throw std::invalid_argument("Zadejte název kontejneru.");
            if(derived_copy.source_ids.empty())
                throw std::invalid_argument(
                    "Vyberte alespoň jeden zdrojový prvek, těleso nebo komponentu.");
            if((!derived_copy.pattern||derived_copy.pattern->circular)&&
                    derived_copy.reference.owner_id.empty())
                throw std::invalid_argument(derived_copy.pattern?
                    "Vyberte osu Pole.":"Vyberte rovinu zrcadlení.");
            commit_(pending,derived_copy);return true;
        } catch(const std::exception& error) {
            set_status(QObject::tr(error.what()));return false;
        }
    }
private:
    std::function<void(document::HistoryContainer,
        document::DerivedCopyParameters)> commit_;
    QWidget* reference_selector_{};
    std::array<QToolButton*,3> reference_presets_{};
    QTableWidget* reference_table_{};ui::ReferenceCellItem* reference_field_{};
    QWidget* reference_indicator_{};
    QString reference_label_;
    QWidget* circular_settings_{};QDoubleSpinBox* angle_{};
    QCheckBox* full_circle_{};QTableWidget* source_table_{};
    std::vector<ui::ReferenceCellItem*> source_fields_;
    std::vector<QWidget*> source_indicators_;
    std::vector<QToolButton*> source_eyes_;
    std::map<std::string,QString> source_labels_;
    std::set<std::string> inspected_sources_;
    QTableWidget* linear_table_{};QLabel* linear_help_{};
    std::array<ui::ReferenceCellItem*,3> axis_fields_{};
    std::array<QWidget*,3> axis_indicators_{};
    std::array<QToolButton*,3> axis_eyes_{};
    std::array<bool,3> axis_inspected_{};
    QLabel* status_{};int active_{-1};
    void notify(){if(changed)changed();}
    void install_pattern_mode() {
        auto* form=new QFormLayout;
        auto* mode=new QComboBox(this);mode->setObjectName("patternMode");
        mode->addItems({tr("Lineární"),tr("Kruhové")});
        mode->setCurrentIndex(derived_copy.pattern->circular?1:0);
        form->addRow(tr("Druh Pole"),mode);content_layout()->addLayout(form);
        circular_settings_=new QWidget(this);
        auto* circular_form=new QFormLayout(circular_settings_);
        auto* count=new QSpinBox(this);count->setObjectName("patternCircularCount");
        count->setRange(2,1000);count->setValue(derived_copy.pattern->count);
        full_circle_=new QCheckBox(tr("Celý kruh"),this);
        full_circle_->setObjectName("patternFullCircle");
        full_circle_->setChecked(derived_copy.pattern->full_circle);
        angle_=new ui::UnitDoubleSpinBox(ui::InputQuantity::Angle,this);
        angle_->setObjectName("patternAngle");angle_->setRange(-359.999,359.999);
        angle_->setValue(derived_copy.pattern->angle_degrees);
        ui::bind_numeric_value_lock(angle_,"pattern:angle",
            derived_copy.value_locks,[this]{notify();});
        circular_form->addRow(tr("Počet včetně zdroje"),count);
        circular_form->addRow(full_circle_);
        circular_form->addRow(tr("Úhel mezi výskyty"),angle_);
        content_layout()->addWidget(circular_settings_);
        connect(mode,&QComboBox::currentIndexChanged,this,[this](int index) {
            derived_copy.pattern->circular=index==1;end_input();
            refresh_pattern();notify();
        });
        connect(count,&QSpinBox::valueChanged,this,[this](int value) {
            derived_copy.pattern->count=value;refresh_pattern();notify();
        });
        connect(angle_,&QDoubleSpinBox::valueChanged,this,[this](double value) {
            derived_copy.pattern->angle_degrees=value;notify();
        });
        connect(full_circle_,&QCheckBox::toggled,this,[this](bool checked) {
            derived_copy.pattern->full_circle=checked;refresh_pattern();notify();
        });
    }
    void install_reference_selector() {
        reference_selector_=new QWidget(this);
        auto* row=new QHBoxLayout(reference_selector_);
        row->setContentsMargins(0,0,0,0);
        row->addWidget(new QLabel(derived_copy.pattern?tr("Osa Pole"):
            tr("Rovina zrcadlení"),this));
        auto* group=new QButtonGroup(reference_selector_);
        group->setExclusive(true);
        const auto labels=derived_copy.pattern?
            std::array<QString,3>{QStringLiteral("X"),QStringLiteral("Y"),
                QStringLiteral("Z")}:
            std::array<QString,3>{QStringLiteral("XY"),QStringLiteral("YZ"),
                QStringLiteral("XZ")};
        for(int index=0;index<3;++index) {
            auto* preset=new QToolButton(reference_selector_);
            preset->setObjectName(QStringLiteral("copyReferencePreset")+
                labels[index]);
            preset->setText(labels[index]);preset->setCheckable(true);
            preset->setFixedWidth(35);preset->setStyleSheet(command_button_style());
            reference_presets_[index]=preset;group->addButton(preset,index);
            row->addWidget(preset);
            connect(preset,&QToolButton::clicked,this,[this,index] {
                const auto keys=derived_copy.pattern?
                    std::array<const char*,3>{"x","y","z"}:
                    std::array<const char*,3>{"xy","yz","xz"};
                set_plane({{},pending.container_origin.id,
                    (derived_copy.pattern?"origin:axis:":"origin:plane:")+
                        std::string(keys[index])},
                    QString::fromLatin1(keys[index]).toUpper());
            });
        }
        reference_table_=new QTableWidget(1,2,this);
        reference_table_->setObjectName("copyReferenceView");
        reference_table_->horizontalHeader()->hide();
        reference_table_->verticalHeader()->hide();
        reference_table_->setSelectionMode(QAbstractItemView::NoSelection);
        reference_table_->setEditTriggers(QAbstractItemView::NoEditTriggers);
        reference_table_->setFixedHeight(36);reference_table_->setMinimumWidth(150);
        reference_table_->setColumnWidth(0,32);
        reference_table_->horizontalHeader()->setSectionResizeMode(1,QHeaderView::Stretch);
        ui::install_reference_cell_delegate(reference_table_);
        reference_field_=new ui::ReferenceCellItem;
        reference_table_->setItem(0,1,reference_field_);
        // The Mirror plane / circular Pattern axis is required and
        // replaceable. Keep the shared entry arrow instead of exposing a
        // remove action that would leave the command incomplete.
        reference_indicator_=ui::build_reference_row_indicator({});
        reference_table_->setCellWidget(0,0,
            ui::centered_cell_widget(reference_indicator_));
        connect(reference_table_,&QTableWidget::cellClicked,this,
            [this](int,int column) {
                if(column==1&&request_input)request_input(plane_field);
            });
        row->addWidget(reference_table_,1);content_layout()->addWidget(reference_selector_);
    }
    void install_source_table() {
        content_layout()->addWidget(new QLabel(tr("Zdroje"),this));
        source_table_=new QTableWidget(0,3,this);
        source_table_->setObjectName("mirrorReferences");
        source_table_->horizontalHeader()->hide();
        source_table_->verticalHeader()->show();
        source_table_->setSelectionMode(QAbstractItemView::NoSelection);
        source_table_->setEditTriggers(QAbstractItemView::NoEditTriggers);
        source_table_->setColumnWidth(0,32);source_table_->setColumnWidth(2,32);
        source_table_->horizontalHeader()->setSectionResizeMode(1,QHeaderView::Stretch);
        source_table_->verticalHeader()->setDefaultSectionSize(32);
        ui::install_reference_cell_delegate(source_table_);
        connect(source_table_,&QTableWidget::cellClicked,this,
            [this](int row,int column) {
                if(column==1&&request_input)
                    request_input(first_source_field+row);
            });
        ensure_source_rows();content_layout()->addWidget(source_table_);
    }
    void append_source_row() {
        const int row=source_table_->rowCount();source_table_->insertRow(row);
        source_table_->setVerticalHeaderItem(row,
            new QTableWidgetItem(QString::number(row+1)));
        auto* field=new ui::ReferenceCellItem;
        source_table_->setItem(row,1,field);source_fields_.push_back(field);
        auto* indicator=ui::build_reference_row_indicator([this,row] {
            if(row<static_cast<int>(derived_copy.source_ids.size())) {
                inspected_sources_.erase(derived_copy.source_ids[row]);
                derived_copy.source_ids.erase(derived_copy.source_ids.begin()+row);
            }
            if(active_==first_source_field+row)active_=-1;
            ensure_source_rows();refresh_fields();notify();
        });
        source_indicators_.push_back(indicator);
        source_table_->setCellWidget(row,0,
            ui::centered_cell_widget(indicator));
        auto* eye=ui::build_reference_inspection_button(false,false,
            [this,row](bool checked) {
                if(row>=static_cast<int>(derived_copy.source_ids.size()))return;
                const auto& id=derived_copy.source_ids[row];
                if(checked)inspected_sources_.insert(id);
                else inspected_sources_.erase(id);
                refresh_fields();notify();
            });
        source_eyes_.push_back(eye);
        source_table_->setCellWidget(row,2,ui::centered_cell_widget(eye));
    }
    void ensure_source_rows() {
        if(!source_table_)return;
        const int needed=std::max(3,
            static_cast<int>(derived_copy.source_ids.size())+1);
        while(source_table_->rowCount()<needed)append_source_row();
        source_table_->setMinimumHeight(3*32+8);
        source_table_->setMaximumHeight(source_table_->rowCount()*32+8);
    }
    void install_linear_directions() {
        setMinimumWidth(620);set_initial_size({680,820});
        linear_table_=new QTableWidget(3,7,this);
        linear_table_->setObjectName("patternLinearDirections");
        linear_table_->setHorizontalHeaderLabels({"",tr("Místní osa"),"",
            tr("Rozložení"),tr("Rozteč"),tr("Počet"),tr("Vzad")});
        linear_table_->setVerticalHeaderLabels({tr("Směr 1"),tr("Směr 2"),
            tr("Směr 3")});
        linear_table_->setSelectionMode(QAbstractItemView::NoSelection);
        linear_table_->setEditTriggers(QAbstractItemView::NoEditTriggers);
        ui::install_reference_cell_delegate(linear_table_);
        linear_table_->setFixedHeight(132);
        const std::array<int,7> widths{32,82,32,112,91,62,62};
        linear_table_->verticalHeader()->setDefaultSectionSize(32);
        for(int column=0;column<7;++column)
            linear_table_->setColumnWidth(column,widths[column]);
        linear_table_->horizontalHeader()->setSectionResizeMode(1,QHeaderView::Stretch);
        for(int column:{3,5,6})
            linear_table_->horizontalHeader()->setSectionResizeMode(column,QHeaderView::ResizeToContents);
        for(int row=0;row<3;++row) {
            auto& direction=derived_copy.pattern->linear[row];
            axis_fields_[row]=new ui::ReferenceCellItem;
            linear_table_->setItem(row,1,axis_fields_[row]);
            axis_indicators_[row]=ui::build_reference_row_indicator([this,row] {
                derived_copy.pattern->linear[row].local_axis=-1;
                axis_inspected_[row]=false;
                if(active_==first_axis_field+row)active_=-1;
                refresh_fields();refresh_pattern();notify();
            });
            linear_table_->setCellWidget(row,0,
                ui::centered_cell_widget(axis_indicators_[row]));
            axis_eyes_[row]=ui::build_reference_inspection_button(false,false,
                [this,row](bool checked) {
                    axis_inspected_[row]=checked;refresh_fields();notify();
                });
            linear_table_->setCellWidget(row,2,
                ui::centered_cell_widget(axis_eyes_[row]));
            auto* distribution=new QComboBox(this);
            distribution->setObjectName(QString("patternDistribution%1").arg(row));
            distribution->addItems({tr("Vpřed"),tr("Vzad"),
                tr("Oboustranně"),tr("Symetricky")});
            distribution->setCurrentIndex(static_cast<int>(direction.distribution));
            linear_table_->setCellWidget(row,3,distribution);
            auto* spacing=new ui::UnitDoubleSpinBox(ui::InputQuantity::Length,this);
            spacing->setObjectName(QString("patternSpacing%1").arg(row));
            spacing->setRange(0.001,1e6);spacing->setValue(direction.spacing);
            linear_table_->setCellWidget(row,4,spacing);
            ui::bind_numeric_value_lock(spacing,
                "pattern:spacing:"+std::to_string(row),derived_copy.value_locks,
                [this]{notify();});
            auto* count=new QSpinBox(this);
            count->setObjectName(QString("patternCount%1").arg(row));
            count->setRange(2,1000);count->setValue(direction.count);
            linear_table_->setCellWidget(row,5,count);
            auto* reverse=new QSpinBox(this);
            reverse->setObjectName(QString("patternReverseCount%1").arg(row));
            reverse->setRange(1,999);reverse->setValue(direction.reverse_count);
            linear_table_->setCellWidget(row,6,reverse);
            connect(distribution,&QComboBox::currentIndexChanged,this,
                [this,row,count](int value) {
                    auto& d=derived_copy.pattern->linear[row];
                    d.distribution=static_cast<kernel::PatternDistribution>(value);
                    if(d.distribution==kernel::PatternDistribution::Symmetric&&
                            d.count%2==0)
                        count->setValue(d.count==1000?999:d.count+1);
                    refresh_pattern();notify();
                });
            connect(spacing,&QDoubleSpinBox::valueChanged,this,
                [this,row](double value) {
                    derived_copy.pattern->linear[row].spacing=value;notify();
                });
            connect(count,&QSpinBox::valueChanged,this,[this,row](int value) {
                derived_copy.pattern->linear[row].count=value;notify();
            });
            connect(reverse,&QSpinBox::valueChanged,this,[this,row](int value) {
                derived_copy.pattern->linear[row].reverse_count=value;notify();
            });
        }
        connect(linear_table_,&QTableWidget::cellClicked,this,
            [this](int row,int column) {
                if(column==1&&request_input)
                    request_input(first_axis_field+row);
            });
        content_layout()->addWidget(linear_table_);
        linear_help_=new QLabel(tr("Vyberte jednu až tři osy počátku Pole. Počet zahrnuje zdroj; Vzad přidává další kopie u oboustranného rozložení. Symetricky používá lichý celkový počet (3, 5, 7…)."),this);
        linear_help_->setWordWrap(true);content_layout()->addWidget(linear_help_);
    }
    void refresh_pattern() {
        if(!derived_copy.pattern)return;
        const bool circular=derived_copy.pattern->circular;
        const QSignalBlocker angle_block(angle_);
        angle_->setValue(derived_copy.pattern->full_circle?
            360.0/derived_copy.pattern->count:
            derived_copy.pattern->angle_degrees);
        reference_selector_->setVisible(circular);
        circular_settings_->setVisible(circular);
        linear_table_->setVisible(!circular);linear_help_->setVisible(!circular);
        angle_->setEnabled(!derived_copy.pattern->full_circle);
        for(int row=0;row<3;++row) {
            const auto& direction=derived_copy.pattern->linear[row];
            const bool enabled=direction.local_axis>=0;
            for(int column=3;column<7;++column)
                linear_table_->cellWidget(row,column)->setEnabled(enabled&&
                    (column!=6||direction.distribution==
                        kernel::PatternDistribution::Both));
            auto* count=qobject_cast<QSpinBox*>(linear_table_->cellWidget(row,5));
            count->setSingleStep(direction.distribution==
                kernel::PatternDistribution::Symmetric?2:1);
            count->setToolTip(direction.distribution==
                kernel::PatternDistribution::Both?
                tr("Počet vpřed včetně zdroje; další kopie vzad určuje sousední pole."):
                tr("Celkový počet v tomto směru včetně zdroje."));
        }
    }
    void refresh_fields() {
        if(!reference_field_)return;
        const auto& reference=derived_copy.reference;
        const auto reference_value=reference.owner_id+reference.semantic_key;
        if(reference_value.empty()) {
            reference_field_->clear_reference();reference_field_->setText(tr("Vyberte ve View…"));
        } else {
            reference_field_->set_reference(QString::fromStdString(reference_value));
            reference_field_->setText(reference_label_.isEmpty()?
                QString::fromStdString(reference.semantic_key):reference_label_);
        }
        reference_field_->set_active_input(active_==plane_field);
        reference_field_->set_inspected(false);
        ui::set_reference_row_populated(reference_indicator_,false);
        int preset=3;
        const auto prefix=derived_copy.pattern?"origin:axis:":"origin:plane:";
        if(reference.owner_id==pending.container_origin.id&&
                reference.semantic_key.starts_with(prefix)) {
            const auto key=reference.semantic_key.substr(std::string(prefix).size());
            const auto keys=derived_copy.pattern?
                std::array<std::string,3>{"x","y","z"}:
                std::array<std::string,3>{"xy","yz","xz"};
            const auto found=std::ranges::find(keys,key);
            if(found!=keys.end())preset=static_cast<int>(found-keys.begin());
        }
        for(int index=0;index<3;++index) {
            const QSignalBlocker block(reference_presets_[index]);
            reference_presets_[index]->setChecked(index==preset);
        }
        ensure_source_rows();
        for(int row=0;row<source_table_->rowCount();++row) {
            const bool populated=row<static_cast<int>(derived_copy.source_ids.size());
            const std::string id=populated?derived_copy.source_ids[row]:std::string{};
            auto* field=source_fields_[row];
            if(!populated) {
                field->clear_reference();field->setText(tr("Vyberte zdroj ve View…"));
            } else {
                field->set_reference(QString::fromStdString(id));
                const auto label=source_labels_.find(id);
                field->setText(label==source_labels_.end()?
                    QString::fromStdString(id):label->second);
            }
            field->set_active_input(active_==first_source_field+row);
            field->set_inspected(populated&&inspected_sources_.contains(id));
            ui::set_reference_row_populated(source_indicators_[row],populated);
            const QSignalBlocker block(source_eyes_[row]);
            source_eyes_[row]->setEnabled(populated);
            source_eyes_[row]->setChecked(populated&&
                inspected_sources_.contains(id));
        }
        if(derived_copy.pattern)for(int row=0;row<3;++row) {
            const auto axis=derived_copy.pattern->linear[row].local_axis;
            const bool populated=axis>=0;
            auto* field=axis_fields_[row];
            if(!populated) {
                field->clear_reference();field->setText(tr("Vyberte…"));
            } else {
                const auto ref=input_reference(first_axis_field+row);
                field->set_reference(QString::fromStdString(
                    ref.owner_id+ref.semantic_key));
                field->setText(tr("Osa %1").arg(QChar("XYZ"[axis])));
            }
            field->set_active_input(active_==first_axis_field+row);
            field->set_inspected(populated&&axis_inspected_[row]);
            ui::set_reference_row_populated(axis_indicators_[row],populated);
            const QSignalBlocker block(axis_eyes_[row]);
            axis_eyes_[row]->setEnabled(populated);
            axis_eyes_[row]->setChecked(populated&&axis_inspected_[row]);
        }
        reference_table_->viewport()->update();
        source_table_->viewport()->update();
        if(linear_table_)linear_table_->viewport()->update();
    }
};
} // namespace zima::app
