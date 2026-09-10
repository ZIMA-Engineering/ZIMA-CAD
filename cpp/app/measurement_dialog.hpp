#pragma once
#include <zima/viewer/measurement.hpp>
#include <zima/kernel/dimension_layout.hpp>
#include <zima/ui/properties_subwindow.hpp>
#include <zima/ui/reference_cell.hpp>
#include <QApplication>
#include <QDialogButtonBox>
#include <QHeaderView>
#include <QLabel>
#include <QLineEdit>
#include <QLocale>
#include <QMouseEvent>
#include <QPushButton>
#include <QSignalBlocker>
#include <QTableWidget>
#include <QToolButton>
#include <QVBoxLayout>
#include <array>

namespace zima::app {
class MeasurementDialog final : public ui::PropertiesSubWindow {
public:
    using Resolve=std::function<std::optional<viewer::MeasurementGeometry>(const kernel::MeasurementReference&)>;
    using Label=std::function<QString(const kernel::MeasurementReference&)>;
    using Save=std::function<void(kernel::SavedMeasurement)>;
    MeasurementDialog(kernel::SavedMeasurement initial,Resolve resolve,Label label,Save save,
            double length_scale,QString length_unit,double mass_scale,QString mass_unit,QWidget* parent)
        :PropertiesSubWindow(tr("Měření"),parent),initial_(std::move(initial)),resolve_(std::move(resolve)),
         label_(std::move(label)),save_(std::move(save)),length_scale_(length_scale),mass_scale_(mass_scale),
         length_unit_(std::move(length_unit)),mass_unit_(std::move(mass_unit)){
        setObjectName("measurementDialog");setAttribute(Qt::WA_DeleteOnClose);set_initial_size({420,480});
        name_=new QLineEdit(QString::fromStdString(initial_.name));name_->setObjectName("measurementName");
        content_layout()->addWidget(new QLabel(tr("Název uloženého měření")));content_layout()->addWidget(name_);
        for(int i=0;i<2;++i){
            content_layout()->addWidget(new QLabel(i==0?tr("Entita 1"):tr("Entita 2 — volitelná")));
            tables_[i]=new QTableWidget(1,3);tables_[i]->setObjectName(QString("measurementReference%1").arg(i+1));
            auto* table=tables_[i];table->verticalHeader()->hide();table->horizontalHeader()->hide();
            table->setEditTriggers(QAbstractItemView::NoEditTriggers);table->setSelectionMode(QAbstractItemView::NoSelection);
            table->setFocusPolicy(Qt::NoFocus);table->setFixedHeight(37);table->setColumnWidth(0,30);table->setColumnWidth(2,30);
            table->horizontalHeader()->setSectionResizeMode(1,QHeaderView::Stretch);
            ui::install_reference_cell_delegate(table);items_[i]=new ui::ReferenceCellItem;table->setItem(0,1,items_[i]);
            indicators_[i]=ui::build_reference_row_indicator([this,i]{references_[i].reset();geometries_[i].reset();inspected_[i]=false;active_=i;refresh();});
            table->setCellWidget(0,0,indicators_[i]);
            eyes_[i]=ui::build_reference_inspection_button(false,false,[this,i](bool on){inspected_[i]=on;publish();});
            table->setCellWidget(0,2,ui::centered_cell_widget(eyes_[i]));
            connect(table,&QTableWidget::cellClicked,this,[this,i](int,int column){if(column==1){active_=i;publish();}});
            content_layout()->addWidget(table);
            info_[i]=new QLabel;info_[i]->setObjectName(QString("measurementInfo%1").arg(i+1));
            info_[i]->setTextInteractionFlags(Qt::TextSelectableByMouse);info_[i]->setWordWrap(true);content_layout()->addWidget(info_[i]);
            if(static_cast<std::size_t>(i)<initial_.references.size())references_[i]=initial_.references[i];
        }
        result_=new QLabel;result_->setObjectName("measurementDistance");result_->setWordWrap(true);
        result_->setTextInteractionFlags(Qt::TextSelectableByMouse);content_layout()->addWidget(result_);
        hint_=new QLabel(tr("Krátký klik prostředním ukončí výběr. Dvojklik prostředním zavře okno."));
        hint_->setWordWrap(true);content_layout()->addWidget(hint_);
        save_button_=new QPushButton(tr("Uložit"));save_button_->setObjectName("saveMeasurement");
        save_button_->setToolTip(tr("Uložit měření do historie zobrazeného dokumentu a zavřít."));
        content_layout()->addWidget(save_button_);
        connect(save_button_,&QPushButton::clicked,this,[this]{
            try{auto record=current();save_(std::move(record));accept();}
            catch(const std::exception& e){result_->setText(QString::fromUtf8(e.what()));}
        });
        connect(name_,&QLineEdit::textChanged,this,[this]{update_save();});
        active_=initial_.references.empty()?0:-1;refresh();
    }
    int active_reference()const{return active_;}
    bool entering()const{return active_>=0;}
    void set_changed(std::function<void()> changed){changed_=std::move(changed);publish();}
    void select_reference(const kernel::MeasurementReference& reference){
        if(active_<0)return;references_[active_]=reference;inspected_[active_]=true;
        active_=active_==0&&!references_[1]?1:-1;refresh();
    }
    void end_entry(){active_=-1;inspected_={false,false};publish();}
    const auto& geometries()const{return geometries_;}
    const auto& inspected()const{return inspected_;}
    const auto& distance()const{return distance_;}
    QString distance_text()const{return distance_?format(distance_->distance,1,length_scale_,length_unit_):QString{};}
    kernel::SavedMeasurement current()const{
        auto record=initial_;record.name=name_->text().trimmed().toStdString();record.references.clear();record.values.clear();
        for(int i=0;i<2;++i)if(references_[i]){
            if(!geometries_[i])throw std::runtime_error("Měření má chybějící referenci.");
            record.references.push_back(*references_[i]);record.values.push_back(geometries_[i]->values);
        }
        if(record.name.empty()||record.references.empty())throw std::runtime_error("Zadejte název a alespoň jednu referenci.");
        record.distance=distance_;return record;
    }
protected:
    bool submit()override{return true;} // OK/double MMB only closes; Save is explicit.
    bool eventFilter(QObject* watched,QEvent* event)override{
        auto* widget=qobject_cast<QWidget*>(watched);
        const auto* owner=parentWidget();
        if(widget&&owner&&(widget==owner||owner->isAncestorOf(widget))){
            if(event->type()==QEvent::MouseButtonPress){
                const auto* mouse=static_cast<QMouseEvent*>(event);
                if(mouse->button()==Qt::MiddleButton){middle_=mouse->globalPosition();pending_middle_=true;dragged_=false;}
                else if(pending_middle_)dragged_=true;
            }else if(event->type()==QEvent::MouseMove&&pending_middle_){
                const auto* mouse=static_cast<QMouseEvent*>(event);
                dragged_|=(mouse->globalPosition()-middle_).manhattanLength()>=QApplication::startDragDistance();
            }else if(event->type()==QEvent::MouseButtonRelease){
                const auto* mouse=static_cast<QMouseEvent*>(event);
                if(mouse->button()==Qt::MiddleButton&&pending_middle_){
                    pending_middle_=false;if(!dragged_)end_entry();
                }
            }
        }
        return PropertiesSubWindow::eventFilter(watched,event);
    }
private:
    QString number(double value)const{return QString::fromStdString(kernel::dimension_number(value,ui::numeric_decimal_places(this)));}
    QString format(kernel::MeasurementValue value,int power,double scale,const QString& unit)const{
        return (value.approximate?QStringLiteral("≈ "):QString{})+number(value.value/std::pow(scale,power))+unit+
            (power==2?QStringLiteral("²"):power==3?QStringLiteral("³"):QString{});
    }
    QString details(const viewer::MeasurementGeometry& geometry)const{
        QStringList lines;const auto& v=geometry.values;
        if(v.position)lines<<tr("X: %1   Y: %2   Z: %3").arg(
            number(v.position->x/length_scale_)+length_unit_,number(v.position->y/length_scale_)+length_unit_,number(v.position->z/length_scale_)+length_unit_);
        if(v.length)lines<<tr("Délka: %1").arg(format(*v.length,1,length_scale_,length_unit_));
        if(v.area)lines<<tr("Obsah: %1").arg(format(*v.area,2,length_scale_,length_unit_));
        if(v.volume)lines<<tr("Objem: %1").arg(format(*v.volume,3,length_scale_,length_unit_));
        if(v.mass)lines<<tr("Hmotnost: %1").arg(format(*v.mass,1,mass_scale_,mass_unit_));
        else if(v.volume)lines<<tr("Hmotnost: není zadaná hustota materiálu.");
        if(geometry.axis)lines<<tr("Směr osy: %1; %2; %3").arg(number(geometry.axis->second.x),number(geometry.axis->second.y),number(geometry.axis->second.z));
        if(geometry.plane)lines<<tr("Nekonečná referenční rovina");
        if(lines.isEmpty())lines<<tr("Geometrie vybrána.");
        return lines.join('\n');
    }
    void update_save(){
        bool any=false,valid=!name_->text().trimmed().isEmpty();
        for(int i=0;i<2;++i)if(references_[i]){any=true;valid&=geometries_[i].has_value();}
        save_button_->setEnabled(any&&valid);
    }
    void refresh(){
        for(int i=0;i<2;++i){
            geometries_[i]=references_[i]?resolve_(*references_[i]):std::nullopt;
            info_[i]->setText(geometries_[i]?details(*geometries_[i]):references_[i]?tr("Reference chybí. Klikněte do pole a vyberte náhradu."):QString{});
        }
        distance_=geometries_[0]&&geometries_[1]?viewer::measure_distance(*geometries_[0],*geometries_[1]):std::nullopt;
        result_->setText(distance_?tr("Nejkratší vzdálenost: %1").arg(distance_text()):QString{});
        bool approximate=distance_&&distance_->distance.approximate;
        for(const auto& g:geometries_)if(g)approximate|=(g->values.area&&g->values.area->approximate)||(g->values.length&&g->values.length->approximate)||(g->values.volume&&g->values.volume->approximate);
        if(approximate)result_->setText(result_->text()+tr("\n≈ Označené hodnoty jsou aproximací zobrazené geometrie."));
        update_save();publish();
    }
    void publish(){
        for(int i=0;i<2;++i){
            auto* item=items_[i];
            if(references_[i]){item->set_reference(QString::fromStdString(references_[i]->owner_id+references_[i]->semantic_key+references_[i]->instance_path));item->setText(label_(*references_[i]));}
            else{item->clear_reference();item->setText(tr("Vyberte entitu…"));}
            item->set_active_input(active_==i);item->set_inspected(inspected_[i]);
            item->set_missing(references_[i]&&!geometries_[i]);ui::set_reference_row_populated(indicators_[i],references_[i].has_value());
            eyes_[i]->setEnabled(references_[i].has_value());const QSignalBlocker blocker(eyes_[i]);eyes_[i]->setChecked(inspected_[i]);tables_[i]->viewport()->update();
        }
        if(changed_)changed_();
    }
    kernel::SavedMeasurement initial_;Resolve resolve_;Label label_;Save save_;std::function<void()> changed_;
    double length_scale_,mass_scale_;QString length_unit_,mass_unit_;
    QLineEdit* name_{};std::array<QTableWidget*,2> tables_{};std::array<ui::ReferenceCellItem*,2> items_{};
    std::array<QWidget*,2> indicators_{};std::array<QToolButton*,2> eyes_{};std::array<QLabel*,2> info_{};
    QLabel *result_{},*hint_{};QPushButton* save_button_{};
    std::array<std::optional<kernel::MeasurementReference>,2> references_;
    std::array<std::optional<viewer::MeasurementGeometry>,2> geometries_;
    std::array<bool,2> inspected_{};std::optional<kernel::MeasurementDistance> distance_;
    int active_{};QPointF middle_;bool pending_middle_{},dragged_{};
};
} // namespace zima::app