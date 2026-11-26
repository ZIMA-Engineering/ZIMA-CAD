void cylindrical_angle_model() {
    using V=kernel::Vec3;
    const auto add=[](V a,V b,double k=1.){return V{a.x+k*b.x,a.y+k*b.y,a.z+k*b.z};};
    const auto dot=[](V a,V b){return a.x*b.x+a.y*b.y+a.z*b.z;};
    const auto cross=[](V a,V b){return V{a.y*b.z-a.z*b.y,a.z*b.x-a.x*b.z,a.x*b.y-a.y*b.x};};
    for (int direction=0;direction<3;++direction) for(bool reversed:{false,true})
    for(bool flip:{false,true}) for(double offset:{-2.,0.,3.}) {
        const V axis=direction==0?V{0,0,1}:direction==1?V{0,1,0}:V{.6,0,.8};
        const V zero=direction==0?V{1,0,0}:direction==1?V{0,0,1}:V{.8,0,-.6};
        const V tangent=cross(axis,zero), center{11,-7,19};
        auto cylinder=std::make_shared<kernel::SurfaceGeometry>();
        cylinder->kind=kernel::SurfaceGeometry::Kind::Cylinder;cylinder->axis=axis;
        cylinder->origin=center;cylinder->radial=zero;cylinder->radius=10.;cylinder->reversed=reversed;
        auto zero_plane=std::make_shared<kernel::SurfaceGeometry>();
        zero_plane->kind=kernel::SurfaceGeometry::Kind::Plane;
        zero_plane->origin={400,500,600}; // Zero direction is independent of plane distance.
        zero_plane->axis=add(zero,axis,.5);const double n=std::sqrt(dot(zero_plane->axis,zero_plane->axis));
        zero_plane->axis={zero_plane->axis.x/n,zero_plane->axis.y/n,zero_plane->axis.z/n};
        zero_plane->radial=tangent;
        auto stop=std::make_shared<kernel::SurfaceGeometry>();
        stop->kind=kernel::SurfaceGeometry::Kind::Plane;stop->axis=axis;stop->origin=center;stop->radial=zero;
        kernel::ViewerReferenceGeometry geometry;
        geometry.triangle_references={{"cylinder","face",{},cylinder},{"zero","plane",{},zero_plane},{"stop","plane",{},stop}};
        auto c=plane("cylinder");c.semantic_key="face";c.offset=offset;
        c.orientation_drives_rotation=true;c.orientation_role="front";
        auto a=plane("zero");a.use_angle=true;a.flip=flip;a.orientation_role="position_only";
        auto s=plane("stop");s.offset=13.;s.orientation_role="position_only";
        document::Placement p;p.references={c,s,a};p.rotation_offset_y=17.;
        for (double degrees:{0.,45.,90.,180.,270.,-90.,359.,360.,-720.,25.}) {
            p.references[2].offset=degrees;
            require(document::resolve_placement(p,geometry),"Cylindrical angle failed to resolve");
            const double angle=degrees*std::acos(-1.)/180.;
            const double radius=10.+(reversed?-offset:offset),sign=flip?-1.:1.;
            const auto expected=add(add(add(center,axis,13.),zero,sign*radius*std::cos(angle)),
                tangent,sign*radius*std::sin(angle));
            near(p.x,expected.x,"Angle X equation failed");near(p.y,expected.y,"Angle Y equation failed");near(p.z,expected.z,"Angle Z equation failed");
            require(document::point_constraint_remaining_dof(p.references,geometry,expected)==0,"Angle/axial plane DOF incorrect");
            require(document::placement_solution_branches(p,geometry).empty(),"Directed angle acquired ambiguous roots");
            const auto measured=document::measure_placement_reference_value(p.references[2],p.references,geometry,expected);
            require(measured.has_value(),"Angle measurement missing");
            near(std::sin(*measured*std::acos(-1.)/180.),std::sin(angle),"Angle measurement changed sign");
            const auto dimensions=document::container_placement_dimensions("placed",p,geometry);
            const auto dimension=std::ranges::find_if(dimensions,[](const auto& d){return d.reference.semantic_key=="parameter:placement:reference_offset:2";});
            require(dimension!=dimensions.end() && dimension->kind==kernel::ViewerDimensionKind::Angular &&
                dimension->value_lock_key=="placement:reference_offset:2","Angle dimension lost kind/key/lock");
            near(dimension->sweep_degrees,degrees-90.,"Directed dimension changed the plane-relative sweep");
            near(dimension->value,degrees-90.,"Dimension and field values disagree");
            near(dot(add(dimension->line_first,dimension->witness_first,-1.),zero_plane->axis),0.,
                "First angular witness does not follow the selected plane");
            const auto u=add(dimension->line_first,dimension->witness_first,-1.);
            const auto v=cross(axis,u);
            const double shown=(degrees-90.)*std::acos(-1.)/180.;
            const auto end=add(add(dimension->witness_first,u,std::cos(shown)),v,std::sin(shown));
            near(end.x,dimension->line_second.x,"Displayed arc endpoint X disagrees with placement");
            near(end.y,dimension->line_second.y,"Displayed arc endpoint Y disagrees with placement");
            near(end.z,dimension->line_second.z,"Displayed arc endpoint Z disagrees with placement");
            near(document::placement_dimension_stored_value(p.references,dimension->reference.semantic_key,dimension->value),degrees,
                "Inline angular conversion changed the stored sweep");
            auto reopened=nlohmann::json(p).get<document::Placement>();
            require(reopened==p && document::resolve_placement(reopened,geometry),"Angle native reference round trip failed");
            near(reopened.x,p.x,"Angle regeneration drifted");
        }
        const auto valid=p;
        require(document::orientation_constraint_remaining_dof({a},geometry,false,{p.x,p.y,p.z})==3,
            "Unmarked angular reference constrained orientation");
        p.references.erase(p.references.begin()+1);
        require(document::resolve_placement(p,geometry),"Angle without axial plane failed");
        require(document::point_constraint_remaining_dof(p.references,geometry,{p.x,p.y,p.z})==1,"Angle constrained axial travel");
        p.x+=axis.x*8.;p.y+=axis.y*8.;p.z+=axis.z*8.;
        require(document::resolve_placement(p,geometry),"Free axial movement failed");
        near(dot(add(V{p.x,p.y,p.z},center,-1.),axis),21.,"Free axial movement was discarded");
        for(int invalid=0;invalid<4;++invalid) {
            auto broken=valid;auto bad=geometry;
            if(invalid==0)bad.triangle_references.erase(bad.triangle_references.begin());
            if(invalid==1) {auto parallel=std::make_shared<kernel::SurfaceGeometry>(*zero_plane);parallel->axis=axis;bad.triangle_references[1].surface=parallel;}
            if(invalid==2)broken.references[0].offset=reversed?10.:-10.;
            if(invalid==3)broken.references[0].use_axis=true;
            require(!document::resolve_placement(broken,bad),"Invalid angular support was accepted");
            near(broken.x,valid.x,"Lost reference moved X");near(broken.y,valid.y,"Lost reference moved Y");near(broken.z,valid.z,"Lost reference moved Z");
            near(broken.rotation_x,valid.rotation_x,"Lost reference rotated container");
        }
        auto ordered=valid;
        std::reverse(ordered.references.begin(),ordered.references.end());
        require(document::resolve_placement(ordered,geometry),"Reordered angular references failed");
        near(ordered.x,valid.x,"Reference order changed angular placement");
    }
}

