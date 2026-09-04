#include "primitive_properties_dialog.hpp"

#include <zima/ui/reference_cell.hpp>

#include <QAction>
#include <QComboBox>
#include <QCheckBox>
#include <QDoubleSpinBox>
#include <QEvent>
#include <QFile>
#include <QFormLayout>
#include <QFont>
#include <QHBoxLayout>
#include <QHeaderView>
#include <QBrush>
#include <QColor>
#include <QLabel>
#include <QLineEdit>
#include <QListWidget>
#include <QMouseEvent>
#include <QPalette>
#include <QPushButton>
#include <QSignalBlocker>
#include <QSizePolicy>
#include <QStringList>
#include <QTableWidget>
#include <QToolButton>
#include <QTreeWidget>

#include <exception>
#include <algorithm>
#include <cmath>
#include <numbers>
#include <limits>

namespace zima::app {
namespace {

struct ThreadCatalogSize {
    QString designation;
    double nominal_diameter{};
    double pitch{};
    double internal_root_diameter{};
    double external_root_diameter{};
    bool preferred{};
};

double thread_catalog_number(QString text, bool* ok=nullptr) {
    text=text.trimmed();
    text.replace(',', '.');
    bool local_ok{};
    const double value=text.toDouble(&local_ok);
    if (ok) *ok=local_ok;
    return value;
}

std::vector<ThreadCatalogSize> load_thread_catalog(const QString& standard) {
    const QString resource=standard=="metric"
        ? ":/zima/data/threads/metric_iso.tsv"
        : standard=="whitworth"
        ? ":/zima/data/threads/whitworth_bsw.tsv"
        : ":/zima/data/threads/pipe_iso228.tsv";
    QFile file(resource);
    if (!file.open(QIODevice::ReadOnly|QIODevice::Text)) return {};
    std::vector<ThreadCatalogSize> result;
    while (!file.atEnd()) {
        const auto fields=QString::fromUtf8(file.readLine()).trimmed().split('\t');
        if (standard=="metric" && fields.size()>=6) {
            bool ok_d{},ok_p{},ok_d1{},ok_d3{};
            const double d=thread_catalog_number(fields[0],&ok_d);
            const double p=thread_catalog_number(fields[1],&ok_p);
            const double d1=thread_catalog_number(fields[3],&ok_d1);
            const double d3=thread_catalog_number(fields[4],&ok_d3);
            if (!(ok_d&&ok_p&&ok_d1&&ok_d3)) continue;
            QString designation=fields[5].trimmed();
            designation.replace("x",QStringLiteral("×"));
            designation.remove(' ');
            result.push_back({designation,d,p,d1,d3,
                !designation.contains(QStringLiteral("×"))});
        } else if (standard=="whitworth" && fields.size()>=8) {
            bool ok_p{},ok_d{},ok_root{};
            const double p=thread_catalog_number(fields[2],&ok_p);
            const double d=thread_catalog_number(fields[3],&ok_d);
            const double root=thread_catalog_number(fields[7],&ok_root);
            if (!(ok_p&&ok_d&&ok_root)) continue;
            const auto designation=fields[0].trimmed();
            const bool common=designation=="W 3/8" || designation=="W 1/2" ||
                designation=="W 5/8" || designation=="W 3/4" ||
                designation=="W 1";
            result.push_back({designation,d,p,root,root,common});
        } else if (standard=="pipe" && fields.size()>=4) {
            bool ok_d{},ok_pitch_diameter{},ok_root{};
            const double d=thread_catalog_number(fields[1],&ok_d);
            const double pitch_diameter=thread_catalog_number(fields[2],
                &ok_pitch_diameter);
            const double root=thread_catalog_number(fields[3],&ok_root);
            if (!(ok_d&&ok_pitch_diameter&&ok_root)) continue;
            const double pitch=(d-pitch_diameter)/0.640327;
            const auto designation=fields[0].trimmed();
            const bool common=designation=="G 1/4" || designation=="G 3/8" ||
                designation=="G 1/2" || designation=="G 3/4" ||
                designation=="G 1";
            result.push_back({designation,d,pitch,root,root,common});
        }
    }
    return result;
}

// Mirrors ConstructionPropertiesDialog's readable_reference_kind() so a
// picked position/orientation reference reads the same way in every
// container's placement UI.
QString readable_placement_reference_kind(const std::string& semantic) {
    const auto key = QString::fromStdString(semantic);
    if (key == QStringLiteral("point") || key.contains(QStringLiteral("point")))
        return QObject::tr("Bod");
    if (key.startsWith(QStringLiteral("origin:axis:")))
        return QObject::tr("Osa %1").arg(key.sliced(12).toUpper());
    if (key == QStringLiteral("axis") || key.contains(QStringLiteral("axis")))
        return QObject::tr("Osa");
    if (key.startsWith(QStringLiteral("origin:plane:")))
        return QObject::tr("Rovina %1").arg(key.sliced(13).toUpper());
    if (key == QStringLiteral("plane") || key.contains(QStringLiteral("plane")))
        return QObject::tr("Rovina");
    if (key.contains(QStringLiteral("edge"))) return QObject::tr("Hrana");
    if (key.contains(QStringLiteral("face"))) return QObject::tr("Plocha");
    return QObject::tr("Geometrická reference");
}

// Every primitive solid shares one universal placement contract (origin +
// FRONT/TOP orientation references, manual RX/RY/RZ correction on top).
bool supports_placement_reference_table(zima::document::FeatureKind kind) {
    using zima::document::FeatureKind;
    return kind == FeatureKind::Box || kind == FeatureKind::Cylinder ||
        kind == FeatureKind::Sphere || kind == FeatureKind::Cone ||
        kind == FeatureKind::Pyramid || kind == FeatureKind::Wedge ||
        kind == FeatureKind::Extrusion || kind == FeatureKind::Revolution ||
        kind == FeatureKind::ImportedStep || kind == FeatureKind::Hole ||
        kind == FeatureKind::Thread;
}

QString primitive_properties_title(zima::document::FeatureKind kind) {
    using zima::document::FeatureKind;
    switch (kind) {
        case FeatureKind::Sketch: return QObject::tr("Vlastnosti skici");
        case FeatureKind::Box: return QObject::tr("Vlastnosti kvádru");
        case FeatureKind::Cylinder: return QObject::tr("Vlastnosti válce");
        case FeatureKind::Sphere: return QObject::tr("Vlastnosti koule");
        case FeatureKind::Cone: return QObject::tr("Vlastnosti kužele");
        case FeatureKind::Pyramid: return QObject::tr("Vlastnosti jehlanu");
        case FeatureKind::Wedge: return QObject::tr("Vlastnosti klínu");
        case FeatureKind::Extrusion: return QObject::tr("Vlastnosti vytažení");
        case FeatureKind::Revolution: return QObject::tr("Vlastnosti rotace");
        case FeatureKind::Sweep3D: return QObject::tr("Vlastnosti 3D Sweepu");
        case FeatureKind::ImportedStep:
            return QObject::tr("Vlastnosti importu STEP");
        case FeatureKind::Fillet: return QObject::tr("Vlastnosti zaoblení");
        case FeatureKind::Chamfer: return QObject::tr("Vlastnosti sražení");
        case FeatureKind::Shell: return QObject::tr("Vlastnosti Shellu");
        case FeatureKind::Hole: return QObject::tr("Vlastnosti otvoru");
        case FeatureKind::Thread: return QObject::tr("Vlastnosti závitu");
        case FeatureKind::DrillPoint:
            return QObject::tr("Vlastnosti vrtací špičky");
    }
    return QObject::tr("Vlastnosti prvku");
}

}  // namespace

PrimitivePropertiesDialog::PrimitivePropertiesDialog(
    const zima::document::HistoryContainer& initial,
    bool edit_mode,
    bool allow_subtract,
    LegacyCommitCallback commit,
    QWidget* parent)
    : PrimitivePropertiesDialog(
          initial, edit_mode, allow_subtract,
          [commit = std::move(commit)](
              zima::document::HistoryContainer value,
              std::vector<std::string>) mutable {
              commit(std::move(value));
          }, parent) {}

PrimitivePropertiesDialog::PrimitivePropertiesDialog(
    const zima::document::HistoryContainer& initial,
    bool edit_mode,
    bool allow_subtract,
    CommitCallback commit,
    QWidget* parent,
    std::vector<AssemblyTarget> assembly_targets,
    std::vector<std::string> selected_targets,
    bool assembly_cut_mode)
    : PropertiesSubWindow(
          primitive_properties_title(initial.feature_kind), parent),
      initial_(initial), edit_mode_(edit_mode),
      accepted_target_baseline_(selected_targets), commit_(std::move(commit)) {
    setAttribute(Qt::WA_DeleteOnClose, true);
    setMinimumWidth(340);
    auto* header_form = new QFormLayout;
    name_ = new QLineEdit(QString::fromStdString(initial.name), this);
    const bool treatment = initial.feature_kind == zima::document::FeatureKind::Fillet ||
        initial.feature_kind == zima::document::FeatureKind::Chamfer ||
        initial.feature_kind == zima::document::FeatureKind::Shell;
    if (!treatment) header_form->addRow(tr("Název"), name_);
    else name_->hide();
    // Thread has no body Boolean and Drill Point is always subtractive; neither
    // exposes a user-selectable operation even though both share this dialog.
    if (!treatment && initial.feature_kind !=
            zima::document::FeatureKind::Thread &&
        initial.feature_kind != zima::document::FeatureKind::DrillPoint) {
        operation_ = new QComboBox(this);
        if (!assembly_cut_mode) operation_->addItem(tr("Přičíst"), "add");
        if (allow_subtract || assembly_cut_mode) {
            operation_->addItem(tr("Odečíst"), "subtract");
        }
        if (initial.combine_mode == zima::document::CombineMode::Subtract) {
            operation_->setCurrentIndex(operation_->findData("subtract"));
        }
        operation_->hide();
        if (assembly_cut_mode) operation_->setEnabled(false);
    }
    content_layout()->addLayout(header_form);

    // Universal container placement (Umístění kontejneru / Orientace
    // kontejneru) is shown immediately below the name/operation header, in
    // the same position as ConstructionPropertiesDialog's Point/Axis/Plane
    // dialogs, before any shape-specific dimension fields.
    if (supports_placement_reference_table(initial.feature_kind)) {
        placement_ = std::make_unique<zima::ui::ContainerPlacementSection>(
            this, content_layout(), /*with_orientation=*/true,
            /*position_rows_can_define_rotation=*/true);
        placement_->initialize_from_references(initial.placement.references,
            [](const std::string& semantic) {
                return readable_placement_reference_kind(semantic);
            });
        placement_->initialize_numeric_values(initial.placement);
        const bool has_orientation_reference = std::any_of(
            initial.placement.references.begin(), initial.placement.references.end(),
            [](const auto& reference) {
                return reference.orientation_drives_rotation;
            });
        placement_->set_orientation_base_rotation(
            {initial.placement.rotation_x, initial.placement.rotation_y,
             initial.placement.rotation_z}, has_orientation_reference);
        translation_ = placement_->translation_fields();
        rotation_ = placement_->rotation_offset_fields();
        for (auto* input : translation_) input->setObjectName("primitiveTranslation");
        for (auto* input : rotation_) input->setObjectName("primitiveRotation");
        reference_status_ = placement_->reference_status_label();
        dof_label_ = placement_->dof_label();
        placement_->reference_table()->setObjectName("primitiveReferenceTable");
        if (placement_->orientation_table() != nullptr) {
            placement_->orientation_table()->setObjectName("primitiveOrientationTable");
        }
        placement_->set_reference_request_callback(
            [this](std::size_t index) {
                if (reference_request_) reference_request_(index);
            });
        placement_->set_changed_callback([this] { notify_preview(); });
        placement_->set_highlights_changed_callback(
            [this] { if (reference_highlights_changed_) reference_highlights_changed_(); });
        placement_->refresh_reference_table();
        placement_->refresh_orientation_table();
        placement_->install_dof_label(content_layout());
    }

    auto* form = new QFormLayout;

    const auto dimension = [this](double value, const char* object_name) {
        auto* field = new QDoubleSpinBox(this);
        field->setRange(0.001, 1'000'000.0);
        field->setDecimals(3);
        field->setSingleStep(1.0);
        field->setSuffix(" mm");
        field->setObjectName(object_name);
        field->setValue(value);
        return field;
    };
    if (initial.feature_kind == zima::document::FeatureKind::Box) {
        length_ = dimension(initial.box.length, "boxLength");
        width_ = dimension(initial.box.width, "boxWidth");
        height_ = dimension(initial.box.height, "boxHeight");
        form->addRow(tr("Délka"), length_);
        form->addRow(tr("Šířka"), width_);
        form->addRow(tr("Výška"), height_);
    } else if (initial.feature_kind == zima::document::FeatureKind::Cylinder) {
        radius_ = dimension(initial.cylinder.radius, "cylinderRadius");
        height_ = dimension(initial.cylinder.height, "cylinderHeight");
        form->addRow(tr("Poloměr"), radius_);
        form->addRow(tr("Výška"), height_);
    } else if (initial.feature_kind == zima::document::FeatureKind::Thread) {
        thread_standard_ = new QComboBox(this);
        thread_standard_->setObjectName("threadStandard");
        thread_standard_->addItem(tr("Metrický ISO"), "metric");
        thread_standard_->addItem(tr("Whitworth BSW"), "whitworth");
        thread_standard_->addItem(tr("Trubkový G (BSPP)"), "pipe");
        thread_standard_->setCurrentIndex(thread_standard_->findData(
            initial.thread.standard == zima::document::ThreadStandard::Metric
                ? "metric" : initial.thread.standard ==
                    zima::document::ThreadStandard::Whitworth
                ? "whitworth" : "pipe"));
        thread_size_ = new QComboBox(this);
        thread_size_->setObjectName("threadSize");
        thread_side_ = new QComboBox(this);
        thread_side_->setObjectName("threadSide");
        thread_side_->addItem(tr("Automaticky podle tělesa"), "automatic");
        thread_side_->addItem(tr("Vnitřní – do otvoru"), "internal");
        thread_side_->addItem(tr("Vnější – na válci"), "external");
        thread_side_->setCurrentIndex(thread_side_->findData(
            initial.thread.side == zima::document::ThreadSide::Internal
                ? "internal" : initial.thread.side ==
                    zima::document::ThreadSide::External
                ? "external" : "automatic"));
        hole_thread_nominal_diameter_ = dimension(
            initial.thread.nominal_diameter, "threadNominalDiameter");
        thread_custom_profile_diameter_ = new QCheckBox(
            tr("Vlastní průměr kresleného válce/kružnice"), this);
        thread_custom_profile_diameter_->setObjectName("threadCustomProfileDiameter");
        thread_custom_profile_diameter_->setChecked(
            initial.thread.custom_profile_diameter);
        thread_profile_diameter_ = dimension(
            initial.thread.profile_diameter, "threadProfileDiameter");
        thread_profile_diameter_->setEnabled(initial.thread.custom_profile_diameter);
        thread_dimension_label_ = new QLineEdit(
            QString::fromStdString(initial.thread.dimension_label), this);
        thread_dimension_label_->setObjectName("threadDimensionLabel");
        thread_dimension_label_->setPlaceholderText(
            tr("Automaticky podle zvoleného závitu"));
        thread_direction_ = new QComboBox(this);
        thread_direction_->addItem(tr("Dopředu"), "forward");
        thread_direction_->addItem(tr("Obrátit"), "reverse");
        thread_direction_->setCurrentIndex(initial.thread.direction ==
                zima::document::ExtrusionDirection::Reverse ? 1 : 0);
        extent_mode_ = new QComboBox(this);
        extent_mode_->setObjectName("threadExtentMode");
        extent_mode_->addItem(tr("Jedna strana"), "one_side");
        extent_mode_->addItem(tr("Obě strany"), "two_sides");
        extent_mode_->addItem(tr("Symetricky"), "symmetric");
        extent_mode_->setCurrentIndex(extent_mode_->findData(
            initial.thread.extent_mode ==
                    zima::document::ProfileExtentMode::TwoSides
                ? "two_sides" : initial.thread.extent_mode ==
                    zima::document::ProfileExtentMode::Symmetric
                ? "symmetric" : "one_side"));
        hole_thread_end_ = new QComboBox(this);
        hole_thread_end_->setObjectName("threadEnd");
        hole_thread_end_->addItem(tr("Délka"), "length");
        hole_thread_end_->addItem(tr("Až k"), "up_to");
        hole_thread_end_->addItem(tr("Skrz vše"), "through_all");
        hole_thread_end_->setCurrentIndex(hole_thread_end_->findData(
            initial.thread.end_condition_forward == zima::document::EndCondition::UpTo
                ? "up_to" : initial.thread.end_condition_forward ==
                    zima::document::EndCondition::ThroughAll
                ? "through_all" : "length"));
        hole_thread_length_ = dimension(
            initial.thread.length_forward, "threadLengthForward");
        forward_end_condition_ = hole_thread_end_;
        forward_length_ = hole_thread_length_;
        hole_left_hand_ = new QCheckBox(tr("Levý závit"), this);
        hole_left_hand_->setChecked(initial.thread.left_hand);
        thread_runout_factor_ = dimension(
            std::max(0.001, initial.thread.runout_pitch_factor),
            "threadRunoutPitchFactor");
        thread_runout_factor_->setSuffix(QStringLiteral(" × P"));
        thread_runout_factor_->setRange(0.0, 100.0);
        thread_runout_factor_->setValue(initial.thread.runout_pitch_factor);
        form->addRow(tr("Typ závitu"), thread_standard_);
        form->addRow(tr("Rozměr"), thread_size_);
        form->addRow(tr("Použití"), thread_side_);
        form->addRow(tr("Skutečný průměr"), hole_thread_nominal_diameter_);
        form->addRow(thread_custom_profile_diameter_);
        form->addRow(tr("Průměr válce závitu"), thread_profile_diameter_);
        form->addRow(tr("Text kóty"), thread_dimension_label_);
        form->addRow(tr("Směr"), thread_direction_);
        form->addRow(tr("Rozsah"), extent_mode_);
        form->addRow(tr("Přední zakončení"), hole_thread_end_);
        form->addRow(tr("Přední délka"), hole_thread_length_);
        forward_end_target_ = new QLineEdit(this);
        forward_end_target_->setReadOnly(true);
        forward_end_target_->setPlaceholderText(tr("Vyberte rovinu nebo plochu…"));
        if (!initial.thread.end_targets_forward.empty()) forward_end_target_->setText(
            QString::fromStdString(initial.thread.end_targets_forward.front().label));
        forward_end_target_->installEventFilter(this);
        forward_end_target_clear_action_ = new QAction(
            tr("Vymazat referenci"), forward_end_target_);
        forward_end_target_clear_action_->setEnabled(
            !initial.thread.end_targets_forward.empty());
        forward_end_target_->addAction(forward_end_target_clear_action_);
        connect(forward_end_target_clear_action_, &QAction::triggered, this,
            [this] { clear_extrusion_target("forward"); });
        forward_end_targets_button_ = zima::ui::build_reference_inspection_button(
            !initial.thread.end_targets_forward.empty(), false,
            [this](bool) { toggle_extrusion_target_highlight("forward"); });
        forward_end_targets_button_->setParent(this);
        auto* target_row = new QWidget(this);
        auto* target_layout = new QHBoxLayout(target_row);
        target_layout->setContentsMargins(0, 0, 0, 0);
        target_layout->addWidget(forward_end_target_, 1);
        target_layout->addWidget(forward_end_targets_button_);
        form->addRow(tr("Přední cíl"), target_row);
        reverse_end_condition_ = new QComboBox(this);
        reverse_end_condition_->setObjectName("threadEndReverse");
        reverse_end_condition_->addItem(tr("Délka"), "length");
        reverse_end_condition_->addItem(tr("Až k"), "up_to");
        reverse_end_condition_->addItem(tr("Skrz vše"), "through_all");
        reverse_end_condition_->setCurrentIndex(
            reverse_end_condition_->findData(
                initial.thread.end_condition_reverse ==
                        zima::document::EndCondition::UpTo
                    ? "up_to" : initial.thread.end_condition_reverse ==
                        zima::document::EndCondition::ThroughAll
                    ? "through_all" : "length"));
        reverse_length_ = dimension(
            initial.thread.length_reverse, "threadLengthReverse");
        reverse_end_target_ = new QLineEdit(this);
        reverse_end_target_->setReadOnly(true);
        reverse_end_target_->setPlaceholderText(
            tr("Vyberte rovinu nebo plochu…"));
        if (!initial.thread.end_targets_reverse.empty()) {
            reverse_end_target_->setText(QString::fromStdString(
                initial.thread.end_targets_reverse.front().label));
        }
        reverse_end_target_->installEventFilter(this);
        reverse_end_target_clear_action_ = new QAction(
            tr("Vymazat referenci"), reverse_end_target_);
        reverse_end_target_clear_action_->setEnabled(
            !initial.thread.end_targets_reverse.empty());
        reverse_end_target_->addAction(reverse_end_target_clear_action_);
        connect(reverse_end_target_clear_action_, &QAction::triggered, this,
            [this] { clear_extrusion_target("reverse"); });
        reverse_end_targets_button_ = zima::ui::build_reference_inspection_button(
            !initial.thread.end_targets_reverse.empty(), false,
            [this](bool) { toggle_extrusion_target_highlight("reverse"); });
        reverse_end_targets_button_->setParent(this);
        auto* reverse_target_row = new QWidget(this);
        auto* reverse_target_layout = new QHBoxLayout(reverse_target_row);
        reverse_target_layout->setContentsMargins(0, 0, 0, 0);
        reverse_target_layout->addWidget(reverse_end_target_, 1);
        reverse_target_layout->addWidget(reverse_end_targets_button_);
        form->addRow(tr("Zpětné zakončení"), reverse_end_condition_);
        form->addRow(tr("Zpětná délka"), reverse_length_);
        form->addRow(tr("Zpětný cíl"), reverse_target_row);
        form->addRow(tr("Výjezd"), thread_runout_factor_);
        form->addRow(hole_left_hand_);
        const auto refresh_thread = [this, target_row, reverse_target_row] {
            hole_thread_length_->setVisible(hole_thread_end_->currentData() == "length");
            target_row->setVisible(hole_thread_end_->currentData() == "up_to");
            const auto extent = extent_mode_->currentData().toString();
            const bool reverse = extent != "one_side";
            reverse_end_condition_->setVisible(reverse);
            reverse_length_->setVisible(reverse &&
                reverse_end_condition_->currentData() == "length");
            reverse_target_row->setVisible(reverse &&
                reverse_end_condition_->currentData() == "up_to");
            if (extent == "symmetric") {
                reverse_length_->setValue(hole_thread_length_->value());
                reverse_end_condition_->setCurrentIndex(
                    hole_thread_end_->currentIndex());
            }
            notify_preview();
        };
        connect(hole_thread_end_, &QComboBox::currentIndexChanged, this,
            [this, refresh_thread](int) {
                refresh_thread();
                if (hole_thread_end_->currentData() == "up_to")
                    request_extrusion_target("forward");
            });
        connect(reverse_end_condition_, &QComboBox::currentIndexChanged, this,
            [this, refresh_thread](int) {
                refresh_thread();
                if (reverse_end_condition_->currentData() == "up_to")
                    request_extrusion_target("reverse");
            });
        connect(extent_mode_, &QComboBox::currentIndexChanged, this,
            [refresh_thread](int) { refresh_thread(); });
        connect(reverse_length_, qOverload<double>(&QDoubleSpinBox::valueChanged),
            this, [this] { notify_preview(); });
        connect(thread_runout_factor_,
            qOverload<double>(&QDoubleSpinBox::valueChanged), this,
            [this] { notify_preview(); });
        const auto populate_sizes = [this, designation=initial.thread.designation] {
            thread_size_->clear();
            const auto standard=thread_standard_->currentData().toString();
            for (const auto& size : load_thread_catalog(standard)) {
                thread_size_->addItem(size.designation,size.designation);
                const int row=thread_size_->count()-1;
                thread_size_->setItemData(row,size.nominal_diameter,Qt::UserRole+1);
                thread_size_->setItemData(row,size.pitch,Qt::UserRole+2);
                thread_size_->setItemData(row,size.internal_root_diameter,
                    Qt::UserRole+3);
                thread_size_->setItemData(row,size.external_root_diameter,
                    Qt::UserRole+4);
                if (size.preferred) {
                    auto font=thread_size_->itemData(row,Qt::FontRole).value<QFont>();
                    font.setBold(true);
                    thread_size_->setItemData(row,font,Qt::FontRole);
                }
            }
            int selected=thread_size_->findData(QString::fromStdString(designation));
            if (selected<0) {
                double best=std::numeric_limits<double>::infinity();
                for (int row=0;row<thread_size_->count();++row) {
                    const double difference=std::abs(thread_size_->itemData(
                        row,Qt::UserRole+1).toDouble()-hole_thread_nominal_diameter_->value());
                    if (difference<best) { best=difference; selected=row; }
                }
            }
            thread_size_->setCurrentIndex(std::max(0,selected));
        };
        const auto calculated_profile_diameter = [this] {
            const bool internal=thread_side_->currentData()=="internal";
            if (thread_size_->currentIndex()>=0) {
                bool valid{};
                const double tabulated=thread_size_->currentData(
                    internal ? Qt::UserRole+3 : Qt::UserRole+4).toDouble(&valid);
                if (valid && tabulated>0.0) return tabulated;
            }
            const double factor=thread_standard_->currentData()=="metric"
                ? internal ? 1.082532 : 1.226869
                : 1.280654;
            const double pitch=thread_size_->currentIndex()>=0
                ? thread_size_->currentData(Qt::UserRole+2).toDouble()
                : 0.0;
            return std::max(0.001,hole_thread_nominal_diameter_->value()-
                factor*pitch);
        };
        const auto apply_size = [this,
                                 calculated_profile_diameter](int) {
            if (thread_size_->currentIndex()<0) return;
            hole_thread_nominal_diameter_->setValue(
                thread_size_->currentData(Qt::UserRole+1).toDouble());
            if (!thread_custom_profile_diameter_->isChecked())
                thread_profile_diameter_->setValue(calculated_profile_diameter());
            notify_preview();
        };
        populate_sizes();
        connect(thread_standard_, &QComboBox::currentIndexChanged, this,
            [populate_sizes,apply_size](int) { populate_sizes(); apply_size(0); });
        connect(thread_size_, &QComboBox::currentIndexChanged, this, apply_size);
        connect(thread_side_, &QComboBox::currentIndexChanged, this,
            [this,calculated_profile_diameter](int) {
                if (!thread_custom_profile_diameter_->isChecked())
                    thread_profile_diameter_->setValue(calculated_profile_diameter());
                notify_preview();
            });
        connect(thread_custom_profile_diameter_, &QCheckBox::toggled, this,
            [this,calculated_profile_diameter](bool custom_value) {
                thread_profile_diameter_->setEnabled(custom_value);
                if (!custom_value)
                    thread_profile_diameter_->setValue(calculated_profile_diameter());
                notify_preview();
            });
        connect(thread_profile_diameter_, &QDoubleSpinBox::valueChanged, this,
            [this](double) { notify_preview(); });
        connect(thread_dimension_label_, &QLineEdit::textChanged, this,
            [this] { notify_preview(); });
        for (auto* field : {hole_thread_nominal_diameter_, hole_thread_length_})
            connect(field, &QDoubleSpinBox::valueChanged, this,
                [this] {
                    if (extent_mode_->currentData() == "symmetric")
                        reverse_length_->setValue(hole_thread_length_->value());
                    notify_preview();
                });
        refresh_thread();
    } else if (initial.feature_kind == zima::document::FeatureKind::DrillPoint) {
        drill_point_angle_ = dimension(
            initial.drill_point.included_angle_degrees,
            "drillPointIncludedAngle");
        drill_point_angle_->setRange(1.0, 179.0);
        drill_point_angle_->setSuffix(QStringLiteral(" °"));
        form->addRow(tr("Vrcholový úhel"), drill_point_angle_);
        auto* faces_label = new QLabel(tr("Dna kruhových otvorů"), this);
        auto label_font = faces_label->font();
        label_font.setBold(true);
        faces_label->setFont(label_font);
        form->addRow(faces_label);
        drill_point_face_list_ = new QListWidget(this);
        drill_point_face_list_->setObjectName("drillPointFaces");
        drill_point_face_list_->setMinimumHeight(120);
        drill_point_face_list_->viewport()->installEventFilter(this);
        form->addRow(drill_point_face_list_);
        connect(drill_point_face_list_, &QListWidget::itemPressed, this,
            [this] {
                if (request_drill_point_face_selection_)
                    request_drill_point_face_selection_();
            });
        remove_drill_point_face_button_ = new QPushButton(
            tr("Odebrat vybranou plochu"), this);
        remove_drill_point_face_button_->setObjectName("drillPointRemoveFace");
        form->addRow(remove_drill_point_face_button_);
        connect(remove_drill_point_face_button_, &QPushButton::clicked,
            this, [this] {
                const int row = drill_point_face_list_->currentRow();
                if (row >= 0 && remove_drill_point_face_)
                    remove_drill_point_face_(static_cast<std::size_t>(row));
            });
        connect(drill_point_angle_,
            qOverload<double>(&QDoubleSpinBox::valueChanged), this,
            [this] { notify_preview(); });
        set_drill_point_faces(initial.drill_point.bottom_faces);
    } else if (initial.feature_kind == zima::document::FeatureKind::Hole) {
        hole_type_ = new QComboBox(this);
        hole_type_->setObjectName("holeType");
        hole_type_->addItem(tr("Hladký otvor"), "plain");
        hole_type_->addItem(tr("Metrický závit"), "metric");
        hole_type_->addItem(tr("Trubkový závit"), "pipe");
        hole_type_->addItem(tr("Whitworthův závit"), "whitworth");
        const char* type = initial.hole.type == zima::document::HoleType::MetricThread
            ? "metric" : initial.hole.type == zima::document::HoleType::PipeThread
            ? "pipe" : initial.hole.type == zima::document::HoleType::WhitworthThread
            ? "whitworth" : "plain";
        hole_type_->setCurrentIndex(hole_type_->findData(type));
        form->addRow(tr("Typ otvoru"), hole_type_);
        form->addRow(new QLabel(tr("Vstup / sražení"), this));
        hole_entrance_chamfer_ = dimension(
            std::max(0.001, initial.hole.entrance_chamfer), "holeEntranceChamfer");
        hole_entrance_chamfer_->setRange(0.0, 1'000'000.0);
        hole_entrance_chamfer_->setValue(initial.hole.entrance_chamfer);
        form->addRow(tr("Sražení"), hole_entrance_chamfer_);
        form->addRow(new QLabel(tr("Válcová část"), this));
        hole_diameter_ = dimension(initial.hole.diameter, "holeDiameter");
        form->addRow(tr("Průměr"), hole_diameter_);
        hole_bore_end_ = new QComboBox(this);
        hole_bore_end_->setObjectName("holeBoreEnd");
        hole_bore_end_->addItem(tr("Délka"), "length");
        hole_bore_end_->addItem(tr("Až k"), "up_to");
        hole_bore_end_->addItem(tr("Skrz vše"), "through_all");
        hole_bore_end_->setCurrentIndex(hole_bore_end_->findData(
            initial.hole.bore_end_condition == zima::document::EndCondition::UpTo
                ? "up_to" : initial.hole.bore_end_condition ==
                    zima::document::EndCondition::ThroughAll ? "through_all" : "length"));
        hole_bore_length_ = dimension(initial.hole.bore_length, "holeBoreLength");
        form->addRow(tr("Ukončení otvoru"), hole_bore_end_);
        form->addRow(tr("Délka otvoru"), hole_bore_length_);
        forward_end_target_ = new QLineEdit(this);
        forward_end_target_->setObjectName("holeBoreEndTarget");
        forward_end_target_->setReadOnly(true);
        forward_end_target_->setPlaceholderText(
            tr("Vyberte rovinu nebo plochu…"));
        if (!initial.hole.bore_end_targets.empty()) {
            forward_end_target_->setText(QString::fromStdString(
                initial.hole.bore_end_targets.front().label));
        }
        forward_end_target_->installEventFilter(this);
        forward_end_target_clear_action_ = new QAction(
            tr("Vymazat referenci"), forward_end_target_);
        forward_end_target_clear_action_->setEnabled(
            !initial.hole.bore_end_targets.empty());
        forward_end_target_->addAction(forward_end_target_clear_action_);
        connect(forward_end_target_clear_action_, &QAction::triggered, this,
            [this] { clear_extrusion_target("forward"); });
        forward_end_targets_button_ =
            zima::ui::build_reference_inspection_button(
                !initial.hole.bore_end_targets.empty(), false,
                [this](bool) { toggle_extrusion_target_highlight("forward"); });
        forward_end_targets_button_->setParent(this);
        auto* hole_target_row = new QWidget(this);
        auto* hole_target_layout = new QHBoxLayout(hole_target_row);
        hole_target_layout->setContentsMargins(0, 0, 0, 0);
        hole_target_layout->addWidget(forward_end_target_, 1);
        hole_target_layout->addWidget(forward_end_targets_button_);
        form->addRow(tr("Cíl otvoru"), hole_target_row);
        hole_drill_point_ = new QCheckBox(tr("Vrtaná špička"), this);
        hole_drill_point_->setChecked(initial.hole.drill_point_enabled);
        hole_drill_angle_ = dimension(initial.hole.drill_point_angle_degrees,
            "holeDrillPointAngle");
        hole_drill_angle_->setSuffix(" °");
        form->addRow(hole_drill_point_);
        form->addRow(tr("Úhel špičky"), hole_drill_angle_);
        hole_exit_chamfer_enabled_ = new QCheckBox(
            tr("Srazit konec otvoru"), this);
        hole_exit_chamfer_enabled_->setObjectName("holeExitChamferEnabled");
        hole_exit_chamfer_enabled_->setChecked(
            initial.hole.exit_chamfer_enabled);
        hole_exit_chamfer_ = dimension(
            std::max(0.001, initial.hole.exit_chamfer), "holeExitChamfer");
        hole_exit_chamfer_->setRange(0.0, 1'000'000.0);
        hole_exit_chamfer_->setValue(initial.hole.exit_chamfer);
        form->addRow(hole_exit_chamfer_enabled_);
        form->addRow(tr("Sražení konce"), hole_exit_chamfer_);
        form->addRow(new QLabel(tr("Závitový drát"), this));
        hole_thread_enabled_ = new QCheckBox(tr("Závit"), this);
        hole_thread_enabled_->setChecked(initial.hole.thread_enabled);
        hole_thread_nominal_diameter_ = dimension(
            initial.hole.thread_nominal_diameter, "holeThreadDiameter");
        hole_thread_pitch_ = dimension(initial.hole.thread_pitch, "holeThreadPitch");
        hole_thread_end_ = new QComboBox(this);
        hole_thread_end_->setObjectName("holeThreadEnd");
        hole_thread_end_->addItem(tr("Délka"), "length");
        hole_thread_end_->addItem(tr("Až k"), "up_to");
        hole_thread_end_->addItem(tr("Skrz válcovou část"), "through_all");
        hole_thread_end_->setCurrentIndex(hole_thread_end_->findData(
            initial.hole.thread_end_condition == zima::document::EndCondition::UpTo
                ? "up_to" : initial.hole.thread_end_condition ==
                    zima::document::EndCondition::ThroughAll ? "through_all" : "length"));
        hole_thread_length_ = dimension(initial.hole.thread_length, "holeThreadLength");
        hole_left_hand_ = new QCheckBox(tr("Levý závit"), this);
        hole_left_hand_->setChecked(initial.hole.left_hand_thread);
        form->addRow(hole_thread_enabled_);
        form->addRow(tr("Jmenovitý průměr"), hole_thread_nominal_diameter_);
        form->addRow(tr("Stoupání"), hole_thread_pitch_);
        form->addRow(tr("Ukončení závitu"), hole_thread_end_);
        form->addRow(tr("Délka závitu"), hole_thread_length_);
        form->addRow(hole_left_hand_);
        const auto refresh_hole = [this] {
            const bool threaded = hole_thread_enabled_->isChecked() ||
                hole_type_->currentData() != "plain";
            hole_thread_enabled_->setChecked(threaded);
            for (auto* field : {hole_thread_nominal_diameter_, hole_thread_pitch_,
                                hole_thread_length_}) field->setEnabled(threaded);
            hole_thread_end_->setEnabled(threaded);
            hole_left_hand_->setEnabled(threaded);
            hole_drill_angle_->setEnabled(hole_drill_point_->isChecked());
            hole_exit_chamfer_enabled_->setEnabled(
                !hole_drill_point_->isChecked());
            hole_exit_chamfer_->setEnabled(
                !hole_drill_point_->isChecked() &&
                hole_exit_chamfer_enabled_->isChecked());
            const bool length = hole_bore_end_->currentData() == "length";
            const bool up_to = hole_bore_end_->currentData() == "up_to";
            hole_bore_length_->setVisible(length);
            forward_end_target_->setVisible(up_to);
            forward_end_targets_button_->setVisible(up_to);
            notify_preview();
        };
        connect(hole_type_, &QComboBox::currentIndexChanged, this,
            [refresh_hole](int) { refresh_hole(); });
        connect(hole_thread_enabled_, &QCheckBox::toggled, this,
            [refresh_hole](bool) { refresh_hole(); });
        connect(hole_drill_point_, &QCheckBox::toggled, this,
            [refresh_hole](bool) { refresh_hole(); });
        connect(hole_exit_chamfer_enabled_, &QCheckBox::toggled, this,
            [refresh_hole](bool) { refresh_hole(); });
        connect(hole_bore_end_, &QComboBox::currentIndexChanged, this,
            [this, refresh_hole](int) {
                refresh_hole();
                if (hole_bore_end_->currentData() == "up_to") {
                    request_extrusion_target("forward");
                } else if (forward_end_target_pick_active_) {
                    finish_extrusion_target_entry();
                    if (extrusion_target_cancel_) extrusion_target_cancel_();
                }
            });
        refresh_hole();
    } else if (initial.feature_kind == zima::document::FeatureKind::Sphere) {
        radius_ = dimension(initial.sphere.radius, "sphereRadius");
        form->addRow(tr("Poloměr"), radius_);
    } else if (initial.feature_kind == zima::document::FeatureKind::Cone) {
        radius_ = dimension(initial.cone.bottom_radius, "coneBottomRadius");
        top_radius_ = dimension(std::max(initial.cone.top_radius, 0.001), "coneTopRadius");
        top_radius_->setRange(0.0, 1'000'000.0);
        top_radius_->setValue(initial.cone.top_radius);
        height_ = dimension(initial.cone.height, "coneHeight");
        form->addRow(tr("Dolní poloměr"), radius_);
        form->addRow(tr("Horní poloměr"), top_radius_);
        form->addRow(tr("Výška"), height_);
    } else if (initial.feature_kind == zima::document::FeatureKind::Pyramid) {
        length_ = dimension(initial.pyramid.length, "pyramidLength");
        width_ = dimension(initial.pyramid.width, "pyramidWidth");
        height_ = dimension(initial.pyramid.height, "pyramidHeight");
        form->addRow(tr("Délka základny"), length_);
        form->addRow(tr("Šířka základny"), width_);
        form->addRow(tr("Výška"), height_);
    } else if (initial.feature_kind == zima::document::FeatureKind::Wedge) {
        length_ = dimension(initial.wedge.length, "wedgeLength");
        width_ = dimension(initial.wedge.width, "wedgeWidth");
        height_ = dimension(initial.wedge.height, "wedgeHeight");
        top_offset_ = dimension(std::max(initial.wedge.top_offset, 0.001), "wedgeTopOffset");
        top_offset_->setRange(0.0, initial.wedge.length);
        top_offset_->setValue(initial.wedge.top_offset);
        connect(length_, qOverload<double>(&QDoubleSpinBox::valueChanged),
            top_offset_, &QDoubleSpinBox::setMaximum);
        form->addRow(tr("Délka"), length_);
        form->addRow(tr("Šířka"), width_);
        form->addRow(tr("Výška"), height_);
        form->addRow(tr("Odsazení horní hrany"), top_offset_);
    } else if (initial.feature_kind == zima::document::FeatureKind::Extrusion ||
               initial.feature_kind == zima::document::FeatureKind::Revolution) {
        const bool revolve = initial.feature_kind ==
            zima::document::FeatureKind::Revolution;
        const auto profile_source = revolve ? initial.revolution.profile_source
                                             : initial.extrusion.profile_source;
        const auto& sketch_id = revolve ? initial.revolution.sketch_id
                                        : initial.extrusion.sketch_id;
        const auto result_type = revolve ? initial.revolution.result_type
                                         : initial.extrusion.result_type;
        const double thin_thickness = revolve ? initial.revolution.thin_thickness
                                              : initial.extrusion.thin_thickness;
        const auto thin_mode = revolve ? initial.revolution.thin_mode
                                       : initial.extrusion.thin_mode;
        const auto extent_mode = revolve ? initial.revolution.extent_mode
                                         : initial.extrusion.extent_mode;
        const auto direction_mode = revolve ? initial.revolution.direction
                                            : initial.extrusion.direction;
        const auto cycle_combo = [](QComboBox* combo) {
            combo->setCurrentIndex((combo->currentIndex() + 1) % combo->count());
        };
        auto* source_row = new QWidget(this);
        auto* source_layout = new QHBoxLayout(source_row);
        source_layout->setContentsMargins(0, 0, 0, 0);
        profile_source_ = new QLineEdit(this);
        profile_source_->setReadOnly(true);
        profile_source_->setText(profile_source ==
                zima::document::ProfileSource::Internal
            ? tr("Vlastní kontejner")
            : QString::fromStdString(sketch_id));
        profile_pick_button_ = new QPushButton(tr("Vybrat"), this);
        profile_pick_button_->setCheckable(true);
        profile_pick_button_->setToolTip(tr("Vybrat zdrojovou skicu"));
        profile_reset_button_ = new QPushButton(QStringLiteral("↶"), this);
        profile_reset_button_->setToolTip(tr("Použít vlastní skicu"));
        source_layout->addWidget(profile_source_, 1);
        source_layout->addWidget(profile_pick_button_);
        source_layout->addWidget(profile_reset_button_);
        form->addRow(tr("Zdroj profilu"), source_row);
        // Extrusion/Revolution always own their Sketch.  A model command must
        // never silently attach the new history container to a Sketch owned
        // by another container.
        source_row->setVisible(false);
        connect(profile_pick_button_, &QPushButton::toggled, this,
            [this](bool checked) {
                if (profile_pick_request_) profile_pick_request_(checked);
            });
        connect(profile_reset_button_, &QPushButton::clicked, this, [this] {
            profile_source_->setText(tr("Vlastní kontejner"));
            profile_pick_button_->setChecked(false);
            notify_preview();
        });
        profile_status_ = new QLineEdit(this);
        profile_status_->setReadOnly(true);
        profile_status_->setText(tr("Uzavřený"));
        form->addRow(tr("Stav profilu"), profile_status_);
        profile_plane_offset_ = dimension(
            revolve ? initial.revolution.profile_plane_offset
                    : initial.extrusion.profile_plane_offset,
            "profilePlaneOffset");
        profile_plane_offset_->setRange(-1'000'000.0, 1'000'000.0);
        profile_plane_offset_->setValue(
            revolve ? initial.revolution.profile_plane_offset
                    : initial.extrusion.profile_plane_offset);
        form->addRow(tr("Odsazení roviny"), profile_plane_offset_);
        connect(profile_plane_offset_,
            qOverload<double>(&QDoubleSpinBox::valueChanged), this,
            [this] { notify_preview(); });
        result_type_ = new QComboBox(this);
        result_type_->addItem(tr("Těleso"), "solid");
        result_type_->addItem(tr("Thin"), "thin");
        result_type_->setCurrentIndex(result_type_->findData(
            result_type == zima::document::ProfileResultType::Thin
                ? "thin" : "solid"));
        auto* result_row = new QWidget(this);
        auto* result_layout = new QHBoxLayout(result_row);
        result_layout->setContentsMargins(0, 0, 0, 0);
        result_type_switch_button_ = new QPushButton(tr("Přepnout"), this);
        result_layout->addWidget(result_type_, 1);
        result_layout->addWidget(result_type_switch_button_);
        form->addRow(tr("Typ výsledku"), result_row);
        connect(result_type_switch_button_, &QPushButton::clicked, this,
            [result_type = result_type_, cycle_combo] { cycle_combo(result_type); });
        thin_thickness_ = dimension(
            thin_thickness, "extrusionThinThickness");
        form->addRow(tr("Tloušťka"), thin_thickness_);
        thin_mode_ = new QComboBox(this);
        thin_mode_->addItem(tr("Jedna strana"), "one_side");
        thin_mode_->addItem(tr("Na druhou stranu"), "other_side");
        thin_mode_->addItem(tr("Symetricky"), "symmetric");
        thin_mode_->setCurrentIndex(thin_mode_->findData(
            thin_mode == zima::document::ThinMode::OtherSide
                ? "other_side"
            : thin_mode == zima::document::ThinMode::Symmetric
                ? "symmetric" : "one_side"));
        auto* thin_row = new QWidget(this);
        auto* thin_layout = new QHBoxLayout(thin_row);
        thin_layout->setContentsMargins(0, 0, 0, 0);
        thin_mode_switch_button_ = new QPushButton(tr("Přepnout"), this);
        thin_layout->addWidget(thin_mode_, 1);
        thin_layout->addWidget(thin_mode_switch_button_);
        form->addRow(tr("Strana tloušťky"), thin_row);
        connect(thin_mode_switch_button_, &QPushButton::clicked, this,
            [thin_mode = thin_mode_, cycle_combo] { cycle_combo(thin_mode); });
        extent_mode_ = new QComboBox(this);
        extent_mode_->setObjectName("extrusionExtentMode");
        extent_mode_->addItem(tr("Jedna strana"), "one_side");
        extent_mode_->addItem(tr("Obě strany"), "two_sides");
        extent_mode_->addItem(tr("Symetricky"), "symmetric");
        extent_mode_->setCurrentIndex(extent_mode_->findData(
            extent_mode == zima::document::ProfileExtentMode::TwoSides
                ? "two_sides"
            : extent_mode == zima::document::ProfileExtentMode::Symmetric
                ? "symmetric" : "one_side"));
        auto* extent_row = new QWidget(this);
        auto* extent_layout = new QHBoxLayout(extent_row);
        extent_layout->setContentsMargins(0, 0, 0, 0);
        extent_switch_button_ = new QPushButton(tr("Přepnout"), this);
        extent_layout->addWidget(extent_mode_, 1);
        extent_layout->addWidget(extent_switch_button_);
        form->addRow(tr("Rozsah"), extent_row);
        connect(extent_switch_button_, &QPushButton::clicked, this,
            [extent = extent_mode_, cycle_combo] { cycle_combo(extent); });
        extrusion_direction_ = new QComboBox(this);
        extrusion_direction_->setObjectName("extrusionDirection");
        extrusion_direction_->addItem(revolve ? QStringLiteral("↻") : QStringLiteral("↑"),
                                      "forward");
        extrusion_direction_->addItem(revolve ? QStringLiteral("↺") : QStringLiteral("↓"),
                                      "reverse");
        const char* direction = direction_mode ==
                zima::document::ExtrusionDirection::Reverse ? "reverse" : "forward";
        extrusion_direction_->setCurrentIndex(
            extrusion_direction_->findData(direction));
        auto* direction_row = new QWidget(this);
        auto* direction_layout = new QHBoxLayout(direction_row);
        direction_layout->setContentsMargins(0, 0, 0, 0);
        direction_flip_button_ = new QPushButton(tr("Obrátit"), this);
        direction_layout->addWidget(extrusion_direction_, 1);
        direction_layout->addWidget(direction_flip_button_);
        form->addRow(tr("Směr"), direction_row);
        connect(direction_flip_button_, &QPushButton::clicked, this,
            [this] {
                extrusion_direction_->setCurrentIndex(
                    extrusion_direction_->currentData() == "forward" ? 1 : 0);
                if (extent_mode_->currentData() == "two_sides") {
                    const double forward = forward_length_->value();
                    forward_length_->setValue(reverse_length_->value());
                    reverse_length_->setValue(forward);
                }
            });
        forward_length_ = dimension(revolve ? initial.revolution.angle_degrees
                                            : initial.extrusion.length_forward,
                                    revolve ? "revolutionAngle" : "extrusionHeight");
        reverse_length_ = dimension(revolve ? initial.revolution.angle_reverse
                                            : initial.extrusion.length_reverse,
                                    revolve ? "revolutionReverseAngle"
                                            : "extrusionReverseLength");
        if (revolve) {
            for (auto* spin : {forward_length_, reverse_length_}) {
                spin->setRange(0.001, 360.0);
                spin->setSuffix(QStringLiteral("°"));
            }
            auto* axis_hint = new QLabel(
                tr("Zelená konstrukční osa ve skici"), this);
            axis_hint->setObjectName("revolutionAxisHint");
            axis_hint->setWordWrap(true);
            form->addRow(tr("Osa rotace"), axis_hint);
        }
        const auto end_condition = [this](zima::document::EndCondition selected,
                                          const char* name) {
            auto* combo = new QComboBox(this);
            combo->setObjectName(name);
            combo->addItem(tr("Na délku"), "length");
            combo->addItem(tr("Až k…"), "up_to");
            combo->addItem(tr("Skrz vše"), "through_all");
            combo->setCurrentIndex(combo->findData(
                selected == zima::document::EndCondition::UpTo ? "up_to"
                : selected == zima::document::EndCondition::ThroughAll
                    ? "through_all" : "length"));
            return combo;
        };
        const auto end_row = [this, &end_condition](const char* side, QComboBox*& combo,
                QDoubleSpinBox* length, QLineEdit*& target, QToolButton*& collection,
                zima::document::EndCondition condition,
                const std::vector<zima::document::ExtrusionParameters::EndTarget>& targets) {
            auto* row = new QWidget(this);
            auto* layout = new QHBoxLayout(row);
            layout->setContentsMargins(0, 0, 0, 0);
            combo = end_condition(condition,
                side == std::string_view("forward")
                    ? "extrusionForwardEndCondition" : "extrusionReverseEndCondition");
            target = new QLineEdit(this);
            target->setObjectName(side == std::string_view("forward")
                ? "extrusionForwardEndTarget" : "extrusionReverseEndTarget");
            target->setReadOnly(true);
            target->setPlaceholderText(tr("Vyberte bod, rovinu nebo rovinnou plochu…"));
            if (!targets.empty()) target->setText(QString::fromStdString(targets.front().label));
            target->installEventFilter(this);
            target->setContextMenuPolicy(Qt::ActionsContextMenu);
            auto* clear_action = new QAction(tr("Vymazat referenci"), target);
            clear_action->setObjectName(side == std::string_view("forward")
                ? "extrusionForwardEndTargetClear"
                : "extrusionReverseEndTargetClear");
            clear_action->setEnabled(!targets.empty());
            target->addAction(clear_action);
            connect(clear_action, &QAction::triggered, this,
                [this, side = std::string(side)] {
                    clear_extrusion_target(side);
                });
            if (side == std::string_view("forward")) {
                forward_end_target_clear_action_ = clear_action;
            } else {
                reverse_end_target_clear_action_ = clear_action;
            }
            collection = zima::ui::build_reference_inspection_button(
                !targets.empty(), false,
                [this, side = std::string(side)](bool) {
                    toggle_extrusion_target_highlight(side);
                });
            collection->setParent(this);
            collection->setObjectName(side == std::string_view("forward")
                ? "extrusionForwardTargetInspection"
                : "extrusionReverseTargetInspection");
            layout->addWidget(combo, 1);
            layout->addWidget(length, 1);
            layout->addWidget(target, 1);
            layout->addWidget(collection);
            const auto refresh = [combo, length, target, collection] {
                const auto value = combo->currentData().toString();
                length->setVisible(value == "length");
                target->setVisible(value == "up_to");
                collection->setVisible(value == "up_to");
            };
            connect(combo, &QComboBox::currentIndexChanged, this,
                [this, combo, refresh, side = std::string(side)](int) {
                    refresh();
                    if (combo->currentData().toString() == "up_to") {
                        request_extrusion_target(side);
                    } else {
                        const bool was_active = side == "reverse"
                            ? reverse_end_target_pick_active_
                            : forward_end_target_pick_active_;
                        if (was_active) {
                            finish_extrusion_target_entry();
                            if (extrusion_target_cancel_) extrusion_target_cancel_();
                        }
                    }
                    notify_preview();
            });
            refresh();
            refresh_extrusion_target_styles();
            return row;
        };
        if (!revolve) {
            auto* forward_row = end_row("forward", forward_end_condition_,
                forward_length_, forward_end_target_, forward_end_targets_button_,
                initial.extrusion.end_condition_forward,
                initial.extrusion.end_targets_forward);
            reverse_end_row_ = end_row("reverse", reverse_end_condition_,
                reverse_length_, reverse_end_target_, reverse_end_targets_button_,
                initial.extrusion.end_condition_reverse,
                initial.extrusion.end_targets_reverse);
            form->addRow(tr("Zakončení"), forward_row);
            form->addRow(tr("Zpětné zakončení"), reverse_end_row_);
        } else {
            form->addRow(tr("Úhel"), forward_length_);
            reverse_end_row_ = reverse_length_;
            form->addRow(tr("Zpětný úhel"), reverse_length_);
        }
        own_sketch_button_ = new QPushButton(QStringLiteral("SKETCH"), this);
        own_sketch_button_->setObjectName("primitiveOwnSketchButton");
        own_sketch_button_->setMinimumHeight(40);
        own_sketch_button_->setStyleSheet(
            "QPushButton{background:#4DD811;color:#102027;font-weight:700;"
            "padding:9px 18px;border-radius:4px}"
            "QPushButton:hover{background:#65ec2c}");
        form->addRow(own_sketch_button_);
        connect(own_sketch_button_, &QPushButton::clicked, this, [this] {
            auto pending = values();
            accept();
            if (edit_sketch_) edit_sketch_(std::move(pending));
        });
        connect(result_type_, &QComboBox::currentIndexChanged, this, [this](int) {
            const bool thin = result_type_->currentData() == "thin";
            thin_thickness_->setVisible(thin);
            thin_mode_->setVisible(thin);
            notify_preview();
        });
        connect(thin_thickness_, qOverload<double>(&QDoubleSpinBox::valueChanged),
            this, [this] { notify_preview(); });
        connect(thin_mode_, &QComboBox::currentIndexChanged,
            this, [this] { notify_preview(); });
        const auto refresh_extent = [this] {
            const auto mode = extent_mode_->currentData().toString();
            const bool reverse = mode == "two_sides";
            reverse_end_row_->setEnabled(reverse);
            reverse_end_row_->setVisible(true);
            if (mode == "symmetric") {
                reverse_length_->setValue(forward_length_->value());
            }
            notify_preview();
        };
        if (revolve) {
            revolution_previous_extent_index_ = extent_mode_->currentIndex();
            revolution_extent_values_[revolution_previous_extent_index_] = {
                forward_length_->value(), reverse_length_->value()};
            connect(extent_mode_, &QComboBox::currentIndexChanged, this,
                [this, refresh_extent](int index) {
                    revolution_extent_values_[revolution_previous_extent_index_] = {
                        forward_length_->value(), reverse_length_->value()};
                    const auto values = revolution_extent_values_[index];
                    forward_length_->setValue(values[0]);
                    reverse_length_->setValue(values[1]);
                    revolution_previous_extent_index_ = index;
                    refresh_extent();
                });
        } else {
            connect(extent_mode_, &QComboBox::currentIndexChanged, this,
                [refresh_extent](int) { refresh_extent(); });
        }
        connect(forward_length_, qOverload<double>(&QDoubleSpinBox::valueChanged),
            this, [this](double value) {
                if (extent_mode_->currentData() == "symmetric") {
                    reverse_length_->setValue(value);
                }
                notify_preview();
            });
        connect(reverse_length_, qOverload<double>(&QDoubleSpinBox::valueChanged),
            this, [this] { notify_preview(); });
        connect(extrusion_direction_, &QComboBox::currentIndexChanged,
            this, [this] { notify_preview(); });
        if (operation_ != nullptr) {
            connect(operation_, &QComboBox::currentIndexChanged,
                this, [this] { notify_preview(); });
        }
        const bool initial_thin = result_type_->currentData() == "thin";
        thin_thickness_->setVisible(initial_thin);
        thin_mode_->setVisible(initial_thin);
        refresh_extent();
    } else if (initial.feature_kind == zima::document::FeatureKind::Shell) {
        shell_thickness_ = dimension(initial.shell.thickness, "shellThickness");
        form->addRow(tr("Tloušťka"), shell_thickness_);
        auto* faces_label = new QLabel(tr("Otevřené plochy"), this);
        auto label_font = faces_label->font();
        label_font.setBold(true);
        faces_label->setFont(label_font);
        form->addRow(faces_label);
        shell_face_list_ = new QListWidget(this);
        shell_face_list_->setObjectName("shellFaces");
        shell_face_list_->setMinimumHeight(100);
        shell_face_list_->viewport()->installEventFilter(this);
        form->addRow(shell_face_list_);
        connect(shell_face_list_, &QListWidget::itemPressed, this,
            [this] {
                if (request_shell_face_selection_)
                    request_shell_face_selection_();
            });
        remove_shell_face_button_ = new QPushButton(
            tr("Odebrat vybranou plochu"), this);
        remove_shell_face_button_->setObjectName("shellRemoveFace");
        form->addRow(remove_shell_face_button_);
        connect(remove_shell_face_button_, &QPushButton::clicked, this, [this] {
            const int row = shell_face_list_->currentRow();
            if (row >= 0 && remove_shell_face_) {
                remove_shell_face_(static_cast<std::size_t>(row));
            }
        });
        connect(shell_thickness_, qOverload<double>(&QDoubleSpinBox::valueChanged),
            this, [this] { notify_preview(); });
        set_shell_faces(initial.shell.removed_faces);
    } else if (initial.feature_kind == zima::document::FeatureKind::ImportedStep) {
        auto* source = new QLabel(
            QString::fromStdString(initial.imported_step.source_path), this);
        source->setTextInteractionFlags(Qt::TextSelectableByMouse);
        source->setWordWrap(true);
        form->addRow(tr("Zdrojový soubor"), source);
    } else {
        const bool fillet =
            initial.feature_kind == zima::document::FeatureKind::Fillet;
        treatment_type_ = new QComboBox(this);
        treatment_type_->setObjectName("edgeTreatmentType");
        if (fillet) {
            treatment_type_->addItem(tr("Konstantní"), "constant");
            treatment_type_->addItem(tr("Proměnlivé R1 → R2"), "linear");
            treatment_type_->setCurrentIndex(treatment_type_->findData(
                initial.edge_treatment.fillet_mode ==
                        zima::document::EdgeTreatmentParameters::FilletMode::Linear
                    ? "linear" : "constant"));
        } else {
            treatment_type_->addItem(tr("A × A"), "equal_distance");
            treatment_type_->addItem(tr("A × B"), "two_distances");
            treatment_type_->addItem(tr("A + úhel"), "distance_angle");
            const char* mode = initial.edge_treatment.chamfer_mode ==
                    zima::document::EdgeTreatmentParameters::ChamferMode::TwoDistances
                ? "two_distances"
                : initial.edge_treatment.chamfer_mode ==
                        zima::document::EdgeTreatmentParameters::ChamferMode::DistanceAngle
                    ? "distance_angle" : "equal_distance";
            treatment_type_->setCurrentIndex(treatment_type_->findData(mode));
        }
        form->addRow(tr("Typ"), treatment_type_);
        treatment_primary_ = dimension(
            initial.edge_treatment.primary_size, "edgeTreatmentPrimary");
        treatment_secondary_ = dimension(
            initial.edge_treatment.secondary_size, "edgeTreatmentSecondary");
        treatment_angle_ = new QDoubleSpinBox(this);
        treatment_angle_->setObjectName("edgeTreatmentAngle");
        treatment_angle_->setRange(0.1, 89.9);
        treatment_angle_->setDecimals(2);
        treatment_angle_->setSingleStep(1.0);
        treatment_angle_->setSuffix(QStringLiteral("°"));
        treatment_angle_->setValue(initial.edge_treatment.angle_degrees);
        treatment_primary_label_ = new QLabel(this);
        treatment_secondary_label_ = new QLabel(this);
        treatment_angle_label_ = new QLabel(tr("Úhel"), this);
        form->addRow(treatment_primary_label_, treatment_primary_);
        form->addRow(treatment_secondary_label_, treatment_secondary_);
        form->addRow(treatment_angle_label_, treatment_angle_);
        treatment_flip_ = new QPushButton(tr("FLIP"), this);
        treatment_flip_->setObjectName("edgeTreatmentFlip");
        treatment_flip_->setCheckable(true);
        treatment_flip_->setChecked(initial.edge_treatment.flip);
        treatment_reverse_ = new QPushButton(tr("OBRÁTIT SMĚR"), this);
        treatment_reverse_->setObjectName("edgeTreatmentReverse");
        treatment_reverse_->setCheckable(true);
        treatment_reverse_->setChecked(initial.edge_treatment.reverse);
        for (auto* button : {treatment_flip_, treatment_reverse_}) {
            button->setMinimumHeight(34);
            button->setStyleSheet(
                "QPushButton{border:2px solid #2d5670;border-radius:6px;"
                "font-weight:700;padding:6px 12px}"
                "QPushButton:checked{background:#00d1ff;color:#101510;"
                "border-color:#6fe3ff}");
            form->addRow(button);
        }
        refresh_edge_treatment_fields();
        auto* edges_label = new QLabel(tr("Vybrané hrany"), this);
        auto label_font = edges_label->font(); label_font.setBold(true);
        edges_label->setFont(label_font);
        form->addRow(edges_label);
        edge_list_ = new QTreeWidget(this);
        edge_list_->setObjectName("edgeTreatmentEdges");
        edge_list_->setColumnCount(2);
        edge_list_->setHeaderLabels({tr("Trasa"), tr("Objekty")});
        edge_list_->header()->setSectionResizeMode(0, QHeaderView::ResizeToContents);
        edge_list_->header()->setSectionResizeMode(1, QHeaderView::Stretch);
        edge_list_->setMinimumHeight(90);
        form->addRow(edge_list_);
        remove_edge_button_ = new QPushButton(tr("Odebrat vybranou hranu"), this);
        restore_route_button_ = new QPushButton(tr("Obnovit celou trasu"), this);
        form->addRow(remove_edge_button_); form->addRow(restore_route_button_);
        connect(remove_edge_button_, &QPushButton::clicked, this, [this] {
            const auto* item = edge_list_->currentItem();
            if (item == nullptr || !remove_edge_) return;
            const auto group = item->data(0, Qt::UserRole).toUInt();
            const auto member_data = item->data(0, Qt::UserRole + 1);
            remove_edge_(group, member_data.isValid()
                ? std::optional<std::size_t>{member_data.toUInt()} : std::nullopt);
        });
        connect(restore_route_button_, &QPushButton::clicked, this, [this] {
            const auto* item = edge_list_->currentItem();
            if (item != nullptr && restore_route_)
                restore_route_(item->data(0, Qt::UserRole).toUInt());
        });
        set_edge_groups(initial.edge_treatment.routes);
    }

    content_layout()->addLayout(form);

    // Shared bottom operation row, matching Python's _build_operation_form
    // for primitives, Extrusion and Revolution. The hidden combo remains an
    // internal value adapter only; it is no longer part of the visible UI.
    if (operation_ != nullptr) {
        auto* operation_row = new QWidget(this);
        auto* operation_layout = new QHBoxLayout(operation_row);
        operation_layout->setContentsMargins(0, 0, 0, 0);
        operation_layout->setSpacing(8);
        add_operation_button_ = new QPushButton(tr("Přičíst"), this);
        subtract_operation_button_ = new QPushButton(tr("Odečíst"), this);
        for (auto* button : {add_operation_button_, subtract_operation_button_}) {
            button->setCheckable(true);
            button->setMinimumHeight(40);
            button->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Fixed);
        }
        add_operation_button_->setObjectName("primitiveAddOperation");
        subtract_operation_button_->setObjectName("primitiveSubtractOperation");
        add_operation_button_->setStyleSheet(
            "QPushButton{border:2px solid #2d5670;border-radius:6px;font-weight:700;"
            "padding:7px 14px} QPushButton:checked{background:#00d1ff;color:#101510;"
            "border-color:#6fe3ff}");
        subtract_operation_button_->setStyleSheet(
            "QPushButton{border:2px solid #713d3d;border-radius:6px;font-weight:700;"
            "padding:7px 14px} QPushButton:checked{background:#c64b4b;color:#ffffff;"
            "border-color:#ed7777}");
        const bool subtract = operation_->currentData() == "subtract";
        add_operation_button_->setChecked(!subtract);
        subtract_operation_button_->setChecked(subtract);
        operation_layout->addWidget(add_operation_button_);
        operation_layout->addWidget(subtract_operation_button_);
        auto* operation_form = new QFormLayout;
        operation_form->addRow(tr("Operace"), operation_row);
        content_layout()->addLayout(operation_form);
        const auto select_operation = [this](bool subtract_selected) {
            operation_->setCurrentIndex(operation_->findData(
                subtract_selected ? "subtract" : "add"));
            add_operation_button_->setChecked(!subtract_selected);
            subtract_operation_button_->setChecked(subtract_selected);
            notify_preview();
        };
        connect(add_operation_button_, &QPushButton::clicked, this,
            [select_operation] { select_operation(false); });
        connect(subtract_operation_button_, &QPushButton::clicked, this,
            [select_operation] { select_operation(true); });
        if (assembly_cut_mode) {
            add_operation_button_->setEnabled(false);
            subtract_operation_button_->setEnabled(false);
        }
    }

