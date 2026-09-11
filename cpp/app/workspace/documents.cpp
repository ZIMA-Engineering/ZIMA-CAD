#include "workspace_internal.hpp"
#include <zima/workspace/document_operations.hpp>

namespace zima::app {
using namespace workspace_detail;

namespace {


class NewDocumentDialog final : public zima::ui::PropertiesSubWindow {
public:
    using Accepted = std::function<QString(QString, QString)>;

    NewDocumentDialog(Accepted accepted, QMainWindow* parent)
        : PropertiesSubWindow(QObject::tr("Nový dokument"), parent),
          accepted_(std::move(accepted)) {
        setObjectName("newDocumentDialog");
        set_centered_on_show();
        setMinimumWidth(420);
        auto* content = new QWidget(this);
        auto* layout = new QVBoxLayout(content);
        auto* form = new QFormLayout;
        name_ = new QLineEdit(QStringLiteral("part"), content);
        name_->setObjectName("newDocumentFileName");
        form->addRow(QObject::tr("Název souboru"), name_);
        layout->addLayout(form);
        layout->addWidget(new QLabel(QObject::tr("Typ dokumentu"), content));
        part_ = add_type(layout, QObject::tr("Díl"), "part", "part", true);
        add_type(layout, QObject::tr("Sestava"), "assembly", "assembly", true);
        add_type(layout, QObject::tr("Výkres"), "drawing", "drawing", true);
        add_type(
            layout, QObject::tr("Formát výkresu"), "drawing_format",
            "drawing-format", true);
        add_type(
            layout, QObject::tr("Razítko výkresu"), "title_block",
            "title-block", true);
        part_->setChecked(true);
        error_ = new QLabel(content);
        error_->setObjectName("newDocumentError");
        error_->setWordWrap(true);
        error_->setStyleSheet(QStringLiteral("color:#F08A85;"));
        error_->hide();
        layout->addWidget(error_);
        content_layout()->addWidget(content);
        setAttribute(Qt::WA_DeleteOnClose);
    }

private:
    QLineEdit* name_{};
    QRadioButton* part_{};
    QLabel* error_{};
    Accepted accepted_;

    QRadioButton* add_type(QVBoxLayout* layout, const QString& label,
                           const QString& type, const QString& icon,
                           bool enabled) {
        auto* radio = new QRadioButton(label, this);
        radio->setIcon(resource_icon(icon));
        radio->setProperty("documentType", type);
        radio->setEnabled(enabled);
        layout->addWidget(radio);
        return radio;
    }

