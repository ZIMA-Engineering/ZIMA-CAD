#pragma once
#include <functional>
#include <set>
#include <zima/document/appearance.hpp>
#include <zima/ui/properties_subwindow.hpp>
#include <zima/viewer/mesh_view.hpp>
class QComboBox;
class QListWidget;
class QLineEdit;
class QSlider;
class QTableWidget;
class QPushButton;
namespace zima::app {
class AppearanceDialog final : public ui::PropertiesSubWindow {
public:
  using Preview = std::function<void(const kernel::Appearance &)>;
  AppearanceDialog(
      kernel::Appearance value, std::string body_id,
      std::vector<kernel::NamedStyle> palette, Preview preview,
      std::function<void(const kernel::Appearance &,
                         const std::vector<kernel::NamedStyle> &)>
          commit,
      std::function<void(const std::vector<std::string> &)> inspect,
      QWidget *parent);
  void select_face(const viewer::ViewerCandidate &face);
  void end_entry();
  const kernel::Appearance &pending() const { return value_; }

protected:
  bool submit() override;

private:
  kernel::Appearance value_, initial_;
  std::string body_id_, target_;
  bool armed_{}, refreshing_{};
  std::vector<kernel::NamedStyle> palette_;
  Preview preview_;
  std::function<void(const kernel::Appearance &,
                     const std::vector<kernel::NamedStyle> &)>
      commit_;
  std::function<void(const std::vector<std::string> &)> inspect_;
  std::set<std::string> inspected_;
  QComboBox *categories_{}, *save_category_{};
  QListWidget *palette_list_{};
  QLineEdit *name_{}, *color_{};
  QSlider *gloss_{}, *metal_{};
  QTableWidget *groups_{};
  QPushButton *body_button_{};
  viewer::MeshView *sphere_{};
  kernel::SurfaceStyle &target_style();
  void load_editor();
  void editor_changed();
  void refresh_palette();
  void refresh_groups();
  void publish();
  void inspect_groups();
};
} // namespace zima::app