    if (assembly_cut_mode) {
        assembly_targets_ = new QListWidget(this);
        assembly_targets_->setObjectName("assemblyCutTargets");
        assembly_targets_->setMinimumHeight(100);
        for (const auto& [id, label] : assembly_targets) {
            auto* item = new QListWidgetItem(QString::fromStdString(label),
                assembly_targets_);
            item->setData(Qt::UserRole, QString::fromStdString(id));
            item->setFlags(item->flags() | Qt::ItemIsUserCheckable);
            item->setCheckState(std::find(selected_targets.begin(),
                selected_targets.end(), id) != selected_targets.end()
                    ? Qt::Checked : Qt::Unchecked);
        }
        content_layout()->addWidget(new QLabel(
            tr("Cílové komponenty sestavy"), this));
        content_layout()->addWidget(assembly_targets_);
    }

    error_ = new QLabel(this);
    error_->setStyleSheet("color: #c64b4b;");
    error_->setWordWrap(true);
    content_layout()->addWidget(error_);
    connect(name_, &QLineEdit::textChanged, this, [this](const QString&) {
        error_->clear();
    });
    // Basic-solid dimensions drive the analytical wire preview immediately.
    // The OCCT body is still calculated only when OK commits the dialog.
    for (auto* input : {length_, width_, height_, radius_, top_radius_, top_offset_}) {
        if (input != nullptr) connect(input, &QDoubleSpinBox::valueChanged,
            this, [this] { notify_preview(); });
    }
    if (treatment_type_ != nullptr) {
        connect(treatment_type_, &QComboBox::currentIndexChanged,
            this, [this] {
                refresh_edge_treatment_fields();
                notify_preview();
            });
        for (auto* field : {
                 treatment_primary_, treatment_secondary_, treatment_angle_}) {
            connect(field, &QDoubleSpinBox::valueChanged,
                this, [this] { notify_preview(); });
        }
        connect(treatment_flip_, &QPushButton::toggled,
            this, [this] { notify_preview(); });
        connect(treatment_reverse_, &QPushButton::toggled,
            this, [this] { notify_preview(); });
    }
    for (auto* input : translation_) {
        if (input != nullptr) connect(input, &QDoubleSpinBox::valueChanged,
            this, [this] { notify_preview(); });
    }
    for (auto* input : rotation_) {
        if (input != nullptr) connect(input, &QDoubleSpinBox::valueChanged,
            this, [this] { notify_preview(); });
    }
    // Compare edits against the values represented by the fully constructed
    // controls, rather than against the raw persisted object. Some widgets
    // normalize equivalent values while loading (for example a placement or
    // extent combo). Pressing OK without a user-visible change must therefore
    // remain a no-op and must not create an Undo revision.
    accepted_baseline_ = values();
}