    bool submit() override {
        const QString stem = QFileInfo(name_->text().trimmed())
                                 .completeBaseName().trimmed();
        if (stem.isEmpty()) {
            error_->setText(QObject::tr("Zadejte název souboru."));
            error_->show();
            return false;
        }
        const auto radios = findChildren<QRadioButton*>();
        const auto selected = std::find_if(radios.begin(), radios.end(),
            [](const auto* radio) { return radio->isChecked(); });
        if (selected == radios.end()) return false;
        const QString error = accepted_(
            (*selected)->property("documentType").toString(), stem);
        if (!error.isEmpty()) {
            error_->setText(error);
            error_->show();
            return false;
        }
        return true;
    }
};

} // namespace



void AssemblyWorkspaceWindow::new_document() {
    if(section_dialog_&&!active_sketch_id_.empty())return;
    if (properties_dialog_ != nullptr) {
        properties_dialog_->raise();
        return;
    }
    auto* dialog = new NewDocumentDialog(
        [this](QString type, QString stem) {
            return create_document(type, stem);
        }, this);
    properties_dialog_ = dialog;
    connect(dialog, &QDialog::finished, this, [this, dialog] {
        if (properties_dialog_ != dialog) return;
        properties_dialog_ = nullptr;
        preserve_view_on_refresh_ = true;
        refresh_scene();
    });
    connect(dialog, &QObject::destroyed, this, [this, dialog] {
        if (properties_dialog_ == dialog) properties_dialog_ = nullptr;
    });
    dialog->show();
}

QString AssemblyWorkspaceWindow::create_document(
    const QString& document_type, const QString& file_stem) {
    const std::string name = file_stem.trimmed().toStdString();
    if (name.empty()) return tr("Zadejte název souboru.");
    const QString suffix = document_type == QStringLiteral("part")
        ? QStringLiteral(".prtz")
        : document_type == QStringLiteral("assembly")
            ? QStringLiteral(".asmz")
            : document_type == QStringLiteral("drawing")
                ? QStringLiteral(".drwz") : document_type == "title_block" ? QStringLiteral(".tblz")
                    : document_type == "drawing_format" ? QStringLiteral(".frmz") : QString{};
    if (suffix.isEmpty()) return tr("Tento typ dokumentu zatím není podporován.");
    const std::filesystem::path path = working_directory_ /
        (name + suffix.toStdString());
    if (std::filesystem::exists(path) || workspace_.document_id_for_path(path)) {
        return tr("Soubor %1 již existuje nebo je otevřený.").arg(
            QString::fromStdString(path.filename().string()));
    }
    std::string id;
    try {
        if (document_type == "title_block" || document_type == "drawing_format") {
            auto document = zima::document::PartDocument::create_default(); document.name=name;
            auto sketch = zima::drawing::create_template_sketch(document_type=="title_block",name);
            auto container=zima::document::PartDocument::create_sketch_container();
            container.name=name;sketch.owner_container_id=container.id;
            document.insert_history_entry(zima::document::PartHistoryKind::Feature,container.id);
            document.history.push_back(std::move(container));document.sketches.push_back(std::move(sketch));
            id=document.document_id;workspace_.add_part(std::move(document),{},path);
        } else {
            const auto type=workspace::native_document_type(path);
            std::map<std::string,std::string> units;
            for(auto it=application_settings_.units.cbegin();it!=application_settings_.units.cend();++it)
                units[it.key().toStdString()]=it.value().toStdString();
            auto prepared=workspace::prepare_new_native_document(type,name,path,
                native_template_settings(application_settings_),units);
            id=workspace::insert_native_document(workspace_,std::move(prepared));
            switch(type) {
                case workspace::NativeDocumentType::Part: active_application_=ApplicationMode::Modeling;break;
                case workspace::NativeDocumentType::Assembly: active_application_=ApplicationMode::Assembly;break;
                case workspace::NativeDocumentType::Drawing: active_application_=ApplicationMode::Drawing;break;
            }
        }
    } catch (const std::exception& error) {
        return tr("Dokument nelze vytvořit: %1").arg(error.what());
    }
    workspace_.activate(id);
    workspace_.display_top_level(id);
    finish_document_switch(false);
    return {};
}

void AssemblyWorkspaceWindow::close_document(int tab_index) {
    if(section_dialog_&&!active_sketch_id_.empty())return;
    if (properties_dialog_ != nullptr) {
        state_->setText(tr("Nejprve dokončete nebo zrušte otevřené vlastnosti."));
        properties_dialog_->raise();
        return;
    }
    if (tab_index < 0) tab_index = tabs_->currentIndex();
    if (tab_index < 0 || tab_index >= tabs_->count()) return;
    const std::string id = tabs_->tabData(tab_index).toString().toStdString();
    const auto* state = workspace_.find(id);
    if (state == nullptr) return;
    bool discard=false;
    const bool dirty=workspace::document_needs_save(workspace_,id);
    if (dirty) {
        const auto answer = QMessageBox::warning(
            this, tr("Neuložené změny"),
            tr("Dokument obsahuje neuložené změny. Chcete je uložit?"),
            QMessageBox::Save | QMessageBox::Discard | QMessageBox::Cancel,
            QMessageBox::Save);
        if (answer == QMessageBox::Cancel) return;
        discard=answer==QMessageBox::Discard;
        if (answer == QMessageBox::Save) {
            workspace_.activate(id);
            workspace_.display_top_level(id);
            save_active_document();
            if(!workspace_.find(id) || workspace::document_needs_save(workspace_,id))return;
        }
    }
    if (workspace::close_document(workspace_,id,discard)!=workspace::CloseDocumentResult::Closed) return;
    active_occurrence_path_.clear();
    active_sketch_id_.clear();
    selected_sketch_id_.clear();
    selected_sketch_segment_id_.clear();
    selected_sketch_point_id_.clear();
    selected_sketch_circle_id_.clear();
    selected_sketch_arc_id_.clear();
    selected_sketch_ellipse_id_.clear();
    selected_sketch_elliptical_arc_id_.clear();
    selected_sketch_bspline_id_.clear();
    selected_sketch_text_id_.clear();
    cancel_sketch_segment();
    refresh_tabs();
    refresh_scene();
}

void AssemblyWorkspaceWindow::new_part() {
    static_cast<void>(create_document(QStringLiteral("part"), tr("Nový díl")));
}

void AssemblyWorkspaceWindow::new_assembly() {
    static_cast<void>(create_document(
        QStringLiteral("assembly"), tr("Nová sestava")));
}

void AssemblyWorkspaceWindow::new_drawing() {
    static_cast<void>(create_document(
        QStringLiteral("drawing"), tr("Nový výkres")));
}

void AssemblyWorkspaceWindow::open_document() {
    const QString path = open_file(this,
        application_settings_.text("file.open_document", tr("Otevřít dokument")),
        QString::fromStdString(working_directory_.string()),
        application_settings_.text("file.filter.document",
            tr("Dokumenty ZIMA-CAD (*.prtz *.asmz *.drwz *.frmz *.tblz)")),
        application_settings_.translations);
    if (path.isEmpty()) return;
    static_cast<void>(open_document_path(path));
}

bool AssemblyWorkspaceWindow::open_document_path(const QString& path) {
    if(section_dialog_)return false;
    const std::filesystem::path opened_path = std::filesystem::u8path(path.toStdString());
    begin_status_operation(tr("Otevírám %1…").arg(
        QString::fromStdString(opened_path.filename().string())));
    try {
        std::string id;
        if (const auto already_open = workspace_.document_id_for_path(opened_path)) {
            update_status_operation(tr("Aktivuji již otevřený dokument…"));
            id = *already_open;
        } else if (path.endsWith(".frmz",Qt::CaseInsensitive) || path.endsWith(".tblz",Qt::CaseInsensitive)) {
            auto sketch=zima::drawing::load_template_sketch(opened_path,[](auto& text){rebuild_sketch_text_contours(text,true);});
            auto document=zima::document::PartDocument::create_default();document.name=opened_path.stem().string();
            auto container=zima::document::PartDocument::create_sketch_container();container.name=sketch.name;
            sketch.owner_container_id=container.id;
            document.insert_history_entry(zima::document::PartHistoryKind::Feature,container.id);
            document.history.push_back(std::move(container));document.sketches.push_back(std::move(sketch));
            id=document.document_id;workspace_.add_part(std::move(document),{},opened_path);
        } else {
            const auto type=workspace::native_document_type(opened_path);
            switch(type) {
                case workspace::NativeDocumentType::Part:
                    update_status_operation(tr("Čtu Part, parametry a uloženou geometrii…"),-1,0);break;
                case workspace::NativeDocumentType::Assembly:
                    update_status_operation(tr("Čtu sestavu a její uložené výskyty…"),-1,0);break;
                case workspace::NativeDocumentType::Drawing:
                    update_status_operation(tr("Čtu výkres, listy a pohledy…"),-1,0);break;
            }
            auto loaded=run_background_task([opened_path] { return workspace::read_native_document(opened_path); });
            switch(type) {
                case workspace::NativeDocumentType::Part:
                    update_status_operation(tr("Vkládám Part do pracovního prostoru…"));break;
                case workspace::NativeDocumentType::Assembly:
                    update_status_operation(tr("Vkládám sestavu do pracovního prostoru…"));break;
                case workspace::NativeDocumentType::Drawing:
                    update_status_operation(tr("Vkládám výkres do pracovního prostoru…"));break;
            }
            id=workspace::insert_native_document(workspace_,std::move(loaded));
        }
        workspace_.activate(id);
        workspace_.display_top_level(id);
    } catch (const std::exception& error) {
        finish_status_operation(tr("Otevření dokumentu selhalo"), false);
        report_operation_error(tr("Otevření dokumentu selhalo"), error.what());
        return false;
    }
    if (!opened_path.parent_path().empty()) {
        working_directory_ = opened_path.parent_path();
    }
    update_status_operation(tr("Připravuji strom a View…"));
    finish_document_switch(true);
    finish_status_operation(tr("Otevřeno: %1").arg(
        QString::fromStdString(opened_path.filename().string())));
    return true;
}

void AssemblyWorkspaceWindow::finish_document_switch(bool opening) {
    active_occurrence_path_.clear();
    active_sketch_id_.clear();
    selected_sketch_id_.clear();
    selected_sketch_segment_id_.clear();
    selected_sketch_point_id_.clear();
    selected_sketch_circle_id_.clear();
    selected_sketch_arc_id_.clear();
    selected_sketch_ellipse_id_.clear();
    selected_sketch_elliptical_arc_id_.clear();
    selected_sketch_bspline_id_.clear();
    selected_sketch_text_id_.clear();
    cancel_sketch_segment();
    if(opening)activate_first_part_body();
    refresh_tabs();refresh_scene();
}

void AssemblyWorkspaceWindow::refresh_tabs() {
    tabs_->blockSignals(true);
    while (tabs_->count() > 0) tabs_->removeTab(0);
    int displayed_index = -1;
    QString displayed_label = tr("Bez dokumentu");
    for (const auto& state : workspace_.documents()) {
        std::visit([&](const auto& document) {
            using State = std::decay_t<decltype(document)>;
            if constexpr(std::is_same_v<State,zima::workspace::DrawingState>) {
                const QString label = document.path.empty()
                    ? QString::fromStdString(document.document().name)
                    : QString::fromStdString(document.path.filename().string());
                const int index=tabs_->addTab(resource_icon("drawing"),
                    label);
                tabs_->setTabData(index,QString::fromStdString(document.document().document_id));
                if(document.document().document_id==workspace_.displayed_document_id()) {
                    displayed_index=index;
                    displayed_label=label;
                }
            } else {
                const auto& model = document.session.document();
                const QString label = document.path.empty()
                    ? QString::fromStdString(model.name)
                    : QString::fromStdString(document.path.filename().string());
                const int index = tabs_->addTab(
                    resource_icon([&]() -> QString {
                        if constexpr(std::is_same_v<State,zima::workspace::PartState>) {
                            if(!model.sketches.empty()&&model.sketches.front().drawing_template)
                                return model.sketches.front().drawing_template->kind=="title_block"?"title-block":"drawing-format";
                            return "part";
                        } else return "assembly";
                    }()),
                    label + (document.session.is_dirty() ? QStringLiteral(" *") : QString{}));
                tabs_->setTabData(index, QString::fromStdString(model.document_id));
                if (model.document_id == workspace_.displayed_document_id()) {
                    displayed_index = index;
                    displayed_label = label;
                }
            }
        }, state);
    }
    for (int index = 0; index < tabs_->count(); ++index) {
        // Reserve an explicit right inset inside the tab button slot. Native
        // styles can otherwise place the red button against/outside the tab edge.
        auto* close_slot = new QWidget(tabs_);
        close_slot->setFixedSize(36, 22);
        auto* close_layout = new QHBoxLayout(close_slot);
        close_layout->setContentsMargins(0, 0, 10, 0);
        close_layout->setSpacing(0);
        auto* close = new TabCloseButton(close_slot);
        close_layout->addWidget(close);
        close->setObjectName("documentTabCloseButton");
        close->setFixedSize(26, 22);
        close->setFocusPolicy(Qt::NoFocus);
        close->setToolTip(tr("Zavřít dokument"));
        close->setAccessibleName(tr("Zavřít dokument"));
        // Match the reference-row remove button. An actual styled widget
        // avoids the platform-specific QTabBar close subcontrol/icon.
        close->setStyleSheet(
            "QPushButton{color:#ffffff;background:#8b2424;"
            "border:1px solid #b94a4a;border-radius:4px;"
            "font-size:16px;font-weight:700;padding:0}"
            "QPushButton:hover{background:#b83232;border-color:#ed7777}"
            "QPushButton:pressed{background:#6f1d1d}");
        const auto document_id = tabs_->tabData(index);
        connect(close, &QPushButton::clicked, this, [this, document_id] {
            for (int current = 0; current < tabs_->count(); ++current) {
                if (tabs_->tabData(current) != document_id) continue;
                emit tabs_->tabCloseRequested(current);
                return;
            }
        });
        tabs_->setTabButton(index, QTabBar::RightSide, close_slot);
    }
    tabs_->setCurrentIndex(displayed_index);
    tabs_->blockSignals(false);
    setWindowTitle(QStringLiteral("ZIMA-CAD — %1 — %2")
        .arg(instance_.label(), displayed_label));
    update_document_area_visibility();
}

void AssemblyWorkspaceWindow::update_document_kind_button() {
    if (document_kind_button_ == nullptr) return;
    const std::string displayed = workspace_.displayed_document_id();
    if (displayed.empty()) {
        document_kind_button_->hide();
        return;
    }
    QString label;
    QString tooltip;
    if(template_sketch()){document_kind_button_->hide();return;}
    if (const auto* drawing = workspace_.open_drawing(displayed)) {
        bool assembly_source = false;
        if (!drawing->document().source_document_id.empty()) {
            assembly_source = workspace_.open_assembly(
                    drawing->document().source_document_id) != nullptr ||
                drawing->document().source_path.extension() == ".asmz";
        } else if (!drawing->document().sheets.empty() &&
            !drawing->document().sheets.front().views.empty()) {
            const auto& view = drawing->document().sheets.front().views.front();
            assembly_source = workspace_.open_assembly(view.source_document_id) != nullptr ||
                view.source_path.extension() == ".asmz";
        }
        label = assembly_source ? tr("SESTAVA") : tr("DÍL");
        tooltip = assembly_source ? tr("Přejít na zdrojovou sestavu")
                                  : tr("Přejít na zdrojový díl");
    } else if (workspace_.open_part(displayed) != nullptr ||
               workspace_.open_assembly(displayed) != nullptr) {
        label = tr("VÝKRES");
        tooltip = tr("Otevřít nebo vytvořit výkres tohoto dokumentu");
    } else {
        document_kind_button_->hide();
        return;
    }
    document_kind_button_->setText(label);
    document_kind_button_->setToolTip(tooltip);
    document_kind_button_->show();
    QTimer::singleShot(0, this, [this] {
        if (document_kind_button_ == nullptr || !document_kind_button_->isVisible()) return;
        const QSize hint = document_kind_button_->sizeHint();
        const int height = std::min(tree_->header()->height() - 6, hint.height());
        const int width = std::max(72, hint.width() + 8);
        document_kind_button_->setGeometry(
            tree_->header()->width() - width - 5,
            std::max(2, (tree_->header()->height() - height) / 2), width, height);
        document_kind_button_->raise();
    });
}

void AssemblyWorkspaceWindow::navigate_document_kind() {
    const std::string displayed = workspace_.displayed_document_id();
    if (const auto* drawing = workspace_.open_drawing(displayed)) {
        std::string source_document_id = drawing->document().source_document_id;
        std::filesystem::path source_path = drawing->document().source_path;
        const auto drawing_directory = drawing->path.parent_path();
        if (source_document_id.empty() && !drawing->document().sheets.empty() &&
            !drawing->document().sheets.front().views.empty()) {
            const auto& first_view = drawing->document().sheets.front().views.front();
            source_document_id = first_view.source_document_id;
            source_path = first_view.source_path;
        }
        if (source_document_id.empty()) {
            state_->setText(tr("Výkres nemá přiřazený zdrojový dokument."));
            return;
        }
        if (!source_path.empty() && source_path.is_relative()) source_path = drawing_directory / source_path;
        if (workspace_.find(source_document_id) == nullptr) {
            if (source_path.empty() ||
                !open_document_path(QString::fromStdString(source_path.string()))) {
                state_->setText(tr("Zdrojový dokument výkresu nelze otevřít."));
                return;
            }
        } else {
            workspace_.activate(source_document_id);
            workspace_.display_top_level(source_document_id);
            active_occurrence_path_.clear();
            activate_first_part_body();
            viewer_->clear_selection();
            refresh_tabs();
            refresh_scene();
        }
        return;
    }

    std::filesystem::path source_path;
    QString source_name;
    if (const auto* part = workspace_.open_part(displayed)) {
        source_path = part->path;
        source_name = QString::fromStdString(part->session.document().name);
    } else if (const auto* assembly = workspace_.open_assembly(displayed)) {
        source_path = assembly->path;
        source_name = QString::fromStdString(assembly->session.document().name);
    } else return;
    if (source_path.empty()) {
        state_->setText(tr("Nejprve zdrojový dokument uložte."));
        return;
    }
    auto drawing_path = source_path;
    drawing_path.replace_extension(".drwz");
    if (const auto open = workspace_.document_id_for_path(drawing_path)) {
        workspace_.activate(*open);
        workspace_.display_top_level(*open);
        refresh_tabs(); refresh_scene();
        return;
    }
    if (std::filesystem::is_regular_file(drawing_path)) {
        static_cast<void>(open_document_path(
            QString::fromStdString(drawing_path.string())));
        return;
    }
    auto drawing = zima::drawing::DrawingDocument::create_default();
    drawing.name = source_name.toStdString();
    drawing.source_document_id = displayed;
    drawing.source_path = source_path;
    drawing.source_name = source_name.toStdString();
    const std::string drawing_id = drawing.document_id;
    workspace_.add_drawing(std::move(drawing), drawing_path);
    workspace_.activate(drawing_id);
    workspace_.display_top_level(drawing_id);
    refresh_tabs(); refresh_scene();
    state_->setText(tr("Nový výkres vytvořen. Pokračujte příkazem Vložit pohled."));
}

std::optional<std::string> AssemblyWorkspaceWindow::selected_occurrence_path() const {
    if (tree_ != nullptr && tree_->currentItem() != nullptr) {
        const auto path = tree_->currentItem()->data(0, Qt::UserRole + 1)
                              .toString().toStdString();
        if (!path.empty()) return path;
    }
    if (viewer_ != nullptr) {
        if (const auto selected = viewer_->confirmed_candidate();
            selected && !selected->instance_path.empty()) {
            return selected->instance_path;
        }
    }
    return std::nullopt;
}

} // namespace zima::app
