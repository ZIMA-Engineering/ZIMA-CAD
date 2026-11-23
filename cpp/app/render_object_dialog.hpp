#pragma once
#include "placement_reference_dialog.hpp"
#include <zima/document/render_definition.hpp>
#include <zima/ui/container_placement_section.hpp>
#include <zima/ui/properties_subwindow.hpp>
#include <QCheckBox>
#include <QColor>
#include <QDoubleSpinBox>
#include <QEvent>
#include <QFormLayout>
#include <QLabel>
#include <QLineEdit>
#include <QPushButton>
#include <QTableWidget>
#include <QVBoxLayout>
#include <charconv>

namespace zima::app {
inline QString render_object_name(document::RenderObjectKind kind) {
    switch(kind){case document::RenderObjectKind::Camera:return QObject::tr("Camera");
    case document::RenderObjectKind::PointLight:return QObject::tr("Point light");
    case document::RenderObjectKind::Sun:return QObject::tr("Sun");}return {};
}
class RenderObjectDialog final : public ui::PropertiesSubWindow,public PlacementReferenceDialog {
public:
    std::function<void()> changed;
    std::function<void()> view_from_camera;
    RenderObjectDialog(document::RenderObject value,std::function<void(document::RenderObject)> commit,QWidget* parent)
        : PropertiesSubWindow(tr("Render object properties"),parent),initial_(std::move(value)),pending_(initial_),commit_(std::move(commit)) {
        setObjectName("renderObjectDialog");setAttribute(Qt::WA_DeleteOnClose);
        auto* form=new QFormLayout;
        auto* kind=new QLabel(render_object_name(initial_.kind),this);form->addRow(kind);
        translate_.push_back([this,kind]{kind->setText(render_object_name(initial_.kind));});
        auto* name=new QLineEdit(QString::fromStdString(initial_.name),this);name->setObjectName("renderObjectName");
        label(form,QT_TR_NOOP("Název"),name);content_layout()->addLayout(form);
        connect(name,&QLineEdit::textChanged,this,[this](const auto& name){pending_.name=name.trimmed().toStdString();notify();});
        placement_=new ui::ContainerPlacementSection(this,content_layout(),true,true);
        placement_->reference_table()->setObjectName("renderPlacementReferences");
        placement_->orientation_table()->setObjectName("renderOrientationReferences");
        placement_->initialize_numeric_values(initial_.placement);
        placement_->initialize_from_references(initial_.placement.references,[](const auto& key){return QString::fromStdString(key);});
        placement_->install_dof_label(content_layout());placement_->set_changed_callback([this]{notify();});
        auto* parameters=new QFormLayout;content_layout()->addLayout(parameters);
        if(initial_.kind==document::RenderObjectKind::Camera) {
            auto* ortho=new QCheckBox(this);ortho->setObjectName("renderOrthographic");ortho->setChecked(initial_.orthographic);
            translate_.push_back([ortho]{ortho->setText(tr("Orthographic camera"));});parameters->addRow(ortho);
            auto* fov=number(parameters,QT_TR_NOOP("Field of view"),"renderFieldOfView",pending_.field_of_view_degrees,1,175," °");
            auto* height=number(parameters,QT_TR_NOOP("Camera frame height"),"renderCameraHeight",pending_.orthographic_height_mm,.001,1e9," mm");
            const auto visibility=[=,this]{parameters->setRowVisible(fov,!pending_.orthographic);parameters->setRowVisible(height,pending_.orthographic);};
            connect(ortho,&QCheckBox::toggled,this,[=,this](bool on){pending_.orthographic=on;visibility();notify();});visibility();
            auto* view=new QPushButton(this);view->setObjectName("renderViewFromCamera");parameters->addRow(view);
            translate_.push_back([view]{view->setText(tr("View from camera"));});
            connect(view,&QPushButton::clicked,this,[this]{if(view_from_camera)view_from_camera();});
        } else {
            auto* enabled=new QCheckBox(this);enabled->setObjectName("renderLightEnabled");enabled->setChecked(initial_.enabled);
            translate_.push_back([enabled]{enabled->setText(tr("Light enabled"));});parameters->addRow(enabled);
            connect(enabled,&QCheckBox::toggled,this,[this](bool on){pending_.enabled=on;notify();});
            color_=new QLineEdit(QColor::fromRgbF(initial_.color[0],initial_.color[1],initial_.color[2]).name(),this);
            color_->setObjectName("renderLightColor");color_->setMaxLength(7);label(parameters,QT_TR_NOOP("Barva (#RRGGBB)"),color_);
            connect(color_,&QLineEdit::textChanged,this,[this](const auto& text){const QColor color(text);if(color.isValid()){
                pending_.color={color.redF(),color.greenF(),color.blueF()};notify();}});
            const bool sun=initial_.kind==document::RenderObjectKind::Sun;
            number(parameters,sun?QT_TR_NOOP("Illuminance"):QT_TR_NOOP("Luminous power"),"renderIntensity",pending_.intensity,0,1e12,sun?" lx":" lm");
            if(sun)number(parameters,QT_TR_NOOP("Angular radius"),"renderLightAngularRadius",pending_.angular_radius_degrees,0,45," °");
            else number(parameters,QT_TR_NOOP("Light radius"),"renderLightRadius",pending_.radius_mm,0,1e9," mm");
        }
        auto* visible=new QCheckBox(this);visible->setObjectName("renderObjectVisible");visible->setChecked(initial_.visible);
        translate_.push_back([visible]{visible->setText(tr("Show marker in View"));});parameters->addRow(visible);
        connect(visible,&QCheckBox::toggled,this,[this](bool on){pending_.visible=on;notify();});
        status_=new QLabel(this);status_->setWordWrap(true);status_->setObjectName("renderObjectStatus");content_layout()->addWidget(status_);
        for(const auto& f:translate_)f();set_initial_size({610,760});
    }
    document::RenderObject pending_value()const {
        auto value=pending_;value.placement=placement_->numeric_placement();value.placement.references=placement_->combined_references(3);return value;
    }
    void set_status(const QString& text){status_->setText(text);}
    // The same placement section and View picker used by ordinary containers.
    document::Placement placement_seed()const override{return placement_->numeric_placement();}
    bool owns_reference_owner(const std::string& id)const override{return id==initial_.id||id==initial_.id+":origin";}
    auto references_without(std::size_t i)const->std::vector<document::ConstructionReference> override{return placement_->references_without(i);}
    document::ConstructionReference prepare_reference_candidate(std::size_t i,document::ConstructionReference r)const override{
        r=placement_->prepare_reference(i,std::move(r));
        return r;
    }
    bool set_reference(std::size_t i,document::ConstructionReference r,const QString& label)override{
        if(owns_reference_owner(r.owner_id))return false;
        QString error;bool ok=placement_->set_reference(i,prepare_reference_candidate(i,std::move(r)),label,&error);
        set_status(error);return ok;
    }
    void set_reference_request_callback(std::function<void(std::size_t)> f){placement_->set_reference_request_callback(std::move(f));}
    void set_reference_highlights_changed_callback(std::function<void()> f){placement_->set_highlights_changed_callback(std::move(f));}
    auto highlighted_reference_entries()const{return placement_->highlighted_reference_entries();}
    std::size_t first_empty_position_index()const override{return placement_->first_empty_position_index();}
    void set_active_reference_index(std::optional<std::size_t> i)override{placement_->set_active_reference_index(i);}
    void set_reference_inspected(std::size_t i,bool v)override{placement_->set_reference_inspected(i,v);}
    void clear_reference_highlights()override{placement_->clear_reference_highlights();}
    void set_origin_selection_mode_callback(std::function<void(bool)> f)override{placement_->set_origin_selection_mode_callback(std::move(f));}
    void set_origin_selection_mode_active(bool v)override{placement_->set_origin_selection_mode_active(v);}
    void set_translation_constraint_state(const document::PointConstraintState& s,const kernel::Vec3& p)override{placement_->set_translation_constraint_state(s,p);}
    void set_remaining_rotation_dof(int n)override{placement_->set_remaining_rotation_dof(n);}
    void set_rotation_constraint_state(const document::OrientationConstraintState& s)override{placement_->set_rotation_constraint_state(s);}
    void set_orientation_base_rotation(const kernel::Vec3& v,bool c)override{placement_->set_orientation_base_rotation(v,c);}
    void set_resolved_rotation(const kernel::Vec3& v,bool valid=true)override{placement_->set_resolved_rotation(v,valid);}
    bool set_inline_parameter_value(std::string_view key,double value)override {
        const auto set=[value](QDoubleSpinBox* w){if(!w||!w->isEnabled()||w->isReadOnly()||!w->isVisible())return false;w->setValue(value);return true;};
        const std::array<std::string_view,3> xyz{"placement:x","placement:y","placement:z"},rot{"placement:rotation_x","placement:rotation_y","placement:rotation_z"};
        for(int i=0;i<3;++i){if(key==xyz[i])return set(placement_->translation_fields()[i]);if(key==rot[i]){if(placement_->rotation_fields()[i]->isEnabled()&&placement_->rotation_fields()[i]->isReadOnly())return false;return set(placement_->rotation_fields()[i])||set(placement_->rotation_offset_fields()[i]);}}
        constexpr std::string_view prefix="placement:reference_offset:";
        if(key.starts_with(prefix)){key.remove_prefix(prefix.size());std::size_t i{};const auto [end,error]=std::from_chars(key.data(),key.data()+key.size(),i);return error==std::errc{}&&end==key.data()+key.size()&&placement_->set_reference_offset(i,value);}
        return false;
    }
protected:
    bool submit()override {
        try{if(color_&&(color_->text().size()!=7||!QColor(color_->text()).isValid()))throw std::invalid_argument("Invalid Render light color.");
            auto value=pending_value();document::validate_render_object(value);commit_(std::move(value));return true;
        }catch(const std::exception& e){set_status(tr(e.what()));return false;}
    }
    void changeEvent(QEvent* event)override {
        PropertiesSubWindow::changeEvent(event);if(event->type()!=QEvent::LanguageChange)return;
        set_internal_title(tr("Render object properties"));for(const auto& f:translate_)f();
    }
private:
    document::RenderObject initial_,pending_;
    std::function<void(document::RenderObject)> commit_;
    ui::ContainerPlacementSection* placement_{};QLabel* status_{};QLineEdit* color_{};
    std::vector<std::function<void()>> translate_;
    void notify(){if(changed)changed();}
    void label(QFormLayout* form,const char* text,QWidget* field) {
        auto* label=new QLabel(tr(text),this);form->addRow(label,field);translate_.push_back([label,text]{label->setText(tr(text));});
    }
    QDoubleSpinBox* number(QFormLayout* form,const char* text,const char* id,double& value,double low,double high,const char* unit) {
        auto* field=new QDoubleSpinBox(this);field->setObjectName(id);field->setRange(low,high);field->setDecimals(6);field->setSuffix(unit);field->setValue(value);
        label(form,text,field);connect(field,&QDoubleSpinBox::valueChanged,this,[this,&value](double v){value=v;notify();});return field;
    }
};
} // namespace zima::app
