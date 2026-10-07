#include <zima/ui/operation_activity.hpp>
#include <zima/ui/properties_subwindow.hpp>
#include <zima/workspace/model_calculation.hpp>
#include <QApplication>
#include <QDialogButtonBox>
#include <QDoubleSpinBox>
#include <QEventLoop>
#include <QKeyEvent>
#include <QLabel>
#include <QLineEdit>
#include <QImage>
#include <QPixmap>
#include <QPushButton>
#include <QTimer>
#include <QVBoxLayout>
#include <chrono>
#include <cmath>
#include <future>
#include <iostream>
#include <thread>

using namespace zima;
namespace {
void check(bool value, const char* message) { if (!value) throw std::runtime_error(message); }
void wait(int milliseconds) { QEventLoop loop; QTimer::singleShot(milliseconds, &loop, &QEventLoop::quit); loop.exec(); }
class TestProperties final : public ui::PropertiesSubWindow {
public:
    explicit TestProperties(QWidget* parent) : PropertiesSubWindow("Activity test", parent) {
        set_initial_size({620,600});
        content_layout()->addWidget(new QLabel("Private calculation candidate",this));
        numeric=new QDoubleSpinBox(this);numeric->setValue(12.5);content_layout()->addWidget(numeric);
    }
    std::function<bool()> work;
    QDoubleSpinBox* numeric{};
private:
    bool submit() override { return work(); }
};
}
int main(int argc, char** argv) {
    QApplication app(argc,argv);
    try {
        QWidget window; window.resize(1200,900);
        QWidget view(&window); view.setGeometry(280,0,920,840);
        view.setStyleSheet("background:#151e28;");
        QLineEdit input(&window); input.setText("unchanged");
        ui::OperationActivity activity(&window,&view);
        window.show(); app.processEvents();
        {
            ui::OperationActivity::Scope fast(&activity,"Fast operation");
            app.processEvents(); check(!activity.overlay()->isVisible(),"Fast work flashes the overlay");
        }
        wait(280); check(!activity.active()&&!activity.overlay()->isVisible(),"Retired delayed timer showed activity");
        int starts{},finishes{};
        std::unique_ptr<workspace::CalculationExecutionScope> execution;
        const auto owner_thread=std::this_thread::get_id();
        std::thread::id worker_thread;
        activity.started=[&] {
            ++starts;
            execution=std::make_unique<workspace::CalculationExecutionScope>([&](auto task) {
                ui::OperationActivity::InputBlock input_block;
                auto future=std::async(std::launch::async,[&,task=std::move(task)] {
                    worker_thread=std::this_thread::get_id();
                    std::this_thread::sleep_for(std::chrono::milliseconds(700));
                    task();
                });
                while(future.wait_for(std::chrono::milliseconds(0))!=std::future_status::ready) {
                    app.processEvents(); std::this_thread::sleep_for(std::chrono::milliseconds(2));
                }
                future.get();
            });
        };
        activity.finished=[&] {++finishes; execution.reset();};
        auto original=document::PartDocument::create_default();
        auto sketch=sketcher::Sketch::create_default();static_cast<void>(sketch.add_circle(0,0,2));
        auto feature=document::PartDocument::create_extrusion_container(sketch.id);
        feature.extrusion.height=10; sketch.owner_container_id=feature.id;
        original.insert_history_entry(document::PartHistoryKind::Feature,feature.id);
        original.history.push_back(feature);original.sketches.push_back(sketch);original.resolve_constructions();
        kernel::OcctKernel kernel;
        auto synchronous=original;
        const auto baseline=workspace::calculate_part_with_resolved_references(kernel,synchronous);
        check(!baseline.empty()&&baseline.back().calculation_errors.empty(),"Activity fixture calculation failed");
        const auto source=original.serialized();
        TestProperties properties(&window);properties.show();app.processEvents();
        auto candidate=original;std::vector<kernel::BodyResult> result;
        int angle_before=-1,angle_after=-1;bool centered{},footer{},blocked{},foreground{};
        QTimer::singleShot(330,&window,[&] {
            const auto* overlay=activity.overlay();
            angle_before=overlay->property("activityAngle").toInt();
            centered=overlay->isVisible()&&
                (overlay->geometry().center()-QRect(view.mapTo(&window,QPoint{}),view.size()).center()).manhattanLength()<=2;
            auto* indicator=properties.findChild<QWidget*>("operationConfirmationActivity");
            footer=indicator&&indicator->isVisible()&&!properties.buttons()->isEnabled();
            QKeyEvent key(QEvent::KeyPress,Qt::Key_A,Qt::NoModifier,"a");app.sendEvent(&input,&key);
            const auto numeric_text=properties.numeric->text();
            QKeyEvent decimal(QEvent::KeyPress,Qt::Key_Comma,Qt::NoModifier,",");
            app.sendEvent(properties.numeric->findChild<QLineEdit*>(),&decimal);
            properties.buttons()->button(QDialogButtonBox::Ok)->click();
            window.close();blocked=input.text()=="unchanged"&&window.isVisible()&&properties.numeric->text()==numeric_text;
            const auto frame=window.grab();
            const auto probe=QPoint(overlay->x()+9,overlay->geometry().center().y());
            foreground=frame.toImage().pixelColor(QPoint(qRound(probe.x()*frame.devicePixelRatio()),
                qRound(probe.y()*frame.devicePixelRatio())))==QColor("#243448");
            frame.save("build/form-diagnostic/operation-activity-ui.png");
        });
        QTimer::singleShot(510,&window,[&] { angle_after=activity.overlay()->property("activityAngle").toInt(); });
        properties.work=[&] {
            result=workspace::calculate_part_with_resolved_references(kernel,candidate);
            return true;
        };
        properties.buttons()->button(QDialogButtonBox::Ok)->click();
        check(centered&&foreground,"Activity panel is not visible over Properties at the View center");
        check(footer&&blocked,"Busy confirmation or conflicting-input protection failed");
        check(angle_before>=0&&angle_before!=angle_after,"Activity animation froze during a real kernel calculation");
        check(worker_thread!=owner_thread&&std::this_thread::get_id()==owner_thread,"Calculation or result ownership thread is incorrect");
        check(!activity.active()&&!activity.overlay()->isVisible()&&!properties.isVisible(),"Successful confirmation left activity visible");
        check(original.serialized()==source&&candidate.serialized()==synchronous.serialized(),"Worker changed its source or calculation behavior");
        check(result.back().source_fingerprint==baseline.back().source_fingerprint&&
              std::abs(result.back().volume-baseline.back().volume)<1e-9,"Worker result differs from synchronous geometry");
        check(starts==1&&finishes==1,"Calculation scope lifecycle was not paired");
        // Exception cleanup and nested phase retirement must be reliable.
        properties.show();properties.work=[&] {
            workspace::CalculationExecutionScope failure([](auto) {throw std::runtime_error("expected activity failure");});
            static_cast<void>(workspace::calculate_part(kernel,original));return true;
        };
        properties.buttons()->button(QDialogButtonBox::Ok)->click();
        check(!activity.active()&&!activity.overlay()->isVisible()&&properties.isVisible()&&properties.buttons()->isEnabled(),"Failed calculation stranded activity or confirmation");
        check(properties.findChild<QLabel*>("propertiesSubmitError")->isVisible(),"Calculation exception was not shown");
        properties.reject();
        const auto outer=activity.begin("Outer operation");wait(270);
        const auto inner=activity.begin("Inner phase");
        check(activity.overlay()->findChild<QLabel*>()->text()=="Inner phase","Nested phase text did not update");
        activity.end(inner);
        check(activity.active()&&activity.overlay()->findChild<QLabel*>()->text()=="Outer operation","Nested completion retired its parent");
        view.resize(700,700);app.processEvents();
        check((activity.overlay()->geometry().center()-QRect(view.mapTo(&window,QPoint{}),view.size()).center()).manhattanLength()<=2,"Activity did not follow View resize");
        static_cast<void>(activity.begin("Unfinished child"));activity.end(outer);
        check(!activity.active()&&!activity.overlay()->isVisible()&&starts==finishes,"Outer exit left a nested activity or executor active");
        std::cout<<"Operation activity: delayed center overlay, foreground, OK indicator, live worker animation, private geometry, input gate, exceptions and nested retirement passed\n";
        return 0;
    }catch(const std::exception& error){std::cerr<<error.what()<<'\n';return 1;}
}
