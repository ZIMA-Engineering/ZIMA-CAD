#include <zima/workspace/appearance_operations.hpp>
#include "appearance_dialog.hpp"
#include "assembly_workspace_window.hpp"
#include <QAction>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QMessageBox>
#include <QSaveFile>
#include <QTreeWidget>
#include <algorithm>
#include <zima/document/appearance.hpp>
#include <zima/document/body_history.hpp>
namespace zima::app {
using kernel::Appearance;
using kernel::SurfaceStyle;
using workspace::part_appearance;
void AssemblyWorkspaceWindow::show_body_color_dialog() {
  if (properties_dialog_ || tree_edit_dialog_ || !section_dialog_.isNull())
    return;
  const auto active = workspace_.active_document_id(),
             displayed = workspace_.displayed_document_id();
  auto *part = workspace_.open_part(active);
  std::string path, owner_id, occurrence_id, body_id;
  Appearance initial;
  if (part) {
    initial = part_appearance(part->session.document());
    body_id = part->session.document().body_history.active_body_id();
    path = displayed == active ? std::string{} : workspace_.active_occurrence_path();
  } else {
    const auto selected = selected_occurrence_path();
    if (!selected)
      return;
    path = *selected;
    const auto address = workspace_.resolve_occurrence(
        displayed, assembly::InstancePath::decode(path));
    if (!address || address->owner_assembly_document_id != active) {
      QMessageBox::information(
          this, tr("Barvy a vzhled"),
          tr("Aktivujte Part nebo jeho bezprostřední sestavu."));
      return;
    }
    owner_id = address->owner_assembly_document_id;
    occurrence_id = address->occurrence_id;
    const auto *occurrence =
        workspace_.open_assembly(owner_id)->session.document().find_occurrence(
            occurrence_id);
    if (occurrence->source_kind != assembly::ComponentSourceKind::Part) {
      QMessageBox::information(this, tr("Barvy a vzhled"),
          tr("Vyberte konkrétní Part v podsestavě a aktivujte jeho vlastnící sestavu."));
      return;
    }
    initial = workspace::occurrence_appearance(workspace_, *occurrence);
  }
  const auto edit = workspace::prepare_appearance_edit(workspace_, active, occurrence_id, body_id);
  initial = edit.initial;
  const auto palette_path = QFileInfo(application_settings_.config_path)
                                .absoluteDir()
                                .filePath("appearances.json");
  auto palette = document::default_surface_palette();
  const auto factory_palette = QFileInfo(application_settings_.base_config_path)
                                   .absoluteDir().filePath("appearances.json");
  QFile file(QFileInfo::exists(palette_path) ? palette_path : factory_palette);
  if (file.exists())
    try {
      if (!file.open(QIODevice::ReadOnly))
        throw std::runtime_error("Nelze otevřít paletu");
      auto custom = document::deserialize_palette(file.readAll().toStdString());
      palette.insert(palette.end(), custom.begin(), custom.end());
    } catch (const std::exception &e) {
      QMessageBox::warning(this, tr("Paleta vzhledů"),
                           QString::fromUtf8(e.what()));
      return;
    }
  const auto builtin_count = document::default_surface_palette().size();
  const auto initial_custom = document::serialize_palette(
      std::vector<kernel::NamedStyle>(palette.begin()+builtin_count,palette.end()));
  auto *dialog = new AppearanceDialog(
      initial, body_id, std::move(palette),
      [this, path](const Appearance &a) {
        update_viewer_body_colors(&a, path);
      },
      [this, edit, palette_path,
       builtin_count, initial_custom](const Appearance &a, const auto &palette) {
        std::vector<kernel::NamedStyle> custom(palette.begin() + builtin_count,
                                               palette.end());
        const auto serialized = document::serialize_palette(custom);
        if (serialized != initial_custom) {
          if (!QDir().mkpath(QFileInfo(palette_path).absolutePath()))
            throw std::runtime_error("Nelze vytvořit adresář palety vzhledů");
          QSaveFile file(palette_path);
          if (!file.open(QIODevice::WriteOnly))
            throw std::runtime_error("Nelze uložit paletu vzhledů");
          const auto bytes = QByteArray::fromStdString(serialized);
          if (file.write(bytes) != bytes.size() || !file.commit())
            throw std::runtime_error("Uložení palety selhalo");
        }
        static_cast<void>(workspace::commit_appearance(workspace_, edit, a));
        update_viewer_body_colors();
      },
      [this, path](const std::vector<std::string> &keys) {
        std::vector<viewer::ViewerCandidate> faces;
        for (const auto &key : keys) {
          const auto split = key.find("::");
          if (split == std::string::npos)
            continue;
          faces.push_back({viewer::CandidateKind::Face, 0, 0,
                           key.substr(0, split), key.substr(split + 2), path,
                           viewer::CandidateGeometry::Display});
        }
        viewer_->set_inspected_faces(std::move(faces));
      },
      this);
  properties_dialog_ = dialog;
  dialog->setAttribute(Qt::WA_DeleteOnClose);
  viewer_->clear_selection();
  viewer_->set_result_face_selection(true);
  viewer_->set_selection_contract({viewer::CandidateKind::Face});
  const auto owners = initial.owner_bodies;
  viewer_->set_candidate_filter([path, body_id, owners](const auto &c) {
    if (c.kind != viewer::CandidateKind::Face || c.instance_path != path ||
        c.geometry != viewer::CandidateGeometry::Display ||
        c.owner_id.empty() || c.semantic_key.empty())
      return false;
    if (c.semantic_key == "plane" || c.semantic_key.starts_with("origin:"))
      return false;
    if (body_id.empty())
      return true;
    const auto found = owners.find(c.owner_id);
    return found != owners.end() && found->second == body_id;
  });
  connect(dialog, &QObject::destroyed, this, [this] {
    properties_dialog_ = nullptr;
    viewer_->set_result_face_selection(false);
    viewer_->set_inspected_faces({});
    viewer_->set_candidate_filter({});
    viewer_->clear_selection();
    refresh_scene();
  });
  dialog->show();
}
void AssemblyWorkspaceWindow::update_body_color_actions() {
  if (custom_body_color_action_)
    custom_body_color_action_->setEnabled(
        workspace_.open_part(workspace_.active_document_id()) ||
        selected_occurrence_path().has_value());
}
void AssemblyWorkspaceWindow::update_viewer_body_colors(
    const Appearance *preview, const std::string &preview_path) {
  SurfaceStyle base;
  std::map<std::string, SurfaceStyle> instances, owners, faces;
  const auto add = [&](const Appearance &a, const std::string &path) {
    const auto prefix = path + "\x1f";
    std::erase_if(owners,
                  [&](const auto &x) { return x.first.starts_with(prefix); });
    std::erase_if(faces,
                  [&](const auto &x) { return x.first.starts_with(prefix); });
    if (path.empty())
      base = a.body;
    else
      instances[path] = a.body;
    for (const auto &[owner, body] : a.owner_bodies)
      if (const auto it = a.bodies.find(body); it != a.bodies.end())
        owners[prefix + owner] = it->second;
    for (const auto &g : a.groups)
      for (const auto &key : g.faces) {
        const auto split = key.find("::");
        if (split != std::string::npos)
          faces[prefix + key.substr(0, split) + "\x1f" +
                key.substr(split + 2)] = g.style;
      }
  };
  const auto displayed = workspace_.displayed_document_id();
  if (const auto *part = workspace_.open_part(displayed))
    add(part_appearance(part->session.document()), {});
  else if (workspace_.open_assembly(displayed)) {
    const auto scene = workspace_.authoritative_viewer_mesh(displayed);
    std::set<std::string> paths;
    for (const auto &f : scene.triangle_references)
      if (!f.instance_path.empty())
        paths.insert(f.instance_path);
    for (const auto &path : paths) {
      const auto address = workspace_.resolve_occurrence(
          displayed, assembly::InstancePath::decode(path));
      if (!address)
        continue;
      const auto *owner =
          workspace_.open_assembly(address->owner_assembly_document_id);
      if (!owner)
        continue;
      const auto *occurrence =
          owner->session.document().find_occurrence(address->occurrence_id);
      if (!occurrence)
        continue;
      const auto a = workspace::occurrence_appearance(workspace_, *occurrence);
      add(a, path);
    }
    if (const auto *active =
            workspace_.open_part(workspace_.active_document_id());
        active && !workspace_.active_occurrence_path().empty())
      add(part_appearance(active->session.document()), workspace_.active_occurrence_path());
  }
  if (preview)
    add(*preview, preview_path);
  viewer_->set_body_surface_styles(base, std::move(instances),
                                   std::move(owners), std::move(faces));
}
} // namespace zima::app
