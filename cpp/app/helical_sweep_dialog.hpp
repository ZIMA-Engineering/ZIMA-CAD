#pragma once
#include <zima/document/part_document.hpp>
#include <zima/ui/properties_subwindow.hpp>
#include <QComboBox>
#include <QDoubleSpinBox>
#include <QFormLayout>
#include <QLabel>
#include <QLineEdit>
#include <QPushButton>
#include <QSignalBlocker>
#include <QTableWidget>
#include <QHeaderView>
#include <QToolButton>
#include <zima/ui/reference_cell.hpp>
#include <array>
#include <functional>
#include <set>
namespace zima::app {
class HelicalSweepDialog final : public ui::PropertiesSubWindow {
public:
    document::HistoryContainer pending;
    std::function<void(unsigned)> edit_sketch;
    std::function<void()> changed;
    std::function<void()> select_plane;
    bool plane_input{};
    bool plane_inspected{};
    HelicalSweepDialog(document::HistoryContainer initial,std::function<void(document::HistoryContainer)> commit,QWidget* parent)
      : PropertiesSubWindow(tr("Vlastnosti Helical Sweepu"),parent),pending(std::move(initial)),commit_(std::move(commit)) {
        setObjectName("helicalSweepDialog");setAttribute(Qt::WA_DeleteOnClose);setMinimumWidth(360);
        auto* form=new QFormLayout;
        auto* name=new QLineEdit(QString::fromStdString(pending.name),this);form->addRow(tr("Název"),name);
        connect(name,&QLineEdit::textChanged,this,[this](auto v){pending.name=v.toStdString();});
        auto* mode=new QComboBox(this);mode->addItems({tr("Přičíst"),tr("Odečíst")});
        mode->setCurrentIndex(pending.combine_mode==document::CombineMode::Subtract?1:0);form->addRow(tr("Operace"),mode);
        connect(mode,&QComboBox::currentIndexChanged,this,[this](int i){pending.combine_mode=i?document::CombineMode::Subtract:document::CombineMode::Add;notify();});
        auto* plane=new QComboBox(this);plane_combo_=plane;plane->setObjectName("helicalBasePlane");plane->addItems({"XY","XZ","YZ",tr("Vybraná rovina / plocha")});
        auto base=sketcher::Sketch::from_serialized(pending.helical.sketches[0]);
        plane->setCurrentIndex(base.plane_reference_owner_id.empty()?static_cast<int>(base.plane):3);
        form->addRow(tr("Rovina první skici"),plane);
        connect(plane,&QComboBox::activated,this,[this](int i){if(i==3){if(select_plane)select_plane();return;}pending.helical.base_plane.reset();plane_input=false;plane_inspected=false;refresh_reference();auto s=sketcher::Sketch::from_serialized(pending.helical.sketches[0]);s.plane_reference_owner_id.clear();s.plane=static_cast<sketcher::SketchPlane>(i);s.refresh_default_frame();set_sketch(0,s);});
        refs_=new QTableWidget(1,2,this);refs_->setObjectName("helicalPlaneReference");
        refs_->horizontalHeader()->hide();refs_->verticalHeader()->hide();refs_->setFixedHeight(38);refs_->setSelectionMode(QAbstractItemView::NoSelection);refs_->setEditTriggers(QAbstractItemView::NoEditTriggers);
        refs_->horizontalHeader()->setSectionResizeMode(0,QHeaderView::Stretch);refs_->setColumnWidth(1,32);
        ui::install_reference_cell_delegate(refs_);plane_cell_=new ui::ReferenceCellItem;refs_->setItem(0,0,plane_cell_);
        eye_=ui::build_reference_inspection_button(false,false,[this](bool enabled){plane_inspected=enabled;refresh_reference();notify();});
        refs_->setCellWidget(0,1,ui::centered_cell_widget(eye_));form->addRow(tr("Reference roviny"),refs_);
        connect(refs_,&QTableWidget::cellClicked,this,[this](int,int column){if(column==0){plane_input=true;refresh_reference();if(select_plane)select_plane();}});
        refresh_reference();
        const std::array<QString,3> names{tr("1. Kružnice a počáteční bod…"),tr("2. Radiální vodicí skica…"),tr("3. Skica průřezu…")};
        for(unsigned i=0;i<3;++i){auto* button=new QPushButton(names[i],this);button->setObjectName(QString("helicalSketch%1").arg(i));form->addRow(button);connect(button,&QPushButton::clicked,this,[this,i]{if(edit_sketch)edit_sketch(i);});}
        circle_=new QComboBox(this);point_=new QComboBox(this);circle_->setObjectName("helicalCircle");point_->setObjectName("helicalStartPoint");
        form->addRow(tr("Základní kružnice"),circle_);form->addRow(tr("Počáteční bod"),point_);
        connect(circle_,&QComboBox::activated,this,[this]{pending.helical.circle_id=circle_->currentData().toString().toStdString();notify();});
        connect(point_,&QComboBox::activated,this,[this]{pending.helical.start_point_id=point_->currentData().toString().toStdString();notify();});
        auto* pitch=new QDoubleSpinBox(this);pitch->setObjectName("helicalPitch");pitch->setDecimals(4);pitch->setRange(.0001,1000000);pitch->setSuffix(" mm");pitch->setValue(pending.helical.pitch);form->addRow(tr("Stoupání"),pitch);
        connect(pitch,&QDoubleSpinBox::valueChanged,this,[this](double p){pending.helical.pitch=p;notify();});
        auto* hand=new QComboBox(this);hand->setObjectName("helicalHandedness");hand->addItems({tr("Pravý"),tr("Levý")});hand->setCurrentIndex(pending.helical.left_handed?1:0);form->addRow(tr("Směr vinutí"),hand);
        connect(hand,&QComboBox::currentIndexChanged,this,[this](int i){pending.helical.left_handed=i!=0;notify();});
        status_=new QLabel(this);status_->setWordWrap(true);form->addRow(status_);content_layout()->addLayout(form);refresh_choices();
    }
    void refresh_reference(){if(!plane_cell_)return;const QSignalBlocker block(eye_);eye_->setChecked(plane_inspected);if(!plane_input){const QSignalBlocker plane_block(plane_combo_);const auto base=sketcher::Sketch::from_serialized(pending.helical.sketches[0]);plane_combo_->setCurrentIndex(pending.helical.base_plane?3:static_cast<int>(base.plane));}plane_cell_->set_reference(pending.helical.base_plane?QString::fromStdString(pending.helical.base_plane->semantic_key):QString());plane_cell_->set_active_input(plane_input);plane_cell_->set_inspected(plane_inspected);eye_->setEnabled(pending.helical.base_plane.has_value());refs_->viewport()->update();}
    void end_reference(){plane_input=false;plane_inspected=false;refresh_reference();notify();}
    void set_status(const QString& text){status_->setText(text);}
    void set_sketch(unsigned stage,const sketcher::Sketch& sketch){pending.helical.sketches.at(stage)=sketch.serialized();if(stage==0)refresh_choices();notify();}
    void refresh_choices(){
        const QSignalBlocker a(circle_),b(point_);circle_->clear();point_->clear();
        circle_->addItem(tr("Vyberte kružnici"),QString());point_->addItem(tr("Vyberte bod"),QString());
        const auto s=sketcher::Sketch::from_serialized(pending.helical.sketches[0]);std::set<std::string> centers;
        for(const auto& c:s.circles){centers.insert(c.center_point_id);if(!c.construction)circle_->addItem(tr("Kružnice %1 — ⌀%2 mm").arg(circle_->count()).arg(2*c.radius),QString::fromStdString(c.id));}
        for(const auto& p:s.points)if(!centers.contains(p.id))point_->addItem(tr("Bod %1 [%2; %3]").arg(point_->count()).arg(p.x).arg(p.y),QString::fromStdString(p.id));
        auto restore=[](QComboBox* box,std::string& id){int i=box->findData(QString::fromStdString(id));if(i<=0&&id.empty()&&box->count()==2){i=1;id=box->itemData(i).toString().toStdString();}box->setCurrentIndex(std::max(0,i));};
        restore(circle_,pending.helical.circle_id);restore(point_,pending.helical.start_point_id);
    }
protected:
    bool submit() override {try{document::PartDocument::reframe_helical_sketches(pending);commit_(pending);return true;}catch(const std::exception& e){set_status(QString::fromUtf8(e.what()));return false;}}
private:
    std::function<void(document::HistoryContainer)> commit_;QComboBox* plane_combo_{};QComboBox* circle_{};QComboBox* point_{};QLabel* status_{};QTableWidget* refs_{};ui::ReferenceCellItem* plane_cell_{};QToolButton* eye_{};
    void notify(){if(changed)changed();}
};
}
