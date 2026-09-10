#pragma once
#include <zima/drawing/model_annotations.hpp>
#include <zima/ui/properties_subwindow.hpp>
class QComboBox;
class QCheckBox;
class QTreeWidget;
class QLabel;
class QPushButton;
class QTableWidget;
namespace zima::ui { class ReferenceCellItem; }
namespace zima::app {
class ShowEraseDialog final : public ui::PropertiesSubWindow {
public:
  using Reference = drawing::ModelAnnotationReference;
  using Preview = std::function<void(const drawing::DrawingView &,
                                     const std::set<Reference> &)>;
  ShowEraseDialog(drawing::DrawingView, Preview,
                  std::function<void(const drawing::DrawingView &)>, QWidget *);
  void toggle(const Reference &);
  void set_view(drawing::DrawingView);
  void set_view_picker(std::function<void()> picker){view_picker_=std::move(picker);}
  void arm_view();
  void set_view_picker_cancel(std::function<void()> cancel){view_picker_cancel_=std::move(cancel);}
  std::string view_id()const{return initial_.id;}

protected:
  bool submit() override;
  bool eventFilter(QObject*,QEvent*) override;

private:
  drawing::DrawingView initial_, pending_;
  drawing::ShowEraseSession session_;
  Preview preview_;
  std::function<void(const drawing::DrawingView &)> commit_;
  QComboBox *selection_{};
  int mode_{};
  QPushButton *show_{},*erase_{};
  QTableWidget *view_field_{};
  ui::ReferenceCellItem *view_item_{};
  std::function<void()> view_picker_,view_picker_cancel_;
  QPointF middle_origin_;
  bool middle_pending_{};
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
