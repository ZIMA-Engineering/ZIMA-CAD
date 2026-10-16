static void verify_file_manager_materials(QApplication& application,zima::app::FileManagerTab& manager,const QString& directory) {
    const auto check=[](bool value,const char* message){if(!value)throw std::runtime_error(message);};
    const QString folder=QDir(directory).filePath("materials");check(QDir().mkpath(folder),"Cannot isolate material fixtures");
    const QString path=QDir(folder).filePath("S235JR.matz");
    check(QFile::copy(QDir::current().filePath("config/materials/01_steels/structural/S235JR.matz"),path),"Cannot copy material fixture");
    QDir().mkpath(QDir(folder).filePath("0000-index"));{QSettings metadata(QDir(folder).filePath("0000-index/metadata.ini"),QSettings::IniFormat);metadata.setValue("Directory/Parameters","MATERIAL_NAME,MASS_DENSITY");metadata.sync();}
    const auto native_path=std::filesystem::u8path(path.toStdString());
    const auto original=zima::document::load_material_library(native_path);
    int density_column=-1;
    for(const auto* language:{"cs","en","de","fr","ru"}) {
        zima::app::ApplicationSettings settings;settings.language=language;settings.file_manager.default_view="files";
        manager.reload_settings(settings);manager.navigate(folder);application.processEvents();
        const auto data=manager.native_parameters_requested(path);
        check(data.error.isEmpty()&&data.values.value("MASS_DENSITY")=="7.850000E-06"&&data.units.value("MASS_DENSITY")=="kg/mm^3","Material values or units were not read natively");
        auto* table=manager.findChild<QTreeWidget*>("fileManagerFiles");density_column=-1;
        for(int column=5;column<table->columnCount();++column)if(table->headerItem()->data(column,Qt::UserRole)=="MASS_DENSITY")density_column=column;
        check(density_column>=5&&table->headerItem()->text(density_column)==data.labels.value("MASS_DENSITY").value(language)+" [kg/mm^3]","Material header language or units changed");
        check(table->topLevelItemCount()==1&&table->topLevelItem(0)->text(density_column)==data.values.value("MASS_DENSITY"),"Material cell differs from its native file");
    }
    QSettings mirror(QDir(folder).filePath("0000-index/metadata.ini"),QSettings::IniFormat);
    check(mirror.value("Directory/Parameters").toString().contains("MASS_DENSITY")&&mirror.value("Parts/S235JR/MASS_DENSITY")=="7.850000E-06"&&mirror.value("Parts/S235JR/MASS_DENSITY/Unit")=="kg/mm^3","Material mirror is incomplete");
    const auto unchanged=QFileInfo(path).lastModified();
    check(manager.native_parameter_edit_requested(path,"MASS_DENSITY","7.850000E-06").isEmpty()&&QFileInfo(path).lastModified()==unchanged,"Unchanged edit rewrote the material");
    auto* table=manager.findChild<QTreeWidget*>("fileManagerFiles");
    const auto cell=table->indexFromItem(table->topLevelItem(0),density_column);
    QStyleOptionViewItem option;
    auto* editor=table->itemDelegate()->createEditor(table,option,cell);
    auto* input=qobject_cast<QLineEdit*>(editor);check(input,"Material cell did not expose its editor");
    input->setText("8.000000E-06");table->itemDelegate()->setModelData(editor,table->model(),cell);delete editor;application.processEvents();
    const auto edited=zima::document::load_material_library(native_path);
    check(edited.properties.at("MASS_DENSITY")=="8.000000E-06"&&edited.units==original.units&&edited.descriptions==original.descriptions,"Material edit lost values, units or language descriptions");
    check(!manager.native_parameter_edit_requested(path,"MASS_DENSITY","-1").isEmpty()&&zima::document::load_material_library(native_path)==edited,"Invalid edit changed the saved material");
    mirror.sync();check(mirror.value("Parts/S235JR/MASS_DENSITY")=="8.000000E-06","Edited material did not refresh its mirror");
    check(static_cast<bool>(manager.open_file_requested),"Material opening callback is missing");manager.open_file_requested(path);application.processEvents();auto* library=manager.window()->findChild<QDialog*>("materialPropertiesDialog");check(library&&library->isVisible()&&!library->findChild<QTreeView*>("materialLibraryTree")&&library->findChild<QLabel*>("materialLibraryPath")->text()==QDir::toNativeSeparators(path),"Opening a material did not open its individual properties");library->reject();QCoreApplication::sendPostedEvents(nullptr,QEvent::DeferredDelete);application.processEvents();
    auto* action=manager.window()->findChild<QAction*>("materialManagerAction");check(action&&action->isEnabled(),"Tools does not expose the material manager");action->trigger();application.processEvents();library=manager.window()->findChild<QDialog*>("materialLibraryDialog");check(library&&library->isVisible(),"Tools did not open the material manager");library->reject();QCoreApplication::sendPostedEvents(nullptr,QEvent::DeferredDelete);application.processEvents();
    zima::app::ApplicationSettings settings;settings.language="cs";settings.file_manager.default_view="files";manager.reload_settings(settings);manager.navigate(directory);
}
