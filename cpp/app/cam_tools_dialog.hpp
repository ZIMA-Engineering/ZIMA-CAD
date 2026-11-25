#pragma once
#include <zima/document/cam_definition.hpp>
#include <zima/kernel/stable_id.hpp>
#include <zima/ui/properties_subwindow.hpp>
#include <zima/ui/reference_cell.hpp>
#include <QCheckBox>
#include <QComboBox>
#include <QDoubleSpinBox>
#include <QEvent>
#include <QFormLayout>
#include <QHeaderView>
#include <QLabel>
#include <QLineEdit>
#include <QPushButton>
#include <QSignalBlocker>
#include <QTableWidget>
#include <QVBoxLayout>
#include <algorithm>
#include <functional>

namespace zima::app {
// One draft owns every tool edit. Switching the selected tool is presentation;
// only the shared OK action publishes the complete definition transaction.
class CamToolsDialog final : public ui::PropertiesSubWindow {
public:
    std::function<void()> import_tool;
    CamToolsDialog(document::CamDefinition definition,
        std::function<void(std::vector<cam::Tool>)> commit,QWidget* parent)
        : PropertiesSubWindow(tr("CAM tools"),parent),pending_(std::move(definition.tools)),
          operations_(std::move(definition.operations)),commit_(std::move(commit)) {
        setObjectName("camToolsDialog");setAttribute(Qt::WA_DeleteOnClose);
        auto* row=new QHBoxLayout;content_layout()->addLayout(row);
        selected_=new QComboBox(this);selected_->setObjectName("camSelectedTool");row->addWidget(selected_,1);
        add_=new QPushButton(this);add_->setObjectName("camAddTool");row->addWidget(add_);
        remove_=new QPushButton(this);remove_->setObjectName("camRemoveTool");row->addWidget(remove_);
        import_=new QPushButton(this);import_->setObjectName("camImportTool");row->addWidget(import_);
        connect(import_,&QPushButton::clicked,this,[this]{if(import_tool)import_tool();});
        connect(selected_,&QComboBox::currentIndexChanged,this,[this]{rebuild_editor();});
        connect(add_,&QPushButton::clicked,this,[this]{
            cam::Tool tool;tool.id=kernel::make_stable_id();
            for(unsigned n=1;;++n){tool.name=tr("Tool %1").arg(n).toStdString();
                if(std::ranges::none_of(pending_,[&](const auto& t){return t.name==tool.name;}))break;}
            tool.cutting={{-20,5},{0,5}};tool.non_cutting={{0,5},{40,5}};
            pending_.push_back(std::move(tool));refresh_selection(int(pending_.size())-1);
        });
        connect(remove_,&QPushButton::clicked,this,[this]{
            const auto i=selected_->currentIndex();if(i<0)return;
            const auto& id=pending_[i].id;
            for(const auto& operation:operations_) {
                if(const auto* facing=std::get_if<cam::Facing>(&operation);facing&&facing->tool_id==id)
                    return error(QT_TR_NOOP("This tool is used by a machining operation."));
                if(const auto* manual=std::get_if<document::CamManualOperation>(&operation))
                    for(const auto& block:manual->program.blocks)if(block.kind==cam::BlockKind::Tool&&block.tool_id==id)
                        return error(QT_TR_NOOP("This tool is used by a machining operation."));
            }
            pending_.erase(pending_.begin()+i);refresh_selection(std::min(i,int(pending_.size())-1));
        });
        status_=new QLabel(this);status_->setObjectName("camToolsStatus");status_->setWordWrap(true);
        content_layout()->addWidget(status_);refresh_selection(pending_.empty()?-1:0);
        set_initial_size({700,760});
    }
    const std::vector<cam::Tool>& pending_value()const{return pending_;}
    void append_tool(cam::Tool tool){cam::validate(tool);pending_.push_back(std::move(tool));refresh_selection(int(pending_.size())-1);}
protected:
    bool submit()override {
        try {
            for(const auto& tool:pending_) {
                if(QString::fromStdString(tool.name).trimmed().isEmpty())throw std::invalid_argument("Specify a nonempty object name.");
                cam::validate(tool);
            }
            commit_(pending_);return true;
        }catch(const cam::ProgramError&){error(QT_TR_NOOP("The tool needs valid cutting and shank profiles, a positive feed and a positive spindle speed."));return false;}
        catch(const std::exception& e){error(e.what());return false;}
    }
    void changeEvent(QEvent* event)override {
        PropertiesSubWindow::changeEvent(event);
        if(event->type()==QEvent::LanguageChange&&status_){rebuild_editor();if(!error_.empty())status_->setText(tr(error_.c_str()));}
    }
private:
    std::vector<cam::Tool> pending_;std::vector<document::CamOperation> operations_;
    std::function<void(std::vector<cam::Tool>)> commit_;
    QComboBox* selected_{};QPushButton *add_{},*remove_{},*import_{};QWidget* editor_{};QLabel* status_{};
    std::string error_;
    void error(const char* key){error_=key;status_->setText(tr(key));}
    void clear_status(){error_.clear();status_->clear();}
    void refresh_selection(int current) {
        {const QSignalBlocker block(selected_);selected_->clear();
            for(const auto& tool:pending_)selected_->addItem(QString::fromStdString(tool.name),QString::fromStdString(tool.id));
            selected_->setCurrentIndex(current);}
        clear_status();rebuild_editor();
    }
    QDoubleSpinBox* number(QWidget* parent,const QString& name,double value,double low,double high,
        const QString& suffix,std::function<void(double)> changed) {
        auto* spin=new QDoubleSpinBox(parent);spin->setObjectName(name);spin->setDecimals(6);
        spin->setRange(low,high);spin->setSuffix(suffix);spin->setValue(value);
        // Connect after initialization so opening and unchanged OK retain the
        // complete authored precision, not the six displayed decimal places.
        connect(spin,&QDoubleSpinBox::valueChanged,this,[this,changed=std::move(changed)](double n){changed(n);clear_status();});
        return spin;
    }
    void profile(QVBoxLayout* layout,int tool_index,bool cutting) {
        const auto prefix=cutting?QStringLiteral("camCutting"):QStringLiteral("camShank");
        auto* title=new QLabel(cutting?tr("Cutting profile"):tr("Shank profile"),editor_);layout->addWidget(title);
        auto* table=new QTableWidget(0,4,editor_);table->setObjectName(prefix+"Profile");
        table->verticalHeader()->hide();table->verticalHeader()->setDefaultSectionSize(32);
        table->setHorizontalHeaderLabels({"#",{},tr("Axial position Z"),tr("Poloměr")});
        table->setEditTriggers(QAbstractItemView::NoEditTriggers);table->setSelectionBehavior(QAbstractItemView::SelectRows);
        table->setSelectionMode(QAbstractItemView::SingleSelection);
        for(int c:{0,1}){table->setColumnWidth(c,32);table->horizontalHeader()->setSectionResizeMode(c,QHeaderView::Fixed);}
        for(int c:{2,3})table->horizontalHeader()->setSectionResizeMode(c,QHeaderView::ResizeToContents);
        const auto& stations=cutting?pending_[tool_index].cutting:pending_[tool_index].non_cutting;
        table->setRowCount(int(stations.size()));
        for(int row=0;row<int(stations.size());++row) {
            table->setItem(row,0,new QTableWidgetItem(QString::number(row+1)));
            const bool boundary=cutting?row==int(stations.size())-1:row==0;
            if(!boundary&&stations.size()>2) {
                auto* remove=ui::build_reference_row_indicator([this,tool_index,cutting,row]{
                    auto& values=cutting?pending_[tool_index].cutting:pending_[tool_index].non_cutting;
                    values.erase(values.begin()+row);clear_status();rebuild_editor();
                });
                ui::set_reference_row_populated(remove,true);remove->setObjectName(prefix+"Remove"+QString::number(row));
                remove->setToolTip(tr("Remove this profile station."));table->setCellWidget(row,1,remove);
            }
            auto* z=number(table,prefix+"Z"+QString::number(row),stations[row].z_mm,cutting?-1e9:0,cutting?0:1e9," mm",
                [this,tool_index,cutting,row](double n){(cutting?pending_[tool_index].cutting:pending_[tool_index].non_cutting)[row].z_mm=n;});
            z->setReadOnly(boundary);table->setCellWidget(row,2,z);
            table->setCellWidget(row,3,number(table,prefix+"Radius"+QString::number(row),stations[row].radius_mm,0,1e9," mm",
                [this,tool_index,cutting,row](double n){(cutting?pending_[tool_index].cutting:pending_[tool_index].non_cutting)[row].radius_mm=n;}));
        }
        table->setMinimumHeight(132);table->setMaximumHeight(196);layout->addWidget(table);
        auto* add=new QPushButton(tr("Add profile station"),editor_);add->setObjectName(prefix+"AddStation");layout->addWidget(add,0,Qt::AlignLeft);
        connect(add,&QPushButton::clicked,this,[this,tool_index,cutting,table]{
            auto& values=cutting?pending_[tool_index].cutting:pending_[tool_index].non_cutting;
            if(values.size()<2)return;
            const auto at=std::clamp(table->currentRow()+1,1,int(values.size())-1);
            const cam::ToolStation midpoint{(values[at-1].z_mm+values[at].z_mm)/2,(values[at-1].radius_mm+values[at].radius_mm)/2};
            values.insert(values.begin()+at,midpoint);clear_status();rebuild_editor();
        });
    }
    void rebuild_editor() {
        set_internal_title(tr("CAM tools"));add_->setText(tr("Add tool"));remove_->setText(tr("Remove tool"));import_->setText(tr("Import from library…"));
        const auto i=selected_->currentIndex();remove_->setEnabled(i>=0);
        if(editor_){content_layout()->removeWidget(editor_);editor_->hide();editor_->deleteLater();editor_=nullptr;}
        if(i<0)return;
        editor_=new QWidget(this);auto* layout=new QVBoxLayout(editor_);layout->setContentsMargins(0,0,0,0);
        content_layout()->insertWidget(1,editor_);
        auto* form=new QFormLayout;form->setFieldGrowthPolicy(QFormLayout::FieldsStayAtSizeHint);layout->addLayout(form);
        auto* name=new QLineEdit(QString::fromStdString(pending_[i].name),editor_);name->setObjectName("camToolName");form->addRow(tr("Název"),name);
        connect(name,&QLineEdit::textChanged,this,[this,i](const auto& value){pending_[i].name=value.toStdString();selected_->setItemText(i,value);clear_status();});
        form->addRow(tr("Cutting feed"),number(editor_,"camToolFeed",pending_[i].feed_mm_min,.000001,1e9," mm/min",[this,i](double n){pending_[i].feed_mm_min=n;}));
        form->addRow(tr("Spindle speed"),number(editor_,"camToolSpindle",pending_[i].spindle_rpm,.000001,1e9," min⁻¹",[this,i](double n){pending_[i].spindle_rpm=n;}));
        auto* clockwise=new QCheckBox(tr("Clockwise spindle"),editor_);clockwise->setObjectName("camToolClockwise");clockwise->setChecked(pending_[i].clockwise);form->addRow(clockwise);
        connect(clockwise,&QCheckBox::toggled,this,[this,i](bool on){pending_[i].clockwise=on;clear_status();});
        profile(layout,i,true);profile(layout,i,false);
        auto* help=new QLabel(tr("Z = 0 is the cutting/shank interface. Cutting stations run from negative Z to zero; shank stations run from zero to positive Z. Stations define exact conical or cylindrical portions."),editor_);
        help->setWordWrap(true);layout->addWidget(help);
    }
};
} // namespace zima::app
