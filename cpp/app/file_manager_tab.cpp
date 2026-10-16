#include "file_manager_tab.hpp"

#include "file_dialog.hpp"
#include "directory_metadata.hpp"
#include "file_manager_includes.hpp"
#include "file_entry_geometry.hpp"
#include "thumbnail_image.hpp"
#include "resource_icon.hpp"
#include "native_file_icon.hpp"
#include "path_name_dialog.hpp"
#include "file_manager_filters.hpp"
#include "tool_button_style.hpp"

#include <zima/ui/properties_subwindow.hpp>

#include <QAction>
#include <QApplication>
#include <QCheckBox>
#include <QComboBox>
#include <QDateTime>
#include <QDebug>
#include <QDesktopServices>
#include <QDialogButtonBox>
#include <QDir>
#include <QDirIterator>
#include <QFile>
#include <QFileIconProvider>
#include <QFileInfo>
#include <QFileSystemModel>
#include <QFormLayout>
#include <QGridLayout>
#include <QGroupBox>
#include <QHash>
#include <QImageReader>
#include <QJsonArray>
#include <QJsonDocument>
#include <QPixmapCache>
#include <QHeaderView>
#include <QHBoxLayout>
#include <QLabel>
#include <QLineEdit>
#include <QListWidget>
#include <QLocale>
#include <QMenu>
#include <QMessageBox>
#include <QMouseEvent>
#include <QTimer>
#include <QSignalBlocker>
#include <QSlider>
#include <QHelpEvent>
#include <QToolTip>
#include <QBuffer>
#include <QCryptographicHash>
#include <QPainter>
#include <QPlainTextEdit>
#include <QTextDocument>
#include <QPushButton>
#include <QRegularExpression>
#include <QSaveFile>
#include <QScrollArea>
#include <QSet>
#include <QSettings>
#include <QSortFilterProxyModel>
#include <QSplitter>
#include <QStackedWidget>
#include <QStyledItemDelegate>
#include <QTabWidget>
#include <QTableWidget>
#include <QToolBar>
#include <QToolButton>
#include <QScrollBar>
#include <QTreeView>
#include <QTreeWidget>
#include <QUrl>
#include <QUuid>
#include <QVBoxLayout>
#include <QWebEngineSettings>
#include <QWebEnginePage>
#include <QWebEngineScript>
#include <QWebEngineScriptCollection>
#include <QWebEngineView>

