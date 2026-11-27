void verify_sweep2d_first_plane(QApplication& application,QWidget& owner) {
    using namespace zima;
    for(const unsigned count:{1U,2U})for(const bool reverse:{false,true}) {
        bool committed=false;unsigned edited=0;
        app::Sweep2DDialog dialog(document::PartDocument::create_sweep2d_container(),[&](auto){committed=true;},&owner);
        dialog.setAttribute(Qt::WA_DeleteOnClose,false);dialog.show();application.processEvents();
        auto guide=sketcher::Sketch::from_serialized(dialog.pending.sweep2d.path_sketch);
        for(unsigned k=0;k<count;++k) {
            const unsigned i=reverse?count-1-k:k;
            if(reverse)static_cast<void>(guide.add_segment(7+12*(i+1),11+16*(i+1),7+12*i,11+16*i));
            else static_cast<void>(guide.add_segment(7+12*i,11+16*i,7+12*(i+1),11+16*(i+1)));
        }
        dialog.set_sketch(0,guide);application.processEvents();
        auto* rows=dialog.findChild<QTableWidget*>("sweep2dProfiles");
        require(rows&&rows->rowCount()==2*count,"Offset path did not offer endpoint station rows");
        auto* reverse_button=dialog.findChild<QPushButton*>("sweep2dReversePath");
        require(reverse_button&&reverse_button->isEnabled(),"Path reversal button missing");
        const auto original_route=document::PartDocument::sweep2d_route(dialog.pending);
        for(unsigned flip=0;flip<4;++flip) {
            reverse_button->click();application.processEvents();
            const auto route=document::PartDocument::sweep2d_route(dialog.pending);
            require(route.stations.front().point_id==(flip%2?original_route.stations.front().point_id:original_route.stations.back().point_id)&&
                reverse_button->isChecked()==dialog.pending.sweep2d.path_reversed,
                "Path reversal button did not switch the initial station");
        }
        dialog.edit_sketch=[&](unsigned stage){edited=stage;};
        auto* last=dialog.findChild<QPushButton*>(QString("sweep2dStationSketch%1").arg(2*count-1));
        require(last&&last->isEnabled(),"Offset path last station editor unavailable");last->click();
        require(edited>0,"Offset station click did not request its Sketch editor");
        const auto identity=dialog.pending.sweep2d.profiles.back().point_id;
        for(auto& point:guide.points){point.x+=3;point.y-=5;}
        dialog.set_sketch(0,guide);application.processEvents();
        require(rows->rowCount()==2*count&&dialog.pending.sweep2d.profiles.back().point_id==identity,
            "Moving the path discarded its station editor");
        dialog.reject();application.processEvents();require(!committed,"Cancel committed free path properties");
    }
    std::cout<<"2D Sweep free-path UI: one/two offset segments, both directions, station buttons and Cancel passed\n";
    for(const kernel::Vec3 normal: {kernel::Vec3{1,0,0},kernel::Vec3{0,1,0},
            kernel::Vec3{0,0,1},kernel::Vec3{.6,0,.8}}) {
        kernel::ViewerReferenceGeometry geometry;
        auto surface=std::make_shared<kernel::SurfaceGeometry>();
        surface->kind=kernel::SurfaceGeometry::Kind::Plane;surface->origin={5,7,9};
        surface->axis=normal;surface->radial=normal.y==1?kernel::Vec3{1,0,0}:kernel::Vec3{0,1,0};
        geometry.triangle_references.push_back({"support","plane",{},surface});
        app::Sweep2DDialog dialog(document::PartDocument::create_sweep2d_container(),[](auto){},&owner);
        dialog.setAttribute(Qt::WA_DeleteOnClose,false);dialog.show();application.processEvents();
        dialog.seed_path_plane({{},dialog.pending.container_origin.id,"origin:plane:xy"});
        dialog.changed=[&] {
            require(dialog.resolve_pending_placement(geometry),"2D Sweep plane placement failed");
            document::PartDocument::resolve_sweep2d_planes(dialog.pending,geometry);
        };
        document::ConstructionReference reference;reference.owner_id="support";
        reference.semantic_key="plane";reference.supports_offset=true;
        // Hover and confirmation must use the identical command authoring rule.
        const auto candidate=dialog.prepare_reference_candidate(0,reference);
        require(candidate.orientation_role=="top","2D Sweep hover did not prepare local XY");
        require(dialog.set_reference(0,reference,QStringLiteral("Support")),"2D Sweep first plane was rejected");
        for(const double offset:{0.,3.,-2.,5.}) {
            if(offset!=0.)require(dialog.set_inline_parameter_value("placement:reference_offset:0",offset),"2D Sweep offset editor unavailable");
            const auto path=sketcher::Sketch::from_serialized(dialog.pending.sweep2d.path_sketch);
            const auto delta=kernel::dimension_sub(path.resolved_origin,surface->origin);
            require(std::abs(kernel::dimension_dot(delta,normal)-offset)<1e-8,"2D Sweep path lost its plane offset");
            require(kernel::dimension_dot(path.resolved_normal,normal)>1-1e-8,"2D Sweep first plane is not path XY");
            require(dialog.pending.sweep2d.path_plane->semantic_key=="origin:plane:xy","2D Sweep changed its local plane name");
        }
        dialog.reject();application.processEvents();
    }
    std::cout<<"2D Sweep first-plane XY: three principal planes, inclined plane and successive signed offsets passed\n";
}
