#include <zima/ui/numeric_value_lock.hpp>
#include "assembly_workspace_window.hpp"
#include "file_dialog.hpp"
#include "resource_icon.hpp"
#include <zima/viewer/mesh_view.hpp>
#include <zima/drawing/drawing_template.hpp>
#include <zima/ui/properties_subwindow.hpp>
#include <zima/kernel/stable_id.hpp>
#include <QDoubleSpinBox>
#include <QCheckBox>
#include <QBuffer>
#include <QImageReader>
#include <QSvgRenderer>
#include <QFile>
#include <QFileInfo>
#include <QPushButton>
#include <QComboBox>
#include <QDialogButtonBox>
#include <QFormLayout>
#include <QLabel>
#include <QLineEdit>
#include <QMessageBox>
#include <QSignalBlocker>
#include <QTreeWidgetItemIterator>
#include <QVBoxLayout>
#include <algorithm>
#include <cmath>

namespace zima::app {
namespace {
zima::sketcher::TemplateImage read_template_image(const QString& path) {
    if(QFileInfo(path).suffix().compare("svg",Qt::CaseInsensitive)==0) {
        QFile source(path);if(!source.open(QIODevice::ReadOnly))throw std::runtime_error(QObject::tr("SVG nelze otevřít.").toStdString());
        if(source.size()>24*1024*1024)throw std::runtime_error(QObject::tr("SVG je příliš velké (nejvýše 24 MB).").toStdString());
        const auto bytes=source.readAll();QSvgRenderer renderer(bytes);
        if(!renderer.isValid()||renderer.viewBoxF().isEmpty())throw std::runtime_error(QObject::tr("Soubor neobsahuje platný obrázek SVG.").toStdString());
        zima::sketcher::TemplateImage result;result.id=zima::kernel::make_stable_id();result.name=QFileInfo(path).fileName().toStdString();
        result.format="svg";result.data_base64=bytes.toBase64().toStdString();
        const auto size=renderer.viewBoxF().size();result.pixel_width=size.width();result.pixel_height=size.height();
        result.width=30;result.height=30*size.height()/size.width();result.validate();return result;
    }
    QImageReader reader(path);reader.setAutoTransform(true);
    const auto size=reader.size();
    if(size.isValid() && static_cast<double>(size.width())*size.height()>32'000'000)
        throw std::runtime_error(QObject::tr("Obrázek je příliš velký (nejvýše 32 megapixelů).").toStdString());
    const auto image=reader.read();
    if(image.isNull())throw std::runtime_error(QObject::tr("Obrázek nelze načíst: %1").arg(reader.errorString()).toStdString());
    QByteArray bytes;QBuffer buffer(&bytes);buffer.open(QIODevice::WriteOnly);
    if(!image.save(&buffer,"PNG"))throw std::runtime_error(QObject::tr("Obrázek nelze uložit do razítka.").toStdString());
    zima::sketcher::TemplateImage result;
    result.id=zima::kernel::make_stable_id();result.name=QFileInfo(path).fileName().toStdString();
    result.data_base64=bytes.toBase64().toStdString();result.pixel_width=image.width();result.pixel_height=image.height();
    result.width=30;result.height=result.width*image.height()/image.width();result.validate();return result;
}
class TemplateImageDialog final : public zima::ui::PropertiesSubWindow {
public:
    using Image=zima::sketcher::TemplateImage;
    TemplateImageDialog(Image initial,bool placed,std::function<void(const Image&)> preview,
        std::function<void(Image)> commit,QWidget* parent)
        :PropertiesSubWindow(tr("Vlastnosti obrázku"),parent),initial_(std::move(initial)),placed_(placed),preview_(std::move(preview)),commit_(std::move(commit)) {
        setObjectName("templateImageDialog");setAttribute(Qt::WA_DeleteOnClose);set_initial_size({420,420});
        auto* form=new QFormLayout;
        file_=new QLabel(QString::fromStdString(initial_.name),this);file_->setWordWrap(true);form->addRow(tr("Obrázek"),file_);
        auto* replace=new QPushButton(tr("Vybrat soubor…"),this);replace->setObjectName("templateImageReplace");form->addRow({},replace);
        const std::array labels{tr("X"),tr("Y"),tr("Šířka"),tr("Výška")};
        const std::array values{initial_.x,initial_.y,initial_.width,initial_.height};
        for(std::size_t i=0;i<fields_.size();++i) {
            fields_[i]=new QDoubleSpinBox(this);fields_[i]->setObjectName(QString("templateImageValue%1").arg(i));
            fields_[i]->setDecimals(zima::ui::numeric_decimal_places(parent));fields_[i]->setRange(i<2?-100000:0.001,100000);
            fields_[i]->setSuffix(" mm");fields_[i]->setValue(values[i]);form->addRow(labels[i],fields_[i]);
            const std::array<const char*,4> keys{"x","y","width","height"};
            zima::ui::bind_numeric_value_lock(fields_[i],keys[i],initial_.value_locks,[this]{update_preview();});
        }
        lock_=new QCheckBox(tr("Zachovat poměr stran"),this);lock_->setObjectName("templateImageAspectLock");lock_->setChecked(initial_.lock_aspect);form->addRow({},lock_);
        horizontal_=new QComboBox(this);horizontal_->setObjectName("templateImageHorizontal");
        horizontal_->addItem(tr("Vlevo"),"left");horizontal_->addItem(tr("Na střed"),"center");horizontal_->addItem(tr("Vpravo"),"right");
        horizontal_->setCurrentIndex(horizontal_->findData(QString::fromStdString(initial_.horizontal)));form->addRow(tr("Vodorovné zarovnání"),horizontal_);
        vertical_=new QComboBox(this);vertical_->setObjectName("templateImageVertical");
        vertical_->addItem(tr("Dole"),"bottom");vertical_->addItem(tr("Uprostřed"),"middle");vertical_->addItem(tr("Nahoře"),"top");
        vertical_->setCurrentIndex(vertical_->findData(QString::fromStdString(initial_.vertical)));form->addRow(tr("Svislé zarovnání"),vertical_);
        content_layout()->addLayout(form);
        note_=new QLabel(tr("Klikněte do skici na bod umístění, nebo zadejte souřadnice. Obrázek je uložen přímo v razítku."),this);note_->setWordWrap(true);content_layout()->addWidget(note_);
        for(std::size_t i=0;i<4;++i)connect(fields_[i],&QDoubleSpinBox::valueChanged,this,[this,i]{
            if(i<2)placed_=true;else resize_other(i);update_preview();
        });
        connect(lock_,&QCheckBox::toggled,this,[this]{resize_other(2);update_preview();});
        connect(horizontal_,&QComboBox::currentIndexChanged,this,[this]{update_preview();});
        connect(vertical_,&QComboBox::currentIndexChanged,this,[this]{update_preview();});
        connect(replace,&QPushButton::clicked,this,[this]{
            const auto path=open_file(this,tr("Vybrat obrázek"),{},tr("Obrázky (*.svg *.png *.jpg *.jpeg *.bmp *.webp)"));if(path.isEmpty())return;
            try {const auto image=read_template_image(path);initial_.name=image.name;initial_.data_base64=image.data_base64;initial_.format=image.format;
                initial_.pixel_width=image.pixel_width;initial_.pixel_height=image.pixel_height;file_->setText(QString::fromStdString(initial_.name));resize_other(2);update_preview();
            }catch(const std::exception& e){QMessageBox::warning(this,tr("Obrázek"),QString::fromUtf8(e.what()));}
        });
        update_preview();
    }
    void set_anchor(double x,double y) {
        const QSignalBlocker a(fields_[0]),b(fields_[1]);if(!fields_[0]->isReadOnly())fields_[0]->setValue(x);if(!fields_[1]->isReadOnly())fields_[1]->setValue(y);placed_=true;update_preview();
    }
protected:
    bool submit() override {
        if(!placed_)throw std::runtime_error(QObject::tr("Nejprve určete bod umístění obrázku.").toStdString());
        auto image=pending();image.validate();commit_(std::move(image));return true;
    }
private:
    Image pending() const {
        auto image=initial_;image.x=fields_[0]->value();image.y=fields_[1]->value();image.width=fields_[2]->value();image.height=fields_[3]->value();
        image.horizontal=horizontal_->currentData().toString().toStdString();image.vertical=vertical_->currentData().toString().toStdString();image.lock_aspect=lock_->isChecked();return image;
    }
    void resize_other(std::size_t changed) {
        if(!lock_->isChecked() || (fields_[2]->isReadOnly() && fields_[3]->isReadOnly()))return;
        const double ratio=static_cast<double>(initial_.pixel_width)/initial_.pixel_height;
        auto* target=fields_[changed==2?3:2];const QSignalBlocker blocked(target);
        if(target->isReadOnly()) {
            auto* source=fields_[changed];const QSignalBlocker restore(source);
            source->setValue(changed==2?target->value()*ratio:target->value()/ratio);return;
        }
        target->setValue(changed==2?fields_[2]->value()/ratio:fields_[3]->value()*ratio);
    }
    void update_preview() {if(placed_&&preview_)preview_(pending());}
    Image initial_;bool placed_{};std::function<void(const Image&)> preview_;std::function<void(Image)> commit_;
    std::array<QDoubleSpinBox*,4> fields_{};QCheckBox* lock_{};QComboBox* horizontal_{};QComboBox* vertical_{};QLabel* file_{};QLabel* note_{};
};
class RepeatRegionDialog final : public zima::ui::PropertiesSubWindow {
public:
    RepeatRegionDialog(zima::sketcher::SketchRepeatRegion initial,
        std::function<void(zima::sketcher::SketchRepeatRegion)> commit,QWidget* parent)
        : PropertiesSubWindow(tr("Vlastnosti oblasti kusovníku"),parent),initial_(std::move(initial)),commit_(std::move(commit)) {
        setObjectName("templateRepeatRegionDialog");setAttribute(Qt::WA_DeleteOnClose);
        set_initial_size({360,360});auto* form=new QFormLayout;
        const std::array labels{tr("X"),tr("Y"),tr("Šířka"),tr("Výška"),tr("Rozteč opakování")};
        const std::array values{initial_.x,initial_.y,initial_.width,initial_.height,initial_.step};
        for(std::size_t i=0;i<fields_.size();++i){
            fields_[i]=new QDoubleSpinBox(this);fields_[i]->setObjectName(QString("templateRegionValue%1").arg(i));
            fields_[i]->setDecimals(zima::ui::numeric_decimal_places(parent));fields_[i]->setRange(i<2?-100000:0.001,100000);
            fields_[i]->setValue(values[i]);fields_[i]->setSuffix(" mm");form->addRow(labels[i],fields_[i]);
            const std::array<const char*,5> keys{"x","y","width","height","step"};
            zima::ui::bind_numeric_value_lock(fields_[i],keys[i],initial_.value_locks);
        }
        direction_=new QComboBox(this);direction_->setObjectName("templateRegionDirection");
        direction_->addItem(tr("Nahoru"),"up");direction_->addItem(tr("Dolů"),"down");direction_->addItem(tr("Doleva"),"left");direction_->addItem(tr("Doprava"),"right");
        direction_->setCurrentIndex(direction_->findData(QString::fromStdString(initial_.direction)));form->addRow(tr("Směr opakování"),direction_);
        content_layout()->addLayout(form);
        auto* note=new QLabel(tr("Opakuje se geometrie a text uvnitř oblasti. Pro číslo položky použijte &bom.item_number, pro množství &bom.quantity. Fialový obrys se netiskne."),this);note->setWordWrap(true);content_layout()->addWidget(note);
    }
protected:
    bool submit() override {
        auto r=initial_;r.x=fields_[0]->value();r.y=fields_[1]->value();r.width=fields_[2]->value();r.height=fields_[3]->value();r.step=fields_[4]->value();r.direction=direction_->currentData().toString().toStdString();
        zima::drawing::validate_repeat_region(r);commit_(std::move(r));return true;
    }
private:
    zima::sketcher::SketchRepeatRegion initial_;
    std::function<void(zima::sketcher::SketchRepeatRegion)> commit_;
    std::array<QDoubleSpinBox*,5> fields_{};QComboBox* direction_{};
};
}
const zima::sketcher::Sketch* AssemblyWorkspaceWindow::template_sketch() const {
    const auto* part=workspace_.open_part(workspace_.active_document_id());
    if(!part||part->session.document().sketches.empty())return nullptr;
    const auto& sketch=part->session.document().sketches.front();return sketch.drawing_template?&sketch:nullptr;
}
void AssemblyWorkspaceWindow::save_template_document(bool copy) {
    const auto* sketch=template_sketch();if(!sketch)return;
    const auto id=workspace_.active_document_id();auto* part=workspace_.open_part(id);
    const bool title=sketch->drawing_template->kind=="title_block";
    const QString suffix=title?"tblz":"frmz";
    const QString filter=title?tr("Razítko ZIMA-CAD (*.tblz)"):tr("Rámeček ZIMA-CAD (*.frmz)");
    QString path=QString::fromStdString(part->path.string());
    if(copy||path.isEmpty())path=save_file(this,copy?tr("Uložit kopii šablony"):tr("Uložit šablonu"),
        QString::fromStdString((working_directory_/(part->session.document().name+"."+suffix.toStdString())).string()),filter,suffix,application_settings_.translations);
    if(path.isEmpty())return;
    auto target=std::filesystem::path(path.toStdString());target.replace_extension("."+suffix.toStdString());
    if(copy&&std::filesystem::absolute(target).lexically_normal()==std::filesystem::absolute(part->path).lexically_normal()) {
        QMessageBox::warning(this,tr("Uložit kopii"),tr("Kopie musí mít jiný název než původní šablona."));return;
    }
    if(const auto other=workspace_.document_id_for_path(target);other&&*other!=id) {
        QMessageBox::warning(this,tr("Soubor je otevřen"),tr("Cílovou šablonu již upravujete v jiné kartě."));return;
    }
    try {zima::drawing::save_template_sketch(*sketch,target);if(!copy){part->path=target;part->session.mark_saved();}
        working_directory_=target.parent_path();refresh_tabs();state_->setText(tr("Šablona uložena: %1").arg(QString::fromStdString(target.filename().string())));
    }catch(const std::exception& e){QMessageBox::critical(this,tr("Uložení šablony selhalo"),QString::fromUtf8(e.what()));}
}
void AssemblyWorkspaceWindow::start_template_region() {
    const auto* sketch=template_sketch();if(!sketch||sketch->drawing_template->kind!="title_block"||properties_dialog_)return;
    cancel_sketch_segment();clear_selected_sketch_geometry();viewer_->clear_selection();
    template_region_picking_=true;template_region_first_.reset();
    viewer_->set_selection_contract({});state_->setText(tr("Oblast kusovníku: určete první a protější roh fialového obdélníku."));
}
bool AssemblyWorkspaceWindow::template_region_ray(const zima::kernel::Vec3& origin,const zima::kernel::Vec3& direction,bool commit) {
    if(!template_region_picking_)return false;
    const auto* sketch=template_sketch();if(!sketch)return false;
    const auto point=sketch->intersect_ray(origin,direction);if(!point)return true;
    if(!template_region_first_){if(commit)template_region_first_=*point;return true;}
    const auto first=*template_region_first_;
    zima::sketcher::SketchRepeatRegion region{zima::kernel::make_stable_id(),std::min(first[0],(*point)[0]),std::min(first[1],(*point)[1]),std::abs(first[0]-(*point)[0]),std::abs(first[1]-(*point)[1]),"up",std::abs(first[1]-(*point)[1])};
    if(region.width<0.001||region.height<0.001)return true;
    if(commit){template_region_picking_=false;template_region_first_.reset();preserve_view_on_refresh_=true;refresh_scene();show_template_region_properties({},region);}
    else {auto preview=*sketch;preview.drawing_template->repeat_regions.push_back(region);show_sketch_drag_preview(preview);}
    return true;
}
void AssemblyWorkspaceWindow::show_template_region_properties(const std::string& id,std::optional<zima::sketcher::SketchRepeatRegion> initial) {
    const auto* sketch=template_sketch();if(!sketch||properties_dialog_)return;
    if(!initial){const auto found=std::ranges::find(sketch->drawing_template->repeat_regions,id,&zima::sketcher::SketchRepeatRegion::id);if(found==sketch->drawing_template->repeat_regions.end())return;initial=*found;}
    const auto owner=workspace_.active_document_id();
    auto* dialog=new RepeatRegionDialog(*initial,[this,owner,id](auto region){
        auto* part=workspace_.open_part(owner);if(!part)throw std::runtime_error(QObject::tr("Šablona již není otevřená.").toStdString());
        auto next=part->session.document();auto& regions=next.sketches.front().drawing_template->repeat_regions;
        const auto found=std::ranges::find(regions,id,&zima::sketcher::SketchRepeatRegion::id);
        if(found==regions.end())regions.push_back(std::move(region));else *found=std::move(region);
        part->session.commit(std::move(next),{});
    },this);properties_dialog_=dialog;
    connect(dialog,&QDialog::finished,this,[this,dialog]{if(properties_dialog_==dialog)properties_dialog_=nullptr;preserve_view_on_refresh_=true;refresh_tabs();refresh_scene();});dialog->show();
}
void AssemblyWorkspaceWindow::remove_template_region(const std::string& id) {
    auto* part=workspace_.open_part(workspace_.active_document_id());if(!part||!template_sketch()||properties_dialog_)return;
    auto next=part->session.document();std::erase_if(next.sketches.front().drawing_template->repeat_regions,[&](const auto& r){return r.id==id;});
    part->session.commit(std::move(next),{});selected_template_region_.clear();viewer_->clear_selection();preserve_view_on_refresh_=true;refresh_tabs();refresh_scene();
}
void AssemblyWorkspaceWindow::select_template_region(const std::string& id) {
    clear_selected_sketch_geometry();selected_template_region_=id;const QSignalBlocker blocked(tree_);tree_->clearSelection();
    QTreeWidgetItemIterator it(tree_);while(*it){auto* item=*it++;
        if(item->data(0,Qt::UserRole+3).toString()=="template-repeat-region"&&item->data(0,Qt::UserRole).toString().toStdString()==id){item->setSelected(true);tree_->scrollToItem(item);break;}}
    state_->setText(tr("Vybrána oblast kusovníku. Dvojklik otevře vlastnosti."));
}
void AssemblyWorkspaceWindow::start_template_image() {
    const auto* sketch=template_sketch();if(!sketch||sketch->drawing_template->kind!="title_block"||properties_dialog_)return;
    const auto path=open_file(this,tr("Vybrat obrázek"),QString::fromStdString(working_directory_.string()),tr("Obrázky (*.svg *.png *.jpg *.jpeg *.bmp *.webp)"),application_settings_.translations);
    if(path.isEmpty())return;
    try {auto image=read_template_image(path);cancel_sketch_segment();clear_selected_sketch_geometry();viewer_->clear_selection();show_template_image_properties({},std::move(image));}
    catch(const std::exception& e){QMessageBox::warning(this,tr("Obrázek"),QString::fromUtf8(e.what()));}
}
void AssemblyWorkspaceWindow::show_template_image_properties(const std::string& id,std::optional<zima::sketcher::TemplateImage> initial) {
    const auto* sketch=template_sketch();if(!sketch||properties_dialog_)return;
    if(!initial){const auto found=std::ranges::find(sketch->drawing_template->images,id,&zima::sketcher::TemplateImage::id);if(found==sketch->drawing_template->images.end())return;initial=*found;}
    const auto owner=workspace_.active_document_id();
    auto* dialog=new TemplateImageDialog(*initial,!id.empty(),[this,owner,id](const auto& image){
        if(workspace_.active_document_id()!=owner)return;const auto* current=template_sketch();if(!current)return;
        auto preview=*current;auto& images=preview.drawing_template->images;
        const auto found=std::ranges::find(images,id,&zima::sketcher::TemplateImage::id);
        if(found==images.end())images.push_back(image);else *found=image;show_sketch_drag_preview(preview);
    },[this,owner,id](auto image){
        auto* part=workspace_.open_part(owner);if(!part)throw std::runtime_error(QObject::tr("Šablona již není otevřená.").toStdString());
        auto next=part->session.document();auto& images=next.sketches.front().drawing_template->images;
        const auto found=std::ranges::find(images,id,&zima::sketcher::TemplateImage::id);
        if(found==images.end())images.push_back(std::move(image));else *found=std::move(image);
        part->session.commit(std::move(next),{});
    },this);
    properties_dialog_=dialog;template_image_anchor_=[dialog](double x,double y){dialog->set_anchor(x,y);};
    viewer_->set_selection_contract({});
    connect(dialog,&QDialog::finished,this,[this,dialog]{template_image_anchor_={};if(properties_dialog_==dialog)properties_dialog_=nullptr;preserve_view_on_refresh_=true;refresh_tabs();refresh_scene();});dialog->show();
}
bool AssemblyWorkspaceWindow::template_image_ray(const zima::kernel::Vec3& origin,const zima::kernel::Vec3& direction) {
    if(!template_image_anchor_)return false;
    if(const auto* sketch=template_sketch())if(const auto point=sketch->intersect_ray(origin,direction))template_image_anchor_((*point)[0],(*point)[1]);
    return true;
}
void AssemblyWorkspaceWindow::remove_template_image(const std::string& id) {
    auto* part=workspace_.open_part(workspace_.active_document_id());if(!part||!template_sketch()||properties_dialog_)return;
    auto next=part->session.document();std::erase_if(next.sketches.front().drawing_template->images,[&](const auto& i){return i.id==id;});
    part->session.commit(std::move(next),{});selected_template_image_.clear();viewer_->clear_selection();preserve_view_on_refresh_=true;refresh_tabs();refresh_scene();
}
void AssemblyWorkspaceWindow::select_template_image(const std::string& id) {
    clear_selected_sketch_geometry();selected_template_image_=id;const QSignalBlocker blocked(tree_);tree_->clearSelection();
    QTreeWidgetItemIterator it(tree_);while(*it){auto* item=*it++;
        if(item->data(0,Qt::UserRole+3).toString()=="template-image"&&item->data(0,Qt::UserRole).toString().toStdString()==id){item->setSelected(true);tree_->scrollToItem(item);break;}}
    state_->setText(tr("Vybrán obrázek. Dvojklik otevře vlastnosti."));
}
} // namespace zima::app
