static int verify_sheet_calibration_ui(QApplication& application,AssemblyWorkspaceWindow& window) {
    try {
        const auto check=[](bool ok,const char* message){if(!ok)throw std::runtime_error(message);};
        const auto flush=[&]{application.processEvents();QCoreApplication::sendPostedEvents(nullptr,QEvent::DeferredDelete);application.processEvents();};
        window.showMaximized();flush();
        for(const auto* language:{"cs","en","de","fr","ru"}) {
            QTemporaryDir directory;QSettings ini(directory.filePath("config.ini"),QSettings::IniFormat);ini.setValue("Application/Language",language);ini.sync();
            const auto settings=ApplicationSettings::load(directory.path());apply_application_translations(application,settings);
            for(const auto* unit:{"mm","in"})for(bool accept:{false,true}) {
                DocumentToolData data;data.sheet_metal=document::SheetMetalDefaults{};data.sheet_metal->thickness_mm=2.;data.sheet_metal->k_factor=.4;data.units["Length"]=unit;
                bool committed=false;DocumentToolData saved;
                auto* dialog=new FileSettingsDialog(data,[&](auto result){committed=true;saved=std::move(result);},settings,&window);
                dialog->show_sheet_metal_page();dialog->show();flush();
                auto* correction=dynamic_cast<ui::UnitDoubleSpinBox*>(dialog->findChild<QDoubleSpinBox*>("kFactorCorrection"));
                auto* calculate=dialog->findChild<QPushButton*>("kFactorCorrectionConfirm");auto* factor=dialog->findChild<QDoubleSpinBox*>("sheetMetalKFactor");
                check(correction&&calculate&&factor,"Sheet calibration controls missing");
                check(correction->toolTip()==settings.qt_translations.value("Change the developed length of one test bend. Positive extends it; negative shortens it."),"Correction tooltip translation missing");
                const double scale=std::string_view(unit)=="in"?25.4:1.;check(correction->native_per_unit()==scale,"Correction uses wrong document length units");
                auto* line=correction->findChild<QLineEdit*>();line->setText(correction->locale().toString(.5/scale,'g',16));correction->interpretText();calculate->click();flush();
                const double expected=.4+.5/std::numbers::pi;
                check(std::abs(factor->value()-expected)<1e-13&&!committed,"Correction lost precision or committed the dialog");
                check(correction->value()==0,"Applied correction was not reset");calculate->click();check(std::abs(factor->value()-expected)<1e-13,"Repeated zero correction changed K");
                correction->setValue(1);dialog->findChild<QDoubleSpinBox*>("sheetMetalThickness")->setValue(.01);calculate->click();flush();
                check(std::abs(factor->value()-expected)<1e-13&&dialog->findChild<QLabel*>("kFactorCorrectionError")->isVisible(),"Out-of-range K was silently clamped");
                dialog->findChild<QDoubleSpinBox*>("sheetMetalThickness")->setValue(2);
                if(accept)dialog->findChild<QDialogButtonBox*>()->button(QDialogButtonBox::Ok)->click();else dialog->reject();flush();
                check(committed==accept,"Calibration settings OK/Cancel failed");if(accept)check(std::abs(saved.sheet_metal->k_factor-expected)<1e-13,"Settings commit rounded calibrated K");
            }
        }
        for(bool line_bend:{false,true}) {
            auto sketch=sketcher::Sketch::create_default();static_cast<void>(sketch.add_segment(0,0,30,0));
            bool committed=false;auto* dialog=new SketchPropertiesDialog(sketch,{},true,{},[&](auto,auto,bool){committed=true;},&window);
            document::SheetMetalDefaults defaults;defaults.thickness_mm=2.;defaults.k_factor=.4;std::set<std::string> locks;
            document::BendParameters bend;bend.angle_degrees=60.;bend.k_factor=.4;
            document::LineBendParameters linear;linear.angle_degrees=60.;linear.k_factor=.4;
            double changed=.4;
            if(line_bend)dialog->set_line_bend_mode(linear,defaults,locks,[&](auto value){changed=value.k_factor;},[]{});
            else dialog->set_bend_mode(bend,defaults,locks,[&](auto value){changed=value.k_factor;},[]{},[](std::size_t){});
            dialog->show();flush();
            auto* correction=dialog->findChild<QDoubleSpinBox*>("kFactorCorrection");auto* calculate=dialog->findChild<QPushButton*>("kFactorCorrectionConfirm");
            check(correction&&!correction->isEnabled(),"Inherited K offers a local correction");
            dialog->findChild<QCheckBox*>(line_bend?"lineBendKFactorOverride":"bendKFactorOverride")->click();flush();check(correction->isEnabled(),"Local K correction stayed disabled");
            correction->setValue(.2);calculate->click();flush();check(std::abs(changed-(.4+.2/(2.*std::numbers::pi/3.)))<1e-13&&!committed,"Bend calibration ignored its own angle");
            if(line_bend) {
                auto* action=dialog->findChild<QAction*>("valueLock:k_factor");check(action,"Line Bend K lock missing");action->setChecked(true);
                const double before=changed;correction->setValue(-.1);calculate->click();check(changed==before,"Correction bypassed a locked K factor");
            }
            dialog->reject();flush();check(!committed,"Bend calibration Cancel committed geometry");
        }
        std::cout<<"K calibration GUI: five languages, mm/in, exact values, validation, override, lock and Cancel passed\n";return 0;
    }catch(const std::exception& error){std::cerr<<"K calibration GUI: "<<error.what()<<'\n';return 1;}
}