zima::document::HistoryContainer PrimitivePropertiesDialog::values() const {
    const QString name = name_->text().trimmed();
    auto result = initial_;
    result.name = name.toStdString();
    if (operation_ != nullptr) {
        result.combine_mode = operation_->currentData().toString() == "subtract"
            ? zima::document::CombineMode::Subtract
            : zima::document::CombineMode::Add;
    }
    if (result.feature_kind == zima::document::FeatureKind::Box) {
        result.box = {length_->value(), width_->value(), height_->value()};
    } else if (result.feature_kind == zima::document::FeatureKind::Cylinder) {
        result.cylinder = {radius_->value(), height_->value()};
    } else if (result.feature_kind == zima::document::FeatureKind::Thread) {
        result.thread.nominal_diameter = hole_thread_nominal_diameter_->value();
        result.thread.pitch = thread_size_->currentData(
            Qt::UserRole+2).toDouble();
        result.thread.profile_diameter = thread_profile_diameter_->value();
        result.thread.custom_profile_diameter =
            thread_custom_profile_diameter_->isChecked();
        const auto standard=thread_standard_->currentData().toString();
        result.thread.standard = standard=="metric"
            ? zima::document::ThreadStandard::Metric
            : standard=="whitworth"
            ? zima::document::ThreadStandard::Whitworth
            : zima::document::ThreadStandard::Pipe;
        const auto side=thread_side_->currentData().toString();
        result.thread.side = side=="internal"
            ? zima::document::ThreadSide::Internal
            : side=="external"
            ? zima::document::ThreadSide::External
            : zima::document::ThreadSide::Automatic;
        result.thread.designation = thread_size_->currentData().toString().toStdString();
        result.thread.dimension_label =
            thread_dimension_label_->text().trimmed().toStdString();
        result.thread.direction = thread_direction_->currentData() == "reverse"
            ? zima::document::ExtrusionDirection::Reverse
            : zima::document::ExtrusionDirection::Forward;
        result.thread.extent_mode = extent_mode_->currentData() == "two_sides"
            ? zima::document::ProfileExtentMode::TwoSides
            : extent_mode_->currentData() == "symmetric"
                ? zima::document::ProfileExtentMode::Symmetric
                : zima::document::ProfileExtentMode::OneSide;
        result.thread.end_condition_forward =
            hole_thread_end_->currentData() == "up_to"
            ? zima::document::EndCondition::UpTo
            : hole_thread_end_->currentData() == "through_all"
                ? zima::document::EndCondition::ThroughAll
                : zima::document::EndCondition::Length;
        result.thread.end_condition_reverse =
            reverse_end_condition_->currentData() == "up_to"
            ? zima::document::EndCondition::UpTo
            : reverse_end_condition_->currentData() == "through_all"
                ? zima::document::EndCondition::ThroughAll
                : zima::document::EndCondition::Length;
        result.thread.length_forward = hole_thread_length_->value();
        result.thread.length_reverse = result.thread.extent_mode ==
                zima::document::ProfileExtentMode::Symmetric
            ? result.thread.length_forward : reverse_length_->value();
        result.thread.runout_pitch_factor = thread_runout_factor_->value();
        result.thread.left_hand = hole_left_hand_->isChecked();
    } else if (result.feature_kind == zima::document::FeatureKind::Hole) {
        const auto end = [](const QComboBox* combo) {
            return combo->currentData() == "up_to"
                ? zima::document::EndCondition::UpTo
                : combo->currentData() == "through_all"
                    ? zima::document::EndCondition::ThroughAll
                    : zima::document::EndCondition::Length;
        };
        const auto type = hole_type_->currentData();
        result.hole.type = type == "metric" ? zima::document::HoleType::MetricThread
            : type == "pipe" ? zima::document::HoleType::PipeThread
            : type == "whitworth" ? zima::document::HoleType::WhitworthThread
            : zima::document::HoleType::Plain;
        result.hole.diameter = hole_diameter_->value();
        auto profile = zima::sketcher::Sketch::from_serialized(
            result.hole.sketch_serialized);
        const auto profile_circle = std::find_if(profile.circles.begin(),
            profile.circles.end(), [&](const auto& circle) {
                return circle.id == result.hole.circle_id;
            });
        if (profile_circle != profile.circles.end()) {
            profile_circle->radius = result.hole.diameter * 0.5;
            result.hole.sketch_serialized = profile.serialized();
        }
        result.hole.bore_end_condition = end(hole_bore_end_);
        result.hole.bore_length = hole_bore_length_->value();
        result.hole.bore_end_targets = initial_.hole.bore_end_targets;
        result.hole.entrance_chamfer = hole_entrance_chamfer_->value();
        result.hole.drill_point_enabled = hole_drill_point_->isChecked();
        result.hole.drill_point_angle_degrees = hole_drill_angle_->value();
        result.hole.exit_chamfer_enabled =
            hole_exit_chamfer_enabled_->isChecked() &&
            !result.hole.drill_point_enabled;
        result.hole.exit_chamfer = hole_exit_chamfer_->value();
        const double radius = result.hole.diameter * 0.5;
        auto chamfer_profile = zima::sketcher::Sketch::from_serialized(
            result.hole.chamfer_sketch_serialized);
        if (result.hole.entrance_chamfer > 0.0) {
            const std::array<std::array<double, 2>, 3> chamfer_points{{
                {{radius, 0.0}},
                {{radius + result.hole.entrance_chamfer, 0.0}},
                {{radius, result.hole.entrance_chamfer}}}};
            for (std::size_t index = 0; index < chamfer_points.size(); ++index) {
                if (auto* point = chamfer_profile.find_point(
                        result.hole.chamfer_point_ids[index])) {
                    point->x = chamfer_points[index][0];
                    point->y = chamfer_points[index][1];
                }
            }
            result.hole.chamfer_sketch_serialized = chamfer_profile.serialized();
        }
        auto tip_profile = zima::sketcher::Sketch::from_serialized(
            result.hole.tip_sketch_serialized);
        const double half_angle = std::clamp(
            result.hole.drill_point_angle_degrees *
                std::numbers::pi / 360.0,
            1.0e-4, std::numbers::pi*0.5-1.0e-4);
        const double point_depth = radius/std::tan(half_angle);
        const std::array<std::array<double, 2>, 3> tip_points =
            result.hole.drill_point_enabled
            ? std::array<std::array<double, 2>, 3>{{
                {{0.0, result.hole.bore_length}},
                {{radius, result.hole.bore_length}},
                {{0.0, result.hole.bore_length + point_depth}}}}
            : std::array<std::array<double, 2>, 3>{{
                {{radius, result.hole.bore_length -
                    result.hole.exit_chamfer}},
                {{radius + result.hole.exit_chamfer,
                    result.hole.bore_length}},
                {{radius, result.hole.bore_length}}}};
        for (std::size_t index = 0; index < tip_points.size(); ++index) {
            if (auto* point = tip_profile.find_point(
                    result.hole.tip_point_ids[index])) {
                point->x = tip_points[index][0];
                point->y = tip_points[index][1];
            }
        }
        result.hole.tip_sketch_serialized = tip_profile.serialized();
        result.hole.thread_enabled = hole_thread_enabled_->isChecked();
        result.hole.thread_nominal_diameter = hole_thread_nominal_diameter_->value();
        result.hole.thread_pitch = hole_thread_pitch_->value();
        result.hole.thread_end_condition = end(hole_thread_end_);
        result.hole.thread_length = result.hole.thread_end_condition ==
                zima::document::EndCondition::ThroughAll
            ? result.hole.bore_length : hole_thread_length_->value();
        result.hole.left_hand_thread = hole_left_hand_->isChecked();
    } else if (result.feature_kind ==
            zima::document::FeatureKind::DrillPoint) {
        result.combine_mode = zima::document::CombineMode::Subtract;
        result.drill_point.bottom_faces = drill_point_faces_;
        result.drill_point.included_angle_degrees =
            drill_point_angle_->value();
    } else if (result.feature_kind == zima::document::FeatureKind::Sphere) {
        result.sphere = {radius_->value()};
    } else if (result.feature_kind == zima::document::FeatureKind::Cone) {
        result.cone = {radius_->value(), top_radius_->value(), height_->value()};
    } else if (result.feature_kind == zima::document::FeatureKind::Pyramid) {
        result.pyramid = {length_->value(), width_->value(), height_->value()};
    } else if (result.feature_kind == zima::document::FeatureKind::Wedge) {
        result.wedge = {length_->value(), width_->value(), height_->value(),
                        top_offset_->value()};
    } else if (result.feature_kind == zima::document::FeatureKind::Extrusion) {
        result.extrusion.profile_plane_offset = profile_plane_offset_->value();
        result.extrusion.profile_source =
            zima::document::ProfileSource::Internal;
        result.extrusion.result_type = result_type_->currentData() == "thin"
            ? zima::document::ProfileResultType::Thin
            : zima::document::ProfileResultType::Solid;
        result.extrusion.thin_thickness = thin_thickness_->value();
        result.extrusion.thin_mode = thin_mode_->currentData() == "other_side"
            ? zima::document::ThinMode::OtherSide
            : thin_mode_->currentData() == "symmetric"
                ? zima::document::ThinMode::Symmetric
                : zima::document::ThinMode::OneSide;
        result.extrusion.extent_mode = extent_mode_->currentData() == "two_sides"
            ? zima::document::ProfileExtentMode::TwoSides
            : extent_mode_->currentData() == "symmetric"
                ? zima::document::ProfileExtentMode::Symmetric
                : zima::document::ProfileExtentMode::OneSide;
        result.extrusion.length_forward = forward_length_->value();
        result.extrusion.length_reverse = reverse_length_->value();
        const auto condition = [](const QComboBox* combo) {
            return combo->currentData() == "up_to"
                ? zima::document::EndCondition::UpTo
                : combo->currentData() == "through_all"
                    ? zima::document::EndCondition::ThroughAll
                    : zima::document::EndCondition::Length;
        };
        result.extrusion.end_condition_forward = condition(forward_end_condition_);
        result.extrusion.end_condition_reverse = condition(reverse_end_condition_);
        const QString direction =
            extrusion_direction_->currentData().toString();
        result.extrusion.direction = direction == "reverse"
            ? zima::document::ExtrusionDirection::Reverse
            : zima::document::ExtrusionDirection::Forward;
        result.extrusion.height = result.extrusion.extent_mode ==
                zima::document::ProfileExtentMode::OneSide
            ? result.extrusion.length_forward
            : result.extrusion.length_forward + result.extrusion.length_reverse;
    } else if (result.feature_kind == zima::document::FeatureKind::Revolution) {
        result.revolution.profile_plane_offset = profile_plane_offset_->value();
        result.revolution.profile_source =
            zima::document::ProfileSource::Internal;
        result.revolution.result_type = result_type_->currentData() == "thin"
            ? zima::document::ProfileResultType::Thin
            : zima::document::ProfileResultType::Solid;
        result.revolution.thin_thickness = thin_thickness_->value();
        result.revolution.thin_mode = thin_mode_->currentData() == "other_side"
            ? zima::document::ThinMode::OtherSide
            : thin_mode_->currentData() == "symmetric"
                ? zima::document::ThinMode::Symmetric
                : zima::document::ThinMode::OneSide;
        result.revolution.extent_mode = extent_mode_->currentData() == "two_sides"
            ? zima::document::ProfileExtentMode::TwoSides
            : extent_mode_->currentData() == "symmetric"
                ? zima::document::ProfileExtentMode::Symmetric
                : zima::document::ProfileExtentMode::OneSide;
        result.revolution.direction = extrusion_direction_->currentData() == "reverse"
            ? zima::document::ExtrusionDirection::Reverse
            : zima::document::ExtrusionDirection::Forward;
        result.revolution.angle_degrees = forward_length_->value();
        result.revolution.angle_reverse = reverse_length_->value();
    } else if (result.feature_kind == zima::document::FeatureKind::Shell) {
        result.shell.removed_faces = shell_faces_;
        result.shell.thickness = shell_thickness_->value();
    } else if (result.feature_kind != zima::document::FeatureKind::ImportedStep) {
        const auto mode = treatment_type_->currentData().toString();
        if (result.feature_kind == zima::document::FeatureKind::Fillet) {
            result.edge_treatment.fillet_mode = mode == "linear"
                ? zima::document::EdgeTreatmentParameters::FilletMode::Linear
                : zima::document::EdgeTreatmentParameters::FilletMode::Constant;
        } else {
            result.edge_treatment.chamfer_mode = mode == "two_distances"
                ? zima::document::EdgeTreatmentParameters::ChamferMode::TwoDistances
                : mode == "distance_angle"
                    ? zima::document::EdgeTreatmentParameters::ChamferMode::DistanceAngle
                    : zima::document::EdgeTreatmentParameters::ChamferMode::EqualDistance;
        }
        result.edge_treatment.primary_size = treatment_primary_->value();
        result.edge_treatment.secondary_size = treatment_secondary_->value();
        result.edge_treatment.angle_degrees = treatment_angle_->value();
        result.edge_treatment.flip = treatment_flip_->isChecked();
        result.edge_treatment.reverse = treatment_reverse_->isChecked();
    }
    if (result.feature_kind == zima::document::FeatureKind::Box ||
        result.feature_kind == zima::document::FeatureKind::Cylinder ||
        result.feature_kind == zima::document::FeatureKind::Hole ||
        result.feature_kind == zima::document::FeatureKind::Thread ||
        result.feature_kind == zima::document::FeatureKind::Sphere ||
        result.feature_kind == zima::document::FeatureKind::Cone ||
        result.feature_kind == zima::document::FeatureKind::Pyramid ||
        result.feature_kind == zima::document::FeatureKind::Wedge ||
        result.feature_kind == zima::document::FeatureKind::Extrusion ||
        result.feature_kind == zima::document::FeatureKind::Revolution ||
        result.feature_kind == zima::document::FeatureKind::ImportedStep) {
        result.placement = placement_->numeric_placement();
        const auto placement_references = placement_
            ? placement_->combined_references(3)
            : std::vector<zima::document::ConstructionReference>{};
        result.placement.references = placement_references;
    }
    return result;
}

