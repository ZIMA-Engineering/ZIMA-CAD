#pragma once
#include "dimension_properties_fields.hpp"
#include <QApplication>
#include <QHeaderView>
#include <QMouseEvent>
#include <QPushButton>
#include <QSignalBlocker>
#include <QTableWidget>
#include <zima/drawing/measurement_dimension.hpp>
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
        set_initial_size({580, 560});
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
            {tr("Podle vazeb"), tr("Vodorovná"), tr("Svislá"), tr("Rovnoběžně s geometrií")});
        direction_->setCurrentIndex(int(value_.direction));
        form->addRow(tr("Kótovací čára"), direction_);
        references_ = new QTableWidget(binding);
        references_->setObjectName("drawingDimensionReferences");
        references_->setColumnCount(6);
        references_->setHorizontalHeaderLabels(
            {tr("Konec"), tr("Napojení"), tr("Reference"), QString(), tr("Druhá reference"), QString()});
        references_->verticalHeader()->hide();
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
            read_fields();
            const bool was = radial(),was_angular=angular();
            value_.kind = drawing::DrawingDimensionKind(index);
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
                if(suffix&&(suffix->text()=="mm"||suffix->text()==QString::fromUtf8("°"))){QSignalBlocker block(suffix);suffix->setText(angular()?QString::fromUtf8("°"):QStringLiteral("mm"));}
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
            read_fields();
            value_.direction = drawing::DimensionDirection(index);
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
        if (row >= int(value_.attachments.size())) {
            request.lines_only = true;
            return request;
        }
        if(angular()){request.lines_only=true;request.mode=int(drawing::DimensionAttachmentKind::Line);return request;}
        request.mode = modes_[row];
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
        active_ = -1;
        inspected_.clear();
        refresh_states();
        publish();
    }
    void set_mode(int kind) {
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
            if (active_ < 0 && (creating_ || extended_))
                placing_ = true;
        }
        rebuild_references();
        publish();
    }
    void extend(bool first) {
        if (radial() || angular())
            return;
        read_fields();
        drawing::extend_dimension_chain(value_, first, {});
        modes_.insert(first ? modes_.begin() : modes_.end(), -1);
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
            drawing::place_drawing_dimension(*view, value_, segment_, point);
            rebuild_placement();
        }
        if (finish)
            placing_ = false;
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
        read_fields();
        const auto *view = view_(value_.view_id);
        if (!view)
            throw std::invalid_argument("Vyberte pohled a vazby kóty.");
        drawing::validate_drawing_dimension(value_);
        const auto evaluation = drawing::evaluate_drawing_dimension(*view, value_);
        if (evaluation.state == drawing::MeasurementState::Unresolved)
            throw std::invalid_argument(evaluation.message);
        drawing::refresh_drawing_dimension(*view, value_);
        commit_(value_);
        return true;
    }

  private:
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
        if (placement_ && segment_ < int(value_.segments.size()))
            value_.segments[segment_].layout = placement_->value();
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
        first_->setEnabled(!radial()&&!angular());
        last_->setEnabled(!radial()&&!angular());
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
        references_->setRowCount(int(value_.attachments.size()) + (parallel ? 1 : 0));
        for (int row = 0; row < references_->rowCount(); ++row) {
            const bool axis = row == int(value_.attachments.size());
            references_->setItem(row, 0, new QTableWidgetItem(axis ? tr("Směr") : QString::number(row + 1)));
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
    void refresh_states() {
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
    bool creating_{}, extended_{}, placing_{}, rebuilding_{};
    int active_{-1}, segment_{};
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
