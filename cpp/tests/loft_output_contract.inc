// Analytic cylinders and planar strips independently check Thin/Surface output.
void verify_loft_outputs(k::OcctKernel& kernel) {
 unsigned cases{};
 for(bool open:{false,true})for(bool reverse:{false,true})for(bool thin:{false,true})
 for(const auto side:{d::ThinMode::OneSide,d::ThinMode::OtherSide,d::ThinMode::Symmetric}) {
  if(!thin&&side!=d::ThinMode::OneSide)continue;
  auto f=fixture(false);
  for(auto& owned:f.loft.loft.sketches) {
   auto sketch=s::Sketch::from_serialized(owned.sketch_serialized);
   if(open) {
    sketch.circles.clear();sketch.points.clear();
    static_cast<void>(sketch.add_segment(-10,0,10,0));
   } else {
    const auto circle=sketch.circles.front().id;
    for(unsigned q=0;q<4;++q) {
     const auto point=sketch.add_point(10*std::cos(q*std::numbers::pi/2),10*std::sin(q*std::numbers::pi/2));
     static_cast<void>(sketch.add_point_on_circle_constraint(point,circle));
    }
   }
   const auto order=d::sweep3d_profile_correspondence(sketch,{},open);
   if(open&&reverse)f.loft.loft.correspondence_starts[owned.sketch_feature->id]=order.point_ids.back();
   else if(reverse)f.loft.loft.correspondence_reversed.insert(owned.sketch_feature->id);
   owned.sketch_serialized=sketch.serialized();
  }
  d::resolve_loft_sketches(f.loft,{});
  f.loft.loft.result_type=thin?d::ProfileResultType::Thin:d::ProfileResultType::Surface;f.loft.loft.thin_mode=side;
  w::Workspace live;live.add_part(f.part,w::calculate_part_with_resolved_references(kernel,f.part));live.activate(f.part.document_id);
  auto* state=live.open_part(f.part.document_id);
  for(double thickness:{.5,.8}) {
   f.loft.loft.thickness=thickness;
   const auto preview=d::loft_definition_mesh(f.part,f.loft);
   const auto guide_count=std::ranges::count_if(preview.edges,[](const auto& edge){return edge.reference.semantic_key.starts_with("loft:correspondence:");});
   check(guide_count==(open?4:8),"Loft preview lost numbered profile correspondence guides");
   for(const auto& owned:f.loft.loft.sketches) {
    const auto sketch=d::general_surface_display_sketch(f.loft,owned);
    const auto prefix="loft:input:from:"+sketch.id+":"+(thin?"thin:":"");
    unsigned contours{};
    for(const auto& edge:preview.edges)if(edge.reference.semantic_key.starts_with(prefix)) {
     ++contours;
     for(const auto& p:edge.points) {
      const auto o=sketch.resolved_origin,n=sketch.resolved_normal;
      near((p.x-o.x)*n.x+(p.y-o.y)*n.y+(p.z-o.z)*n.z,0);
     }
    }
    check(contours>=(thin?2u:1u),"Loft thickness preview did not draw both section-plane contours");
   }
   check(w::commit_loft(live,kernel,f.part.document_id,f.loft),"Loft Thin/Surface commit failed");
   auto body=state->session.calculated_boundaries().back();kernel.ensure_body_properties(body);
   check(body.calculation_errors.empty(),"Loft Thin/Surface retained a calculation error");
   check(BRepCheck_Analyzer(k::read_calculated_body_shape(body)).IsValid(),"Loft Thin/Surface B-Rep is invalid");
   if(thin) {
    const double inner=side==d::ThinMode::OneSide?10-thickness:side==d::ThinMode::OtherSide?10:10-thickness/2;
    const double outer=side==d::ThinMode::OneSide?10:side==d::ThinMode::OtherSide?10+thickness:10+thickness/2;
    near(body.volume,open?200*thickness:std::numbers::pi*(outer*outer-inner*inner)*10);
   } else {
    near(body.volume,0);near(body.surface_area,open?200:200*std::numbers::pi);
    for(const auto& face:body.mesh.triangle_references)check(face.valid()&&face.surface_result,"Loft Surface lost yellow style or native face identity");
   }
   const auto file=std::filesystem::temp_directory_path()/("zima-loft-output-"+f.loft.id+".prtz");
   auto analyzed=state->session.calculated_boundaries();analyzed.back()=body;
   state->session.document().save(file,analyzed);std::vector<k::BodyResult> cache;
   const auto reopened=d::PartDocument::load(file,&cache);std::filesystem::remove(file);
   check(reopened.find_container(f.loft.id)->loft==f.loft.loft,"Loft Thin/Surface native reopen changed output or correspondence");
   near(cache.back().volume,body.volume);
   check(w::step_part_document_history(live,f.part.document_id,false),"Loft Thin/Surface Undo failed");
   check(w::step_part_document_history(live,f.part.document_id,true)&&state->session.document().find_container(f.loft.id)->loft==f.loft.loft,"Loft Thin/Surface Redo changed its definition");
   ++cases;
  }
 }
 std::cout<<"Loft Thin/Surface: "<<cases<<" open/closed, forward/reverse, side/thickness cases, analytic properties, planar preview, native reopen and Undo/Redo passed"<<std::endl;
}
