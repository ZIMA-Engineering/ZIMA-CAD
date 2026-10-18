// Coupled diameter and chamfer-angle edits must keep the source contact.
void verify_symmetric_wall_matrix() {
    using namespace zima::sketcher;
    for (bool horizontal : {false,true}) for (double side : {-1.,1.})
    for (bool reverse : {false,true}) for (int source_mode : {0,1,2})
    for (bool locked : {false,true}) for(bool native_axis:{false,true}) {
        auto s=Sketch::create_default();
        const auto xy=[&](double x,double y){return horizontal?std::array{y,x}:std::array{x,y};};
        const auto segment=[&](double x,double y,double xx,double yy){const auto a=xy(x,y),b=xy(xx,yy);return s.add_segment(a[0],a[1],b[0],b[1]);};
        const auto wall=segment(side*7,0,side*7,-26);
        const auto wall_end=s.segments.front().second_point_id;
        const auto chamfer=reverse?segment(side*7,-26,side*5,-32):segment(side*5,-32,side*7,-26);
        const auto contact=reverse?s.segments.back().second_point_id:s.segments.back().first_point_id;
        std::string axis=horizontal?"sketch_axis:x":"sketch_axis:y";
        if(native_axis) {
            const auto a=xy(0,0),b=xy(0,-32);
            axis=s.add_segment(a[0],a[1],b[0],b[1],1e-6,true);
            s.set_point_fixed(s.segments.back().first_point_id,true);
            static_cast<void>(s.add_segment_constraint(axis,horizontal?ConstraintKind::Horizontal:ConstraintKind::Vertical));
            const auto bottom=segment(side*5,-32,0,-32);
            static_cast<void>(s.add_segment_pair_constraint(axis,bottom,ConstraintKind::Perpendicular));
        }
        double source_height=-32,source_radius=5;
        const bool external=source_mode!=0;
        if(source_mode==1) {
            auto ref=Sketch::create_external_reference(ExternalReferenceKind::Point);
            ref.source_document_id="source";ref.source_owner_id="support";ref.source_semantic_key="contact";
            ref.cached_points={xy(side*5,-32)};const auto id=ref.id;s.add_external_reference(ref);
            static_cast<void>(s.add_point_reference_constraint(contact,id));
        } else if(source_mode==2) {
            for(bool vertical:{true,false}) {
                auto ref=Sketch::create_external_reference(ExternalReferenceKind::Edge);
                ref.source_document_id="source";ref.source_owner_id="support";ref.source_semantic_key=vertical?"vertical":"horizontal";
                ref.cached_points=vertical?std::vector{xy(side*5,-50),xy(side*5,10)}:std::vector{xy(-10,-32),xy(10,-32)};
                const auto id=ref.id;s.add_external_reference(ref);static_cast<void>(s.add_point_on_line_constraint(contact,id));
            }
        } else s.set_point_fixed(contact,true);
        auto diameter=s.create_line_symmetric_dimension(axis,wall,{},DimensionKind::DistanceLineSymmetric);
        diameter.locked=locked;s.apply_dimension(diameter);
        auto angle=s.create_line_pair_dimension(axis,chamfer,DimensionKind::AngleBetween);
        angle.value=std::min(angle.value,180-angle.value);angle.angle_sector=1;
        angle.locked=locked;s.apply_dimension(angle);
        const auto acute_angle=[&] {
            const auto* a=s.find_point(contact);const auto* b=s.find_point(wall_end);
            const double dx=horizontal?b->y-a->y:b->x-a->x;
            const double dy=horizontal?b->x-a->x:b->y-a->y;
            return std::atan2(std::abs(dx),std::abs(dy))*180/std::numbers::pi;
        };
        const auto verify=[&](double expected_angle) {
            const auto* a=s.find_point(s.segments.front().first_point_id);const auto* b=s.find_point(wall_end);
            const auto offset=[&](const auto* p){return horizontal?p->y:p->x;};
            const auto source=xy(side*source_radius,source_height);const auto* anchor=s.find_point(contact);
            require(std::hypot(anchor->x-source[0],anchor->y-source[1])<1e-7,"Coupled edit moved its source contact");
            require(std::abs(offset(a)-offset(b))<1e-7 &&
                std::abs(2*std::abs(offset(b))-s.dimensions.front().value)<1e-7,"Coupled edit broke the symmetric wall");
            require(std::abs(acute_angle()-expected_angle)<1e-6,"Diameter edit changed the chamfer angle");
        };
        const double initial=acute_angle();
        for(double value:{16.,12.,14.}) {
            require(s.set_dimension_value(diameter.id,value),"Coupled symmetric wall value edit failed");verify(initial);
        }
        for(double value:{25.,30.,15.}) {
            // The factory preserves the original directed ray branch.
            const double target=angle.value>90?180-value:value;
            require(s.set_dimension_value(angle.id,target),"Coupled chamfer angle value edit failed");verify(value);
        }
        require(s.set_dimension_value(diameter.id,11)&&s.set_dimension_value(angle.id,30),"Source movement setup rejected 11 mm / 30 degrees");verify(30);
        if(external) {
            const auto before=s.serialized();zima::kernel::ViewerReferenceGeometry crossed;
            if(source_mode==1) {
                const auto p=xy(side*6,-27.5);crossed.points.push_back({{p[0],p[1],0},{"support","contact",{}}});
            } else for(bool vertical:{true,false}) {
                zima::kernel::ViewerEdge edge;edge.reference={"support",vertical?"vertical":"horizontal",{}};
                for(const auto& p:vertical?std::vector{xy(side*6,-50),xy(side*6,10)}:std::vector{xy(-10,-27.5),xy(10,-27.5)})edge.points.push_back({p[0],p[1],0});
                crossed.edges.push_back(std::move(edge));
            }
            bool rejected=false;try{static_cast<void>(s.refresh_external_references("source",crossed));}catch(const std::exception&){rejected=true;}
            require(rejected&&s.serialized()==before,"Crossed shoulder source was not rejected atomically");
        }
        if(external)for(const auto& position:std::array<std::array<double,2>,4>{{{5.1,-31.},{6.,-27.5},{5.,-33.},{5.,-32.}}}) {
            const auto [radius,height]=position;
            const bool grow=radius>=source_radius;
            if(grow)require(s.set_dimension_value(diameter.id,2*(radius+.5)),"Source movement diameter setup failed");
            source_height=height;source_radius=radius;zima::kernel::ViewerReferenceGeometry source;
            if(source_mode==1) {
                const auto p=xy(side*radius,height);source.points.push_back({{p[0],p[1],0},{"support","contact",{}}});
            } else for(bool vertical:{true,false}) {
                zima::kernel::ViewerEdge edge;edge.reference={"support",vertical?"vertical":"horizontal",{}};
                for(const auto& p:vertical?std::vector{xy(side*radius,-50),xy(side*radius,10)}:std::vector{xy(-10,height),xy(10,height)})edge.points.push_back({p[0],p[1],0});
                source.edges.push_back(std::move(edge));
            }
            static_cast<void>(s.refresh_external_references("source",source));
            if(!grow)require(s.set_dimension_value(diameter.id,2*(radius+.5)),"Source movement diameter reduction failed");
            verify(30);
            const auto* anchor=s.find_point(contact);const auto* free=s.find_point(wall_end);
            require((horizontal?free->x-anchor->x:free->y-anchor->y)>0,"Moving a source contact reversed the chamfer branch");
            for(double width:{2*(radius+1),2*(radius+.5)}) {require(s.set_dimension_value(diameter.id,width),"Source refresh blocked a following diameter edit");verify(30);}
        }
        auto saved=Sketch::from_serialized(s.serialized());
        require(saved.dimensions.size()==2 && saved.dimensions.front().locked==locked,"Coupled wall persistence lost dimensions");
    }
    std::cout<<"Symmetric wall + chamfer angle: 96 orientation/order/source/lock/axis matrices with successive value and source-contact edits passed\n";
}
