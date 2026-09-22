#pragma once
#include "dimension_properties_fields.hpp"
#include <QApplication>
#include <QDialogButtonBox>
#include <QHeaderView>
#include <QMouseEvent>
#include <QPushButton>
#include <QSignalBlocker>
#include <QTableWidget>
#include <zima/drawing/measurement_dimension.hpp>
#include <zima/sketcher/sketch.hpp>
#include <zima/kernel/stable_id.hpp>
#include <zima/ui/reference_cell.hpp>
namespace zima::app {
class DrawingDimensionDialog final : public ui::PropertiesSubWindow {
  public:
    using ViewResolver = std::function<const drawing::DrawingView *(const std::string &)>;
    DrawingDimensionDialog(drawing::DrawingDimension initial, bool creating, ViewResolver view,
                           std::function<void(drawing::DrawingDimension)> commit, QWidget *parent)
        : PropertiesSubWindow(tr("Vlastnosti kóty"), parent), value_(std::move(initial)),
          view_(std::move(view)), commit_(std::move(commit)), creating_(creating) {
        setObjectName("drawingDimensionProperties");

        automatic_placement_ = value_.direction == drawing::DimensionDirection::Automatic;
        set_initial_size({580, 400});
        setAttribute(Qt::WA_DeleteOnClose);
        tabs_ = new QTabWidget(this);
        content_layout()->addWidget(tabs_);
        auto *binding = new QWidget(tabs_);
        auto *column = new QVBoxLayout(binding);
        auto *form = new QFormLayout;
        column->addLayout(form);
        view_label_ = new QLabel(binding);
        view_label_->setObjectName("drawingDimensionView");
        form->addRow(tr("Pohled"), view_label_);
        type_ = new QComboBox(binding);
        type_->setObjectName("drawingDimensionType");
        type_->addItems({tr("Lineární"), tr("Poloměr"), tr("Průměr"), tr("Řetězová"), tr("Úhlová")});
        type_->setCurrentIndex(int(value_.kind));
        form->addRow(tr("Typ kóty"), type_);
        direction_ = new QComboBox(binding);
        direction_->setObjectName("drawingDimensionDirection");
        direction_->addItems(
            {creating ? tr("Automaticky podle tažení") : tr("Podle vazeb"), tr("Vodorovná"), tr("Svislá"), tr("Rovnoběžně s geometrií")});
        direction_->setCurrentIndex(int(value_.direction));
        form->addRow(tr("Kótovací čára"), direction_);
        references_ = new QTableWidget(binding);
        references_->setObjectName("drawingDimensionReferences");
        references_->setColumnCount(6);
        references_->setHorizontalHeaderLabels(
            {QString(), tr("Napojení"), tr("Reference"), QString(), tr("Druhá reference"), QString()});
        references_->verticalHeader()->show();
        references_->verticalHeader()->setMinimumSectionSize(32);
        references_->verticalHeader()->setDefaultSectionSize(32);
        references_->setSelectionMode(QAbstractItemView::NoSelection);
        references_->setEditTriggers(QAbstractItemView::NoEditTriggers);
        references_->horizontalHeader()->setSectionResizeMode(QHeaderView::ResizeToContents);
        references_->horizontalHeader()->setSectionResizeMode(2, QHeaderView::Stretch);
        references_->horizontalHeader()->setSectionResizeMode(4, QHeaderView::Stretch);
        ui::install_reference_cell_delegate(references_);
        column->addWidget(references_, 1);
        auto *chain = new QHBoxLayout;
        column->addLayout(chain);
        first_ = new QPushButton(tr("Přidat na začátek"), binding);
        last_ = new QPushButton(tr("Přidat na konec"), binding);
        first_->setObjectName("drawingDimensionExtendFirst");
        last_->setObjectName("drawingDimensionExtendLast");
        first_->setAutoDefault(false);
        last_->setAutoDefault(false);
        chain->addWidget(first_);
        chain->addWidget(last_);
        auto *hint =
            new QLabel(tr("Klikněte do reference a vyberte geometrii v pohledu. C = střed, T = tečna, I = "
                          "průsečík. RMB střídá nabízené možnosti; krátké MMB ukončí zadávání reference."),
                       binding);
        hint->setWordWrap(true);
        column->addWidget(hint);
        tabs_->addTab(binding, tr("Typ a vazby"));
        text_ = new DimensionTextFields(value_.style, tabs_);
        tabs_->addTab(text_, tr("Hodnota a tolerance"));
        auto *placement_page = new QWidget(tabs_);
        placement_column_ = new QVBoxLayout(placement_page);
        segments_ = new QComboBox(placement_page);
        segments_->setObjectName("drawingDimensionSegment");
        placement_column_->addWidget(segments_);
        tabs_->addTab(placement_page, tr("Umístění"));
        status_ = new QLabel(this);
        status_->setWordWrap(true);
        status_->setObjectName("drawingDimensionStatus");
        content_layout()->addWidget(status_);
        modes_.resize(value_.attachments.size(), creating ? -1 : 0);
        if (!creating)
            for (std::size_t i = 0; i < modes_.size(); ++i)
                modes_[i] = int(value_.attachments[i].kind);
        if(angular())std::fill(modes_.begin(),modes_.end(),int(drawing::DimensionAttachmentKind::Line));
        active_ = creating ? 0 : -1;
        rebuild();
        connect(references_, &QTableWidget::cellClicked, this, [this](int row, int col) {
            if(row==draft_row_){begin_branches();return;}
            branch_entry_=false;
            if (col != 2 && col != 4)
                return;
            if (col == 4 && (row >= int(modes_.size()) ||
                             modes_[row] != int(drawing::DimensionAttachmentKind::Intersection)))
                return;
            active_ = row * 2 + (col == 4);
            placing_ = false;
            refresh_states();
            publish();
        });
        connect(type_, &QComboBox::currentIndexChanged, this, [this](int index) {
            branch_entry_=false;
            read_fields();
            const bool was = radial(),was_angular=angular();
            value_.kind = drawing::DrawingDimensionKind(index);
            value_.chain_datum_only=false;
            value_.chain_direction.reset();
            if (radial() != was) {
                value_.attachments.assign(radial() ? 1 : 2, {});
                value_.segments.clear();
                value_.parallel_reference = {};
                value_.anchor_attachment = 0;
                active_ = 0;
            }
            if (value_.kind == drawing::DrawingDimensionKind::Linear && value_.attachments.size() > 2) {
                value_.attachments.resize(2);
                value_.anchor_attachment = 0;
            }
            if(angular()) {
                value_.attachments.resize(2);value_.anchor_attachment=0;value_.direction=drawing::DimensionDirection::Automatic;value_.parallel_reference={};
                {QSignalBlocker block(direction_);direction_->setCurrentIndex(0);}
                for(auto& attachment:value_.attachments)if(attachment.kind!=drawing::DimensionAttachmentKind::Line)attachment={};
            }
            if(angular()!=was_angular) {
                for(auto& segment:value_.segments){segment.layout={};segment.last_presentation.reset();segment.last_angular_leaders=false;}
                auto* suffix=text_->findChild<QLineEdit*>("sketchDimensionSuffix");
                if(suffix&&(suffix->text().isEmpty()||suffix->text()=="mm"||suffix->text()==QString::fromUtf8("°"))){QSignalBlocker block(suffix);suffix->setText(angular()?QString::fromUtf8("°"):QString{});}
            }
            drawing::resize_dimension_segments(value_);
            modes_.resize(value_.attachments.size(), -1);
            if(angular()) {
                std::fill(modes_.begin(),modes_.end(),int(drawing::DimensionAttachmentKind::Line));active_=-1;
                for(std::size_t i=0;i<value_.attachments.size();++i)if(!value_.attachments[i].reference.valid()){active_=int(i)*2;break;}
                placing_=active_<0&&creating_;
            }
            segment_ = 0;
            inspected_.clear();
            rebuild();
            publish();
        });
        connect(direction_, &QComboBox::currentIndexChanged, this, [this](int index) {
            if(branch_entry_){branch_entry_=false;active_=-1;}
            read_fields();
            value_.direction = drawing::DimensionDirection(index);
            value_.chain_direction.reset();
            automatic_placement_ = index == 0;
            if (value_.direction == drawing::DimensionDirection::Parallel)
                active_ = int(value_.attachments.size()) * 2;
            rebuild_references();
            publish();
        });
        connect(first_, &QPushButton::clicked, this, [this] { extend(true); });
        connect(last_, &QPushButton::clicked, this, [this] { extend(false); });
        connect(segments_, &QComboBox::currentIndexChanged, this, [this](int index) {
            if (rebuilding_)
                return;
            read_fields();
            segment_ = std::max(0, index);
            rebuild_placement();
        });
        for (auto *input : text_->findChildren<QLineEdit *>())
            connect(input, &QLineEdit::textChanged, this, [this] { publish(); });
        for (auto *input : text_->findChildren<QSpinBox *>())
            connect(input, qOverload<int>(&QSpinBox::valueChanged), this, [this] { publish(); });
        for (auto *input : text_->findChildren<QComboBox *>())
            connect(input, &QComboBox::currentIndexChanged, this, [this] { publish(); });
    }
    void enable_chain_command() {
        chain_command_=true;
        if(value_.kind!=drawing::DrawingDimensionKind::Chain)type_->setCurrentIndex(int(drawing::DrawingDimensionKind::Chain));
        type_->setEnabled(false);rebuild();
    }
    bool awaiting_chain_seed() const {
        return chain_command_&&!extended_&&std::ranges::none_of(value_.attachments,[](const auto& a){return a.reference.valid();});
    }
    void adopt_chain_seed(const drawing::DrawingDimension& source) {
        if(!awaiting_chain_seed()||source.kind!=drawing::DrawingDimensionKind::Chain)return;
        value_=source;value_.id=kernel::make_stable_id();
        if(value_.chain_group.empty())value_.chain_group=source.id;
        if(!value_.chain_direction)if(const auto* view=view_(value_.view_id)) {
            const auto before=drawing::evaluate_drawing_dimension(*view,value_);
            auto probe=value_;for(auto& s:probe.segments)s.layout.line_offset+=1;
            const auto after=drawing::evaluate_drawing_dimension(*view,probe);
            if(!before.presentations.empty()&&!after.presentations.empty()) {
                const auto a=before.presentations.front().line_first,b=after.presentations.front().line_first;
                value_.chain_direction=drawing::Point2{b.y-a.y,a.x-b.x};
            }
        }
        value_.attachments={source.attachments[source.anchor_attachment],source.attachments[source.anchor_attachment?0:1]};
        value_.anchor_attachment=0;value_.segments={source.segments.front()};value_.segments.front().id=kernel::make_stable_id();
        value_.chain_datum_only=true;seed_adopted_=true;extended_=true;automatic_placement_=false;
        modes_={int(value_.attachments[0].kind),int(value_.attachments[1].kind)};segment_=0;placing_=false;active_=-1;
        {QSignalBlocker block(direction_);direction_->setCurrentIndex(int(value_.direction));}
        tabs_->removeTab(1);delete text_;text_=new DimensionTextFields(value_.style,tabs_);tabs_->insertTab(1,text_,tr("Hodnota a tolerance"));
        for(auto* input:text_->findChildren<QLineEdit*>())connect(input,&QLineEdit::textChanged,this,[this]{publish();});
        for(auto* input:text_->findChildren<QSpinBox*>())connect(input,qOverload<int>(&QSpinBox::valueChanged),this,[this]{publish();});
        for(auto* input:text_->findChildren<QComboBox*>())connect(input,qOverload<int>(&QComboBox::currentIndexChanged),this,[this]{publish();});
        rebuild();begin_branches();
    }
    void set_changed(std::function<void()> callback) {
        changed_ = std::move(callback);
        publish();
    }
    const drawing::DrawingDimension &value() const { return value_; }
    bool entering() const { return active_ >= 0; }
    bool placing() const { return placing_; }
    int active_reference() const { return active_; }
    drawing::MeasurementPickRequest pick_request() const {
        drawing::MeasurementPickRequest request;
        if (active_ < 0)
            return request;
        const auto row = active_ / 2;
        if (row >= int(value_.attachments.size()) && row!=draft_row_) {
            request.lines_only = true;
            return request;
        }
        if(angular()){request.lines_only=true;request.mode=int(drawing::DimensionAttachmentKind::Line);return request;}
        request.mode = row==draft_row_?-1:modes_[row];
        request.circles_only = radial();
        if (const auto *view = view_(value_.view_id)) {
            if (value_.direction == drawing::DimensionDirection::Vertical)
                request.tangent_direction = {0, 1};
            const auto &first = value_.attachments[value_.anchor_attachment];
            const auto direction_reference = value_.direction == drawing::DimensionDirection::Parallel
                                                 ? value_.parallel_reference
                                                 : first.reference;
            if (value_.direction == drawing::DimensionDirection::Parallel ||
                (value_.direction == drawing::DimensionDirection::Automatic &&
                 first.kind == drawing::DimensionAttachmentKind::Line)) {
                for (const auto &curve : drawing::projected_measurement_curves(*view))
                    if (curve.source == direction_reference && curve.line) {
                        const double x = curve.points.back().x - curve.points.front().x,
                                     y = curve.points.back().y - curve.points.front().y,
                                     size = std::hypot(x, y);
                        if (size > 1e-9)
                            request.tangent_direction =
                                value_.direction == drawing::DimensionDirection::Parallel
                                    ? drawing::Point2{x / size, y / size}
                                    : drawing::Point2{-y / size, x / size};
                    }
            } else if (value_.direction == drawing::DimensionDirection::Automatic &&
                       row != int(value_.anchor_attachment))
                request.tangent_origin = drawing::resolve_dimension_attachment(*view, first);
        }
        if (request.mode == int(drawing::DimensionAttachmentKind::Intersection) && (active_ % 2))
            request.intersection_first = value_.attachments[row].reference;
        if (row != int(value_.anchor_attachment) &&
            value_.attachments[value_.anchor_attachment].kind == drawing::DimensionAttachmentKind::Line)
            request.parallel_line = value_.attachments[value_.anchor_attachment].reference;
        return request;
    }
    std::vector<kernel::EdgeReference> inspected_references() const {
        std::vector<kernel::EdgeReference> result;
        for (int slot : inspected_) {
            const auto ref = reference(slot);
            if (ref.valid())
                result.push_back(ref);
        }
        return result;
    }
    void end_entry() {
        branch_entry_=false;
        active_ = -1;
        inspected_.clear();
        refresh_states();
        publish();
    }
    void set_mode(int kind) {
        if(branch_entry_&&active_>=0&&active_/2==draft_row_)extend(false);
        if (active_ < 0 || active_ / 2 >= int(modes_.size()) || radial() || angular())
            return;
        read_fields();
        const auto row = active_ / 2;
        modes_[row] = kind;
        value_.attachments[row] = {};
        active_ = row * 2;
        rebuild_references();
        publish();
    }
    void accept_candidate(const std::string &view_id, const drawing::MeasurementCandidate &candidate) {
        if (active_ < 0 || (!value_.view_id.empty() && value_.view_id != view_id))
            return;
        if(angular()&&candidate.attachment.kind!=drawing::DimensionAttachmentKind::Line)return;
        if(branch_entry_&&active_/2==draft_row_)extend(false);
        read_fields();
        value_.view_id = view_id;
        const auto row = active_ / 2;
        if (row >= int(value_.attachments.size())) {
            value_.parallel_reference = candidate.attachment.reference;
            active_ = -1;
        } else if (modes_[row] == int(drawing::DimensionAttachmentKind::Intersection) && active_ % 2 == 0) {
            value_.attachments[row] = {drawing::DimensionAttachmentKind::Intersection,
                                       candidate.attachment.reference};
            active_ = row * 2 + 1;
        } else {
            value_.attachments[row] = candidate.attachment;
            if(value_.chain_datum_only&&row==0&&candidate.attachment.kind==drawing::DimensionAttachmentKind::Line) {
                value_.attachments[1]=candidate.attachment;modes_[1]=int(candidate.attachment.kind);
            }
            // The picker owns the semantic binding: a requested point may be
            // a persisted vertex, a curve endpoint or the centre of an axis.
            active_ = -1;
            for (std::size_t i = 0; i < value_.attachments.size(); ++i)
                if (!value_.attachments[i].reference.valid() ||
                    (value_.attachments[i].kind == drawing::DimensionAttachmentKind::Intersection &&
                     !value_.attachments[i].other_reference.valid())) {
                    active_ = int(i) * 2;
                    break;
                }
            if (active_ < 0 && creating_ && !extended_)
                placing_ = true;
        }
        rebuild_references();
        if(branch_entry_&&bindings_complete()&&draft_row_>=0){active_=draft_row_*2;refresh_states();}
        publish();
    }
    void extend(bool first) {
        if (radial() || angular())
            return;
        read_fields();
        const bool datum_only=value_.chain_datum_only;
        drawing::extend_dimension_chain(value_, first, {});
        branch_entry_=value_.kind==drawing::DrawingDimensionKind::Chain;
        if(datum_only){modes_={int(value_.attachments[0].kind),-1};first=false;}
        else modes_.insert(first ? modes_.begin() : modes_.end(), -1);
        segment_ = first ? 0 : int(value_.segments.size()) - 1;
        active_ = first ? 0 : (int(value_.attachments.size()) - 1) * 2;
        extended_ = true;
        placing_ = false;
        inspected_.clear();
        {
            QSignalBlocker block(type_);
            type_->setCurrentIndex(int(value_.kind));
        }
        rebuild();
        publish();
    }
    void position(drawing::Point2 point, bool finish) {
        if (!placing_)
            return;
        read_fields();
        if (const auto *view = view_(value_.view_id)) {
            if (automatic_placement_ && creating_ && !extended_ &&
                (value_.kind == drawing::DrawingDimensionKind::Linear || value_.kind == drawing::DrawingDimensionKind::Chain) && value_.attachments.size() == 2 &&
                std::ranges::none_of(value_.attachments, [](const auto& a) {
                    return a.kind == drawing::DimensionAttachmentKind::Line ||
                           a.kind == drawing::DimensionAttachmentKind::Tangent;
                })) {
                const auto first = drawing::resolve_dimension_attachment(*view, value_.attachments[0]);
                const auto second = drawing::resolve_dimension_attachment(*view, value_.attachments[1]);
                if (first && second) {
                    if(value_.chain_datum_only)value_.chain_direction.reset();
                    const auto kind = sketcher::classify_linear_dimension(
                        {first->x, first->y}, {second->x, second->y}, {point.x, point.y});
                    value_.direction = kind == sketcher::DimensionKind::DistanceX
                        ? drawing::DimensionDirection::Horizontal
                        : kind == sketcher::DimensionKind::DistanceY
                            ? drawing::DimensionDirection::Vertical : drawing::DimensionDirection::Automatic;
                }
            }
            drawing::place_drawing_dimension(*view, value_, segment_, point);
            rebuild_placement();
        }
        if (finish)
            placing_ = false;
        if(value_.chain_datum_only)last_->setEnabled(value_.chain_direction.has_value()&&!placing_);
        if(finish){rebuild_references();if(chain_command_)begin_branches();}
        publish();
    }
    void change_presentation(drawing::DrawingDimension value) {
        read_fields();
        value_ = std::move(value);
        rebuild_placement();
        publish();
    }