#include <algorithm>
namespace zima::app {
namespace {

QString directory_label(const QString& directory, const QString& language);
QString column_settings_path(const ApplicationSettings& settings);

enum DirectoryBrandingRole {
    BrandingPathRole = Qt::UserRole + 400,
    BrandingModeRole,
};

enum class DirectoryBrandingMode { None, LogoOnly, LogoWithText };

struct DirectoryBranding {
    DirectoryBrandingMode mode{DirectoryBrandingMode::None};
    QString path;
};

DirectoryBranding directory_branding(const QString& directory) {
    const QDir index(QDir(directory).filePath(QStringLiteral("0000-index")));
    QSettings metadata(index.filePath(QStringLiteral("metadata.ini")),QSettings::IniFormat);
    const QString configured_mode=metadata.value(QStringLiteral("Directory/LogoMode")).toString().trimmed().toLower();
    if(configured_mode==QStringLiteral("none"))return {};
    const QString configured_file=metadata.value(QStringLiteral("Directory/Logo")).toString().trimmed();
    if(!configured_file.isEmpty()){
        const QString candidate=index.filePath(configured_file);
        if(QFileInfo::exists(candidate))return {configured_mode==QStringLiteral("logo-text")?DirectoryBrandingMode::LogoWithText:DirectoryBrandingMode::LogoOnly,candidate};
    }
    const auto find=[&](const QString& base){for(const auto& suffix:{QStringLiteral("svg"),QStringLiteral("png")}){const QString candidate=index.filePath(base+QLatin1Char('.')+suffix);if(QFileInfo::exists(candidate))return candidate;}return QString{};};
    if(configured_mode==QStringLiteral("logo")){const QString candidate=find(QStringLiteral("logo"));return candidate.isEmpty()?DirectoryBranding{}:DirectoryBranding{DirectoryBrandingMode::LogoOnly,candidate};}
    if(configured_mode==QStringLiteral("logo-text")){const QString candidate=find(QStringLiteral("logo-text"));return candidate.isEmpty()?DirectoryBranding{}:DirectoryBranding{DirectoryBrandingMode::LogoWithText,candidate};}
    if(const QString candidate=find(QStringLiteral("logo"));!candidate.isEmpty())return {DirectoryBrandingMode::LogoOnly,candidate};
    if(const QString candidate=find(QStringLiteral("logo-text"));!candidate.isEmpty())return {DirectoryBrandingMode::LogoWithText,candidate};
    return {};
}

QString localized_index_file(const QString& directory, QString language) {
    const QDir index(QDir(directory).filePath(QStringLiteral("0000-index")));
    language.replace(QLatin1Char('-'), QLatin1Char('_'));
    QStringList languages;
    if (!language.isEmpty()) languages << language;
    const QString short_language = language.left(2);
    if (!short_language.isEmpty() && !languages.contains(short_language)) languages << short_language;
    QStringList names;
    for (const auto& selected : languages) {
        names << QStringLiteral("index_%1.html").arg(selected)
              << QStringLiteral("index_%1.htm").arg(selected);
    }
    names << QStringLiteral("index.html") << QStringLiteral("index.htm");
    for (const auto& name : names) {
        const QString candidate = index.filePath(name);
        if (QFileInfo(candidate).isFile() && QFileInfo(candidate).isReadable()) return candidate;
    }
    return {};
}

class ColumnFilterHeader final : public QHeaderView {
public:
    explicit ColumnFilterHeader(QWidget* parent):QHeaderView(Qt::Horizontal,parent){
        setDefaultAlignment(Qt::AlignTop|Qt::AlignHCenter);
        connect(this,&QHeaderView::sectionResized,this,[this]{layout_fields();});
        connect(this,&QHeaderView::sectionMoved,this,[this]{layout_fields();});
        connect(this,&QHeaderView::geometriesChanged,this,[this]{layout_fields();});
        if(auto* view=qobject_cast<QAbstractItemView*>(parent))
            connect(view->horizontalScrollBar(),&QScrollBar::valueChanged,this,[this]{layout_fields();});
    }
    std::function<void()> filters_changed;
    void configure(const QStringList& headers){
        if(headers_==headers)return;headers_=headers;
        for(auto* edit:fields_)delete edit;fields_.clear();filters_.clear();fields_.resize(headers.size());filters_.resize(headers.size());
        for(int column=0;column<headers.size();++column){
            if(column==file_preview_column)continue;
            auto* edit=new QLineEdit(this);edit->setObjectName(QStringLiteral("fileManagerColumnFilter_%1").arg(column));edit->setPlaceholderText(tr("Filtrovat…"));edit->setClearButtonEnabled(true);fields_[column]=edit;
            connect(edit,&QLineEdit::textChanged,this,[this,column](const QString& text){filters_[column]=text;if(filters_changed)filters_changed();});
        }
        updateGeometry();layout_fields();
    }
    [[nodiscard]] QString filter(int column)const{return column>=0&&column<filters_.size()?filters_.at(column):QString{};}
    QSize sizeHint()const override{auto size=QHeaderView::sizeHint();size.setHeight(size.height()+28);return size;}
protected:
    void showEvent(QShowEvent* event)override{QHeaderView::showEvent(event);layout_fields();}
private:
    QStringList headers_;QVector<QLineEdit*> fields_;QVector<QString> filters_;
    void layout_fields(){const int top=QHeaderView::sizeHint().height();for(int column=0;column<fields_.size();++column)if(auto* edit=fields_[column]){edit->setGeometry(sectionViewportPosition(column)+2,top, std::max(12,sectionSize(column)-4),std::max(22,height()-top-2));edit->setVisible(!isSectionHidden(column));}}
};

class DirectoryTreeFilter final : public QSortFilterProxyModel {
public:
    DirectoryTreeFilter(QString language,QStringList global_filters,QObject* parent)
        :QSortFilterProxyModel(parent),language_(std::move(language)),global_filters_(std::move(global_filters)){}
    QVariant data(const QModelIndex& index,int role=Qt::DisplayRole)const override{
        if(index.column()==0){
            const auto* files=qobject_cast<QFileSystemModel*>(sourceModel());
            if(files){const QFileInfo info=files->fileInfo(mapToSource(index));if(info.isDir()){
                const QString path=info.absoluteFilePath();
                auto branded=branding_.constFind(path);if(branded==branding_.cend())branded=branding_.insert(path,directory_branding(path));
                if(role==BrandingPathRole)return branded->path;
                if(role==BrandingModeRole)return static_cast<int>(branded->mode);
                if(role==Qt::DecorationRole&&!branded->path.isEmpty())return QIcon(branded->path);
                if(role==Qt::DisplayRole||role==Qt::AccessibleTextRole){
                    const auto cached=labels_.constFind(path);const QString label=cached!=labels_.cend()?cached.value():directory_label(path,language_);
                    if(cached==labels_.cend())labels_.insert(path,label);
                    if(role==Qt::DisplayRole&&branded->mode==DirectoryBrandingMode::LogoOnly)return QString{};
                    return label;
                }
                if(role==Qt::ToolTipRole)return directory_label(path,language_)+QLatin1Char('\n')+QDir::toNativeSeparators(path);
            }}
        }
        return QSortFilterProxyModel::data(index,role);
    }
    void clear_label_cache(){labels_.clear();branding_.clear();filters_.clear();invalidate();}
protected:
    bool lessThan(const QModelIndex& left,const QModelIndex& right)const override {
        const auto* files=qobject_cast<QFileSystemModel*>(sourceModel());
        if(files&&left.column()==0)return QString::localeAwareCompare(files->fileName(left),files->fileName(right))<0;
        return QSortFilterProxyModel::lessThan(left,right);
    }
    bool filterAcceptsRow(int row,const QModelIndex& parent)const override {
        const auto* files=qobject_cast<QFileSystemModel*>(sourceModel());
        if(!files)return QSortFilterProxyModel::filterAcceptsRow(row,parent);
        const QString directory=files->filePath(parent);auto filter=filters_.constFind(directory);if(filter==filters_.cend())filter=filters_.insert(directory,directory_file_filter(directory,global_filters_));
        return (*filter)(files->fileName(files->index(row,0,parent)));
    }
private:
    QString language_;
    QStringList global_filters_;
    mutable QHash<QString,std::function<bool(const QString&)>> filters_;
    mutable QHash<QString,QString> labels_;
    mutable QHash<QString,DirectoryBranding> branding_;
};

class DirectoryTreeDelegate final : public QStyledItemDelegate {
public:
    using QStyledItemDelegate::QStyledItemDelegate;
    QSize sizeHint(const QStyleOptionViewItem& option,const QModelIndex& index)const override{
        auto size=QStyledItemDelegate::sizeHint(option,index);size.setHeight(std::max(size.height(),38));return size;
    }
    void paint(QPainter* painter,const QStyleOptionViewItem& option,const QModelIndex& index)const override{
        const auto mode=static_cast<DirectoryBrandingMode>(index.data(BrandingModeRole).toInt());
        if(mode!=DirectoryBrandingMode::LogoOnly){QStyledItemDelegate::paint(painter,option,index);return;}
        QStyleOptionViewItem background(option);initStyleOption(&background,index);background.text.clear();background.icon=QIcon();
        const QWidget* widget=option.widget;const QStyle* style=widget?widget->style():QApplication::style();
        style->drawControl(QStyle::CE_ItemViewItem,&background,painter,widget);
        const QIcon logo(index.data(BrandingPathRole).toString());
        if(logo.isNull())return;
        const QSize available(std::min(160,std::max(1,option.rect.width()-8)),30);
        const QPixmap pixmap=logo.pixmap(available);
        const QRect target(QPoint(option.rect.left()+4,option.rect.center().y()-pixmap.height()/2),pixmap.size());
        painter->drawPixmap(target,pixmap);
    }
};

QString clean_path(const QString& path) {
    return QDir::cleanPath(QFileInfo(path).absoluteFilePath());
}

bool path_belongs_to(const QString& path, const QString& root) {
    const QString clean = clean_path(path);
    const QString base = clean_path(root);
#ifdef Q_OS_WIN
    constexpr auto sensitivity = Qt::CaseInsensitive;
#else
    constexpr auto sensitivity = Qt::CaseSensitive;
#endif
    return clean.compare(base, sensitivity) == 0 ||
        clean.startsWith(base + QLatin1Char('/'), sensitivity);
}

QString metadata_path(const QString& directory) {
    return QDir(directory).filePath(QStringLiteral("0000-index/metadata.ini"));
}

bool directory_prevents_removal(const QString& directory) {
    QSettings metadata(metadata_path(directory), QSettings::IniFormat);
    metadata.setFallbacksEnabled(false);
    return metadata.value(QStringLiteral("Directory/PreventRemoval"), false).toBool();
}

QString ancestor_removal_lock(QString directory){
    for(;;){if(directory_prevents_removal(directory))return directory;QDir parent(directory);if(!parent.cdUp()||parent.absolutePath()==directory)break;directory=parent.absolutePath();}return {};
}

bool directory_prevents_parameter_edit(QString directory){
    for(;;){QSettings metadata(metadata_path(directory),QSettings::IniFormat);if(metadata.value("Directory/PreventParameterEdit",false).toBool())return true;QDir parent(directory);if(!parent.cdUp()||parent.absolutePath()==directory)break;directory=parent.absolutePath();}return false;
}

QString removal_lock(const QFileInfo& entry) {
    const QString parent = entry.absolutePath();
    if (!entry.isDir() || entry.isSymLink())
        return ancestor_removal_lock(parent);

    const QString directory = entry.absoluteFilePath();
    if(const QString locked=ancestor_removal_lock(directory);!locked.isEmpty())return locked;
    if (entry.fileName().compare(QStringLiteral("0000-index"), Qt::CaseInsensitive) == 0
        && directory_prevents_removal(parent)) return parent;

    QDirIterator children(directory,
        QDir::Dirs | QDir::NoDotAndDotDot | QDir::Hidden | QDir::System | QDir::NoSymLinks,
        QDirIterator::Subdirectories);
    while (children.hasNext()) {
        const QString child = children.next();
        if (directory_prevents_removal(child)) return child;
    }
    return {};
}

QString metadata_string(QSettings& metadata,const QString& key){
    QVariant value=metadata.value(key);
    if(!value.isValid()){
        // Earlier CAD-created metadata escaped key separators literally.
        // Standard QSettings hierarchy is authoritative; retain authored values
        // from those files until they are explicitly edited through this UI.
        QString previous=key;
        if(metadata.group()==QStringLiteral("Parts"))previous.replace(QLatin1Char('/'),QLatin1Char('\\'));
        else if(previous.startsWith(QStringLiteral("Parts/")))previous=QStringLiteral("Parts/")+previous.mid(6).replace(QLatin1Char('/'),QLatin1Char('\\'));
        else {const qsizetype separator=previous.lastIndexOf(QLatin1Char('/'));if(separator>previous.indexOf(QLatin1Char('/')))previous[separator]=QLatin1Char('\\');}
        if(previous!=key)value=metadata.value(previous);
    }
    return value.metaType().id()==QMetaType::QStringList?value.toStringList().join(QStringLiteral(", ")):value.toString();
}

QStringList metadata_parameter_ids(QSettings& metadata){
    QStringList ids;for(const auto& value:metadata.value(QStringLiteral("Directory/Parameters")).toStringList())for(QString id:value.split(QLatin1Char(','),Qt::SkipEmptyParts)){id=id.trimmed();if(!id.isEmpty()&&!ids.contains(id))ids.push_back(id);}return ids;
}

QString translated_value(QSettings& metadata, const QString& base,
                         const QString& language) {
    const QString selected = metadata_string(metadata,base + QLatin1Char('/') + language).trimmed();
    if (!selected.isEmpty()) return selected;
    for (const auto& fallback : {QStringLiteral("cs"), QStringLiteral("en"),
                                QStringLiteral("de"), QStringLiteral("fr"), QStringLiteral("ru")}) {
        const QString value = metadata_string(metadata,base + QLatin1Char('/') + fallback).trimmed();
        if (!value.isEmpty()) return value;
    }
    return metadata_string(metadata,base).trimmed();
}

QString directory_label(const QString& directory, const QString& language) {
    return localized_directory_label(directory, language);
}

QString first_existing_image(const QString& directory, const QString& preferred,
                             const QString& language, const QStringList& bases) {
    const QDir index(QDir(directory).filePath(QStringLiteral("0000-index")));
    if (!preferred.trimmed().isEmpty()) {
        const QString candidate = index.filePath(preferred);
        if (QFileInfo::exists(candidate)) return candidate;
    }
    const QStringList suffixes{QStringLiteral("png"),QStringLiteral("jpg"),
        QStringLiteral("jpeg"),QStringLiteral("webp"),QStringLiteral("gif"),QStringLiteral("svg")};
    for (const auto& base : bases) {
        for (const auto& localized : {base + QLatin1Char('_') + language, base}) {
            for (const auto& suffix : suffixes) {
                const QString candidate=index.filePath(localized+QLatin1Char('.')+suffix);
                if(QFileInfo::exists(candidate))return candidate;
            }
        }
    }
    return {};
}

QIcon compact_preview_icon(const QString& path) {
    if(path.isEmpty())return {};
    if(QFileInfo(path).suffix().compare("svg",Qt::CaseInsensitive)==0)return QIcon(path);
    const QFileInfo file(path);
    // Hash small previews themselves: same-size replacement or preserved file
    // timestamps must not reuse an old picture. Large images decode on demand.
    QByteArray bytes;
    if(file.size()<=1024*1024){QFile input(path);if(input.open(QIODevice::ReadOnly))bytes=input.readAll();}
    const QString key=bytes.isEmpty()?QString{}:"zima-preview:"+file.absoluteFilePath()+":"+
        QString::fromLatin1(QCryptographicHash::hash(bytes,QCryptographicHash::Sha256).toHex());
    QPixmap pixmap;
    if(key.isEmpty()||!QPixmapCache::find(key,&pixmap)) {
        QBuffer buffer(&bytes);if(!bytes.isEmpty())buffer.open(QIODevice::ReadOnly);
        QImageReader reader;
        if(bytes.isEmpty())reader.setFileName(path);else reader.setDevice(&buffer);
        const auto size=reader.size();
        if(size.width()>320||size.height()>240)reader.setScaledSize(size.scaled(320,240,Qt::KeepAspectRatio));
        const auto image=compact_thumbnail_image(reader.read());
        if(image.isNull())return QIcon(path);
        pixmap=QPixmap::fromImage(image);if(!key.isEmpty())QPixmapCache::insert(key,pixmap);
    }
    return QIcon(pixmap);
}

bool legacy_project(const QString& directory) {
    const QDir index(QDir(directory).filePath(QStringLiteral("0000-index")));
    const bool html = !index.entryList({QStringLiteral("index_*.html"),QStringLiteral("index.html")},QDir::Files).isEmpty();
    const bool image = !first_existing_image(directory,{},QStringLiteral("cs"),{QStringLiteral("01")}).isEmpty();
    return html && image;
}

bool is_project_directory(const QString& directory) {
    QSettings metadata(metadata_path(directory), QSettings::IniFormat);
    return metadata.value(QStringLiteral("Directory/Kind")).toString().compare(
        QStringLiteral("project"),Qt::CaseInsensitive)==0 || legacy_project(directory);
}

QString logical_file_id(const QFileInfo& info) {
    const auto name=info.fileName();
    const QRegularExpression proe(QStringLiteral(R"(^(.+)\.(?:prt|asm|drw)\.\d+$)"),QRegularExpression::CaseInsensitiveOption);
    const auto match=proe.match(name);if(match.hasMatch())return match.captured(1);
    const QString suffix=info.suffix().toLower();
    if(QStringList{"prtz","asmz","drwz","frmz","tblz","symz","matz"}.contains(suffix))return info.completeBaseName();
    return name;
}

bool is_version_file(const QFileInfo& info) {
    static const QRegularExpression version(QStringLiteral(R"(\.(?:prt|asm|drw|prtz|asmz|drwz|frmz|tblz|symz)\.\d+$)"),QRegularExpression::CaseInsensitiveOption);
    return version.match(info.fileName()).hasMatch();
}

bool copy_path(const QString& source, const QString& destination_directory, QString* error) {
    const QFileInfo from(source);
    const QString target=QDir(destination_directory).filePath(from.fileName());
    if(QFileInfo::exists(target)){
        if(error)*error=QObject::tr("Cíl již existuje: %1").arg(target);return false;
    }
    if(from.isFile()){
        if(QFile::copy(source,target))return true;
        if(error)*error=QObject::tr("Soubor nelze zkopírovat: %1").arg(source);return false;
    }
    if(!from.isDir()||!QDir().mkpath(target)){
        if(error)*error=QObject::tr("Adresář nelze zkopírovat: %1").arg(source);return false;
    }
    const QDir input(source);
    const auto entries=input.entryInfoList(QDir::Dirs|QDir::Files|QDir::NoDotAndDotDot|QDir::Hidden|QDir::System);
    for(const auto& entry:entries)if(!copy_path(entry.absoluteFilePath(),target,error)){
        QDir(target).removeRecursively();return false;
    }
    return true;
}

bool atomic_metadata_update(const QString& path, const QMap<QString,QVariant>& values, QString* error) {
    const QFileInfo target(path);if(!QDir().mkpath(target.absolutePath())){if(error)*error=QObject::tr("Adresář metadat nelze vytvořit.");return false;}
    QSettings output(path,QSettings::IniFormat);output.setAtomicSyncRequired(true);
    for(auto it=values.cbegin();it!=values.cend();++it)output.setValue(it.key(),it.value());output.sync();
    if(output.status()!=QSettings::NoError){
        if(error)*error=QObject::tr("Metadata nelze atomicky uložit.");return false;
    }
    return true;
}

struct ProjectValues {QString number,title,customer,description,preview,logo;};

class FileFiltersDialog final : public zima::ui::PropertiesSubWindow {
public:
  FileFiltersDialog(const QString &directory,
                    const QMap<QString, QVariant> &policy,
                    const QStringList &global, bool writable, QWidget *parent)
      : PropertiesSubWindow(QObject::tr("File filters"), parent) {
    setObjectName("fileManagerFiltersDialog");
    setProperty("expandBottomTable", true);
    auto *path = new QLabel(QDir::toNativeSeparators(directory), this);
    path->setWordWrap(true);
    content_layout()->addWidget(path);
    inherit_ = new QCheckBox(QObject::tr("Use global filters"), this);
    inherit_->setObjectName("fileManagerInheritGlobalFilters");
    inherit_->setChecked(policy.value("Filters/InheritGlobal", true).toBool());
    inherit_->setEnabled(writable);
    content_layout()->addWidget(inherit_);
    auto *inherited = new QLabel(
        QObject::tr("Global filters: %1").arg(global.join(", ")), this);
    inherited->setWordWrap(true);
    content_layout()->addWidget(inherited);
    auto *label = new QLabel(QObject::tr("Hidden names and patterns"), this);
    content_layout()->addWidget(label);
    patterns_ = new QPlainTextEdit(
        file_pattern_list(policy.value("Filters/Hide")).join('\n'), this);
    patterns_->setObjectName("fileManagerHiddenPatterns");
    patterns_->setReadOnly(!writable);
    patterns_->setToolTip(
        QObject::tr("One name or wildcard pattern per line. Matches files and "
                    "folders, without case sensitivity."));
    content_layout()->addWidget(patterns_, 1);
    label->setBuddy(patterns_);
    buttons()->button(QDialogButtonBox::Ok)->setEnabled(writable);
    set_initial_size({650, 450});
  }
  std::function<void(QStringList, bool)> saved;

protected:
  bool submit() override {
    QStringList hidden;
    for (const auto &pattern : patterns_->toPlainText().split('\n'))
      if (!pattern.trimmed().isEmpty() && !hidden.contains(pattern.trimmed()))
        hidden << pattern.trimmed();
    if (saved)
      saved(hidden, inherit_->isChecked());
    return true;
  }

private:
  QCheckBox *inherit_{};
  QPlainTextEdit *patterns_{};
};

class ProjectEditor final : public zima::ui::PropertiesSubWindow {
public:
  ProjectEditor(ProjectValues values, const QString &directory,
                const ApplicationSettings &settings, QWidget *parent)
      : PropertiesSubWindow(QObject::tr("Projektový přehled"), parent),
        values_(std::move(values)), directory_(directory), settings_(settings) {
    setObjectName("fileManagerProjectEditor");
    auto *form = new QFormLayout;
    number_ = new QLineEdit(values_.number, this);
    title_ = new QLineEdit(values_.title, this);
    customer_ = new QLineEdit(values_.customer, this);
    description_ = new QPlainTextEdit(values_.description, this);
    description_->setMaximumHeight(100);
    preview_ = new QLineEdit(values_.preview, this);
    logo_ = new QLineEdit(values_.logo, this);
    const auto image_row = [&](QLineEdit *edit) {
      auto *row = new QWidget(this);
      auto *layout = new QHBoxLayout(row);
      layout->setContentsMargins(0, 0, 0, 0);
      layout->addWidget(edit, 1);
      auto *browse = new QPushButton(QObject::tr("Procházet…"), row);
      layout->addWidget(browse);
      connect(browse, &QPushButton::clicked, this,
              [this, edit] {
                const QString path=open_file(this,QObject::tr("Vybrat obrázek"),directory_,QObject::tr("Obrázky (*.svg *.png *.jpg *.jpeg *.webp)"),settings_.translations);
                if(!path.isEmpty())
                  edit->setText(path);
              });
      return row;
    };
    form->addRow(QObject::tr("Číslo projektu"), number_);
    form->addRow(QObject::tr("Název projektu"), title_);
    form->addRow(QObject::tr("Zákazník"), customer_);
    form->addRow(QObject::tr("Popis"), description_);
    form->addRow(QObject::tr("Hlavní obrázek"), image_row(preview_));
    form->addRow(QObject::tr("Logo projektu"), image_row(logo_));
    content_layout()->addLayout(form);
    set_initial_size({680, 0});
  }
  ProjectValues result() const { return values_; }
protected:
    bool submit()override{
        if(title_->text().trimmed().isEmpty()){QMessageBox::warning(this,QObject::tr("Projektový přehled"),QObject::tr("Zadejte název projektu."));return false;}
        values_={number_->text().trimmed(),title_->text().trimmed(),customer_->text().trimmed(),description_->toPlainText().trimmed(),preview_->text().trimmed(),logo_->text().trimmed()};return true;
    }
private:
    ProjectValues values_;QString directory_;ApplicationSettings settings_;QLineEdit *number_{},*title_{},*customer_{},*preview_{},*logo_{};QPlainTextEdit* description_{};
};

struct DirectoryParameterValues {
    QString id;
    QMap<QString,QString> labels;
};

struct DirectoryValues {
    QMap<QString,QString> labels;
    bool subdirectories_as_parts{true};
    bool auto_index{true};
    bool prevent_removal{};
    bool prevent_parameter_edit{};
    DirectoryBrandingMode branding{DirectoryBrandingMode::None};
    QString logo;
    QVector<DirectoryParameterValues> parameters;
    QStringList include_parameters,include_thumbnails;
};

class DirectoryIncludeEditor final : public QWidget {
public:
    DirectoryIncludeEditor(const QString& directory,const QStringList& links,const ApplicationSettings& settings,QWidget* parent)
        :QWidget(parent),directory_(directory){
        setSizePolicy(QSizePolicy::Preferred,QSizePolicy::Fixed);
        auto* layout=new QVBoxLayout(this);layout->setContentsMargins(0,0,0,0);layout->setSpacing(4);
        auto* row=new QHBoxLayout;paths_=new QPlainTextEdit(this);paths_->document()->setDocumentMargin(2);paths_->setLineWrapMode(QPlainTextEdit::NoWrap);paths_->setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);paths_->setVerticalScrollBarPolicy(Qt::ScrollBarAlwaysOff);paths_->setPlainText(links.join('\n'));paths_->setFixedHeight(paths_->fontMetrics().lineSpacing()+2*paths_->frameWidth()+qRound(2*paths_->document()->documentMargin()));paths_->setToolTip(QObject::tr("Jeden zdrojový adresář na řádek. Relativní cesty se vztahují k tomuto adresáři."));row->addWidget(paths_,1);
        auto* browse=new QPushButton(QObject::tr("Procházet…"),this);browse->setObjectName("includeBrowse");row->addWidget(browse);
        auto* clear=new QToolButton(this);clear->setObjectName("includeClear");clear->setIcon(resource_icon("delete"));clear->setToolTip(QObject::tr("Zrušit odkazy na zdrojové adresáře"));row->addWidget(clear);layout->addLayout(row);
        resolved_=new QLabel(this);resolved_->setObjectName("includeResolved");resolved_->setTextFormat(Qt::PlainText);resolved_->setWordWrap(true);layout->addWidget(resolved_);
        connect(clear,&QToolButton::clicked,paths_,&QPlainTextEdit::clear);
        connect(browse,&QPushButton::clicked,this,[this,settings]{const auto selected=choose_directory(this,QObject::tr("Vybrat zdrojový adresář"),directory_,settings.translations);if(selected.isEmpty())return;auto links=paths();const auto relative=QDir(directory_).relativeFilePath(selected);if(!links.contains(relative))links.push_back(relative);paths_->setPlainText(links.join('\n'));});
        connect(paths_,&QPlainTextEdit::textChanged,this,[this,clear]{QStringList resolved;for(const auto& link:paths())resolved.push_back(QDir::toNativeSeparators(include_directory_path(directory_,link)));resolved_->setText(resolved.join('\n'));resolved_->setVisible(!resolved.isEmpty());clear->setEnabled(!paths().isEmpty());});
        emit paths_->textChanged();
    }
    QStringList paths()const{QStringList result;for(const auto& line:paths_->toPlainText().split('\n')){const auto path=line.trimmed();if(!path.isEmpty())result.push_back(QDir(directory_).relativeFilePath(include_directory_path(directory_,path)));}result.removeDuplicates();return result;}
private:
    QString directory_;QPlainTextEdit* paths_{};QLabel* resolved_{};
};

class DirectoryEditor final : public zima::ui::PropertiesSubWindow {
public:
    DirectoryEditor(const QString& directory,const ApplicationSettings& settings,QWidget* parent)
        :PropertiesSubWindow(QObject::tr("Vlastnosti adresáře"),parent),directory_(directory),settings_(settings){
        setObjectName("fileManagerDirectoryEditor");setProperty("expandBottomTable",true);
        QSettings metadata(metadata_path(directory_),QSettings::IniFormat);
        auto* location=new QLineEdit(QDir::toNativeSeparators(directory_),this);location->setObjectName("fileManagerDirectoryPath");location->setReadOnly(true);location->setCursorPosition(0);location->setToolTip(location->text());
        auto* location_form=new QFormLayout;location_form->addRow(QObject::tr("Adresář"),location);content_layout()->addLayout(location_form);
        auto* tabs=new QTabWidget(this);tabs->setObjectName("fileManagerDirectoryPropertiesTabs");
        auto* general=new QWidget(tabs);auto* general_form=new QFormLayout(general);general_form->setFormAlignment(Qt::AlignTop);
        for(const auto& language:{QStringLiteral("cs"),QStringLiteral("en"),QStringLiteral("de"),QStringLiteral("fr"),QStringLiteral("ru")}){
            auto* edit=new QLineEdit(metadata_string(metadata,QStringLiteral("Directory/Label/%1").arg(language)),general);edit->setObjectName(QStringLiteral("fileManagerDirectoryLabel_%1").arg(language));labels_.insert(language,edit);general_form->addRow(language.toUpper(),edit);
        }
        subdirectories_=new QCheckBox(QObject::tr("Zobrazovat podadresáře v záložce Soubory"),general);subdirectories_->setObjectName("fileManagerDirectorySubdirectories");subdirectories_->setChecked(metadata.value("Directory/SubdirectoriesAsParts",true).toBool());general_form->addRow(subdirectories_);
        auto_index_=new QCheckBox(QObject::tr("Vytvořit automatický přehled, když chybí HTML"),general);auto_index_->setObjectName("fileManagerDirectoryAutoIndex");auto_index_->setChecked(metadata.value("Directory/AutoIndex",true).toBool());general_form->addRow(auto_index_);
        prevent_removal_=new QCheckBox(QObject::tr("Chránit obsah adresáře před smazáním a přesunutím"),general);prevent_removal_->setObjectName("fileManagerDirectoryPreventRemoval");prevent_removal_->setChecked(metadata.value("Directory/PreventRemoval",false).toBool());general_form->addRow(prevent_removal_);
        prevent_parameter_edit_=new QCheckBox(QObject::tr("Zakázat úpravy parametrů"),general);prevent_parameter_edit_->setObjectName("fileManagerDirectoryPreventParameterEdit");prevent_parameter_edit_->setChecked(metadata.value("Directory/PreventParameterEdit",false).toBool());prevent_parameter_edit_->setToolTip(QObject::tr("Ochrana úprav parametrů platí také pro podadresáře."));general_form->addRow(prevent_parameter_edit_);
        include_parameters_=new DirectoryIncludeEditor(directory_,metadata.value("Directory/IncludeParameters").toStringList(),settings_,general);include_parameters_->setObjectName("fileManagerIncludeParameters");general_form->addRow(QObject::tr("Parametry ze zdrojových adresářů"),include_parameters_);
        include_thumbnails_=new DirectoryIncludeEditor(directory_,metadata.value("Directory/IncludeThumbnails").toStringList(),settings_,general);include_thumbnails_->setObjectName("fileManagerIncludeThumbnails");general_form->addRow(QObject::tr("Náhledy ze zdrojových adresářů"),include_thumbnails_);tabs->addTab(general,QObject::tr("Obecné"));

        auto* appearance=new QWidget(tabs);auto* appearance_form=new QFormLayout(appearance);
        branding_=new QComboBox(appearance);branding_->setObjectName("fileManagerDirectoryLogoMode");branding_->addItem(QObject::tr("Bez loga"),static_cast<int>(DirectoryBrandingMode::None));branding_->addItem(QObject::tr("Pouze logo"),static_cast<int>(DirectoryBrandingMode::LogoOnly));branding_->addItem(QObject::tr("Logo a název"),static_cast<int>(DirectoryBrandingMode::LogoWithText));
        const auto current_branding=directory_branding(directory_);branding_->setCurrentIndex(branding_->findData(static_cast<int>(current_branding.mode)));appearance_form->addRow(QObject::tr("Zobrazení"),branding_);
        auto* logo_row=new QWidget(appearance);auto* logo_layout=new QHBoxLayout(logo_row);logo_layout->setContentsMargins(0,0,0,0);logo_=new QLineEdit(current_branding.path,logo_row);logo_->setObjectName("fileManagerDirectoryLogo");logo_layout->addWidget(logo_,1);auto* browse=new QPushButton(QObject::tr("Procházet…"),logo_row);browse->setObjectName("fileManagerDirectoryLogoBrowse");logo_layout->addWidget(browse);appearance_form->addRow(QObject::tr("Soubor loga"),logo_row);
        connect(browse,&QPushButton::clicked,this,[this]{const QString selected=open_file(this,QObject::tr("Vybrat logo"),directory_,QObject::tr("Loga (*.svg *.png)"),settings_.translations);if(!selected.isEmpty())logo_->setText(selected);});
        connect(branding_,&QComboBox::currentIndexChanged,this,[this,browse]{const bool enabled=branding_->currentData().toInt()!=static_cast<int>(DirectoryBrandingMode::None);logo_->setEnabled(enabled);browse->setEnabled(enabled);});tabs->addTab(appearance,QObject::tr("Vzhled"));

        auto* parameters_page=new QWidget(tabs);auto* parameters_layout=new QVBoxLayout(parameters_page);parameters_=new QTableWidget(parameters_page);parameters_->setObjectName("fileManagerDirectoryParameters");parameters_->setColumnCount(6);parameters_->setHorizontalHeaderLabels({QObject::tr("ID"),QStringLiteral("CS"),QStringLiteral("EN"),QStringLiteral("DE"),QStringLiteral("FR"),QStringLiteral("RU")});parameters_->horizontalHeader()->setSectionResizeMode(0,QHeaderView::ResizeToContents);for(int column=1;column<6;++column)parameters_->horizontalHeader()->setSectionResizeMode(column,QHeaderView::Stretch);parameters_->setSelectionBehavior(QAbstractItemView::SelectRows);parameters_->setSelectionMode(QAbstractItemView::SingleSelection);parameters_layout->addWidget(parameters_,1);
        const QStringList ids=metadata_parameter_ids(metadata);for(QString id:ids){id=id.trimmed();if(id.isEmpty())continue;const int row=parameters_->rowCount();parameters_->insertRow(row);parameters_->setItem(row,0,new QTableWidgetItem(id));int column=1;for(const auto& language:{QStringLiteral("cs"),QStringLiteral("en"),QStringLiteral("de"),QStringLiteral("fr"),QStringLiteral("ru")})parameters_->setItem(row,column++,new QTableWidgetItem(metadata_string(metadata,QStringLiteral("Parameters/%1/Label/%2").arg(id,language))));}
        auto* parameter_actions=new QHBoxLayout;auto* add=new QPushButton(resource_icon("new"),QObject::tr("Přidat"),parameters_page);auto* remove=new QPushButton(resource_icon("delete"),QObject::tr("Odebrat"),parameters_page);auto* up=new QPushButton(resource_icon("arrow-up"),QObject::tr("Nahoru"),parameters_page);auto* down=new QPushButton(resource_icon("arrow-down"),QObject::tr("Dolů"),parameters_page);parameter_actions->addWidget(add);parameter_actions->addWidget(remove);parameter_actions->addStretch();parameter_actions->addWidget(up);parameter_actions->addWidget(down);parameters_layout->addLayout(parameter_actions);
        connect(add,&QPushButton::clicked,this,[this]{const int row=parameters_->rowCount();parameters_->insertRow(row);for(int column=0;column<parameters_->columnCount();++column)parameters_->setItem(row,column,new QTableWidgetItem);parameters_->setCurrentCell(row,0);parameters_->editItem(parameters_->item(row,0));});connect(remove,&QPushButton::clicked,this,[this]{if(parameters_->currentRow()>=0)parameters_->removeRow(parameters_->currentRow());});
        const auto move_row=[this](int delta){const int row=parameters_->currentRow(),target=row+delta;if(row<0||target<0||target>=parameters_->rowCount())return;for(int column=0;column<parameters_->columnCount();++column){auto* first=parameters_->takeItem(row,column);auto* second=parameters_->takeItem(target,column);parameters_->setItem(row,column,second);parameters_->setItem(target,column,first);}parameters_->setCurrentCell(target,0);};connect(up,&QPushButton::clicked,this,[move_row]{move_row(-1);});connect(down,&QPushButton::clicked,this,[move_row]{move_row(1);});tabs->addTab(parameters_page,QObject::tr("Parametry"));
        const bool logo_enabled=branding_->currentData().toInt()!=static_cast<int>(DirectoryBrandingMode::None);logo_->setEnabled(logo_enabled);browse->setEnabled(logo_enabled);content_layout()->addWidget(tabs);set_initial_size({900,0});
    }
    [[nodiscard]] DirectoryValues result()const{return values_;}
protected:
    bool submit()override{
        DirectoryValues result;for(auto it=labels_.cbegin();it!=labels_.cend();++it)result.labels.insert(it.key(),it.value()->text().trimmed());result.subdirectories_as_parts=subdirectories_->isChecked();result.auto_index=auto_index_->isChecked();result.prevent_removal=prevent_removal_->isChecked();result.prevent_parameter_edit=prevent_parameter_edit_->isChecked();result.branding=static_cast<DirectoryBrandingMode>(branding_->currentData().toInt());result.logo=logo_->text().trimmed();
        result.include_parameters=include_parameters_->paths();result.include_thumbnails=include_thumbnails_->paths();
        for(const auto& entry:{std::pair{QStringLiteral("Directory/IncludeParameters"),result.include_parameters},std::pair{QStringLiteral("Directory/IncludeThumbnails"),result.include_thumbnails}}){const auto included=directory_includes(directory_,entry.first,&entry.second);if(!included.error.isEmpty()){QMessageBox::warning(this,QObject::tr("Vlastnosti adresáře"),included.error);return false;}}
        QSet<QString> ids;const QRegularExpression valid(QStringLiteral("^[A-Za-z0-9_.-]+$"));for(int row=0;row<parameters_->rowCount();++row){DirectoryParameterValues parameter;parameter.id=parameters_->item(row,0)?parameters_->item(row,0)->text().trimmed():QString{};if(parameter.id.isEmpty()||!valid.match(parameter.id).hasMatch()){QMessageBox::warning(this,QObject::tr("Vlastnosti adresáře"),QObject::tr("ID parametru smí obsahovat pouze písmena bez diakritiky, číslice, tečku, pomlčku a podtržítko."));return false;}const QString normalized=parameter.id.toCaseFolded();if(ids.contains(normalized)){QMessageBox::warning(this,QObject::tr("Vlastnosti adresáře"),QObject::tr("ID parametrů musí být jedinečná."));return false;}ids.insert(normalized);int column=1;for(const auto& language:{QStringLiteral("cs"),QStringLiteral("en"),QStringLiteral("de"),QStringLiteral("fr"),QStringLiteral("ru")})parameter.labels.insert(language,parameters_->item(row,column)?parameters_->item(row,column)->text().trimmed():QString{}),++column;result.parameters.push_back(std::move(parameter));}
        if(result.branding!=DirectoryBrandingMode::None&&(!QFileInfo(result.logo).isFile()||!QStringList{QStringLiteral("svg"),QStringLiteral("png")}.contains(QFileInfo(result.logo).suffix().toLower()))){QMessageBox::warning(this,QObject::tr("Vlastnosti adresáře"),QObject::tr("Vyberte existující logo ve formátu SVG nebo PNG."));return false;}values_=std::move(result);return true;
    }
private:
    QString directory_;ApplicationSettings settings_;QMap<QString,QLineEdit*> labels_;QCheckBox *subdirectories_{},*auto_index_{},*prevent_removal_{},*prevent_parameter_edit_{};QComboBox* branding_{};QLineEdit* logo_{};QTableWidget* parameters_{};DirectoryValues values_;DirectoryIncludeEditor *include_parameters_{},*include_thumbnails_{};
};

QString local_entry_thumbnail(const QFileInfo& info,const QString& directory,const QString& language,QSettings* supplied=nullptr){
    const QDir thumbnails(QDir(directory).filePath("0000-index/thumbnails"));
    QSettings local_metadata(metadata_path(directory),QSettings::IniFormat);QSettings& metadata=supplied?*supplied:local_metadata;
    const QString configured=metadata.value("Files/"+info.fileName()+"/Thumbnail",metadata.value("Files/"+logical_file_id(info)+"/Thumbnail")).toString();
    if(!configured.isEmpty()){const QString candidate=QDir(directory).filePath(configured);if(QFileInfo(candidate).isFile())return candidate;}
    if(QFileInfo::exists(document_thumbnail_path(info.absoluteFilePath())))return document_thumbnail_path(info.absoluteFilePath());
    for(const auto& suffix:{"svg","png","jpg","jpeg","webp","gif"}){
        const QString candidate=thumbnails.filePath(logical_file_id(info)+QLatin1Char('.')+suffix);if(QFileInfo(candidate).isFile())return candidate;
        // Parts strips the foreign format extension when matching previews.
        for(const auto& folder:{QDir(directory),thumbnails}){const QString fallback=folder.filePath(info.completeBaseName()+QLatin1Char('.')+suffix);if(QFileInfo(fallback).isFile()&&fallback!=info.absoluteFilePath())return fallback;}
    }
    if(info.isDir())return first_existing_image(info.absoluteFilePath(),{},language,{"01"});
    if(QStringList{"png","jpg","jpeg","webp","gif","svg"}.contains(info.suffix().toLower()))return info.absoluteFilePath();
    return {};
}

QString entry_thumbnail(const QFileInfo& info,const QString& directory,const QString& language,QSettings* supplied=nullptr,const QStringList* sources=nullptr){
    if(const auto local=local_entry_thumbnail(info,directory,language,supplied);!local.isEmpty())return local;
    const auto included=sources?DirectoryIncludes{*sources,{}}:directory_includes(directory,"Directory/IncludeThumbnails");
    for(int index=1;index<included.directories.size();++index){
        const auto& source=included.directories[index];
        if(const auto image=local_entry_thumbnail(QFileInfo(QDir(source).filePath(info.fileName())),source,language);!image.isEmpty())return image;
        if(const auto image=local_entry_thumbnail(QFileInfo(QDir(source).filePath(info.completeBaseName()+".prtz")),source,language);!image.isEmpty())return image;
    }
    return {};
}

QIcon entry_icon(const QFileInfo& info,const QString& directory,int size,const QString& language,bool search_thumbnail=true) {
    if(search_thumbnail)if(const QString thumbnail=entry_thumbnail(info,directory,language);!thumbnail.isEmpty())return QIcon(thumbnail);
    const QDir thumbnails(QDir(directory).filePath(QStringLiteral("0000-index/thumbnails")));
    const QString stem=logical_file_id(info);
    if(info.isDir())for(const auto& suffix:{QStringLiteral("svg"),QStringLiteral("png"),QStringLiteral("jpg"),QStringLiteral("jpeg"),QStringLiteral("webp"),QStringLiteral("gif")}){
        const QString candidate=QDir(directory).filePath(stem+QLatin1Char('.')+suffix);if(QFileInfo::exists(candidate))return QIcon(candidate);
    }
    for(const auto& suffix:{QStringLiteral("svg"),QStringLiteral("png"),QStringLiteral("jpg"),QStringLiteral("jpeg"),QStringLiteral("webp"),QStringLiteral("gif")}){
        const QString candidate=thumbnails.filePath(stem+QLatin1Char('.')+suffix);if(QFileInfo::exists(candidate))return QIcon(candidate);
    }
    if(info.isDir()){
        QSettings metadata(metadata_path(info.absoluteFilePath()),QSettings::IniFormat);
        const QString preview=first_existing_image(info.absoluteFilePath(),metadata.value("Project/Preview").toString(),language,{QStringLiteral("01")});if(!preview.isEmpty())return QIcon(preview);
        const auto branding=directory_branding(info.absoluteFilePath());if(!branding.path.isEmpty())return QIcon(branding.path);
    }
    if(info.suffix().compare("png",Qt::CaseInsensitive)==0||info.suffix().compare("jpg",Qt::CaseInsensitive)==0||
       info.suffix().compare("jpeg",Qt::CaseInsensitive)==0||info.suffix().compare("webp",Qt::CaseInsensitive)==0)return QIcon(info.absoluteFilePath());
    Q_UNUSED(size);
    return QFileIconProvider().icon(info);
}

class OverviewTileDelegate final : public QStyledItemDelegate {
public:
    explicit OverviewTileDelegate(QListWidget* view):QStyledItemDelegate(view),view_(view){}
    QSize sizeHint(const QStyleOptionViewItem&,const QModelIndex&)const override{return view_->gridSize()-QSize(16,16);}
    void paint(QPainter* painter,const QStyleOptionViewItem& option,const QModelIndex& index)const override{
        painter->save();const QRect card=option.rect.adjusted(2,2,-2,-2);
        const bool highlighted=option.state&(QStyle::State_Selected|QStyle::State_MouseOver);
        painter->setPen(option.palette.color(highlighted?QPalette::Highlight:QPalette::Mid));
        painter->setBrush(option.palette.brush(highlighted?QPalette::Highlight:QPalette::AlternateBase));painter->drawRoundedRect(card,5,5);
        painter->setPen(option.palette.color(highlighted?QPalette::HighlightedText:QPalette::Text));QFont font=option.font;font.setBold(true);painter->setFont(font);
        painter->drawText(card.adjusted(10,10,-10,-card.height()+58),Qt::AlignHCenter|Qt::AlignTop|Qt::TextWordWrap,index.data(Qt::DisplayRole).toString());
        const QRect image=card.adjusted(14,66,-14,-14);const QIcon icon=qvariant_cast<QIcon>(index.data(Qt::DecorationRole));
        icon.paint(painter,image,Qt::AlignCenter,option.state&QStyle::State_Enabled?QIcon::Normal:QIcon::Disabled);
        if(option.state&QStyle::State_HasFocus){QPen pen(option.palette.color(QPalette::Text));pen.setStyle(Qt::DotLine);painter->setPen(pen);painter->setBrush(Qt::NoBrush);painter->drawRoundedRect(card.adjusted(3,3,-3,-3),3,3);}
        painter->restore();
    }
private:
    QListWidget* view_{};
};

class ImagePreviewDialog final : public zima::ui::PropertiesSubWindow {
public:
    using PropertiesSubWindow::PropertiesSubWindow;
protected:
    bool submit()override{return true;}
};

class FileEntryItem final : public QTreeWidgetItem {
public:
    using QTreeWidgetItem::QTreeWidgetItem;
    bool operator<(const QTreeWidgetItem& other)const override {
        const bool directory=data(0,Qt::UserRole+3).toBool(),other_directory=other.data(0,Qt::UserRole+3).toBool();
        if(directory!=other_directory)return treeWidget()->header()->sortIndicatorOrder()==Qt::AscendingOrder?directory:other_directory;
        if(directory&&treeWidget()->sortColumn()==0)return QString::localeAwareCompare(
            QFileInfo(data(0,Qt::UserRole).toString()).fileName(),
            QFileInfo(other.data(0,Qt::UserRole).toString()).fileName())<0;
        return QTreeWidgetItem::operator<(other);
    }
};

class FileTable final : public QTreeWidget {
public:
    using QTreeWidget::QTreeWidget;
    std::function<void(const QModelIndex&)> preview_requested;
protected:
    void mouseDoubleClickEvent(QMouseEvent* event)override{
        const auto index=indexAt(event->position().toPoint());auto* item=itemFromIndex(index);
        if(event->button()==Qt::LeftButton&&index.column()==file_preview_column&&item){
            if(preview_requested)preview_requested(index);
            event->accept();return;
        }
        if(event->button()==Qt::LeftButton&&index.column()==0&&item){
            QStyleOptionViewItem option;initViewItemOption(&option);option.rect=visualRect(index);option.features|=QStyleOptionViewItem::HasCheckIndicator|QStyleOptionViewItem::HasDecoration|QStyleOptionViewItem::HasDisplay;option.checkState=item->checkState(0);option.icon=item->icon(0);option.decorationSize=iconSize();option.text=item->text(0);
            const auto geometry=file_entry_geometry(option,index,this);
            if(style()->subElementRect(QStyle::SE_ItemViewItemCheckIndicator,&geometry.content,this).contains(event->position().toPoint())){event->accept();return;}
        }
        QTreeWidget::mouseDoubleClickEvent(event);
    }
    void mousePressEvent(QMouseEvent* event)override{
        const auto index=indexAt(event->position().toPoint());auto* item=itemFromIndex(index);
        if(event->button()==Qt::LeftButton&&index.column()==0&&item&&item->isSelected()&&selectedItems().size()>1){
            QStyleOptionViewItem option;initViewItemOption(&option);option.rect=visualRect(index);option.features|=QStyleOptionViewItem::HasCheckIndicator|QStyleOptionViewItem::HasDecoration|QStyleOptionViewItem::HasDisplay;option.checkState=item->checkState(0);option.icon=item->icon(0);option.decorationSize=iconSize();option.text=item->text(0);
            if(style()->subElementRect(QStyle::SE_ItemViewItemCheckIndicator,&option,this).contains(event->position().toPoint())){
                const auto checked=item->checkState(0)==Qt::Checked?Qt::Unchecked:Qt::Checked;QSignalBlocker block(this);for(auto* selected:selectedItems())selected->setCheckState(0,checked);event->accept();return;
            }
        }
        QTreeWidget::mousePressEvent(event);
    }
};

class ParameterDelegate final : public QStyledItemDelegate {
public:
    ParameterDelegate(std::function<void(const QModelIndex&)> committed,QObject* parent)
        :QStyledItemDelegate(parent),committed_(std::move(committed)){setObjectName("fileManagerParameterDelegate");}
    void paint(QPainter* painter,const QStyleOptionViewItem& option,const QModelIndex& index)const override{
        if(index.column()!=0&&index.column()!=file_preview_column){QStyledItemDelegate::paint(painter,option,index);return;}
        auto* view=qobject_cast<QAbstractItemView*>(parent());
        QStyleOptionViewItem styled(option);initStyleOption(&styled,index);
        const auto geometry=file_entry_geometry(styled,index,view);
        painter->save();painter->setClipRect(option.rect);
        if(!geometry.thumbnail.isEmpty()){
            auto background=styled;background.text.clear();background.icon={};
            background.features&=~(QStyleOptionViewItem::HasDisplay|QStyleOptionViewItem::HasDecoration|QStyleOptionViewItem::HasCheckIndicator);
            background.state&=~QStyle::State_HasFocus;
            view->style()->drawControl(QStyle::CE_ItemViewItem,&background,painter,view);
        }
        view->style()->drawControl(QStyle::CE_ItemViewItem,&geometry.content,painter,view);
        if(index.column()==file_preview_column&&!geometry.thumbnail.isEmpty()) {
            const auto icon=qvariant_cast<QIcon>(index.data(Qt::DecorationRole));
            const auto pixmap=icon.pixmap(geometry.thumbnail.size(),painter->device()->devicePixelRatioF(),
                option.state&QStyle::State_Enabled?QIcon::Normal:QIcon::Disabled);
            if(!pixmap.isNull()) {
                // Raster QIcons may return their smaller original size. Fit the
                // decoded image explicitly, also when the cell needs upscaling.
                const auto size=pixmap.size().scaled(geometry.thumbnail.size(),Qt::KeepAspectRatio);
                const QRect target(geometry.thumbnail.x()+(geometry.thumbnail.width()-size.width())/2,
                    geometry.thumbnail.y()+(geometry.thumbnail.height()-size.height())/2,size.width(),size.height());
                painter->setRenderHint(QPainter::SmoothPixmapTransform);
                painter->drawPixmap(target,pixmap,pixmap.rect());
            }
        }
        painter->restore();
    }
    QSize sizeHint(const QStyleOptionViewItem& option,const QModelIndex& index)const override{
        auto styled=option;if(index.column()==0)styled.decorationSize={20,20};
        auto size=QStyledItemDelegate::sizeHint(styled,index);
        if(index.column()==file_preview_column){
            const auto* view=qobject_cast<QAbstractItemView*>(parent());size.setWidth(view->iconSize().width()+4);
            size.setHeight(std::max(size.height(),view->iconSize().height()+4));
        }
        return size;
    }
    QWidget* createEditor(QWidget* parent,const QStyleOptionViewItem& option,
                          const QModelIndex& index)const override{
        if(index.column()<file_parameter_first_column||!(index.flags()&Qt::ItemIsEditable))return nullptr;
        return QStyledItemDelegate::createEditor(parent,option,index);
    }
    void setModelData(QWidget* editor,QAbstractItemModel* model,const QModelIndex& index)const override{
        QStyledItemDelegate::setModelData(editor,model,index);if(committed_)committed_(index);
    }
private:
    std::function<void(const QModelIndex&)> committed_;
public:
    bool helpEvent(QHelpEvent* event,QAbstractItemView* view,const QStyleOptionViewItem& option,const QModelIndex& index)override {
        if(index.column()==file_preview_column){
            QStyleOptionViewItem styled(option);initStyleOption(&styled,index);
            const auto geometry=file_entry_geometry(styled,index,view);
            if(geometry.thumbnail.contains(event->pos())){
                const QString path=index.siblingAtColumn(0).data(Qt::UserRole+4).toString();
                if(!path.isEmpty()){
                    const QPixmap pixmap=QPixmap(path).scaled(320,240,Qt::KeepAspectRatio,Qt::SmoothTransformation);
                    QByteArray bytes;QBuffer buffer(&bytes);buffer.open(QIODevice::WriteOnly);pixmap.save(&buffer,"PNG");
                    QToolTip::showText(event->globalPos(),"<img src=\"data:image/png;base64,"+QString::fromLatin1(bytes.toBase64())+"\">",view);return true;
                }
            }
        }
        return QStyledItemDelegate::helpEvent(event,view,option,index);
    }

};

} // namespace

