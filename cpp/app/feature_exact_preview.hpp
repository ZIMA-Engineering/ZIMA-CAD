#pragma once
#include <zima/kernel/occt_kernel.hpp>
#include <QObject>
#include <QThreadPool>
#include <QTimer>
#include <QVariant>
#include <functional>
#include <mutex>
#include <optional>

namespace zima::app {
// One command-local calculation at a time. Workers own immutable snapshots and
// their own kernel; QObject/UI access stays on the owning GUI thread.
class FeatureExactPreview final : public QObject {
public:
    struct Job {
        kernel::HistoryOperation operation;
        kernel::BodySnapshot references,input;
        kernel::KernelRigidPlacement frame;
        std::vector<kernel::OrderedUpToPatch> external_sources;
        std::string source_generation;
    };
    using Wire=std::vector<kernel::ViewerEdge>;
    FeatureExactPreview(QObject* parent,std::function<void(Wire)> publish,
            std::function<void(QString)> status,std::function<std::string()> generation)
        :QObject(parent),publish_(std::move(publish)),status_(std::move(status)),generation_(std::move(generation)) {
        debounce_.setSingleShot(true);debounce_.setInterval(150);poll_.setInterval(20);
        connect(&debounce_,&QTimer::timeout,this,[this]{settled_=true;start();});
        connect(&poll_,&QTimer::timeout,this,[this]{finish();});
    }
    void request(Job job,std::optional<Wire> existing={},bool calculate=true) {
        auto keyed=job.operation;keyed.body.id="preview-frame";keyed.body.translation=job.frame.translation;keyed.body.rotation_degrees=job.frame.rotation_degrees;
        std::vector<kernel::HistoryOperation> keys{std::move(keyed)};
        if(!job.external_sources.empty()) {
            kernel::ExtrusionRequest sources;sources.ordered_end_limit.emplace();sources.ordered_end_limit->patches=job.external_sources;
            keys.push_back({"preview-sources",std::move(sources)});
        }
        const auto key=kernel::history_fingerprint(keys,keys.size())+":"+job.references->source_fingerprint+":"+
            job.input->source_fingerprint+":"+job.source_generation;
        if(key==wanted_)return;
        wanted_=key;pending_=std::move(job);settled_=false;
        if(existing){cached_key_=key;cached_=std::move(*existing);}
        if(cached_key_==key) {
            debounce_.stop();publish_(cached_);status_(tr("Exact preview"));set_state("ready");return;
        }
        if(!calculate) {
            debounce_.stop();publish_({});status_(tr("Stored preview unavailable. Change an input to calculate it."));set_state("unavailable");return;
        }
        publish_({});status_(tr("Calculating exact preview…"));set_state("pending");debounce_.start();
    }
    void clear() {
        wanted_.clear();pending_.reset();debounce_.stop();settled_=false;set_state("idle");
    }
private:
    struct Outcome {std::string key,generation,error;Wire wire;};
    struct Shared {std::mutex mutex;std::optional<Outcome> done;kernel::OcctKernel kernel;};
    void set_state(const char* state){if(parent())parent()->setProperty("featureExactPreviewState",state);}
    void start() {
        if(busy_||!settled_||!pending_||wanted_.empty()||cached_key_==wanted_)return;
        busy_=true;const auto job=*pending_;const auto key=wanted_;const auto state=shared_;
        if(parent())parent()->setProperty("featureExactPreviewCalculations",parent()->property("featureExactPreviewCalculations").toInt()+1);
        poll_.start();
        QThreadPool::globalInstance()->start([state,job,key] {
            Outcome result;result.key=key;result.generation=job.source_generation;
            try {result.wire=state->kernel.calculate_feature_preview(job.operation,job.references.get(),job.frame,job.input.get(),job.external_sources);}
            catch(const std::exception& error){result.error=error.what();}
            std::lock_guard guard(state->mutex);state->done=std::move(result);
        });
    }
    void finish() {
        std::optional<Outcome> result;
        {std::lock_guard guard(shared_->mutex);if(shared_->done){result=std::move(shared_->done);shared_->done.reset();}}
        if(!result)return;
        busy_=false;poll_.stop();
        if(result->key==wanted_) {
            // A failed current request is terminal. Only a different input may
            // schedule another attempt; polling must never create a retry loop.
            settled_=false;
            if(result->generation!=generation_()) {
                publish_({});status_(tr("Preview source changed. Update the input."));set_state("failed");
            } else if(!result->error.empty()) {
                publish_({});status_(tr("Preview unavailable: %1").arg(tr(result->error.c_str())));set_state("failed");
            } else {
                cached_key_=result->key;cached_=std::move(result->wire);publish_(cached_);status_(tr("Exact preview"));set_state("ready");
            }
        }
        start();
    }
    std::function<void(Wire)> publish_;std::function<void(QString)> status_;
    std::function<std::string()> generation_;
    QTimer debounce_,poll_;std::shared_ptr<Shared> shared_=std::make_shared<Shared>();
    std::optional<Job> pending_;std::string wanted_,cached_key_;Wire cached_;
    bool busy_{},settled_{};
};
} // namespace zima::app
