#include "show_erase_dialog.hpp"
#include <QCheckBox>
#include <QMouseEvent>
#include <QDialogButtonBox>
#include <QTableWidget>
#include <QHeaderView>
#include <zima/ui/reference_cell.hpp>
#include <QComboBox>
#include <QFormLayout>
#include <QHBoxLayout>
#include <QLabel>
#include <QPushButton>
#include <QTreeWidget>
#include <QVBoxLayout>
#include <algorithm>
namespace zima::app {
ShowEraseDialog::ShowEraseDialog(
    drawing::DrawingView view, Preview preview,
    std::function<void(const std::vector<drawing::DrawingView> &)> commit, QWidget *parent)
    : PropertiesSubWindow(tr("Show / Erase"), parent),
      initial_(std::move(view)), pending_(initial_), session_(initial_),
      preview_(std::move(preview)), commit_(std::move(commit)) {
  setObjectName("drawingShowEraseDialog");
  set_initial_size({470, 480});
  setMinimumSize(420, 380);
  set_centered_on_show(false);
  auto *form = new QFormLayout;
  view_field_=new QTableWidget(1,1,this);view_field_->setObjectName("showEraseView");
  view_field_->horizontalHeader()->hide();view_field_->verticalHeader()->hide();
  view_field_->horizontalHeader()->setSectionResizeMode(QHeaderView::Stretch);
  view_field_->setFixedHeight(38);view_field_->setSelectionMode(QAbstractItemView::NoSelection);
  view_field_->setEditTriggers(QAbstractItemView::NoEditTriggers);
  ui::install_reference_cell_delegate(view_field_);
  view_item_=new ui::ReferenceCellItem;view_field_->setItem(0,0,view_item_);
  form->addRow(tr("Pohled"),view_field_);
  connect(view_field_,&QTableWidget::cellClicked,this,[this](int,int){arm_view();});
  auto* modes=new QHBoxLayout;
  show_=new QPushButton("SHOW",this);erase_=new QPushButton("ERASE",this);
  show_->setObjectName("showEraseShow");erase_->setObjectName("showEraseErase");
  for(auto* button:{show_,erase_}){button->setCheckable(true);button->setStyleSheet("QPushButton:checked { background: #398414; color: white; }");modes->addWidget(button);}
  form->addRow(modes);
  const auto select_mode=[this](int mode){mode_=mode;show_->setChecked(mode==0);erase_->setChecked(mode==1);selected_.clear();selection_->setCurrentIndex(mode);rebuild();};
  connect(show_,&QPushButton::clicked,this,[select_mode]{select_mode(0);});
  connect(erase_,&QPushButton::clicked,this,[select_mode]{select_mode(1);});
  show_->setChecked(true);
  selection_ = new QComboBox(this);
  selection_->setObjectName("showEraseSelection");
  selection_->addItems(
      {tr("Vybrat položky k ponechání"), tr("Vybrat položky k odebrání")});
  form->addRow(tr("Výběr"), selection_);
  content_layout()->addLayout(form);
  auto *filters = new QHBoxLayout;
  dimensions_ = new QCheckBox(tr("Kóty"), this);
  axes_ = new QCheckBox(tr("Osy"), this);
  construction_ = new QCheckBox(tr("Pomocná geometrie"), this);
  dimensions_->setObjectName("showEraseDimensions");
  axes_->setObjectName("showEraseAxes");
  construction_->setObjectName("showEraseConstruction");
  for (auto *box : {dimensions_, axes_, construction_}) {
    box->setChecked(true);
    filters->addWidget(box);
    connect(box, &QCheckBox::toggled, this, [this] { rebuild(); });
  }
  content_layout()->addLayout(filters);
  auto *hint = new QLabel(
      tr("Vyberte položky ve výkresu nebo v seznamu. Pravé tlačítko cykluje "
         "překrývající se nabídku. Krátký stisk prostředního ukončí výběr pro pohled. "
         "OK nebo dvojklik prostředním potvrdí všechny pohledy; Zrušit zahodí změny."),
      this);
  hint->setWordWrap(true);
  content_layout()->addWidget(hint);
  items_ = new QTreeWidget(this);
  items_->setObjectName("showEraseItems");
  items_->setHeaderLabels({tr("Položka"), tr("Výskyt")});
  items_->setRootIsDecorated(false);
  items_->setSelectionMode(QAbstractItemView::NoSelection);
  items_->setColumnWidth(0, 280);
  content_layout()->addWidget(items_);
  auto *row = new QHBoxLayout;
  auto *all = new QPushButton(tr("Vybrat vše"), this);
  all->setObjectName("showEraseAll");
  auto *none = new QPushButton(tr("Zrušit výběr"), this);
  none->setObjectName("showEraseNone");
  row->addWidget(all);
  row->addWidget(none);
  content_layout()->addLayout(row);
  status_ = new QLabel(this);
  status_->setWordWrap(true);
  content_layout()->addWidget(status_);
  connect(all, &QPushButton::clicked, this, [this] {
    selected_ = {offered_.begin(), offered_.end()};
    rebuild();
  });
  connect(none, &QPushButton::clicked, this, [this] {
    selected_.clear();
    rebuild();
  });
  connect(selection_, &QComboBox::currentIndexChanged, this,
          [this] { rebuild(); });
  connect(items_, &QTreeWidget::itemChanged, this, [this](auto *item, int) {
    if (rebuilding_)
      return;
    const auto id = offered_.at(item->data(0, Qt::UserRole).toUInt());
    if (item->checkState(0) == Qt::Checked)
      selected_.insert(id);
    else
      selected_.erase(id);
    publish();
  });
  set_view(initial_);
}
std::set<drawing::ModelAnnotationKind> ShowEraseDialog::kinds() const {
  std::set<drawing::ModelAnnotationKind> result;
  if (dimensions_->isChecked())
    result.insert(drawing::ModelAnnotationKind::Dimension);
  if (axes_->isChecked())
    result.insert(drawing::ModelAnnotationKind::Axis);
  if (construction_->isChecked())
    result.insert(drawing::ModelAnnotationKind::Construction);
  return result;
}
void ShowEraseDialog::arm_view(){view_item_->set_active_input(true);view_field_->viewport()->update();if(view_picker_)view_picker_();}
void ShowEraseDialog::set_view(drawing::DrawingView view){
  if(!initial_.id.empty())staged_[initial_.id]=pending_;
  if(auto found=staged_.find(view.id);found!=staged_.end())view=found->second;
  initial_=std::move(view);pending_=initial_;session_=drawing::ShowEraseSession(initial_);selected_.clear();
  view_item_->set_reference(QString::fromStdString(initial_.id));
  view_item_->setText(initial_.id.empty()?tr("Vyberte pohled ve výkresu…"):QString::fromStdString(initial_.name.empty()?initial_.id:initial_.name));
  view_item_->set_active_input(initial_.id.empty());
  buttons()->button(QDialogButtonBox::Ok)->setEnabled(!initial_.id.empty());
  rebuild();
}
void ShowEraseDialog::rebuild() {
  rebuilding_ = true;
  offered_ = session_.candidates(
      static_cast<drawing::ShowEraseMode>(mode_), kinds());
  std::erase_if(selected_, [this](const auto &id) {
    return std::ranges::find(offered_, id) == offered_.end();
  });
  items_->clear();
  for (std::size_t i = 0; i < offered_.size(); ++i) {
    const auto &id = offered_[i];
    const auto item = std::ranges::find(initial_.model_annotations, id,
                                        &drawing::ModelAnnotation::source);
    auto *row = new QTreeWidgetItem(items_);
    auto label = item->kind == drawing::ModelAnnotationKind::Dimension
                     ? tr("Kóta %1").arg(QString::fromStdString(item->text))
                 : item->kind == drawing::ModelAnnotationKind::Axis
                     ? tr("Osa")
                     : tr("Pomocná geometrie");
    if(item->kind==drawing::ModelAnnotationKind::Axis) {
      if(id.semantic_id.starts_with("sketch_axis:"))label=tr("Osa skici %1").arg(QString::fromStdString(id.semantic_id.substr(12)).toUpper());
      else if(id.semantic_id.starts_with("origin:axis:"))label=tr("Osa počátku %1").arg(QString::fromStdString(id.semantic_id.substr(12)).toUpper());
      else if(id.semantic_id=="axis:primary" || id.semantic_id.starts_with("axis:profile:"))label=tr("Osa válce");
    }
    row->setText(0, label);
    row->setText(1, QString::fromStdString(id.instance_path));
    row->setToolTip(
        0, QString::fromStdString(id.owner_id + " / " + id.semantic_id));
    row->setFlags(Qt::ItemIsEnabled | Qt::ItemIsUserCheckable);
    row->setData(0, Qt::UserRole, static_cast<unsigned>(i));
    row->setCheckState(0, selected_.contains(id) ? Qt::Checked : Qt::Unchecked);
  }
  rebuilding_ = false;
  publish();
}
void ShowEraseDialog::publish() {
  pending_ = initial_;
  pending_.model_annotations = session_.preview(
      static_cast<drawing::ShowEraseMode>(mode_),
      static_cast<drawing::ShowEraseSelection>(selection_->currentIndex()),
      kinds(), selected_);
  const auto missing = std::ranges::count_if(
      initial_.model_annotations, [](const auto &a) { return a.unresolved; });
  status_->setText(
      initial_.id.empty() ? tr("Vyberte pohled ve výkresu.") : initial_.model_annotations.empty()
          ? tr("Pohled nemá uložené položky. Zavřete nástroj a zvolte "
               "Regenerovat pro načtení zdrojových kót a os.")
          : tr("Nabídnuto: %1 · vybráno: %2 · nevyřešeno: %3")
                .arg(offered_.size())
                .arg(selected_.size())
                .arg(missing));
  if (preview_ && !initial_.id.empty() && !view_item_->is_active_input())
    preview_(pending_, {offered_.begin(), offered_.end()},pending_views());
}
void ShowEraseDialog::toggle(const Reference &id) {
  if (std::ranges::find(offered_, id) == offered_.end())
    return;
  if (!selected_.erase(id))
    selected_.insert(id);
  rebuild();
}

bool ShowEraseDialog::eventFilter(QObject* watched,QEvent* event) {
  const auto* widget=qobject_cast<QWidget*>(watched);
  const bool inside=widget && parentWidget() && (widget==parentWidget() || parentWidget()->isAncestorOf(widget));
  if(isVisible() && inside && view_item_) {
    if(event->type()==QEvent::MouseButtonPress) {
      const auto* mouse=static_cast<QMouseEvent*>(event);
      if(mouse->button()==Qt::MiddleButton){middle_origin_=mouse->globalPosition();middle_pending_=true;}
    } else if(event->type()==QEvent::MouseMove && middle_pending_) {
      if((static_cast<QMouseEvent*>(event)->globalPosition()-middle_origin_).manhattanLength()>4)middle_pending_=false;
    } else if(event->type()==QEvent::MouseButtonRelease) {
      const auto* mouse=static_cast<QMouseEvent*>(event);
      if(mouse->button()==Qt::MiddleButton && middle_pending_) {
        middle_pending_=false;
        if(view_item_->is_active_input()) {
          view_item_->set_active_input(false);view_field_->viewport()->update();
          if(view_picker_cancel_)view_picker_cancel_();
          publish();
        } else if(!initial_.id.empty())arm_view();
      }
    }
  }
  return PropertiesSubWindow::eventFilter(watched,event);
}

std::vector<drawing::DrawingView> ShowEraseDialog::pending_views() const {
  auto views=staged_;if(!initial_.id.empty())views[initial_.id]=pending_;
  std::vector<drawing::DrawingView> result;
  for(const auto& [id,view]:views)result.push_back(view);
  return result;
}
bool ShowEraseDialog::submit() {
  if(initial_.id.empty())return false;
  commit_(pending_views());
  return true;
}
} // namespace zima::app