  protected:
    bool eventFilter(QObject *watched, QEvent *event) override {
        if(watched->property("dimensionEntryRow").isValid()&&event->type()==QEvent::MouseButtonRelease&&static_cast<QMouseEvent*>(event)->button()==Qt::LeftButton){
            arm_row(watched->property("dimensionEntryRow").toInt());return true;
        }
        const auto *widget = qobject_cast<QWidget *>(watched);
        if (isVisible() && widget && parentWidget() &&
            (widget == parentWidget() || parentWidget()->isAncestorOf(widget))) {
            if (event->type() == QEvent::MouseButtonPress) {
                const auto *mouse = static_cast<QMouseEvent *>(event);
                if (mouse->button() == Qt::MiddleButton) {
                    middle_origin_ = mouse->globalPosition();
                    middle_pending_ = true;
                }
                if ((mouse->buttons() & Qt::MiddleButton) && (mouse->buttons() & Qt::RightButton))
                    middle_pending_ = false;
            } else if (event->type() == QEvent::MouseMove && middle_pending_) {
                if ((static_cast<QMouseEvent *>(event)->globalPosition() - middle_origin_)
                        .manhattanLength() >= QApplication::startDragDistance())
                    middle_pending_ = false;
            } else if (event->type() == QEvent::MouseButtonRelease) {
                const auto *mouse = static_cast<QMouseEvent *>(event);
                if (mouse->button() == Qt::MiddleButton && middle_pending_) {
                    middle_pending_ = false;
                    end_entry();
                }
            }
        }
        return PropertiesSubWindow::eventFilter(watched, event);
    }
    bool submit() override {
        if(seed_adopted_&&value_.chain_datum_only)return true;
        read_fields();
        if(!bindings_complete())return false;
        if(value_.chain_datum_only&&!value_.chain_direction)if(const auto* view=view_(value_.view_id)) {
            const auto evaluation=drawing::evaluate_drawing_dimension(*view,value_);
            if(!evaluation.presentations.empty()) {
                const auto p=evaluation.presentations.front().line_first;
                drawing::place_drawing_dimension(*view,value_,0,{p.x,p.y});
            }
        }
        commit_(value_);
        return true;
    }

