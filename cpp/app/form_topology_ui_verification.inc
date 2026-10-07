// FORM regression: use real common-picker hover and mouse confirmation.
if(qEnvironmentVariableIsSet("ZIMA_VERIFY_FORM_TOPOLOGY")) {
    const auto diagnostic_source=qEnvironmentVariable("ZIMA_FORM_RENDER_SOURCE");
    std::vector<kernel::BodyResult> calculated;
    const auto source=document::PartDocument::load(diagnostic_source.isEmpty()?
        std::filesystem::path("cpp/tests/fixtures/boolean/form.prtz"):
        std::filesystem::path(diagnostic_source.toStdString()),&calculated);
    kernel::OcctKernel kernel;
    if(diagnostic_source.isEmpty())calculated=kernel.evaluate_history(source.kernel_operations());
    const auto path=directory/(stem+"-form.prtz");source.save(path,calculated);
    run("open",{{"path",path.generic_string()}});
    if(qEnvironmentVariableIsSet("ZIMA_FORM_RENDER_DIAGNOSTIC")) {
        const auto highlighted_owner=qEnvironmentVariable("ZIMA_FORM_RENDER_OWNER");
        if(!diagnostic_source.isEmpty()) {
            const auto& mesh=calculated.back().mesh;
            const auto shading=viewer::shaded_triangle_vertices(mesh);
            using ShadingKey=std::tuple<long long,long long,long long,std::string,std::string,std::string>;
            std::map<ShadingKey,std::array<float,3>> normals;
            for(std::size_t t=0;t<mesh.triangles.size()/3;++t) {
                const auto& face=mesh.triangle_references.at(t);if(!face.valid())continue;
                for(std::size_t corner=0;corner<3;++corner) {
                    const auto& point=mesh.vertices.at(mesh.triangles[t*3+corner]);
                    const ShadingKey key{std::llround(point.x*1e7),std::llround(point.y*1e7),std::llround(point.z*1e7),
                        face.owner_id,face.semantic_key,face.instance_path};
                    const auto i=(t*3+corner)*6+3;
                    const std::array value{shading[i],shading[i+1],shading[i+2]};
                    const auto [entry,inserted]=normals.emplace(key,value);
                    for(std::size_t c=0;c<3;++c)check(std::abs(entry->second[c]-value[c])<1e-5,
                        "Current FORM has triangle-dependent normals inside the same face");
                }
            }
            std::cout<<"Current FORM same-face vertex fan normals: "<<normals.size()<<" consistent vertices\n";
            auto clean=calculated.back().mesh;
            if(highlighted_owner.isEmpty())clean.original_references={};
            std::erase_if(clean.edges,[](const auto& edge){return edge.overlay||edge.construction;});
            clean.points.clear();clean.axes.clear();view->set_mesh(std::move(clean));
            for(const auto kind:{viewer::ReferenceVisibility::Origins,viewer::ReferenceVisibility::Planes,
                viewer::ReferenceVisibility::Sketches,viewer::ReferenceVisibility::Dimensions})view->set_reference_visibility(kind,false);
        }
        view->fit_all();auto camera=view->camera_state();
        const auto rotation=QQuaternion::fromEulerAngles(-65,0,35);
        camera[0]=rotation.scalar();camera[1]=rotation.x();camera[2]=rotation.y();camera[3]=rotation.z();
        view->set_camera_state(camera);
        if(!diagnostic_source.isEmpty()) {
            flush();const std::string oldest_fillet="8a6e127b-744b-4301-a1d6-0cd78733b1e8";
            const auto offered=pick(viewer::CandidateKind::Container,oldest_fillet,{});
            check(offered.geometry==viewer::CandidateGeometry::Display,
                "Current FORM Fillet hover used intermediate geometry");
            check(tree->currentItem()&&tree->currentItem()->data(0,Qt::UserRole).toString().toStdString()==oldest_fillet,
                "Current FORM Fillet click selected the cutter in the Tree");
            check(!view->edge_treatment_boundary_edge_indices(oldest_fillet).empty(),
                "Current FORM Fillet has no actual visible inspection boundary");
            click(QPointF(3,3));flush();
            view->set_feature_hover_edges({});view->set_feature_selected_edges({});
            view->set_feature_hover_edge_indices({});view->set_feature_selected_edge_indices({});
            view->set_container_inspection({});view->set_container_inspection_wire({});
            std::cout<<"Current FORM Fillet common hover/click, Tree and visible boundary passed\n";
        }
        if(!highlighted_owner.isEmpty()) {
            pick(viewer::CandidateKind::Container,highlighted_owner.toStdString(),{});flush();
        }
        const auto focus=qEnvironmentVariable("ZIMA_FORM_RENDER_FOCUS").split(',');
        if(focus.size()==3) {
            const kernel::Vec3 p{focus[0].toDouble(),focus[1].toDouble(),focus[2].toDouble()};
            view->fit_points({{p.x-4,p.y-4,p.z-4},{p.x+4,p.y+4,p.z+4}});flush();
        }
        for(const auto mode:{viewer::DisplayMode::ShadedWithEdges,viewer::DisplayMode::Wire,viewer::DisplayMode::Shaded}) {
            view->set_display_mode(mode);flush();
            const auto output=directory/(mode==viewer::DisplayMode::Wire?"form-wire.png":
                mode==viewer::DisplayMode::Shaded?"form-smooth.png":"form-shaded.png");
            check(view->grabFramebuffer().save(QString::fromStdString(output.string())),"Cannot save FORM rendering diagnostic");
            std::cout<<"FORM rendering diagnostic: "<<output.generic_string()<<std::endl;
        }
        view->set_display_mode(viewer::DisplayMode::ShadedWithEdges);
        const auto drawing_directory=qEnvironmentVariable("ZIMA_FORM_DRAWING_DIAGNOSTIC");
        if(!drawing_directory.isEmpty())for(const auto name:{"FORM-before-fix.drwz","FORM.drwz"}) {
            const auto drawing_path=std::filesystem::path(drawing_directory.toStdString())/name;
            if(!std::filesystem::exists(drawing_path))continue;
            const auto drawing_document=drawing::DrawingDocument::load(drawing_path);
            std::size_t index=0;
            for(const auto& sheet:drawing_document.sheets)for(const auto& projected:sheet.views) {
                QRectF bounds;bool first=true;
                const auto include=[&](drawing::Point2 p) {
                    if(first){bounds=QRectF(p.x,p.y,0,0);first=false;}
                    else {bounds.setLeft(std::min(bounds.left(),p.x));bounds.setRight(std::max(bounds.right(),p.x));
                        bounds.setTop(std::min(bounds.top(),p.y));bounds.setBottom(std::max(bounds.bottom(),p.y));}
                };
                for(const auto& edge:projected.projected_edges)for(const auto p:edge.points)include(p);
                for(const auto& triangle:projected.projected_triangles)for(const auto p:triangle.points)include(p);
                bounds.adjust(-2,-2,2,2);
                const auto rendered=drawing_depth_view().render(projected,bounds,10,10,Qt::black,QColor("#666666"),2,1);
                QImage image(rendered.size(),QImage::Format_ARGB32);image.fill(Qt::white);
                {QPainter painter(&image);painter.drawImage(0,0,rendered);}
                const auto output=directory/(std::string(name)+"-view-"+std::to_string(++index)+".png");
                check(!image.isNull()&&image.save(QString::fromStdString(output.string())),"Cannot save FORM Drawing rendering diagnostic");
                std::cout<<"FORM Drawing rendering diagnostic: "<<output.generic_string()<<std::endl;
                const auto dark=drawing_depth_view().render(projected,bounds,10,10,Qt::white,QColor("#666666"),5,2.5);
                QImage dark_image(dark.size(),QImage::Format_ARGB32);dark_image.fill(QColor("#252c34"));
                {QPainter painter(&dark_image);painter.drawImage(0,0,dark);}
                check(dark_image.save(QString::fromStdString((directory/(std::string(name)+"-view-"+
                    std::to_string(index)+"-dark.png")).string())),"Cannot save dark FORM drawing diagnostic");
            }
        }
        if(!diagnostic_source.isEmpty()) {
            std::cout<<"Current FORM cached rendering: "<<source.history.size()<<" operations\n";
            return 0;
        }
    }
    const auto frame=[&](bool back=false) {
        view->fit_all();auto camera=view->camera_state();
        const auto rotation=QQuaternion::fromAxisAndAngle(1,0,0,-90)*
            QQuaternion::fromAxisAndAngle(0,0,1,back?0:180);
        camera[0]=rotation.scalar();camera[1]=rotation.x();camera[2]=rotation.y();camera[3]=rotation.z();
        view->set_camera_state(camera);flush();
    };
    std::vector<kernel::EdgeReference> vertical;
    std::map<std::pair<std::string,std::string>,kernel::FaceReference> front;
    for(const auto& edge:calculated.back().mesh.edges)
        if(edge.reference.owner_id=="01a1128b1f7a74f1b43389f983258e0d"&&edge.measured_length&&
            std::abs(*edge.measured_length-10)<1e-6)vertical.push_back(edge.reference);
    for(const auto& face:calculated.back().mesh.triangle_references)
        if(face.semantic_key.starts_with("boolean:subtract:split-face:from:")&&face.surface&&
            face.surface->kind==kernel::SurfaceGeometry::Kind::Plane&&
            std::abs(face.surface->origin.y)<1e-6&&std::abs(face.surface->axis.y)>.99)
            front.emplace(std::pair{face.owner_id,face.semantic_key},face);
    check(vertical.size()==2&&front.size()==2,"FORM GUI fixture lost its two sides");
    const auto select=[&](viewer::CandidateKind kind,const std::string& owner,const std::string& key) {
        frame();
        try { return pick(kind,owner,key); }
        catch(const std::runtime_error&) { frame(true);return pick(kind,owner,key); }
    };
    const auto is_front=[](const auto& face) {
        return face.surface&&face.surface->kind==kernel::SurfaceGeometry::Kind::Plane&&
            std::abs(face.surface->origin.y)<1e-6&&std::abs(face.surface->axis.y)>.99;
    };
    const auto oldest=std::ranges::find_if(calculated.front().mesh.triangle_references,is_front);
    check(oldest!=calculated.front().mesh.triangle_references.end(),"FORM original front reference is missing");
    const auto merged=std::ranges::find_if(calculated[2].mesh.triangle_references,is_front);
    check(merged!=calculated[2].mesh.triangle_references.end()&&*merged==*oldest,
        "FORM coplanar union replaced its oldest original reference");
    for(const bool commit:{false,true}) {
        auto* pending=edit("01a1128b1f7a74f1b43389f983258e0d");
        auto* references=pending->findChild<QTableWidget*>("primitiveReferenceTable");
        check(references,"FORM cutter placement fields are missing");
        QMetaObject::invokeMethod(references,"cellClicked",Qt::DirectConnection,Q_ARG(int,0),Q_ARG(int,1));flush();
        const auto offered=select(viewer::CandidateKind::Face,oldest->owner_id,oldest->semantic_key);
        check(offered.geometry==viewer::CandidateGeometry::Display,
            "FORM placement offered an intermediate source face instead of the visible boundary");
        const auto value=pending->pending_value();
        check(!value.placement.references.empty(),"FORM placement click did not enter a reference");
        const auto& reference=value.placement.references[0];
        check(reference.owner_id==oldest->owner_id&&reference.semantic_key==oldest->semantic_key,
            "FORM placement selected an operational descendant instead of its oldest original face");
        finish(commit);run("save");
        const auto loaded=document::PartDocument::load(path);
        if(commit) {
            const auto& reference=loaded.history.back().placement.references[0];
            check(reference.owner_id==oldest->owner_id&&reference.semantic_key==oldest->semantic_key,
                "FORM oldest placement reference did not survive native save/reopen");
            const auto revision=run("documents")[0].at("revision");
            run("regenerate");
            if(run("documents")[0].at("revision")!=revision)run("undo");
            run("undo");run("redo");run("undo");
        } else check(loaded.history.back().placement==source.history.back().placement,
            "FORM placement Cancel changed the document");
    }
    // Creation sees the final cut body, so both disconnected front fragments
    // must be offered as displayed geometry and stored as the same oldest face.
    for(const auto& [identity,face]:front) {
        auto* create=window.findChild<QAction*>("featurePrototypeAction");
        check(create&&create->isEnabled(),"FORM placement creation unavailable");
        create->trigger();flush();auto* pending=dialog();
        auto* references=pending->findChild<QTableWidget*>("primitiveReferenceTable");
        check(references,"FORM new primitive placement fields missing");
        QMetaObject::invokeMethod(references,"cellClicked",Qt::DirectConnection,Q_ARG(int,0),Q_ARG(int,1));flush();
        const auto offered=select(viewer::CandidateKind::Face,identity.first,identity.second);
        check(offered.geometry==viewer::CandidateGeometry::Display,"FORM final fragment offered as intermediate geometry");
        const auto value=pending->pending_value();
        check(!value.placement.references.empty()&&value.placement.references.front().owner_id==oldest->owner_id&&
            value.placement.references.front().semantic_key==oldest->semantic_key,
            "FORM final fragment did not map to its oldest original placement identity");
        pending->set_reference_inspected(0,true);flush();
        view->clear_selection();flush();
        const auto confirmed_image=view->grabFramebuffer();
        view->set_constraint_reference_highlights({},{});view->set_inspected_faces({offered});flush();
        check(confirmed_image==view->grabFramebuffer(),
            "FORM click inspection differs from the exact hovered visible fragment");
        view->set_inspected_faces({});
        finish(false);
    }
    auto* fillet=window.findChild<QAction*>("filletAction");
    auto* surfaces=window.findChild<QAction*>("surfaceFromSolidAction");
    check(fillet&&fillet->isEnabled()&&surfaces&&surfaces->isEnabled(),"FORM modeling actions are disabled");
    for(const auto& edge:vertical)for(const bool commit:{false,true}) {
        std::cout<<"FORM GUI Fillet commit="<<commit<<std::endl;
        fillet->trigger();flush();QPointer<PrimitivePropertiesDialog> pending=dialog();
        select(viewer::CandidateKind::Edge,edge.owner_id,edge.semantic_key);
        check(pending->pending_value().edge_treatment.routes.size()==1&&
            pending->pending_value().edge_treatment.routes[0][0]==edge,"FORM mouse selected a different edge");
        pending->findChild<QDoubleSpinBox*>("edgeTreatmentPrimary")->setValue(.1);finish(commit);
        if(pending&&pending->isVisible()) {
            window.grab().save(QString::fromStdString((directory/"form-fillet-confirm-failure.png").string()));
            throw std::runtime_error("FORM Fillet confirmation did not close Properties");
        }
        run("save");std::vector<kernel::BodyResult> saved;
        auto loaded=document::PartDocument::load(path,&saved);
        check(loaded.history.size()==(commit?5:4),"FORM Fillet GUI OK/Cancel changed the wrong history");
        if(commit) {
            check(loaded.history.back().edge_treatment.routes[0][0]==edge,"FORM GUI saved the wrong Fillet edge");
            const auto id=loaded.history.back().id;
            edit(id);finish(false);run("undo");run("redo");run("undo");
        }
    }
    for(const auto& [id,face]:front)for(const bool commit:{false,true}) {
        std::cout<<"FORM GUI Surface commit="<<commit<<std::endl;
        surfaces->trigger();flush();QPointer<PrimitivePropertiesDialog> pending=dialog();
        select(viewer::CandidateKind::Face,face.owner_id,face.semantic_key);
        check(pending->pending_value().shell.removed_faces==std::vector<kernel::FaceReference>{face},
            "FORM mouse selected a different front face");
        finish(commit);
        if(pending&&pending->isVisible()) {
            const auto failure=directory/"form-surface-confirm-failure.png";
            window.grab().save(QString::fromStdString(failure.string()));
            throw std::runtime_error("FORM surface confirmation did not close Properties: "+failure.string());
        }
        run("save");std::vector<kernel::BodyResult> saved;
        const auto loaded=document::PartDocument::load(path,&saved);
        check(loaded.history.size()==(commit?5:4),"FORM surface GUI OK/Cancel changed the wrong history");
        if(commit) {
            check(loaded.history.back().shell.removed_faces==std::vector<kernel::FaceReference>{face}&&
                std::abs(saved.back().volume)<1e-7,"FORM GUI did not persist the selected surface opening");
            run("undo");run("redo");run("undo");
        }
    }
    run("close",{{"discard",true}});
    std::cout<<"FORM GUI: both circular-cut front edges and both front faces, mouse selection, OK/Cancel, Properties, native persistence and Undo/Redo passed\n";
    return 0;
}
