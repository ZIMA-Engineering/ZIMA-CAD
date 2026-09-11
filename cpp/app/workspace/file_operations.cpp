#include "workspace_internal.hpp"
#include <zima/workspace/document_operations.hpp>

namespace zima::app {
using namespace workspace_detail;

namespace {


class RenameDocumentDialog final : public zima::ui::PropertiesSubWindow {
public:
    RenameDocumentDialog(const QString& initial_name,
                         std::function<QString(QString)> accepted,
                         const ApplicationSettings& settings, QMainWindow* parent)
        : PropertiesSubWindow(settings.text("dialog.rename.title", tr("Přejmenovat")),
                              parent),
          accepted_(std::move(accepted)) {
        setObjectName("renameDocumentDialog");
        set_centered_on_show();
        setMinimumWidth(360);
        auto* form = new QFormLayout;
        name_ = new QLineEdit(initial_name, this);
        name_->setObjectName("renameDocumentName");
        form->addRow(settings.text("dialog.rename.label", tr("Nový název:")), name_);
        content_layout()->addLayout(form);
        error_ = new QLabel(this);
        error_->setObjectName("renameDocumentError");
        error_->setWordWrap(true);
        error_->setStyleSheet(QStringLiteral("color:#F08A85;"));
        error_->hide();
        content_layout()->addWidget(error_);
        setAttribute(Qt::WA_DeleteOnClose);
    }

private:
    QLineEdit* name_{};
    QLabel* error_{};
    std::function<QString(QString)> accepted_;

