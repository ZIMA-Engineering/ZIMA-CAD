static int verify_imported_body_interaction(QApplication& application,zima::app::AssemblyWorkspaceWindow& window,
        const std::filesystem::path& directory) {
    using namespace zima;
    try {
        const auto check=[](bool value,const char* message){if(!value)throw std::runtime_error(message);};
        const auto flush=[&]{application.processEvents();QCoreApplication::sendPostedEvents(nullptr,QEvent::DeferredDelete);application.processEvents();};
        window.resize(1100,850);window.show();flush();
        QTimer modal_errors;
        QObject::connect(&modal_errors,&QTimer::timeout,[&] {
            if(auto* box=qobject_cast<QMessageBox*>(QApplication::activeModalWidget())) {
                std::cerr<<"Unexpected import GUI dialog: "<<box->text().toStdString()<<std::endl;box->reject();
            }
        });modal_errors.start(100);
        std::filesystem::create_directories(directory);
        const auto path=directory/"imported-body-preview.prtz";
        if(const auto source=qEnvironmentVariable("ZIMA_VERIFY_IMPORTED_BODY_FILE");!source.isEmpty())
            std::filesystem::copy_file(std::filesystem::u8path(source.toStdString()),path,std::filesystem::copy_options::overwrite_existing);
        else {
            kernel::OcctKernel kernel;kernel::StepProduct product;product.definition_id="preview-source";product.name="Import preview fixture";
            product.body=zima::test::profile_body(kernel,{10,20,30});
            const auto step=directory/"preview-source.step";kernel.export_step(product,step.string());
            auto imported=interchange::import_step_part(document::PartDocument::create_default(),{},step);
            imported.document.save(path,imported.calculated);std::filesystem::remove(step);
        }
        std::vector<kernel::BodyResult> calculated;const auto original=document::PartDocument::load(path,&calculated);
        const auto found=std::ranges::find_if(original.history,[](const auto& feature){return feature.feature_kind==document::FeatureKind::ImportedStep;});
        check(found!=original.history.end()&&!calculated.empty(),"Imported Body fixture is missing");
        const auto* owner=original.body_owner_for_object(found->id);check(owner,"Import is outside its Body");
        const auto body_id=owner->scope.id;
        const auto branch=calculated.back().body_boundaries.find(body_id);
        check(branch!=calculated.back().body_boundaries.end()&&!branch->second.empty(),"Import lacks local calculated packet");
        const auto& local=branch->second.back().mesh;
        check(window.open_document_path(QString::fromStdWString(path.wstring())),"Cannot open imported preview fixture");flush();
        const auto run=[&](const char* command){const auto result=window.execute_console_command(QString::fromStdString(nlohmann::json{{"command",command},{"arguments",nlohmann::json::object()}}.dump()));check(result.ok,"Import GUI console action failed");flush();};
        const auto dialog=[&]()->app::BodyPropertiesDialog* {
            for(auto* value:window.findChildren<QDialog*>())if(value->isVisible())if(auto* typed=dynamic_cast<app::BodyPropertiesDialog*>(value))return typed;return nullptr;
        };
        auto* view=dynamic_cast<viewer::MeshView*>(window.findChild<QOpenGLWidget*>());check(view,"Import View missing");
        if(qEnvironmentVariableIsSet("ZIMA_VERIFY_IMPORT_EXTRUSION_ONLY")) {
            qputenv("ZIMA_CPP_VERIFY_GPU_UPLOAD","1");view->setProperty("forceMeshUpload",true);view->repaint();flush();
            if(!qEnvironmentVariableIsSet("ZIMA_VERIFY_IMPORTED_BODY_FILE")) {
                const auto original_mesh=view->mesh();auto changed=original_mesh;
                check(!changed.vertices.empty(),"GPU invalidation fixture is empty");
                changed.vertices.front().x=0.;view->set_mesh(std::move(changed),false);view->repaint();flush();
                auto uploads=view->property("gpuMeshUploadCount").toULongLong();changed=view->mesh();
                changed.vertices.front().x=-0.;view->set_mesh(std::move(changed),false);view->repaint();flush();
                check(view->property("gpuMeshUploadCount").toULongLong()==uploads+1,"Signed-zero GPU input change failed to invalidate reuse");
                uploads=view->property("gpuMeshUploadCount").toULongLong();changed=view->mesh();
                changed.vertices.front().x=1.;view->set_mesh(std::move(changed),false);view->repaint();flush();
                check(view->property("gpuMeshUploadCount").toULongLong()==uploads+1,"Changed geometry failed to invalidate GPU reuse");
                view->set_mesh(original_mesh,false);view->repaint();flush();
            }
            check(activate_test_body(application,window,body_id),"Cannot activate imported Body for Extrusion");flush();
            const auto feature_dialog=[&]()->app::PrimitivePropertiesDialog* {
                for(auto* value:window.findChildren<QDialog*>())if(value->isVisible())
                    if(auto* typed=dynamic_cast<app::PrimitivePropertiesDialog*>(value))return typed;
                return nullptr;
            };
            const auto open=[&] {
                auto* action=window.findChild<QAction*>("extrusionAction");
                check(action&&action->isEnabled(),"Extrusion is unavailable in imported Body");action->trigger();flush();
                check(feature_dialog(),"Extrusion over import did not open");
                check(view->mesh().triangles.size()>=calculated.back().mesh.triangles.size(),"Extrusion hid imported context");
            };
            open();feature_dialog()->buttons()->button(QDialogButtonBox::Cancel)->click();flush();
            const auto packet=[&] {
                kernel::BodyResult value;value.mesh=view->mesh();
                const auto bytes=QByteArray::fromStdString(document::serialize_body_result(value,false).dump());
                return QCryptographicHash::hash(bytes,QCryptographicHash::Sha256);
            };
            const auto expected=packet();const auto state=window.execute_console_command("documents").data;
            const auto camera=view->camera_state();
            const auto* undo=window.findChild<QAction*>("undoAction");const auto* redo=window.findChild<QAction*>("redoAction");
            const bool can_undo=undo->isEnabled(),can_redo=redo->isEnabled();
            for(int trial=0;trial<3;++trial) {
                const auto uploads=view->property("gpuMeshUploadCount").toULongLong();
                const auto before=view->base_mesh_revision();QElapsedTimer timer;timer.start();open();
                const auto opening=timer.nsecsElapsed()/1e6;const auto opened_revision=view->base_mesh_revision();
                const auto opened_uploads=view->property("gpuMeshUploadCount").toULongLong();
                timer.restart();feature_dialog()->buttons()->button(QDialogButtonBox::Cancel)->click();flush();
                const auto closing=timer.nsecsElapsed()/1e6;
                check(!feature_dialog()&&view->camera_state()==camera&&packet()==expected,"Extrusion Cancel did not restore imported scene/camera");
                check(window.execute_console_command("documents").data==state&&undo->isEnabled()==can_undo&&redo->isEnabled()==can_redo,
                    "Extrusion Cancel changed document or Undo/Redo");
                std::cout<<"Imported Extrusion trial="<<trial<<" open_ms="<<opening<<" cancel_ms="<<closing
                    <<" open_publications="<<opened_revision-before<<" close_publications="<<view->base_mesh_revision()-opened_revision
                    <<" open_uploads="<<opened_uploads-uploads<<" close_uploads="<<view->property("gpuMeshUploadCount").toULongLong()-opened_uploads<<std::endl;
            }
            // Compare retained GPU data against an explicitly invalidated full
            // upload of the exact same final scene, without resetting picking,
            // annotation layout or any other scene state through set_mesh().
            QEventLoop settled;QTimer::singleShot(700,&settled,&QEventLoop::quit);settled.exec();flush();
            view->repaint();flush();const auto retained=view->grabFramebuffer();
            const auto retained_digest=view->property("gpuMeshUploadDigest").toByteArray();
            qputenv("ZIMA_CPP_VERIFY_GPU_UPLOAD","1");view->setProperty("forceMeshUpload",true);
            view->repaint();flush();const auto fresh=view->grabFramebuffer();
            const auto fresh_digest=view->property("gpuMeshUploadDigest").toByteArray();
            std::cout<<"GPU upload digest retained="<<retained_digest.constData()<<" fresh="<<fresh_digest.constData()<<std::endl;
            check(!retained_digest.isEmpty()&&retained_digest==fresh_digest,"Reused GPU buffers differ from a fresh preparation");
            const auto a=retained.convertToFormat(QImage::Format_RGBA8888);
            const auto b=fresh.convertToFormat(QImage::Format_RGBA8888);
            check(a.size()==b.size(),"Fresh upload changed framebuffer dimensions");
            int maximum_channel_difference=0;std::size_t changed_pixels=0;
            for(int y=0;y<a.height();++y)for(int x=0;x<a.width();++x) {
                bool changed=false;
                for(int channel=0;channel<4;++channel) {
                    const int difference=std::abs(int(a.constScanLine(y)[4*x+channel])-int(b.constScanLine(y)[4*x+channel]));
                    changed|=difference!=0;maximum_channel_difference=std::max(maximum_channel_difference,difference);
                }
                changed_pixels+=changed;
            }
            std::cout<<"GPU frame comparison changed_pixels="<<changed_pixels<<" maximum_channel_difference="<<maximum_channel_difference<<std::endl;
            if(maximum_channel_difference>1) {
                retained.save(QString::fromStdWString((directory/"extrusion-retained.png").wstring()));
                fresh.save(QString::fromStdWString((directory/"extrusion-fresh.png").wstring()));
                std::cerr<<"Fresh upload packet equal="<<(packet()==expected)<<" camera equal="<<(view->camera_state()==camera)<<std::endl;
            }
            // Upload bytes are bit-exact above. The desktop GL framebuffer can
            // round a few antialiased channels by one 8-bit level after upload.
            check(maximum_channel_difference<=1,"Reused GPU geometry differs from a fresh upload");
            qunsetenv("ZIMA_CPP_VERIFY_GPU_UPLOAD");
            std::cout<<"Imported Extrusion context, exact Cancel packet, camera and transaction checks passed\n";
            return 0;
        }
        const auto check_preview=[&] {
            check(dialog(),"Imported Body editor missing");
            auto pending=original;auto graph=pending.body_history;graph.update_body(dialog()->pending_value());pending.set_body_history(graph);
            const auto expected=pending.place_body_mesh(local,body_id);
            check(view->mesh().vertices.size()>=expected.vertices.size()&&view->mesh().triangles.size()>=expected.triangles.size(),"Imported Body disappeared or was reduced during placement");
            check(std::equal(expected.vertices.begin(),expected.vertices.end(),view->mesh().vertices.begin()),"Pending Body geometry does not follow the resolved placement");
            check(std::equal(expected.triangles.begin(),expected.triangles.end(),view->mesh().triangles.begin()),"Pending Body lost topology/triangle order");
        };
        const auto show_properties=[&] {
            auto* tree=window.findChild<QTreeWidget*>("documentTree");check(tree,"Import Tree missing");
            QTreeWidgetItem* row=nullptr;
            for(QTreeWidgetItemIterator it(tree);*it;++it)
                if((*it)->data(0,Qt::UserRole).toString().toStdString()==body_id){row=*it;break;}
            check(row,"Imported Body Tree row missing");window.show_tree_item_properties(row);flush();
        };
        const auto open=[&]{show_properties();check_preview();};
        if(!qEnvironmentVariableIsSet("ZIMA_VERIFY_IMPORT_PLACEMENT_ONLY")) {
        open();const auto before_camera=view->camera_state();
        for(double offset:{3.,7.,-2.,0.}) {
            check(dialog()->set_inline_parameter_value("placement:reference_offset:0",offset),"Body offset not editable");flush();check_preview();
            check(view->camera_state()==before_camera,"Body placement changed the camera");
        }
        check(dialog()->set_inline_parameter_value("placement:rotation_z",15),"Body rotation not editable");flush();check_preview();
        window.grab().save(QString::fromStdWString((directory/"imported-body-moved.png").wstring()));
        std::cout<<"Imported Body complete preview passed"<<std::endl;
        dialog()->reject();flush();run("save");check(document::PartDocument::load(path).body_history==original.body_history,"Body Cancel changed persistence");
        std::cout<<"Imported Body Cancel/save passed"<<std::endl;
        open();auto* undo=window.findChild<QAction*>("undoAction");const bool before_undo=undo->isEnabled();
        dialog()->buttons()->button(QDialogButtonBox::Ok)->click();flush();check(!dialog()&&undo->isEnabled()==before_undo,"Unchanged Body OK added an Undo transaction");
        std::cout<<"Imported Body unchanged OK passed"<<std::endl;
        open();check(dialog()->set_inline_parameter_value("placement:reference_offset:0",5),"Body edit failed");flush();check_preview();
        const auto accepted=dialog()->pending_value().scope.placement;
        dialog()->buttons()->button(QDialogButtonBox::Ok)->click();flush();check(!dialog(),"Imported Body OK failed");run("save");
        check(document::PartDocument::load(path).body_history.find(body_id)->scope.placement==accepted,"Imported Body placement did not persist");
        std::cout<<"Imported Body changed OK/save passed"<<std::endl;
        run("undo");run("save");check(document::PartDocument::load(path).body_history.find(body_id)->scope.placement==owner->scope.placement,"Body placement Undo failed");
        run("redo");run("save");check(document::PartDocument::load(path).body_history.find(body_id)->scope.placement==accepted,"Body placement Redo failed");
        run("close");check(window.open_document_path(QString::fromStdWString(path.wstring())),"Cannot reopen moved import");flush();
        show_properties();check(dialog(),"Cannot edit reopened import");dialog()->reject();flush();
        }
        auto* tree=window.findChild<QTreeWidget*>("documentTree");
        bool created=false;
        QTimer::singleShot(0,&window,[&] {
            auto* menu=window.findChild<QMenu*>("partActivationMenu");
            if(!menu)return;
            auto* action=menu->findChild<QAction*>("createBodyFromPartAction");
            if(!action||!action->isEnabled()){menu->close();return;}
            created=true;menu->setActiveAction(action);
            QKeyEvent enter(QEvent::KeyPress,Qt::Key_Return,Qt::NoModifier);QApplication::sendEvent(menu,&enter);
        });
        tree->customContextMenuRequested(tree->visualItemRect(tree->topLevelItem(0)).center());flush();
        check(created&&dialog(),"Cannot create a Body for imported-face placement");
        for(int i=2;i>=0;--i) {
            auto* table=dialog()->findChild<QTableWidget*>("bodyReferenceTable");
            check(table&&table->cellWidget(i,0),"Imported-face placement section missing");
            auto* remove=table->cellWidget(i,0)->findChild<QPushButton*>();
            check(remove&&remove->isVisible(),"Cannot clear the initial Origin reference");remove->click();flush();
        }
        std::optional<QPointF> hit;viewer::ViewerCandidate offered;std::size_t offered_index{};
        for(int y=view->height()/2;y<view->height()&&!hit;y+=16)
            for(int x=view->width()/2;x<view->width()&&!hit;x+=16) {
                const auto candidates=view->selection_candidates_at(QPointF(x,y));
                const auto face=std::ranges::find_if(candidates,[&](const auto& value){return value.kind==viewer::CandidateKind::Face&&value.owner_id==found->id;});
                if(face!=candidates.end())
                    {hit=QPointF(x,y);offered=*face;offered_index=static_cast<std::size_t>(face-candidates.begin());}
            }
        check(hit.has_value(),"Imported face cannot be offered for Body placement");
        const auto start=std::chrono::steady_clock::now();
        for(int i=0;i<20;++i)check(view->selection_candidates_at(*hit).at(offered_index)==offered,"Warm placement changed the offered face");
        std::cout<<"Imported-face warm placement picker mean ms="<<std::chrono::duration<double,std::milli>(std::chrono::steady_clock::now()-start).count()/20<<std::endl;
        const auto filter=view->candidate_filter();std::size_t filter_calls=0;
        view->set_candidate_filter([&](const auto& candidate){++filter_calls;return filter(candidate);});
        static_cast<void>(view->selection_candidates_at(*hit));const auto one_pass=filter_calls;filter_calls=0;
        QMouseEvent move(QEvent::MouseMove,*hit,*hit,*hit,Qt::NoButton,Qt::NoButton,Qt::NoModifier);
        QApplication::sendEvent(view,&move);flush();
        check(filter_calls==one_pass,"Unchanged command hover performed a second complete picker pass");
        view->set_candidate_filter(filter);QApplication::sendEvent(view,&move);flush();
        for(std::size_t i=0;i<offered_index;++i) {
            QMouseEvent right_press(QEvent::MouseButtonPress,*hit,*hit,*hit,Qt::RightButton,Qt::RightButton,Qt::NoModifier);
            QMouseEvent right_release(QEvent::MouseButtonRelease,*hit,*hit,*hit,Qt::RightButton,Qt::NoButton,Qt::NoModifier);
            QApplication::sendEvent(view,&right_press);QApplication::sendEvent(view,&right_release);flush();
        }
        check(view->offered_candidate()==offered,"RMB did not cycle to the exact offered face");
        QEventLoop settled;QTimer::singleShot(700,&settled,&QEventLoop::quit);settled.exec();flush();
        view->repaint();flush();const auto cold=view->grabFramebuffer();
        const auto cold_camera=view->camera_state();view->repaint();flush();const auto warm=view->grabFramebuffer();
        if(warm!=cold) {
            cold.save(QString::fromStdWString((directory/"face-cold.png").wstring()));
            warm.save(QString::fromStdWString((directory/"face-warm.png").wstring()));
            std::cerr<<"Face frame camera equal="<<(cold_camera==view->camera_state())<<std::endl;
        }
        check(warm==cold,"Cached face highlight changed its displayed boundary");
        QMouseEvent press(QEvent::MouseButtonPress,*hit,*hit,*hit,Qt::LeftButton,Qt::LeftButton,Qt::NoModifier);
        QMouseEvent release(QEvent::MouseButtonRelease,*hit,*hit,*hit,Qt::LeftButton,Qt::NoButton,Qt::NoModifier);
        QApplication::sendEvent(view,&press);QApplication::sendEvent(view,&release);flush();
        const auto selected=dialog()->pending_value().scope.placement.references;
        check(std::ranges::any_of(selected,[&](const auto& ref){return ref.owner_id==offered.owner_id&&ref.semantic_key==offered.semantic_key&&ref.instance_path==offered.instance_path;}),"Imported-face click did not accept the offered identity");
        dialog()->reject();flush();
        std::cout<<"Imported-face GUI: warm identity, single hover pass, cached highlight and exact click identity passed\n";
        if(!qEnvironmentVariableIsSet("ZIMA_VERIFY_IMPORT_PLACEMENT_ONLY"))
            std::cout<<"Imported Body GUI: complete geometry, successive offsets/rotation, unchanged OK, Cancel, commit, Undo/Redo and reopen passed\n";
        return 0;
    }catch(const std::exception& error){std::cerr<<error.what()<<'\n';return 1;}
}