bool file_manager_can_write(const ApplicationSettings& settings,const QString& working,const QString& directory,bool parameter_edit){
    const QString path=QFileInfo(directory).canonicalFilePath();if(path.isEmpty())return false;
    qsizetype length=-1;bool read_only=true;
    const auto consider=[&](const QString& root,bool locked){const QString canonical=QFileInfo(root).canonicalFilePath();if(!canonical.isEmpty()&&path_belongs_to(path,canonical)&&canonical.size()>length){length=canonical.size();read_only=locked;}};
    consider(working,false);for(const auto& source:settings.file_manager.sources)if(source.enabled)consider(source.resolved_path,source.read_only);
    return !read_only&&(parameter_edit?!directory_prevents_parameter_edit(path):ancestor_removal_lock(path).isEmpty());
}

FileManagerTab::FileManagerTab(const ApplicationSettings& settings,const QString& working_directory,
                               const QString& initial_path,QWidget* parent)
    :QWidget(parent),settings_(settings),working_directory_(clean_path(working_directory)){
    setObjectName("fileManagerTab");build_ui();rebuild_sources();
    navigate(initial_path.isEmpty()?working_directory_:initial_path);
}

void FileManagerTab::build_ui(){
    auto* root=new QVBoxLayout(this);root->setContentsMargins(0,0,0,0);root->setSpacing(3);
    auto* navigation=new QHBoxLayout;
    const auto nav_button=[&](const char* name,const QString& tooltip,const QString& icon){auto* button=new QToolButton(this);button->setObjectName(name);button->setIcon(resource_icon(icon));button->setToolTip(tooltip);navigation->addWidget(button);return button;};
    auto* home=nav_button("fileManagerHome",tr("Domů"),"web-home");back_=nav_button("fileManagerBack",tr("Zpět"),"web-back");
    forward_=nav_button("fileManagerForward",tr("Vpřed"),"web-forward");up_=nav_button("fileManagerUp",tr("Nahoru"),"select-parent");
    auto* working_label=new QLabel(tr("Pracovní adresář")+QLatin1Char(':'),this);working_label->setObjectName("fileManagerWorkingDirectoryLabel");navigation->addWidget(working_label);
    path_=new QLineEdit(this);path_->setObjectName("fileManagerPath");path_->setReadOnly(true);working_label->setBuddy(path_);navigation->addWidget(path_,1);choose_working_directory_=nav_button("fileManagerChooseWorkingDirectory",tr("Nastavit pracovní adresář"),"working-directory");root->addLayout(navigation);
    connect(home,&QToolButton::clicked,this,[this]{navigate(working_directory_);});connect(back_,&QToolButton::clicked,this,[this]{if(history_index_>0){--history_index_;navigate(history_.at(history_index_),false);}});
    connect(forward_,&QToolButton::clicked,this,[this]{if(history_index_+1<history_.size()){++history_index_;navigate(history_.at(history_index_),false);}});
    connect(up_,&QToolButton::clicked,this,[this]{QDir current(current_directory_);if(current.cdUp())navigate(current.absolutePath());});

    auto* splitter=new QSplitter(Qt::Horizontal,this);splitter->setObjectName("fileManagerSplitter");
    auto* source_panel=new QWidget(splitter);source_panel->setObjectName("fileManagerSourcePanel");source_panel->setMinimumWidth(220);
    auto* source_layout=new QVBoxLayout(source_panel);source_layout->setContentsMargins(0,0,0,0);source_layout->setSpacing(0);
    auto* source_toolbar=new QToolBar(source_panel);source_toolbar->setObjectName("fileManagerSourceToolbar");source_toolbar->setIconSize({18,18});
    auto* source_settings=source_toolbar->addAction(resource_icon("settings"),tr("Nastavení Správce souborů"));source_settings->setObjectName("fileManagerSettings");source_layout->addWidget(source_toolbar);
    connect(source_settings,&QAction::triggered,this,[this]{if(settings_requested)settings_requested();});
    source_list_=new QListWidget(source_panel);source_list_->setObjectName("fileManagerSources");source_list_->setIconSize({24,24});source_list_->setSelectionMode(QAbstractItemView::SingleSelection);source_list_->setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);source_list_->setContextMenuPolicy(Qt::CustomContextMenu);source_layout->addWidget(source_list_);
    source_stack_=new QStackedWidget(source_panel);source_stack_->setObjectName("fileManagerSourceTrees");source_layout->addWidget(source_stack_,1);
    views_=new QTabWidget(splitter);views_->setObjectName("fileManagerViews");views_->setDocumentMode(true);
    overview_stack_=new QStackedWidget(views_);overview_stack_->setObjectName("fileManagerOverviewStack");
    automatic_overview_=new QWidget(overview_stack_);automatic_overview_->setObjectName("fileManagerAutomaticOverview");
    auto* overview_layout=new QVBoxLayout(automatic_overview_);overview_layout->setContentsMargins(0,0,0,0);overview_layout->setSpacing(6);
    auto* header=new QHBoxLayout;header->setContentsMargins(4,4,4,0);
    project_title_=new QLabel(automatic_overview_);project_title_->setObjectName("fileManagerOverviewTitle");project_title_->setWordWrap(true);
    auto title_font=project_title_->font();title_font.setPointSizeF(title_font.pointSizeF()+5);title_font.setBold(true);project_title_->setFont(title_font);header->addWidget(project_title_,1);
    project_logo_=new QLabel(automatic_overview_);project_logo_->setObjectName("fileManagerOverviewLogo");project_logo_->setFixedSize(180,52);project_logo_->setAlignment(Qt::AlignRight|Qt::AlignVCenter);header->addWidget(project_logo_);overview_layout->addLayout(header);
    project_details_=new QLabel(automatic_overview_);project_details_->setObjectName("fileManagerOverviewDetails");project_details_->setWordWrap(true);project_details_->setTextFormat(Qt::PlainText);overview_layout->addWidget(project_details_);
    project_image_=new QLabel(automatic_overview_);project_image_->setObjectName("fileManagerOverviewPreview");project_image_->setAlignment(Qt::AlignCenter);project_image_->setFixedHeight(220);overview_layout->addWidget(project_image_);
    overview_empty_=new QLabel(tr("Žádné podadresáře"),automatic_overview_);overview_empty_->setObjectName("fileManagerOverviewEmpty");overview_empty_->setAlignment(Qt::AlignCenter);overview_layout->addWidget(overview_empty_);
    overview_=new QListWidget(automatic_overview_);overview_->setObjectName("fileManagerOverview");overview_->setViewMode(QListView::IconMode);overview_->setResizeMode(QListView::Adjust);overview_->setMovement(QListView::Static);overview_->setWordWrap(true);overview_->setSpacing(8);overview_->setUniformItemSizes(true);overview_->setMouseTracking(true);overview_->setFrameShape(QFrame::NoFrame);overview_->setItemDelegate(new OverviewTileDelegate(overview_));overview_layout->addWidget(overview_,1);
    overview_stack_->addWidget(automatic_overview_);
    connect(overview_,&QListWidget::itemClicked,this,[this](QListWidgetItem* item){open_entry(item->data(Qt::UserRole).toString());});views_->addTab(overview_stack_,resource_icon("overview"),tr("Přehled"));

    auto* files_page=new QWidget(views_);auto* files_layout=new QVBoxLayout(files_page);files_layout->setContentsMargins(0,0,0,12);files_=new FileTable(files_page);files_->setObjectName("fileManagerFiles");files_->setHeader(new ColumnFilterHeader(files_));files_->setRootIsDecorated(false);files_->setUniformRowHeights(true);files_->setAlternatingRowColors(true);files_->setSelectionMode(QAbstractItemView::ExtendedSelection);files_->setEditTriggers(QAbstractItemView::SelectedClicked);files_->header()->setSortIndicator(0,Qt::AscendingOrder);files_->setSortingEnabled(true);files_->setIconSize({48,48});files_->header()->setSectionsMovable(true);files_->header()->setStretchLastSection(false);
    static_cast<ColumnFilterHeader*>(files_->header())->filters_changed=[this]{apply_file_filters();};
    connect(files_,&QTreeWidget::itemDoubleClicked,this,[this](QTreeWidgetItem* item,int column){if(column==0)open_entry(item->data(0,Qt::UserRole).toString());});
    auto* include_status=new QLabel(files_page);include_status->setObjectName("fileManagerIncludeStatus");include_status->setTextFormat(Qt::PlainText);include_status->setWordWrap(true);include_status->hide();files_layout->addWidget(include_status);files_layout->addWidget(files_,1);
    auto* actions=new QHBoxLayout;filter_versions_=new QToolButton(files_page);filter_versions_->setText(tr("Verze"));filter_versions_->setCheckable(true);filter_versions_->setStyleSheet(command_button_style());filter_versions_->setIcon(resource_icon("restore"));filter_versions_->setToolButtonStyle(Qt::ToolButtonTextBesideIcon);filter_versions_->setObjectName("fileManagerFilterVersions");actions->addWidget(filter_versions_);connect(filter_versions_,&QToolButton::toggled,this,[this]{save_type_filters();refresh_overview();refresh_files();});
    filters_=new QToolButton(files_page);filters_->setObjectName("fileManagerFilters");filters_->setText(tr("Filters…"));filters_->setIcon(resource_icon("filter"));filters_->setToolButtonStyle(Qt::ToolButtonTextBesideIcon);filters_->setCheckable(true);filters_->setStyleSheet(command_button_style());actions->addWidget(filters_);connect(filters_,&QToolButton::clicked,this,&FileManagerTab::edit_filters);
    for(const auto* extension:{"prtz","asmz","drwz","matz","frmz","tblz","symz"}){const QString type=QString::fromLatin1(extension);auto* button=new QToolButton(files_page);button->setText(type);button->setStyleSheet(command_button_style());button->setIcon(type=="matz"?resource_icon("material"):native_file_icon(QFileInfo("file."+type)));button->setToolButtonStyle(Qt::ToolButtonTextBesideIcon);button->setObjectName("fileManagerType_"+type);button->setCheckable(true);button->setChecked(true);type_filters_.insert(type,button);actions->addWidget(button);connect(button,&QToolButton::toggled,this,[this]{save_type_filters();apply_file_filters();});}
    files_->header()->setContextMenuPolicy(Qt::CustomContextMenu);connect(files_->header(),&QWidget::customContextMenuRequested,this,[this](const QPoint& point){QMenu menu(this);auto* type=menu.addAction(tr("Show Type column"));type->setCheckable(true);type->setChecked(!files_->isColumnHidden(2));connect(type,&QAction::toggled,this,[this](bool visible){files_->setColumnHidden(2,!visible);save_column_layout();});menu.exec(files_->header()->mapToGlobal(point));});
    new_directory_=new QPushButton(tr("New folder…"),files_page);new_directory_->setObjectName("fileManagerNewDirectory");new_directory_->setIcon(resource_icon("web-folder"));actions->addWidget(new_directory_);connect(new_directory_,&QPushButton::clicked,this,&FileManagerTab::create_directory);
    const auto operation=[&](const QString& text,const char* name){auto* button=new QPushButton(text,files_page);button->setObjectName(name);actions->addWidget(button);return button;};
    auto* thumbnail_size=new QSlider(Qt::Horizontal,files_page);thumbnail_size->setObjectName("fileManagerThumbnailSize");thumbnail_size->setRange(48,192);thumbnail_size->setMinimumWidth(80);thumbnail_size->setMaximumWidth(300);thumbnail_size->setToolTip(tr("Velikost náhledů"));thumbnail_size->setAccessibleName(tr("Velikost náhledů"));actions->addWidget(thumbnail_size,1);
    int initial_size=96;if(!column_settings_path(settings_).isEmpty()){QSettings layout(column_settings_path(settings_),QSettings::IniFormat);initial_size=layout.value("Thumbnails/ColumnSize",std::max(96,layout.value("Thumbnails/Size",96).toInt())).toInt();}thumbnail_size->setValue(initial_size);files_->setIconSize({thumbnail_size->value(),thumbnail_size->value()});
    connect(thumbnail_size,&QSlider::valueChanged,this,[this](int size){files_->setIconSize({size,size});files_->setColumnWidth(file_preview_column,size+4);if(!column_settings_path(settings_).isEmpty()){QSettings layout(column_settings_path(settings_),QSettings::IniFormat);layout.setValue("Thumbnails/ColumnSize",size);}});
    clean_=operation(tr("Vyčistit"),"fileManagerClean");clean_->setIcon(resource_icon("clean"));
    copy_=operation(tr("Kopírovat…"),"fileManagerCopy");copy_->setIcon(resource_icon("file-copy"));move_=operation(tr("Přesunout…"),"fileManagerMove");move_->setIcon(resource_icon("file-move"));remove_=operation(tr("Smazat…"),"fileManagerDelete");remove_->setIcon(resource_icon("delete"));files_layout->addLayout(actions);
    connect(clean_,&QPushButton::clicked,this,[this]{clean_directory(current_directory_);});
    connect(copy_,&QPushButton::clicked,this,[this]{copy_selected(false);});connect(move_,&QPushButton::clicked,this,[this]{copy_selected(true);});connect(remove_,&QPushButton::clicked,this,&FileManagerTab::delete_selected);
    files_->setItemDelegate(new ParameterDelegate([this](const QModelIndex& index){save_parameter(index);},files_));
    static_cast<FileTable*>(files_)->preview_requested=[this](const QModelIndex& index){
        auto* item=files_->itemFromIndex(index);if(!item)return;const QString file=item->data(0,Qt::UserRole).toString();
        const QString suffix=QFileInfo(file).suffix().toLower();
        if(QStringList{"step","stp","iges","igs","dxf"}.contains(suffix)&&preview_file_requested){preview_file_requested(file);return;}
        const QString thumbnail=item->data(0,Qt::UserRole+4).toString();if(thumbnail.isEmpty())return;
        auto* dialog=new ImagePreviewDialog(tr("Náhled"),window());dialog->setObjectName("fileManagerImagePreview");dialog->setAttribute(Qt::WA_DeleteOnClose);
        auto* image=new QLabel(dialog);image->setAlignment(Qt::AlignCenter);image->setPixmap(QPixmap(thumbnail).scaled(720,540,Qt::KeepAspectRatio,Qt::SmoothTransformation));dialog->content_layout()->addWidget(image);dialog->set_initial_size({760,600});dialog->show();
    };
    connect(files_,&QTreeWidget::itemChanged,this,[this](QTreeWidgetItem* item,int column){
        if(refreshing_files_||column!=0||!item->isSelected())return;QSignalBlocker block(files_);
        for(auto* selected:files_->selectedItems())selected->setCheckState(0,item->checkState(0));
    });
    auto* layout_timer=new QTimer(this);layout_timer->setSingleShot(true);layout_timer->setInterval(250);
    connect(files_->header(),&QHeaderView::sectionMoved,layout_timer,qOverload<>(&QTimer::start));
    connect(files_->header(),&QHeaderView::sectionResized,layout_timer,qOverload<>(&QTimer::start));
    connect(layout_timer,&QTimer::timeout,this,[this]{if(!refreshing_files_)save_column_layout();});
    views_->addTab(files_page,resource_icon("file-manager"),tr("Soubory"));views_->setCurrentIndex(settings_.file_manager.default_view=="files"?1:0);splitter->addWidget(source_panel);splitter->addWidget(views_);splitter->setStretchFactor(0,0);splitter->setStretchFactor(1,1);splitter->setSizes({270,900});root->addWidget(splitter,1);
    connect(source_list_,&QListWidget::itemClicked,this,[this](QListWidgetItem* item){const int index=source_list_->row(item);if(index>=0&&index<sources_.size())navigate(sources_.at(index).source.resolved_path);});
    connect(source_list_,&QListWidget::itemActivated,this,[this](QListWidgetItem* item){const int index=source_list_->row(item);if(index>=0&&index<sources_.size())navigate(sources_.at(index).source.resolved_path);});
    connect(source_list_,&QListWidget::customContextMenuRequested,this,&FileManagerTab::show_source_menu);
}

