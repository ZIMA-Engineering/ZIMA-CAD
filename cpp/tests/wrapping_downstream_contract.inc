// A later extrusion must not reorient a cylinder-attached predecessor when
// intermediate viewer packets have been compacted by native save/reopen.
static void wrapping_downstream_contract() {
    for(bool subtract:{false,true}) {
        workspace::Workspace live;kernel::OcctKernel engine;
        auto part=document::PartDocument::create_default();const auto id=part.document_id;
        document::BodyHistoryGraph graph;static_cast<void>(graph.create_body("Downstream Wrap"));part.set_body_history(std::move(graph));
        live.add_part(part);live.activate(id);
        auto outline=sketcher::Sketch::create_default();outline.plane_auto=false;
        static_cast<void>(outline.add_circle(0,0,30));
        auto base=document::PartDocument::create_feature_container(outline.id);
        base.feature.sides[0].length=40;base.feature.sides[1].length=15;
        workspace::commit_profile(live,engine,id,base,workspace::ProfileEditMode::Create,outline);
        auto* state=live.open_part(id);kernel::FaceReference cylinder,cap;
        for(const auto& face:state->session.calculated_boundaries().back().mesh.triangle_references)if(face.surface) {
            if(face.surface->kind==kernel::SurfaceGeometry::Kind::Cylinder)cylinder=face;
            if(face.surface->kind==kernel::SurfaceGeometry::Kind::Plane&&std::abs(face.surface->origin.z-40)<1e-6)cap=face;
        }
        require(cylinder.valid()&&cap.valid(),"Downstream fixture has no cylinder/end cap");
        auto sketch=sketcher::Sketch::create_default();sketch.plane_auto=false;
        static_cast<void>(sketch.add_rectangle(-3,-4,3,4));
        auto wrap=document::PartDocument::create_feature_container(sketch.id);
        wrap.feature.type=document::FeatureType::Wrapping;wrap.feature.wrapping.output=document::WrappingOutput::Relief;
        wrap.feature.wrapping.target.emplace();wrap.feature.wrapping.target->reference=cylinder;wrap.feature.wrapping.height=1.25;
        wrap.combine_mode=subtract?document::CombineMode::Subtract:document::CombineMode::Add;
        wrap.placement.x=30*std::cos(.371);wrap.placement.y=30*std::sin(.371);wrap.placement.z=20;
        wrap.placement.rotation_y=wrap.placement.absolute_rotation_y=90;
        wrap.placement.rotation_z=wrap.placement.absolute_rotation_z=.371*180/std::numbers::pi;
        wrap.placement.references={{{},cylinder.owner_id,cylinder.semantic_key,0,true,"top",true}};
        workspace::commit_profile(live,engine,id,wrap,workspace::ProfileEditMode::Create,sketch);
        valid(state->session.calculated_boundaries());
        const auto before=state->session.document();
        const auto expected=*before.find_container(wrap.id);
        const auto profile=*std::ranges::find(before.sketches,sketch.id,&sketcher::Sketch::id);
        const auto volume=volume_of(state->session.calculated_boundaries().back());
        std::vector<kernel::BodyResult> cache;
        auto reopened=document::PartDocument::from_serialized(before.serialized(state->session.calculated_boundaries()),&cache);
        workspace::Workspace resumed;resumed.add_part(reopened,cache);resumed.activate(id);state=resumed.open_part(id);
        const auto verify=[&] {
            const auto& current=state->session.document();valid(state->session.calculated_boundaries());
            const auto& actual=*current.find_container(wrap.id);
            const auto close=[](double a,double b){return std::abs(a-b)<1e-8;};
            const auto& a=actual.placement;const auto& b=expected.placement;
            require(a.reference_valid&&a.references==b.references&&actual.feature==expected.feature,
                "A downstream extrusion changed the preceding Wrap definition or reference identity");
            require(close(a.x,b.x)&&close(a.y,b.y)&&close(a.z,b.z)&&close(a.rotation_x,b.rotation_x)&&
                close(a.rotation_y,b.rotation_y)&&close(a.rotation_z,b.rotation_z),
                "A downstream extrusion moved or rotated the preceding Wrap");
            const auto& s=*std::ranges::find(current.sketches,sketch.id,&sketcher::Sketch::id);
            const auto near=[&](auto x,auto y){return close(x.x,y.x)&&close(x.y,y.y)&&close(x.z,y.z);};
            require(s.owner_container_id==profile.owner_container_id&&near(s.resolved_origin,profile.resolved_origin)&&
                near(s.resolved_x_axis,profile.resolved_x_axis)&&near(s.resolved_normal,profile.resolved_normal),
                "A downstream extrusion changed the owned Wrap Sketch frame");
            const auto calls=kernel::body_calculation_count.load();const auto revision=state->session.revision();
            const auto statuses=workspace::cached_history_placement_reference_status(current,state->session.calculated_boundaries());
            workspace::ReferenceIndex fallback;
            for(const auto& feature:current.history)for(const auto& ref:feature.placement.references)
                require(statuses.contains(feature.id)&&statuses.at(feature.id).missing(ref,fallback).empty(),
                    "A valid input reference is missing after downstream material changed");
            require(calls==kernel::body_calculation_count.load()&&revision==state->session.revision(),
                "Read-only history reference diagnostics calculated or changed the model");
        };
        auto extension=sketcher::Sketch::create_default();extension.plane_auto=false;
        static_cast<void>(extension.add_circle(0,0,30));
        auto tail=document::PartDocument::create_feature_container(extension.id);tail.feature.sides[0].length=25;
        tail.placement.z=40;tail.placement.references={{{},cap.owner_id,cap.semantic_key,0,true,"top",true}};
        workspace::commit_profile(resumed,engine,id,tail,workspace::ProfileEditMode::Create,extension);
        verify();
        require(std::abs(volume_of(state->session.calculated_boundaries().back())-volume-25*900*std::numbers::pi)<1e-5,
            "Downstream extrusion changed preceding material");
        for(int repeat=0;repeat<3;++repeat) {workspace::regenerate_part(resumed,engine,id);verify();}
        require(state->session.undo()&&state->session.redo(),"Downstream Undo/Redo unavailable");verify();
        cache.clear();auto final=document::PartDocument::from_serialized(state->session.document().serialized(state->session.calculated_boundaries()),&cache);
        workspace::Workspace final_session;final_session.add_part(final,cache);final_session.activate(id);state=final_session.open_part(id);
        workspace::regenerate_part(final_session,engine,id);verify();
        auto broken=state->session.document();auto* feature=broken.find_container(tail.id);
        feature->placement.references.front().semantic_key="face:missing";
        workspace::ReferenceIndex stale;stale.add(feature->placement.references.front());
        const auto statuses=workspace::cached_history_placement_reference_status(broken,state->session.calculated_boundaries());
        require(!statuses.at(tail.id).missing(feature->placement.references.front(),stale).empty(),
            "A missing history input fell back to stale final geometry");
    }
    std::cout<<"PASS downstream extrusion: signed Wrap, compact reopen, unchanged frames/identities/material, three regenerations, Undo/Redo and exact read-only reference diagnostics\n";
}
