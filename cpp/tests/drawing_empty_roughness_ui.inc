int verify_empty_roughness_ui() {
    using namespace zima;
    unsigned cases{};
    for(bool with_empty:{false,true})for(const QString language:{"cs","en","de","fr","ru"}) {
        QTemporaryDir temporary;require(temporary.isValid(),"Empty roughness directory unavailable");
        QSettings config(temporary.filePath("config.ini"),QSettings::IniFormat);
        config.setValue("Application/Language",language);config.sync();
        const auto settings=app::ApplicationSettings::load(temporary.path());app::apply_application_translations(*qApp,settings);
        auto doc=drawing::DrawingDocument::create_default();
        const auto source=std::filesystem::current_path()/"config/formats/ZE-TITLE-BLOCK.tblz";
        workspace::load_drawing_template(doc,doc.sheets.front().id,source,true,nullptr,{},"ze-title-block-variant-"+language.toUpper().toStdString());
        const auto target=std::ranges::find_if(doc.sheets.front().title_block_symbols,[](const auto& symbol){return symbols::Definition::from_serialized(symbol.definition).id=="ze:general-surface-texture:iso21920";});
        require(target!=doc.sheets.front().title_block_symbols.end(),"Factory title block has no general roughness");
        if(!with_empty){auto definition=symbols::Definition::from_serialized(target->definition);definition.variants.erase("empty");target->definition=definition.serialized();}
        const auto original=*target;const auto id=original.id;
        workspace::Workspace live;live.add_drawing(doc);
        app::DrawingWindow window(&live,false);window.setAttribute(Qt::WA_DontShowOnScreen);window.resize(1300,900);window.edit_workspace_document(doc.document_id);window.show();flush();
        auto* canvas=window.findChild<QWidget*>("drawingCanvas");require(canvas,"Empty roughness canvas missing");
        const auto open=[&] {
            const auto at=window.title_field_center_for_test("symbol:"+id);require(at.has_value(),"Visible roughness has no common hit region");
            click(canvas,*at);mouse(canvas,QEvent::MouseButtonDblClick,*at,Qt::LeftButton,Qt::LeftButton);flush();
            auto* dialog=window.findChild<QDialog*>("symbolPropertiesDialog");require(dialog&&dialog->isVisible(),"Roughness Properties did not open");return dialog;
        };
        const auto revision=live.open_drawing(doc.document_id)->revision();
        open()->findChild<QDialogButtonBox*>()->button(QDialogButtonBox::Ok)->click();flush();
        require(live.open_drawing(doc.document_id)->revision()==revision,"Unchanged roughness OK inserted a new definition or transaction");
        for(bool cancel:{true,false}) {
            auto* dialog=open();auto* variants=dialog->findChild<QComboBox*>("symbolVariant");
            const auto index=variants->findData("empty");require(index>=0&&variants->itemText(index)==settings.qt_translations.value("Prázdná varianta"),"Empty roughness choice is missing or untranslated");
            variants->setCurrentIndex(index);flush();
            auto* field=dialog->findChild<QComboBox*>("symbolField:Specification");require(field&&!field->isVisible(),"Empty variant still exposes a roughness text field");
            if(cancel){dialog->reject();flush();require(window.document_for_test().sheets.front().title_block_symbols==doc.sheets.front().title_block_symbols,"Empty preview Cancel changed the title block");continue;}
            dialog->findChild<QDialogButtonBox*>()->button(QDialogButtonBox::Ok)->click();flush();
        }
        auto expected=original;expected.variant="empty";
        if(!with_empty){auto definition=symbols::Definition::from_serialized(original.definition);definition.variants.try_emplace("empty");expected.definition=definition.serialized();}
        const auto& symbols=window.document_for_test().sheets.front().title_block_symbols;
        require(std::ranges::find(symbols,id,&sketcher::SymbolInstance::id)!=symbols.end()&&*std::ranges::find(symbols,id,&sketcher::SymbolInstance::id)==expected,"Empty OK changed the symbol definition, placement or text values");
        const auto empty=[&](const drawing::DrawingSheet& sheet) {
            const auto layout=drawing::title_block_layout(sheet,{});
            require(std::ranges::none_of(layout.lines,[&](const auto& item){return item.field_id=="symbol:"+id;})&&
                std::ranges::none_of(layout.texts,[&](const auto& item){return item.field_id=="symbol:"+id;}),"Empty title-block symbol still renders lines, brackets or text");
        };
        empty(window.document_for_test().sheets.front());
        auto* state=live.open_drawing(doc.document_id);require(state->undo(),"Empty roughness has no Undo");window.edit_workspace_document(doc.document_id);flush();
        require(window.title_field_center_for_test("symbol:"+id).has_value(),"Undo did not restore the visible roughness mark");
        require(state->redo(),"Empty roughness has no Redo");window.edit_workspace_document(doc.document_id);flush();empty(window.document_for_test().sheets.front());
        const auto file=std::filesystem::path(temporary.filePath("assembly-title.drwz").toStdWString());window.document_for_test().save(file);
        const auto reopened=drawing::DrawingDocument::load(file);empty(reopened.sheets.front());
        require(reopened.sheets.front().title_block_symbols==window.document_for_test().sheets.front().title_block_symbols,"Empty roughness did not survive native reopen");++cases;
    }
    std::cout<<"General roughness Empty GUI: "<<cases<<" factory/embedded title-block language cases, real Properties click, localized choice, unchanged OK, hidden fields, Cancel, OK, Undo/Redo and native reopen passed\n";return 0;
}
