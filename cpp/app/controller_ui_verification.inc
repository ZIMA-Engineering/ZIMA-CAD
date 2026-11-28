// Runs only through the explicit console verification entry point, in an
// isolated working directory. Synthetic input verifies application ownership;
// it is not evidence of physical device motion or native Linux operation.
Q_NEVER_INLINE static int verify_controller_ui(QApplication& application,AssemblyWorkspaceWindow& window,
    const std::filesystem::path& directory) {
    using namespace controller;
    try {
        const auto check=[](bool ok,const char* message){if(!ok)throw std::runtime_error(message);};
        const auto flush=[&]{application.processEvents();QCoreApplication::sendPostedEvents(nullptr,QEvent::DeferredDelete);application.processEvents();};
        const auto run=[&](const QString& command){auto result=window.execute_console_command(command);if(!result.ok)throw std::runtime_error(result.message);flush();return result;};
        auto settings=ApplicationSettings::load(QString::fromStdString(directory.string()));
        check(QFileInfo(settings.config_path).absolutePath()==QString::fromStdString(directory.string()),"Controller test requires isolated config.ini");
        window.showMaximized();window.raise();window.activateWindow();flush();
        run(QString::fromStdString(commands::Json{{"command","new"},{"arguments",{{"type","part"},{"name","controller-fixture"}}}}.dump()));
        auto* view=dynamic_cast<viewer::MeshView*>(window.findChild<QWidget*>("modelWorkspace"));
        auto* service=window.controller_input_for_test();auto* action=window.findChild<QAction*>("controllerSettingsAction");
        check(view&&service&&action&&!action->icon().isNull(),"Controller integration is missing");
        QFile inventory(QString::fromStdString((directory/"controller-devices.json").string()));
        QJsonArray devices;for(const auto& device:service->devices()) {
            QJsonArray buttons;for(int button:device.buttons)buttons.append(button);
            devices.append(QJsonObject{{"name",device.name},{"transport",device.transport},{"connected",device.connected},{"buttons",buttons}});
        }
        check(inventory.open(QIODevice::WriteOnly),"Cannot record device descriptors");inventory.write(QJsonDocument(devices).toJson());inventory.close();
        const int live_seconds=qEnvironmentVariableIntValue("ZIMA_VERIFY_CONTROLLER_LIVE_SECONDS");
        if(live_seconds>0) {
            zima::test::gui_rectangular_profile(window,40,25,15);flush();view->setFocus();
            const auto before=run("documents").data;const auto calls=kernel::body_calculation_count.load();
            const auto initial=view->camera_state();QMap<QString,Axes> peaks;QMap<QString,QSet<int>> pressed;
            const auto navigate=service->navigate;const auto command=service->command;
            bool navigation_unchanged=true;int navigation_frames=0;QJsonArray commands;
            service->navigate=[&](const Motion& motion) {
                const auto state=window.execute_console_command("documents").data;
                const auto calculations=kernel::body_calculation_count.load();
                navigate(motion);++navigation_frames;
                navigation_unchanged&=window.execute_console_command("documents").data==state&&
                    kernel::body_calculation_count.load()==calculations;
            };
            service->command=[&](const QString& id){commands.append(id);command(id);};
            const auto restore_callbacks=qScopeGuard([&]{service->navigate=navigate;service->command=command;});
            QElapsedTimer timer;timer.start();int samples=0;
            std::cout<<"Physical controller observation ready for "<<std::min(live_seconds,300)<<" seconds\n"<<std::flush;
            while(timer.elapsed()<std::min(live_seconds,300)*1000) {
                flush();++samples;
                for(const auto& device:service->devices())if(device.connected) {
                    const auto state=service->state(device.id);
                    for(int i=0;i<6;++i)peaks[device.id][i]=std::max(peaks[device.id][i],std::abs(state.axes[i]));
                    pressed[device.id].unite(state.held);
                }
                QThread::msleep(8);
            }
            QJsonArray observations;
            for(const auto& device:service->devices()) {
                QJsonArray axes,buttons;for(double value:peaks.value(device.id))axes.append(value);
                auto ids=pressed.value(device.id).values();std::sort(ids.begin(),ids.end());for(int id:ids)buttons.append(id);
                observations.append(QJsonObject{{"name",device.name},{"connected",device.connected},{"axisPeaks",axes},{"pressedButtons",buttons}});
            }
            const bool unchanged=run("documents").data==before&&kernel::body_calculation_count.load()==calls;
            const QJsonObject record{{"seconds",timer.elapsed()/1000.},{"samples",samples},{"devices",observations},
                {"cameraChanged",view->camera_state()!=initial},{"documentUnchanged",unchanged},
                {"navigationFrames",navigation_frames},{"navigationUnchanged",navigation_unchanged},{"controllerCommands",commands}};
            QFile result(QString::fromStdString((directory/"controller-physical-input.json").string()));
            check(result.open(QIODevice::WriteOnly),"Cannot record physical observation");result.write(QJsonDocument(record).toJson());result.close();
            window.grab().save(QString::fromStdString((directory/"controller-physical-input.png").string()));
            // Interactive command mappings may deliberately Save, Undo or edit.
            // Navigation itself must remain strictly transient on every frame.
            check(navigation_unchanged,"Physical controller navigation changed the model");
            std::cout<<QJsonDocument(record).toJson(QJsonDocument::Compact).constData()<<'\n';return 0;
        }
        action->trigger();flush();
        for(auto* candidate:window.findChildren<QDialog*>())if(candidate->isVisible())
            if(auto* native=dynamic_cast<SettingsDialog*>(candidate)) {
                check(native->grab().save(QString::fromStdString((directory/"controller-native-dialog.png").string())),"Cannot capture native controller dialog");
                native->buttons()->button(QDialogButtonBox::Cancel)->click();break;
            }
        flush();
        for(auto* bar:window.findChildren<QToolBar*>())if(auto* button=bar->widgetForAction(action)) {
            check(button->grab().save(QString::fromStdString((directory/"controller-native-command.png").string())),"Cannot capture native controller toolbar control");break;
        }
        const Device fake{"test:controller","Controller test","USB HID",{1,2,21},true};
        const auto install=[&]{service->refresh_devices({fake});};install();
        Preferences preferences;preferences.selected_device=fake.id;
        Profile model;model.dead_zone=0;model.buttons={{1,"fitViewAction"},{2,"standardTopAction"}};
        preferences.set_profile(fake.id,Context::Model,model);settings.controllers=preferences;
        QString error;check(settings.save(&error),"Cannot save isolated test preferences");service->configure(preferences);
        const auto find_dialog=[&]() -> SettingsDialog* {
            for(auto* candidate:window.findChildren<QDialog*>())if(candidate->isVisible())
                if(auto* dialog=dynamic_cast<SettingsDialog*>(candidate))return dialog;
            return nullptr;
        };
        const auto open=[&]{install();action->trigger();flush();auto* dialog=find_dialog();check(dialog,"Controller action did not open its dialog");return dialog;};
        auto* dialog=open();
        check(dialog->windowType()==Qt::SubWindow&&dialog->parentWidget()==&window,"Controller settings escaped the internal properties contract");
        check(dialog->windowTitle()==settings.text("controller.title","3D controllers"),"Controller title is not localized");
        auto* buttons=dialog->findChild<QTableWidget*>("controllerButtons");check(buttons&&buttons->rowCount()==3,"Actual device buttons not offered");
        auto* pan=dialog->findChild<QDoubleSpinBox*>("controllerPanSpeed");pan->setValue(2.4);
        auto* context=dialog->findChild<QComboBox*>("controllerContext");context->setCurrentIndex(1);flush();
        auto* rotation=dialog->findChild<QCheckBox*>("controllerRotation");check(!rotation->isChecked(),"Sketch rotation enabled by default");
        pan->setValue(.65);context->setCurrentIndex(2);flush();check(!rotation->isEnabled()&&!rotation->isChecked(),"Drawing permits rotation");
        context->setCurrentIndex(0);flush();check(pan->value()==2.4,"Profile switch lost pending values");
        const auto before=run("documents").data;const auto calls=kernel::body_calculation_count.load();const auto camera=view->camera_state();
        service->receive_motion(fake.id,{1,1,1,1,1,1},0x3f);service->receive_buttons(fake.id,{1});service->advance(monotonic_ms()+16);
        check(view->camera_state()==camera,"Settings diagnostics moved the camera");
        window.grab().save(QString::fromStdString((directory/"controller-settings.png").string()));
        dialog->buttons()->button(QDialogButtonBox::Cancel)->click();flush();
        check(!find_dialog()&&ApplicationSettings::load(QString::fromStdString(directory.string())).controllers==preferences,"Cancel persisted controller changes");
        dialog=open();dialog->findChild<QDoubleSpinBox*>("controllerPanSpeed")->setValue(2.4);
        dialog->findChild<QCheckBox*>("controllerInvert0")->setChecked(true);
        dialog->findChild<QComboBox*>("controllerButton21")->setCurrentIndex(0);
        dialog->buttons()->button(QDialogButtonBox::Ok)->click();flush();
        auto saved=ApplicationSettings::load(QString::fromStdString(directory.string())).controllers;
        check(!find_dialog()&&saved.profile(fake.id,Context::Model).pan_speed==2.4&&saved.profile(fake.id,Context::Model).inverted[0],"OK did not persist controller preferences");
        check(service->preferences()==saved,"Saved preferences not applied to input service");
        dialog=open();check(dialog->findChild<QDoubleSpinBox*>("controllerPanSpeed")->value()==2.4,"Reopened settings lost sensitivity");
        dialog->buttons()->button(QDialogButtonBox::Cancel)->click();flush();
        install();view->setFocus();window.activateWindow();flush();
        for(int i=0;i<10&&QApplication::applicationState()!=Qt::ApplicationActive;++i){QThread::msleep(20);flush();}
        check(!service->target().identity.isEmpty()&&service->target().commands,"Controller view is not active");
        service->configure(preferences);service->advance(monotonic_ms());
        const auto step=[&](Axes axes){install();service->receive_motion(fake.id,axes,0x3f);QThread::msleep(17);service->advance(monotonic_ms());};
        const auto initial=view->camera_state();step({.4,.3,.2,.1,.2,.3});
        check(view->camera_state()!=initial,"Controller input failed to move active camera");
        service->receive_motion(fake.id,{},0x3f);service->receive_buttons(fake.id,{});
        int triggers=0;auto* fit=window.findChild<QAction*>("fitViewAction");
        const auto connection=QObject::connect(fit,&QAction::triggered,&window,[&]{++triggers;});
        service->receive_buttons(fake.id,{1});service->receive_buttons(fake.id,{1});check(triggers==1,"Held device button repeated an action");
        fit->setEnabled(false);service->receive_buttons(fake.id,{});service->receive_buttons(fake.id,{1});check(triggers==1,"Device invoked disabled command");fit->setEnabled(true);
        window.findChild<QAction*>("showCommandConsoleAction")->trigger();flush();
        auto* input=window.findChild<QLineEdit*>("commandConsoleInput");check(input,"Console input missing");input->setFocus();flush();
        check(!service->target().commands,"Text entry did not block controller commands");
        service->receive_buttons(fake.id,{});service->receive_buttons(fake.id,{1});check(triggers==1,"Controller stole focused text entry");
        window.findChild<QDockWidget*>("commandConsoleDock")->hide();view->setFocus();flush();
        QObject::disconnect(connection);service->receive_buttons(fake.id,{});
        check(run("documents").data==before&&kernel::body_calculation_count.load()==calls,"Controller navigation changed document/Undo or calculated geometry");
        verify_cursor_orbit_view(application,window);
        check(run("documents").data==before&&kernel::body_calculation_count.load()==calls,"Shared mouse navigation changed document or calculated geometry");
        // The base dialog owns middle-button double-click confirmation, including
        // delivery over the document view rather than over its OK button.
        dialog=open();dialog->findChild<QDoubleSpinBox*>("controllerZoomSpeed")->setValue(1.7);
        const QPointF at(view->rect().center()),global(view->mapToGlobal(at.toPoint()));
        QMouseEvent confirm(QEvent::MouseButtonDblClick,at,global,Qt::MiddleButton,Qt::MiddleButton,Qt::NoModifier);
        QApplication::sendEvent(view,&confirm);flush();
        check(!find_dialog()&&ApplicationSettings::load(QString::fromStdString(directory.string())).controllers.profile(fake.id,Context::Model).zoom_speed==1.7,
            "Controller dialog lost shared middle-button confirmation");
        run("new drawing controller-sheet");
        auto* drawing=dynamic_cast<DrawingWindow*>(window.findChild<QWidget*>("drawingWorkspace"));
        auto* canvas=window.findChild<QWidget*>("drawingCanvas");check(drawing&&canvas,"Drawing navigation target missing");
        canvas->setFocus();flush();install();service->advance(monotonic_ms());
        check(service->target().context==Context::Drawing&&!service->target().identity.isEmpty(),"Drawing profile did not become active");
        const auto drawing_revision=run("documents").data;const auto drawing_calls=kernel::body_calculation_count.load();
        drawing->fit_sheet();const auto sheet=drawing->sheet_rectangle_for_test();
        drawing->navigate_view({.1,.2},0);const auto panned=drawing->sheet_rectangle_for_test();
        check(std::abs(panned.x()-sheet.x()-.1*canvas->height())<1e-6&&std::abs(panned.y()-sheet.y()+.2*canvas->height())<1e-6&&panned.size()==sheet.size(),
            "Drawing pan has wrong displacement or changes scale");
        drawing->navigate_view({},std::log(1.15));const auto zoomed=drawing->sheet_rectangle_for_test();
        check(std::abs(zoomed.width()/panned.width()-1.15)<1e-9,"Drawing zoom has wrong scale");
        step({0,0,0,1,1,1});check(drawing->sheet_rectangle_for_test()==zoomed,"Drawing rotated from controller input");
        step({.3,.2,.1,0,0,0});check(drawing->sheet_rectangle_for_test()!=zoomed,"Controller failed to navigate Drawing");
        service->receive_motion(fake.id,{},0x3f);
        check(run("documents").data==drawing_revision&&kernel::body_calculation_count.load()==drawing_calls,"Drawing navigation modified a document or calculated geometry");
        std::cout<<"Controller GUI: internal settings, localized title, buttons, profiles, Cancel/OK/reopen, diagnostics, camera, press edges, disabled commands, mouse orbit, middle confirmation and unchanged document passed\n";
        return 0;
    }catch(const std::exception& error){std::cerr<<error.what()<<'\n';return 1;}
}