void FileManagerTab::rebuild_sources(){
    while(source_stack_->count()){auto* page=source_stack_->widget(0);source_stack_->removeWidget(page);delete page;}source_list_->clear();sources_.clear();
    FileManagerSource working;working.id="working";working.name=tr("Pracovní adresář");working.path=working_directory_;working.resolved_path=working_directory_;working.enabled=true;working.read_only=false;working.factory=true;
    QVector<FileManagerSource> all{working};for(const auto& source:settings_.file_manager.sources)if(source.enabled)all.push_back(source);
    for(auto source:all){
        if(source.factory&&source.id!="working"&&QFileInfo(metadata_path(source.resolved_path)).isFile())source.name=directory_label(source.resolved_path,settings_.language);
        auto* tree=new QTreeView(source_stack_);tree->setObjectName(QStringLiteral("fileManagerSourceTree_%1").arg(source.id));tree->setHeaderHidden(true);tree->setAnimated(false);tree->setIconSize({28,28});tree->setItemDelegate(new DirectoryTreeDelegate(tree));tree->setContextMenuPolicy(Qt::CustomContextMenu);
        auto* model=new QFileSystemModel(tree);QDir::Filters model_filters=QDir::Dirs|QDir::NoDotAndDotDot;
        if(settings_.file_manager.show_hidden)model_filters|=QDir::Hidden|QDir::System;
        model->setFilter(model_filters);
        model->setRootPath(source.resolved_path);auto* proxy=new DirectoryTreeFilter(settings_.language,settings_.file_manager.hidden_patterns,tree);proxy->setSourceModel(model);proxy->sort(0,Qt::AscendingOrder);tree->setModel(proxy);tree->setRootIndex(proxy->mapFromSource(model->index(source.resolved_path)));
        for(int column=1;column<proxy->columnCount();++column)tree->hideColumn(column);
        const int index=source_stack_->addWidget(tree);auto* item=new QListWidgetItem(source.name,source_list_);item->setData(Qt::UserRole,index);sources_.push_back({source,model,proxy,tree});
        connect(tree,&QTreeView::clicked,this,[this,model,proxy](const QModelIndex& selected){const QFileInfo info=model->fileInfo(proxy->mapToSource(selected));if(info.isDir())navigate(info.absoluteFilePath());});
        connect(tree,&QTreeView::customContextMenuRequested,this,[this,tree](const QPoint& point){show_tree_menu(tree,point);});
        if(!QFileInfo(source.resolved_path).isDir()){item->setIcon(resource_icon("suppress"));item->setToolTip(tr("Zdroj není dostupný: %1").arg(source.resolved_path));}
        else{item->setIcon(source.id=="working"?resource_icon("web-home"):resource_icon("web-folder"));item->setToolTip(QDir::toNativeSeparators(source.resolved_path));}
    }
    refresh_source_labels();
    const int row=source_list_->count()?std::max(28,source_list_->sizeHintForRow(0)):28;source_list_->setFixedHeight(row*source_list_->count()+2*source_list_->frameWidth());
}

