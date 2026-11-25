#pragma once
#include <zima/workspace/cam_calculation.hpp>
#include <zima/ui/properties_subwindow.hpp>
#include <QCloseEvent>
#include <QDialogButtonBox>
#include <QLabel>
#include <QProgressBar>
#include <QPushButton>
#include <QTimer>
#include <QVBoxLayout>
#include <atomic>
#include <future>

namespace zima::app {
// Private snapshots run on a worker; only the owning window publishes a complete
// result after its runtime/revision/input checks. Cancel never publishes a prefix.
class CamCalculationDialog final : public ui::PropertiesSubWindow {
public:
    CamCalculationDialog(workspace::CamCalculationInput input,
        std::function<void(document::CamCalculated)> publish,QWidget* parent)
        : PropertiesSubWindow(tr("Calculate CAM"),parent),publish_(std::move(publish)),progress_(std::make_shared<Progress>()) {
        setObjectName("camCalculationDialog");setAttribute(Qt::WA_DeleteOnClose);
        message_=new QLabel(tr("Calculating tool paths and remaining stock…"),this);message_->setWordWrap(true);content_layout()->addWidget(message_);
        bar_=new QProgressBar(this);bar_->setObjectName("camCalculationProgress");bar_->setRange(0,0);content_layout()->addWidget(bar_);
        buttons()->button(QDialogButtonBox::Ok)->setEnabled(false);set_initial_size({480,0});
        auto progress=progress_;
        worker_=std::async(std::launch::async,[input=std::move(input),progress]{return workspace::calculate_cam(input,[progress](std::size_t done,std::size_t total){
            if(progress->cancelled.load())throw std::runtime_error("CAM calculation cancelled.");progress->total.store(total);progress->done.store(done);
        });});
        poll_=new QTimer(this);poll_->setInterval(40);connect(poll_,&QTimer::timeout,this,[this]{poll();});poll_->start();
    }
    ~CamCalculationDialog()override{progress_->cancelled.store(true);if(worker_.valid())worker_.wait();}
    void reject()override{if(worker_.valid()){progress_->cancelled.store(true);message_->setText(tr("Cancelling CAM calculation…"));buttons()->button(QDialogButtonBox::Cancel)->setEnabled(false);}else QDialog::reject();}
protected:
    bool submit()override{return !worker_.valid();}
    void closeEvent(QCloseEvent* e)override{if(worker_.valid()){e->ignore();reject();}else PropertiesSubWindow::closeEvent(e);}
    void changeEvent(QEvent* e)override{PropertiesSubWindow::changeEvent(e);if(e->type()==QEvent::LanguageChange&&message_){set_internal_title(tr("Calculate CAM"));
        message_->setText(!error_.empty()?tr(error_.c_str()):progress_->cancelled.load()?tr("Cancelling CAM calculation…"):tr("Calculating tool paths and remaining stock…"));}}
private:
    struct Progress{std::atomic<bool> cancelled{};std::atomic<std::size_t> done{},total{};};
    std::function<void(document::CamCalculated)> publish_;std::shared_ptr<Progress> progress_;
    std::future<document::CamCalculated> worker_;QLabel* message_{};QProgressBar* bar_{};QTimer* poll_{};std::string error_;
    void poll(){
        const auto total=progress_->total.load(),done=progress_->done.load();if(total){bar_->setRange(0,100);bar_->setValue(int(100.*done/total));}
        if(worker_.wait_for(std::chrono::milliseconds(0))!=std::future_status::ready)return;poll_->stop();
        try{auto result=worker_.get();if(progress_->cancelled.load()){QDialog::reject();return;}publish_(std::move(result));QDialog::accept();}
        catch(const std::exception& e){if(progress_->cancelled.load()){QDialog::reject();return;}error_=e.what();message_->setText(tr(error_.c_str()));buttons()->button(QDialogButtonBox::Ok)->setEnabled(true);}
    }
};
} // namespace zima::app
