static int verify_imported_body_interaction(QApplication& application,zima::app::AssemblyWorkspaceWindow& window,
        const std::filesystem::path& directory) {
    using namespace zima;
    try {
        const auto check=[](bool value,const char* message){if(!value)throw std::runtime_error(message);};
        const auto flush=[&]{application.processEvents();QCoreApplication::sendPostedEvents(nullptr,QEvent::DeferredDelete);application.processEvents();};
        window.resize(1100,850);window.show();flush();
        if(qEnvironmentVariableIsSet("ZIMA_VERIFY_ADJACENCY_REUSE"))qputenv("ZIMA_CPP_VERIFY_GPU_UPLOAD","1");
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
            if(qEnvironmentVariableIsSet("ZIMA_VERIFY_IMPORT_PLANE_VISIBILITY")){
                auto graph=imported.document.body_history;const auto active=graph.active_body_id();static_cast<void>(graph.create_body("Following empty Body"));graph.activate(active);imported.document.set_body_history(std::move(graph));
                imported.calculated=kernel.evaluate_history(imported.document.kernel_operations());
            }
            imported.document.save(path,imported.calculated);std::filesystem::remove(step);
        }
        std::vector<kernel::BodyResult> calculated;auto original=document::PartDocument::load(path,&calculated);
        const auto found=std::ranges::find_if(original.history,[](const auto& feature){return feature.feature_kind==document::FeatureKind::ImportedStep;});
        check(found!=original.history.end()&&!calculated.empty(),"Imported Body fixture is missing");
        check(!found->imported_step.topology.empty(),"Imported Body fixture lacks persisted STEP topology");
        const auto import_id=found->id;
        const auto* owner=original.body_owner_for_object(found->id);check(owner,"Import is outside its Body");
        const auto body_id=owner->scope.id;
        const auto branch=calculated.back().body_boundaries.find(body_id);
        check(branch!=calculated.back().body_boundaries.end()&&!branch->second.empty(),"Import lacks local calculated packet");
        auto local=std::move(branch->second.back().mesh);
        const auto source_triangle_indices=calculated.back().mesh.triangles.size();
        // The oracle needs the complete local viewer packet and Body graph,
        // not another copy of every archive/branch during measured interaction.
        calculated.clear();
        check(window.open_document_path(QString::fromStdWString(path.wstring())),"Cannot open imported preview fixture");flush();
        const auto run=[&](const char* command){QElapsedTimer timer;timer.start();const auto result=window.execute_console_command(QString::fromStdString(nlohmann::json{{"command",command},{"arguments",nlohmann::json::object()}}.dump()));if(!result.ok)std::cerr<<"Imported GUI command failed: "<<command<<": "<<result.code<<": "<<result.message<<std::endl;check(result.ok,"Import GUI console action failed");flush();std::cout<<"Imported GUI command="<<command<<" ms="<<timer.nsecsElapsed()/1e6<<std::endl;};
        const auto dialog=[&]()->app::BodyPropertiesDialog* {
            for(auto* value:window.findChildren<QDialog*>())if(value->isVisible())if(auto* typed=dynamic_cast<app::BodyPropertiesDialog*>(value))return typed;return nullptr;
        };
        auto* view=dynamic_cast<viewer::MeshView*>(window.findChild<QOpenGLWidget*>());check(view,"Import View missing");
        if(qEnvironmentVariableIsSet("ZIMA_VERIFY_IMPORT_NAVIGATION_ONLY")) {
            const auto state=window.execute_console_command("documents").data;
            const auto revision=view->base_mesh_revision();
            const auto camera=view->camera_state();
            QCursor::setPos(window.mapToGlobal(QPoint(5,5)));flush();
            std::size_t segments=0;
            for(const auto& edge:view->mesh().original_references.edges)
                if(edge.reference.owner_id==import_id&&edge.points.size()>1)segments+=edge.points.size()-1;
            std::cout<<"Import navigation original_edges="<<view->mesh().original_references.edges.size()<<" selected_segments="<<segments<<" triangles="<<source_triangle_indices/3<<std::endl;
            const auto capture=qEnvironmentVariable("ZIMA_VERIFY_IMPORT_NAVIGATION_CAPTURE");
            const auto compare=qEnvironmentVariable("ZIMA_VERIFY_IMPORT_NAVIGATION_COMPARE");
            for(const auto mode:{viewer::DisplayMode::Wire,viewer::DisplayMode::ShadedWithEdges,viewer::DisplayMode::Shaded}) {
                view->set_display_mode(mode);
                for(const bool selected:{false,true}) {
                    view->clear_selection();if(selected)view->confirm_container(import_id);
                    std::vector<double> samples;
                    for(int frame=-3;frame<20;++frame) {
                        auto next=camera;
                        const auto rotation=QQuaternion::fromAxisAndAngle(0,1,0,float(std::max(0,frame)*2))*QQuaternion(camera[0],camera[1],camera[2],camera[3]);
                        next[0]=rotation.scalar();next[1]=rotation.x();next[2]=rotation.y();next[3]=rotation.z();
                        view->set_camera_state(next);
                        QElapsedTimer timer;timer.start();const auto image=view->grabFramebuffer();
                        if(frame>=0)samples.push_back(timer.nsecsElapsed()/1e6);
                        check(!image.isNull(),"Navigation has no framebuffer");
                        if(frame==0||frame==19) {
                            const auto name=QString("/mode-%1-selected-%2-frame-%3.png").arg(int(mode)).arg(selected).arg(frame);
                            if(!capture.isEmpty()){QDir().mkpath(capture);check(image.save(capture+name),"Cannot capture navigation frame");}
                            if(!compare.isEmpty()) {
                                const auto a=image.convertToFormat(QImage::Format_RGBA8888),b=QImage(compare+name).convertToFormat(QImage::Format_RGBA8888);
                                check(a.size()==b.size(),"Navigation framebuffer changed size");
                                int maximum=0;std::size_t changed=0;
                                for(int y=0;y<a.height();++y)for(int x=0;x<a.width();++x){bool difference=false;for(int channel=0;channel<4;++channel){const auto delta=std::abs(int(a.constScanLine(y)[4*x+channel])-int(b.constScanLine(y)[4*x+channel]));maximum=std::max(maximum,delta);difference|=delta!=0;}changed+=difference;}
                                std::cout<<"Navigation frame "<<name.toStdString()<<" changed_pixels="<<changed<<" maximum_channel_difference="<<maximum<<std::endl;
                                // The existing retained/fresh GPU oracle uses
                                // the same one-channel rounding tolerance.
                                check(maximum<=1,"Navigation rendering differs from baseline beyond GPU channel rounding");
                            }
                        }
                    }
                    std::sort(samples.begin(),samples.end());
                    std::cout<<"Import navigation mode="<<int(mode)<<" selected="<<selected<<" viewport="<<view->width()<<'x'<<view->height()<<" samples="<<samples.size()<<" median_ms="<<samples[10]<<" p90_ms="<<samples[18]<<std::endl;
                    check(window.execute_console_command("documents").data==state&&view->base_mesh_revision()==revision,"Navigation changed the document or republished geometry");
                }
            }
            view->clear_selection();view->set_camera_state(camera);flush();
            return 0;
        }
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
                check(view->mesh().triangles.size()>=source_triangle_indices,"Extrusion hid imported context");
            };
            open();feature_dialog()->buttons()->button(QDialogButtonBox::Cancel)->click();flush();
            if(qEnvironmentVariableIsSet("ZIMA_VERIFY_IMPORT_PLANE_VISIBILITY")){
                const auto state=window.execute_console_command("documents").data;const auto camera=view->camera_state();open();
                for(int row=2;row>=0;--row){auto* table=feature_dialog()->findChild<QTableWidget*>("primitiveReferenceTable");auto* indicator=table?table->cellWidget(row,0):nullptr;auto* remove=indicator?indicator->findChild<QPushButton*>():nullptr;if(remove&&remove->isVisible())remove->click();flush();}
                auto* table=feature_dialog()->findChild<QTableWidget*>("primitiveReferenceTable");check(table,"Extrusion reference table missing");table->cellClicked(0,1);flush();
                std::optional<QPointF> hit;viewer::ViewerCandidate offered;std::size_t offered_index{};
                for(int y=16;y<view->height()&&!hit;y+=16)for(int x=16;x<view->width()&&!hit;x+=16){const auto candidates=view->selection_candidates_at({double(x),double(y)});const auto face=std::ranges::find_if(candidates,[&](const auto& candidate){return candidate.kind==viewer::CandidateKind::Face&&candidate.owner_id==import_id;});if(face!=candidates.end()){hit=QPointF(x,y);offered=*face;offered_index=static_cast<std::size_t>(face-candidates.begin());}}
                check(hit.has_value(),"No imported face can be entered for Extrusion");
                QMouseEvent move(QEvent::MouseMove,*hit,*hit,*hit,Qt::NoButton,Qt::NoButton,Qt::NoModifier);QApplication::sendEvent(view,&move);flush();
                for(std::size_t index=0;index<offered_index;++index){QMouseEvent press(QEvent::MouseButtonPress,*hit,*hit,*hit,Qt::RightButton,Qt::RightButton,Qt::NoModifier);QMouseEvent release(QEvent::MouseButtonRelease,*hit,*hit,*hit,Qt::RightButton,Qt::NoButton,Qt::NoModifier);QApplication::sendEvent(view,&press);QApplication::sendEvent(view,&release);flush();}
                check(view->offered_candidate()==offered,"Extrusion did not offer the exact imported face");
                QMouseEvent press(QEvent::MouseButtonPress,*hit,*hit,*hit,Qt::LeftButton,Qt::LeftButton,Qt::NoModifier);QMouseEvent release(QEvent::MouseButtonRelease,*hit,*hit,*hit,Qt::LeftButton,Qt::NoButton,Qt::NoModifier);QApplication::sendEvent(view,&press);QApplication::sendEvent(view,&release);flush();
                const auto pending=feature_dialog()->pending_value();check(!pending.placement.references.empty()&&pending.placement.references.front().owner_id==offered.owner_id,"Extrusion lost the clicked source identity");
                const auto capture=qEnvironmentVariable("ZIMA_VERIFY_IMPORT_PLANE_CAPTURE");if(!capture.isEmpty()){view->repaint();flush();check(window.grab().save(capture),"Cannot capture imported Extrusion plane");}
                const auto plane=std::ranges::find_if(view->mesh().edges,[&](const auto& edge){return edge.reference.owner_id==pending.feature_id&&edge.reference.semantic_key=="border";});
                if(plane==view->mesh().edges.end()){
                    std::cerr<<"Missing work plane: selected="<<offered.owner_id<<':'<<offered.semantic_key<<" pending="<<pending.id<<" feature="<<pending.feature_id<<" refs="<<pending.placement.references.size()<<std::endl;
                    for(const auto& reference:pending.placement.references)std::cerr<<"Plane reference "<<reference.owner_id<<':'<<reference.semantic_key<<" role="<<reference.orientation_role<<" orientation="<<reference.orientation_drives_rotation<<std::endl;
                    for(const auto& edge:view->mesh().edges)if(edge.reference.semantic_key=="border")std::cerr<<"Other plane border "<<edge.reference.owner_id<<std::endl;
                    for(auto* label:feature_dialog()->findChildren<QLabel*>())if(!label->text().isEmpty())std::cerr<<"Plane properties label "<<label->text().toStdString()<<std::endl;
                }
                check(plane!=view->mesh().edges.end()&&plane->points.size()==5,"Extrusion work plane has no visible border carrier");
                const auto plane_reference=plane->reference;
                const auto initial_border=plane->points;
                const auto& geometry=view->mesh().original_references;
                const auto source_plane=std::ranges::find_if(geometry.triangle_references,[&](const auto& value){return value.owner_id==offered.owner_id&&value.semantic_key==offered.semantic_key&&value.instance_path==offered.instance_path;});
                check(source_plane!=geometry.triangle_references.end(),"Clicked imported face has no persisted source geometry");
                const auto triangle=static_cast<std::size_t>(source_plane-geometry.triangle_references.begin());
                const auto first=geometry.vertices.at(geometry.triangles.at(triangle*3)),second=geometry.vertices.at(geometry.triangles.at(triangle*3+1)),third=geometry.vertices.at(geometry.triangles.at(triangle*3+2));
                const kernel::Vec3 a{second.x-first.x,second.y-first.y,second.z-first.z},b{third.x-first.x,third.y-first.y,third.z-first.z};
                kernel::Vec3 normal{a.y*b.z-a.z*b.y,a.z*b.x-a.x*b.z,a.x*b.y-a.y*b.x};const auto length=std::hypot(std::hypot(normal.x,normal.y),normal.z);check(length>1e-12,"Imported support plane is degenerate");normal={normal.x/length,normal.y/length,normal.z/length};
                kernel::Vec3 equation_origin=first;
                if(source_plane->surface){
                    const auto& surface=*source_plane->surface;
                    if(surface.kind==kernel::SurfaceGeometry::Kind::Plane){equation_origin=surface.origin;normal=surface.axis;}
                    else {
                        equation_origin={(initial_border[0].x+initial_border[2].x)/2,(initial_border[0].y+initial_border[2].y)/2,(initial_border[0].z+initial_border[2].z)/2};
                        const kernel::Vec3 delta{equation_origin.x-surface.origin.x,equation_origin.y-surface.origin.y,equation_origin.z-surface.origin.z};const auto axial=delta.x*surface.axis.x+delta.y*surface.axis.y+delta.z*surface.axis.z;
                        normal={delta.x-axial*surface.axis.x,delta.y-axial*surface.axis.y,delta.z-axial*surface.axis.z};const auto radial=std::hypot(std::hypot(normal.x,normal.y),normal.z);
                        const auto slope=surface.kind==kernel::SurfaceGeometry::Kind::Cone?std::tan(surface.semi_angle):0.;check(radial>1e-12&&std::abs(radial-surface.radius-axial*slope)<1e-7,"Tangent work-plane origin left its exact analytic support");
                        normal={normal.x/radial-slope*surface.axis.x,normal.y/radial-slope*surface.axis.y,normal.z/radial-slope*surface.axis.z};const auto norm=std::hypot(std::hypot(normal.x,normal.y),normal.z);normal={normal.x/norm,normal.y/norm,normal.z/norm};
                    }
                }
                const kernel::Vec3 u{initial_border[1].x-initial_border[0].x,initial_border[1].y-initial_border[0].y,initial_border[1].z-initial_border[0].z},v{initial_border[2].x-initial_border[1].x,initial_border[2].y-initial_border[1].y,initial_border[2].z-initial_border[1].z};
                const auto orientation=(u.y*v.z-u.z*v.y)*normal.x+(u.z*v.x-u.x*v.z)*normal.y+(u.x*v.y-u.y*v.x)*normal.z;const auto offset_sign=orientation<0?-1.:1.;
                for(const double offset:{0.,3.,-2.,0.}){
                    feature_dialog()->set_profile_offset_and_forward_length(offset,10);flush();const auto border=std::ranges::find_if(view->mesh().edges,[&](const auto& edge){return edge.reference==plane_reference;});check(border!=view->mesh().edges.end()&&border->points.size()==5,"Successive offset edits lost the work plane identity");
                    for(const auto& point:border->points){const auto distance=(point.x-equation_origin.x)*normal.x+(point.y-equation_origin.y)*normal.y+(point.z-equation_origin.z)*normal.z;check(std::abs(distance-offset_sign*offset)<1e-7,"Work plane border does not satisfy its independent signed source equation");}
                    check(window.execute_console_command("documents").data==state,"Transient plane edit changed the document");
                }
                std::cout<<"Imported Extrusion plane reference="<<offered.semantic_key<<" border=";for(const auto& point:initial_border)std::cout<<'['<<point.x<<','<<point.y<<','<<point.z<<']';std::cout<<std::endl;
                feature_dialog()->buttons()->button(QDialogButtonBox::Cancel)->click();flush();check(view->camera_state()==camera&&window.execute_console_command("documents").data==state,"Plane inspection changed document or camera");return 0;
            }
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
            qputenv("ZIMA_CPP_VERIFY_GPU_UPLOAD","1");view->setProperty("forceAdjacencyBuild",true);view->setProperty("forceMeshUpload",true);
            view->repaint();flush();const auto fresh=view->grabFramebuffer();
            view->setProperty("forceAdjacencyBuild",false);
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
            if(qEnvironmentVariableIsSet("ZIMA_VERIFY_ADJACENCY_REUSE")) {
                view->repaint();flush();const auto retained=view->grabFramebuffer();
                const auto digest=view->property("gpuMeshUploadDigest").toByteArray();
                check(!digest.isEmpty(),"Body adjacency digest is missing");
                const auto camera=view->camera_state();
                view->setProperty("forceAdjacencyBuild",true);view->setProperty("forceMeshUpload",true);
                view->repaint();flush();const auto fresh=view->grabFramebuffer();
                check(view->property("gpuMeshUploadDigest").toByteArray()==digest,"Body reused adjacency differs from complete GPU preparation");
                view->setProperty("forceAdjacencyBuild",false);
                const auto before_reuse=view->property("gpuAdjacencyReuseCount").toULongLong();
                view->setProperty("forceMeshUpload",true);view->repaint();flush();
                check(view->property("gpuAdjacencyReuseCount").toULongLong()>before_reuse,"Verified adjacency did not reuse its identical actual inputs");
                check(view->property("gpuMeshUploadDigest").toByteArray()==digest&&view->camera_state()==camera,
                    "Body adjacency reuse changed exact GPU data or camera");
                const auto a=retained.convertToFormat(QImage::Format_RGBA8888),b=fresh.convertToFormat(QImage::Format_RGBA8888);
                check(a.size()==b.size(),"Body adjacency comparison dimensions differ");
                int maximum=0;
                for(int y=0;y<a.height();++y)for(int x=0;x<a.width()*4;++x)
                    maximum=std::max(maximum,std::abs(int(a.constScanLine(y)[x])-int(b.constScanLine(y)[x])));
                check(maximum<=1,"Body adjacency reuse changed the rendered geometry");
                std::cout<<"Body adjacency verified full GPU bytes; reuse="<<view->property("gpuAdjacencyReuseCount").toULongLong()
                    <<" build="<<view->property("gpuAdjacencyBuildCount").toULongLong()<<" maximum_channel_difference="<<maximum<<std::endl;
            }
        };
        const auto check_committed_geometry=[&](const document::Placement& placement) {
            auto expected_document=original;auto graph=expected_document.body_history;
            auto body=*graph.find(body_id);body.scope.placement=placement;graph.update_body(body);
            expected_document.set_body_history(std::move(graph));
            const auto expected=expected_document.place_body_mesh(local,body_id);
            check(view->mesh().vertices.size()>=expected.vertices.size()&&view->mesh().triangles.size()>=expected.triangles.size(),
                "Committed imported Body lost geometry");
            check(std::equal(expected.vertices.begin(),expected.vertices.end(),view->mesh().vertices.begin())&&
                std::equal(expected.triangles.begin(),expected.triangles.end(),view->mesh().triangles.begin())&&
                std::equal(expected.triangle_references.begin(),expected.triangle_references.end(),view->mesh().triangle_references.begin()),
                "Committed imported Body world geometry or face identity differs from independent placement");
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
            QElapsedTimer preview_timer;preview_timer.start();
            check(dialog()->set_inline_parameter_value("placement:reference_offset:0",offset),"Body offset not editable");flush();
            std::cout<<"Imported Body offset="<<offset<<" preview_ms="<<preview_timer.nsecsElapsed()/1e6<<std::endl;check_preview();
            check(view->camera_state()==before_camera,"Body placement changed the camera");
        }
        check(dialog()->set_inline_parameter_value("placement:rotation_z",15),"Body rotation not editable");flush();check_preview();
        window.grab().save(QString::fromStdWString((directory/"imported-body-moved.png").wstring()));
        std::cout<<"Imported Body complete preview passed"<<std::endl;
        if(qEnvironmentVariableIsSet("ZIMA_VERIFY_IMPORT_PREVIEW_ONLY")) {
            QElapsedTimer cancel_timer;cancel_timer.start();dialog()->reject();flush();
            std::cout<<"Imported Body preview Cancel ms="<<cancel_timer.nsecsElapsed()/1e6<<std::endl;return 0;
        }
        dialog()->reject();flush();check_committed_geometry(owner->scope.placement);run("save");check(document::PartDocument::load(path).body_history==original.body_history,"Body Cancel changed persistence");
        std::cout<<"Imported Body Cancel/save passed"<<std::endl;
        open();auto* undo=window.findChild<QAction*>("undoAction");const bool before_undo=undo->isEnabled();
        dialog()->buttons()->button(QDialogButtonBox::Ok)->click();flush();check(!dialog()&&undo->isEnabled()==before_undo,"Unchanged Body OK added an Undo transaction");
        std::cout<<"Imported Body unchanged OK passed"<<std::endl;
        open();check(dialog()->set_inline_parameter_value("placement:reference_offset:0",5),"Body edit failed");flush();check_preview();
        const auto accepted=dialog()->pending_value().scope.placement;
        QElapsedTimer commit_timer;commit_timer.start();
        dialog()->buttons()->button(QDialogButtonBox::Ok)->click();flush();check(!dialog(),"Imported Body OK failed");
        std::cout<<"Imported Body changed OK ms="<<commit_timer.nsecsElapsed()/1e6<<std::endl;check_committed_geometry(accepted);run("save");
        check(document::PartDocument::load(path).body_history.find(body_id)->scope.placement==accepted,"Imported Body placement did not persist");
        std::cout<<"Imported Body changed OK/save passed"<<std::endl;
        run("undo");check_committed_geometry(owner->scope.placement);run("save");check(document::PartDocument::load(path).body_history.find(body_id)->scope.placement==owner->scope.placement,"Body placement Undo failed");
        std::cout<<"Imported Body Undo/save passed"<<std::endl;
        run("redo");check_committed_geometry(accepted);run("save");check(document::PartDocument::load(path).body_history.find(body_id)->scope.placement==accepted,"Body placement Redo failed");
        std::cout<<"Imported Body Redo/save passed"<<std::endl;
        run("close");check(window.open_document_path(QString::fromStdWString(path.wstring())),"Cannot reopen moved import");flush();
        check_committed_geometry(accepted);show_properties();check(dialog(),"Cannot edit reopened import");check_preview();dialog()->reject();flush();
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
        // Closing/reopening a document may replace its widget. Always exercise
        // the current visible View, rather than an earlier tab's widget.
        for(auto* widget:window.findChildren<QOpenGLWidget*>())if(widget->isVisible())
            if(auto* current=dynamic_cast<viewer::MeshView*>(widget))view=current;
        for(int i=2;i>=0;--i) {
            auto* table=dialog()->findChild<QTableWidget*>("bodyReferenceTable");
            check(table&&table->cellWidget(i,0),"Imported-face placement section missing");
            auto* remove=table->cellWidget(i,0)->findChild<QPushButton*>();
            check(remove&&remove->isVisible(),"Cannot clear the initial Origin reference");remove->click();flush();
        }
        std::optional<QPointF> hit;viewer::ViewerCandidate offered;std::size_t offered_index{};
        std::cout<<"Import placement View visible="<<view->isVisible()<<" triangles="<<view->mesh().triangles.size()/3<<std::endl;
        window.grab().save(QString::fromStdWString((directory/"import-placement-input.png").wstring()));
        const auto pick_point=qEnvironmentVariable("ZIMA_VERIFY_IMPORT_PICK_POINT").split(',');
        if(pick_point.size()==2) {
            const QPointF point(pick_point[0].toDouble(),pick_point[1].toDouble());
            // Re-arm the input synchronously, without pumping pointer events:
            // clearing default references above may already warm the picker.
            auto* table=dialog()->findChild<QTableWidget*>("bodyReferenceTable");
            const auto arming=std::chrono::steady_clock::now();
            table->cellClicked(0,1);
            std::cout<<"Imported-face reference entry ms="<<std::chrono::duration<double,std::milli>(std::chrono::steady_clock::now()-arming).count()<<std::endl;
            const auto before=std::chrono::steady_clock::now();
            const auto candidates=view->selection_candidates_at(point);
            std::cout<<"Imported-face first picker ms="<<std::chrono::duration<double,std::milli>(std::chrono::steady_clock::now()-before).count()<<std::endl;
            const auto face=std::ranges::find_if(candidates,[&](const auto& value){return value.kind==viewer::CandidateKind::Face&&value.owner_id==import_id;});
            check(face!=candidates.end(),"Fixed import pick point did not offer a face");
            hit=point;offered=*face;offered_index=static_cast<std::size_t>(face-candidates.begin());
        }
        // A large model and the internal Properties window can occupy different
        // quadrants after save/reopen. Search the complete visible viewport;
        // the common picker itself excludes points covered by the dialog.
        for(int y=16;y<view->height()&&!hit;y+=16)
            for(int x=16;x<view->width()&&!hit;x+=16) {
                const auto candidates=view->selection_candidates_at(QPointF(x,y));
                const auto face=std::ranges::find_if(candidates,[&](const auto& value){return value.kind==viewer::CandidateKind::Face&&value.owner_id==import_id;});
                if(face!=candidates.end())
                    {hit=QPointF(x,y);offered=*face;offered_index=static_cast<std::size_t>(face-candidates.begin());}
            }
        if(!hit) {
            std::set<std::string> owners;
            for(const auto& face:view->mesh().triangle_references)owners.insert(face.owner_id);
            std::cerr<<"Import placement expected="<<import_id<<" triangles="<<view->mesh().triangles.size()/3
                <<" face owners="<<nlohmann::json(owners).dump()<<std::endl;
            window.grab().save(QString::fromStdWString((directory/"import-placement-missing.png").wstring()));
        }
        check(hit.has_value(),"Imported face cannot be offered for Body placement");
        std::cout<<"Imported-face pick point="<<hit->x()<<","<<hit->y()<<" identity="<<offered.semantic_key<<std::endl;
        const auto expected_candidates=view->selection_candidates_at(*hit);
        std::cout<<"Imported-face ordered candidate count="<<expected_candidates.size()<<std::endl;
        const auto start=std::chrono::steady_clock::now();
        for(int i=0;i<20;++i)check(view->selection_candidates_at(*hit)==expected_candidates,"Warm placement changed the ordered candidate list");
        std::cout<<"Imported-face warm placement picker mean ms="<<std::chrono::duration<double,std::milli>(std::chrono::steady_clock::now()-start).count()/20<<std::endl;
        const auto revision=view->base_mesh_revision();
        view->set_mesh(view->mesh(),false);
        check(view->base_mesh_revision()!=revision,"Republished scene retained its validation revision");
        check(view->selection_candidates_at(*hit)==expected_candidates,"Fresh placement validation changed the ordered candidate list");
        std::cout<<"Imported-face scene revision: fresh and cached candidate lists match\n";
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
        const auto a=cold.convertToFormat(QImage::Format_RGBA8888),b=warm.convertToFormat(QImage::Format_RGBA8888);
        check(a.size()==b.size(),"Cached face highlight changed framebuffer dimensions");
        int maximum=0;
        for(int y=0;y<a.height();++y)for(int x=0;x<a.width()*4;++x)
            maximum=std::max(maximum,std::abs(int(a.constScanLine(y)[x])-int(b.constScanLine(y)[x])));
        // Desktop GL may round antialiased channels by one 8-bit level on
        // successive identical frames. Candidate identity remains exact above.
        check(maximum<=1,"Cached face highlight changed its displayed boundary");
        QMouseEvent press(QEvent::MouseButtonPress,*hit,*hit,*hit,Qt::LeftButton,Qt::LeftButton,Qt::NoModifier);
        QMouseEvent release(QEvent::MouseButtonRelease,*hit,*hit,*hit,Qt::LeftButton,Qt::NoButton,Qt::NoModifier);
        const auto confirming=std::chrono::steady_clock::now();
        const auto confirmation_revision=view->base_mesh_revision();
        QApplication::sendEvent(view,&press);QApplication::sendEvent(view,&release);flush();
        std::cout<<"Imported-face confirmation ms="<<std::chrono::duration<double,std::milli>(std::chrono::steady_clock::now()-confirming).count()<<std::endl;
        const auto publications=view->base_mesh_revision()-confirmation_revision;
        std::cout<<"Imported-face confirmation scene publications="<<publications<<std::endl;
        check(publications==1,"Body reference confirmation must publish exactly one complete scene");
        const auto selected=dialog()->pending_value().scope.placement.references;
        check(std::ranges::any_of(selected,[&](const auto& ref){return ref.owner_id==offered.owner_id&&ref.semantic_key==offered.semantic_key&&ref.instance_path==offered.instance_path;}),"Imported-face click did not accept the offered identity");
        dialog()->reject();flush();
        std::cout<<"Imported-face GUI: warm identity, single hover pass, cached highlight and exact click identity passed\n";
        if(!qEnvironmentVariableIsSet("ZIMA_VERIFY_IMPORT_PLACEMENT_ONLY"))
            std::cout<<"Imported Body GUI: complete geometry, successive offsets/rotation, unchanged OK, Cancel, commit, Undo/Redo and reopen passed\n";
        return 0;
    }catch(const std::exception& error){std::cerr<<error.what()<<'\n';return 1;}
}
