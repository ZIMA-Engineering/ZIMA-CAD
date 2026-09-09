#pragma once
#include <zima/drawing/model_annotations.hpp>
#include <zima/ui/properties_subwindow.hpp>
class QComboBox;
class QCheckBox;
class QTreeWidget;
class QLabel;
namespace zima::app {
class ShowEraseDialog final : public ui::PropertiesSubWindow {
public:
  using Reference = drawing::ModelAnnotationReference;
  using Preview = std::function<void(const drawing::DrawingView &,
                                     const std::set<Reference> &)>;
  ShowEraseDialog(drawing::DrawingView, Preview,
                  std::function<void(const drawing::DrawingView &)>, QWidget *);
  void toggle(const Reference &);

protected:
  bool submit() override;

private:
  drawing::DrawingView initial_, pending_;
  drawing::ShowEraseSession session_;
  Preview preview_;
  std::function<void(const drawing::DrawingView &)> commit_;
  QComboBox *mode_{}, *selection_{};
  QCheckBox *dimensions_{}, *axes_{}, *construction_{};
  QTreeWidget *items_{};
  QLabel *status_{};
  std::set<Reference> selected_;
  std::vector<Reference> offered_;
  bool rebuilding_{};
  std::set<drawing::ModelAnnotationKind> kinds() const;
  void rebuild();
  void publish();
};
} // namespace zima::app