int FileManagerTab::source_index_for(const QString& path)const{
    int best=-1;qsizetype length=-1;for(int i=0;i<sources_.size();++i)if(path_belongs_to(path,sources_.at(i).source.resolved_path)&&sources_.at(i).source.resolved_path.size()>length){best=i;length=sources_.at(i).source.resolved_path.size();}return best;
}

void FileManagerTab::refresh_source_labels(){
    for(int index=0;index<sources_.size();++index){auto& source=sources_[index].source;if(source.factory&&source.id!="working"&&QFileInfo(metadata_path(source.resolved_path)).isFile()){source.name=directory_label(source.resolved_path,settings_.language);source_list_->item(index)->setText(source.name);}}
}

void FileManagerTab::navigate(const QString& requested,bool track_history){
    const QString target=clean_path(requested);if(!QFileInfo(target).isDir()){if(QFileInfo(target).isFile()){open_entry(target);return;}QMessageBox::warning(this,tr("Správce souborů"),tr("Adresář neexistuje: %1").arg(target));return;}
    const int source=source_index_for(target);if(source<0){QMessageBox::warning(this,tr("Správce souborů"),tr("Adresář nepatří do žádného zapnutého zdroje dat."));return;}
    current_directory_=target;changing_source_=true;source_list_->setCurrentRow(source);source_stack_->setCurrentIndex(source);changing_source_=false;
    auto& page=sources_[source];const auto index=page.proxy->mapFromSource(page.model->index(target));page.tree->setCurrentIndex(index);page.tree->scrollTo(index);
    for(auto parent=index.parent();parent.isValid()&&parent!=page.tree->rootIndex();parent=parent.parent())page.tree->expand(parent);
    path_->setText(QDir::toNativeSeparators(working_directory_));path_->setToolTip(QDir::toNativeSeparators(working_directory_));
    if(track_history){while(history_.size()>history_index_+1)history_.removeLast();if(history_.isEmpty()||history_.last()!=target){history_.push_back(target);history_index_=history_.size()-1;}}
    back_->setEnabled(history_index_>0);forward_->setEnabled(history_index_+1<history_.size());up_->setEnabled(target!=clean_path(page.source.resolved_path));
    refresh_directory();
}

void FileManagerTab::refresh_directory(){
    refresh_type_filters();refresh_overview();refresh_files();update_actions();
    const QString label=directory_label(current_directory_,settings_.language);if(title_changed)title_changed(tr("Soubory – %1").arg(label));
}

void FileManagerTab::refresh_overview_header(){
    QSettings metadata(metadata_path(current_directory_),QSettings::IniFormat);const bool project=is_project_directory(current_directory_);
    const QString title=project?translated_value(metadata,"Project/Title",settings_.language):QString{};
    project_title_->setText(title.isEmpty()?directory_label(current_directory_,settings_.language):title);
    QStringList details;
    if(project){
        const QString number=metadata.value("Project/Number").toString().trimmed(),customer=metadata.value("Project/Customer").toString().trimmed(),description=translated_value(metadata,"Project/Description",settings_.language);
        if(!number.isEmpty())details<<tr("Číslo projektu: %1").arg(number);if(!customer.isEmpty())details<<tr("Zákazník: %1").arg(customer);if(!description.isEmpty())details<<description;
    }
    project_details_->setText(details.join(QStringLiteral("\n")));project_details_->setVisible(!details.isEmpty());
    const QString preview=project?first_existing_image(current_directory_,metadata.value("Project/Preview").toString(),settings_.language,{"01"}):QString{};
    project_image_->setVisible(!preview.isEmpty());project_image_->setPixmap(preview.isEmpty()?QPixmap{}:QIcon(preview).pixmap({640,220}));
    QString logo=first_existing_image(current_directory_,project?metadata.value("Project/Logo").toString():metadata.value("Directory/Logo").toString(),settings_.language,{"logo-text","logo","ZIMA-Engineering"});if(logo.isEmpty())logo=settings_.file_manager.company_logo;
    project_logo_->setVisible(!logo.isEmpty());project_logo_->setPixmap(logo.isEmpty()?QPixmap{}:QIcon(logo).pixmap(project_logo_->size()));
}

