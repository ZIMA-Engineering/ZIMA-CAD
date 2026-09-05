#pragma once
#include "placement_reference_dialog.hpp"
#include <zima/ui/container_placement_section.hpp>
#include <zima/ui/properties_subwindow.hpp>
#include <QDoubleSpinBox>
#include <QLabel>
#include <QTableWidget>
#include <algorithm>

namespace zima::app {
// The sweep editors consume the ordinary placement section and picker contract.
// No feature-specific reference solver or placement widget is maintained here.
class SweepPlacementDialog : public ui::PropertiesSubWindow, public PlacementReferenceDialog {
public:
    document::HistoryContainer pending;
    std::function<void()> changed;
    std::function<void(unsigned)> edit_sketch;
    virtual void set_status(const QString&)=0;
    virtual void set_sketch(unsigned,const sketcher::Sketch&)=0;
    std::function<void(std::size_t)> request_placement;
    SweepPlacementDialog(const QString& title, document::HistoryContainer value, QWidget* parent)
        : PropertiesSubWindow(title,parent),pending(std::move(value)) {}
    void install_placement() {
        placement_=new ui::ContainerPlacementSection(this,content_layout(),true,true);
        placement_->initialize_from_references(pending.placement.references,[](const std::string& key){return QString::fromStdString(key);});
        placement_->initialize_numeric_values(pending.placement);
        placement_->reference_table()->setObjectName("sweepPlacementReferences");
        placement_->orientation_table()->setObjectName("sweepPlacementOrientation");
        for(unsigned i=0;i<3;++i){placement_->translation_fields()[i]->setObjectName(QString("sweepTranslation%1").arg(i));placement_->rotation_fields()[i]->setObjectName(QString("sweepRotation%1").arg(i));placement_->rotation_offset_fields()[i]->setObjectName(QString("sweepRotationOffset%1").arg(i));}
        placement_->set_reference_request_callback([this](std::size_t i){if(request_placement)request_placement(i);});
        placement_->set_changed_callback([this]{read_placement();if(changed)changed();});
        placement_->set_highlights_changed_callback([this]{if(changed)changed();});
        placement_->install_dof_label(content_layout());
    }
    void read_placement(){pending.placement=placement_->numeric_placement();pending.placement.references=placement_->combined_references(3);}
    bool resolve_pending_placement(const kernel::ViewerReferenceGeometry& geometry){
        read_placement();kernel::Vec3 base;bool referenced=false;
        const bool valid=document::resolve_placement(pending.placement,geometry,&base,&referenced);
        const auto& p=pending.placement;
        placement_->set_translation_constraint_state(document::point_constraint_state(p.references,geometry),{p.x,p.y,p.z});
        placement_->set_rotation_constraint_state(document::orientation_constraint_state(p.references,geometry,true,{p.x,p.y,p.z}));
        placement_->set_orientation_base_rotation(base,referenced);
        placement_->set_resolved_rotation({p.rotation_x,p.rotation_y,p.rotation_z},valid);
        return valid;
    }
    auto highlighted_reference_entries() const {return placement_->highlighted_reference_entries();}
    std::vector<document::ConstructionReference> references_without(std::size_t i) const override{return placement_->references_without(i);}
    bool owns_reference_owner(const std::string& owner) const override {
        return owner==pending.id||owner==pending.feature_id||owner==pending.container_origin.id||std::ranges::any_of(pending.container_origin.children,[&](const auto& c){return c.id==owner;});
    }
    bool set_reference(std::size_t i,document::ConstructionReference ref,const QString& label) override {
        QString error;const bool ok=placement_->set_reference(i,std::move(ref),label,&error);
        placement_->reference_status_label()->setText(error);return ok;
    }
    std::size_t first_empty_position_index() const override{return placement_->first_empty_position_index();}
    void set_active_reference_index(std::optional<std::size_t> i) override{placement_->set_active_reference_index(i);}
    void set_reference_inspected(std::size_t i,bool value) override{placement_->set_reference_inspected(i,value);}
    void clear_reference_highlights() override{placement_->clear_reference_highlights();}
    void set_origin_selection_mode_callback(std::function<void(bool)> cb) override{placement_->set_origin_selection_mode_callback(std::move(cb));}
    void set_origin_selection_mode_active(bool value) override{placement_->set_origin_selection_mode_active(value);}
    void set_translation_constraint_state(const document::PointConstraintState& s,const kernel::Vec3& p) override{placement_->set_translation_constraint_state(s,p);}
    void set_remaining_rotation_dof(int n) override{placement_->set_remaining_rotation_dof(n);}
    void set_rotation_constraint_state(const document::OrientationConstraintState& s) override{placement_->set_rotation_constraint_state(s);}
    void set_orientation_base_rotation(const kernel::Vec3& r,bool c) override{placement_->set_orientation_base_rotation(r,c);}
    void set_resolved_rotation(const kernel::Vec3& r,bool valid=true) override{placement_->set_resolved_rotation(r,valid);}
    bool set_inline_parameter_value(std::string_view key,double value) override{
        constexpr std::string_view prefix{"placement:"};
        if(!key.starts_with(prefix))return false;
        key.remove_prefix(prefix.size());
        const auto set=[value](QDoubleSpinBox* field){if(!field||!field->isEnabled()||!field->isVisible())return false;field->setValue(value);return true;};
        constexpr std::array<std::string_view,3> positions{"x","y","z"},rotations{"rotation_x","rotation_y","rotation_z"};
        for(unsigned i=0;i<3;++i){if(key==positions[i])return set(placement_->translation_fields()[i]);if(key==rotations[i])return set(placement_->rotation_fields()[i])||set(placement_->rotation_offset_fields()[i]);}
        constexpr std::string_view offset{"reference_offset:"};
        if(key.starts_with(offset)){
            key.remove_prefix(offset.size());if(key.empty())return false;
            std::size_t index{};for(char digit:key){if(digit<'0'||digit>'9')return false;index=index*10+static_cast<std::size_t>(digit-'0');}
            return placement_->set_reference_offset(index,value);
        }
        return false;
    }
private:
    ui::ContainerPlacementSection* placement_{};
};
}
