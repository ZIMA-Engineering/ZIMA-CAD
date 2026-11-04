#include <zima/ui/operation_activity.hpp>
static int verify_assembly_presentation_ui(QApplication& application,const std::filesystem::path& parent) {
    using namespace zima;using test::mechanism::require;
    try {
        QTemporaryDir isolated(QString::fromStdString((parent/"assembly-presentation-XXXXXX").string()));
        require(isolated.isValid(),"Cannot isolate Assembly presentation test");
        const auto directory=std::filesystem::path(isolated.path().toStdWString());
        auto part=document::PartDocument::create_default();part.name="Color source";
        auto box=test::rectangular_feature(part,{20,15,10});part.history={box};
        document::BodyHistoryGraph graph;const auto body=graph.create_body("Body");
        graph.insert({document::PartHistoryKind::Feature,box.id});graph.activate({});part.set_body_history(graph);
        part.appearance.bodies[body].color="#F02020";
        kernel::OcctKernel kernel;const auto calculated=kernel.evaluate_history(part.kernel_operations());
        const auto source=directory/"colored.prtz";part.save(source,calculated);
        auto nested=assembly::AssemblyDocument::create_default();nested.name="Closed owner";
        for(unsigned i=0;i<2;++i) {
            auto component=assembly::AssemblyDocument::create_part_occurrence("Part",part.document_id,source,calculated.back());
            component.placement.x=i*30;
            if(i){auto appearance=document::component_appearance(part);appearance.bodies[body].color="#2020F0";component.appearance_override=std::move(appearance);}
            nested.components.push_back(std::move(component));
        }
        const auto nested_path=directory/"nested.asmz";static_cast<void>(nested.save(nested_path));
        auto root=assembly::AssemblyDocument::create_default();root.name="Repeated nested colors";
        for(unsigned i=0;i<2;++i) {
            auto component=assembly::AssemblyDocument::create_assembly_occurrence("Nested",nested.document_id,nested_path,nested);
            component.placement.y=i*30;root.components.push_back(std::move(component));
        }
        const auto root_path=directory/"root.asmz";static_cast<void>(root.save(root_path));
        app::AssemblyWorkspaceWindow window(isolated.path());window.setAttribute(Qt::WA_DontShowOnScreen);window.resize(1300,900);window.show();
        const auto flush=[&]{application.processEvents();QCoreApplication::sendPostedEvents(nullptr,QEvent::DeferredDelete);application.processEvents();};
        const auto run=[&](const std::string& command,const commands::Json& arguments=commands::Json::object()) {
            const auto result=window.execute_console_command(QString::fromStdString(commands::Json{{"command",command},{"arguments",arguments}}.dump()));
            if(!result.ok)throw std::runtime_error(command+": "+result.message);flush();return result;
        };
        require(window.open_document_path(QString::fromStdWString(root_path.wstring())),"Nested colored Assembly cannot open");flush();
        auto* view=dynamic_cast<viewer::MeshView*>(window.findChild<QOpenGLWidget*>("modelWorkspace"));
        auto* tabs=window.findChild<QTabBar*>("documentTabs");
        require(view&&tabs&&tabs->count()==1,"Color display unexpectedly opened source tabs");
        const auto colors=[&] {
            view->set_display_mode(viewer::DisplayMode::Shaded);view->fit_all();flush();
            const auto image=view->grabFramebuffer();require(!image.isNull(),"Assembly framebuffer unavailable");
            std::array<unsigned,3> counts{};
            for(int y=0;y<image.height();++y)for(int x=0;x<image.width();++x) {
                const auto c=image.pixelColor(x,y);
                if(c.red()>60&&c.red()>c.green()*1.8&&c.red()>c.blue()*1.8)++counts[0];
                if(c.green()>60&&c.green()>c.red()*1.8&&c.green()>c.blue()*1.8)++counts[1];
                if(c.blue()>60&&c.blue()>c.red()*1.8&&c.blue()>c.green()*1.8)++counts[2];
            }
            return counts;
        };
        const auto initial=colors();require(initial[0]>500&&initial[2]>500,"Closed nested source Body color or occurrence override is absent from the real framebuffer");
        require(window.open_document_path(QString::fromStdWString(source.wstring())),"Color source Part cannot open");flush();
        require(activate_test_body(application,window,body),"Color source Body cannot activate");
        run("appearance.set",{{"style",{{"color","#20F020"}}}});
        int root_tab=-1;for(int i=0;i<tabs->count();++i)if(tabs->tabData(i).toString().toStdString()==root.document_id)root_tab=i;
        require(root_tab>=0,"Root Assembly tab disappeared");tabs->setCurrentIndex(root_tab);flush();
        const auto changed=colors();require(changed[1]>500&&changed[2]>500&&changed[0]<initial[0]/4,"Open unsaved Part color did not reach closed nested Assembly occurrences");
        int part_tab=-1;for(int i=0;i<tabs->count();++i)if(tabs->tabData(i).toString().toStdString()==part.document_id)part_tab=i;
        require(part_tab>=0,"Source Part tab disappeared");tabs->setCurrentIndex(part_tab);flush();
        auto* close=window.findChild<QAction*>("closeDocumentAction");auto* activity=ui::OperationActivity::find(view);
        require(close&&activity,"Close activity integration missing");
        struct Paints final:QObject {
            QWidget* overlay{};unsigned count{},visible{};
            bool eventFilter(QObject*,QEvent* e)override {if(e->type()==QEvent::Paint){++count;if(overlay->isVisible())++visible;}return false;}
        } paints;paints.overlay=activity->overlay();activity->overlay()->installEventFilter(&paints);
        const auto answer=[&](QMessageBox::StandardButton button){QTimer::singleShot(0,&window,[&,button]{
            if(auto* box=qobject_cast<QMessageBox*>(QApplication::activeModalWidget()))box->button(button)->click();
        });close->trigger();flush();};
        answer(QMessageBox::Cancel);require(tabs->count()==2&&!activity->active()&&!paints.visible,"Cancel closed a document or started closing activity");
        answer(QMessageBox::Discard);require(tabs->count()==1&&paints.visible>0&&!activity->active()&&!activity->overlay()->isVisible(),"Close did not paint and retire centered activity");
        require(!view->mesh().triangles.empty()&&document::PartDocument::load(source).appearance.bodies.at(body).color=="#F02020","Discard damaged the remaining scene or saved pending source colors");
        const auto before=paints.visible;close->trigger();flush();
        require(tabs->count()==0&&paints.visible>before&&!activity->active(),"Clean close did not publish and retire activity");
        std::cout<<"Assembly presentation GUI: closed/repeated nested Body colors and overrides, open unsaved source color, dirty Cancel/Discard, clean close, synchronous overlay paint and remaining scene passed\n";
        return 0;
    }catch(const std::exception& e){std::cerr<<"Assembly presentation GUI: "<<e.what()<<'\n';return 1;}
}