void PrimitivePropertiesDialog::add_edge_reference(
    const zima::kernel::EdgeReference& edge) {
    if (!edge.valid()) return;
    auto edges = initial_.edge_treatment.flattened_edges();
    if (std::find(edges.begin(), edges.end(), edge) == edges.end()) {
        edges.push_back(edge);
    }
    if (edge_list_ != nullptr) set_edge_groups({std::move(edges)});
    notify_preview();
}

void PrimitivePropertiesDialog::set_edge_references(
    std::vector<zima::kernel::EdgeReference> edges) {
    initial_.edge_treatment.routes = edges.empty()
        ? std::vector<EdgeGroup>{}
        : std::vector<EdgeGroup>{std::move(edges)};
    if (edge_list_ != nullptr && edge_groups_.empty()) {
        set_edge_groups(initial_.edge_treatment.routes);
    }
    notify_preview();
}

void PrimitivePropertiesDialog::set_edge_groups(std::vector<EdgeGroup> groups) {
    edge_groups_ = std::move(groups);
    initial_.edge_treatment.routes = edge_groups_;
    if (edge_list_ == nullptr) return;
    edge_list_->clear();
    for (std::size_t group_index = 0; group_index < edge_groups_.size(); ++group_index) {
        const auto& group = edge_groups_[group_index];
        if (group.empty()) continue;
        auto* route = new QTreeWidgetItem(edge_list_, {
            tr("Trasa %1").arg(group_index + 1),
            tr("%1 objektů").arg(group.size())});
        route->setData(0, Qt::UserRole, static_cast<uint>(group_index));
        for (std::size_t member = 0; member < group.size(); ++member) {
            const auto& edge = group[member];
            auto* child = new QTreeWidgetItem(route, {QString{},
                QString::fromStdString(edge.owner_id + " / " + edge.semantic_key)});
            child->setData(0, Qt::UserRole, static_cast<uint>(group_index));
            child->setData(0, Qt::UserRole + 1, static_cast<uint>(member));
        }
        route->setExpanded(true);
    }
    const bool available =
        !initial_.edge_treatment.flattened_edges().empty();
    if (remove_edge_button_) remove_edge_button_->setEnabled(available);
    if (restore_route_button_) restore_route_button_->setEnabled(available);
    notify_preview();
}

