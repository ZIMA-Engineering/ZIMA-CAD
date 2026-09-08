#pragma once

#include <zima/document/body_history.hpp>
#include <zima/ui/container_placement_section.hpp>
#include "placement_reference_dialog.hpp"
#include <QTableWidget>
#include <QLabel>
#include <zima/ui/properties_subwindow.hpp>
#include <QCheckBox>
#include <QComboBox>
#include <QDoubleSpinBox>
#include <QFormLayout>
#include <QLineEdit>
#include <QVBoxLayout>
#include <array>
#include <charconv>
#include <functional>
#include <stdexcept>

namespace zima::app {

class BodyPropertiesDialog final : public zima::ui::PropertiesSubWindow, public PlacementReferenceDialog {
public:
    using Commit = std::function<void(zima::document::BodyHistory, bool)>;
    BodyPropertiesDialog(zima::document::BodyHistory initial, bool active, Commit commit, QWidget* parent,
        int decimal_places = 3)
        : PropertiesSubWindow(tr("Vlastnosti tělesa"), parent), initial_(std::move(initial)), commit_(std::move(commit)) {
        setAttribute(Qt::WA_DeleteOnClose);
        setProperty("zimaValueLockOwner",QString::fromStdString(initial_.scope.id));
        auto* form = new QFormLayout;
        name_ = new QLineEdit(QString::fromStdString(initial_.name), this);
        name_->setObjectName("bodyName");
        form->addRow(tr("Název"), name_);
        active_ = new QCheckBox(tr("Aktivní těleso"), this);
        active_->setObjectName("bodyActive"); active_->setChecked(active);
        visible_ = new QCheckBox(tr("Viditelné"), this);
        visible_->setChecked(initial_.visible);
        form->addRow(active_); form->addRow(visible_);
        content_layout()->addLayout(form);
        placement_ = new zima::ui::ContainerPlacementSection(this,content_layout(),true,false,decimal_places);
        placement_->reference_table()->setObjectName("bodyReferenceTable");
        placement_->orientation_table()->setObjectName("bodyOrientationTable");
        placement_->initialize_numeric_values(initial_.scope.placement);
        placement_->initialize_from_references(initial_.scope.placement.references,
            [](const std::string& key) { return QString::fromStdString(key); });
        placement_->install_dof_label(content_layout());
        placement_->set_changed_callback([this] { if (preview_) preview_(pending_value()); });
        set_initial_size({360, 360});
    }
    void set_preview_callback(std::function<void(zima::document::BodyHistory)> callback) { preview_=std::move(callback); }
    void set_reference_request_callback(std::function<void(std::size_t)> callback) { placement_->set_reference_request_callback(std::move(callback)); }
    void set_reference_highlights_changed_callback(std::function<void()> callback) { placement_->set_highlights_changed_callback(std::move(callback)); }
    void set_forbidden_owner(std::function<bool(const std::string&)> callback) { forbidden_=std::move(callback); }
    bool owns_reference_owner(const std::string& owner) const override {
        return owner==initial_.scope.id || owner==initial_.origin().id || (forbidden_ && forbidden_(owner));
    }
    bool owns_parameter_owner(const std::string& owner) const override {
        return owner==initial_.scope.id;
    }
    auto references_without(std::size_t index) const -> std::vector<zima::document::ConstructionReference> override { return placement_->references_without(index); }
    bool set_reference(std::size_t index,zima::document::ConstructionReference reference,const QString& label) override {
        if (owns_reference_owner(reference.owner_id)) return false;
        QString error;
        const bool accepted=placement_->set_reference(index,std::move(reference),label,&error);
        placement_->reference_status_label()->setText(error);
        return accepted;
    }
    std::size_t first_empty_position_index() const override { return placement_->first_empty_position_index(); }
    void set_active_reference_index(std::optional<std::size_t> index) override { placement_->set_active_reference_index(index); }
    void set_reference_inspected(std::size_t index,bool inspected) override { placement_->set_reference_inspected(index,inspected); }
    void clear_reference_highlights() override { placement_->clear_reference_highlights(); }
    auto highlighted_reference_entries() const { return placement_->highlighted_reference_entries(); }
    void set_origin_selection_mode_callback(std::function<void(bool)> callback) override { placement_->set_origin_selection_mode_callback(std::move(callback)); }
    void set_origin_selection_mode_active(bool active) override { placement_->set_origin_selection_mode_active(active); }
    void set_translation_constraint_state(const zima::document::PointConstraintState& state,const zima::kernel::Vec3& solution) override { placement_->set_translation_constraint_state(state,solution); }
    void set_remaining_rotation_dof(int dof) override { placement_->set_remaining_rotation_dof(dof); }
    void set_rotation_constraint_state(const zima::document::OrientationConstraintState& state) override { placement_->set_rotation_constraint_state(state); }
    void set_orientation_base_rotation(const zima::kernel::Vec3& rotation,bool constrained) override { placement_->set_orientation_base_rotation(rotation,constrained); }
    void set_resolved_rotation(const zima::kernel::Vec3& rotation,bool valid=true) override { placement_->set_resolved_rotation(rotation,valid); }
    bool set_inline_parameter_value(std::string_view key,double value) override {
        const auto set_field = [value](QDoubleSpinBox* field) {
            if (!field || !field->isEnabled() || field->isReadOnly() || !field->isVisible()) return false;
            field->setValue(value);
            return true;
        };
        const std::array<std::string_view,3> keys{"placement:x","placement:y","placement:z"};
        const std::array<std::string_view,3> angles{
            "placement:rotation_x","placement:rotation_y","placement:rotation_z"};
        for (std::size_t index=0;index<keys.size();++index) {
            if (key==keys[index]) return set_field(placement_->translation_fields()[index]);
            if (key==angles[index]) {
                if(placement_->rotation_fields()[index]->isEnabled()&&placement_->rotation_fields()[index]->isReadOnly())return false;
                return set_field(placement_->rotation_fields()[index]) || set_field(placement_->rotation_offset_fields()[index]);
            }
        }
        constexpr std::string_view prefix="placement:reference_offset:";
        if (key.starts_with(prefix)) {
            key.remove_prefix(prefix.size());
            std::size_t index{};
            const auto [end,error]=std::from_chars(key.data(),key.data()+key.size(),index);
            if (error!=std::errc{} || end!=key.data()+key.size()) return false;
            return placement_->set_reference_offset(index,value);
        }
        return false;
    }
    zima::document::BodyHistory pending_value() const {
        auto value=initial_;
        value.name=name_->text().trimmed().toStdString();value.visible=visible_->isChecked();
        value.scope.placement=placement_->numeric_placement();
        value.scope.placement.references=placement_->combined_references(3);
        return value;
    }
protected:
    bool submit() override {
        auto value=pending_value();
        if (value.name.empty()) throw std::invalid_argument("Zadejte název tělesa.");
        commit_(std::move(value),active_->isChecked());return true;
    }
private:
    zima::document::BodyHistory initial_;
    Commit commit_;
    QLineEdit* name_{};
    QCheckBox* active_{};
    QCheckBox* visible_{};
    zima::ui::ContainerPlacementSection* placement_{};
    std::function<void(zima::document::BodyHistory)> preview_;
    std::function<bool(const std::string&)> forbidden_;
};

class BodyBooleanPropertiesDialog final : public zima::ui::PropertiesSubWindow {
public:
    using Commit = std::function<void(zima::document::BodyBoolean)>;
    BodyBooleanPropertiesDialog(zima::document::BodyBoolean initial,
        const std::vector<std::pair<std::string,std::string>>& inputs, Commit commit, QWidget* parent)
        : PropertiesSubWindow(tr("Vlastnosti Boolean"), parent), initial_(std::move(initial)), commit_(std::move(commit)) {
        setAttribute(Qt::WA_DeleteOnClose);
        auto* form = new QFormLayout;
        name_ = new QLineEdit(QString::fromStdString(initial_.name), this);
        name_->setObjectName("bodyBooleanName"); form->addRow(tr("Název"), name_);
        operation_ = new QComboBox(this); operation_->setObjectName("bodyBooleanOperation");
        for (const auto& [label, mode] : std::vector<std::pair<QString,zima::kernel::BodyCombination>>{
                {tr("Sjednocení"),zima::kernel::BodyCombination::Add},
                {tr("Rozdíl"),zima::kernel::BodyCombination::Subtract},
                {tr("Průnik"),zima::kernel::BodyCombination::Intersect}})
            operation_->addItem(label, static_cast<int>(mode));
        operation_->setCurrentIndex(operation_->findData(static_cast<int>(initial_.operation)));
        form->addRow(tr("Operace"), operation_);
        target_ = new QComboBox(this); target_->setObjectName("bodyBooleanTarget");
        tool_ = new QComboBox(this); tool_->setObjectName("bodyBooleanTool");
        for (const auto& [id, label] : inputs) {
            target_->addItem(QString::fromStdString(label), QString::fromStdString(id));
            tool_->addItem(QString::fromStdString(label), QString::fromStdString(id));
        }
        target_->setCurrentIndex(target_->findData(QString::fromStdString(initial_.target_id)));
        tool_->setCurrentIndex(tool_->findData(QString::fromStdString(initial_.tool_id)));
        form->addRow(tr("Cíl"), target_); form->addRow(tr("Nástroj"), tool_);
        content_layout()->addLayout(form); set_initial_size({360,240});
    }
protected:
    bool submit() override {
        auto value = initial_;
        value.name = name_->text().trimmed().toStdString();
        value.operation = static_cast<zima::kernel::BodyCombination>(operation_->currentData().toInt());
        value.target_id = target_->currentData().toString().toStdString();
        value.tool_id = tool_->currentData().toString().toStdString();
        if (value.name.empty() || value.target_id.empty() || value.tool_id.empty() || value.target_id == value.tool_id)
            throw std::invalid_argument("Zadejte název a dva různé vstupy Booleanu.");
        commit_(std::move(value)); return true;
    }
private:
    zima::document::BodyBoolean initial_;
    Commit commit_;
    QLineEdit* name_{};
    QComboBox* operation_{};
    QComboBox* target_{};
    QComboBox* tool_{};
};

} // namespace zima::app
