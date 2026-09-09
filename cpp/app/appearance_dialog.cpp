#include "appearance_dialog.hpp"
#include <QComboBox>
#include <QFormLayout>
#include <QHBoxLayout>
#include <QHeaderView>
#include <QLabel>
#include <QLineEdit>
#include <QListWidget>
#include <QPainter>
#include <QPushButton>
#include <QSignalBlocker>
#include <QSlider>
#include <QTableWidget>
#include <QToolButton>
#include <QUuid>
#include <QVBoxLayout>
#include <algorithm>
#include <cmath>
#include <zima/ui/reference_cell.hpp>
namespace zima::app {
namespace {
QString swatch(const kernel::SurfaceStyle &s) {
  return "background:" + QString::fromStdString(s.color) +
         ";border:1px solid #70757a;border-radius:3px;min-width:28px;";
}
QIcon style_icon(const kernel::SurfaceStyle &s) {
  QPixmap p(44, 44);
  p.fill(Qt::transparent);
  QPainter painter(&p);
  painter.setRenderHint(QPainter::Antialiasing);
  QRadialGradient g(17, 13, 30);
  QColor c(QString::fromStdString(s.color));
  g.setColorAt(0, c.lighter(s.roughness < .3 ? 220 : 135));
  g.setColorAt(.4, c);
  g.setColorAt(1, c.darker(210));
  painter.setBrush(g);
  painter.setPen(Qt::NoPen);
  painter.drawEllipse(3, 3, 38, 38);
  return QIcon(p);
}
kernel::ViewerMesh sphere_mesh() {
  kernel::ViewerMesh mesh;
  constexpr unsigned rows = 48, cols = 96;
  constexpr double pi = 3.141592653589793;
  for (unsigned y = 0; y <= rows; ++y)
    for (unsigned x = 0; x <= cols; ++x) {
      double v = pi * y / rows, u = 2 * pi * x / cols;
      mesh.vertices.push_back(
          {std::sin(v) * std::cos(u), std::sin(v) * std::sin(u), std::cos(v)});
    }
  for (unsigned y = 0; y < rows; ++y)
    for (unsigned x = 0; x < cols; ++x) {
      auto a = y * (cols + 1) + x, b = a + cols + 1;
      mesh.triangles.insert(mesh.triangles.end(),
                            {a, b, a + 1, a + 1, b, b + 1});
      mesh.triangle_references.push_back({"preview", "sphere", {}});
      mesh.triangle_references.push_back({"preview", "sphere", {}});
    }
  return mesh;
}
} // namespace
AppearanceDialog::AppearanceDialog(
    kernel::Appearance value, std::string body_id,
    std::vector<kernel::NamedStyle> palette, Preview preview,
    std::function<void(const kernel::Appearance &,
                       const std::vector<kernel::NamedStyle> &)>
        commit,
    std::function<void(const std::vector<std::string> &)> inspect,
    QWidget *parent)
    : PropertiesSubWindow(tr("Barvy a vzhled"), parent),
      value_(std::move(value)), initial_(value_), body_id_(std::move(body_id)),
      palette_(std::move(palette)), preview_(std::move(preview)),
      commit_(std::move(commit)), inspect_(std::move(inspect)) {
  setObjectName("bodyColorPropertiesDialog");
  set_initial_size({630, 680});
  setMinimumSize(480, 500);
  set_centered_on_show();
  auto *top = new QHBoxLayout;
  auto *left = new QVBoxLayout;
  categories_ = new QComboBox(this);
  categories_->setObjectName("appearanceCategory");
  for (const auto *c : {"Základní barvy", "Plasty", "Laky", "Kovy", "Vlastní"})
    categories_->addItem(tr(c));
  for (const auto &p : palette_)
    if (categories_->findText(QString::fromStdString(p.category)) < 0)
      categories_->addItem(QString::fromStdString(p.category));
  left->addWidget(categories_);
  palette_list_ = new QListWidget(this);
  palette_list_->setObjectName("appearancePalette");
  palette_list_->setViewMode(QListView::IconMode);
  palette_list_->setIconSize({44, 44});
  palette_list_->setGridSize({110, 76});
  palette_list_->setResizeMode(QListView::Adjust);
  palette_list_->setMinimumHeight(165);
  left->addWidget(palette_list_);
  top->addLayout(left, 2);
  sphere_ = new viewer::MeshView(this);
  sphere_->setObjectName("appearanceSpherePreview");
  sphere_->setMinimumSize(150, 150);
  sphere_->setMaximumSize(205, 205);
  sphere_->set_display_mode(viewer::DisplayMode::Shaded);
  sphere_->set_mesh(sphere_mesh());
  top->addWidget(sphere_, 1);
  content_layout()->addLayout(top);
  auto *editor = new QFormLayout;
  name_ = new QLineEdit(this);
  name_->setObjectName("appearanceName");
  color_ = new QLineEdit(this);
  color_->setObjectName("appearanceColor");
  color_->setMaxLength(9);
  gloss_ = new QSlider(Qt::Horizontal, this);
  gloss_->setObjectName("appearanceGloss");
  gloss_->setRange(0, 96);
  metal_ = new QSlider(Qt::Horizontal, this);
  metal_->setObjectName("appearanceMetallic");
  metal_->setRange(0, 100);
  editor->addRow(tr("Název vzhledu"), name_);
  editor->addRow(tr("Barva (#RRGGBB)"), color_);
  editor->addRow(tr("Lesk: matný → leštěný"), gloss_);
  editor->addRow(tr("Kovový charakter"), metal_);
  content_layout()->addLayout(editor);
  auto *save_row = new QHBoxLayout;
  save_category_ = new QComboBox(this);
  save_category_->setEditable(true);
  for (int i = 0; i < categories_->count(); ++i)
    save_category_->addItem(categories_->itemText(i));
  save_category_->setCurrentText(tr("Vlastní"));
  save_row->addWidget(save_category_);
  auto *save = new QPushButton(tr("Přidat do palety"), this);
  save->setObjectName("appearanceAddColor");
  save_row->addWidget(save);
  content_layout()->addLayout(save_row);
  body_button_ = new QPushButton(tr("Těleso — základní vzhled"), this);
  body_button_->setObjectName("appearanceBody");
  content_layout()->addWidget(body_button_);
  auto *header = new QHBoxLayout;
  header->addWidget(new QLabel(tr("Skupiny ploch"), this));
  header->addStretch();
  auto *add = new QPushButton(tr("+ Skupina"), this);
  add->setObjectName("appearanceAddGroup");
  header->addWidget(add);
  content_layout()->addLayout(header);
  groups_ = new QTableWidget(this);
  groups_->setObjectName("appearanceGroups");
  groups_->setColumnCount(5);
  groups_->setHorizontalHeaderLabels({tr("Název"), tr("Vzhled"),
                                      tr("Plochy — kliknutím vybírat"),
                                      tr("Oko"), tr("Smazat")});
  groups_->setSelectionMode(QAbstractItemView::NoSelection);
  groups_->horizontalHeader()->setSectionResizeMode(2, QHeaderView::Stretch);
  groups_->setColumnWidth(0, 125);
  groups_->setColumnWidth(1, 60);
  groups_->setColumnWidth(3, 42);
  groups_->setColumnWidth(4, 48);
  groups_->setMinimumHeight(120);
  ui::install_reference_cell_delegate(groups_);
  content_layout()->addWidget(groups_);
  auto *reset_row = new QHBoxLayout;
  auto *clear = new QPushButton(tr("Vyčistit plochy"), this);
  clear->setObjectName("appearanceClearFaces");
  auto *defaults = new QPushButton(tr("Výchozí nastavení"), this);
  defaults->setObjectName("appearanceDefaults");
  reset_row->addWidget(clear);
  reset_row->addWidget(defaults);
  content_layout()->addLayout(reset_row);
  connect(categories_, &QComboBox::currentTextChanged, this,
          [this] { refresh_palette(); });
  connect(palette_list_, &QListWidget::itemClicked, this,
          [this](QListWidgetItem *item) {
            const auto index = item->data(Qt::UserRole).toInt();
            target_style() = palette_.at(index).style;
            name_->setText(QString::fromStdString(palette_[index].name));
            load_editor();
            publish();
            refresh_groups();
          });
  connect(color_, &QLineEdit::textEdited, this, [this] { editor_changed(); });
  connect(gloss_, &QSlider::valueChanged, this, [this] { editor_changed(); });
  connect(metal_, &QSlider::valueChanged, this, [this] { editor_changed(); });
  connect(body_button_, &QPushButton::clicked, this, [this] {
    target_.clear();
    armed_ = false;
    load_editor();
    refresh_groups();
  });
  connect(save, &QPushButton::clicked, this, [this] {
    if (name_->text().trimmed().isEmpty() ||
        save_category_->currentText().trimmed().isEmpty() ||
        !QColor(color_->text()).isValid())
      return;
    const auto category = save_category_->currentText().trimmed();
    palette_.push_back(
        {QUuid::createUuid().toString(QUuid::WithoutBraces).toStdString(),
         name_->text().trimmed().toStdString(), category.toStdString(),
         target_style()});
    if (categories_->findText(category) < 0)
      categories_->addItem(category);
    categories_->setCurrentText(category);
    refresh_palette();
  });
  connect(add, &QPushButton::clicked, this, [this] {
    kernel::SurfaceGroup group;
    group.id = QUuid::createUuid().toString(QUuid::WithoutBraces).toStdString();
    group.name = tr("Skupina %1").arg(value_.groups.size() + 1).toStdString();
    group.body_id = body_id_;
    group.style = target_style();
    target_ = group.id;
    value_.groups.push_back(std::move(group));
    armed_ = true;
    refresh_groups();
    load_editor();
  });
  connect(clear, &QPushButton::clicked, this, [this] {
    for (auto &g : value_.groups)
      if (g.body_id == body_id_)
        g.faces.clear();
    end_entry();
    refresh_groups();
    publish();
  });
  connect(defaults, &QPushButton::clicked, this, [this] {
    std::erase_if(value_.groups,
                  [this](const auto &g) { return g.body_id == body_id_; });
    target_.clear();
    if (body_id_.empty())
      value_.body = {};
    else
      value_.bodies.erase(body_id_);
    end_entry();
    load_editor();
    refresh_groups();
    publish();
  });
  connect(groups_, &QTableWidget::itemChanged, this,
          [this](QTableWidgetItem *item) {
            if (refreshing_ || item->column() != 0)
              return;
            const auto id = item->data(Qt::UserRole).toString().toStdString();
            for (auto &g : value_.groups)
              if (g.id == id && !item->text().trimmed().isEmpty())
                g.name = item->text().trimmed().toStdString();
          });
  connect(groups_, &QTableWidget::cellClicked, this, [this](int row, int col) {
    if (col != 2)
      return;
    target_ =
        groups_->item(row, 0)->data(Qt::UserRole).toString().toStdString();
    armed_ = true;
    refresh_groups();
    load_editor();
  });
  connect(this, &QDialog::rejected, this, [this] {
    end_entry();
    if (preview_)
      preview_(initial_);
  });
  refresh_palette();
  load_editor();
  refresh_groups();
}
kernel::SurfaceStyle &AppearanceDialog::target_style() {
  if (!target_.empty())
    for (auto &g : value_.groups)
      if (g.id == target_)
        return g.style;
  if (body_id_.empty())
    return value_.body;
  return value_.bodies.try_emplace(body_id_, value_.body).first->second;
}
void AppearanceDialog::load_editor() {
  refreshing_ = true;
  const auto s = target_style();
  color_->setText(QString::fromStdString(s.color));
  gloss_->setValue(qRound((1 - s.roughness) * 100));
  metal_->setValue(qRound(s.metallic * 100));
  sphere_->set_body_surface_styles(s);
  body_button_->setIcon(
      style_icon(body_id_.empty() ? value_.body : value_.bodies.at(body_id_)));
  refreshing_ = false;
}
void AppearanceDialog::editor_changed() {
  if (refreshing_)
    return;
  const QColor color(color_->text());
  if (!color.isValid())
    return;
  auto &s = target_style();
  s = {color.name(QColor::HexArgb).toStdString(), 1 - gloss_->value() / 100.0,
       metal_->value() / 100.0};
  sphere_->set_body_surface_styles(s);
  body_button_->setIcon(
      style_icon(body_id_.empty() ? value_.body : value_.bodies.at(body_id_)));
  refresh_groups();
  publish();
}
void AppearanceDialog::publish() {
  if (preview_)
    preview_(value_);
}
void AppearanceDialog::refresh_palette() {
  palette_list_->clear();
  for (std::size_t i = 0; i < palette_.size(); ++i) {
    const auto &p = palette_[i];
    if (QString::fromStdString(p.category) != categories_->currentText())
      continue;
    auto *item = new QListWidgetItem(
        style_icon(p.style), QString::fromStdString(p.name), palette_list_);
    item->setData(Qt::UserRole, static_cast<int>(i));
    item->setToolTip(QString::fromStdString(p.name));
  }
}
void AppearanceDialog::refresh_groups() {
  refreshing_ = true;
  groups_->setRowCount(0);
  for (const auto &g : value_.groups) {
    if (g.body_id != body_id_)
      continue;
    const int row = groups_->rowCount();
    groups_->insertRow(row);
    auto *label = new QTableWidgetItem(QString::fromStdString(g.name));
    label->setData(Qt::UserRole, QString::fromStdString(g.id));
    groups_->setItem(row, 0, label);
    auto *color = new QPushButton(groups_);
    color->setStyleSheet(swatch(g.style));
    connect(color, &QPushButton::clicked, this, [this, id = g.id] {
      target_ = id;
      armed_ = false;
      load_editor();
      refresh_groups();
    });
    groups_->setCellWidget(row, 1, color);
    auto *references =
        new ui::ReferenceCellItem(tr("%1 ploch · vybrat…").arg(g.faces.size()));
    references->set_reference(QString::fromStdString(g.id));
    references->set_active_input(armed_ && target_ == g.id);
    references->set_inspected(inspected_.contains(g.id));
    groups_->setItem(row, 2, references);
    auto *eye = ui::build_reference_inspection_button(
        !g.faces.empty(), inspected_.contains(g.id),
        [this, id = g.id](bool on) {
          if (on)
            inspected_.insert(id);
          else
            inspected_.erase(id);
          inspect_groups();
          refresh_groups();
        });
    groups_->setCellWidget(row, 3, ui::centered_cell_widget(eye));
    auto *remove = new QPushButton("×", groups_);
    connect(remove, &QPushButton::clicked, this, [this, id = g.id] {
      std::erase_if(value_.groups, [&](const auto &x) { return x.id == id; });
      inspected_.erase(id);
      if (target_ == id) {
        target_.clear();
        armed_ = false;
      }
      refresh_groups();
      inspect_groups();
      load_editor();
      publish();
    });
    groups_->setCellWidget(row, 4, remove);
  }
  refreshing_ = false;
}
void AppearanceDialog::select_face(const viewer::ViewerCandidate &face) {
  if (!armed_ || target_.empty() || face.kind != viewer::CandidateKind::Face ||
      face.geometry != viewer::CandidateGeometry::Display)
    return;
  const auto key = face.owner_id + "::" + face.semantic_key;
  for (auto &g : value_.groups)
    std::erase(g.faces, key);
  for (auto &g : value_.groups)
    if (g.id == target_)
      g.faces.push_back(key);
  refresh_groups();
  inspect_groups();
  publish();
}
void AppearanceDialog::inspect_groups() {
  std::vector<std::string> faces;
  for (const auto &g : value_.groups)
    if (inspected_.contains(g.id))
      faces.insert(faces.end(), g.faces.begin(), g.faces.end());
  if (inspect_)
    inspect_(faces);
}
void AppearanceDialog::end_entry() {
  armed_ = false;
  inspected_.clear();
  inspect_groups();
  refresh_groups();
}
bool AppearanceDialog::submit() {
  if (!QColor(color_->text()).isValid())
    throw std::invalid_argument("Neplatná barva");
  document::validate_appearance(value_);
  commit_(value_, palette_);
  end_entry();
  return true;
}
} // namespace zima::app
