// Shares the existing edge-treatment GUI's real common-picker helpers.
        if(qEnvironmentVariableIsSet("ZIMA_VERIFY_TREATMENT_SELECTION_ONLY")) {
            auto doc=document::PartDocument::create_default();
            auto box=test::rectangular_feature(doc,{20,20,20});doc.history={box};
            auto sketch_feature=document::PartDocument::create_sketch_container();
            auto sketch=sketcher::Sketch::create_default();sketch.owner_container_id=sketch_feature.id;
            static_cast<void>(sketch.add_segment(32,0,42,10));doc.history.push_back(sketch_feature);doc.sketches.push_back(sketch);
            auto curve=document::PartDocument::create_construction(document::ConstructionKind::Curve3D);
            for(const auto position:{kernel::Vec3{0,32,0},kernel::Vec3{10,42,0}}) {
                auto point=document::create_owned_point(curve.id);point.origin=position;curve.curve_points.push_back(point);
            }
            doc.constructions.push_back(curve);
            document::BodyHistoryGraph graph;const auto body=graph.create_body("Treatment input");
            graph.insert({document::PartHistoryKind::Feature,box.id});graph.insert({document::PartHistoryKind::Feature,sketch_feature.id});
            graph.insert({document::PartHistoryKind::Construction,curve.id});doc.set_body_history(graph);doc.resolve_constructions();
            kernel::OcctKernel kernel;const auto file=directory/"treatment-selection.prtz";
            doc.save(file,kernel.evaluate_history(doc.kernel_operations()));
            check(window.open_document_path(QString::fromStdString(file.string())),"Treatment input cannot open");flush();
            run("body.activate",{{"body",body}});view->fit_all();flush();
            bool saw_curve=false,saw_sketch=false;
            for(int y=10;y<view->height()-10;y+=8)for(int x=10;x<view->width()-10;x+=8)
                for(const auto& candidate:view->selection_candidates_at(QPointF(x,y))) {
                    saw_curve|=candidate.owner_id==curve.id;
                    saw_sketch|=candidate.owner_id==sketch.id||candidate.owner_id==sketch_feature.id;
                }
            check(saw_curve&&saw_sketch,"Selection fixture does not expose its visible 3D Curve and Sketch");
            const auto verify=[&](viewer::CandidateKind kind) {
                const auto filter=view->candidate_filter();check(bool(filter),"Treatment lost its candidate filter");
                for(const auto& edge:view->mesh().edges)if(edge.reference.owner_id==curve.id||edge.reference.owner_id==sketch.id) {
                    viewer::ViewerCandidate wrong{viewer::CandidateKind::Edge,0,0,edge.reference.owner_id,edge.reference.semantic_key,{},viewer::CandidateGeometry::Display};
                    check(!filter(wrong),"Treatment filter accepts a construction or Sketch edge");
                }
                bool found=false;
                for(int y=10;y<view->height()-10;y+=8)for(int x=10;x<view->width()-10;x+=8) {
                    const auto candidates=view->selection_candidates_at(QPointF(x,y));
                    for(const auto& candidate:candidates) {
                        check(candidate.kind==kind||candidate.kind==viewer::CandidateKind::Dimension,"Treatment offers a non-input candidate kind");
                        check(candidate.owner_id!=curve.id&&candidate.owner_id!=sketch.id&&candidate.owner_id!=sketch_feature.id,"Treatment offers 3D Curve or Sketch");
                        if(candidate.kind==kind&&candidate.owner_id==box.id)found=true;
                    }
                }
                check(found,"Treatment stopped offering real body topology");
            };
            for(const auto command:{"filletAction","chamferAction","shellAction"}) {
                const bool shell=std::string(command)=="shellAction";
                auto* action=window.findChild<QAction*>(command);check(action&&action->isEnabled(),"Treatment action unavailable");
                action->trigger();flush();verify(shell?viewer::CandidateKind::Face:viewer::CandidateKind::Edge);
                // Selection/preview refresh is the previously broken Part path.
                const auto chosen=pick(shell?viewer::CandidateKind::Face:viewer::CandidateKind::Edge,box.id,{});
                verify(shell?viewer::CandidateKind::Face:viewer::CandidateKind::Edge);
                auto* pending=dialog();
                if(shell)check(pending->pending_value().shell.removed_faces==std::vector<kernel::FaceReference>{{chosen.owner_id,chosen.semantic_key,{}}},"Shell click did not confirm its offered face");
                else check(!pending->pending_value().edge_treatment.routes.empty()&&pending->pending_value().edge_treatment.routes.front().front().owner_id==box.id,"Treatment click did not confirm its offered edge");
                finish(false);run("save");
                check(document::PartDocument::load(file).history.size()==2,"Treatment Cancel changed the document");
            }
            std::cout<<"Fillet/Chamfer/Shell GUI: visible Curve3D and Sketch excluded, real body hover/click retained, selection refresh and Cancel passed\n";return 0;
        }
