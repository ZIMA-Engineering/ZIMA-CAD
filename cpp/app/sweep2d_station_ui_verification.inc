    const auto flush_station_ui=[&]{application.processEvents();QCoreApplication::sendPostedEvents(nullptr,QEvent::DeferredDelete);application.processEvents();};
    {
        const auto station_path=directory/"sweep2d-endpoint-stations.prtz";
        zima::document::PartDocument::create_default().save(station_path);
        if(!verify(window.open_document_path(QString::fromStdString(station_path.string())),"Cannot open station fixture"))return 1;
        action->trigger();flush_station_ui();
        dialog=dynamic_cast<zima::app::Sweep2DDialog*>(window.findChild<QDialog*>("sweep2dDialog"));
        auto guide=zima::sketcher::Sketch::from_serialized(dialog->pending.sweep2d.path_sketch);
        static_cast<void>(guide.add_segment(0,0,20,0));
        static_cast<void>(guide.add_arc(20,10,20,0,30,10));
        const auto junction=guide.segments.front().second_point_id;
        if(!verify(junction==guide.arcs.front().start_point_id,"Fixture lost shared line/arc incidence"))return 1;
        dialog->set_sketch(0,guide);flush_station_ui();
        auto* table=dialog->findChild<QTableWidget*>("sweep2dProfiles");
        if(!verify(table->rowCount()==4,"Line plus arc must expose the start/end of both segments"))return 1;
        const auto add_profile=[&](int row,double radius) {
            const auto route=zima::document::PartDocument::sweep2d_route(dialog->pending);
            const auto station=route.stations[row];
            const auto index=zima::document::PartDocument::ensure_sweep2d_profile(dialog->pending,station.point_id,station.incoming);
            auto profile=zima::sketcher::Sketch::from_serialized(dialog->pending.sweep2d.profiles[index].sketch_serialized);
            const auto circle=profile.add_circle(0,0,radius);
            profile.apply_dimension(profile.create_circle_radius_dimension(circle));
            add_profile_points(profile);
            dialog->set_sketch(static_cast<unsigned>(index+1),profile);flush_station_ui();
        };
        add_profile(0,2);add_profile(1,3);add_profile(3,4);
        const auto definition=dialog->pending;
        const auto mapping=zima::document::sweep3d_profile_correspondence(zima::sketcher::Sketch::from_serialized(definition.sweep2d.profiles.front().sketch_serialized));
        for(bool reverse:{false,true}) {
            dialog->findChild<QPushButton*>("sweep2dPointOrder0")->click();flush_station_ui();
            auto* order=dynamic_cast<zima::app::SweepPointOrderDialog*>(window.findChild<QDialog*>("sweepPointOrderDialog"));
            if(!verify(order&&order->isVisible(),"2D Sweep point-order editor did not open"))return 1;
            view->set_standard_view(zima::viewer::StandardView::Isometric);view->fit_all();flush_station_ui();
            QEventLoop settle;QTimer::singleShot(950,&settle,&QEventLoop::quit);settle.exec();
            for(const auto point:{mapping.point_ids.front(),reverse?mapping.point_ids.back():mapping.point_ids[1]}) {
                std::optional<QPointF> hit;
                for(int y=8;y<view->height()-8&&!hit;y+=3)for(int x=8;x<view->width()-8&&!hit;x+=3) {
                    const auto candidates=view->selection_candidates_at(QPointF(x,y));
                    if(!candidates.empty()&&candidates.front().semantic_key.ends_with(":"+point))hit=QPointF(x,y);
                }
                if(!verify(hit.has_value(),"2D Sweep point-order View picker lost the profile point"))return 1;
                mouse(*hit,Qt::LeftButton);flush_station_ui();
            }
            const auto& profile=dialog->pending.sweep2d.profiles.front();
            if(!verify(profile.correspondence_start_point_id==mapping.point_ids.front()&&profile.correspondence_reversed==reverse,
                "2D Sweep View clicks lost first point or direction"))return 1;
            if(!verify(std::ranges::count_if(view->transient_edges(),[](const auto& edge){return edge.reference.semantic_key.starts_with("profile-correspondence-link:");})==4,
                "2D Sweep point ordering lost its adjacent profile links"))return 1;
            window.grab().save(QString::fromStdString((directory/"sweep-point-order-view.png").string()));
            order->buttons()->button(QDialogButtonBox::Cancel)->click();flush_station_ui();
            if(!verify(dialog->pending==definition,"2D Sweep point-order Cancel changed its native draft"))return 1;
        }
        const auto original_language=zima::app::ApplicationSettings::load();
        dialog->hide();
        for(const auto* language:{"cs","en","de","fr","ru"}) {
            QTemporaryDir language_directory;
            QSettings config(language_directory.filePath("config.ini"),QSettings::IniFormat);
            config.setValue("Application/Language",language);
            config.setValue("Paths/Localization",QString::fromStdString((std::filesystem::current_path()/"config/localization").generic_string()));config.sync();
            const auto settings=zima::app::ApplicationSettings::load(language_directory.path());
            zima::app::apply_application_translations(application,settings);
            zima::app::Sweep2DDialog translated(definition,[](auto){},&window);
            translated.show();flush_station_ui();
            auto* rows=translated.findChild<QTableWidget*>("sweep2dProfiles");
            const auto expected=settings.qt_translations.value("Vlastní");
            if(!verify(rows&&rows->rowCount()==4&&!expected.isEmpty()&&rows->item(1,3)->text()==expected,
                "Endpoint station presentation is not localized"))return 1;
            window.grab().save(QString::fromStdString((directory/(std::string("sweep-stations-")+language+".png")).string()));
        }
        zima::app::apply_application_translations(application,original_language);dialog->show();
        if(!verify(dialog->pending==definition,"Displaying endpoint stations changed native data"))return 1;
        if(!verify(!dialog->findChild<QPushButton*>("sweep2dPointOrder2")->isEnabled(),"Inherited outgoing profile unexpectedly became explicit"))return 1;
        dialog->findChild<QPushButton*>("sweep2dStationSketch2")->click();flush_station_ui();
        if(!verify(dialog->pending.sweep2d.profiles.size()==4,"Outgoing Sketch did not retain the separate native definition"))return 1;
        auto context=view->read_only_profile_context();
        if(!verify(context.points.size()==8&&!context.edges.empty(),"Profile Sketch is missing numbered prior/following station context"))return 1;
        for(const auto& point:context.points) {
            const bool first=point.label.starts_with("1 · ");
            const bool last=point.label.starts_with("3 · ");
            if(!verify(first||last,"Context includes the active shared station or has wrong station labels"))return 1;
            const auto p=point.position;
            if(first&&!verify(std::abs(p.x)<1e-7&&std::abs(p.y*p.y+p.z*p.z-4)<1e-7,"First profile context is not on its radius-2 circle and actual plane"))return 1;
            if(last&&!verify(std::abs(p.y-10)<1e-7&&std::abs((p.x-30)*(p.x-30)+p.z*p.z-16)<1e-7,"Last profile context is not on its radius-4 circle and actual plane"))return 1;
            if(!verify(std::ranges::none_of(view->mesh().points,[&](const auto& p){return p.reference.semantic_key==point.reference.semantic_key;}),"Read-only profile leaked into editable mesh points"))return 1;
        }
        // Ordinary drawing previews must not replace the persistent guides.
        const auto context_edges=context.edges.size();
        view->set_transient_edges({});view->set_transient_points({{1,2,3}});flush_station_ui();
        if(!verify(view->read_only_profile_context().edges.size()==context_edges,"Drawing preview erased previous profiles"))return 1;
        for(const auto* theme:{"light","dark","light"}) {
            auto settings=zima::app::ApplicationSettings{};settings.theme=theme;
            zima::app::apply_application_appearance(application,settings);flush_station_ui();
            window.grab().save(QString::fromStdString((directory/(std::string("sweep-profile-context-")+theme+".png")).string()));
            if(!verify(view->read_only_profile_context().points.size()==8,"Live theme change erased profile guides"))return 1;
        }
        finish->trigger();flush_station_ui();
        if(!verify(view->read_only_profile_context().edges.empty(),"Finishing profile retained its Sketcher-only guides"))return 1;
        const auto unchanged_profiles=dialog->pending.sweep2d.profiles;
        for(int output:{1,2,0}) {
            dialog->findChild<QComboBox*>("sweep2dResultType")->setCurrentIndex(output);flush_station_ui();
            dialog->findChild<QPushButton*>("sweep2dStationSketch2")->click();flush_station_ui();
            for(const auto& edge:view->read_only_profile_context().edges) {
                if(!edge.reference.semantic_key.starts_with("sweep2d:profile:"))continue;
                const bool first=edge.reference.semantic_key.starts_with("sweep2d:profile:"+guide.segments.front().first_point_id+":");
                for(const auto& p:edge.points) {
                    const double equation=first?p.y*p.y+p.z*p.z-4:(p.x-30)*(p.x-30)+p.z*p.z-16;
                    if(!verify(std::abs(equation)<1e-6,"Thin/Surface context replaced the authored circle with an offset contour"))return 1;
                }
            }
            finish->trigger();flush_station_ui();
            if(!verify(dialog->pending.sweep2d.profiles==unchanged_profiles,"Changing output/display modified profile definitions"))return 1;
        }
        auto outgoing=std::ranges::find_if(dialog->pending.sweep2d.profiles,[&](const auto& profile){return profile.point_id==junction&&!profile.incoming;});
        const auto outgoing_index=static_cast<unsigned>(outgoing-dialog->pending.sweep2d.profiles.begin());
        auto profile=zima::sketcher::Sketch::from_serialized(outgoing->sketch_serialized);
        static_cast<void>(profile.add_circle(0,0,3));add_profile_points(profile);dialog->set_sketch(outgoing_index+1,profile);flush_station_ui();
        if(!verify(table->rowCount()==4&&dialog->findChild<QPushButton*>("sweep2dPointOrder2")->isEnabled(),"Explicit outgoing profile changed endpoint rows or lost ordering"))return 1;
        auto expected=dialog->pending;
        zima::document::PartDocument::reframe_sweep2d_sketches(expected);
        dialog->buttons()->button(QDialogButtonBox::Ok)->click();flush_station_ui();
        if(!verify(!dialog->isVisible(),"Sweep could not commit both joint profiles"))return 1;
        save->trigger();flush_station_ui();
        auto stored=zima::document::PartDocument::load(station_path);
        if(!verify(stored.history.size()==1,"Endpoint-station Sweep was not saved"))return 1;
        // A path-plane reference persists its source and offset only. The
        // unused placement/orientation bookkeeping is not its native contract.
        auto expected_document=stored;expected_document.history.front()=expected;
        const auto expected_native=expected_document.serialized().at("history").at(0).at("sweep2d");
        if(!verify(stored.serialized().at("history").at(0).at("sweep2d")==expected_native&&
            stored.history.front().sweep2d.profiles==expected.sweep2d.profiles&&stored.history.front().sweep2d.reference_valid,
            "Native reopen lost per-side profile definitions"))return 1;
        window.findChild<QAction*>("undoAction")->trigger();flush_station_ui();
        window.findChild<QAction*>("redoAction")->trigger();flush_station_ui();save->trigger();flush_station_ui();
        if(!verify(zima::document::PartDocument::load(station_path).serialized().at("history").at(0).at("sweep2d")==expected_native,"Undo/Redo lost shared-station profiles"))return 1;
        QCoreApplication::sendPostedEvents(nullptr,QEvent::DeferredDelete);
        QTreeWidgetItem* station_row{};
        for(QTreeWidgetItemIterator i(tree);*i;++i)if((*i)->data(0,Qt::UserRole).toString().toStdString()==expected.id&&(*i)->data(0,Qt::UserRole+3)=="part-container"){station_row=*i;break;}
        if(!verify(station_row,"Saved Sweep row missing"))return 1;
        window.show_tree_item_properties(station_row);flush_station_ui();
        dialog=dynamic_cast<zima::app::Sweep2DDialog*>(window.findChild<QDialog*>("sweep2dDialog"));
        table=dialog->findChild<QTableWidget*>("sweep2dProfiles");
        auto* remove=qobject_cast<QAbstractButton*>(table->cellWidget(2,4)->property("_removeWidget").value<QObject*>());
        if(!verify(remove,"Explicit joint profile has no removal control"))return 1;
        remove->click();flush_station_ui();
        if(!verify(table->rowCount()==4&&dialog->pending.sweep2d.profiles.size()==3&&
            std::ranges::none_of(dialog->pending.sweep2d.profiles,[&](const auto& p){return p.point_id==junction&&!p.incoming;})&&
            std::ranges::any_of(dialog->pending.sweep2d.profiles,[&](const auto& p){return p.point_id==junction&&p.incoming;})&&
            !dialog->findChild<QPushButton*>("sweep2dPointOrder2")->isEnabled(),"Removing an explicit side removed the station or the other profile"))return 1;
        dialog->buttons()->button(QDialogButtonBox::Cancel)->click();flush_station_ui();save->trigger();flush_station_ui();
        if(!verify(zima::document::PartDocument::load(station_path).serialized().at("history").at(0).at("sweep2d")==expected_native,"Cancel committed profile removal"))return 1;
        QCoreApplication::sendPostedEvents(nullptr,QEvent::DeferredDelete);
    }
    if(const auto source=qEnvironmentVariable("ZIMA_VERIFY_SWEEP2D_SOURCE");!source.isEmpty()) {
        const auto original=zima::document::PartDocument::load(std::filesystem::path(source.toStdWString()));
        const auto feature=std::ranges::find_if(original.history,[](const auto& item){return item.feature_kind==zima::document::FeatureKind::Sweep2D;});
        if(!verify(feature!=original.history.end(),"Representative input has no 2D Sweep"))return 1;
        const auto copy=directory/"representative-sweep-copy.prtz";
        std::filesystem::copy_file(std::filesystem::path(source.toStdWString()),copy,std::filesystem::copy_options::overwrite_existing);
        if(!verify(window.open_document_path(QString::fromStdString(copy.string())),"Cannot open representative Sweep copy"))return 1;
        flush_station_ui();
        QTreeWidgetItem* row{};
        for(QTreeWidgetItemIterator i(tree);*i;++i)if((*i)->data(0,Qt::UserRole).toString().toStdString()==feature->id&&(*i)->data(0,Qt::UserRole+3)=="part-container"){row=*i;break;}
        if(!verify(row,"Representative Sweep Tree item missing"))return 1;
        window.show_tree_item_properties(row);flush_station_ui();
        dialog=dynamic_cast<zima::app::Sweep2DDialog*>(window.findChild<QDialog*>("sweep2dDialog"));
        if(!verify(dialog&&dialog->findChild<QTableWidget*>("sweep2dProfiles")->rowCount()==4,"Representative line/arc model does not show four endpoint stations"))return 1;
        for(int station=0;station<4;++station) {
            dialog->findChild<QPushButton*>(QString("sweep2dStationSketch%1").arg(station))->click();flush_station_ui();
            if(!verify(!view->read_only_profile_context().edges.empty()&&!view->read_only_profile_context().points.empty(),"Representative Sketch lacks numbered other profiles"))return 1;
            if(station==1) {
                QEventLoop animation;QTimer::singleShot(1000,&animation,&QEventLoop::quit);animation.exec();
                window.grab().save(QString::fromStdString((directory/"representative-sweep-context.png").string()));
            }
            finish->trigger();flush_station_ui();
        }
        dialog->buttons()->button(QDialogButtonBox::Cancel)->click();flush_station_ui();
        save->trigger();flush_station_ui();
        if(!verify(zima::document::PartDocument::load(copy).history==original.history,"Read-only inspection changed the representative model"))return 1;
        QCoreApplication::sendPostedEvents(nullptr,QEvent::DeferredDelete);
    }