void PrimitivePropertiesDialog::set_edge_group_callbacks(
    std::function<void(std::size_t, std::optional<std::size_t>)> remove,
    std::function<void(std::size_t)> restore) {
    remove_edge_ = std::move(remove);
    restore_route_ = std::move(restore);
}

void PrimitivePropertiesDialog::set_extrusion_target(
    zima::kernel::FaceReference reference, zima::kernel::Vec3 origin,
    zima::kernel::Vec3 normal, std::string label) {
    zima::document::ExtrusionParameters::EndTarget target;
    target.kind = zima::document::EndTargetKind::Plane;
    target.reference = std::move(reference);
    target.label = label.empty()
        ? target.reference.owner_id + " / " + target.reference.semantic_key
        : std::move(label);
    target.fallback_origin = origin;
    target.fallback_normal = normal;
    auto& targets = initial_.feature_kind == zima::document::FeatureKind::Hole
        ? initial_.hole.bore_end_targets
        : initial_.feature_kind == zima::document::FeatureKind::Thread
            ? active_end_target_side_ == "reverse"
                ? initial_.thread.end_targets_reverse
                : initial_.thread.end_targets_forward
        : active_end_target_side_ == "reverse"
            ? initial_.extrusion.end_targets_reverse
            : initial_.extrusion.end_targets_forward;
    targets = {target};
    auto* edit = active_end_target_side_ == "reverse"
        ? reverse_end_target_ : forward_end_target_;
    if (edit != nullptr) edit->setText(QString::fromStdString(target.label));
    if (active_end_target_side_ == "reverse") {
        reverse_end_target_pick_active_ = false;
        if (reverse_end_target_clear_action_)
            reverse_end_target_clear_action_->setEnabled(true);
    } else {
        forward_end_target_pick_active_ = false;
        if (forward_end_target_clear_action_)
            forward_end_target_clear_action_->setEnabled(true);
    }
    refresh_extrusion_target_styles();
    notify_preview();
}

