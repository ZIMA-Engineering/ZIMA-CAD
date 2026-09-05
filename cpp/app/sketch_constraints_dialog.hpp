#pragma once
#include "sketch_inference_policy.hpp"
#include <zima/ui/properties_subwindow.hpp>
#include <QAction>
#include <QDialogButtonBox>
#include <QGridLayout>
#include <QLabel>
#include <QPushButton>
#include <QButtonGroup>
#include <QVBoxLayout>
#include <functional>
namespace zima::app {
class SketchConstraintsDialog final : public ui::PropertiesSubWindow {
public:
    using Row=std::pair<std::optional<sketcher::ConstraintKind>,QAction*>;
    SketchConstraintsDialog(SketchInferenceSettings settings,const std::vector<Row>& rows,
            std::function<void(SketchInferenceSettings)> commit,QWidget* parent)
        : PropertiesSubWindow(tr("Vazby"),parent),pending_(std::move(settings)),commit_(std::move(commit)) {
        setObjectName("sketchConstraintsDialog");setAttribute(Qt::WA_DeleteOnClose);setMinimumWidth(280);set_initial_size(QSize(300,0));
        choices_=new QButtonGroup(this);
        auto* layout=new QGridLayout;
        layout->addWidget(new QLabel(tr("Vazba"),this),0,0);
        layout->addWidget(new QLabel(tr("Automaticky"),this),0,1);
        int row=1;
        for (const auto& [kind,action] : rows) {
            auto* choose=new QPushButton(action->icon(),action->text(),this);
            choose->setObjectName(action->objectName()+"Choose");
            choose->setEnabled(kind.has_value() || action->isEnabled());
            choose->setAutoDefault(false);
            choose->setCheckable(kind.has_value());
            choose->setStyleSheet("QPushButton:checked { background: #356E22; border: 1px solid #4DD811; color: white; }");
            choices_->addButton(choose);
            connect(action,&QAction::changed,this,[choose,action,kind] {
                if (!kind) choose->setEnabled(action->isEnabled());
            });
            choose->setToolTip(tr("Zadat tuto vazbu ručně"));
            connect(choose,&QPushButton::clicked,this,[this,action,choose,kind]{
                // Selection starts a command, never a dialog transaction.
                action->setEnabled(true);
                action->trigger();
                if (kind) { chosen_=action;choose->setChecked(true); }
            });
            if (!kind) {
                layout->addWidget(choose,row,0);layout->addWidget(new QLabel(tr("Ručně"),this),row++,1);continue;
            }
            auto* enabled=new QPushButton(this);
            enabled->setObjectName(action->objectName()+"Automatic");
            enabled->setCheckable(true);enabled->setChecked(pending_.enabled(*kind));
            enabled->setStyleSheet("QPushButton:checked { background: #356E22; border: 1px solid #4DD811; color: white; }");
            const auto update=[enabled](bool checked){enabled->setText(checked ? tr("✓ Zapnuto") : tr("Vypnuto"));};
            update(enabled->isChecked());
            connect(enabled,&QPushButton::toggled,this,[this,kind,update](bool checked){
                if (checked) pending_.disabled.erase(*kind);else pending_.disabled.insert(*kind);update(checked);
            });

            layout->addWidget(choose,row,0);layout->addWidget(enabled,row++,1);
        }
        content_layout()->addLayout(layout);
    }
    bool has_active_choice() const { return chosen_ != nullptr; }
    void clear_active_choice() {
        chosen_=nullptr;
        choices_->setExclusive(false);
        for (auto* button : choices_->buttons()) button->setChecked(false);
        choices_->setExclusive(true);
    }
protected:
    bool submit() override {
        commit_(pending_);
        return true;
    }
private:
    SketchInferenceSettings pending_;
    std::function<void(SketchInferenceSettings)> commit_;
    QAction* chosen_{};
    QButtonGroup* choices_{};
};
}