void FileManagerTab::sync_overview_theme(){
    if(!overview_web_)return;
    const auto colors=palette();const bool dark=colors.color(QPalette::Window).lightness()<128;
    QString source=QStringLiteral("document.documentElement.dataset.zimaTheme='%1';").arg(dark?"dark":"light");
    for(const auto& entry:{std::pair{"paper",QPalette::Base},std::pair{"text",QPalette::Text},
        std::pair{"muted",QPalette::PlaceholderText},std::pair{"line",dark?QPalette::Midlight:QPalette::Mid},
        std::pair{"head",QPalette::Button},std::pair{"stripe",QPalette::AlternateBase},std::pair{"link",QPalette::Link}})
        source+=QStringLiteral("document.documentElement.style.setProperty('--zima-%1','%2');")
            .arg(QString::fromLatin1(entry.first),colors.color(entry.second).name(QColor::HexRgb));
    const auto website=QUrl::fromUserInput(settings_.file_manager.company_website.trimmed());
    const bool linked=!settings_.file_manager.company_website.trimmed().isEmpty()&&
        website.isValid()&&(website.scheme()=="https"||website.scheme()=="http");
    const auto link=QJsonDocument(QJsonArray{linked?website.toString(QUrl::FullyEncoded):QString{}}).toJson(QJsonDocument::Compact);
    source+=QStringLiteral("{const website=%1[0];document.querySelectorAll('a.reference-brand').forEach(a=>{if(website)a.setAttribute('href',website);else a.removeAttribute('href');});}")
        .arg(QString::fromUtf8(link));
    auto& scripts=overview_web_->page()->scripts();
    const QString name=QStringLiteral("zima-file-manager-theme");
    for(const auto& old:scripts.find(name))scripts.remove(old);
    QWebEngineScript script;script.setName(name);script.setInjectionPoint(QWebEngineScript::DocumentReady);
    script.setWorldId(QWebEngineScript::MainWorld);script.setSourceCode(source);scripts.insert(script);
    overview_web_->page()->runJavaScript(source);
}

void FileManagerTab::changeEvent(QEvent* event){
    QWidget::changeEvent(event);
    if(event->type()==QEvent::PaletteChange)sync_overview_theme();
}

void FileManagerTab::refresh_overview(){
    const QString index_file=localized_index_file(current_directory_,settings_.language);
    if(!index_file.isEmpty()){
        if(!overview_web_){overview_web_=new QWebEngineView(overview_stack_);overview_web_->setObjectName("fileManagerOverviewWeb");overview_web_->settings()->setAttribute(QWebEngineSettings::LocalContentCanAccessFileUrls,true);overview_stack_->addWidget(overview_web_);sync_overview_theme();}
        overview_stack_->setCurrentWidget(overview_web_);overview_web_->load(QUrl::fromLocalFile(index_file));return;
    }
    if(overview_web_&&overview_stack_->currentWidget()==overview_web_)overview_web_->setUrl(QUrl(QStringLiteral("about:blank")));
    overview_stack_->setCurrentWidget(automatic_overview_);refresh_overview_header();
    overview_->clear();const int thumbnail=std::max(150,settings_.file_manager.thumbnail_size);overview_->setIconSize({thumbnail,thumbnail});overview_->setGridSize({std::max(220,thumbnail+48),thumbnail+84});
    QSettings metadata(metadata_path(current_directory_),QSettings::IniFormat);const bool automatic=metadata.value("Directory/AutoIndex",true).toBool();
    if(automatic){
        QDir directory(current_directory_);auto filters=QDir::Dirs|QDir::NoDotAndDotDot;if(settings_.file_manager.show_hidden)filters|=QDir::Hidden|QDir::System;
        for(const auto& info:directory.entryInfoList(filters,QDir::Name|QDir::IgnoreCase)){
            if(file_filter_matches_&&!file_filter_matches_(info.fileName()))continue;
            const QString label=directory_label(info.absoluteFilePath(),settings_.language);
            auto* item=new QListWidgetItem(entry_icon(info,current_directory_,thumbnail,settings_.language),label,overview_);item->setData(Qt::UserRole,info.absoluteFilePath());item->setData(Qt::AccessibleTextRole,label);item->setToolTip(label+QLatin1Char('\n')+info.absoluteFilePath());
        }
    }
    overview_empty_->setVisible(automatic&&overview_->count()==0);

}

void FileManagerTab::refresh_files(){
    if(files_->columnCount()>=4)save_column_layout();
    refreshing_files_=true;files_->clear();QSettings metadata(metadata_path(current_directory_),QSettings::IniFormat);const int version=metadata.value("Directory/Version",2).toInt();
    QStringList parameters=metadata_parameter_ids(metadata);for(auto& value:parameters)value=value.trimmed();parameters.removeDuplicates();
    const auto parameter_sources=directory_includes(current_directory_,"Directory/IncludeParameters");
    const auto thumbnail_sources=directory_includes(current_directory_,"Directory/IncludeThumbnails");
    if(auto* status=findChild<QLabel*>("fileManagerIncludeStatus")){status->setText(parameter_sources.error.isEmpty()?thumbnail_sources.error:parameter_sources.error);status->setVisible(!status->text().isEmpty());}
    QMap<QString,QVariant> inherited_metadata;
    QDir directory(current_directory_);auto filters=QDir::Dirs|QDir::Files|QDir::NoDotAndDotDot;if(settings_.file_manager.show_hidden)filters|=QDir::Hidden|QDir::System;
    auto entries=directory.entryInfoList(filters,QDir::DirsFirst|QDir::Name|QDir::IgnoreCase);if(!metadata.value("Directory/SubdirectoriesAsParts",true).toBool())entries.erase(std::remove_if(entries.begin(),entries.end(),[](const QFileInfo& info){return info.isDir();}),entries.end());
    QMap<QString,FileManagerNativeParameters> native_parameters;
    const auto read_native=[this](const QFileInfo& info){const QString stamp=info.lastModified().toUTC().toString(Qt::ISODateWithMs)+QLatin1Char(':')+QString::number(info.size())+(native_parameters_revision_requested?QLatin1Char(':')+native_parameters_revision_requested(info.absoluteFilePath()):QString{});auto cached=native_parameter_cache_.constFind(info.absoluteFilePath());if(cached!=native_parameter_cache_.cend()&&cached->first==stamp)return cached->second;auto data=native_parameters_requested(info.absoluteFilePath());if(data.error.isEmpty())native_parameter_cache_.insert(info.absoluteFilePath(),{stamp,data});return data;};
    const auto native_file=[](const QFileInfo& info){return info.isFile()&&QStringList{"prtz","asmz","frmz","tblz","symz","matz"}.contains(info.suffix().toLower());};
    if(native_parameters_requested)for(const auto& info:entries){if(!native_file(info))continue;auto data=read_native(info);if(!data.error.isEmpty())continue;native_parameters.insert(info.absoluteFilePath(),std::move(data));}
    QSet<QString> stems;for(const auto& info:entries)if(info.isFile())stems.insert(info.completeBaseName());
    for(int source_index=1;source_index<parameter_sources.directories.size();++source_index){
        const auto& source=parameter_sources.directories[source_index];QSettings included(metadata_path(source),QSettings::IniFormat);QMap<QString,QVariant> values;
        for(const auto& key:included.allKeys())if(key.startsWith("Parts/")||key.startsWith("Parameters/"))values.insert(key,included.value(key));
        for(const auto& parameter:metadata_parameter_ids(included))if(!parameter.trimmed().isEmpty()&&!parameters.contains(parameter))parameters.push_back(parameter);
        if(native_parameters_requested)for(const auto& info:QDir(source).entryInfoList(QDir::Files,QDir::Name)){
            if(!native_file(info)||!stems.contains(info.completeBaseName()))continue;const auto data=read_native(info);if(!data.error.isEmpty())continue;
            const auto prefix=QStringLiteral("Parts/%1/").arg(logical_file_id(info));
            // Native data is authoritative over the source's saved mirror,
            // including empty values and unsaved values of open documents.
            for(auto it=values.begin();it!=values.end();)if(it.key().startsWith(prefix))it=values.erase(it);else ++it;
            for(auto it=data.values.cbegin();it!=data.values.cend();++it)values.insert(prefix+it.key(),it.value());
            for(auto it=data.localized_values.cbegin();it!=data.localized_values.cend();++it)for(auto language=it->cbegin();language!=it->cend();++language)values.insert(prefix+it.key()+(language.key().isEmpty()?QString{}:QLatin1Char('/')+language.key()),language.value());
            for(auto it=data.labels.cbegin();it!=data.labels.cend();++it)for(auto language=it->cbegin();language!=it->cend();++language)values.insert("Parameters/"+it.key()+"/Label"+(language.key().isEmpty()?QString{}:QLatin1Char('/')+language.key()),language.value());
        }
        for(auto it=values.cbegin();it!=values.cend();++it)if(!inherited_metadata.contains(it.key()))inherited_metadata.insert(it.key(),it.value());
    }
    const auto inherited_value=[&](const QString& key)->QString{return inherited_metadata.value(key).toStringList().join(", ");};
    QStringList headers{tr("Název souboru"),tr("Náhled"),tr("Typ"),tr("Velikost"),tr("Změněno")};
    const auto parameter_unit_suffix=[&](const QString& parameter){
        QSet<QString> units;
        for(const auto& data:native_parameters)if(data.values.contains(parameter))units.insert(data.units.value(parameter));
        if(units.size()==1&&!units.constBegin()->isEmpty()&&*units.constBegin()!="1")return QStringLiteral(" [%1]").arg(*units.constBegin());
        return QString{};
    };
    for(const auto& parameter:parameters){const auto key=QStringLiteral("Parameters/%1/Label").arg(parameter);QString label=translated_value(metadata,key,settings_.language);if(label.isEmpty())label=inherited_value(key+QLatin1Char('/')+settings_.language);if(label.isEmpty())label=inherited_value(key);if(label.isEmpty())for(auto it=native_parameters.cbegin();it!=native_parameters.cend();++it){const auto labels=it->labels.value(parameter);label=labels.value(settings_.language,labels.value(QString{}));if(!label.isEmpty())break;}headers<<((label.isEmpty()?parameter:label)+parameter_unit_suffix(parameter));}files_->setColumnCount(headers.size());files_->setHeaderLabels(headers);static_cast<ColumnFilterHeader*>(files_->header())->configure(headers);
    for(int p=0;p<parameters.size();++p)files_->headerItem()->setData(file_parameter_first_column+p,Qt::UserRole,parameters[p]);
    files_->header()->setSectionResizeMode(QHeaderView::Interactive);
    const bool editable=!current_source_read_only()&&!directory_prevents_parameter_edit(current_directory_);
    QSet<QString> existing_labels;for(const auto& parameter:parameters)for(const auto& language:{QString{},QStringLiteral("cs"),QStringLiteral("en"),QStringLiteral("de"),QStringLiteral("fr"),QStringLiteral("ru")}){const QString key=QStringLiteral("Parameters/%1/Label").arg(parameter)+(language.isEmpty()?QString{}:QLatin1Char('/')+language);if(metadata.contains(key))existing_labels.insert(key);}
    const bool saved_native=std::ranges::any_of(native_parameters,[](const auto& data){return !data.unsaved;});
    QMap<QString,QVariant> mirror_updates;
    if(editable&&saved_native&&!parameters.isEmpty()){mirror_updates.insert("Directory/Version",3);if(!metadata.contains("Directory/Kind"))mirror_updates.insert("Directory/Kind","directory");if(!metadata.contains("Directory/Id"))mirror_updates.insert("Directory/Id",QUuid::createUuid().toString(QUuid::WithoutBraces));}
    for(const auto& info:entries){
        if((info.isDir()&&file_filter_matches_&&!file_filter_matches_(info.fileName()))||(!filter_versions_->isChecked()&&is_version_file(info)))continue;
        auto* item=new FileEntryItem(files_);item->setData(0,Qt::UserRole+3,info.isDir());item->setData(0,Qt::UserRole,info.absoluteFilePath());item->setData(0,Qt::UserRole+1,logical_file_id(info));item->setCheckState(0,Qt::Unchecked);const QString thumbnail=entry_thumbnail(info,current_directory_,settings_.language,&metadata,&thumbnail_sources.directories);item->setData(0,Qt::UserRole+4,thumbnail);const auto type_icon=native_file_icon(info);item->setIcon(0,type_icon.isNull()?QFileIconProvider().icon(info):type_icon);item->setIcon(file_preview_column,compact_preview_icon(thumbnail));item->setText(0,info.isDir()?directory_label(info.absoluteFilePath(),settings_.language):info.fileName());item->setText(2,info.isDir()?tr("Adresář"):info.suffix().toLower());item->setText(3,info.isDir()?QString{}:QLocale().formattedDataSize(info.size()));item->setText(4,QLocale().toString(info.lastModified(),QLocale::ShortFormat));
        const QString id=logical_file_id(info);const auto native=native_parameters.constFind(info.absoluteFilePath());item->setData(0,Qt::UserRole+2,native==native_parameters.cend()?QStringLiteral("metadata"):QStringLiteral("zima-cad"));
        metadata.beginGroup("Parts");for(int p=0;p<parameters.size();++p){
            const QString parameter=parameters[p],key=id+QLatin1Char('/')+parameter;QString value;bool localized=false;
            if(native!=native_parameters.cend()){
                const auto variants=native->localized_values.value(parameter);localized=std::ranges::any_of(variants.keys(),[](const QString& language){return !language.isEmpty();});
                value=variants.value(settings_.language,variants.value(QString{},native->values.value(parameter)));
            }else for(const auto& language:{"cs","en","de","fr","ru"})localized=localized||metadata.contains(key+QLatin1Char('/')+language);
            if(value.isEmpty())value=metadata_string(metadata,key+QLatin1Char('/')+settings_.language);
            if(value.isEmpty())value=metadata_string(metadata,key);
            if(value.isEmpty())for(const auto& language:{"cs","en","de","fr","ru"}){value=metadata_string(metadata,key+QLatin1Char('/')+language);if(!value.isEmpty())break;}
            if(native==native_parameters.cend()){
                bool local=metadata.contains(key);for(const auto& language:{"cs","en","de","fr","ru"})local=local||metadata.contains(key+QLatin1Char('/')+language);
                if(local){if(metadata.contains(key+QLatin1Char('/')+settings_.language))value=metadata_string(metadata,key+QLatin1Char('/')+settings_.language);else if(metadata.contains(key))value=metadata_string(metadata,key);}
                else {
                    for(const auto& source_id:{id,info.completeBaseName()}){
                        const auto base="Parts/"+source_id+QLatin1Char('/')+parameter;
                        for(const auto& language:{"cs","en","de","fr","ru"})localized=localized||inherited_metadata.contains(base+QLatin1Char('/')+language);
                        for(const auto& language:{settings_.language,QString{},QStringLiteral("cs"),QStringLiteral("en"),QStringLiteral("de"),QStringLiteral("fr"),QStringLiteral("ru")}){const auto inherited_key=base+(language.isEmpty()?QString{}:QLatin1Char('/')+language);if(inherited_metadata.contains(inherited_key)){value=inherited_value(inherited_key);break;}}
                        if(inherited_metadata.contains(base)||inherited_metadata.contains(base+QLatin1Char('/')+settings_.language))break;
                    }
                }
            }
            item->setText(file_parameter_first_column+p,value);if(native!=native_parameters.cend())item->setToolTip(file_parameter_first_column+p,value+QLatin1Char(' ')+native->units.value(parameter));item->setData(file_parameter_first_column+p,Qt::UserRole+5,localized);if(editable&&(version>=3||native!=native_parameters.cend()||parameter_sources.directories.size()>1))item->setFlags(item->flags()|Qt::ItemIsEditable);
            if(native!=native_parameters.cend()&&editable&&!native->unsaved){
                const QString mirror=QStringLiteral("Parts/%1/%2").arg(id,parameter);if(native->units.contains(parameter))mirror_updates.insert(mirror+QStringLiteral("/Unit"),native->units.value(parameter));if(native->values.contains(parameter))mirror_updates.insert(mirror,native->values.value(parameter));
                const auto variants=native->localized_values.value(parameter);for(auto it=variants.cbegin();it!=variants.cend();++it)mirror_updates.insert(it.key().isEmpty()?mirror:mirror+QLatin1Char('/')+it.key(),it.value());
                const auto labels=native->labels.value(parameter);for(auto it=labels.cbegin();it!=labels.cend();++it){const QString label_key=QStringLiteral("Parameters/%1/Label").arg(parameter)+(it.key().isEmpty()?QString{}:QLatin1Char('/')+it.key());if(!existing_labels.contains(label_key))mirror_updates.insert(label_key,it.value());}
            }
        }metadata.endGroup();
        if(native!=native_parameters.cend()&&editable&&!native->unsaved&&!parameters.isEmpty()){mirror_updates.insert(QStringLiteral("Files/%1/Provider").arg(id),"zima-cad");mirror_updates.insert(QStringLiteral("Files/%1/Path").arg(id),info.fileName());mirror_updates.insert(QStringLiteral("Files/%1/ModifiedUtc").arg(id),info.lastModified().toUTC().toString(Qt::ISODateWithMs));}
    }
    if (!mirror_updates.isEmpty()) {
      QString error;
      if (!atomic_metadata_update(metadata_path(current_directory_),
                                  mirror_updates, &error) &&
          isVisible())
        QMessageBox::warning(this, tr("Správce souborů"), error);
    }
    restore_column_layout();
    refreshing_files_ = false;
    apply_file_filters();
}

