static int verify_reference_pick_performance_ui(QApplication& application,const std::filesystem::path& directory) {
    using namespace zima;
    const auto check=[](bool ok,const char* text){if(!ok)throw std::runtime_error(text);};
    const auto flush=[&]{application.processEvents();QCoreApplication::sendPostedEvents(nullptr,QEvent::DeferredDelete);application.processEvents();};
    try {
        const auto source=std::filesystem::u8path(qEnvironmentVariable("ZIMA_VERIFY_REFERENCE_PICK_SOURCE").toStdString());
        const auto file=directory/"reference-pick-source.prtz";std::filesystem::copy_file(source,file,std::filesystem::copy_options::overwrite_existing);
        const auto source_document=document::PartDocument::load(file);const auto body=source_document.body_history.active_body_id();check(!body.empty(),"Reference fixture has no active Body");
        app::AssemblyWorkspaceWindow window(QString::fromStdWString(directory.wstring()));window.resize(1400,1000);window.show();flush();
        check(window.open_document_path(QString::fromStdWString(file.wstring())),"Reference performance copy cannot open");flush();
        check(activate_test_body(application,window,body),"Reference performance Body cannot activate");
        auto* view=dynamic_cast<viewer::MeshView*>(window.findChild<QOpenGLWidget*>());check(view,"Reference performance View missing");view->fit_all();flush();
        const auto original=window.execute_console_command("documents").data;
        auto* action=window.findChild<QAction*>("sketchAction");check(action&&action->isEnabled(),"Reference fixture cannot create a Sketch");
        QElapsedTimer timer;timer.start();action->trigger();flush();
        app::SketchPropertiesDialog* dialog{};
        for(auto* widget:window.findChildren<QDialog*>())if(auto* candidate=dynamic_cast<app::SketchPropertiesDialog*>(widget);candidate&&candidate->isVisible())dialog=candidate;
        check(dialog,"Reference performance Sketch Properties missing");
        std::cout<<"Reference GUI open ms="<<timer.elapsed()<<" vertices="<<view->mesh().vertices.size()<<" triangles="<<view->mesh().triangles.size()/3<<std::endl;
        auto* table=dialog->findChild<QTableWidget*>("sketchReferenceTable");check(table&&table->rowCount(),"Reference performance input row missing");
        table->scrollToItem(table->item(0,1));flush();const auto field=table->visualItemRect(table->item(0,1)).center();
        timer.restart();
        for(auto type:{QEvent::MouseButtonPress,QEvent::MouseButtonRelease}) {
            QMouseEvent event(type,QPointF(field),QPointF(table->viewport()->mapToGlobal(field)),Qt::LeftButton,
                type==QEvent::MouseButtonPress?Qt::LeftButton:Qt::NoButton,Qt::NoModifier);QApplication::sendEvent(table->viewport(),&event);flush();
        }
        std::cout<<"Reference GUI arm ms="<<timer.elapsed()<<std::endl;
        std::vector<QPointF> samples;std::set<std::tuple<std::string,std::string,std::string>> seen;
        const viewer::PickingIndex picking(view->mesh());
        for(int y=25;y<view->height()&&samples.size()<16;y+=35)for(int x=25;x<view->width()&&samples.size()<16;x+=35) {
            const QPointF point(x,y);const auto ray=view->ray_at(point);if(!ray)continue;
            for(const auto& hit:viewer::ordered_ray_candidates(view->mesh(),ray->first,ray->second,true,&picking)) {
                const auto& ref=hit.reference;
                if(ref.valid()&&!ref.semantic_key.starts_with("origin:")&&seen.emplace(ref.owner_id,ref.semantic_key,ref.instance_path).second) {samples.push_back(point);break;}
            }
        }
        check(!samples.empty(),"Reference performance fixture has no visible face samples");
        const auto scene_revision=view->base_mesh_revision();
        std::optional<QPointF> accepted;
        std::optional<viewer::ViewerCandidate> accepted_face;
        std::size_t accepted_depth=std::numeric_limits<std::size_t>::max();
        for(std::size_t i=0;i<samples.size();++i) {
            const auto pixel=samples[i];
            timer.restart();QMouseEvent move(QEvent::MouseMove,pixel,QPointF(view->mapToGlobal(pixel.toPoint())),Qt::NoButton,Qt::NoButton,Qt::NoModifier);
            QApplication::sendEvent(view,&move);flush();const auto mouse_ns=timer.nsecsElapsed();
            timer.restart();const auto cold=view->selection_candidates_at(pixel);const auto cold_ns=timer.nsecsElapsed();
            timer.restart();const auto warm=view->selection_candidates_at(pixel);const auto warm_ns=timer.nsecsElapsed();
            const auto faces=std::ranges::count_if(cold,[](const auto& c){return c.kind==viewer::CandidateKind::Face;});
            if(faces) {
                for(std::size_t depth=0;depth<cold.size()&&depth<accepted_depth;++depth) {
                    if(cold[depth].kind!=viewer::CandidateKind::Face)continue;
                    const auto triangles=view->candidate_face_triangles(cold[depth]);if(triangles.size()<3)continue;
                    const auto normal=kernel::dimension_unit(kernel::dimension_cross(
                        kernel::dimension_sub(triangles[1],triangles[0]),kernel::dimension_sub(triangles[2],triangles[0])));
                    if(kernel::dimension_dot(normal,normal)<.5||std::ranges::any_of(triangles,[&](const auto& p){
                        return std::abs(kernel::dimension_dot(kernel::dimension_sub(p,triangles[0]),normal))>1e-7;
                    }))continue;
                    accepted=pixel;accepted_face=cold[depth];accepted_depth=depth;
                }
            }
            std::cout<<"Reference GUI sample="<<i<<" query_ms="<<cold_ns/1.e6<<" warm_ms="<<warm_ns/1.e6<<" cold_mouse_ms="<<mouse_ns/1.e6<<" candidates="<<cold.size()<<" faces="<<faces<<std::endl;
            check(cold==warm,"Unchanged reference candidate query changed ordering or geometry");
        }
        check(view->base_mesh_revision()==scene_revision,"Reference hover republished the base model scene");
        check(window.execute_console_command("documents").data==original,"Reference hover changed the model");
        check(accepted.has_value(),"Reference sampling did not offer an actual Face; confirmation was not verified");
        if(accepted) {
            QMouseEvent move(QEvent::MouseMove,*accepted,QPointF(view->mapToGlobal(accepted->toPoint())),Qt::NoButton,Qt::NoButton,Qt::NoModifier);
            QApplication::sendEvent(view,&move);flush();
            const auto choices=view->selection_candidates_at(*accepted);
            for(std::size_t cycle=0;cycle<choices.size();++cycle) {
                const auto current=view->hovered_candidate();
                if(current==accepted_face)break;
                for(auto type:{QEvent::MouseButtonPress,QEvent::MouseButtonRelease}) {
                    QMouseEvent event(type,*accepted,QPointF(view->mapToGlobal(accepted->toPoint())),Qt::RightButton,
                        type==QEvent::MouseButtonPress?Qt::RightButton:Qt::NoButton,Qt::NoModifier);
                    QApplication::sendEvent(view,&event);flush();
                }
            }
            check(view->hovered_candidate()==accepted_face,
                "Actual mouse cycling did not reach the offered Face");
            const auto face_triangles=view->candidate_face_triangles(*accepted_face);
            const auto before_confirmation=view->base_mesh_revision();
            timer.restart();for(auto type:{QEvent::MouseButtonPress,QEvent::MouseButtonRelease}) {
                QMouseEvent event(type,*accepted,QPointF(view->mapToGlobal(accepted->toPoint())),Qt::LeftButton,
                    type==QEvent::MouseButtonPress?Qt::LeftButton:Qt::NoButton,Qt::NoModifier);QApplication::sendEvent(view,&event);flush();
            }
            std::cout<<"Reference GUI confirmation ms="<<timer.elapsed()<<std::endl;
            check(view->base_mesh_revision()==before_confirmation+1,
                "Sketch face confirmation must publish exactly one complete final scene");
            const auto pending=dialog->pending_value().second;
            kernel::ViewerMesh frame;frame.vertices={{pending.x,pending.y,pending.z}};
            const auto point=source_document.place_body_mesh(std::move(frame),body).vertices.front();
            const auto normal=kernel::dimension_unit(kernel::dimension_cross(
                kernel::dimension_sub(face_triangles[1],face_triangles[0]),kernel::dimension_sub(face_triangles[2],face_triangles[0])));
            check(std::abs(kernel::dimension_dot(kernel::dimension_sub(point,face_triangles[0]),normal))<1e-6,
                "Confirmed Sketch Origin violates the independent selected-plane equation");
        }
        dialog->findChild<QDialogButtonBox*>()->button(QDialogButtonBox::Cancel)->click();flush();
        check(window.execute_console_command("documents").data==original,"Reference performance Cancel changed its source");
        std::cout<<"Reference GUI: real saved model, cold/warm candidate queries, actual hover, confirmation and unchanged Cancel completed"<<std::endl;return 0;
    }catch(const std::exception& error){std::cerr<<"Reference performance GUI: "<<error.what()<<std::endl;return 1;}
}
