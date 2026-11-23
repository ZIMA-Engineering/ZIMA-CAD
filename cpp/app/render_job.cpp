#include "assembly_workspace_window.hpp"
#include "workspace/workspace_internal.hpp"
#include "file_dialog.hpp"
#include <zima/render/renderer.hpp>
#include <zima/workspace/render_operations.hpp>
#include <zima/drawing_render/image_export.hpp>
#include <zima/document/file_path.hpp>
#include <zima/kernel/stable_id.hpp>
#include <QBuffer>
#include <QDateTime>
#include <QFileInfo>
#include <QImageReader>
#include <QListWidget>
#include <QPainter>
#include <QProgressBar>
#include <QPushButton>
#include <QScrollArea>
#include <QSplitter>
#include <QTimer>
#include <atomic>
#include <algorithm>
#include <chrono>
#include <mutex>
#include <thread>

namespace zima::app {
using namespace workspace_detail;
namespace {
QString tab_id(const std::string& document,const std::string& body) {return QString::fromStdString("render:"+document+":"+body);}
QImage decode_image(const std::string& encoded,QSize expected={}) {
    const auto decoded=QByteArray::fromBase64Encoding(QByteArray::fromStdString(encoded),QByteArray::AbortOnBase64DecodingErrors);
    if(!decoded)return {};
    QBuffer buffer;buffer.setData(decoded.decoded);buffer.open(QIODevice::ReadOnly);
    QImageReader reader(&buffer,"PNG");const auto size=reader.size();
    if(!size.isValid()||size.width()>8192||size.height()>8192||qint64(size.width())*size.height()>33554432||
        (expected.isValid()&&size!=expected))return {};
    return reader.read();
}
std::string encode_image(const QImage& image) {
    QByteArray bytes;QBuffer buffer(&bytes);buffer.open(QIODevice::WriteOnly);
    if(!image.save(&buffer,"PNG"))throw std::runtime_error("Cannot encode the Render image.");
    return bytes.toBase64().toStdString();
}
class RenderCanvas final : public QWidget {
public:
    QImage image;
    explicit RenderCanvas(QWidget* parent):QWidget(parent) {
        setMinimumSize(240,135);setSizePolicy(QSizePolicy::Expanding,QSizePolicy::Expanding);setObjectName("renderImageCanvas");
    }
protected:
    void paintEvent(QPaintEvent*)override {
        QPainter painter(this);painter.fillRect(rect(),palette().brush(QPalette::Base));if(image.isNull())return;
        auto size=image.size();size.scale(this->size(),Qt::KeepAspectRatio);
        const QRect target(QPoint((width()-size.width())/2,(height()-size.height())/2),size);
        painter.setRenderHint(QPainter::SmoothPixmapTransform);painter.drawImage(target,image);
    }
};
class RenderPage final : public QWidget {
public:
    std::string document_id,body_id;
    RenderCanvas* canvas;QScrollArea* scroll;QListWidget* gallery;QProgressBar* progress;QLabel* status;
    QPushButton *keep,*remove,*rename,*stop,*export_button,*fit,*actual;
    QImage live_image;QString live_status;bool live_running{},live_complete{},live_exists{};
    std::string retained_id;
    std::vector<document::RenderImage> saved;
    std::function<void(std::vector<document::RenderImage>)> commit;
    RenderPage(std::string document,std::string body,QWidget* parent):QWidget(parent),document_id(std::move(document)),body_id(std::move(body)) {
        setObjectName("renderGalleryPage");auto* layout=new QVBoxLayout(this);layout->setContentsMargins(8,8,8,8);layout->setSpacing(6);
        auto* tools=new QHBoxLayout;tools->setSpacing(6);layout->addLayout(tools);
        const auto button=[&](const char* name){auto* b=new QPushButton(this);b->setObjectName(name);tools->addWidget(b);return b;};
        fit=button("renderFit");actual=button("renderActualSize");tools->addSpacing(8);
        keep=button("renderKeepImage");rename=button("renderRenameImage");remove=button("renderDeleteImage");
        tools->addStretch();stop=button("renderStop");export_button=button("renderExport");
        auto* split=new QSplitter(this);layout->addWidget(split,1);
        gallery=new QListWidget(split);gallery->setObjectName("renderGalleryImages");gallery->setIconSize({128,80});gallery->setMinimumWidth(150);
        gallery->setMaximumWidth(300);gallery->setViewMode(QListView::IconMode);gallery->setFlow(QListView::TopToBottom);
        gallery->setMovement(QListView::Static);gallery->setWrapping(false);gallery->setResizeMode(QListView::Adjust);
        gallery->setGridSize({140,110});gallery->setTextElideMode(Qt::ElideRight);
        gallery->setSpacing(4);gallery->setEditTriggers(QAbstractItemView::NoEditTriggers);
        scroll=new QScrollArea(split);scroll->setWidgetResizable(true);scroll->setFrameShape(QFrame::NoFrame);
        canvas=new RenderCanvas(scroll);scroll->setWidget(canvas);split->setStretchFactor(0,0);split->setStretchFactor(1,1);split->setSizes({190,900});
        auto* footer=new QHBoxLayout;layout->addLayout(footer);status=new QLabel(this);status->setObjectName("renderProgressStatus");
        status->setWordWrap(true);footer->addWidget(status,1);
        progress=new QProgressBar(this);progress->setObjectName("renderProgress");progress->setFixedWidth(160);footer->addWidget(progress);
        connect(fit,&QPushButton::clicked,this,[this]{scroll->setWidgetResizable(true);canvas->setMaximumSize(QWIDGETSIZE_MAX,QWIDGETSIZE_MAX);canvas->setMinimumSize(240,135);});
        connect(actual,&QPushButton::clicked,this,[this]{if(canvas->image.isNull())return;scroll->setWidgetResizable(false);canvas->setFixedSize(canvas->image.size());});
        connect(gallery,&QListWidget::currentRowChanged,this,[this]{show_selection();});
        connect(rename,&QPushButton::clicked,this,[this]{if(auto* item=gallery->currentItem();item&&!item->data(Qt::UserRole).toString().isEmpty())gallery->editItem(item);});
        connect(remove,&QPushButton::clicked,this,[this]{const auto id=selected_id();if(id.empty())return;auto next=saved;
            std::erase_if(next,[&](const auto& i){return i.id==id;});if(commit)commit(std::move(next));});
        connect(gallery,&QListWidget::itemChanged,this,[this](QListWidgetItem* item){
            const auto id=item->data(Qt::UserRole).toString().toStdString();if(id.empty())return;
            auto next=saved;const auto found=std::ranges::find(next,id,&document::RenderImage::id);if(found==next.end())return;
            const auto name=item->text().trimmed();if(name.isEmpty()){QSignalBlocker block(gallery);item->setText(QString::fromStdString(found->name));return;}
            found->name=name.toStdString();if(commit)commit(std::move(next));
        });
        retranslate();
    }
    std::string selected_id()const {const auto* item=gallery->currentItem();return item?item->data(Qt::UserRole).toString().toStdString():std::string{};}
    void retranslate() {
        fit->setText(tr("Fit image"));actual->setText("100%");keep->setText(tr("Keep image"));rename->setText(tr("Rename"));
        remove->setText(tr("Delete image"));stop->setText(tr("Stop rendering"));export_button->setText(tr("Exportovat"));
        keep->setToolTip(tr("Keep this snapshot inside the Part file."));rebuild_list();
    }
    void rebuild_list() {
        const auto previous=selected_id();QSignalBlocker block(gallery);gallery->clear();
        if(live_exists){auto* item=new QListWidgetItem(tr("Current render"),gallery);if(!live_image.isNull())item->setIcon(QPixmap::fromImage(live_image.scaled(128,80,Qt::KeepAspectRatio,Qt::SmoothTransformation)));}
        for(const auto& image:saved) {
            auto* item=new QListWidgetItem(QPixmap::fromImage(decode_image(image.data->thumbnail_base64)),QString::fromStdString(image.name),gallery);
            item->setData(Qt::UserRole,QString::fromStdString(image.id));item->setFlags(item->flags()|Qt::ItemIsEditable);
            item->setToolTip(QString::fromStdString(image.created_utc)+QString(" · %1 × %2").arg(image.width).arg(image.height));
        }
        int selected=gallery->count()?0:-1;for(int n=0;n<gallery->count();++n)if(gallery->item(n)->data(Qt::UserRole).toString().toStdString()==previous){selected=n;break;}
        gallery->setCurrentRow(selected);show_selection();
    }
    void set_saved(const std::vector<document::RenderImage>& images) {if(saved==images)return;saved=images;rebuild_list();}
    void show_selection() {
        const auto id=selected_id();const auto found=std::ranges::find(saved,id,&document::RenderImage::id);
        if(found!=saved.end()) {
            canvas->image=decode_image(found->data->png_base64,{int(found->width),int(found->height)});
            status->setText(canvas->image.isNull()?tr("Cannot read the retained Render image."):
                tr("Retained snapshot · %1 × %2 · %3").arg(found->width).arg(found->height).arg(QString::fromStdString(found->created_utc)));
        }else {canvas->image=live_image;status->setText(live_exists?live_status:tr("No retained images."));}
        if(!scroll->widgetResizable()&&!canvas->image.isNull())canvas->setFixedSize(canvas->image.size());canvas->update();
        update_controls();
    }
    void update_controls() {
        const auto found=std::ranges::find(saved,selected_id(),&document::RenderImage::id);
        rename->setEnabled(found!=saved.end());remove->setEnabled(found!=saved.end());
        keep->setEnabled(found==saved.end()&&live_complete&&std::ranges::none_of(saved,[&](const auto& i){return i.id==retained_id;}));
        export_button->setEnabled(!canvas->image.isNull()&&(found!=saved.end()||live_complete));
        stop->setVisible(live_running);progress->setVisible(live_running);actual->setEnabled(!canvas->image.isNull());
    }
protected:
    void changeEvent(QEvent* e)override {QWidget::changeEvent(e);if(e->type()==QEvent::LanguageChange)retranslate();}
};
}
struct AssemblyWorkspaceWindow::RenderJob {
    std::string document_id,body_id,retained_id,captured_utc;
    std::weak_ptr<const int> runtime;
    render::SceneInput input;
    std::atomic<bool> done{false};
    bool accepted{};
    std::mutex mutex;
    QImage latest;
    unsigned completed{};
    bool cancelled{};
    std::string error;
    std::uint64_t rays{};
    double seconds{};
    // Last member: joining precedes destruction of everything used by the worker.
    std::jthread worker;
};
bool AssemblyWorkspaceWindow::render_inputs_current()const {
    if(!render_job_)return false;
    const auto& job=*render_job_;const auto* part=workspace_.open_part(job.document_id);
    if(!part||job.runtime.lock()!=part->runtime_identity)return false;
    const auto& doc=part->session.document();const auto* body=doc.body_history.find(job.body_id);
    if(!body||body->suppressed||!body->render||!document::same_render_scene(*body->render,job.input.definition)||doc.appearance!=job.input.appearance)return false;
    try {
        const auto sources=workspace::read_render_sources(part->session,job.body_id);if(sources.size()!=job.input.sources.size())return false;
        for(std::size_t i=0;i<sources.size();++i)if(sources[i].first!=job.input.sources[i].body_id||!sources[i].second.shares_with(job.input.sources[i].body))return false;
    }catch(const std::exception&){return false;}
    return true;
}
bool AssemblyWorkspaceWindow::render_result_current()const {return render_job_&&render_job_->done.load()&&render_job_->accepted&&render_inputs_current();}
void AssemblyWorkspaceWindow::stop_render(){if(render_job_)render_job_->worker.request_stop();}
bool AssemblyWorkspaceWindow::render_running()const{return render_job_&&!render_job_->done.load();}
bool AssemblyWorkspaceWindow::render_gallery_available()const {
    const auto* part=workspace_.open_part(workspace_.active_document_id());if(!part)return false;
    const auto& doc=part->session.document();const auto* body=doc.body_history.find(doc.body_history.active_body_id());
    return body&&body->render&&(!body->render->images.empty()||(render_job_&&render_job_->document_id==doc.document_id&&render_job_->body_id==body->scope.id));
}
void AssemblyWorkspaceWindow::start_render() {
    if(properties_dialog_||!active_render_body())return;
    if(render_running()){state_->setText(tr("A render is already running."));return;}
    const auto* part=workspace_.open_part(workspace_.active_document_id());const auto& doc=part->session.document();
    auto job=std::make_shared<RenderJob>();job->document_id=doc.document_id;job->body_id=doc.body_history.active_body_id();job->runtime=part->runtime_identity;
    job->captured_utc=QDateTime::currentDateTimeUtc().toString(Qt::ISODate).toStdString();
    try {
        job->input.definition=*doc.body_history.find(job->body_id)->render;job->input.definition.images.clear();job->input.appearance=doc.appearance;
        for(auto& [id,body]:workspace::read_render_sources(part->session,job->body_id))job->input.sources.push_back({std::move(id),std::move(body)});
    }catch(const std::exception& e){state_->setText(tr(e.what()));return;}
    render_job_=job;
    job->worker=std::jthread([target=job.get()](std::stop_token stop) {
        const auto started=std::chrono::steady_clock::now();auto last_publish=started-std::chrono::seconds(1);
        try {
            render::Scene scene(target->input,stop);
            const auto publish=[target,&last_publish](const render::Image& image) {
                const auto now=std::chrono::steady_clock::now();
                if(image.completed_samples!=target->input.definition.settings.samples&&now-last_publish<std::chrono::milliseconds(250))return;
                last_publish=now;QImage pixels(image.rgba.data(),int(image.width),int(image.height),QImage::Format_RGBA8888);
                auto private_image=pixels.copy();std::lock_guard guard(target->mutex);
                target->latest=std::move(private_image);target->completed=image.completed_samples;target->rays=image.rays;
            };
            // Leave CPU capacity for interaction; keep every configured sample and bounce.
            const unsigned workers=std::clamp(std::thread::hardware_concurrency()/2,1u,4u);
            const auto result=scene.render(stop,publish,workers);target->cancelled=result.cancelled||stop.stop_requested();
        }catch(const std::exception& e){if(stop.stop_requested())target->cancelled=true;else target->error=e.what();}
        target->seconds=std::chrono::duration<double>(std::chrono::steady_clock::now()-started).count();target->done.store(true);
    });
    if(const auto found=render_tabs_.find(tab_id(job->document_id,job->body_id));found!=render_tabs_.end())
        static_cast<RenderPage*>(found->second)->live_exists=false;
    show_render_result();
    if(auto* page=static_cast<RenderPage*>(active_render_page_.data()))page->gallery->setCurrentRow(0);
    auto* timer=new QTimer(this);timer->setInterval(250);
    connect(timer,&QTimer::timeout,this,[this,timer,job] {
        if(render_job_!=job){job->worker.request_stop();timer->stop();timer->deleteLater();return;}
        const auto* part=workspace_.open_part(job->document_id);
        if(!part||part->runtime_identity!=job->runtime.lock()||!part->session.document().body_history.find(job->body_id))job->worker.request_stop();
        if(job->done.load()) {
            timer->stop();timer->deleteLater();job->accepted=!job->cancelled&&job->error.empty()&&job->completed==job->input.definition.settings.samples;
        }
        refresh_render_tabs();
        // Never rebuild the model scene or command toolbar during background progress.
        if(job->done.load()&&active_render_page_)update_document_area_visibility();
    });timer->start();
}
void AssemblyWorkspaceWindow::refresh_render_tabs() {
    for(auto it=render_tabs_.begin();it!=render_tabs_.end();) {
        auto* page=static_cast<RenderPage*>(it->second);const auto* part=workspace_.open_part(page->document_id);
        const auto* body=part?part->session.document().body_history.find(page->body_id):nullptr;
        if(!body||!body->render) {
            if(render_job_&&render_job_->document_id==page->document_id&&render_job_->body_id==page->body_id)stop_render();
            if(active_render_page_==page)active_render_page_=nullptr;
            render_stack_->removeWidget(page);page->deleteLater();it=render_tabs_.erase(it);continue;
        }
        page->setProperty("renderTitle",tr("Render — %1").arg(QString::fromStdString(part->session.document().name+" / "+body->name)));
        if(page==active_render_page_) {
            page->set_saved(body->render->images);
            const auto job=render_job_;const bool has_job=job&&job->document_id==page->document_id&&job->body_id==page->body_id;
            const bool changed=page->live_exists!=has_job;const bool was_running=page->live_running;page->live_exists=has_job;
            if(has_job) {
                const bool done=job->done.load();page->live_running=!done;page->live_complete=done&&job->accepted;page->retained_id=job->retained_id;
                unsigned completed;{std::lock_guard guard(job->mutex);page->live_image=job->latest;completed=job->completed;}
                page->progress->setRange(0,int(job->input.definition.settings.samples));page->progress->setValue(int(completed));
                page->live_status=!done?tr("Rendering: %1 / %2 samples").arg(completed).arg(job->input.definition.settings.samples):
                    !job->error.empty()?tr(job->error.c_str()):job->cancelled?tr("Rendering cancelled."):
                    !render_inputs_current()?tr("Snapshot of an earlier model state. You can keep or export it."):
                    tr("Rendered %1 × %2 pixels in %3 s. Export with F7.").arg(job->input.definition.settings.width).arg(job->input.definition.settings.height).arg(job->seconds,0,'f',1);
            }else {page->live_image={};page->live_running=page->live_complete=false;}
            if(changed||(was_running&&!page->live_running))page->rebuild_list();
            else if(page->selected_id().empty())page->show_selection();else page->update_controls();
        }
        ++it;
    }
}
void AssemblyWorkspaceWindow::select_render_tab(const QString& id) {
    const auto found=render_tabs_.find(id);if(found==render_tabs_.end())return;
    if(properties_dialog_||!active_sketch_id_.empty()||section_dialog_){refresh_tabs();return;}
    auto* page=static_cast<RenderPage*>(found->second);
    if(workspace_.displayed_document_id()!=page->document_id) {
        document_camera_states_[workspace_.active_document_id()]=viewer_->camera_state();
        workspace_.activate(page->document_id);workspace_.display_top_level(page->document_id);refresh_scene();
    }
    active_browser_=nullptr;active_file_manager_=nullptr;active_preview_=nullptr;active_render_page_=page;
    render_stack_->setCurrentWidget(page);refresh_render_tabs();update_document_area_visibility();rebuild_application_toolbar();
}
bool AssemblyWorkspaceWindow::close_render_tab(int index) {
    if(index<0||index>=tabs_->count())return false;const auto id=tabs_->tabData(index).toString();
    const auto found=render_tabs_.find(id);if(found==render_tabs_.end())return false;
    auto* page=found->second;const bool active=page==active_render_page_;if(active)active_render_page_=nullptr;
    render_stack_->removeWidget(page);page->deleteLater();render_tabs_.erase(found);refresh_tabs();if(active)rebuild_application_toolbar();return true;
}
void AssemblyWorkspaceWindow::show_render_result() {
    if(properties_dialog_||!active_sketch_id_.empty()||section_dialog_)return;
    const auto* part=workspace_.open_part(workspace_.active_document_id());if(!part)return;
    auto document_id=part->session.document().document_id,body_id=part->session.document().body_history.active_body_id();
    const auto* owner=workspace_.open_part(document_id);const auto* body=owner?owner->session.document().body_history.find(body_id):nullptr;
    if(!body||!body->render)return;
    const auto id=tab_id(document_id,body_id);
    if(!render_tabs_.contains(id)) {
        auto* page=new RenderPage(document_id,body_id,render_stack_);render_stack_->addWidget(page);render_tabs_[id]=page;
        page->commit=[this,page](std::vector<document::RenderImage> images){try {
            static_cast<void>(workspace::commit_render_images(workspace_,page->document_id,page->body_id,std::move(images)));refresh_tabs();
        }catch(const std::exception& e){state_->setText(tr(e.what()));refresh_render_tabs();}};
        connect(page->keep,&QPushButton::clicked,this,[this]{retain_render_image();});
        connect(page->stop,&QPushButton::clicked,this,[this]{stop_render();});
        connect(page->export_button,&QPushButton::clicked,this,[this]{export_render_image();});
    }
    select_render_tab(id);refresh_tabs();
}
void AssemblyWorkspaceWindow::retain_render_image() {
    auto* page=static_cast<RenderPage*>(active_render_page_.data());const auto job=render_job_;
    if(!page||!job||!job->done.load()||!job->accepted||page->document_id!=job->document_id||page->body_id!=job->body_id)return;
    try {
        const auto pixels=job->latest;
        auto data=run_background_task([pixels]{auto data=std::make_shared<document::RenderImageData>();data->png_base64=encode_image(pixels);
            data->thumbnail_base64=encode_image(pixels.scaled(160,100,Qt::KeepAspectRatio,Qt::SmoothTransformation));return data;});
        const auto* part=workspace_.open_part(job->document_id);const auto* body=part?part->session.document().body_history.find(job->body_id):nullptr;
        if(!body||!body->render||part->runtime_identity!=job->runtime.lock())return;
        auto images=body->render->images;if(std::ranges::any_of(images,[&](const auto& i){return i.id==job->retained_id;}))return;
        document::RenderImage image;image.id=kernel::make_stable_id();image.name=tr("Image %1").arg(images.size()+1).toStdString();
        image.created_utc=job->captured_utc;image.width=pixels.width();image.height=pixels.height();image.data=std::move(data);
        const auto retained=image.id;images.push_back(std::move(image));
        static_cast<void>(workspace::commit_render_images(workspace_,job->document_id,job->body_id,std::move(images)));job->retained_id=retained;refresh_tabs();
    }catch(const std::exception& e){state_->setText(tr(e.what()));}
}
void AssemblyWorkspaceWindow::export_render_image() {
    QImage pixels;std::string owner;
    if(const auto* page=static_cast<RenderPage*>(active_render_page_.data())) {
        if(!page->export_button->isEnabled())return;pixels=page->canvas->image;owner=page->document_id;
    }else if(render_job_&&render_job_->done.load()&&render_job_->accepted&&render_job_->document_id==workspace_.active_document_id()) {
        pixels=render_job_->latest;owner=render_job_->document_id;
    }
    if(pixels.isNull()){state_->setText(tr("Render the image before exporting it."));return;}
    const auto* part=workspace_.open_part(owner);if(!part)return;
    auto suggested=part->path.empty()?working_directory_/"render.jpg":part->path;suggested.replace_extension(".jpg");
    const auto path=save_file(this,tr("Exportovat"),QString::fromStdString(document::path_to_utf8(suggested)),
        tr("JPEG (*.jpg *.jpeg);;PNG (*.png)"),"jpg",application_settings_.translations);
    if(path.isEmpty())return;
    try {
        const auto extension=QFileInfo(path).suffix().toLower();
        if(extension!="jpg"&&extension!="jpeg"&&extension!="png")throw std::invalid_argument("Choose JPEG or PNG for the rendered image.");
        run_background_task([pixels,target=std::filesystem::u8path(path.toStdString())]{return drawing_render::write_image(pixels,target,true);});
        state_->setText(tr("Export uložen: %1").arg(path));
    }catch(const std::exception& e){QMessageBox::warning(this,tr("Export obrázku selhal"),tr(e.what()));}
}
} // namespace zima::app