void cylindrical_angle_widget() {
    {
        auto ordinary=plane("zero");ordinary.orientation_drives_rotation=true;ordinary.orientation_role="front";
        auto paired=ordinary;paired.orientation_only=true;
        std::vector<document::ConstructionReference> position{ordinary},orientation{paired};
        std::array<bool,3> locks{};auto angular=ordinary;angular.use_angle=true;
        const auto result=document::assign_placement_reference({position,orientation,locks},true,0,angular);
        require(result.error==document::PlacementReferenceError::None&&orientation.front().owner_id.empty(),
            "Changing the same plane to Angle retained its automatic orientation twin");
    }
    auto cylinder=std::make_shared<kernel::SurfaceGeometry>();
    cylinder->kind=kernel::SurfaceGeometry::Kind::Cylinder;cylinder->axis={0,0,1};
    cylinder->radial={1,0,0};cylinder->radius=10.;
    auto zero=std::make_shared<kernel::SurfaceGeometry>();
    zero->kind=kernel::SurfaceGeometry::Kind::Plane;zero->axis={1,0,0};zero->radial={0,1,0};
    auto replacement=std::make_shared<kernel::SurfaceGeometry>(*zero);replacement->axis={0,1,0};replacement->radial={1,0,0};
    auto stop=std::make_shared<kernel::SurfaceGeometry>(*zero);stop->axis={0,0,1};
    kernel::ViewerReferenceGeometry geometry;
    geometry.triangle_references={{"cylinder","face",{},cylinder},{"zero","plane",{},zero},
        {"replacement","plane",{},replacement},{"stop","plane",{},stop}};
    auto c=plane("cylinder");c.semantic_key="face";c.orientation_drives_rotation=true;c.orientation_role="front";
    auto s=plane("stop");s.offset=12.;s.orientation_role="position_only";
    document::Placement resolved;resolved.x=10.;resolved.z=12.;resolved.references={c,s};
    QWidget owner;auto* layout=new QVBoxLayout(&owner);ui::ContainerPlacementSection section(&owner,layout,true);
    section.set_surface_resolver([&](const auto& ref){return document::placement_surface(ref,geometry);});
    section.set_branch_geometry_resolver([&]() -> const auto& {return geometry;});
    section.initialize_from_references(resolved.references,{});section.initialize_numeric_values(resolved);
    section.set_remaining_translation_dof(1);
    int updates{};
    section.set_changed_callback([&] {
        ++updates;auto pending=section.numeric_placement();pending.references=section.combined_references(3);
        require(document::resolve_placement(pending,geometry),"Angular row edit could not resolve");
        resolved=pending;
        section.set_translation_constraint_state(document::point_constraint_state(pending.references,geometry,
            {pending.x,pending.y,pending.z}),{pending.x,pending.y,pending.z});
    });
    const auto type=[&] {return qobject_cast<QComboBox*>(section.reference_table()->cellWidget(2,2));};
    const auto field=[&] {return qobject_cast<QDoubleSpinBox*>(section.reference_table()->cellWidget(2,3));};
    require(type() && type()->findData(2)>=0,"Empty row does not offer Angle before picking its plane");
    type()->setCurrentIndex(type()->findData(2));
    auto a=section.prepare_reference(2,plane("zero"));
    require(a.use_angle && !a.orientation_drives_rotation,"Angle candidate acquired orientation role");
    require(section.set_reference(2,a,QStringLiteral("Zero plane"),nullptr),"Cannot assign angular zero plane");
    require(field() && field()->suffix().contains(QString::fromUtf8("°")),"Angular row still displays a length unit");
    const auto* orientation=section.reference_table()->cellWidget(2,5)->findChild<QCheckBox*>();
    require(orientation && !orientation->isEnabled(),"Angle row offers unrelated FRONT orientation");
    field()->setValue(90.);near(resolved.x,-10.,"Widget angle X incorrect");near(resolved.y,0.,"Widget angle Y incorrect");
    field()->setValue(-45.);near(resolved.x,std::sqrt(50.),"Successive widget angle X incorrect");
    field()->setValue(90.);
    auto* lock=field()->findChild<QToolButton*>("numericValueLockButton");
    require(lock,"Angular value lacks shared numeric lock");lock->click();
    auto next=section.prepare_reference(2,plane("replacement"));
    require(next.use_angle && next.offset_locked,"Replacement dropped angular mode or lock");
    near(next.offset,90.,"Locked angular replacement captured a length instead of an angle");
    require(section.set_reference(2,next,QStringLiteral("Replacement plane"),nullptr),"Angular replacement failed");
    near(resolved.x,-10.,"Locked angular replacement moved the origin");
    require(field()->isReadOnly(),"Angular numeric lock is not enforced");
    field()->findChild<QToolButton*>("numericValueLockButton")->click();
    field()->setValue(30.);near(resolved.x,-std::sqrt(75.),"Unlocked angle no longer edits the origin");
    const auto before=resolved;
    type()->setCurrentIndex(type()->findData(0));
    require(!section.references()[2].use_angle && field()->suffix().contains("mm"),"Face mode retained degrees");
    near(resolved.x,before.x,"Changing Angle to Face moved the valid origin");
    type()->setCurrentIndex(type()->findData(2));
    near(resolved.x,before.x,"Changing Face to Angle moved the valid origin");
    require(updates>=8,"Angular row edits did not publish their pending state");
}
