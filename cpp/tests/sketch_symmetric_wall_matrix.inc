// Coupled diameter and chamfer-angle edits must keep the source contact.
void verify_symmetric_wall_matrix() {
    using namespace zima::sketcher;
    for (bool horizontal : {false,true}) for (double side : {-1.,1.})
    for (bool reverse : {false,true}) for (bool external : {false,true})
    for (bool locked : {false,true}) {
        auto s=Sketch::create_default();
        const auto xy=[&](double x,double y){return horizontal?std::array{y,x}:std::array{x,y};};
        const auto segment=[&](double x,double y,double xx,double yy){const auto a=xy(x,y),b=xy(xx,yy);return s.add_segment(a[0],a[1],b[0],b[1]);};
        const auto wall=segment(side*7,0,side*7,-26);
        const auto wall_end=s.segments.front().second_point_id;
        const auto chamfer=reverse?segment(side*7,-26,side*5,-32):segment(side*5,-32,side*7,-26);
        const auto contact=reverse?s.segments.back().second_point_id:s.segments.back().first_point_id;
        if(external) {
            auto ref=Sketch::create_external_reference(ExternalReferenceKind::Point);
            ref.source_document_id="source";ref.source_owner_id="support";ref.source_semantic_key="contact";
            ref.cached_points={xy(side*5,-32)};const auto id=ref.id;s.add_external_reference(ref);
            static_cast<void>(s.add_point_reference_constraint(contact,id));
        } else s.set_point_fixed(contact,true);
        const std::string axis=horizontal?"sketch_axis:x":"sketch_axis:y";
        auto diameter=s.create_line_symmetric_dimension(axis,wall,{},DimensionKind::DistanceLineSymmetric);
        diameter.locked=locked;s.apply_dimension(diameter);
        auto angle=s.create_line_pair_dimension(axis,chamfer,DimensionKind::AngleBetween);
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
            const auto source=xy(side*5,-32);const auto* anchor=s.find_point(contact);
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
        auto saved=Sketch::from_serialized(s.serialized());
        require(saved.dimensions.size()==2 && saved.dimensions.front().locked==locked,"Coupled wall persistence lost dimensions");
    }
    std::cout<<"Symmetric wall + chamfer angle: 32 orientation/order/source/lock matrices with successive value edits passed\n";
}
