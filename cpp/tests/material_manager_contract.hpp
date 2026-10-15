#pragma once
#include "document_tools_dialogs.hpp"
#include <QApplication>
#include <QElapsedTimer>
#include <QThread>
#include <QFileSystemModel>
#include <QSortFilterProxyModel>
#include <QComboBox>
#include <QDialogButtonBox>
#include <QFile>
#include <QFileDialog>
#include <QLineEdit>
#include <QListWidget>
#include <QMouseEvent>
#include <QPushButton>
#include <QTableWidget>
#include <QTemporaryDir>
#include <QTimer>
#include <QTreeView>
#include <stdexcept>
#include <iostream>
#include <zima/document/material_library.hpp>

inline void
verify_material_manager(QApplication &application, QWidget &parent,
                        const zima::app::ApplicationSettings &settings) {
  const auto check = [](bool valid, const char *error) {
    if (!valid)
      throw std::runtime_error(error);
  };
  const auto bytes = [&](const QString &path) {
    QFile file(path);
    check(file.open(QIODevice::ReadOnly), "Cannot read material fixture");
    return file.readAll();
  };
  QTemporaryDir fixture;
  check(fixture.isValid(), "Cannot create material manager fixture");
  check(QDir().mkpath(fixture.filePath("Materials")),"Cannot create material tree fixture");
  for(const auto* path:{"0000-index","Materials/0000-INDEX","Materials/Visible"})
    check(QDir().mkpath(fixture.filePath(path)),"Cannot create material metadata tree fixture");
  const QString source = fixture.filePath("Steel.matz");
  const auto root =
      std::filesystem::path(__FILE__).parent_path().parent_path().parent_path();
  check(QFile::copy(
            QString::fromStdString(
                (root / "config/materials/01_steels/structural/S235JR.matz")
                    .generic_string()),
            source),
        "Cannot copy material manager fixture");
  const auto original = bytes(source);
  const auto native = std::filesystem::u8path(source.toStdString());
  const auto original_data = zima::document::load_material_library(native);
  auto configuration = settings;
  configuration.resolved_paths["Materials"] = fixture.path();
  int saved = 0;
  bool writable = true;
  const auto policy = [&](const QString &, bool) { return writable; };
  const auto density = [&](zima::app::MaterialDialog &dialog) {
    auto *table = dialog.findChild<QTableWidget *>("materialTable");
    check(table, "Missing shared material table");
    for (int row = 0; row < table->rowCount(); ++row)
      if (table->item(row, 1) && table->item(row, 1)->text() == "MASS_DENSITY")
        return table->item(row, 2);
    throw std::runtime_error("Missing density row");
  };
  {
    zima::app::MaterialDialog dialog(
        fixture.path(), policy, [&] { ++saved; }, configuration, &parent);
    dialog.findChild<QListWidget*>("materialLibraryFiles")->setCurrentRow(0);
    dialog.show();
    application.processEvents();
    auto* directory_tree=dialog.findChild<QTreeView*>("materialLibraryTree");QElapsedTimer directory_timer;directory_timer.start();while(directory_tree->model()->rowCount(directory_tree->rootIndex())==0&&directory_timer.elapsed()<1000){application.processEvents();QThread::msleep(5);}check(directory_tree->model()->rowCount(directory_tree->rootIndex())>0,"Material directory tree did not load its subfolders");
    auto* proxy=qobject_cast<QSortFilterProxyModel*>(directory_tree->model());check(proxy,"Material directory filtering model is missing");auto* filesystem=qobject_cast<QFileSystemModel*>(proxy->sourceModel());check(filesystem,"Material source directory model is missing");
    const auto metadata_index=filesystem->index(fixture.filePath("0000-index"));check(metadata_index.isValid()&&!proxy->mapFromSource(metadata_index).isValid(),"Material manager exposes root metadata directory");
    const auto materials=proxy->mapFromSource(filesystem->index(fixture.filePath("Materials")));check(materials.isValid(),"Ordinary material folder was filtered out");directory_tree->expand(materials);directory_tree->setCurrentIndex(materials);application.processEvents();check(dialog.findChild<QLabel*>("materialLibraryPath")->text()==QDir::toNativeSeparators(fixture.filePath("Materials")),"Filtered directory selection resolved the wrong source path");
    const auto nested_metadata=filesystem->index(fixture.filePath("Materials/0000-INDEX"));check(nested_metadata.isValid()&&!proxy->mapFromSource(nested_metadata).isValid(),"Material manager exposes nested metadata directory with different case");check(proxy->mapFromSource(filesystem->index(fixture.filePath("Materials/Visible"))).isValid(),"Nested ordinary material folder was filtered out");
    check(QDir().mkpath(fixture.filePath("Materials/Visible/0000-index")),"Cannot create new metadata folder");const auto created_metadata=filesystem->index(fixture.filePath("Materials/Visible/0000-index"));check(created_metadata.isValid()&&!proxy->mapFromSource(created_metadata).isValid(),"New metadata folder was not filtered");
    directory_tree->setCurrentIndex(proxy->mapFromSource(filesystem->index(fixture.path())));application.processEvents();auto* material_files=dialog.findChild<QListWidget*>("materialLibraryFiles");check(material_files->count()==1,"Returning from filtered tree lost material files");material_files->setCurrentRow(0);application.processEvents();
    const QString captures=qEnvironmentVariable("ZIMA_MATERIAL_MANAGER_CAPTURE_DIRECTORY");
    if(!captures.isEmpty()){QDir().mkpath(captures);check(dialog.grab().save(QDir(captures).filePath(settings.language+".png")),"Cannot capture material manager");}
    check(dialog.windowFlags().testFlag(Qt::SubWindow),
          "Material manager is not an internal properties window");
    check(
        dialog.findChild<QTreeView *>("materialLibraryTree") &&
            dialog.findChild<QListWidget *>("materialLibraryFiles")->count() ==
                1,
        "Material manager navigation is incomplete");
    for (const auto &pair :
         {std::pair{"materialLibraryNewFolder", "New folder…"},
          std::pair{"materialLibraryNew", "New material"},
          std::pair{"materialLibrarySaveAs", "Save As…"}})
      check(dialog.findChild<QPushButton *>(pair.first)->text() ==
                settings.qt_translations.value(QString::fromUtf8(pair.second)),
            "Material manager action is untranslated");
    const QPointF point(10, 10);
    QMouseEvent short_click(QEvent::MouseButtonRelease, point,
                            parent.mapToGlobal(point.toPoint()),
                            Qt::MiddleButton, Qt::NoButton, Qt::NoModifier);
    QApplication::sendEvent(&parent, &short_click);
    check(dialog.isVisible(),
          "Short middle click confirmed the material manager");
    QMouseEvent confirm(QEvent::MouseButtonDblClick, point,
                        parent.mapToGlobal(point.toPoint()), Qt::MiddleButton,
                        Qt::MiddleButton, Qt::NoModifier);
    QApplication::sendEvent(&parent, &confirm);
    application.processEvents();
    check(!dialog.isVisible() && bytes(source) == original && saved == 0,
          "Unchanged material confirmation wrote the file or failed over the "
          "owner view");
  }
  {
    zima::app::MaterialDialog dialog(
        fixture.path(), policy, [&] { ++saved; }, configuration, &parent);
    dialog.findChild<QListWidget*>("materialLibraryFiles")->setCurrentRow(0);
    dialog.show();
    density(dialog)->setText("9.000000E-06");
    dialog.reject();
    check(bytes(source) == original && saved == 0,
          "Material Cancel saved pending edits");
  }
  {
    zima::app::MaterialDialog dialog(
        fixture.path(), policy, [&] { ++saved; }, configuration, &parent);
    dialog.findChild<QListWidget*>("materialLibraryFiles")->setCurrentRow(0);
    dialog.show();
    density(dialog)->setText("-1");
    dialog.buttons()->button(QDialogButtonBox::Ok)->click();
    check(dialog.isVisible() && bytes(source) == original && saved == 0,
          "Invalid material edit committed");
    density(dialog)->setText("8.000000E-06");
    writable = false;
    dialog.buttons()->button(QDialogButtonBox::Ok)->click();
    check(dialog.isVisible() && bytes(source) == original && saved == 0,
          "Read-only material was changed");
    writable = true;
    dialog.buttons()->button(QDialogButtonBox::Ok)->click();
    check(!dialog.isVisible() && saved == 1,
          "Valid material edit did not save");
    const auto edited = zima::document::load_material_library(native);
    auto expected = original_data;
    expected.properties["MASS_DENSITY"] = "8.000000E-06";
    check(edited == expected,
          "Material manager changed unrelated material data");
  }
  {
    zima::app::MaterialDialog dialog(
        fixture.path(), policy, [&] { ++saved; }, configuration, &parent);
    dialog.findChild<QListWidget*>("materialLibraryFiles")->setCurrentRow(0);
    dialog.show();
    const auto before = bytes(source);
    density(dialog)->setText("9.000000E-06");
    {
      QFile external(source);
      check(external.open(QIODevice::Append),
            "Cannot simulate another material editor");
      external.write("\n; external change\n");
    }
    const auto changed = bytes(source);
    dialog.buttons()->button(QDialogButtonBox::Ok)->click();
    check(dialog.isVisible() && bytes(source) == changed,
          "Material manager overwrote an external change");
    dialog.reject();
    {
      QFile restore(source);
      check(restore.open(QIODevice::WriteOnly | QIODevice::Truncate),
            "Cannot restore material fixture");
      restore.write(before);
    }
  }
  {
    zima::app::MaterialDialog dialog(
        fixture.path(), policy, [&] { ++saved; }, configuration, &parent);
    dialog.findChild<QListWidget*>("materialLibraryFiles")->setCurrentRow(0);
    dialog.show();
    dialog.findChild<QPushButton *>("materialLibraryNewFolder")->click();
    auto *naming = parent.findChild<QDialog *>("pathNameDialog");
    check(naming, "New folder name editor is missing");
    naming->findChild<QLineEdit *>("pathName")->setText("New folder");
    naming->findChild<QDialogButtonBox *>()
        ->button(QDialogButtonBox::Ok)
        ->click();
    application.processEvents();
    check(QDir(fixture.filePath("New folder")).exists(),
          "Material folder was not created");
    QCoreApplication::sendPostedEvents(nullptr, QEvent::DeferredDelete);
    dialog.findChild<QPushButton *>("materialLibraryNew")->click();
    application.processEvents();auto* table=dialog.findChild<QTableWidget*>("materialTable");const int row=table->rowCount()-1;check(table->item(row,1)&&table->item(row,1)->text().isEmpty(),"New material has no editable property row");table->item(row,1)->setText("MASS_DENSITY");table->item(row,2)->setText("7.900000E-06");auto* unit=qobject_cast<QComboBox*>(table->cellWidget(row,3));check(unit&&unit->findText("kg/mm^3")>=0,"New material property does not offer its native units");unit->setCurrentText("kg/mm^3");
    QTimer::singleShot(0, [&] {
      auto *chooser = dialog.findChild<QFileDialog *>();
      if (chooser) {
        chooser->setDirectory(fixture.path());
        QMetaObject::invokeMethod(chooser, "accept", Qt::DirectConnection);
      }
    });
    dialog.findChild<QPushButton *>("materialLibrarySaveAs")->click();
    naming = parent.findChild<QDialog *>("pathNameDialog");
    check(naming, "Save As name editor is missing");
    naming->findChild<QLineEdit *>("pathName")->setText("New material");
    naming->findChild<QDialogButtonBox *>()
        ->button(QDialogButtonBox::Ok)
        ->click();
    application.processEvents();
    const auto created =
        zima::document::load_material_library(std::filesystem::u8path(
            fixture.filePath("New material.matz").toStdString()));
    check(created.properties.contains("MATERIAL_NAME")&&created.properties.at("MASS_DENSITY")=="7.900000E-06"&&created.units.at("MASS_DENSITY")=="kg/mm^3",
          "New material was not saved and reopened");
    dialog.reject();
    QCoreApplication::sendPostedEvents(nullptr, QEvent::DeferredDelete);
  }
  {
    zima::app::MaterialDialog dialog(source,policy,[&]{++saved;},configuration,&parent);dialog.show();application.processEvents();
    check(dialog.objectName()=="materialPropertiesDialog"&&dialog.windowTitle()==settings.qt_translations.value("Material properties"),"Individual material properties title is missing or untranslated");
    check(!dialog.findChild<QTreeView*>("materialLibraryTree")&&!dialog.findChild<QListWidget*>("materialLibraryFiles")&&!dialog.findChild<QPushButton*>("materialLibraryNew"),"Individual material properties constructed library navigation");
    check(dialog.buttons()->buttons().size()==3&&dialog.findChild<QPushButton*>("materialLibrarySaveAs")->parentWidget()==dialog.buttons(),"Individual material properties must expose OK, Cancel and Save As at the bottom");
    const auto before=bytes(source);density(dialog)->setText("8.500000E-06");
    QTimer::singleShot(0,[&]{if(auto* chooser=dialog.findChild<QFileDialog*>()){chooser->setDirectory(fixture.path());QMetaObject::invokeMethod(chooser,"accept",Qt::DirectConnection);}});
    dialog.findChild<QPushButton*>("materialLibrarySaveAs")->click();auto* naming=parent.findChild<QDialog*>("pathNameDialog");check(naming,"Individual Save As name editor is missing");naming->findChild<QLineEdit*>("pathName")->setText("Individual copy");naming->findChild<QDialogButtonBox*>()->button(QDialogButtonBox::Ok)->click();application.processEvents();QCoreApplication::sendPostedEvents(nullptr,QEvent::DeferredDelete);
    const auto copy=std::filesystem::u8path(fixture.filePath("Individual copy.matz").toStdString());check(bytes(source)==before&&zima::document::load_material_library(copy).properties.at("MASS_DENSITY")=="8.500000E-06"&&!dialog.findChild<QTreeView*>("materialLibraryTree"),"Individual Save As changed its source or opened the manager");
    density(dialog)->setText("9.000000E-06");dialog.reject();check(zima::document::load_material_library(copy).properties.at("MASS_DENSITY")=="8.500000E-06","Individual Cancel committed edits");
  }
  std::cout << "Material manager verified: " << settings.language.toStdString()
            << '\n';
}