void FileManagerTab::apply_file_filters() {
  if (!files_)
    return;
  const auto *header = static_cast<ColumnFilterHeader *>(files_->header());
  for (int row = 0; row < files_->topLevelItemCount(); ++row) {
    auto *item = files_->topLevelItem(row);
    const QFileInfo info(item->data(0, Qt::UserRole).toString());
    QString suffix = info.suffix().toLower(), name = info.fileName();
    if (is_version_file(info)) {
      name = info.completeBaseName();
      suffix = QFileInfo(name).suffix().toLower();
    }
    bool visible = (!file_filter_matches_ || file_filter_matches_(info.fileName())) &&
        (item->data(0, Qt::UserRole + 3).toBool() || !type_filters_.contains(suffix) || type_filters_.value(suffix)->isChecked());
    for (int column = 0; visible && column < files_->columnCount(); ++column) {
      const QString filter = header->filter(column).trimmed();
      if (!filter.isEmpty() &&
          !item->text(column).contains(filter, Qt::CaseInsensitive)) {
        visible = false;
        break;
      }
    }
    item->setHidden(!visible);
  }
}

void FileManagerTab::refresh_type_filters() {
  file_filter_policy_ = local_file_filters(current_directory_);
  file_filter_matches_ = compile_file_filters(effective_file_filters(
      file_filter_policy_, settings_.file_manager.hidden_patterns));
  for (auto it = type_filters_.cbegin(); it != type_filters_.cend(); ++it) {
    QSignalBlocker block(it.value());
    it.value()->setChecked(file_filter_matches_("file." + it.key()));
  }
  filters_->setChecked(
      !effective_file_filters(file_filter_policy_,
                              settings_.file_manager.hidden_patterns)
           .isEmpty());
  QSignalBlocker block(filter_versions_);
  filter_versions_->setChecked(
      file_filter_policy_.value("Filters/ShowZimaVersions", false).toBool());
}
void FileManagerTab::save_type_filters() {
  auto hidden = file_pattern_list(file_filter_policy_.value("Filters/Hide"));
  bool inherit =
      file_filter_policy_.value("Filters/InheritGlobal", true).toBool();
  if (inherit)
    for (auto it = type_filters_.cbegin(); it != type_filters_.cend(); ++it)
      if (it.value()->isChecked() &&
          settings_.file_manager.hidden_patterns.contains(
              "*." + it.key(), Qt::CaseInsensitive)) {
        hidden = effective_file_filters(file_filter_policy_,
                                        settings_.file_manager.hidden_patterns);
        inherit = false;
        break;
      }
  for (auto it = type_filters_.cbegin(); it != type_filters_.cend(); ++it) {
    const QString pattern = "*." + it.key();
    hidden.removeIf([&](const QString &value) {
      return value.compare(pattern, Qt::CaseInsensitive) == 0;
    });
    if (!it.value()->isChecked())
      hidden << pattern;
  }
  file_filter_policy_["Filters/Hide"] = hidden;
  file_filter_policy_["Filters/InheritGlobal"] = inherit;
  file_filter_policy_["Filters/ShowZimaVersions"] =
      filter_versions_->isChecked();
  file_filter_matches_ = compile_file_filters(effective_file_filters(
      file_filter_policy_, settings_.file_manager.hidden_patterns));
  filters_->setChecked(
      !effective_file_filters(file_filter_policy_,
                              settings_.file_manager.hidden_patterns)
           .isEmpty());
  if (file_manager_can_write(settings_, working_directory_,
                             current_directory_)) {
    QString error;
    if (!atomic_metadata_update(file_filters_path(current_directory_),
                                file_filter_policy_, &error))
      QMessageBox::warning(this, tr("Uložení selhalo"), error);
  }
}

void FileManagerTab::edit_filters() {
  filters_->setChecked(
      !effective_file_filters(file_filter_policy_,
                              settings_.file_manager.hidden_patterns)
           .isEmpty());
  const QString directory = current_directory_;
  auto *dialog = new FileFiltersDialog(
      directory, file_filter_policy_, settings_.file_manager.hidden_patterns,
      !current_source_read_only() && ancestor_removal_lock(directory).isEmpty(),
      window());
  dialog->saved = [this, directory](QStringList hidden, bool inherit) {
    if (!file_manager_can_write(settings_, working_directory_, directory))
      throw std::runtime_error(
          tr("The destination is read-only or protected.").toStdString());
    QString error;
    if (!atomic_metadata_update(
            file_filters_path(directory),
            {{"Filters/Hide", hidden}, {"Filters/InheritGlobal", inherit}},
            &error))
      throw std::runtime_error(error.toStdString());
    for (auto &source : sources_)
      static_cast<DirectoryTreeFilter *>(source.proxy)->clear_label_cache();
    if (current_directory_ == directory)
      refresh_directory();
  };
  dialog->setAttribute(Qt::WA_DeleteOnClose);
  dialog->show();
}

void FileManagerTab::update_actions() {
  const auto protection = ancestor_removal_lock(current_directory_);
  const bool writable = !current_source_read_only() && protection.isEmpty();
  new_directory_->setEnabled(writable);
  move_->setEnabled(writable);
  remove_->setEnabled(writable);
  clean_->setEnabled(writable);
  clean_->setToolTip(protection.isEmpty()
                         ? QString{}
                         : tr("Čištění je zakázané ochranou obsahu ve "
                              "Vlastnostech adresáře."));
  copy_->setEnabled(true);
}
void FileManagerTab::create_directory() {
  if (!file_manager_can_write(settings_, working_directory_,
                              current_directory_))
    return;
  const QString directory = current_directory_;
  auto *dialog = new PathNameDialog(
      tr("New folder…"), {},
      [this, directory](const QString &name) {
        if (!file_manager_can_write(settings_, working_directory_, directory) ||
            QFileInfo::exists(QDir(directory).filePath(name)) ||
            !QDir(directory).mkdir(name))
          throw std::runtime_error(
              tr("The folder cannot be created.").toStdString());
        if (current_directory_ == directory)
          refresh_directory();
      },
      window());
  dialog->setAttribute(Qt::WA_DeleteOnClose);
  dialog->show();
}
void FileManagerTab::save_parameter(const QModelIndex &index) {
  if (refreshing_files_ || index.column() < file_parameter_first_column)
    return;
  if (current_source_read_only() ||
      directory_prevents_parameter_edit(current_directory_)) {
    refresh_files();
    return;
  }
  auto *item = files_->itemFromIndex(index);
  auto *header = files_->headerItem();
  if (!item || !header)
    return;
  const QString parameter =
                    header->data(index.column(), Qt::UserRole).toString(),
                id = item->data(0, Qt::UserRole + 1).toString();
  if (parameter.isEmpty() || id.isEmpty())
    return;
  if (item->data(0, Qt::UserRole + 2).toString() == "zima-cad") {
    if (!native_parameter_edit_requested) {
      if (isVisible())
        QMessageBox::warning(this, tr("Správce souborů"),
                             tr("Nativní parametry nyní nelze upravit."));
      refresh_files();
      return;
    }
    const QString native_error =
        native_parameter_edit_requested(item->data(0, Qt::UserRole).toString(),
                                        parameter, item->text(index.column()));
    if (!native_error.isEmpty()) {
      if (isVisible())
        QMessageBox::critical(this, tr("Uložení selhalo"), native_error);
      refresh_files();
      return;
    }
    native_parameter_cache_.remove(item->data(0, Qt::UserRole).toString());
  }
  QSettings existing(metadata_path(current_directory_), QSettings::IniFormat);
  QString value_key = QStringLiteral("Parts/%1/%2").arg(id, parameter);
  if (item->data(index.column(), Qt::UserRole + 5).toBool())
    value_key += QLatin1Char('/') + settings_.language;
  QMap<QString, QVariant> update{{value_key, item->text(index.column())}};
  if (existing.value("Directory/Version", 2).toInt() < 3) {
    QStringList parameters;
    for (int column = file_parameter_first_column;
         column < files_->columnCount(); ++column) {
      const QString key = header->data(column, Qt::UserRole).toString();
      if (!key.isEmpty())
        parameters.push_back(key);
    }
    update.insert("Directory/Version", 3);
    update.insert("Directory/Kind",
                  existing.value("Directory/Kind", "directory"));
    update.insert("Directory/Id",
                  existing.value("Directory/Id", QUuid::createUuid().toString(
                                                     QUuid::WithoutBraces)));
    update.insert("Directory/Parameters", parameters.join(','));
  }
  if (item->data(0, Qt::UserRole + 2).toString() == "zima-cad") {
    const QString file_path = item->data(0, Qt::UserRole).toString();
    update.insert(QStringLiteral("Files/%1/Provider").arg(id), "zima-cad");
    update.insert(QStringLiteral("Files/%1/Path").arg(id),
                  QFileInfo(file_path).fileName());
    update.insert(QStringLiteral("Files/%1/ModifiedUtc").arg(id),
                  QFileInfo(file_path).lastModified().toUTC().toString(
                      Qt::ISODateWithMs));
  }
  QString error;
  if (atomic_metadata_update(metadata_path(current_directory_), update,
                             &error)) {
    apply_file_filters();
    return;
  }
  if (isVisible())
    QMessageBox::critical(this, tr("Uložení selhalo"), error);
  else
    qWarning().noquote() << error;
  refresh_files();
}
bool FileManagerTab::current_source_read_only()const{const int index=source_index_for(current_directory_);return index<0||sources_.at(index).source.read_only;}
QString FileManagerTab::current_source_label()const{const int index=source_index_for(current_directory_);return index<0?QString{}:sources_.at(index).source.name;}

void FileManagerTab::open_entry(const QString& path){const QFileInfo info(path);if(info.isDir()){navigate(path);return;}const QString suffix=info.suffix().toLower();if(QStringList{"prtz","asmz","drwz","frmz","tblz","symz","matz"}.contains(suffix)&&open_file_requested){open_file_requested(path);return;}QDesktopServices::openUrl(QUrl::fromLocalFile(path));}

void FileManagerTab::show_source_menu(const QPoint& point){
    auto* item=source_list_->itemAt(point);if(!item)return;const int index=source_list_->row(item);if(index<0||index>=sources_.size())return;
    const auto& source=sources_.at(index).source;const QString directory=source.resolved_path;if(!QFileInfo(directory).isDir())return;
    QMenu menu(this);menu.setObjectName("fileManagerSourceMenu");auto* open=menu.addAction(resource_icon("web-folder"),tr("Otevřít"));auto* tab=menu.addAction(resource_icon("new"),tr("Otevřít v novém tabu"));auto* working=menu.addAction(resource_icon("web-home"),tr("Nastavit jako pracovní adresář"));working->setObjectName("fileManagerSetWorkingDirectory");working->setEnabled(static_cast<bool>(working_directory_requested));connect(working,&QAction::triggered,this,[this,directory]{if(working_directory_requested)working_directory_requested(directory);});menu.addSeparator();auto* properties=menu.addAction(resource_icon("properties"),tr("Vlastnosti adresáře…"));properties->setObjectName("fileManagerSourceProperties");
    auto* project=menu.addAction(resource_icon("properties"),is_project_directory(directory)?tr("Upravit projektový přehled"):tr("Vytvořit projektový přehled"));project->setObjectName("fileManagerSourceProject");project->setEnabled(!source.read_only);auto* clean=menu.addAction(resource_icon("clean"),tr("Vyčistit"));clean->setObjectName("fileManagerSourceClean");clean->setEnabled(!source.read_only&&ancestor_removal_lock(directory).isEmpty());if(!ancestor_removal_lock(directory).isEmpty())clean->setToolTip(tr("Čištění je zakázané ochranou obsahu ve Vlastnostech adresáře."));connect(clean,&QAction::triggered,this,[this,directory]{clean_directory(directory);});
    connect(open,&QAction::triggered,this,[this,directory]{navigate(directory);});connect(tab,&QAction::triggered,this,[this,directory]{if(new_tab_requested)new_tab_requested(directory);});connect(properties,&QAction::triggered,this,[this,directory]{edit_directory(directory);});connect(project,&QAction::triggered,this,[this,directory]{edit_project(directory,!is_project_directory(directory));});
    menu.exec(source_list_->viewport()->mapToGlobal(point));
}

void FileManagerTab::show_tree_menu(QTreeView* tree,const QPoint& point){const QModelIndex index=tree->indexAt(point);if(!index.isValid())return;const auto page=std::ranges::find_if(sources_,[tree](const auto& source){return source.tree==tree;});if(page==sources_.end())return;const QFileInfo info=page->model->fileInfo(page->proxy->mapToSource(index));if(!info.isDir())return;const int source_index=source_index_for(info.absoluteFilePath());if(source_index<0)return;const bool read_only=sources_.at(source_index).source.read_only;QMenu menu(this);menu.setObjectName("fileManagerTreeMenu");auto* open=menu.addAction(resource_icon("web-folder"),tr("Otevřít"));auto* tab=menu.addAction(resource_icon("new"),tr("Otevřít v novém tabu"));auto* working=menu.addAction(resource_icon("web-home"),tr("Nastavit jako pracovní adresář"));working->setObjectName("fileManagerSetWorkingDirectory");working->setEnabled(static_cast<bool>(working_directory_requested));connect(working,&QAction::triggered,this,[this,info]{if(working_directory_requested)working_directory_requested(info.absoluteFilePath());});menu.addSeparator();auto* properties=menu.addAction(resource_icon("properties"),tr("Vlastnosti adresáře…"));auto* project=menu.addAction(resource_icon("properties"),is_project_directory(info.absoluteFilePath())?tr("Upravit projektový přehled"):tr("Vytvořit projektový přehled"));project->setEnabled(!read_only);auto* clean=menu.addAction(resource_icon("clean"),tr("Vyčistit"));clean->setEnabled(!read_only&&ancestor_removal_lock(info.absoluteFilePath()).isEmpty());if(!ancestor_removal_lock(info.absoluteFilePath()).isEmpty())clean->setToolTip(tr("Čištění je zakázané ochranou obsahu ve Vlastnostech adresáře."));const auto selected=menu.exec(tree->viewport()->mapToGlobal(point));if(selected==clean)clean_directory(info.absoluteFilePath());else if(selected==open)navigate(info.absoluteFilePath());else if(selected==tab&&new_tab_requested)new_tab_requested(info.absoluteFilePath());else if(selected==properties)edit_directory(info.absoluteFilePath());else if(selected==project)edit_project(info.absoluteFilePath(),!is_project_directory(info.absoluteFilePath()));}

