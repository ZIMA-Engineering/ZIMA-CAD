// Included inside verify_edge_treatment_ui; shares its actual Tree/picker helpers.
        if(qEnvironmentVariableIsSet("ZIMA_VERIFY_DRILL_OPENING")) {
            const auto settle=[&] {QEventLoop loop;QTimer::singleShot(80,&loop,&QEventLoop::quit);loop.exec();flush();};
            for(const bool many:{false,true}) {
                auto source=test::drill_point_fixture();
                if(many)for(const double y:{-10.,10.})for(const double x:{-12.,-4.,4.,12.}) {
                    auto hole=test::circular_feature(source,1.2,21);
                    hole.placement.x=x;hole.placement.y=y;hole.combine_mode=document::CombineMode::Subtract;
                    source.insert_history_entry(document::PartHistoryKind::Feature,hole.id);source.history.push_back(hole);
                }
                source.resolve_constructions();kernel::OcctKernel kernel;
                const auto file=directory/(many?"drill-opening-many.prtz":"drill-opening-small.prtz");
                source.save(file,kernel.evaluate_history(source.kernel_operations()));
                check(window.open_document_path(QString::fromStdWString(file.wstring())),"Cannot open Drill benchmark");flush();
                Json faces=Json::array();
                for(std::size_t i=1;i<source.history.size();++i)faces.push_back({{"owner",source.history[i].id},{"key",test::profile_key(source,source.history[i],"z_min")}});
                const auto owner=run("drill_point.create",{{"faces",faces}}).at("container").get<std::string>();run("save");
                const auto open=[&] {
                    QTreeWidgetItem* row=nullptr;
                    for(QTreeWidgetItemIterator it(tree);*it;++it)if((*it)->data(0,Qt::UserRole).toString().toStdString()==owner&&(*it)->data(0,Qt::UserRole+3)=="part-container"){row=*it;break;}
                    check(row,"Missing Drill row");window.show_tree_item_properties(row);flush();return dialog();
                };
                view->set_standard_view(viewer::StandardView::Top);
                for(auto* animation:view->findChildren<QVariantAnimation*>())animation->setCurrentTime(animation->duration());flush();
                open();finish(false); // Establish the existing first-open camera framing.
                QCursor::setPos(window.mapToGlobal(QPoint(5,5)));settle();
                const auto state=run("documents"),saved=run("drill_point.get",{{"container",owner}});
                const auto camera=view->camera_state();
                auto* undo=window.findChild<QAction*>("undoAction");auto* redo=window.findChild<QAction*>("redoAction");
                check(undo&&redo,"Missing Undo/Redo");const bool undo_before=undo->isEnabled(),redo_before=redo->isEnabled();
                const auto packet=[&] {kernel::BodyResult body;body.mesh=view->mesh();return document::serialize_body_result(body,false);};
                const auto original=packet();
                // This opaque View has no displayed alpha; compare canonical RGB pixels.
                const auto hash=[&] {settle();const auto image=view->grabFramebuffer().convertToFormat(QImage::Format_RGB32);
                    return QCryptographicHash::hash(QByteArrayView(reinterpret_cast<const char*>(image.constBits()),image.sizeInBytes()),QCryptographicHash::Sha256).toHex();};
                QByteArray first;
                for(int trial=0;trial<6;++trial) {
                    QCursor::setPos(window.mapToGlobal(QPoint(5,5)));
                    const auto revision=view->base_mesh_revision();QElapsedTimer timer;timer.start();open();const auto elapsed=timer.nsecsElapsed()/1e6;
                    const auto publications=view->base_mesh_revision()-revision;
                    check(publications==1,"Drill opening published an unused scene");

                    const auto preview=hash();if(trial==0)first=preview;
                    check(first==preview,"Repeated Drill preview changed");finish(trial<3);
                    check(run("documents")==state&&undo->isEnabled()==undo_before&&redo->isEnabled()==redo_before,"Unchanged Drill changed document or Undo/Redo");
                    check(packet()==original&&view->camera_state()==camera,"Drill close did not restore geometry/camera");
                    std::cout<<"Drill opening holes="<<faces.size()<<" trial="<<trial<<" ms="<<elapsed<<" publications="<<publications<<" preview="<<preview.constData()<<" restored="<<hash().constData()<<std::endl;
                }
                auto* pending=open();auto* list=pending->findChild<QTableWidget*>("drillPointFaces");
                check(list&&list->rowCount()==int(faces.size()+1),"Drill lost bottom references");
                list->cellWidget(0,0)->findChild<QPushButton*>()->click();flush();
                check(pending->pending_value().drill_point.bottom_faces.size()==faces.size()-1,"Drill removal did not change pending references");
                view->set_standard_view(viewer::StandardView::Top);
                for(auto* animation:view->findChildren<QVariantAnimation*>())animation->setCurrentTime(animation->duration());view->fit_all();flush();
                pick(viewer::CandidateKind::Face,source.history[1].id,faces[0].at("key").get<std::string>());
                check(pending->pending_value().drill_point.bottom_faces.size()==faces.size(),"Drill picker did not restore bottom reference");
                pending->findChild<QDoubleSpinBox*>("drillPointIncludedAngle")->setValue(120);finish(false);
                check(run("drill_point.get",{{"container",owner}})==saved&&packet()==original,"Drill Cancel changed model");
                pending=open();pending->findChild<QDoubleSpinBox*>("drillPointIncludedAngle")->setValue(120);finish(true);
                check(run("drill_point.get",{{"container",owner}}).at("angle_degrees")==120,"Drill OK did not commit angle");
                run("undo");check(run("drill_point.get",{{"container",owner}})==saved,"Drill Undo failed");
                run("redo");run("save");std::vector<kernel::BodyResult> results;
                const auto loaded=document::PartDocument::load(file,&results);
                check(loaded.find_container(owner)->drill_point.included_angle_degrees==120,"Drill native save lost angle");
                const double extra=many?8*std::numbers::pi*(1.2*1.2*20+1.2*1.2*1.2/(3*std::tan(std::numbers::pi/3))):0;
                check(std::abs(results.back().volume-(test::drilled_block_volume(120)-extra))<1e-5,"Drill saved volume is incorrect");
                run("close",{{"discard",true}});
            }
            return 0;
        }
