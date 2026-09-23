#include <zima/workspace/family_operations.hpp>
#include <zima/workspace/metadata_operations.hpp>
#include "workspace_internal.hpp"
#include "updateservice.h"
#include <zima_build_info.hpp>
#include <zima/workspace/document_operations.hpp>
#include <zima/workspace/archive_operations.hpp>
#include <zima/workspace/file_removal_operations.hpp>
#include <zima/workspace/file_rename_operations.hpp>
#include <zima/command_host/host.hpp>
#include <zima/document/file_path.hpp>
#include <zima/document/versioned_file.hpp>
#include <QSvgRenderer>
#include <QPainter>

namespace zima::app {
using namespace workspace_detail;

namespace {


class RenameDocumentDialog final : public zima::ui::PropertiesSubWindow {
public:
    RenameDocumentDialog(const QString& initial_name,
                         std::function<QString(QString)> accepted,
                         const ApplicationSettings& settings, QMainWindow* parent,
                         DocumentNaming naming = {})
        : PropertiesSubWindow(settings.text("dialog.rename.title", tr("Přejmenovat")),
                              parent),
          accepted_(std::move(accepted)) {
        setObjectName("renameDocumentDialog");
        set_centered_on_show();
        setMinimumWidth(360);
        auto* form = new QFormLayout;
        name_ = new QLineEdit(initial_name, this);
        name_->setObjectName("renameDocumentName");
        connect(name_, &QLineEdit::textEdited, this, [this, naming](const QString& text) {
            const auto convert = [&naming](QString input) {
                // Preserve an unfinished trailing space, as in New Document.
                input = naming.normalize(input + QLatin1Char('|'));
                input.chop(1);
                return naming.normalize(input); // Keep native extensions lowercase.
            };
            const auto cursor = convert(text.left(name_->cursorPosition())).size();
            const auto converted = convert(text);
            if (converted != text) {
                name_->setText(converted);
                name_->setCursorPosition(static_cast<int>(cursor));
            }
        });
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
        setObjectName("aboutDialog");
        auto* artwork = new QLabel(this);
        artwork->setObjectName("aboutCompanyLogo");
        artwork->setAlignment(Qt::AlignCenter);
        QSvgRenderer logo(QStringLiteral(":/zima/branding/ZIMA-Engineering.svg"));
        if (logo.isValid()) {
            const auto size=logo.defaultSize().scaled(420,100,Qt::KeepAspectRatio);
            QPixmap pixmap(size*devicePixelRatioF());
            pixmap.setDevicePixelRatio(devicePixelRatioF());
            pixmap.fill(Qt::transparent);
            QPainter painter(&pixmap);
            logo.render(&painter,QRectF(QPointF(0,0),QSizeF(size)));
            painter.end();
            artwork->setPixmap(pixmap);
            content_layout()->addWidget(artwork);
        }
        auto* description = new QLabel(
            QObject::tr(
                "ZIMA-CAD\nOpen-source parametrický 3D CAD založený na geometrickém "
                "jádře OCCT a napsaný v jazyce C++.\n\n"
                "Vydání: ZIMA-CAD-%1")
                .arg(QString::fromUtf8(distribution::version.data(), static_cast<qsizetype>(distribution::version.size()))),
            this);
        description->setObjectName(QStringLiteral("aboutDescription"));
        description->setAlignment(Qt::AlignCenter);
        description->setWordWrap(true);
        content_layout()->addWidget(description);
        auto* author = new QLabel(QObject::tr("Autor koncepce a vývoje: %1\n%2")
            .arg(QString::fromUtf8("Ing. Vladimír Zima"), QStringLiteral("ZIMA-Engineering")), this);
        author->setObjectName("aboutAuthor");
        author->setAlignment(Qt::AlignCenter);
        author->setWordWrap(true);
        content_layout()->addWidget(author);
        auto* contact = new QLabel(QStringLiteral(
            "<a href=\"mailto:kontakt@zima-engineering.cz\">kontakt@zima-engineering.cz</a><br>"
            "<a href=\"https://www.zima-engineering.cz\">www.zima-engineering.cz</a>"), this);
        contact->setObjectName("aboutContact");
        contact->setAlignment(Qt::AlignCenter);
        contact->setOpenExternalLinks(true);
        contact->setTextInteractionFlags(Qt::TextBrowserInteraction);
        content_layout()->addWidget(contact);
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
    synchronize_instance_files();
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
    QString path = QString::fromStdString(zima::document::path_to_utf8(assembly->path));
    if (path.isEmpty()) path = save_file(
        this, application_settings_.text("file.save_assembly", tr("Uložit sestavu ZIMA-CAD")),
        QString::fromStdString(zima::document::path_to_utf8(working_directory_ / "assembly.asmz")),
        application_settings_.text("file.filter.assembly",
            tr("Sestava ZIMA-CAD (*.asmz)")), "asmz",
        application_settings_.translations);
    if (path.isEmpty()) return;
    if (!path.endsWith(".asmz", Qt::CaseInsensitive)) {
        auto normalized = std::filesystem::u8path(path.toStdString());
        normalized.replace_extension(".asmz");
        path = QString::fromStdString(zima::document::path_to_utf8(normalized));
    }
    const auto id = assembly->session.document().document_id;
    std::optional<workspace::DocumentSave> pending;
    try {pending=workspace::prepare_document_save_if_needed(
        workspace_,id,std::filesystem::u8path(path.toStdString()));}
    catch(const std::exception& error) {
        report_operation_error(application_settings_.text("message.save_failed",
            tr("Uložení se nezdařilo")),error.what());return;
    }
    if(!pending)return;
    begin_status_operation(tr("Ukládám sestavu %1…").arg(
        QFileInfo(path).fileName()));
    try {
        update_status_operation(
            tr("Zapisuji komponenty, vazby a uloženou geometrii…"), -1, 0);
        auto job = std::move(*pending);
        const auto saved = run_background_task([job = std::move(job)] { return job.write(); });
        if (!workspace::complete_document_save(workspace_, saved))
            throw std::runtime_error(tr("Uložený dokument byl mezitím zavřen nebo změnil cestu.").toStdString());
        change_working_directory(workspace_.open_assembly(id)->path.parent_path());
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
        QString path=QString::fromStdString(zima::document::path_to_utf8(drawing->path));
        if(path.isEmpty()) path=save_file(
            this, application_settings_.text("file.save_drawing", tr("Uložit výkres")),
            QString::fromStdString(zima::document::path_to_utf8(working_directory_ / "drawing.drwz")),
            application_settings_.text("file.filter.drawing",
                tr("Výkres ZIMA-CAD (*.drwz)")), "drwz",
            application_settings_.translations);
        if(path.isEmpty()) return;
        if(!path.endsWith(".drwz", Qt::CaseInsensitive)) {
            auto normalized = std::filesystem::u8path(path.toStdString());
            normalized.replace_extension(".drwz");
            path = QString::fromStdString(zima::document::path_to_utf8(normalized));
        }
        const auto id = drawing->document().document_id;
        std::optional<workspace::DocumentSave> pending;
        try {pending=workspace::prepare_document_save_if_needed(
            workspace_,id,std::filesystem::u8path(path.toStdString()));}
        catch(const std::exception& error) {
            report_operation_error(application_settings_.text("message.save_failed",
                tr("Uložení se nezdařilo")),error.what());return;
        }
        if(!pending)return;
        begin_status_operation(tr("Ukládám výkres %1…").arg(
            QFileInfo(path).fileName()));
        try {
            update_status_operation(
                tr("Zapisuji listy, pohledy a popisové pole…"), -1, 0);
            auto job = std::move(*pending);
            const auto saved = run_background_task([job = std::move(job)] { return job.write(); });
            if (!workspace::complete_document_save(workspace_, saved))
                throw std::runtime_error(tr("Uložený dokument byl mezitím zavřen nebo změnil cestu.").toStdString());
            change_working_directory(workspace_.open_drawing(id)->path.parent_path());
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
    QString path = QString::fromStdString(zima::document::path_to_utf8(part->path));
    if (path.isEmpty()) path = save_file(
        this, application_settings_.text("file.save_part", tr("Uložit díl ZIMA-CAD")),
        QString::fromStdString(zima::document::path_to_utf8(working_directory_ / "part.prtz")),
        application_settings_.text("file.filter.part",
            tr("Díl ZIMA-CAD (*.prtz)")), "prtz",
        application_settings_.translations);
    if (path.isEmpty()) return;
    if (!path.endsWith(".prtz", Qt::CaseInsensitive)) {
        auto normalized = std::filesystem::u8path(path.toStdString());
        normalized.replace_extension(".prtz");
        path = QString::fromStdString(zima::document::path_to_utf8(normalized));
    }
    const auto id = part->session.document().document_id;
    std::optional<workspace::DocumentSave> pending;
    try {pending=workspace::prepare_document_save_if_needed(
        workspace_,id,std::filesystem::u8path(path.toStdString()));}
    catch(const std::exception& error) {
        report_operation_error(application_settings_.text("message.save_failed",
            tr("Uložení se nezdařilo")),error.what());return;
    }
    if(!pending)return;
    begin_status_operation(tr("Ukládám Part %1…").arg(
        QFileInfo(path).fileName()));
    try {
        update_status_operation(
            tr("Připravuji neměnný snímek dokumentu…"));
        auto job = std::move(*pending);
        update_status_operation(
            tr("Zapisuji parametry, B-Rep a data pro View…"), -1, 0);
        const auto saved = run_background_task([job = std::move(job)] { return job.write(); });
        if (!workspace::complete_document_save(workspace_, saved))
            throw std::runtime_error(tr("Uložený dokument byl mezitím zavřen nebo změnil cestu.").toStdString());
        change_working_directory(workspace_.open_part(id)->path.parent_path());
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
    const QString initial = QString::fromStdString(zima::document::path_to_utf8(working_directory_ /
        std::filesystem::u8path(fallback_name.toStdString())));
    QString selected = save_file(this, caption, initial, filter, suffix,
                                 application_settings_.translations);
    if (selected.isEmpty()) return;
    const QString dotted_suffix = QStringLiteral(".") + suffix;
    std::filesystem::path target = std::filesystem::u8path(selected.toStdString());
    if (QString::fromStdString(zima::document::path_to_utf8(target.extension())).compare(
            dotted_suffix, Qt::CaseInsensitive) != 0) {
        target.replace_extension(dotted_suffix.toStdString());
        selected = QString::fromStdString(zima::document::path_to_utf8(target));
    }
    target.replace_filename(std::filesystem::u8path(application_settings_.document_naming(
        zima::document::path_to_utf8(target.filename()))));
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
        if (!target.parent_path().empty()) change_working_directory(target.parent_path());
        finish_status_operation(tr("Uložena kopie: %1 (%2 souborů)").arg(
            QString::fromStdString(zima::document::path_to_utf8(target.filename()))).arg(files.size()));
    } catch (const std::exception& error) {
        finish_status_operation(tr("Vytvoření kopie selhalo"), false);
        QMessageBox::critical(this, tr("Uložení kopie se nezdařilo"), error.what());
    }
}

void AssemblyWorkspaceWindow::change_working_directory(const std::filesystem::path& path,bool required) {
    try {instance_.set_directory(QString::fromStdString(document::path_to_utf8(path)));}
    catch(const std::exception&) {if(required)throw;return;}
    working_directory_=path;
    setProperty("instanceWorkingDirectory",QString::fromStdString(document::path_to_utf8(path)));
}

void AssemblyWorkspaceWindow::set_working_directory() {
    const QString selected = choose_directory(
        this, application_settings_.text("file.set_working_directory",
            tr("Nastavit pracovní adresář")),
        QString::fromStdString(zima::document::path_to_utf8(working_directory_)),
        application_settings_.translations);
    if (selected.isEmpty()) return;
    const std::filesystem::path target = std::filesystem::u8path(selected.toStdString());
    if (!std::filesystem::is_directory(target)) {
        QMessageBox::warning(this, tr("Neplatný adresář"),
            tr("Vybraná cesta není existující adresář."));
        return;
    }
    try {change_working_directory(target,true);}
    catch(const std::exception& error) {report_operation_error(tr("Pracovní adresář je obsazený"),QObject::tr(error.what()));return;}
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
    // Menu availability only needs names, not sizes/timestamps for a delete snapshot.
    return document::archive_paths(std::filesystem::absolute(file_path).lexically_normal());
}

void AssemblyWorkspaceWindow::refresh_delete_file_actions() {
    const auto target = active_document_file_path();
    const bool has_saved_document = target.has_value() && std::filesystem::is_regular_file(*target);
    const auto archives = has_saved_document
        ? document_archive_paths(*target) : std::vector<std::filesystem::path>{};
    const auto active=workspace_.active_document_id();
    rename_document_action_->setEnabled(has_saved_document||(!active.empty()&&workspace::family_owner(workspace_,active)!=active));
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

}  // namespace

bool AssemblyWorkspaceWindow::native_file_operation_ready(QDialog* own_dialog) {
    QDialog* other = nullptr;
    for (auto* dialog : findChildren<QDialog*>())
        if (dialog != own_dialog && dialog->isVisible()) { other = dialog; break; }
    const auto* modal = QApplication::activeModalWidget();
    const bool editing = other || (modal && modal != own_dialog) || properties_dialog_ ||
        !active_sketch_id_.empty() || template_sketch() ||
        (tree_ && tree_->property("commandSelectionActive").toBool());
    if (!editing) return true;
    state_->setText(tr("Nejprve dokončete nebo zrušte otevřené vlastnosti."));
    if (other) other->raise();
    else if (properties_dialog_) properties_dialog_->raise();
    return false;
}

void AssemblyWorkspaceWindow::add_object_rename_action(QMenu& menu,const std::string& document,const std::string& kind,const std::string& object) {
    if(properties_dialog_||!active_sketch_id_.empty()||document!=workspace_.active_document_id())return;
    const auto initial=workspace::tree_object_name(workspace_,document,kind,object);
    if(!initial)return;
    auto* action=menu.addAction(tr("Přejmenovat…"));action->setObjectName("renameTreeItemAction");
    connect(action,&QAction::triggered,this,[this,document,kind,object,initial] {
        auto* dialog=new RenameDocumentDialog(QString::fromStdString(*initial),[this,document,kind,object](QString name) {
            try {static_cast<void>(workspace::rename_tree_object(workspace_,document,kind,object,name.toStdString()));return QString{};}
            catch(const std::exception& error){return tr(error.what());}
        },application_settings_,this);
        dialog->setObjectName("renameTreeItemDialog");
        dialog->findChild<QLineEdit*>("renameDocumentName")->setObjectName("renameTreeItemName");
        properties_dialog_=dialog;
        connect(dialog,&QDialog::finished,this,[this,dialog](int result) {
            if(properties_dialog_==dialog)properties_dialog_=nullptr;
            if(result==QDialog::Accepted){preserve_view_on_refresh_=true;refresh_tabs();refresh_scene();}
        });
        dialog->show();
    });
}

void AssemblyWorkspaceWindow::add_tree_rename_action(QMenu& menu,QTreeWidgetItem* item) {
    if(!item||properties_dialog_||!active_sketch_id_.empty())return;
    if(!item->parent()) {
        if(workspace_.active_document_id()!=workspace_.displayed_document_id())return;
        if(rename_document_action_->isEnabled())menu.addAction(rename_document_action_);
        else add_object_rename_action(menu,workspace_.active_document_id(),"document",workspace_.active_document_id());
        return;
    }
    const auto path=item->data(0,Qt::UserRole+1).toString().toStdString();
    if(!path.empty()&&path!=resolve_active_occurrence(workspace_.active_document_id()).value_or(std::string{}))return;
    const auto kind=item->data(0,Qt::UserRole+3).toString().toStdString();
    add_object_rename_action(menu,workspace_.active_document_id(),kind,item->data(0,Qt::UserRole).toString().toStdString());
}

QAction* AssemblyWorkspaceWindow::exec_tree_menu(QMenu& menu,QTreeWidgetItem* item,const QPoint& position) {
    add_tree_rename_action(menu,item);
    return menu.exec(tree_->viewport()->mapToGlobal(position));
}

void AssemblyWorkspaceWindow::rename_document_file(std::string document_id) {
    if (rename_document_dialog_) { rename_document_dialog_->raise(); return; }
    if (!native_file_operation_ready()) return;
    const auto id = document_id.empty()?workspace_.active_document_id():document_id;
    const bool family_instance=!id.empty()&&workspace::family_owner(workspace_,id)!=id;
    const auto* selected=workspace_.find(id);
    const auto target=selected?std::visit([](const auto& value)->std::optional<std::filesystem::path> {
        return value.path.empty()?std::nullopt:std::optional<std::filesystem::path>{value.path};
    },*selected):std::nullopt;
    if (!target&&!family_instance) return;
    const auto old_path = target?std::filesystem::absolute(*target).lexically_normal():std::filesystem::path{};
    const auto text_path = [](const auto& path) { return QString::fromStdString(document::path_to_utf8(path)); };
    const auto initial=family_instance?QString::fromStdString(workspace::user_parameters(workspace_,id).flat.at("name")):text_path(old_path.filename());
    auto* dialog = new RenameDocumentDialog(initial,
        [this, id, old_path, text_path, family_instance](QString name) -> QString {
            name=application_settings_.document_naming.normalize(name);
            if (!native_file_operation_ready(rename_document_dialog_))
                return tr("Nejprve dokončete nebo zrušte otevřené vlastnosti.");
            const auto* current = workspace_.find(id);
            if(family_instance) {
                if(!current)return tr("Dokument již není otevřený.");
                try {
                    auto values=workspace::user_parameters(workspace_,id);
                    values.flat["name"]=name.toStdString();values.values["name"][""]=name.toStdString();
                    static_cast<void>(workspace::set_user_parameters(workspace_,id,std::move(values)));
                    apply_console_change({command_host::ChangeKind::Rename,id});
                    return {};
                }catch(const std::exception& error){return tr(error.what());}
            }
            if (!current || std::visit([](const auto& state) {
                    return std::filesystem::absolute(state.path).lexically_normal();
                }, *current) != old_path)
                return tr("Open documents changed before native file rename.");
            bool started = false;
            try {
                auto job = workspace::prepare_document_file_rename(workspace_, id, name.toStdString(), working_directory_, application_settings_.document_naming);
                const auto progress = tr("Přejmenovávám %1…").arg(text_path(old_path.filename()));
                begin_status_operation(progress); update_status_operation(progress, -1, 0); started = true;
                run_background_task([&job] { job.stage(); });
                const auto result = job.commit(workspace_);
                if (!result.ok()) {
                    auto message = tr(result.message.c_str()) + QStringLiteral("\n") + text_path(result.failed_path);
                    if (!result.recovery_paths.empty()) {
                        message += QStringLiteral("\n") + tr("Original files could not all be restored. Recovery data:");
                        for (const auto& path : result.recovery_paths) message += QStringLiteral("\n") + text_path(path);
                    }
                    if (result.changed) apply_console_change({command_host::ChangeKind::Files, id});
                    finish_status_operation(message, false);
                    return message;
                }
                if (result.changed) apply_console_change({command_host::ChangeKind::Rename, id});
                finish_status_operation(tr("Soubor přejmenován na %1").arg(text_path(result.to.filename())), true);
                return {};
            } catch (const std::exception& error) {
                const auto message = tr(error.what());
                if (started) finish_status_operation(message, false);
                return message;
            }
        }, application_settings_, this, application_settings_.document_naming);
    rename_document_dialog_ = dialog;
    connect(dialog, &QObject::destroyed, this, [this, dialog] {
        if (rename_document_dialog_ == dialog) rename_document_dialog_ = nullptr;
    });
    dialog->show();
}

void AssemblyWorkspaceWindow::delete_current_document_file() {
    delete_document_file(false);
}

void AssemblyWorkspaceWindow::delete_old_file_versions_keep_latest() {
    prune_file_archives(false, 1);
}
void AssemblyWorkspaceWindow::delete_old_file_versions() {
    prune_file_archives(false, 0);
}

void AssemblyWorkspaceWindow::delete_all_file_versions() {
    delete_document_file(true);
}
void AssemblyWorkspaceWindow::delete_document_file(bool include_archives) {
    if (!native_file_operation_ready()) return;
    const auto id = workspace_.active_document_id();
    if (id.empty()) return;
    const auto title = include_archives ? tr("Aktuální soubor a všechny verze") : tr("Odstranit aktuální soubor");
    try {
        // This is a read-only draft; Yes confirms deletion and any stated discard.
        const auto plan = workspace::prepare_document_file_removal(workspace_, id, include_archives, true);
        const auto name = QString::fromStdString(document::path_to_utf8(plan.path().filename()));
        auto message = include_archives
            ? tr("Odstranit soubor %1 včetně archivů (%2)?").arg(name).arg(plan.archive_count())
            : tr("Opravdu chcete odstranit soubor %1?").arg(name);
        if (plan.has_unsaved_changes())
            message += QStringLiteral("\n\n") + tr("Unsaved changes in this document will also be discarded.");
        if (QMessageBox::warning(this, title, message, QMessageBox::Yes | QMessageBox::No, QMessageBox::No) != QMessageBox::Yes)
            return;
        const auto result = workspace::remove_document_file(workspace_, plan);
        if (!result.closed_document.empty())
            apply_console_change({command_host::ChangeKind::Close, workspace_.displayed_document_id()});
        else refresh_delete_file_actions();
        if (!result.ok()) {
            const auto failed_path = QString::fromStdString(document::path_to_utf8(result.failed_path));
            QMessageBox::critical(this, tr("Odstranění selhalo"), tr(result.message.c_str()) +
                QStringLiteral("\n") + failed_path + QStringLiteral("\n") +
                tr("Files removed before the failure:") + QStringLiteral(" ") + QString::number(result.removed.size()));
            return;
        }
        state_->setText(include_archives ? tr("Soubor %1 a všechny verze odstraněny.").arg(name)
                                       : tr("Soubor %1 odstraněn.").arg(name));
    } catch (const std::exception& error) {
        QMessageBox::critical(this, tr("Odstranění selhalo"), tr(error.what()));
    }
}

void AssemblyWorkspaceWindow::delete_working_directory_old_versions() {
    prune_file_archives(true, 0);
}
void AssemblyWorkspaceWindow::delete_working_directory_old_versions_keep_latest() {
    prune_file_archives(true, 1);
}
void AssemblyWorkspaceWindow::prune_file_archives(bool whole_directory, std::size_t keep) {
    const auto target = whole_directory
        ? std::optional<std::filesystem::path>(std::filesystem::absolute(working_directory_).lexically_normal())
        : active_document_file_path();
    if (!target) return;
    const auto text_path = [](const std::filesystem::path& path) {
        return QString::fromStdString(document::path_to_utf8(path));
    };
    const QString title = whole_directory ? tr("Pracovní adresář")
        : keep ? tr("Staré verze kromě nejnovější") : tr("Staré verze");
    try {
        const auto groups = whole_directory ? workspace::directory_archives(*target)
            : workspace::ArchiveGroups{{*target, workspace::document_archives(*target)}};
        const auto files = workspace::archives_to_remove(groups, keep);
        if (files.empty()) {
            QMessageBox::information(this, title, whole_directory
                ? tr("V pracovním adresáři %1 nebyly nalezeny žádné starší verze.").arg(text_path(*target))
                : tr("Žádné starší verze souboru %1 nebyly nalezeny.").arg(text_path(target->filename())));
            return;
        }
        std::uintmax_t bytes = 0;
        for (const auto& file : files) bytes += file.size;
        const auto message = whole_directory
            ? (keep ? tr("Odstranit %1 souborů starších verzí (%2) z pracovního adresáře %3? "
                         "Nejnovější verze každého dokumentu zůstane zachována.")
                    : tr("Odstranit %1 souborů starších verzí (%2) z pracovního adresáře %3?"))
                .arg(files.size()).arg(format_file_size(bytes)).arg(text_path(*target))
            : tr("Odstranit %1 starších verzí souboru %2?").arg(files.size()).arg(text_path(target->filename()));
        const auto answer = whole_directory
            ? QMessageBox::warning(this, title, message, QMessageBox::Yes | QMessageBox::No, QMessageBox::No)
            : QMessageBox::question(this, title, message, QMessageBox::Yes | QMessageBox::No, QMessageBox::No);
        if (answer != QMessageBox::Yes) return;
        const auto result = workspace::remove_archives(files);
        refresh_delete_file_actions();
        state_->setText(tr("Odstraněno %1 souborů starších verzí.").arg(result.removed.size()));
        if (!result.ok())
            QMessageBox::critical(this, tr("Odstranění selhalo"),
                tr(result.message.c_str()) + QStringLiteral("\n") + text_path(result.failed_path) +
                QStringLiteral("\n") + state_->text());
    } catch (const std::exception& error) {
        QMessageBox::critical(this, tr("Odstranění selhalo"), tr(error.what()));
    }
}

void AssemblyWorkspaceWindow::show_global_settings() {
    if (global_settings_dialog_ != nullptr) {
        global_settings_dialog_->raise();
        global_settings_dialog_->activateWindow();
        return;
    }
    auto pending = application_settings_;
    pending.language = ApplicationSettings::load(
        QFileInfo(application_settings_.config_path).absolutePath()).language;
    auto* dialog = new GlobalSettingsDialog(std::move(pending), this);
    global_settings_dialog_ = dialog;
    connect(dialog, &QDialog::accepted, this, [this] {
        auto next = ApplicationSettings::load(
            QFileInfo(application_settings_.config_path).absolutePath());
        const bool language_changed = next.language != application_settings_.language;
        if (language_changed) {
            // Keep every existing and newly opened surface in the current
            // language until the complete workspace window is reconstructed.
            next.language = application_settings_.language;
            next.translations = application_settings_.translations;
            next.qt_translations = application_settings_.qt_translations;
        }
        application_settings_ = std::move(next);
        drawing_workspace_->set_formats_directory(application_settings_.resolved_paths.value("Formats"));
        if (!language_changed) apply_application_translations(*qApp, application_settings_);
        apply_application_font(*qApp, application_settings_);
        const QString configured =
            application_settings_.resolved_paths.value("WorkingDirectory");
        if (!configured.trimmed().isEmpty() && QFileInfo(configured).isDir()) {
            try {change_working_directory(std::filesystem::u8path(QFileInfo(configured).absoluteFilePath().toStdString()),true);}
            catch(const std::exception& error) {report_operation_error(tr("Pracovní adresář je obsazený"),QObject::tr(error.what()));}
            refresh_delete_file_actions();
        }
        if (language_changed) QTimer::singleShot(0, this, [this] {
            // Update installation already owns process shutdown and restart.
            if (UpdateService::get()->phase() != "waiting") request_language_restart();
        });
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
