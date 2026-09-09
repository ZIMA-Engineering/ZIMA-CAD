#include "show_erase_dialog.hpp"
#include <QCheckBox>
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
    std::function<void(const drawing::DrawingView &)> commit, QWidget *parent)
    : PropertiesSubWindow(tr("Show / Erase"), parent),
      initial_(std::move(view)), pending_(initial_), session_(initial_),
      preview_(std::move(preview)), commit_(std::move(commit)) {
  setObjectName("drawingShowEraseDialog");
  set_initial_size({470, 480});
  setMinimumSize(420, 380);
  set_centered_on_show(false);
  auto *form = new QFormLayout;
  mode_ = new QComboBox(this);
  mode_->setObjectName("showEraseMode");
  mode_->addItems(
      {tr("Show — zobrazit skryté"), tr("Erase — odebrat zobrazené")});
  selection_ = new QComboBox(this);
  selection_->setObjectName("showEraseSelection");
  selection_->addItems(
      {tr("Vybrat položky k ponechání"), tr("Vybrat položky k odebrání")});
  form->addRow(tr("Režim"), mode_);
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
         "překrývající se nabídku. Zrušit obnoví výchozí stav."),
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
  connect(mode_, &QComboBox::currentIndexChanged, this, [this] {
    selected_.clear();
    selection_->setCurrentIndex(mode_->currentIndex());
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
  rebuild();
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
void ShowEraseDialog::rebuild() {
  rebuilding_ = true;
  offered_ = session_.candidates(
      static_cast<drawing::ShowEraseMode>(mode_->currentIndex()), kinds());
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
      static_cast<drawing::ShowEraseMode>(mode_->currentIndex()),
      static_cast<drawing::ShowEraseSelection>(selection_->currentIndex()),
      kinds(), selected_);
  const auto missing = std::ranges::count_if(
      initial_.model_annotations, [](const auto &a) { return a.unresolved; });
  status_->setText(
      initial_.model_annotations.empty()
          ? tr("Pohled nemá uložené položky. Zavřete nástroj a zvolte "
               "Regenerovat pro načtení zdrojových kót a os.")
          : tr("Nabídnuto: %1 · vybráno: %2 · nevyřešeno: %3")
                .arg(offered_.size())
                .arg(selected_.size())
                .arg(missing));
  if (preview_)
    preview_(pending_, {offered_.begin(), offered_.end()});
}
void ShowEraseDialog::toggle(const Reference &id) {
  if (std::ranges::find(offered_, id) == offered_.end())
    return;
  if (!selected_.erase(id))
    selected_.insert(id);
  rebuild();
}

bool ShowEraseDialog::submit() {
  commit_(pending_);
  return true;
}
} // namespace zima::app