void FileManagerTab::edit_directory(const QString& directory){
    auto* dialog=new DirectoryEditor(directory,settings_,window());dialog->setAttribute(Qt::WA_DeleteOnClose);connect(dialog,&QDialog::accepted,this,[this,dialog,directory]{const DirectoryValues values=dialog->result();const QString index=QDir(directory).filePath(QStringLiteral("0000-index"));if(!QDir().mkpath(index)){QMessageBox::critical(this,tr("Uložení selhalo"),tr("Adresář metadat nelze vytvořit."));return;}QSettings metadata(metadata_path(directory),QSettings::IniFormat);metadata.setAtomicSyncRequired(true);metadata.setValue("Directory/Version",3);if(metadata.value("Directory/Kind").toString().trimmed().isEmpty())metadata.setValue("Directory/Kind","directory");if(metadata.value("Directory/Id").toString().trimmed().isEmpty())metadata.setValue("Directory/Id",QUuid::createUuid().toString(QUuid::WithoutBraces));for(auto it=values.labels.cbegin();it!=values.labels.cend();++it)metadata.setValue(QStringLiteral("Directory/Label/%1").arg(it.key()),it.value());metadata.setValue("Directory/SubdirectoriesAsParts",values.subdirectories_as_parts);metadata.setValue("Directory/AutoIndex",values.auto_index);metadata.setValue("Directory/PreventRemoval",values.prevent_removal);metadata.setValue("Directory/PreventParameterEdit",values.prevent_parameter_edit);
        metadata.setValue("Directory/IncludeParameters",values.include_parameters);metadata.setValue("Directory/IncludeThumbnails",values.include_thumbnails);
        QString logo_name;if(values.branding!=DirectoryBrandingMode::None){const QFileInfo source(values.logo);const QString base=values.branding==DirectoryBrandingMode::LogoOnly?QStringLiteral("logo"):QStringLiteral("logo-text");const QString target=QDir(index).filePath(base+QLatin1Char('.')+source.suffix().toLower());if(clean_path(source.absoluteFilePath())!=clean_path(target)){QFile input(source.absoluteFilePath());QSaveFile output(target);if(!input.open(QIODevice::ReadOnly)||!output.open(QIODevice::WriteOnly)||output.write(input.readAll())<0||!output.commit()){QMessageBox::critical(this,tr("Uložení selhalo"),tr("Logo nelze zkopírovat do adresáře metadat."));return;}}logo_name=QFileInfo(target).fileName();}metadata.setValue("Directory/LogoMode",values.branding==DirectoryBrandingMode::None?QStringLiteral("none"):(values.branding==DirectoryBrandingMode::LogoOnly?QStringLiteral("logo"):QStringLiteral("logo-text")));metadata.setValue("Directory/Logo",logo_name);
        QStringList parameter_ids;metadata.beginGroup("Parameters");metadata.remove("");metadata.endGroup();for(const auto& parameter:values.parameters){parameter_ids<<parameter.id;for(auto it=parameter.labels.cbegin();it!=parameter.labels.cend();++it)metadata.setValue(QStringLiteral("Parameters/%1/Label/%2").arg(parameter.id,it.key()),it.value());}metadata.setValue("Directory/Parameters",parameter_ids.join(','));metadata.sync();if(metadata.status()!=QSettings::NoError){QMessageBox::critical(this,tr("Uložení selhalo"),tr("Metadata nelze atomicky uložit."));return;}for(auto& source:sources_)static_cast<DirectoryTreeFilter*>(source.proxy)->clear_label_cache();refresh_source_labels();if(path_belongs_to(current_directory_,clean_path(directory)))refresh_directory();});dialog->show();
}

void FileManagerTab::edit_project(const QString& directory,bool create){
    QSettings metadata(metadata_path(directory),QSettings::IniFormat);const int version=metadata.value("Directory/Version",create?3:2).toInt();if(version<3&&!create){QMessageBox::information(this,tr("Projektový přehled"),tr("Tento adresář používá starší metadata. Úpravy budou dostupné po řízeném převodu na verzi 3."));return;}
    ProjectValues values{metadata.value("Project/Number").toString(),translated_value(metadata,"Project/Title",settings_.language),metadata.value("Project/Customer").toString(),translated_value(metadata,"Project/Description",settings_.language),metadata.value("Project/Preview").toString(),metadata.value("Project/Logo").toString()};if(values.title.isEmpty())values.title=directory_label(directory,settings_.language);
    auto* dialog=new ProjectEditor(values,directory,settings_,window());dialog->setAttribute(Qt::WA_DeleteOnClose);connect(dialog,&QDialog::accepted,this,[this,dialog,directory]{auto values=dialog->result();QMap<QString,QVariant> updates{{"Directory/Version",3},{"Directory/Kind","project"},{"Directory/Label/"+settings_.language,values.title},{"Project/Number",values.number},{"Project/Title/"+settings_.language,values.title},{"Project/Customer",values.customer},{"Project/Description/"+settings_.language,values.description}};
        if(!QFileInfo(metadata_path(directory)).exists())updates.insert("Directory/Id",QUuid::createUuid().toString(QUuid::WithoutBraces));
        const auto import_image=[&](const QString& selected,const QString& base)->QString{if(selected.isEmpty())return {};const QFileInfo source(selected);if(!source.exists())return selected;const QString index=QDir(directory).filePath("0000-index");QDir().mkpath(index);const QString target=QDir(index).filePath(base+QLatin1Char('.')+source.suffix().toLower());if(clean_path(selected)!=clean_path(target)){QFile input(selected);if(input.open(QIODevice::ReadOnly)){QSaveFile output(target);if(output.open(QIODevice::WriteOnly)){output.write(input.readAll());output.commit();}}}return QFileInfo(target).fileName();};
        updates.insert("Project/Preview",import_image(values.preview,"01"));updates.insert("Project/Logo",import_image(values.logo,"logo"));QString error;if(!atomic_metadata_update(metadata_path(directory),updates,&error)){QMessageBox::critical(this,tr("Uložení selhalo"),error);return;}for(auto& source:sources_)static_cast<DirectoryTreeFilter*>(source.proxy)->clear_label_cache();refresh_source_labels();if(path_belongs_to(current_directory_,clean_path(directory)))refresh_directory();});dialog->show();
}

FileManagerTab::~FileManagerTab(){save_column_layout();}
void FileManagerTab::refresh_materials(){
    for(auto it=native_parameter_cache_.begin();it!=native_parameter_cache_.end();)if(QFileInfo(it.key()).suffix().compare("matz",Qt::CaseInsensitive)==0)it=native_parameter_cache_.erase(it);else ++it;
    refresh_overview();refresh_files();update_actions();
}

namespace {
QString column_id(QTreeWidget* table,int column){
    if(column<file_parameter_first_column)return QStringList{"file","preview","type","size","modified"}.at(column);
    return "parameter:"+table->headerItem()->data(column,Qt::UserRole).toString();
}
QString column_settings_path(const ApplicationSettings& settings){
    if(!settings.local_config_path.isEmpty())return QDir(QFileInfo(settings.local_config_path).absolutePath()).filePath("file-manager-layout.ini");
    if(!settings.installation_root.isEmpty())return QDir(settings.installation_root).filePath("config/file-manager-layout.ini");
    return settings.base_config_path.isEmpty()?QString{}:QDir(QFileInfo(settings.base_config_path).absolutePath()).filePath("file-manager-layout.ini");
}
}
void FileManagerTab::save_column_layout(){
    if(!files_||files_->columnCount()<4||refreshing_files_)return;const QString path=column_settings_path(settings_);if(path.isEmpty())return;
    QSettings settings(path,QSettings::IniFormat);QStringList order;
    for(int visual=0;visual<files_->columnCount();++visual){const int logical=files_->header()->logicalIndex(visual);const QString id=column_id(files_,logical);order<<id;settings.setValue("Widths/"+id,files_->columnWidth(logical));}
    settings.setValue("Table/Order",order);
    settings.setValue("Table/HideType",files_->isColumnHidden(2));
    settings.sync();
}
void FileManagerTab::restore_column_layout(){
    QSettings settings(column_settings_path(settings_),QSettings::IniFormat);
    files_->setColumnHidden(2,settings.value("Table/HideType",false).toBool());
    QStringList order=settings_.base_config_path.isEmpty()?QStringList{}:settings.value("Table/Order").toStringList();
    if(!order.isEmpty()&&!order.contains("preview"))order.insert(std::max<qsizetype>(0,order.indexOf("file")+1),"preview");
    for(int column=0;column<files_->columnCount();++column){files_->resizeColumnToContents(column);const int initial=std::clamp(files_->columnWidth(column),column==0?180:95,column==0?360:240);files_->setColumnWidth(column,settings.value("Widths/"+column_id(files_,column),initial).toInt());}
    files_->setColumnWidth(file_preview_column,settings.value("Widths/preview",files_->iconSize().width()+4).toInt());
    int visual=0;for(const auto& id:order)for(int logical=0;logical<files_->columnCount();++logical)if(column_id(files_,logical)==id){files_->header()->moveSection(files_->header()->visualIndex(logical),visual++);break;}
}

void FileManagerTab::clean_directory(const QString& directory){
    const int source=source_index_for(directory);if(source<0||sources_.at(source).source.read_only||!ancestor_removal_lock(directory).isEmpty())return;
    // Parts' zima-clean rule: every numeric suffix is an archive, including
    // orphaned archives and suffixes larger than uint64. Never touch current files.
    const QRegularExpression archive("^.+\\.(?:prtz|asmz|drwz|frmz|tblz|symz)\\.[0-9]+$",QRegularExpression::CaseInsensitiveOption);
    const QString canonical_root=QFileInfo(directory).canonicalFilePath();if(canonical_root.isEmpty())return;
    const auto fingerprint=[](const QString& path){QFile file(path);if(!file.open(QIODevice::ReadOnly))return QByteArray{};QCryptographicHash hash(QCryptographicHash::Sha256);if(!hash.addData(&file))return QByteArray{};return hash.result();};
    QStringList candidates;QMap<QString,QByteArray> fingerprints;QDirIterator iterator(directory,QDir::Files|QDir::NoDotAndDotDot,QDirIterator::Subdirectories);
    while(iterator.hasNext()){const QString path=iterator.next();const QFileInfo info(path);const int owner=source_index_for(path);if(info.isSymLink()||info.isJunction()||owner<0||sources_[owner].source.read_only||!path_belongs_to(info.canonicalFilePath(),canonical_root)||!archive.match(info.fileName()).hasMatch()||!removal_lock(info).isEmpty())continue;const auto hash=fingerprint(path);if(hash.isEmpty())continue;candidates<<path;fingerprints[path]=hash;}
    if(candidates.isEmpty()){QMessageBox::information(this,tr("Vyčistit"),tr("Žádné archivní verze k vyčištění."));return;}
    QMessageBox confirmation(QMessageBox::Question,tr("Vyčistit"),tr("Přesunout %1 archivních verzí do koše? Aktuální dokumenty zůstanou zachovány.").arg(candidates.size()),QMessageBox::Yes|QMessageBox::No,this);confirmation.setDefaultButton(QMessageBox::No);confirmation.setDetailedText(candidates.join('\n'));if(confirmation.exec()!=QMessageBox::Yes)return;
    for(const auto& path:candidates){const QFileInfo info(path);if(!removal_lock(info).isEmpty()||info.isSymLink()||!path_belongs_to(info.canonicalFilePath(),canonical_root)||fingerprint(path)!=fingerprints.value(path)||!QFile::moveToTrash(path)){QMessageBox::warning(this,tr("Operace se nezdařila"),tr("Archivní verzi nelze přesunout do koše: %1").arg(path));break;}}
    refresh_directory();
}

QStringList FileManagerTab::selected_paths()const{QStringList paths;for(int i=0;i<files_->topLevelItemCount();++i){auto* item=files_->topLevelItem(i);if(item->checkState(0)==Qt::Checked)paths<<item->data(0,Qt::UserRole).toString();}return paths;}

void FileManagerTab::copy_selected(bool move){const auto paths=selected_paths();if(paths.isEmpty()){QMessageBox::information(this,tr("Správce souborů"),tr("Nejprve zaškrtněte soubory nebo adresáře."));return;}if(move)for(const auto& path:paths){const QString locked=removal_lock(QFileInfo(path));if(!locked.isEmpty()){QMessageBox::warning(this,tr("Operace se nezdařila"),tr("Adresář „%1“ je chráněný. Ochranu vypněte ve Vlastnostech adresáře před mazáním nebo přesouváním jeho obsahu.").arg(QDir::toNativeSeparators(locked)));return;}}const QString destination=choose_directory(this,move?tr("Přesunout do"):tr("Kopírovat do"),working_directory_,settings_.translations);if(destination.isEmpty())return;for(const auto& path:paths){QString error;if(!copy_path(path,destination,&error)){QMessageBox::critical(this,tr("Operace se nezdařila"),error);return;}if(move){const QFileInfo info(path);bool removed=info.isDir()?QDir(path).removeRecursively():QFile::remove(path);if(!removed){QMessageBox::critical(this,tr("Operace se nezdařila"),tr("Kopie vznikla, ale původní položku nelze odstranit: %1").arg(path));return;}}}refresh_directory();}

void FileManagerTab::delete_selected(){const auto paths=selected_paths();if(paths.isEmpty()){QMessageBox::information(this,tr("Správce souborů"),tr("Nejprve zaškrtněte soubory nebo adresáře."));return;}for(const auto& path:paths){const QString locked=removal_lock(QFileInfo(path));if(!locked.isEmpty()){QMessageBox::warning(this,tr("Odstranění selhalo"),tr("Adresář „%1“ je chráněný. Ochranu vypněte ve Vlastnostech adresáře před mazáním nebo přesouváním jeho obsahu.").arg(QDir::toNativeSeparators(locked)));return;}}if(QMessageBox::warning(this,tr("Smazat vybrané položky"),tr("Opravdu chcete přesunout %1 vybraných položek do koše?").arg(paths.size()),QMessageBox::Yes|QMessageBox::No,QMessageBox::No)!=QMessageBox::Yes)return;for(const auto& path:paths){if(!QFile::moveToTrash(path)){QMessageBox::critical(this,tr("Odstranění selhalo"),tr("Položku nelze přesunout do koše: %1").arg(path));break;}}refresh_directory();}

void FileManagerTab::set_working_directory_action(QAction* action){choose_working_directory_->setDefaultAction(action);choose_working_directory_->setToolButtonStyle(Qt::ToolButtonIconOnly);}

void FileManagerTab::set_working_directory(const QString& path){working_directory_=clean_path(path);if(!sources_.isEmpty()){sources_[0].source.path=working_directory_;sources_[0].source.resolved_path=working_directory_;sources_[0].model->setRootPath(working_directory_);sources_[0].tree->setRootIndex(sources_[0].proxy->mapFromSource(sources_[0].model->index(working_directory_)));source_list_->item(0)->setToolTip(QDir::toNativeSeparators(working_directory_));}const QString target=source_index_for(current_directory_)<0?working_directory_:current_directory_;history_.clear();history_index_=-1;navigate(target);}

void FileManagerTab::reload_settings(const ApplicationSettings& settings){const QString current=current_directory_;settings_=settings;sync_overview_theme();rebuild_sources();history_.clear();history_index_=-1;navigate(source_index_for(current)>=0?current:working_directory_);}

} // namespace zima::app