    bool submit() override {
        const QString error = accepted_(name_->text().trimmed());
        if (!error.isEmpty()) {
            error_->setText(error);
            error_->show();
            return false;
        }
        return true;
    }
};

class AboutSubWindow final : public zima::ui::PropertiesSubWindow {
public:
    explicit AboutSubWindow(QMainWindow* parent)
        : PropertiesSubWindow(QObject::tr("O aplikaci ZIMA-CAD"), parent) {
        setMinimumWidth(520);
        auto* artwork = new QLabel(this);
        artwork->setAlignment(Qt::AlignCenter);
        const QPixmap pixmap(QStringLiteral(":/zima/branding/about.svg"));
        if (!pixmap.isNull()) {
            artwork->setPixmap(pixmap.scaled(
                480, 260, Qt::KeepAspectRatio, Qt::SmoothTransformation));
            content_layout()->addWidget(artwork);
        }
        auto* description = new QLabel(
            QObject::tr(
                "ZIMA-CAD\nNativní CAD aplikace pro parametrické modelování, "
                "sestavy a technické výkresy.\n\n"
                "Vydání: ZIMA-CAD-%1")
                .arg(QStringLiteral(ZIMA_RELEASE_DATE)),
            this);
        description->setObjectName(QStringLiteral("aboutDescription"));
        description->setAlignment(Qt::AlignCenter);
        description->setWordWrap(true);
        content_layout()->addWidget(description);
        setAttribute(Qt::WA_DeleteOnClose);
    }

private:
    bool submit() override { return true; }
};

} // namespace



void AssemblyWorkspaceWindow::begin_status_operation(
    const QString& message) {
    ++operation_progress_generation_;
    operation_progress_->setRange(0, 0);
    operation_progress_->setFormat(message);
    operation_progress_->show();
    state_->setText(message);
    QApplication::processEvents(QEventLoop::ExcludeUserInputEvents, 1);
}

void AssemblyWorkspaceWindow::update_status_operation(
    const QString& message, int value, int maximum) {
    if (!operation_progress_->isVisible()) {
        begin_status_operation(message);
    }
    if (maximum == 0) {
        operation_progress_->setRange(0, 0);
    } else if (maximum > 0) {
        operation_progress_->setRange(0, maximum);
    }
    if (value >= 0 && operation_progress_->maximum() > 0) {
        operation_progress_->setValue(
            std::clamp(value, operation_progress_->minimum(),
                       operation_progress_->maximum()));
    }
    const bool percentage = operation_progress_->maximum() > 0;
    operation_progress_->setFormat(
        percentage ? message + QStringLiteral(" — %p%") : message);
    state_->setText(message);
    QApplication::processEvents(QEventLoop::ExcludeUserInputEvents, 1);
}

void AssemblyWorkspaceWindow::finish_status_operation(
    const QString& message, bool success) {
    operation_progress_->setRange(0, 100);
    operation_progress_->setValue(success ? 100 : 0);
    operation_progress_->setFormat(message);
    operation_progress_->show();
    state_->setText(message);
    const int generation = ++operation_progress_generation_;
    QApplication::processEvents(QEventLoop::ExcludeUserInputEvents, 1);
    QTimer::singleShot(1400, this, [this, generation] {
        if (generation == operation_progress_generation_) {
            operation_progress_->hide();
        }
    });
}

void AssemblyWorkspaceWindow::save_active_assembly() {
    auto* assembly = workspace_.open_assembly(workspace_.active_document_id());
    if (assembly == nullptr) return;
    QString path = QString::fromStdString(assembly->path.string());
    if (path.isEmpty()) path = save_file(
        this, application_settings_.text("file.save_assembly", tr("Uložit sestavu ZIMA-CAD")),
        QString::fromStdString((working_directory_ / "assembly.asmz").string()),
        application_settings_.text("file.filter.assembly",
            tr("Sestava ZIMA-CAD (*.asmz)")), "asmz",
        application_settings_.translations);
    if (path.isEmpty()) return;
    if (!path.endsWith(".asmz", Qt::CaseInsensitive)) {
        auto normalized = std::filesystem::path(path.toStdString());
        normalized.replace_extension(".asmz");
        path = QString::fromStdString(normalized.string());
    }
    begin_status_operation(tr("Ukládám sestavu %1…").arg(
        QFileInfo(path).fileName()));
    try {
        update_status_operation(
            tr("Zapisuji komponenty, vazby a uloženou geometrii…"), -1, 0);
        const auto id = assembly->session.document().document_id;
        auto job = workspace::prepare_document_save(workspace_, id, path.toStdString());
        const auto saved = run_background_task([job = std::move(job)] { return job.write(); });
        if (!workspace::complete_document_save(workspace_, saved))
            throw std::runtime_error(tr("Uložený dokument byl mezitím zavřen nebo změnil cestu.").toStdString());
        working_directory_ = workspace_.open_assembly(id)->path.parent_path();
        update_status_operation(tr("Aktualizuji stav dokumentu…"));
        refresh_tabs();
        finish_status_operation(tr("Sestava uložena: %1").arg(
            QFileInfo(path).fileName()));
    } catch (const std::exception& error) {
        finish_status_operation(tr("Uložení sestavy selhalo"), false);
        report_operation_error(
            application_settings_.text("message.save_failed", tr("Uložení se nezdařilo")),
            error.what());
    }
}

void AssemblyWorkspaceWindow::save_active_document() {
    if(template_sketch()){save_template_document(false);return;}
    if(auto* drawing=workspace_.open_drawing(workspace_.active_document_id())) {
        QString path=QString::fromStdString(drawing->path.string());
        if(path.isEmpty()) path=save_file(
            this, application_settings_.text("file.save_drawing", tr("Uložit výkres")),
            QString::fromStdString((working_directory_ / "drawing.drwz").string()),
            application_settings_.text("file.filter.drawing",
                tr("Výkres ZIMA-CAD (*.drwz)")), "drwz",
            application_settings_.translations);
        if(path.isEmpty()) return;
        if(!path.endsWith(".drwz", Qt::CaseInsensitive)) {
            auto normalized = std::filesystem::path(path.toStdString());
            normalized.replace_extension(".drwz");
            path = QString::fromStdString(normalized.string());
        }
        begin_status_operation(tr("Ukládám výkres %1…").arg(
            QFileInfo(path).fileName()));
        try {
            update_status_operation(
                tr("Zapisuji listy, pohledy a popisové pole…"), -1, 0);
            const auto id = drawing->document().document_id;
            auto job = workspace::prepare_document_save(workspace_, id, path.toStdString());
            const auto saved = run_background_task([job = std::move(job)] { return job.write(); });
            if (!workspace::complete_document_save(workspace_, saved))
                throw std::runtime_error(tr("Uložený dokument byl mezitím zavřen nebo změnil cestu.").toStdString());
            working_directory_ = workspace_.open_drawing(id)->path.parent_path();
            drawing_workspace_->edit_workspace_document(id);
            update_status_operation(tr("Aktualizuji stav dokumentu…"));
            refresh_tabs();
            finish_status_operation(tr("Výkres uložen: %1").arg(
                QFileInfo(path).fileName()));
        }
        catch(const std::exception& error) {
            finish_status_operation(tr("Uložení výkresu selhalo"), false);
            report_operation_error(
                application_settings_.text("message.save_failed", tr("Uložení se nezdařilo")),
                error.what()); }
        return;
    }
    if (workspace_.open_assembly(workspace_.active_document_id()) != nullptr) {
        save_active_assembly();
        return;
    }
    auto* part = workspace_.open_part(workspace_.active_document_id());
    if (part == nullptr) return;
    QString path = QString::fromStdString(part->path.string());
    if (path.isEmpty()) path = save_file(
        this, application_settings_.text("file.save_part", tr("Uložit díl ZIMA-CAD")),
        QString::fromStdString((working_directory_ / "part.prtz").string()),
        application_settings_.text("file.filter.part",
            tr("Díl ZIMA-CAD (*.prtz)")), "prtz",
        application_settings_.translations);
    if (path.isEmpty()) return;
    if (!path.endsWith(".prtz", Qt::CaseInsensitive)) {
        auto normalized = std::filesystem::path(path.toStdString());
        normalized.replace_extension(".prtz");
        path = QString::fromStdString(normalized.string());
    }
    begin_status_operation(tr("Ukládám Part %1…").arg(
        QFileInfo(path).fileName()));
    try {
        update_status_operation(
            tr("Připravuji neměnný snímek dokumentu…"));
        const auto id = part->session.document().document_id;
        auto job = workspace::prepare_document_save(workspace_, id, path.toStdString());
        update_status_operation(
            tr("Zapisuji parametry, B-Rep a data pro View…"), -1, 0);
        const auto saved = run_background_task([job = std::move(job)] { return job.write(); });
        if (!workspace::complete_document_save(workspace_, saved))
            throw std::runtime_error(tr("Uložený dokument byl mezitím zavřen nebo změnil cestu.").toStdString());
        working_directory_ = workspace_.open_part(id)->path.parent_path();
        update_status_operation(tr("Aktualizuji stav dokumentu…"));
        refresh_tabs();
        // Saving changes persistence state and the tab's dirty marker only.
        // Rebuilding an unchanged scene would invoke the viewer's automatic
        // fit and unexpectedly move the user's camera.
        finish_status_operation(tr("Part uložen: %1").arg(
            QFileInfo(path).fileName()));
    } catch (const std::exception& error) {
        finish_status_operation(tr("Uložení Partu selhalo"), false);
        report_operation_error(
            application_settings_.text("message.save_failed", tr("Uložení se nezdařilo")),
            error.what());
    }
}

void AssemblyWorkspaceWindow::save_active_document_as() {
    if(template_sketch()){save_template_document(true);return;}
    const std::string document_id = workspace_.active_document_id();
    if (document_id.empty()) return;
    QString caption;
    QString fallback_name;
    QString filter;
    QString suffix;
    if (const auto* drawing = workspace_.open_drawing(document_id)) {
        static_cast<void>(drawing);
        caption = application_settings_.text("file.save_drawing", tr("Uložit výkres"));
        fallback_name = QStringLiteral("drawing.drwz");
        filter = application_settings_.text("file.filter.drawing",
            tr("Výkres ZIMA-CAD (*.drwz)"));
        suffix = "drwz";
    } else if (const auto* assembly = workspace_.open_assembly(document_id)) {
        static_cast<void>(assembly);
        caption = application_settings_.text("file.save_assembly",
            tr("Uložit sestavu ZIMA-CAD"));
        fallback_name = QStringLiteral("assembly.asmz");
        filter = application_settings_.text("file.filter.assembly",
            tr("Sestava ZIMA-CAD (*.asmz)"));
        suffix = "asmz";
    } else if (const auto* part = workspace_.open_part(document_id)) {
        static_cast<void>(part);
        caption = application_settings_.text("file.save_part",
            tr("Uložit díl ZIMA-CAD"));
        fallback_name = QStringLiteral("part.prtz");
        filter = application_settings_.text("file.filter.part",
            tr("Díl ZIMA-CAD (*.prtz)"));
        suffix = "prtz";
    } else {
        return;
    }
    filter += tr(";;JPEG – aktuální pohled (*.jpg *.jpeg)");
    if(workspace_.open_drawing(document_id)) filter += tr(";;DXF – aktuální list (*.dxf)");
    const QString initial = QString::fromStdString((working_directory_ /
        fallback_name.toStdString()).string());
    QString selected = save_file(this, caption, initial, filter, suffix,
                                 application_settings_.translations);
    if (selected.isEmpty()) return;
    const auto export_extension=QFileInfo(selected).suffix().toLower();
    if(export_extension=="jpg" || export_extension=="jpeg" ||
       (export_extension=="dxf" && workspace_.open_drawing(document_id))) {
        try {
            if(export_extension=="dxf") drawing_workspace_->export_dxf(selected.toStdString());
            else if(workspace_.open_drawing(document_id)) drawing_workspace_->export_jpg(selected.toStdString());
            else {
                const auto image=viewer_->grabFramebuffer();
                QSaveFile output(selected);
                if(image.isNull() || !output.open(QIODevice::WriteOnly) ||
                   !image.save(&output,"JPG",95) || !output.commit())
                    throw std::runtime_error("Cannot save current view as JPG");
            }
            state_->setText(tr("Export uložen: %1").arg(selected));
        } catch(const std::exception& error) {
            QMessageBox::warning(this,tr("Export selhal"),error.what());
        }
        return;
    }
    const QString dotted_suffix = QStringLiteral(".") + suffix;
    std::filesystem::path target = selected.toStdString();
    if (QString::fromStdString(target.extension().string()).compare(
            dotted_suffix, Qt::CaseInsensitive) != 0) {
        target.replace_extension(dotted_suffix.toStdString());
        selected = QString::fromStdString(target.string());
    }
    if (const auto owner = workspace_.document_id_for_path(target);
        owner && *owner != document_id) {
        QMessageBox::warning(
            this, tr("Soubor je již otevřen"),
            tr("Cílový soubor již používá jiný otevřený dokument."));
        return;
    }
    begin_status_operation(tr("Připravuji kopii dokumentu a výkresu…"));
    try {
        auto snapshot = workspace_;
        const auto files = run_background_task(
            [snapshot = std::move(snapshot), document_id, target,
             search_directory = working_directory_] {
                return snapshot.save_copy(document_id, target, search_directory);
            });
        if (!target.parent_path().empty()) working_directory_ = target.parent_path();
        finish_status_operation(tr("Uložena kopie: %1 (%2 souborů)").arg(
            QString::fromStdString(target.filename().string())).arg(files.size()));
    } catch (const std::exception& error) {
        finish_status_operation(tr("Vytvoření kopie selhalo"), false);
        QMessageBox::critical(this, tr("Uložení kopie se nezdařilo"), error.what());
    }
}

void AssemblyWorkspaceWindow::set_working_directory() {
    const QString selected = choose_directory(
        this, application_settings_.text("file.set_working_directory",
            tr("Nastavit pracovní adresář")),
        QString::fromStdString(working_directory_.string()),
        application_settings_.translations);
    if (selected.isEmpty()) return;
    const std::filesystem::path target = selected.toStdString();
    if (!std::filesystem::is_directory(target)) {
        QMessageBox::warning(this, tr("Neplatný adresář"),
            tr("Vybraná cesta není existující adresář."));
        return;
    }
    working_directory_ = target;
    state_->setText(tr("Pracovní adresář: %1").arg(selected));
}

std::optional<std::filesystem::path>
AssemblyWorkspaceWindow::active_document_file_path() const {
    const std::string id = workspace_.active_document_id();
    if (id.empty()) return std::nullopt;
    if (const auto* part = workspace_.open_part(id);
        part != nullptr && !part->path.empty()) return part->path;
    if (const auto* assembly = workspace_.open_assembly(id);
        assembly != nullptr && !assembly->path.empty()) return assembly->path;
    if (const auto* drawing = workspace_.open_drawing(id);
        drawing != nullptr && !drawing->path.empty()) return drawing->path;
    return std::nullopt;
}

std::vector<std::filesystem::path> AssemblyWorkspaceWindow::document_archive_paths(
    const std::filesystem::path& file_path) {
    std::vector<std::pair<int, std::filesystem::path>> archives;
    const auto target = std::filesystem::absolute(file_path).lexically_normal();
    const auto parent = target.parent_path();
    if (std::filesystem::is_directory(parent)) {
        const std::string prefix = target.filename().string() + ".";
        for (const auto& entry : std::filesystem::directory_iterator(parent)) {
            if (!entry.is_regular_file()) continue;
            const std::string name = entry.path().filename().string();
            if (name.rfind(prefix, 0) != 0) continue;
            const std::string suffix = name.substr(prefix.size());
            if (suffix.empty() || !std::all_of(suffix.begin(), suffix.end(),
                    [](unsigned char ch) { return std::isdigit(ch) != 0; })) continue;
            archives.emplace_back(std::stoi(suffix), entry.path());
        }
    }
    std::sort(archives.begin(), archives.end(),
        [](const auto& lhs, const auto& rhs) { return lhs.first < rhs.first; });
    std::vector<std::filesystem::path> result;
    result.reserve(archives.size());
    for (auto& [version, path] : archives) result.push_back(std::move(path));
    return result;
}

std::map<std::filesystem::path, std::vector<std::filesystem::path>>
AssemblyWorkspaceWindow::working_directory_archive_groups(
    const std::filesystem::path& directory) {
    std::map<std::filesystem::path, std::vector<std::pair<int, std::filesystem::path>>>
        groups;
    if (!std::filesystem::is_directory(directory)) return {};
    static const std::array<std::string, 5> document_extensions = {
        ".prtz", ".asmz", ".drwz", ".frmz", ".tblz"};
    for (const auto& entry : std::filesystem::directory_iterator(directory)) {
        if (!entry.is_regular_file()) continue;
        const auto& path = entry.path();
        const std::string numeric_suffix = path.extension().string().empty() ? "" :
            path.extension().string().substr(1);
        if (numeric_suffix.empty() || !std::all_of(numeric_suffix.begin(),
                numeric_suffix.end(),
                [](unsigned char ch) { return std::isdigit(ch) != 0; })) continue;
        const auto document_path = path.stem().empty() ? path :
            path.parent_path() / path.stem();
        const auto document_extension = document_path.extension().string();
        std::string lowered = document_extension;
        std::transform(lowered.begin(), lowered.end(), lowered.begin(),
            [](unsigned char ch) { return std::tolower(ch); });
        if (std::find(document_extensions.begin(), document_extensions.end(), lowered) ==
                document_extensions.end()) continue;
        groups[document_path].emplace_back(std::stoi(numeric_suffix), path);
    }
    std::map<std::filesystem::path, std::vector<std::filesystem::path>> result;
    for (auto& [document_path, archives] : groups) {
        std::sort(archives.begin(), archives.end(),
            [](const auto& lhs, const auto& rhs) { return lhs.first < rhs.first; });
        std::vector<std::filesystem::path> paths;
        paths.reserve(archives.size());
        for (auto& [version, path] : archives) paths.push_back(std::move(path));
        result.emplace(document_path, std::move(paths));
    }
    return result;
}

void AssemblyWorkspaceWindow::refresh_delete_file_actions() {
    const auto target = active_document_file_path();
    const bool has_saved_document = target.has_value() && std::filesystem::is_regular_file(*target);
    const auto archives = has_saved_document
        ? document_archive_paths(*target) : std::vector<std::filesystem::path>{};
    rename_document_action_->setEnabled(has_saved_document);
    delete_old_versions_action_->setEnabled(!archives.empty());
    delete_old_versions_keep_latest_action_->setEnabled(archives.size() > 1);
    delete_current_file_action_->setEnabled(has_saved_document);
    delete_all_versions_action_->setEnabled(has_saved_document);
    const bool has_working_directory = std::filesystem::is_directory(working_directory_);
    delete_working_directory_old_versions_action_->setEnabled(has_working_directory);
    delete_working_directory_keep_latest_action_->setEnabled(has_working_directory);
}

namespace {
QString format_file_size(std::uintmax_t size) {
    double value = static_cast<double>(size);
    for (const char* unit : {"B", "kB", "MB"}) {
        if (value < 1000.0) {
            return QStringLiteral("%1 %2").arg(
                QString::number(value, 'f', std::string(unit) == "B" ? 0 : 1), unit);
        }
        value /= 1000.0;
    }
    return QStringLiteral("%1 GB").arg(QString::number(value, 'f', 1));
}

std::uintmax_t paths_total_size(const std::vector<std::filesystem::path>& paths) {
    std::uintmax_t total = 0;
    for (const auto& path : paths) {
        std::error_code error;
        const auto size = std::filesystem::file_size(path, error);
        if (!error) total += size;
    }
    return total;
}
}  // namespace

void AssemblyWorkspaceWindow::rename_document_file() {
    const auto target = active_document_file_path();
    if (!target.has_value()) return;
    if (rename_document_dialog_ != nullptr) {
        rename_document_dialog_->raise();
        rename_document_dialog_->activateWindow();
        return;
    }
    const std::filesystem::path old_path = std::filesystem::absolute(*target).lexically_normal();
    auto* dialog = new RenameDocumentDialog(
        QString::fromStdString(old_path.filename().string()),
        [this, old_path](QString new_name) -> QString {
            std::filesystem::path candidate(new_name.toStdString());
            candidate = candidate.filename();
            if (candidate.empty())
                return tr("Zadejte platný název souboru.");
            std::string requested_extension = candidate.extension().string();
            std::string current_extension = old_path.extension().string();
            std::transform(requested_extension.begin(), requested_extension.end(),
                requested_extension.begin(),
                [](unsigned char ch) { return std::tolower(ch); });
            std::string current_extension_lower = current_extension;
            std::transform(current_extension_lower.begin(), current_extension_lower.end(),
                current_extension_lower.begin(),
                [](unsigned char ch) { return std::tolower(ch); });
            if (requested_extension != current_extension_lower) {
                candidate = candidate.stem();
                candidate += current_extension;
            }
            const std::filesystem::path new_path = old_path.parent_path() / candidate;
            if (new_path == old_path) return QString();
            if (std::filesystem::exists(new_path)) {
                return tr("Soubor %1 již existuje.")
                    .arg(QString::fromStdString(new_path.filename().string()));
            }
            const bool is_source_document =
                old_path.extension() == ".prtz" || old_path.extension() == ".asmz";
            const std::filesystem::path old_drawing_path = is_source_document
                ? std::filesystem::path(old_path).replace_extension(".drwz")
                : std::filesystem::path{};
            const std::filesystem::path new_drawing_path = is_source_document
                ? std::filesystem::path(new_path).replace_extension(".drwz")
                : std::filesystem::path{};
            const bool rename_companion_drawing = is_source_document &&
                std::filesystem::is_regular_file(old_drawing_path);
            if (rename_companion_drawing && std::filesystem::exists(new_drawing_path)) {
                return tr("Soubor %1 již existuje.")
                    .arg(QString::fromStdString(new_drawing_path.filename().string()));
            }

            // Rewrite in-memory Assembly component references and Drawing
            // source references that point at the file being renamed, both
            // for currently open documents and for documents saved on disk
            // in the same directory or the working directory.
            std::unordered_set<std::string> updated_ids;
            const auto rewrite_open_assembly_paths = [&](zima::workspace::AssemblyState& state) {
                bool changed = false;
                auto document = state.session.document();
                for (auto& component : document.components) {
                    if (std::filesystem::absolute(component.source_path).lexically_normal() ==
                            old_path) {
                        component.source_path = new_path;
                        changed = true;
                    }
                }
                if (changed) state.session.replace(std::move(document));
                return changed;
            };
            for (auto& state : workspace_.documents()) {
                if (auto* assembly = std::get_if<zima::workspace::AssemblyState>(&state)) {
                    if (rewrite_open_assembly_paths(*assembly)) {
                        updated_ids.insert(assembly->session.document().document_id);
                    }
                } else if (auto* drawing = std::get_if<zima::workspace::DrawingState>(&state)) {
                    auto updated=drawing->document();
                    bool changed=false;
                    if (updated.source_document_id ==
                            workspace_.active_document_id() ||
                        (!updated.source_path.empty() &&
                         std::filesystem::absolute(updated.source_path)
                                 .lexically_normal() == old_path)) {
                        updated.source_path = new_path; changed=true;
                        updated.source_name =
                            new_path.stem().string();
                    }
                    for (auto& sheet : updated.sheets) {
                        for (auto& view : sheet.views) {
                            if (!view.source_path.empty() &&
                                std::filesystem::absolute(view.source_path).lexically_normal() ==
                                    old_path) {
                                view.source_path = new_path; changed=true;
                                updated_ids.insert(updated.document_id);
                            }
                        }
                    }
                    if(changed) {
                        updated_ids.insert(updated.document_id);
                        drawing->commit(std::move(updated));
                    }
                }
            }

            // Also rewrite Assembly/Drawing documents saved on disk but not
            // currently open, matching Python's _rename_document_file_to
            // (which loads every candidate document in the file's directory
            // and the working directory, rewrites any reference to the
            // renamed file, and re-saves it). Only Assembly (.asmz) and
            // Drawing (.drwz) documents can hold such references; Part
            // (.prtz) documents cannot reference other documents.
            std::unordered_set<std::string> open_document_paths;
            for (auto& state : workspace_.documents()) {
                if (auto* part = std::get_if<zima::workspace::PartState>(&state)) {
                    open_document_paths.insert(
                        std::filesystem::absolute(part->path).lexically_normal().string());
                } else if (auto* assembly = std::get_if<zima::workspace::AssemblyState>(&state)) {
                    open_document_paths.insert(
                        std::filesystem::absolute(assembly->path).lexically_normal().string());
                } else if (auto* drawing = std::get_if<zima::workspace::DrawingState>(&state)) {
                    open_document_paths.insert(
                        std::filesystem::absolute(drawing->path).lexically_normal().string());
                }
            }
            std::unordered_set<std::string> scanned_paths;
            const auto scan_directory_for_references = [&](const std::filesystem::path& directory) {
                if (!std::filesystem::is_directory(directory)) return;
                for (const auto& entry : std::filesystem::directory_iterator(directory)) {
                    if (!entry.is_regular_file()) continue;
                    const auto candidate =
                        std::filesystem::absolute(entry.path()).lexically_normal();
                    const std::string extension_lower = [&] {
                        std::string ext = candidate.extension().string();
                        std::transform(ext.begin(), ext.end(), ext.begin(),
                            [](unsigned char ch) { return std::tolower(ch); });
                        return ext;
                    }();
                    if (extension_lower != ".asmz" && extension_lower != ".drwz") continue;
                    const std::string key = candidate.string();
                    if (open_document_paths.count(key) != 0) continue;
                    if (!scanned_paths.insert(key).second) continue;
                    if (extension_lower == ".asmz") {
                        zima::assembly::AssemblyDocument document;
                        try {
                            document = zima::assembly::AssemblyDocument::load(candidate);
                        } catch (const std::exception&) {
                            continue;
                        }
                        bool changed = false;
                        for (auto& component : document.components) {
                            if (std::filesystem::absolute(component.source_path)
                                    .lexically_normal() == old_path) {
                                component.source_path = new_path;
                                changed = true;
                            }
                        }
                        if (changed) {
                            try {
                                document.save(candidate);
                            } catch (const std::exception&) {
                            }
                        }
                    } else {
                        zima::drawing::DrawingDocument document;
                        try {
                            document = zima::drawing::DrawingDocument::load(candidate);
                        } catch (const std::exception&) {
                            continue;
                        }
                        bool changed = false;
                        for (auto& sheet : document.sheets) {
                            for (auto& view : sheet.views) {
                                if (!view.source_path.empty() &&
                                    std::filesystem::absolute(view.source_path)
                                            .lexically_normal() == old_path) {
                                    view.source_path = new_path;
                                    changed = true;
                                }
                            }
                        }
                        if (changed) {
                            try {
                                document.save(candidate);
                            } catch (const std::exception&) {
                            }
                        }
                    }
                }
            };
            scan_directory_for_references(old_path.parent_path());
            if (!working_directory_.empty() &&
                std::filesystem::is_directory(working_directory_)) {
                scan_directory_for_references(working_directory_);
                for (const auto& entry :
                        std::filesystem::recursive_directory_iterator(working_directory_)) {
                    if (entry.is_directory()) {
                        scan_directory_for_references(entry.path());
                    }
                }
            }

            try {
                std::filesystem::rename(old_path, new_path);
                if (rename_companion_drawing) {
                    std::filesystem::rename(old_drawing_path, new_drawing_path);
                }
            } catch (const std::exception& error) {
                return QString::fromStdString(error.what());
            }

            for (auto& state : workspace_.documents()) {
                if (auto* part = std::get_if<zima::workspace::PartState>(&state)) {
                    if (std::filesystem::absolute(part->path).lexically_normal() == old_path) {
                        part->path = new_path;
                        auto renamed = part->session.document();
                        renamed.name = new_path.stem().string();
                        part->session.replace(std::move(renamed));
                    }
                } else if (auto* assembly = std::get_if<zima::workspace::AssemblyState>(&state)) {
                    if (std::filesystem::absolute(assembly->path).lexically_normal() == old_path) {
                        assembly->path = new_path;
                        auto renamed = assembly->session.document();
                        renamed.name = new_path.stem().string();
                        assembly->session.replace(std::move(renamed));
                    }
                    if (rename_companion_drawing) continue;
                } else if (auto* drawing = std::get_if<zima::workspace::DrawingState>(&state)) {
                    if (std::filesystem::absolute(drawing->path).lexically_normal() == old_path)
                        drawing->path = new_path;
                    else if (rename_companion_drawing &&
                             std::filesystem::absolute(drawing->path).lexically_normal() ==
                                 std::filesystem::absolute(old_drawing_path).lexically_normal())
                        drawing->path = new_drawing_path;
                }
            }
            refresh_tabs();
            refresh_scene();
            state_->setText(tr("Soubor přejmenován na %1")
                .arg(QString::fromStdString(new_path.filename().string())));
            return QString();
        }, application_settings_, this);
    rename_document_dialog_ = dialog;
    connect(dialog, &QObject::destroyed, this, [this, dialog] {
        if (rename_document_dialog_ == dialog) rename_document_dialog_ = nullptr;
    });
    dialog->show();
}

void AssemblyWorkspaceWindow::delete_current_document_file() {
    const auto target = active_document_file_path();
    if (!target.has_value() || !std::filesystem::is_regular_file(*target)) return;
    const auto answer = QMessageBox::warning(
        this, tr("Odstranit aktuální soubor"),
        tr("Opravdu chcete odstranit soubor %1?")
            .arg(QString::fromStdString(target->filename().string())),
        QMessageBox::Yes | QMessageBox::No, QMessageBox::No);
    if (answer != QMessageBox::Yes) return;
    std::error_code error;
    std::filesystem::remove(*target, error);
    if (error) {
        QMessageBox::critical(this, tr("Odstranění selhalo"),
            QString::fromStdString(error.message()));
        return;
    }
    const QString deleted_name = QString::fromStdString(target->filename().string());
    close_document(-1);
    state_->setText(tr("Soubor %1 odstraněn.").arg(deleted_name));
}

void AssemblyWorkspaceWindow::delete_old_file_versions_keep_latest() {
    const auto target = active_document_file_path();
    if (!target.has_value()) return;
    const auto archives = document_archive_paths(*target);
    if (archives.size() < 2) {
        QMessageBox::information(this, tr("Staré verze kromě nejnovější"),
            tr("Žádné starší verze souboru %1 nebyly nalezeny.")
                .arg(QString::fromStdString(target->filename().string())));
        return;
    }
    const std::vector<std::filesystem::path> to_delete(
        archives.begin(), archives.end() - 1);
    const auto answer = QMessageBox::question(
        this, tr("Staré verze kromě nejnovější"),
        tr("Odstranit %1 starších verzí souboru %2?")
            .arg(to_delete.size())
            .arg(QString::fromStdString(target->filename().string())),
        QMessageBox::Yes | QMessageBox::No, QMessageBox::No);
    if (answer != QMessageBox::Yes) return;
    for (const auto& path : to_delete) {
        std::error_code error;
        std::filesystem::remove(path, error);
        if (error) {
            QMessageBox::critical(this, tr("Odstranění selhalo"),
                QString::fromStdString(error.message()));
            return;
        }
    }
    refresh_delete_file_actions();
    state_->setText(tr("Odstraněno %1 starších verzí.").arg(to_delete.size()));
}

void AssemblyWorkspaceWindow::delete_old_file_versions() {
    const auto target = active_document_file_path();
    if (!target.has_value()) return;
    const auto archives = document_archive_paths(*target);
    if (archives.empty()) {
        QMessageBox::information(this, tr("Staré verze"),
            tr("Žádné starší verze souboru %1 nebyly nalezeny.")
                .arg(QString::fromStdString(target->filename().string())));
        return;
    }
    const auto answer = QMessageBox::question(
        this, tr("Staré verze"),
        tr("Odstranit %1 starších verzí souboru %2?")
            .arg(archives.size())
            .arg(QString::fromStdString(target->filename().string())),
        QMessageBox::Yes | QMessageBox::No, QMessageBox::No);
    if (answer != QMessageBox::Yes) return;
    for (const auto& path : archives) {
        std::error_code error;
        std::filesystem::remove(path, error);
        if (error) {
            QMessageBox::critical(this, tr("Odstranění selhalo"),
                QString::fromStdString(error.message()));
            return;
        }
    }
    refresh_delete_file_actions();
    state_->setText(tr("Odstraněno %1 starších verzí.").arg(archives.size()));
}

void AssemblyWorkspaceWindow::delete_all_file_versions() {
    const auto target = active_document_file_path();
    if (!target.has_value()) return;
    auto archives = document_archive_paths(*target);
    std::vector<std::filesystem::path> existing_paths;
    for (auto& path : archives) {
        if (std::filesystem::is_regular_file(path)) existing_paths.push_back(std::move(path));
    }
    if (std::filesystem::is_regular_file(*target)) existing_paths.push_back(*target);
    const auto answer = QMessageBox::warning(
        this, tr("Aktuální soubor a všechny verze"),
        tr("Odstranit soubor %1 a všech %2 souvisejících souborů?")
            .arg(QString::fromStdString(target->filename().string()))
            .arg(existing_paths.size()),
        QMessageBox::Yes | QMessageBox::No, QMessageBox::No);
    if (answer != QMessageBox::Yes) return;
    for (const auto& path : existing_paths) {
        std::error_code error;
        std::filesystem::remove(path, error);
        if (error) {
            QMessageBox::critical(this, tr("Odstranění selhalo"),
                QString::fromStdString(error.message()));
            return;
        }
    }
    const QString deleted_name = QString::fromStdString(target->filename().string());
    close_document(-1);
    state_->setText(tr("Soubor %1 a všechny verze odstraněny.").arg(deleted_name));
}

void AssemblyWorkspaceWindow::delete_working_directory_old_versions() {
    const auto directory = std::filesystem::absolute(working_directory_).lexically_normal();
    const auto groups = working_directory_archive_groups(directory);
    std::vector<std::filesystem::path> paths;
    for (const auto& [document_path, archives] : groups) {
        for (const auto& path : archives) paths.push_back(path);
    }
    if (paths.empty()) {
        QMessageBox::information(this, tr("Pracovní adresář"),
            tr("V pracovním adresáři %1 nebyly nalezeny žádné starší verze.")
                .arg(QString::fromStdString(directory.string())));
        return;
    }
    const QString size_text = format_file_size(paths_total_size(paths));
    const auto answer = QMessageBox::warning(
        this, tr("Pracovní adresář"),
        tr("Odstranit %1 souborů starších verzí (%2) z pracovního adresáře %3?")
            .arg(paths.size()).arg(size_text)
            .arg(QString::fromStdString(directory.string())),
        QMessageBox::Yes | QMessageBox::No, QMessageBox::No);
    if (answer != QMessageBox::Yes) return;
    for (const auto& path : paths) {
        std::error_code error;
        std::filesystem::remove(path, error);
        if (error) {
            QMessageBox::critical(this, tr("Odstranění selhalo"),
                QString::fromStdString(error.message()));
            return;
        }
    }
    refresh_delete_file_actions();
    state_->setText(tr("Odstraněno %1 souborů starších verzí.").arg(paths.size()));
}

void AssemblyWorkspaceWindow::delete_working_directory_old_versions_keep_latest() {
    const auto directory = std::filesystem::absolute(working_directory_).lexically_normal();
    const auto groups = working_directory_archive_groups(directory);
    std::vector<std::filesystem::path> paths;
    for (const auto& [document_path, archives] : groups) {
        if (archives.size() < 2) continue;
        paths.insert(paths.end(), archives.begin(), archives.end() - 1);
    }
    if (paths.empty()) {
        QMessageBox::information(this, tr("Pracovní adresář"),
            tr("V pracovním adresáři %1 nebyly nalezeny žádné starší verze.")
                .arg(QString::fromStdString(directory.string())));
        return;
    }
    const QString size_text = format_file_size(paths_total_size(paths));
    const auto answer = QMessageBox::warning(
        this, tr("Pracovní adresář"),
        tr("Odstranit %1 souborů starších verzí (%2) z pracovního adresáře %3? "
           "Nejnovější verze každého dokumentu zůstane zachována.")
            .arg(paths.size()).arg(size_text)
            .arg(QString::fromStdString(directory.string())),
        QMessageBox::Yes | QMessageBox::No, QMessageBox::No);
    if (answer != QMessageBox::Yes) return;
    for (const auto& path : paths) {
        std::error_code error;
        std::filesystem::remove(path, error);
        if (error) {
            QMessageBox::critical(this, tr("Odstranění selhalo"),
                QString::fromStdString(error.message()));
            return;
        }
    }
    refresh_delete_file_actions();
    state_->setText(tr("Odstraněno %1 souborů starších verzí.").arg(paths.size()));
}

void AssemblyWorkspaceWindow::open_new_window() {
    // Isolate application-wide translations/settings as well as documents.
    QProcess process;
    process.setProgram(QCoreApplication::applicationFilePath());
    process.setArguments({QStringLiteral("--working-directory"),
        QString::fromStdString(working_directory_.string())});
    process.setWorkingDirectory(QString::fromStdString(working_directory_.string()));
    if (!process.startDetached())
        state_->setText(tr("Novou instanci ZIMA-CAD se nepodařilo spustit."));
}

void AssemblyWorkspaceWindow::show_global_settings() {
    if (global_settings_dialog_ != nullptr) {
        global_settings_dialog_->raise();
        global_settings_dialog_->activateWindow();
        return;
    }
    auto* dialog = new GlobalSettingsDialog(application_settings_, this);
    global_settings_dialog_ = dialog;
    connect(dialog, &QDialog::accepted, this, [this] {
        application_settings_ = ApplicationSettings::load(
            QFileInfo(application_settings_.config_path).absolutePath());
        drawing_workspace_->set_formats_directory(application_settings_.resolved_paths.value("Formats"));
        apply_application_translations(*qApp, application_settings_);
        apply_application_font(*qApp, application_settings_);
        const QString configured =
            application_settings_.resolved_paths.value("WorkingDirectory");
        if (!configured.trimmed().isEmpty() && QFileInfo(configured).isDir()) {
            working_directory_ = QFileInfo(configured).absoluteFilePath().toStdString();
            refresh_delete_file_actions();
        }
    });
    connect(dialog, &QObject::destroyed, this, [this] {
        global_settings_dialog_ = nullptr;
    });
    dialog->setAttribute(Qt::WA_DeleteOnClose);
    dialog->show();
}

void AssemblyWorkspaceWindow::show_about() {
    if (properties_dialog_ != nullptr) {
        properties_dialog_->raise();
        return;
    }
    auto* dialog = new AboutSubWindow(this);
    properties_dialog_ = dialog;
    connect(dialog, &QObject::destroyed, this, [this] {
        properties_dialog_ = nullptr;
    });
    dialog->show();
}

} // namespace zima::app