void PrimitivePropertiesDialog::set_edge_route_start_vertices(
    std::vector<zima::kernel::VertexReference> vertices) {
    initial_.edge_treatment.route_start_vertices = std::move(vertices);
}

void PrimitivePropertiesDialog::set_extrusion_surface_target(
    zima::kernel::FaceReference reference,
    std::vector<zima::kernel::Vec3> triangles, std::string label) {
    zima::document::ExtrusionParameters::EndTarget target;
    target.kind = zima::document::EndTargetKind::Face;
    target.reference = std::move(reference);
    target.label = label.empty()
        ? target.reference.owner_id + " / " + target.reference.semantic_key
        : std::move(label);
    target.fallback_triangles = std::move(triangles);
    auto& targets = initial_.feature_kind == zima::document::FeatureKind::Hole
        ? initial_.hole.bore_end_targets
        : initial_.feature_kind == zima::document::FeatureKind::Thread
            ? active_end_target_side_ == "reverse"
                ? initial_.thread.end_targets_reverse
                : initial_.thread.end_targets_forward
        : active_end_target_side_ == "reverse"
            ? initial_.extrusion.end_targets_reverse
            : initial_.extrusion.end_targets_forward;
    targets = {target};
    auto* edit = active_end_target_side_ == "reverse"
        ? reverse_end_target_ : forward_end_target_;
    if (edit != nullptr) edit->setText(QString::fromStdString(target.label));
    if (active_end_target_side_ == "reverse") {
        reverse_end_target_pick_active_ = false;
        if (reverse_end_target_clear_action_)
            reverse_end_target_clear_action_->setEnabled(true);
    } else {
        forward_end_target_pick_active_ = false;
        if (forward_end_target_clear_action_)
            forward_end_target_clear_action_->setEnabled(true);
    }
    refresh_extrusion_target_styles();
    notify_preview();
}

