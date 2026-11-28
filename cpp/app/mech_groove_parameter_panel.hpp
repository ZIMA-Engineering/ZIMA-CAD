#pragma once
#include <zima/document/groove_catalog.hpp>
#include <zima/ui/unit_spin_box.hpp>
#include <QComboBox>
#include <QEvent>
#include <QFormLayout>
#include <QLabel>
#include <QSignalBlocker>
#include <functional>
#include <array>
#include <stdexcept>

namespace zima::app {
// Position references belong exclusively to the ordinary placement section.
// This panel edits only the groove's section and manufacturing dimensions.
class MechGrooveParameterPanel final:public QWidget {
public:
    std::function<void()> changed;
    explicit MechGrooveParameterPanel(document::MechGrooveParameters value,QWidget* parent)
        :QWidget(parent),pending_(std::move(value)) {
        setObjectName("mechGrooveParameters");
        form_=new QFormLayout(this);form_->setContentsMargins(0,0,0,0);
        side_=new QLabel(this);add(QT_TR_NOOP("Support"),side_);
        profile_=new QComboBox(this);profile_->setObjectName("mechGrooveProfile");
        profile_->addItems({tr("Rectangular groove"),tr("Circular groove")});
        profile_->setCurrentIndex(static_cast<int>(pending_.profile));add(QT_TR_NOOP("Groove profile"),profile_);
        catalogue_=new QComboBox(this);catalogue_->setObjectName("mechGrooveCatalogue");
        add(QT_TR_NOOP("Standard / series"),catalogue_);
        size_=new QLabel(this);size_->setObjectName("mechGrooveSize");add(QT_TR_NOOP("Nominal size"),size_);
        width_=number(QT_TR_NOOP("Width"),"mechGrooveWidth",pending_.width,[this](double n){
            pending_.width=n;
            if(!pending_.catalogue.empty())try {
                const auto& r=document::groove_size(pending_.catalogue,pending_.support_diameter);
                pending_.width_position=r.width_max>r.width_min?(n-r.width_min)/(r.width_max-r.width_min):0.;
            }catch(const std::exception&){}
        });
        range_=new QLabel(this);add(QT_TR_NOOP("Width range"),range_);
        depth_=number(QT_TR_NOOP("Depth"),"mechGrooveDepth",pending_.depth,[this](double n){pending_.depth=n;});
        radius_=number(QT_TR_NOOP("Profile radius"),"mechGrooveRadius",pending_.radius,[this](double n){pending_.radius=n;});
        error_=new QLabel(this);error_->setWordWrap(true);error_->setObjectName("mechGrooveDraftError");form_->addRow(error_);
        connect(profile_,&QComboBox::currentIndexChanged,this,[this](int n){
            pending_.profile=static_cast<document::GrooveProfile>(n);pending_.catalogue.clear();pending_.row.clear();
            refresh();notify();
        });
        connect(catalogue_,&QComboBox::currentIndexChanged,this,[this](int){
            pending_.catalogue=catalogue_->currentData().toString().toStdString();pending_.row.clear();pending_.width_position=0;
            refresh();notify();
        });
        refresh();ready_=true;
    }
    const document::MechGrooveParameters& parameters()const{return pending_;}
    void update_derived(const document::MechGrooveParameters& p){pending_=p;error_key_.clear();refresh();}
    void set_draft_error(const char* key){error_key_=key;refresh();}
protected:
    void changeEvent(QEvent* event)override {
        QWidget::changeEvent(event);if(event->type()!=QEvent::LanguageChange)return;
        for(const auto& [widget,key]:labels_)static_cast<QLabel*>(form_->labelForField(widget))->setText(tr(key));
        profile_->setItemText(0,tr("Rectangular groove"));profile_->setItemText(1,tr("Circular groove"));refresh();
    }
private:
    void add(const char* key,QWidget* widget){form_->addRow(tr(key),widget);labels_.emplace_back(widget,key);}
    ui::UnitDoubleSpinBox* number(const char* key,const char* name,double value,std::function<void(double)> write) {
        auto* field=new ui::UnitDoubleSpinBox(ui::InputQuantity::Length,this);field->setObjectName(name);
        field->setRange(1e-5,1e6);field->set_display_decimals(ui::numeric_decimal_places(this));field->setValue(value);add(key,field);
        connect(field,&QDoubleSpinBox::valueChanged,this,[this,write=std::move(write)](double n){write(n);notify();});return field;
    }
    void notify(){if(ready_&&changed)changed();}
    void refresh() {
        const bool circular=pending_.profile==document::GrooveProfile::Circular,custom=pending_.catalogue.empty();
        side_->setText(pending_.support_diameter>0?
            tr("%1 · Ø %2 mm").arg(pending_.internal?tr("Internal groove"):tr("External groove"),
                locale().toString(pending_.support_diameter,'g',12)):tr("Select a cylindrical material face"));
        {
            const QSignalBlocker guard(catalogue_);catalogue_->clear();catalogue_->addItem(tr("Custom"),QString{});
            for(const auto& c:document::groove_catalogues())if(c.profile==pending_.profile)
                catalogue_->addItem(QString::fromStdString(c.standard)+QStringLiteral(" · SEEGER"),QString::fromStdString(c.id));
            // Preserve an incompatible authored selection until the user repairs it.
            int selected=catalogue_->findData(QString::fromStdString(pending_.catalogue));
            if(selected<0){catalogue_->addItem(QString::fromStdString(pending_.catalogue),QString::fromStdString(pending_.catalogue));selected=catalogue_->count()-1;}
            catalogue_->setCurrentIndex(selected);
        }
        for(const auto& [field,value]:std::array<std::pair<ui::UnitDoubleSpinBox*,double>,3>{{{width_,pending_.width},{depth_,pending_.depth},{radius_,pending_.radius}}}) {
            const QSignalBlocker guard(field);field->setRange(1e-5,1e6);field->setValue(value);
        }
        width_->setEnabled(!circular);depth_->setEnabled(custom);radius_->setEnabled(custom);
        form_->setRowVisible(radius_,circular);form_->setRowVisible(range_,!custom&&!circular);
        size_->setText(pending_.row.empty()?QStringLiteral("—"):QString::fromStdString(pending_.row));
        form_->setRowVisible(size_,!custom);
        if(!custom&&!circular)try {
            const auto& r=document::groove_size(pending_.catalogue,pending_.support_diameter);
            const QSignalBlocker guard(width_);width_->setRange(r.width_min,r.width_max);width_->setValue(pending_.width);
            range_->setText(tr("%1–%2 mm (H13)").arg(locale().toString(r.width_min,'g',12),locale().toString(r.width_max,'g',12)));
        }catch(const std::exception&){range_->setText(QStringLiteral("—"));}
        error_->setText(error_key_.empty()?QString{}:tr(error_key_.c_str()));error_->setVisible(!error_key_.empty());
    }
    document::MechGrooveParameters pending_;
    std::string error_key_;bool ready_{};
    QFormLayout* form_{};QComboBox *profile_{},*catalogue_{};QLabel *side_{},*size_{},*range_{},*error_{};
    ui::UnitDoubleSpinBox *width_{},*depth_{},*radius_{};
    std::vector<std::pair<QWidget*,const char*>> labels_;
};
}
