#pragma once
#include <zima/document/feature_parameters.hpp>
#include <zima/ui/reference_cell.hpp>
#include <QTableWidget>
#include <QHeaderView>
#include <QVBoxLayout>
#include <QHBoxLayout>
#include <QPushButton>
#include <QComboBox>
#include <QToolButton>
#include <QSignalBlocker>
#include <QEvent>
#include <QStyle>
#include <functional>

namespace zima::app {
// The Feature owns its draft data. This widget edits only that side's ordered
// references/retained relationships; entry and viewer publication stay with the
// existing dialog host. Inspection and ordering selection are independent.
class FeatureLimitTable final : public QWidget {
public:
    FeatureLimitTable(document::FeatureSideParameters& side,QWidget* parent)
        :QWidget(parent),side_(side) {
        auto* layout=new QVBoxLayout(this);layout->setContentsMargins(0,0,0,0);layout->setSpacing(4);
        table_=new QTableWidget(this);table_->setColumnCount(7);
        headers();
        table_->verticalHeader()->hide();table_->verticalHeader()->setMinimumSectionSize(34);
        table_->verticalHeader()->setDefaultSectionSize(34);table_->setEditTriggers(QAbstractItemView::NoEditTriggers);
        for(int column:{0,1,2,4,5,6})table_->horizontalHeader()->setSectionResizeMode(column,QHeaderView::Fixed);
        table_->setColumnWidth(0,26);table_->setColumnWidth(1,28);table_->setColumnWidth(2,34);
        table_->setColumnWidth(4,42);table_->setColumnWidth(5,42);table_->setColumnWidth(6,32);
        table_->horizontalHeader()->setSectionResizeMode(3,QHeaderView::Stretch);
        table_->setMinimumWidth(26+28+34+42+42+32+120+
            table_->style()->pixelMetric(QStyle::PM_ScrollBarExtent)+2*table_->frameWidth());
        table_->setMinimumHeight(3*34+table_->horizontalHeader()->sizeHint().height()+2*table_->frameWidth());
        table_->setSizePolicy(QSizePolicy::Expanding,QSizePolicy::Expanding);
        ui::install_reference_cell_delegate(table_);layout->addWidget(table_,1);
        buttons_=new QWidget(this);auto* actions=new QHBoxLayout(buttons_);actions->setContentsMargins(0,0,0,0);
        up_=new QPushButton(ui::reference_arrow_icon(Qt::UpArrow),tr("Nahoru"),buttons_);
        down_=new QPushButton(ui::reference_arrow_icon(Qt::DownArrow),tr("Dolů"),buttons_);
        up_->setObjectName("featureLimitUp");down_->setObjectName("featureLimitDown");
        actions->addWidget(up_);actions->addWidget(down_);actions->addStretch();layout->addWidget(buttons_);
        connect(up_,&QPushButton::clicked,this,[this]{move(-1);});connect(down_,&QPushButton::clicked,this,[this]{move(1);});
        connect(table_,&QTableWidget::cellClicked,this,[this](int row,int column){if(column==3)begin_entry(row);});
        connect(table_,&QTableWidget::itemChanged,this,[this](QTableWidgetItem* item){
            if(item->column()!=0||item->row()>=static_cast<int>(side_.targets.size()))return;
            const auto source=side_.targets[item->row()].reference;
            if(item->checkState()==Qt::Checked)ordering_=source;
            else if(ordering_&&*ordering_==source)ordering_.reset();
            refresh_states();
        });
        hide();refresh_states();
    }
    QSize sizeHint() const override {
        if(!table_||!buttons_)return QWidget::sizeHint();
        auto size=QWidget::sizeHint();size.setHeight(table_->minimumHeight()+buttons_->sizeHint().height()+layout()->spacing());return size;
    }
    QTableWidget* table() const {return table_;}
    std::optional<std::size_t> active_row() const {return active_;}
    void on_pick(std::function<void()> callback){pick_=std::move(callback);}
    void on_change(std::function<void()> callback){changed_=std::move(callback);}
    void on_inspection(std::function<void()> callback){inspection_=std::move(callback);}
    void begin_entry(std::size_t row) {
        active_=std::min(row,side_.targets.size());refresh_states();if(pick_)pick_();
    }
    void finish_entry(bool clear_inspection=false) {
        active_.reset();const bool cleared=clear_inspection&&!inspected_.empty();
        if(clear_inspection)inspected_.clear();refresh_states();if(cleared&&inspection_)inspection_();
    }
    const std::vector<kernel::FaceReference>& inspected() const {return inspected_;}
    void assign(document::ExtrusionParameters::EndTarget target,bool notify=true) {
        const auto row=std::min(active_.value_or(0),side_.targets.size());
        for(std::size_t i=0;i<side_.targets.size();++i)if(i!=row&&side_.targets[i].reference==target.reference)
            throw std::invalid_argument("Source is already present in the Up To chain.");
        const auto count=side_.targets.size()+(row==side_.targets.size());
        if(count>1&&(target.kind==document::EndTargetKind::Point||std::ranges::any_of(side_.targets,[&](const auto& source){
            return &source!= (row<side_.targets.size()?&side_.targets[row]:nullptr)&&source.kind==document::EndTargetKind::Point;})))
            throw std::invalid_argument("Multiple Up To limits require faces or planes.");
        if(count>1&&side_.targets.size()==1&&!side_.up_to.primary_cap_source)
            side_.up_to.primary_cap_source=side_.targets.front().reference;
        if(row==side_.targets.size())side_.targets.push_back(std::move(target));else side_.targets[row]=std::move(target);
        document::prune_feature_up_to_patch_choices(side_);active_.reset();refresh();
        if(notify&&changed_)changed_();if(inspection_)inspection_();
    }
    void refresh() {
        if(isHidden()){dirty_=true;return;}
        dirty_=false;
        const QSignalBlocker blocked(table_);eyes_.clear();table_->clearContents();
        const auto count=side_.targets.size();table_->setRowCount(static_cast<int>(std::max<std::size_t>(3,count+1)));
        const auto present=[&](const kernel::FaceReference& ref){return std::ranges::any_of(side_.targets,[&](const auto& t){return t.reference==ref;});};
        if(ordering_&&!present(*ordering_))ordering_.reset();std::erase_if(inspected_,[&](const auto& ref){return !present(ref);});
        for(int row=0;row<table_->rowCount();++row) {
            const bool populated=static_cast<std::size_t>(row)<count;
            auto* order=new QTableWidgetItem;order->setFlags(populated?Qt::ItemIsEnabled|Qt::ItemIsUserCheckable:Qt::NoItemFlags);
            order->setCheckState(Qt::Unchecked);order->setToolTip(tr("Vybrat položku pro změnu pořadí"));table_->setItem(row,0,order);
            auto* number=new QTableWidgetItem(QString::number(row+1));number->setFlags(Qt::ItemIsEnabled);number->setTextAlignment(Qt::AlignCenter);table_->setItem(row,1,number);
            const kernel::FaceReference source=populated?side_.targets[row].reference:kernel::FaceReference{};
            auto* indicator=ui::build_reference_row_indicator([this,source]{remove(source);});ui::set_reference_row_populated(indicator,populated);
            if(auto* remove=indicator->findChild<QPushButton*>())remove->setToolTip(tr("Odstranit cílovou referenci a její řádek"));
            table_->setCellWidget(row,2,ui::centered_cell_widget(indicator));
            auto* field=new ui::ReferenceCellItem(populated?QString::fromStdString(side_.targets[row].label.empty()?
                source.owner_id+" / "+source.semantic_key:side_.targets[row].label):tr("Vyberte referenci"));
            if(populated){field->set_reference(QString::fromStdString(source.owner_id+" / "+source.semantic_key));field->setToolTip(field->text()+"\n"+QString::fromStdString(source.instance_path));}
            table_->setItem(row,3,field);
            for(bool next:{false,true}) {
                const int column=next?5:4;const bool adjacent=populated&&(next?static_cast<std::size_t>(row+1)<count:row>0);
                auto* choice=new QComboBox(table_);choice->addItems({QStringLiteral("A"),QStringLiteral("+"),QStringLiteral("−")});choice->setFixedWidth(40);
                choice->setProperty("featureLimitChoice",true);
                choice->setItemData(0,tr("Automaticky"),Qt::ToolTipRole);choice->setItemData(1,tr("Kladná strana sousední plochy"),Qt::ToolTipRole);
                choice->setItemData(2,tr("Záporná strana sousední plochy"),Qt::ToolTipRole);
                choice->setToolTip(next?tr("Ponechat část vůči následující ploše; A: automaticky"):tr("Ponechat část vůči předchozí ploše; A: automaticky"));
                choice->setEnabled(adjacent);
                if(adjacent) {
                    const auto neighbour=side_.targets[next?row+1:row-1].reference;const auto kept=side_.up_to.retained_side(source,neighbour);
                    choice->setCurrentIndex(kept?(*kept?1:2):0);
                    connect(choice,&QComboBox::currentIndexChanged,this,[this,source,neighbour](int value){
                        if(value==0)std::erase_if(side_.up_to.retained_patches,[&](const auto& p){return p.source==source&&p.adjacent==neighbour;});
                        else side_.up_to.set_retained_side(source,neighbour,value==1);
                        if(changed_)changed_();
                    });
                }
                table_->setCellWidget(row,column,ui::centered_cell_widget(choice));
            }
            auto* eye=ui::build_reference_inspection_button(populated,false,[this,source](bool on){
                std::erase(inspected_,source);if(on)inspected_.push_back(source);refresh_states();if(inspection_)inspection_();
            });eyes_.push_back(eye);table_->setCellWidget(row,6,ui::centered_cell_widget(eye));
        }
        refresh_states();
    }
protected:
    void showEvent(QShowEvent* event) override {
        QWidget::showEvent(event);if(dirty_)refresh();
    }
    void changeEvent(QEvent* event) override {
        QWidget::changeEvent(event);
        if(event->type()==QEvent::LanguageChange&&table_&&up_&&down_) {
            up_->setText(tr("Nahoru"));down_->setText(tr("Dolů"));headers();refresh();
        }
    }
private:
    document::FeatureSideParameters& side_;
    QTableWidget* table_{};QWidget* buttons_{};QPushButton *up_{},*down_{};
    std::optional<std::size_t> active_;std::optional<kernel::FaceReference> ordering_;
    std::vector<kernel::FaceReference> inspected_;std::vector<QToolButton*> eyes_;
    std::function<void()> pick_,changed_,inspection_;
    bool dirty_{true};
    void headers() {
        table_->setHorizontalHeaderLabels({QString{},QStringLiteral("#"),QString{},tr("Reference"),QStringLiteral("←"),QStringLiteral("→"),QString{}});
    }
    void refresh_states() {
        const QSignalBlocker blocked(table_);std::optional<std::size_t> selected;
        for(int row=0;row<table_->rowCount();++row) {
            const bool populated=static_cast<std::size_t>(row)<side_.targets.size();
            const bool ordered=populated&&ordering_&&*ordering_==side_.targets[row].reference;
            table_->item(row,0)->setCheckState(ordered?Qt::Checked:Qt::Unchecked);if(ordered)selected=row;
            const bool inspected=populated&&std::ranges::find(inspected_,side_.targets[row].reference)!=inspected_.end();
            auto* field=static_cast<ui::ReferenceCellItem*>(table_->item(row,3));field->set_active_input(active_&&*active_==static_cast<std::size_t>(row));field->set_inspected(inspected);
            if(static_cast<std::size_t>(row)<eyes_.size()){const QSignalBlocker eye(eyes_[row]);eyes_[row]->setChecked(inspected);}
        }
        up_->setEnabled(selected&&*selected>0);down_->setEnabled(selected&&*selected+1<side_.targets.size());table_->viewport()->update();
    }
    void remove(const kernel::FaceReference& source) {
        const auto item=std::ranges::find_if(side_.targets,[&](const auto& t){return t.reference==source;});if(item==side_.targets.end())return;
        const auto row=static_cast<std::size_t>(item-side_.targets.begin());side_.targets.erase(item);document::prune_feature_up_to_patch_choices(side_);
        refresh();if(changed_)changed_();if(inspection_)inspection_();begin_entry(row);
    }
    void move(int delta) {
        if(!ordering_)return;
        const auto found=std::ranges::find_if(side_.targets,[&](const auto& t){return t.reference==*ordering_;});
        if(found==side_.targets.end())return;const auto row=found-side_.targets.begin(),next=row+delta;
        if(next<0||next>=static_cast<std::ptrdiff_t>(side_.targets.size()))return;
        std::swap(side_.targets[row],side_.targets[next]);document::prune_feature_up_to_patch_choices(side_);active_.reset();refresh();
        if(changed_)changed_();
    }
};
} // namespace zima::app