bool PrimitivePropertiesDialog::eventFilter(QObject* watched, QEvent* event) {
    if (drill_point_face_list_ != nullptr &&
        watched == drill_point_face_list_->viewport() &&
        event->type() == QEvent::MouseButtonPress) {
        const auto* mouse = static_cast<QMouseEvent*>(event);
        if (mouse->button() == Qt::LeftButton &&
            request_drill_point_face_selection_ &&
            drill_point_face_list_->itemAt(
                mouse->position().toPoint()) == nullptr) {
            request_drill_point_face_selection_();
        }
    }
    if (shell_face_list_ != nullptr &&
        watched == shell_face_list_->viewport() &&
        event->type() == QEvent::MouseButtonPress) {
        const auto* mouse = static_cast<QMouseEvent*>(event);
        if (mouse->button() == Qt::LeftButton &&
            request_shell_face_selection_ &&
            shell_face_list_->itemAt(mouse->position().toPoint()) == nullptr) {
            request_shell_face_selection_();
        }
    }
    if ((watched == forward_end_target_ || watched == reverse_end_target_) &&
        event->type() == QEvent::MouseButtonPress) {
        const auto* mouse = static_cast<QMouseEvent*>(event);
        if (mouse->button() == Qt::LeftButton) {
            const std::string side = watched == reverse_end_target_
                ? "reverse" : "forward";
            const auto& targets = side == "reverse"
                ? initial_.extrusion.end_targets_reverse
                : initial_.extrusion.end_targets_forward;
            static_cast<void>(targets);
            request_extrusion_target(side);
            event->accept();
            return true;
        }
    }
    return PropertiesSubWindow::eventFilter(watched, event);
}

void PrimitivePropertiesDialog::request_extrusion_target(
    const std::string& side) {
    const bool had_highlights = forward_end_target_highlighted_ ||
        reverse_end_target_highlighted_;
    active_end_target_side_ = side;
    forward_end_target_pick_active_ = side == "forward";
    reverse_end_target_pick_active_ = side == "reverse";
    forward_end_target_highlighted_ = false;
    reverse_end_target_highlighted_ = false;
    refresh_extrusion_target_styles();
    if (had_highlights && reference_highlights_changed_)
        reference_highlights_changed_();
    if (extrusion_target_request_) extrusion_target_request_();
}

void PrimitivePropertiesDialog::finish_extrusion_target_entry() {
    forward_end_target_pick_active_ = false;
    reverse_end_target_pick_active_ = false;
    refresh_extrusion_target_styles();
}

void PrimitivePropertiesDialog::clear_extrusion_target(
    const std::string& side) {
    const bool was_active = side == "reverse"
        ? reverse_end_target_pick_active_ : forward_end_target_pick_active_;
    auto& targets = initial_.feature_kind == zima::document::FeatureKind::Hole
        ? initial_.hole.bore_end_targets
        : initial_.feature_kind == zima::document::FeatureKind::Thread
            ? side == "reverse" ? initial_.thread.end_targets_reverse
                                : initial_.thread.end_targets_forward
        : side == "reverse" ? initial_.extrusion.end_targets_reverse
                            : initial_.extrusion.end_targets_forward;
    targets.clear();
    auto* edit = side == "reverse" ? reverse_end_target_ : forward_end_target_;
    if (edit != nullptr) edit->clear();
    if (side == "reverse") {
        reverse_end_target_highlighted_ = false;
        reverse_end_target_pick_active_ = false;
        if (reverse_end_target_clear_action_)
            reverse_end_target_clear_action_->setEnabled(false);
    } else {
        forward_end_target_highlighted_ = false;
        forward_end_target_pick_active_ = false;
        if (forward_end_target_clear_action_)
            forward_end_target_clear_action_->setEnabled(false);
    }
    refresh_extrusion_target_styles();
    if (reference_highlights_changed_) reference_highlights_changed_();
    if (was_active && extrusion_target_cancel_) extrusion_target_cancel_();
    notify_preview();
}

void PrimitivePropertiesDialog::toggle_extrusion_target_highlight(
    const std::string& side) {
    if (side == "reverse") {
        reverse_end_target_highlighted_ = !reverse_end_target_highlighted_;
    } else {
        forward_end_target_highlighted_ = !forward_end_target_highlighted_;
    }
    refresh_extrusion_target_styles();
    if (reference_highlights_changed_) reference_highlights_changed_();
}

void PrimitivePropertiesDialog::refresh_extrusion_target_styles() {
    const auto apply = [](QLineEdit* edit, QToolButton* inspection,
                          bool active, bool highlighted, bool populated) {
        if (edit == nullptr) return;
        const QString background = highlighted
            ? QStringLiteral("background:#00d1ff;color:#102027;")
            : QString{};
        const QString border = active
            ? QStringLiteral("border:2px solid #42d66b;")
            : QStringLiteral("border:1px solid palette(mid);");
        edit->setStyleSheet(QStringLiteral("QLineEdit{%1%2padding:2px 5px}")
            .arg(background, border));
        if (inspection != nullptr) {
            const QSignalBlocker blocked(inspection);
            inspection->setEnabled(populated);
            inspection->setChecked(highlighted && populated);
        }
    };
    apply(forward_end_target_, forward_end_targets_button_,
        forward_end_target_pick_active_, forward_end_target_highlighted_,
        initial_.feature_kind == zima::document::FeatureKind::Hole
            ? !initial_.hole.bore_end_targets.empty()
            : initial_.feature_kind == zima::document::FeatureKind::Thread
                ? !initial_.thread.end_targets_forward.empty()
            : !initial_.extrusion.end_targets_forward.empty());
    apply(reverse_end_target_, reverse_end_targets_button_,
        reverse_end_target_pick_active_, reverse_end_target_highlighted_,
        initial_.feature_kind == zima::document::FeatureKind::Thread
            ? !initial_.thread.end_targets_reverse.empty()
            : !initial_.extrusion.end_targets_reverse.empty());
}

void PrimitivePropertiesDialog::set_extrusion_target_request(
    std::function<void()> callback) {
    extrusion_target_request_ = std::move(callback);
}

void PrimitivePropertiesDialog::set_profile_pick_request(
    std::function<void(bool)> callback) {
    profile_pick_request_ = std::move(callback);
}

void PrimitivePropertiesDialog::set_edit_sketch_callback(
    std::function<void(zima::document::HistoryContainer)> callback) {
    edit_sketch_ = std::move(callback);
}

void PrimitivePropertiesDialog::set_commit_required(bool required) {
    commit_required_ = required;
}

void PrimitivePropertiesDialog::set_preview_callback(
    std::function<void(const zima::document::HistoryContainer&)> callback) {
    preview_ = std::move(callback);
    notify_preview();
}

double PrimitivePropertiesDialog::profile_plane_offset() const {
    return profile_plane_offset_ == nullptr ? 0.0 : profile_plane_offset_->value();
}

double PrimitivePropertiesDialog::forward_extent_length() const {
    return forward_length_ == nullptr ? 0.0 : forward_length_->value();
}

double PrimitivePropertiesDialog::reverse_extent_length() const {
    return reverse_length_ == nullptr ? 0.0 : reverse_length_->value();
}

zima::document::ProfileExtentMode
PrimitivePropertiesDialog::profile_extent_mode() const {
    if (extent_mode_ == nullptr)
        return zima::document::ProfileExtentMode::OneSide;
    return extent_mode_->currentData() == "two_sides"
        ? zima::document::ProfileExtentMode::TwoSides
        : extent_mode_->currentData() == "symmetric"
            ? zima::document::ProfileExtentMode::Symmetric
            : zima::document::ProfileExtentMode::OneSide;
}

bool PrimitivePropertiesDialog::extrusion_direction_reversed() const {
    return extrusion_direction_ != nullptr &&
        extrusion_direction_->currentData() == "reverse";
}

void PrimitivePropertiesDialog::set_profile_offset_and_forward_length(
    double offset, double length) {
    if (profile_plane_offset_ == nullptr || forward_length_ == nullptr) return;
    const QSignalBlocker offset_blocker(profile_plane_offset_);
    const QSignalBlocker length_blocker(forward_length_);
    profile_plane_offset_->setValue(offset);
    forward_length_->setValue(std::max(0.001, length));
    notify_preview();
}

void PrimitivePropertiesDialog::set_forward_extent_length(double length) {
    if (forward_length_ == nullptr) return;
    forward_length_->setValue(std::max(0.001, length));
}

void PrimitivePropertiesDialog::set_forward_extent_and_direction(
        double length, bool reversed) {
    if (forward_length_ == nullptr || extrusion_direction_ == nullptr) return;
    const QSignalBlocker length_blocker(forward_length_);
    const QSignalBlocker direction_blocker(extrusion_direction_);
    forward_length_->setValue(std::clamp(std::abs(length),
        forward_length_->minimum(), forward_length_->maximum()));
    extrusion_direction_->setCurrentIndex(
        extrusion_direction_->findData(reversed ? "reverse" : "forward"));
    notify_preview();
}

void PrimitivePropertiesDialog::set_reverse_extent_length(double length) {
    if (reverse_length_ == nullptr) return;
    reverse_length_->setValue(std::clamp(std::abs(length),
        reverse_length_->minimum(), reverse_length_->maximum()));
}

void PrimitivePropertiesDialog::set_reverse_extent_and_direction(
        double length, bool reversed) {
    if (reverse_length_ == nullptr || extrusion_direction_ == nullptr) return;
    const QSignalBlocker length_blocker(reverse_length_);
    const QSignalBlocker direction_blocker(extrusion_direction_);
    reverse_length_->setValue(std::clamp(std::abs(length),
        reverse_length_->minimum(), reverse_length_->maximum()));
    extrusion_direction_->setCurrentIndex(
        extrusion_direction_->findData(reversed ? "reverse" : "forward"));
    notify_preview();
}

void PrimitivePropertiesDialog::notify_preview() {
    if (preview_) preview_(values());
}

void PrimitivePropertiesDialog::set_extrusion_target_cancel(
    std::function<void()> callback) {
    extrusion_target_cancel_ = std::move(callback);
}

void PrimitivePropertiesDialog::refresh_edge_treatment_fields() {
    if (treatment_type_ == nullptr) return;
    const bool fillet =
        initial_.feature_kind == zima::document::FeatureKind::Fillet;
    const QString mode = treatment_type_->currentData().toString();
    const bool second = fillet ? mode == "linear" : mode == "two_distances";
    const bool angle = !fillet && mode == "distance_angle";
    treatment_primary_label_->setText(fillet
        ? (second ? tr("R1") : tr("Poloměr R")) : tr("Vzdálenost A"));
    treatment_secondary_label_->setText(fillet ? tr("R2") : tr("Vzdálenost B"));
    treatment_secondary_label_->setVisible(second);
    treatment_secondary_->setVisible(second);
    treatment_angle_label_->setVisible(angle);
    treatment_angle_->setVisible(angle);
    treatment_flip_->setVisible(!fillet && mode != "equal_distance");
    treatment_reverse_->setVisible(fillet && second);
}

void PrimitivePropertiesDialog::set_shell_faces(
    std::vector<zima::kernel::FaceReference> faces) {
    shell_faces_ = std::move(faces);
    initial_.shell.removed_faces = shell_faces_;
    if (shell_face_list_ == nullptr) return;
    shell_face_list_->clear();
    for (std::size_t index = 0; index < shell_faces_.size(); ++index) {
        auto* item = new QListWidgetItem(
            tr("Plocha %1").arg(index + 1), shell_face_list_);
        item->setToolTip(QString::fromStdString(
            shell_faces_[index].owner_id + " / " +
            shell_faces_[index].semantic_key));
    }
    remove_shell_face_button_->setEnabled(!shell_faces_.empty());
}

