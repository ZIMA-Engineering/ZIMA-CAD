int verify_hidden_step_tabs(QApplication& application) {
    using namespace zima;
    const auto check=[](bool ok,const char* message){if(!ok)throw std::runtime_error(message);};
    const auto flush=[&]{application.processEvents();QCoreApplication::sendPostedEvents(nullptr,QEvent::DeferredDelete);application.processEvents();};
    try {
        QTemporaryDir directory;check(directory.isValid(),"STEP temporary directory missing");
        const auto output=std::filesystem::path(directory.path().toStdWString());
        kernel::OcctKernel kernel;
        kernel::StepProduct part;part.definition_id="hidden-part";part.name="Hidden Part";
        part.body=zima::test::rectangular_body(kernel,4,5,6);
        kernel::StepProduct group;group.definition_id="hidden-group";group.name="Hidden Group";group.children={part};
        const auto step=output/"hidden-tabs.step";kernel.export_step(group,document::path_to_utf8(step));
        app::AssemblyWorkspaceWindow window(directory.path(),directory.path());window.resize(1366,768);window.show();flush();
        check(window.execute_console_command("new assembly step-tab-owner").ok,"Cannot create STEP owner");
        const auto result=window.execute_console_command(QString::fromStdString(commands::Json{{"command","import.step"},{"arguments",{{"path",document::path_to_utf8(step)},{"output_directory",document::path_to_utf8(output/"sources")}}}}.dump()));
        check(result.ok,"STEP Assembly import failed");flush();
        auto* tabs=window.findChild<QTabBar*>("documentTabs");check(tabs&&tabs->count()==1,"STEP exposed hidden source tabs");
        check(window.execute_console_command("save").ok,"STEP Assembly save failed");flush();
        bool assembly_thumbnail=false;
        for(const auto& file:std::filesystem::recursive_directory_iterator(output))if(QString::fromStdString(document::path_to_utf8(file.path().filename())).endsWith(".asmz.png",Qt::CaseInsensitive)){
            const QImage image(QString::fromStdString(document::path_to_utf8(file.path())));assembly_thumbnail=!image.isNull()&&image.width()<=320&&image.height()<=240;
        }
        check(assembly_thumbnail,"Assembly Save did not capture a bounded current-view PNG");
        std::size_t parts=0,assemblies=0;
        for(const auto& file:std::filesystem::recursive_directory_iterator(output/"sources")) {
            if(file.path().extension()==".prtz") {const auto loaded=document::PartDocument::load(file.path());check(!loaded.history.empty(),"Saved STEP source is empty");++parts;}
            if(file.path().extension()==".asmz")++assemblies;
        }
        check(parts==1&&assemblies>=1,"STEP Assembly did not save native sources");
        tabs->tabCloseRequested(tabs->currentIndex());flush();
        check(tabs->count()==0,"Closing STEP owner revealed hidden source tabs");
        std::cout<<"STEP import, native source save and last Assembly tab close passed\n";return 0;
    } catch(const std::exception& error){std::cerr<<error.what()<<'\n';return 1;}
}