  private:
    bool bindings_complete() const {
        return !value_.view_id.empty()&&
            std::ranges::all_of(value_.attachments,[](const auto& a){return a.reference.valid()&&
                (a.kind!=drawing::DimensionAttachmentKind::Intersection||a.other_reference.valid());})&&
            (radial()||angular()||value_.direction!=drawing::DimensionDirection::Parallel||value_.parallel_reference.valid());
    }
    bool angular() const {return value_.kind==drawing::DrawingDimensionKind::Angular;}
    bool radial() const {
        return value_.kind == drawing::DrawingDimensionKind::Radius ||
               value_.kind == drawing::DrawingDimensionKind::Diameter;
    }
    kernel::EdgeReference reference(int slot) const {
        if (slot / 2 >= int(value_.attachments.size()))
            return value_.parallel_reference;
        return slot % 2 ? value_.attachments[slot / 2].other_reference
                        : value_.attachments[slot / 2].reference;
    }
    void read_fields() {
        if (rebuilding_)
            return;
        value_.style = text_->value();
        if (placement_ && segment_ < int(value_.segments.size())) {
            value_.segments[segment_].layout = placement_->value();
            if (value_.kind == drawing::DrawingDimensionKind::Chain)
                for (auto& segment : value_.segments)
                    segment.layout.line_offset = value_.segments[segment_].layout.line_offset;
        }
    }
    void publish() {
        if (rebuilding_)
            return;
        read_fields();
        const auto *view = view_(value_.view_id);
        view_label_->setText(view ? QString::fromStdString(view->name) : tr("Vyberte geometrii pohledu"));
        QString message = tr("Vyberte geometrické vazby kóty.");
        if (view) {
            const auto evaluation = drawing::evaluate_drawing_dimension(*view, value_);
            message = tr(evaluation.message.c_str());
            if (view->measurement_geometry->curves.empty())
                message = tr("Pro zadání vazeb nejprve regenerujte tento pohled.");
            if (evaluation.state == drawing::MeasurementState::Resolved) {
                QStringList values;
                for (const auto &d : evaluation.presentations)
                    values << QString::fromStdString(drawing::drawing_dimension_text(value_, d));
                message = values.join("  ·  ");
                drawing::refresh_drawing_dimension(*view, value_);
            }
        }
        if (placing_)
            message += tr(" — LMB umístí kótu.");
        status_->setText(message);
        refresh_states();
        if (changed_)
            changed_();
    }
    void rebuild() {
        rebuilding_ = true;
        direction_->setEnabled(!radial()&&!angular());
        direction_->setItemText(0,creating_&&value_.kind!=drawing::DrawingDimensionKind::Chain?tr("Automaticky podle tažení"):tr("Podle vazeb"));
        first_->setEnabled(!radial()&&!angular());
        last_->setEnabled(!radial()&&!angular());
        if(value_.chain_datum_only)last_->setEnabled(value_.chain_direction.has_value()&&!placing_);
        first_->hide();
        last_->hide();
        last_->setText(value_.kind==drawing::DrawingDimensionKind::Chain?tr("Přidat větev"):tr("Přidat na konec"));
        segments_->clear();
        for (std::size_t i = 0; i < value_.segments.size(); ++i)
            segments_->addItem(tr("Úsek %1").arg(i + 1));
        segments_->setCurrentIndex(segment_);
        segments_->setVisible(value_.segments.size() > 1);
        rebuilding_ = false;
        rebuild_references();
        rebuild_placement();
    }
    void rebuild_placement() {
        rebuilding_ = true;
        if (placement_) {
            placement_column_->removeWidget(placement_);
            delete placement_;
            placement_ = nullptr;
        }
        if (segment_ < int(value_.segments.size())) {
            kernel::ViewerDimension d;
            d.kind = radial() ? (value_.kind == drawing::DrawingDimensionKind::Radius
                                     ? kernel::ViewerDimensionKind::Radius
                                     : kernel::ViewerDimensionKind::Diameter)
                              : angular()?kernel::ViewerDimensionKind::Angular:kernel::ViewerDimensionKind::Linear;
            placement_ = new DimensionPlacementFields(d, value_.segments[segment_].layout, tabs_, true);
            if(value_.kind==drawing::DrawingDimensionKind::Chain) {
                auto* form=qobject_cast<QFormLayout*>(placement_->layout());
                form->setRowVisible(placement_->findChild<QDoubleSpinBox*>("dimensionTextAlong"),false);
            }
            placement_column_->addWidget(placement_);
            for (auto *input : placement_->findChildren<QDoubleSpinBox *>())
                connect(input, qOverload<double>(&QDoubleSpinBox::valueChanged), this, [this] { publish(); });
            for (auto *input : placement_->findChildren<QCheckBox *>())
                connect(input, &QCheckBox::toggled, this, [this] { publish(); });
        }
        rebuilding_ = false;
    }
    void rebuild_references() {
        rebuilding_ = true;
        const bool parallel = !radial() && !angular() && value_.direction == drawing::DimensionDirection::Parallel;
        const bool offer_branch=chain_command_&&value_.kind==drawing::DrawingDimensionKind::Chain&&!placing_&&
            (!value_.chain_datum_only||value_.chain_direction.has_value())&&
            bindings_complete();
        const int stored_rows=int(value_.attachments.size())+(parallel?1:0);
        draft_row_=offer_branch?stored_rows:-1;
        for(auto* action:references_->findChildren<QWidget*>())if(action->objectName().startsWith("tableRowAction"))action->setObjectName({});
        references_->clearContents();
        references_->setRowCount(stored_rows+(offer_branch?1:0));
        const bool intersections=std::ranges::find(modes_,int(drawing::DimensionAttachmentKind::Intersection))!=modes_.end();
        references_->setColumnHidden(4,!intersections);references_->setColumnHidden(5,!intersections);
        bool preceding_complete=true;
        for (int row = 0; row < references_->rowCount(); ++row) {
            if(row==draft_row_) {
                references_->setRowHidden(row,false);
                references_->setItem(row,2,new ui::ReferenceCellItem(tr("Přidat větev")));
                references_->setVerticalHeaderItem(row,new QTableWidgetItem(QString()));
                set_row_action(row,false);
                continue;
            }
            const bool axis = row == int(value_.attachments.size());
            const bool has_reference=reference(row*2).valid();
            const bool complete=has_reference&&(axis||modes_[row]!=int(drawing::DimensionAttachmentKind::Intersection)||reference(row*2+1).valid());
            const bool hidden=(chain_command_&&offer_branch&&!value_.chain_datum_only&&!axis&&std::size_t(row)!=value_.anchor_attachment)||
                (chain_command_&&!offer_branch&&!value_.chain_datum_only&&!axis&&std::size_t(row)!=value_.anchor_attachment&&row!=int(value_.attachments.size())-1)||
                (!axis&&!preceding_complete&&!has_reference)||
                (value_.chain_datum_only&&row==1&&(seed_adopted_||value_.attachments[0].kind==drawing::DimensionAttachmentKind::Line));
            references_->setRowHidden(row,hidden);
            if(!axis)preceding_complete=preceding_complete&&complete;
            const auto label=axis?tr("Směr"):value_.kind==drawing::DrawingDimensionKind::Chain?
                (std::size_t(row)==value_.anchor_attachment?QStringLiteral("0"):value_.chain_datum_only?tr("Směr"):QString::number(row<int(value_.anchor_attachment)?row+1:row)):
                QString::number(row+1);
            references_->setVerticalHeaderItem(row,new QTableWidgetItem(label));
            const bool populated=reference(row*2).valid()&&
                (axis||modes_[row]!=int(drawing::DimensionAttachmentKind::Intersection)||reference(row*2+1).valid());
            set_row_action(row,populated);
            if (!axis) {
                auto *mode = new QComboBox(references_);
                mode->setObjectName(QString("dimensionAttachmentMode%1").arg(row));
                mode->addItem(tr("Automaticky"), -1);
                mode->addItem(tr("Bod"), int(drawing::DimensionAttachmentKind::Point));
                mode->addItem(tr("Bod na křivce"), int(drawing::DimensionAttachmentKind::CurvePoint));
                mode->addItem(tr("Úsečka"), int(drawing::DimensionAttachmentKind::Line));
                mode->addItem(tr("Střed (C)"), int(drawing::DimensionAttachmentKind::Center));
                mode->addItem(tr("Tečna (T)"), int(drawing::DimensionAttachmentKind::Tangent));
                mode->addItem(tr("Průsečík (I)"), int(drawing::DimensionAttachmentKind::Intersection));
                mode->setCurrentIndex(mode->findData(modes_[row]));
                mode->setEnabled(!radial()&&!angular());
                references_->setCellWidget(row, 1, mode);
                connect(mode, &QComboBox::currentIndexChanged, this, [this, row, mode] {
                    if (rebuilding_)
                        return;
                    active_ = row * 2;
                    set_mode(mode->currentData().toInt());
                });
            }
            for (int side = 0; side < 2; ++side) {
                const int slot = row * 2 + side, col = side ? 4 : 2;
                const auto ref = reference(slot);
                const bool enabled =
                    side == 0 ||
                    (!axis && modes_[row] == int(drawing::DimensionAttachmentKind::Intersection));
                auto *item = new ui::ReferenceCellItem(enabled ? tr("Vyberte…") : QString());
                if (enabled && ref.valid()) {
                    const auto label = QString::fromStdString(ref.semantic_key);
                    item->setText(label);
                    item->set_reference(QString::fromStdString(ref.owner_id + "/" + ref.semantic_key + "@" +
                                                               ref.instance_path));
                    item->setToolTip(item->reference());
                }
                references_->setItem(row, col, item);
                references_->setCellWidget(
                    row, col + 1,
                    ui::centered_cell_widget(ui::build_reference_inspection_button(
                        enabled && ref.valid(), inspected_.contains(slot), [this, slot](bool on) {
                            if (on)
                                inspected_.insert(slot);
                            else
                                inspected_.erase(slot);
                            refresh_states();
                            if (changed_)
                                changed_();
                        })));
            }
        }
        rebuilding_ = false;
        refresh_states();
    }
    void begin_branches() {
        if(draft_row_<0)return;
        branch_entry_=true;active_=draft_row_*2;placing_=false;refresh_states();publish();
    }
    void arm_row(int row) {
        if(row==draft_row_){begin_branches();return;}
        branch_entry_=false;active_=row*2;
        if(row<int(modes_.size())&&modes_[row]==int(drawing::DimensionAttachmentKind::Intersection)&&reference(active_).valid())++active_;
        placing_=false;refresh_states();publish();
    }
    void set_row_action(int row,bool populated) {
        auto* indicator=ui::build_reference_row_indicator([this,row]{remove_reference_row(row);});
        indicator->setObjectName(QString("tableRowAction%1").arg(row));
        ui::set_reference_row_populated(indicator,populated);
        auto* arrow=indicator->property("_arrowWidget").value<QObject*>();
        arrow->setProperty("dimensionEntryRow",row);arrow->installEventFilter(this);
        references_->setCellWidget(row,0,ui::centered_cell_widget(indicator));
    }
    void remove_reference_row(int row) {
        branch_entry_=false;
        read_fields();placing_=false;inspected_.clear();
        if(row>=int(value_.attachments.size())) {
            value_.parallel_reference={};active_=row*2;
        }else if(value_.kind==drawing::DrawingDimensionKind::Chain&&!value_.chain_datum_only&&std::size_t(row)!=value_.anchor_attachment) {
            const auto segment=std::size_t(row)<value_.anchor_attachment?std::size_t(row):std::size_t(row)-1;
            if(const auto* view=view_(value_.view_id))drawing::erase_dimension_branch(*view,value_,value_.segments[segment].id);
            modes_.clear();for(const auto& a:value_.attachments)modes_.push_back(int(a.kind));
            active_=-1;segment_=std::min(segment_,int(value_.segments.size())-1);
        }else {
            if(value_.chain_datum_only)value_.chain_direction.reset();
            if(value_.chain_datum_only&&row==0){value_.attachments[1]={};modes_[1]=-1;}
            value_.attachments[row]={};modes_[row]=angular()?int(drawing::DimensionAttachmentKind::Line):-1;active_=row*2;
        }
        rebuild();publish();
    }
    void refresh_states() {
        buttons()->button(QDialogButtonBox::Ok)->setEnabled(bindings_complete());
        std::erase_if(inspected_, [&](int slot) { return !reference(slot).valid(); });
        const auto *view = view_(value_.view_id);
        std::optional<drawing::DimensionEvaluation> evaluation;
        if (view)
            evaluation = drawing::evaluate_drawing_dimension(*view, value_);
        for (int row = 0; row < references_->rowCount(); ++row)
            for (int side = 0; side < 2; ++side)
                if (auto *item =
                        dynamic_cast<ui::ReferenceCellItem *>(references_->item(row, side ? 4 : 2))) {
                    item->set_active_input(active_ == row * 2 + side);
                    item->set_inspected(inspected_.contains(row * 2 + side));
                    item->set_missing(item->has_reference() && evaluation &&
                                      (row < int(evaluation->resolved_attachments.size())
                                           ? !evaluation->resolved_attachments[row]
                                           : !evaluation->direction_resolved));
                }
        references_->viewport()->update();
    }
    QPointF middle_origin_;
    bool middle_pending_{};
    drawing::DrawingDimension value_;
    ViewResolver view_;
    std::function<void(drawing::DrawingDimension)> commit_;
    std::function<void()> changed_;
    bool creating_{}, extended_{}, placing_{}, rebuilding_{}, branch_entry_{}, chain_command_{}, seed_adopted_{};
    bool automatic_placement_{true};
    int active_{-1}, segment_{}, draft_row_{-1};
    std::vector<int> modes_;
    std::set<int> inspected_;
    QTabWidget *tabs_{};
    QComboBox *type_{}, *direction_{}, *segments_{};
    QTableWidget *references_{};
    QLabel *view_label_{}, *status_{};
    QPushButton *first_{}, *last_{};
    DimensionTextFields *text_{};
    DimensionPlacementFields *placement_{};
    QVBoxLayout *placement_column_{};
};
} // namespace zima::app
