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
// All editors write this private definition. Picking an editable field and
// selecting an item for ordering are deliberately independent actions.
class CamOperationsDialog final : public ui::PropertiesSubWindow {
public:
    CamOperationsDialog(document::CamDefinition definition,
        std::function<void(std::vector<document::CamOperation>)> commit,QWidget* parent)
        : PropertiesSubWindow(tr("Machining operations"),parent),pending_(std::move(definition)),commit_(std::move(commit)) {
        setObjectName("camOperationsDialog");setAttribute(Qt::WA_DeleteOnClose);
        list_=table("camOperations",this);content_layout()->addWidget(list_);
        connect(list_,&QTableWidget::cellClicked,this,[this](int row,int col){if(col==3){editing_=document::cam_operation_id(pending_.operations.at(row));editing_block_.clear();rebuild_editor();}});
        auto* actions=new QHBoxLayout;content_layout()->addLayout(actions);
        facing_=new QPushButton(this);facing_->setObjectName("camAddFacing");actions->addWidget(facing_);
        manual_=new QPushButton(this);manual_->setObjectName("camAddManual");actions->addWidget(manual_);actions->addStretch();
        up_=new QPushButton(ui::reference_arrow_icon(Qt::UpArrow),{},this);up_->setObjectName("camOperationUp");actions->addWidget(up_);
        down_=new QPushButton(ui::reference_arrow_icon(Qt::DownArrow),{},this);down_->setObjectName("camOperationDown");actions->addWidget(down_);
        connect(up_,&QPushButton::clicked,this,[this]{move_operation(-1);});connect(down_,&QPushButton::clicked,this,[this]{move_operation(1);});
        connect(facing_,&QPushButton::clicked,this,[this]{cam::Facing value;value.id=kernel::make_stable_id();value.name=tr("Facing").toStdString();
            if(!pending_.tools.empty())value.tool_id=pending_.tools.front().id;editing_=value.id;pending_.operations.emplace_back(std::move(value));refresh();});
        connect(manual_,&QPushButton::clicked,this,[this]{document::CamManualOperation value;value.id=kernel::make_stable_id();value.name=tr("Manual operation").toStdString();
            value.program.display_units=pending_.display_units;editing_=value.id;pending_.operations.emplace_back(std::move(value));refresh();});
        status_=new QLabel(this);status_->setObjectName("camOperationsStatus");status_->setWordWrap(true);content_layout()->addWidget(status_);
        refresh();set_initial_size({780,0});
    }
    const auto& pending_value()const{return pending_.operations;}
protected:
    bool submit()override {
        try{document::validate_cam_definition(pending_);commit_(pending_.operations);return true;}
        catch(const std::exception& e){error_=e.what();status_->setText(tr(error_.c_str()));return false;}
    }
    void changeEvent(QEvent* e)override{PropertiesSubWindow::changeEvent(e);if(e->type()==QEvent::LanguageChange&&status_)refresh();}
private:
    document::CamDefinition pending_;
    std::function<void(std::vector<document::CamOperation>)> commit_;
    QTableWidget* list_{};QPushButton *facing_{},*manual_{},*up_{},*down_{};QLabel* status_{};QWidget* editor_{};
    std::string ordered_,editing_,ordered_block_,editing_block_,error_;
    QTableWidget* table(const char* name,QWidget* parent) {
        auto* w=new QTableWidget(0,4,parent);w->setObjectName(name);w->verticalHeader()->hide();w->verticalHeader()->setDefaultSectionSize(32);
        w->setSelectionMode(QAbstractItemView::NoSelection);w->setEditTriggers(QAbstractItemView::NoEditTriggers);
        for(int c:{0,1,2}){w->setColumnWidth(c,32);w->horizontalHeader()->setSectionResizeMode(c,QHeaderView::Fixed);}
        w->horizontalHeader()->setSectionResizeMode(3,QHeaderView::Stretch);return w;
    }
    void clear_status(){error_.clear();status_->clear();}
    int operation_index(const std::string& id)const{for(int i=0;i<int(pending_.operations.size());++i)if(document::cam_operation_id(pending_.operations[i])==id)return i;return -1;}
    static int block_index(const cam::Program& p,const std::string& id){for(int i=0;i<int(p.blocks.size());++i)if(p.blocks[i].id==id)return i;return -1;}
    void row_controls(QTableWidget* table,int row,const std::string& id,const std::string& selected,
        std::function<void(bool)> choose,std::function<void()> remove,const QString& tooltip) {
        table->setItem(row,0,new QTableWidgetItem(QString::number(row+1)));
        auto* order=new QCheckBox(table);order->setObjectName(table->objectName()+"Order"+QString::number(row));order->setChecked(id==selected);
        connect(order,&QCheckBox::toggled,this,std::move(choose));table->setCellWidget(row,1,ui::centered_cell_widget(order));
        auto* indicator=ui::build_reference_row_indicator(std::move(remove));ui::set_reference_row_populated(indicator,true);
        indicator->setObjectName(table->objectName()+"Remove"+QString::number(row));indicator->setToolTip(tooltip);table->setCellWidget(row,2,indicator);
    }
    void refresh_list() {
        list_->setHorizontalHeaderLabels({"#",{},{},tr("Operation")});list_->setRowCount(0);list_->setRowCount(int(pending_.operations.size()));
        for(int i=0;i<int(pending_.operations.size());++i){const auto id=document::cam_operation_id(pending_.operations[i]);
            row_controls(list_,i,id,ordered_,[this,id](bool on){ordered_=on?id:std::string{};refresh_list();},[this,id]{
                const auto i=operation_index(id);if(i<0)return;pending_.operations.erase(pending_.operations.begin()+i);
                if(editing_==id)editing_.clear();if(ordered_==id)ordered_.clear();refresh();},tr("Remove this machining operation."));
            list_->setItem(i,3,new QTableWidgetItem(QString::fromStdString(document::cam_operation_name(pending_.operations[i]))));}
        list_->setFixedHeight(32*std::clamp(int(pending_.operations.size()),2,4)+list_->horizontalHeader()->height()+4);
        const auto selected=operation_index(ordered_);up_->setEnabled(selected>0);down_->setEnabled(selected>=0&&selected+1<int(pending_.operations.size()));
    }
    void move_operation(int direction){const auto at=operation_index(ordered_),to=at+direction;if(at<0||to<0||to>=int(pending_.operations.size()))return;
        std::swap(pending_.operations[at],pending_.operations[to]);clear_status();refresh_list();rebuild_editor();}
    QDoubleSpinBox* number(QWidget* parent,const QString& name,double value,double low,double high,const QString& suffix,std::function<void(double)> changed){
        auto* spin=new QDoubleSpinBox(parent);spin->setObjectName(name);spin->setDecimals(6);spin->setRange(low,high);spin->setSuffix(suffix);spin->setValue(value);
        // Preserve authored precision on opening, language changes and no-op OK.
        connect(spin,&QDoubleSpinBox::valueChanged,this,[this,changed=std::move(changed)](double n){changed(n);clear_status();});return spin;
    }
    void point(QFormLayout* form,const QString& label,const QString& prefix,cam::Point value,std::function<void(int,double)> changed) {
        auto* row=new QWidget(editor_);auto* layout=new QHBoxLayout(row);layout->setContentsMargins(0,0,0,0);
        const double values[]{value.x,value.y,value.z};for(int i=0;i<3;++i){auto* field=number(row,prefix+QChar('X'+i),values[i],-1e9,1e9," mm",[changed,i](double n){changed(i,n);});field->setPrefix(QString(QChar('X'+i))+" ");layout->addWidget(field);}
        form->addRow(label,row);
    }
    static void coordinate(cam::Point& p,int index,double value){(index==0?p.x:index==1?p.y:p.z)=value;}
    QComboBox* tools(const QString& name,const std::string& current,std::function<void(std::string)> changed){
        auto* combo=new QComboBox(editor_);combo->setObjectName(name);for(const auto& tool:pending_.tools)combo->addItem(QString::fromStdString(tool.name),QString::fromStdString(tool.id));
        int at=combo->findData(QString::fromStdString(current));if(at<0&&!current.empty()){combo->addItem(QString::fromStdString(current),QString::fromStdString(current));at=combo->count()-1;}combo->setCurrentIndex(at);
        connect(combo,&QComboBox::currentIndexChanged,this,[this,combo,changed=std::move(changed)](int i){if(i>=0){changed(combo->itemData(i).toString().toStdString());clear_status();}});return combo;
    }
    void refresh(){set_internal_title(tr("Machining operations"));facing_->setText(tr("Add facing"));manual_->setText(tr("Add manual operation"));up_->setText(tr("Nahoru"));down_->setText(tr("Dolů"));
        refresh_list();rebuild_editor();if(!error_.empty())status_->setText(tr(error_.c_str()));}
    void rebuild_editor() {
        if(editor_){content_layout()->removeWidget(editor_);editor_->hide();editor_->deleteLater();editor_=nullptr;}
        const auto at=operation_index(editing_);if(at<0)return;
        editor_=new QWidget(this);auto* layout=new QVBoxLayout(editor_);layout->setContentsMargins(0,0,0,0);content_layout()->insertWidget(2,editor_);
        auto* form=new QFormLayout;layout->addLayout(form);auto* name=new QLineEdit(QString::fromStdString(document::cam_operation_name(pending_.operations[at])),editor_);name->setObjectName("camOperationName");form->addRow(tr("Název"),name);
        connect(name,&QLineEdit::textChanged,this,[this,at](const auto& text){std::visit([&](auto& o){o.name=text.toStdString();},pending_.operations[at]);refresh_list();clear_status();});
        if(auto* f=std::get_if<cam::Facing>(&pending_.operations[at])) {
            form->addRow(tr("Tool"),tools("camFacingTool",f->tool_id,[this,at](auto id){std::get<cam::Facing>(pending_.operations[at]).tool_id=std::move(id);}));
            const char* keys[]{QT_TR_NOOP("Step down"),QT_TR_NOOP("Step over"),QT_TR_NOOP("Clearance"),QT_TR_NOOP("Stock allowance")};
            const char* names[]{"camFacingStepDown","camFacingStepOver","camFacingClearance","camFacingAllowance"};
            double cam::Facing::* members[]{&cam::Facing::step_down_mm,&cam::Facing::step_over_mm,&cam::Facing::clearance_mm,&cam::Facing::allowance_mm};
            for(int i=0;i<4;++i)form->addRow(tr(keys[i]),number(editor_,names[i],f->*members[i],i==3?0:.000001,1e9," mm",[this,at,member=members[i]](double n){std::get<cam::Facing>(pending_.operations[at]).*member=n;}));
            return;
        }
        auto& manual=std::get<document::CamManualOperation>(pending_.operations[at]);
        point(form,tr("Initial tool tip"),"camInitialTip",manual.program.initial_tip,[this,at](int i,double n){coordinate(std::get<document::CamManualOperation>(pending_.operations[at]).program.initial_tip,i,n);});
        auto* blocks=table("camBlocks",editor_);blocks->setHorizontalHeaderLabels({"#",{},{},tr("NC block")});layout->addWidget(blocks);
        blocks->setRowCount(int(manual.program.blocks.size()));
        for(int i=0;i<int(manual.program.blocks.size());++i){const auto block=manual.program.blocks[i];
            row_controls(blocks,i,block.id,ordered_block_,[this,id=block.id](bool on){ordered_block_=on?id:std::string{};rebuild_editor();},[this,at,id=block.id]{auto& p=std::get<document::CamManualOperation>(pending_.operations[at]).program;
                const auto index=block_index(p,id);if(index>=0)p.blocks.erase(p.blocks.begin()+index);if(editing_block_==id)editing_block_.clear();if(ordered_block_==id)ordered_block_.clear();rebuild_editor();clear_status();},tr("Remove this NC block."));
            blocks->setItem(i,3,new QTableWidgetItem(block_label(block)));}
        blocks->setFixedHeight(32*std::clamp(int(manual.program.blocks.size()),2,4)+blocks->horizontalHeader()->height()+4);
        connect(blocks,&QTableWidget::cellClicked,this,[this,at](int row,int col){if(col==3){editing_block_=std::get<document::CamManualOperation>(pending_.operations[at]).program.blocks.at(row).id;rebuild_editor();}});
        auto* controls=new QHBoxLayout;layout->addLayout(controls);auto* add=new QPushButton(tr("Add NC block"),editor_);add->setObjectName("camAddBlock");controls->addWidget(add);controls->addStretch();
        connect(add,&QPushButton::clicked,this,[this,at]{auto& op=std::get<document::CamManualOperation>(pending_.operations[at]);cam::Block block;block.id=kernel::make_stable_id();block.operation_id=op.id;
            block.end=op.program.initial_tip;for(const auto& previous:op.program.blocks)if(previous.kind==cam::BlockKind::Motion)block.end=previous.end;
            editing_block_=block.id;op.program.blocks.push_back(std::move(block));rebuild_editor();clear_status();});
        const auto selected=block_index(manual.program,ordered_block_);
        for(int direction:{-1,1}){auto* button=new QPushButton(ui::reference_arrow_icon(direction<0?Qt::UpArrow:Qt::DownArrow),direction<0?tr("Nahoru"):tr("Dolů"),editor_);
            button->setObjectName(direction<0?"camBlockUp":"camBlockDown");button->setEnabled(selected>=0&&selected+direction>=0&&selected+direction<int(manual.program.blocks.size()));controls->addWidget(button);
            connect(button,&QPushButton::clicked,this,[this,at,direction]{auto& p=std::get<document::CamManualOperation>(pending_.operations[at]).program;const auto i=block_index(p,ordered_block_),j=i+direction;
                if(i>=0&&j>=0&&j<int(p.blocks.size()))std::swap(p.blocks[i],p.blocks[j]);rebuild_editor();clear_status();});}
        const auto index=block_index(manual.program,editing_block_);if(index>=0)block_editor(layout,at,index);
    }
    static QString block_label(const cam::Block& b){switch(b.kind){case cam::BlockKind::Tool:return tr("Tool change");case cam::BlockKind::Spindle:return tr("Spindle speed");case cam::BlockKind::Feed:return tr("Cutting feed");case cam::BlockKind::Stop:return tr("Stop");case cam::BlockKind::Motion:
        switch(b.motion){case cam::MotionKind::Rapid:return tr("Rapid move");case cam::MotionKind::Linear:return tr("Linear cut");case cam::MotionKind::ArcClockwise:return tr("Clockwise XY arc");case cam::MotionKind::ArcCounterclockwise:return tr("Counterclockwise XY arc");}}return {};}
    void block_editor(QVBoxLayout* layout,int operation,int index) {
        const auto access=[this,operation,index]() -> cam::Block& {return std::get<document::CamManualOperation>(pending_.operations[operation]).program.blocks[index];};
        const auto block=access();auto* form=new QFormLayout;layout->addLayout(form);auto* kind=new QComboBox(editor_);kind->setObjectName("camBlockKind");
        const cam::BlockKind kinds[]{cam::BlockKind::Tool,cam::BlockKind::Spindle,cam::BlockKind::Feed,cam::BlockKind::Motion,cam::BlockKind::Stop};
        for(auto k:kinds){auto b=block;b.kind=k;kind->addItem(k==cam::BlockKind::Motion?tr("Motion"):block_label(b),int(k));}kind->setCurrentIndex(kind->findData(int(block.kind)));form->addRow(tr("Typ"),kind);
        connect(kind,&QComboBox::currentIndexChanged,this,[this,kind,access](int i){auto& b=access();b.kind=cam::BlockKind(kind->itemData(i).toInt());
            if(b.kind==cam::BlockKind::Tool&&b.tool_id.empty()&&!pending_.tools.empty())b.tool_id=pending_.tools.front().id;
            if(b.value<=0&&(b.kind==cam::BlockKind::Feed||b.kind==cam::BlockKind::Spindle))b.value=b.kind==cam::BlockKind::Feed?300:6000;clear_status();rebuild_editor();});
        if(block.kind==cam::BlockKind::Tool)form->addRow(tr("Tool"),tools("camBlockTool",block.tool_id,[access](auto id){access().tool_id=std::move(id);}));
        if(block.kind==cam::BlockKind::Feed||block.kind==cam::BlockKind::Spindle)form->addRow(tr("Hodnota"),number(editor_,"camBlockValue",block.value,.000001,1e9,block.kind==cam::BlockKind::Feed?" mm/min":" min⁻¹",[access](double n){access().value=n;}));
        if(block.kind==cam::BlockKind::Spindle){auto* clockwise=new QCheckBox(tr("Clockwise spindle"),editor_);clockwise->setObjectName("camBlockClockwise");clockwise->setChecked(block.clockwise);form->addRow(clockwise);connect(clockwise,&QCheckBox::toggled,this,[this,access](bool on){access().clockwise=on;clear_status();});}
        if(block.kind!=cam::BlockKind::Motion)return;
        auto* motion=new QComboBox(editor_);motion->setObjectName("camBlockMotion");for(const auto k:{cam::MotionKind::Rapid,cam::MotionKind::Linear,cam::MotionKind::ArcClockwise,cam::MotionKind::ArcCounterclockwise}){auto b=block;b.motion=k;motion->addItem(block_label(b),int(k));}
        motion->setCurrentIndex(motion->findData(int(block.motion)));form->addRow(tr("Motion"),motion);
        connect(motion,&QComboBox::currentIndexChanged,this,[this,motion,access](int i){access().motion=cam::MotionKind(motion->itemData(i).toInt());clear_status();rebuild_editor();});
        point(form,tr("End tool tip"),"camBlockEnd",block.end,[access](int i,double n){coordinate(access().end,i,n);});
        if(block.motion==cam::MotionKind::ArcClockwise||block.motion==cam::MotionKind::ArcCounterclockwise)point(form,tr("Arc center"),"camBlockCenter",block.center,[access](int i,double n){coordinate(access().center,i,n);});
    }
};
} // namespace zima::app