void PrimitivePropertiesDialog::set_drill_point_faces(
        std::vector<zima::kernel::FaceReference> faces) {
    drill_point_faces_ = std::move(faces);
    initial_.drill_point.bottom_faces = drill_point_faces_;
    if (drill_point_face_list_ == nullptr) return;
    drill_point_face_list_->clear();
    for (std::size_t index = 0; index < drill_point_faces_.size(); ++index) {
        auto* item = new QListWidgetItem(
            tr("Dno otvoru %1").arg(index + 1), drill_point_face_list_);
        item->setToolTip(QString::fromStdString(
            drill_point_faces_[index].owner_id + " / " +
            drill_point_faces_[index].semantic_key));
    }
    remove_drill_point_face_button_->setEnabled(!drill_point_faces_.empty());
    notify_preview();
}

void PrimitivePropertiesDialog::set_drill_point_face_callbacks(
        std::function<void(std::size_t)> remove,
        std::function<void()> request_selection) {
    remove_drill_point_face_ = std::move(remove);
    request_drill_point_face_selection_ = std::move(request_selection);
}

void PrimitivePropertiesDialog::set_drill_point_face_selection_active(
        bool active) {
    if (drill_point_face_list_ == nullptr) return;
    drill_point_face_list_->setStyleSheet(active
        ? QStringLiteral("QListWidget { border: 2px solid #00aa44; }")
        : QString{});
}

void PrimitivePropertiesDialog::set_shell_face_callbacks(
    std::function<void(std::size_t)> remove,
    std::function<void()> request_selection) {
    remove_shell_face_ = std::move(remove);
    request_shell_face_selection_ = std::move(request_selection);
}

void PrimitivePropertiesDialog::set_shell_face_selection_active(bool active) {
    if (shell_face_list_ == nullptr) return;
    shell_face_list_->setStyleSheet(active
        ? QStringLiteral("QListWidget{border:2px solid #80AA1A;}")
        : QString{});
}

bool PrimitivePropertiesDialog::submit() {
    if (name_->text().trimmed().isEmpty()) {
        error_->setText(tr("Název nesmí být prázdný."));
        name_->setFocus();
        return false;
    }
    auto result = values();
    if (result.feature_kind == zima::document::FeatureKind::Hole) {
        result.combine_mode = zima::document::CombineMode::Subtract;
        if (result.hole.bore_end_condition ==
                zima::document::EndCondition::UpTo &&
            (result.hole.bore_end_targets.empty() ||
             !result.hole.bore_end_targets.front().reference.valid())) {
            error_->setText(tr("Vyberte cílovou rovinu nebo plochu otvoru."));
            return false;
        }
        if (result.hole.thread_enabled &&
            result.hole.thread_length > result.hole.bore_length) {
            error_->setText(tr(
                "Délka závitu nesmí přesáhnout délku válcové části otvoru."));
            hole_thread_length_->setFocus();
            return false;
        }
    }
    if (result.feature_kind == zima::document::FeatureKind::Thread) {
        const auto missing = [](zima::document::EndCondition condition,
                const auto& targets) {
            return condition == zima::document::EndCondition::UpTo &&
                (targets.empty() || !targets.front().reference.valid());
        };
        if (missing(result.thread.end_condition_forward,
                result.thread.end_targets_forward) ||
            (result.thread.extent_mode !=
                    zima::document::ProfileExtentMode::OneSide &&
             missing(result.thread.end_condition_reverse,
                result.thread.end_targets_reverse))) {
            error_->setText(tr("Vyberte cílovou rovinu nebo plochu závitu."));
            return false;
        }
    }
    if (result.feature_kind == zima::document::FeatureKind::DrillPoint &&
        result.drill_point.bottom_faces.empty() && !edit_mode_) {
        error_->setText(tr("Vyberte alespoň jedno kruhové dno otvoru."));
        return false;
    }
    std::vector<std::string> selected_targets;
    if (assembly_targets_ != nullptr) {
        for (int row = 0; row < assembly_targets_->count(); ++row) {
            const auto* item = assembly_targets_->item(row);
            if (item->checkState() == Qt::Checked) {
                selected_targets.push_back(
                    item->data(Qt::UserRole).toString().toStdString());
            }
        }
        if (selected_targets.empty()) {
            error_->setText(tr("Vyberte alespoň jednu cílovou komponentu sestavy."));
            return false;
        }
        result.combine_mode = zima::document::CombineMode::Subtract;
    }
    if (result.feature_kind == zima::document::FeatureKind::Extrusion) {
        const auto missing_target = [](zima::document::EndCondition condition,
                                       const auto& targets) {
            return condition == zima::document::EndCondition::UpTo &&
                (targets.empty() || !targets.front().reference.valid());
        };
        if (missing_target(result.extrusion.end_condition_forward,
                           result.extrusion.end_targets_forward) ||
            (result.extrusion.extent_mode ==
                 zima::document::ProfileExtentMode::TwoSides &&
             missing_target(result.extrusion.end_condition_reverse,
                            result.extrusion.end_targets_reverse))) {
            error_->setText(tr("Vyberte cílový bod, rovinu nebo rovinnou plochu."));
            return false;
        }
        if (result.combine_mode != zima::document::CombineMode::Subtract &&
            (result.extrusion.end_condition_forward ==
                 zima::document::EndCondition::ThroughAll ||
             (result.extrusion.extent_mode ==
                  zima::document::ProfileExtentMode::TwoSides &&
              result.extrusion.end_condition_reverse ==
                  zima::document::EndCondition::ThroughAll))) {
            error_->setText(tr("Skrz vše je dostupné pouze pro řez."));
            return false;
        }
    }
    if (!commit_required_ && edit_mode_ && accepted_baseline_ &&
        result == *accepted_baseline_ &&
        selected_targets == accepted_target_baseline_) {
        return true;
    }
    try {
        commit_(std::move(result), std::move(selected_targets));
    } catch (const std::exception& failure) {
        error_->setText(QString::fromUtf8(failure.what()));
        return false;
    }
    return true;
}

void PrimitivePropertiesDialog::set_reference_request_callback(
    ReferenceRequestCallback callback) {
    reference_request_ = std::move(callback);
}

void PrimitivePropertiesDialog::set_origin_selection_mode_callback(
        std::function<void(bool)> callback) {
    if (placement_) placement_->set_origin_selection_mode_callback(std::move(callback));
}

void PrimitivePropertiesDialog::set_origin_selection_mode_active(bool active) {
    if (placement_) placement_->set_origin_selection_mode_active(active);
}

void PrimitivePropertiesDialog::set_reference_highlights_changed_callback(
    ReferenceHighlightsChangedCallback callback) {
    reference_highlights_changed_ = std::move(callback);
}

std::set<std::string>
PrimitivePropertiesDialog::highlighted_reference_owner_ids() const {
    auto result = placement_ ? placement_->highlighted_reference_owner_ids()
                             : std::set<std::string>{};
    const auto append_target = [&](bool highlighted, const auto& targets) {
        if (highlighted && !targets.empty() && targets.front().reference.valid())
            result.insert(targets.front().reference.owner_id);
    };
    append_target(forward_end_target_highlighted_,
        initial_.feature_kind == zima::document::FeatureKind::Hole
            ? initial_.hole.bore_end_targets
            : initial_.feature_kind == zima::document::FeatureKind::Thread
                ? initial_.thread.end_targets_forward
            : initial_.extrusion.end_targets_forward);
    append_target(reverse_end_target_highlighted_,
        initial_.feature_kind == zima::document::FeatureKind::Thread
            ? initial_.thread.end_targets_reverse
            : initial_.extrusion.end_targets_reverse);
    return result;
}

std::vector<zima::document::ConstructionReference>
PrimitivePropertiesDialog::highlighted_reference_entries() const {
    auto result = placement_ ? placement_->highlighted_reference_entries()
                             : std::vector<zima::document::ConstructionReference>{};
    const auto append_target = [&](bool highlighted, const auto& targets) {
        if (!highlighted || targets.empty() ||
            !targets.front().reference.valid()) return;
        const auto& reference = targets.front().reference;
        result.push_back({reference.instance_path, reference.owner_id,
            reference.semantic_key});
    };
    append_target(forward_end_target_highlighted_,
        initial_.feature_kind == zima::document::FeatureKind::Hole
            ? initial_.hole.bore_end_targets
            : initial_.feature_kind == zima::document::FeatureKind::Thread
                ? initial_.thread.end_targets_forward
            : initial_.extrusion.end_targets_forward);
    append_target(reverse_end_target_highlighted_,
        initial_.feature_kind == zima::document::FeatureKind::Thread
            ? initial_.thread.end_targets_reverse
            : initial_.extrusion.end_targets_reverse);
    return result;
}

bool PrimitivePropertiesDialog::set_reference(std::size_t index,
    zima::document::ConstructionReference reference, const QString& label) {
    if (!placement_) return false;
    QString error;
    if (!placement_->set_reference(index, std::move(reference), label, &error)) {
        if (!error.isEmpty()) error_->setText(error);
        return false;
    }
    error_->clear();
    return true;
}

const std::string& PrimitivePropertiesDialog::container_id() const {
    return initial_.id;
}

zima::document::FeatureKind PrimitivePropertiesDialog::feature_kind() const {
    return initial_.feature_kind;
}

bool PrimitivePropertiesDialog::owns_reference_owner(
    const std::string& owner_id) const {
    return owner_id == initial_.id || owner_id == initial_.feature_id ||
        owner_id == initial_.container_origin.id ||
        std::ranges::any_of(initial_.container_origin.children,
            [&](const auto& child) { return child.id == owner_id; });
}

std::vector<zima::document::ConstructionReference>
PrimitivePropertiesDialog::references_without(std::size_t index) const {
    return placement_ ? placement_->references_without(index)
                       : std::vector<zima::document::ConstructionReference>{};
}

std::size_t PrimitivePropertiesDialog::first_empty_position_index() const {
    return placement_ ? placement_->first_empty_position_index() : 3;
}

void PrimitivePropertiesDialog::set_active_reference_index(
    std::optional<std::size_t> index) {
    if (placement_) placement_->set_active_reference_index(index);
}

void PrimitivePropertiesDialog::set_reference_inspected(
    std::size_t index, bool inspected) {
    if (placement_) placement_->set_reference_inspected(index, inspected);
}

void PrimitivePropertiesDialog::clear_reference_highlights() {
    if (placement_) placement_->clear_reference_highlights();
    const bool had_target_highlights = forward_end_target_highlighted_ ||
        reverse_end_target_highlighted_;
    forward_end_target_highlighted_ = false;
    reverse_end_target_highlighted_ = false;
    refresh_extrusion_target_styles();
    if (had_target_highlights && reference_highlights_changed_)
        reference_highlights_changed_();
}

void PrimitivePropertiesDialog::set_remaining_translation_dof(int dof) {
    if (!placement_) return;
    placement_->set_remaining_translation_dof(dof);
    remaining_translation_dof_ = placement_->remaining_translation_dof();
}

void PrimitivePropertiesDialog::set_remaining_rotation_dof(int dof) {
    if (!placement_) return;
    placement_->set_remaining_rotation_dof(dof);
    remaining_rotation_dof_ = placement_->remaining_rotation_dof();
}

void PrimitivePropertiesDialog::set_rotation_constraint_state(
        const zima::document::OrientationConstraintState& state) {
    if (!placement_) return;
    placement_->set_rotation_constraint_state(state);
    remaining_rotation_dof_ = placement_->remaining_rotation_dof();
}

void PrimitivePropertiesDialog::set_translation_constraint_state(
    const zima::document::PointConstraintState& state,
    const zima::kernel::Vec3& solution) {
    set_remaining_translation_dof(state.remaining_dof);
    placement_->set_translation_constraint_state(state, solution);
}

void PrimitivePropertiesDialog::set_orientation_base_rotation(
    const zima::kernel::Vec3& rotation, bool constrained) {
    if (!placement_) return;
    placement_->set_orientation_base_rotation(rotation, constrained);
}

void PrimitivePropertiesDialog::set_resolved_rotation(
        const zima::kernel::Vec3& rotation, bool valid) {
    if (!placement_) return;
    placement_->set_resolved_rotation(rotation, valid);
}

bool PrimitivePropertiesDialog::set_inline_parameter_value(
    std::string_view key, double value) {
    const auto set_field = [value](QDoubleSpinBox* field) {
        if (field == nullptr || !field->isEnabled() || !field->isVisible())
            return false;
        field->setValue(value);
        return true;
    };
    constexpr std::string_view placement_prefix{"placement:"};
    if (key.starts_with(placement_prefix)) {
        key.remove_prefix(placement_prefix.size());
        if (key == "x") return set_field(translation_[0]);
        if (key == "y") return set_field(translation_[1]);
        if (key == "z") return set_field(translation_[2]);
        if (key == "rotation_x" || key == "rotation_y" ||
            key == "rotation_z") {
            if (placement_ == nullptr) return false;
            const std::size_t index = key == "rotation_x" ? 0
                : key == "rotation_y" ? 1 : 2;
            if (set_field(placement_->rotation_fields()[index])) return true;
            return set_field(placement_->rotation_offset_fields()[index]);
        }
        constexpr std::string_view reference_prefix{"reference_offset:"};
        if (!key.starts_with(reference_prefix) || placement_ == nullptr)
            return false;
        const auto suffix = key.substr(reference_prefix.size());
        if (suffix.empty()) return false;
        std::size_t index{};
        for (const char digit : suffix) {
            if (digit < '0' || digit > '9') return false;
            index = index * 10 + static_cast<std::size_t>(digit - '0');
        }
        return placement_->set_reference_offset(index, value);
    }
    if (key == "length") return set_field(length_);
    if (key == "width") return set_field(width_);
    if (key == "height") return set_field(height_);
    if (key == "radius" || key == "bottom_radius") return set_field(radius_);
    if (key == "top_radius") return set_field(top_radius_);
    if (key == "top_offset") return set_field(top_offset_);
    if (key == "length_forward") return set_field(forward_length_);
    if (key == "length_reverse" && extent_mode_ != nullptr &&
        extent_mode_->currentData() == "symmetric") {
        return set_field(forward_length_);
    }
    if (key == "length_reverse") return set_field(reverse_length_);
    if (key == "profile_offset") return set_field(profile_plane_offset_);
    if (key == "thread_nominal_diameter")
        return set_field(hole_thread_nominal_diameter_);
    if (key == "thread_length") return set_field(hole_thread_length_);
    if (key == "included_angle") return set_field(drill_point_angle_);
    if (key == "angle") return set_field(forward_length_);
    if (key == "size" || key == "primary") return set_field(treatment_primary_);
    if (key == "secondary") return set_field(treatment_secondary_);
    if (key == "treatment_angle") return set_field(treatment_angle_);
    return false;
}

}  // namespace zima::app
