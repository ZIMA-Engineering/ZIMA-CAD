Fixture mixed_offset_fixture() {
 auto f=fixture(false);
 for(unsigned i=0;i<3;++i) {
  auto& owned=f.loft.loft.sketches[i];auto sketch=s::Sketch::create_default();
  sketch.id=owned.sketch_feature->feature.sketch_id;sketch.owner_container_id=owned.sketch_feature->id;
  sketch.plane_reference_owner_id=owned.sketch_feature->container_origin.id;sketch.plane_auto=false;
  owned.sketch_feature->placement.z=i==0?0.:i==1?65.:153.;
  double first_x=-90,first_y=-35;
  if(i==0)static_cast<void>(sketch.add_rectangle(-90,-35,90,35));
  if(i==1) {
   const double x=std::sqrt(55.*55.-35.*35.);first_x=-x;
   const auto circle=sketch.add_circle(0,0,55);
   for(const auto [px,py]:{std::pair{-x,-35.},std::pair{x,-35.},std::pair{x,35.},std::pair{-x,35.}}) {
    const auto point=sketch.add_point(px,py);
    static_cast<void>(sketch.add_point_on_circle_constraint(point,circle));
   }
  }
  if(i==2) {static_cast<void>(sketch.add_slot(-65,0,65,0,12));first_x=-65;first_y=-12;}
  const auto start=std::ranges::find_if(sketch.points,[&](const auto& p){return std::hypot(p.x-first_x,p.y-first_y)<1e-8;});
  check(start!=sketch.points.end(),"Mixed Loft has no authored correspondence start");
  f.loft.loft.correspondence_starts[owned.sketch_feature->id]=start->id;
  owned.sketch_serialized=sketch.serialized();
 }
 d::resolve_loft_sketches(f.loft,{});return f;
}

auto offset_binding_keys(const k::BodyResult& body) {
 std::set<std::tuple<int,std::string,std::string,std::string>> keys;
 const auto add=[&](int kind,const auto& bindings){for(const auto& item:bindings)
  keys.emplace(kind,item.reference.owner_id,item.reference.semantic_key,item.reference.instance_path);};
 std::set<const k::BodyResult*> visited;
 const auto walk=[&](auto&& self,const k::BodyResult& value)->void {
  if(!visited.insert(&value).second)return;
  add(0,value.kernel_faces);add(1,value.kernel_edges);add(2,value.kernel_vertices);
  for(const auto& [id,snapshot]:value.body_outputs)self(self,snapshot.get());
 };
 walk(walk,body);return keys;
}
double offset_document_volume(const k::BodyResult& body) {
 if(!body.calculation_errors.empty())throw std::runtime_error(body.calculation_errors.begin()->second);
 const auto shape=k::read_calculated_body_shape(body);
 check(!shape.IsNull()&&BRepCheck_Analyzer(shape,true,false,true).IsValid()&&BRepAlgoAPI_Check(shape,false,true).IsValid(),
  "Mixed offset document has invalid material");
 GProp_GProps properties;BRepGProp::VolumeProperties(shape,properties);
 check(properties.Mass()>0&&properties.Mass()<700000,"Mixed offset document contains the full carrier");
 check(!offset_binding_keys(body).empty(),"Mixed offset document lost native bindings");
 return properties.Mass();
}

void verify_mixed_offset_documents(k::OcctKernel& kernel,const std::filesystem::path& directory) {
 std::filesystem::create_directories(directory);
 for(bool shell_mode:{false,true}) {
  auto f=mixed_offset_fixture();
  // Shell receives an already placed solid, so this also verifies that its
  // offset/cap intersections do not assume world XY/Z or an origin at zero.
  if(shell_mode) {f.loft.placement.x=17;f.loft.placement.y=-23;f.loft.placement.z=7;
   f.loft.placement.rotation_offset_x=13;f.loft.placement.rotation_offset_y=29;f.loft.placement.rotation_offset_z=41;}
  w::Workspace live;live.add_part(f.part,w::calculate_part_with_resolved_references(kernel,f.part));live.activate(f.part.document_id);
  check(w::commit_loft(live,kernel,f.part.document_id,f.loft),"Cannot create mixed solid Loft");
  auto* state=live.open_part(f.part.document_id);
  state->session.document().save(directory/(shell_mode?"mixed-placed-solid.prtz":"mixed-solid.prtz"),state->session.calculated_boundaries());
  auto feature=shell_mode?d::PartDocument::create_shell_container():*state->session.document().find_container(f.loft.id);
  bool shell_created=false;
  const auto commit=[&](const d::HistoryContainer& next) {
   if(shell_mode) {
    const bool result=w::commit_shell(live,kernel,f.part.document_id,next,
     shell_created?w::ShellEditMode::Replace:w::ShellEditMode::Create);
    if(result)shell_created=true;return result;
   }
   return w::commit_loft(live,kernel,f.part.document_id,next);
  };
  for(unsigned variant=0;variant<(shell_mode?4u:3u);++variant)for(double thickness:{.5,1.}) {
   if(shell_mode) {
    feature.shell.thickness=thickness;feature.shell.removed_faces.clear();
    for(unsigned end=0;end<2;++end)if(variant&(1u<<end))feature.shell.removed_faces.push_back({
     f.loft.id,"loft:cap:from:"+(end?f.loft.loft.profiles.back():f.loft.loft.profiles.front()),{}});
   } else {feature.loft.result_type=d::ProfileResultType::Thin;feature.loft.thickness=thickness;
    feature.loft.thin_mode=variant==0?d::ThinMode::OneSide:variant==1?d::ThinMode::OtherSide:d::ThinMode::Symmetric;}
   check(commit(feature),"Mixed offset edit did not commit");
   const auto definition=state->session.document().serialized();
   const auto body=state->session.calculated_boundaries().back();const double volume=offset_document_volume(body);
   const auto keys=offset_binding_keys(body);
   const auto calculations=k::body_calculation_count.load();
   feature=*state->session.document().find_container(feature.id);
   check(!commit(feature)&&k::body_calculation_count.load()==calculations&&state->session.document().serialized()==definition,
    "Unchanged mixed offset OK recalculated or changed the document");
   check(w::step_part_document_history(live,f.part.document_id,false),"Mixed offset Undo failed");
   check(w::step_part_document_history(live,f.part.document_id,true)&&state->session.document().serialized()==definition,
    "Mixed offset Redo changed the definition");
   check(offset_binding_keys(state->session.calculated_boundaries().back())==keys,"Mixed offset Redo changed native references");
   const auto file=directory/((shell_mode?"shell-":"thin-")+std::to_string(variant)+"-"+std::to_string(thickness)+".prtz");
   state->session.document().save(file,state->session.calculated_boundaries());std::vector<k::BodyResult> cache;
   const auto reopened=d::PartDocument::load(file,&cache);
   check(!cache.empty()&&offset_binding_keys(cache.back())==keys,"Mixed offset native reopen lost references");
   near(offset_document_volume(cache.back()),volume);
   k::OcctKernel cold;const auto rebuilt=cold.evaluate_history(reopened.kernel_operations());
   check(offset_binding_keys(rebuilt.back())==keys,"Cold mixed offset regeneration changed reference identities");
   near(offset_document_volume(rebuilt.back()),volume);
   std::cout<<(shell_mode?"Placed Shell":"Thin Loft")<<" document variant="<<variant<<" thickness="<<thickness
    <<": no-op, Undo/Redo, native reopen and cold references passed"<<std::endl;
  }
 }
}

void verify_mixed_offset_references(k::OcctKernel& kernel,const std::filesystem::path& directory) {
 unsigned cases{},faces{},edges{};
 for(bool shell:{false,true})for(unsigned variant=0;variant<(shell?4u:3u);++variant)for(double thickness:{.5,1.}) {
  const auto file=directory/((shell?"shell-":"thin-")+std::to_string(variant)+"-"+std::to_string(thickness)+".prtz");
  std::vector<k::BodyResult> cache;const auto part=d::PartDocument::load(file,&cache);
  check(!cache.empty(),"Mixed offset dependency fixture has no native result");
  const auto& body=cache.back().body_outputs.at(part.body_history.bodies().front().scope.id).get();
  const auto before=offset_binding_keys(body);
  std::vector<k::FaceReference> face_refs;std::vector<k::EdgeReference> edge_refs;
  for(const auto& face:body.mesh.triangle_references)if(face.valid()&&std::ranges::find(face_refs,face)==face_refs.end())face_refs.push_back(face);
  for(const auto& edge:body.mesh.edges)if(!edge.construction&&!edge.parameter_seam&&edge.reference.valid()&&
      std::ranges::find(edge_refs,edge.reference)==edge_refs.end())edge_refs.push_back(edge.reference);
  check(!face_refs.empty()&&!edge_refs.empty(),"Mixed offset has no selectable native faces or edges");
  for(const auto& reference:face_refs) {
   const auto captured=kernel.capture_reference_face(body,reference);
   check(!k::read_calculated_body_shape(captured).IsNull()&&captured.calculation_errors.empty(),
    "Dependent face capture lost a mixed offset reference");++faces;
  }
  const auto supports=kernel.inspect_edge_treatment_supports(body,edge_refs);
  check(supports.segments.size()==edge_refs.size(),"Mixed offset lost an edge-treatment support");
  for(std::size_t i=0;i<supports.segments.size();++i) {
   check(supports.segments[i].edge==edge_refs[i]&&supports.segments[i].adjacent_faces.size()==2,
    "Mixed offset edge reference no longer resolves to two native faces");++edges;
  }
  check(offset_binding_keys(body)==before,"Dependent reference inspection mutated the mixed offset source");
  const auto source=d::PartDocument::load(directory/(shell?"mixed-placed-solid.prtz":"mixed-solid.prtz"));
  const auto loft=std::ranges::find_if(part.history,[](const auto& f){return f.feature_kind==d::FeatureKind::Loft;});
  check(loft!=part.history.end()&&source.find_container(loft->id)->loft.sketches==loft->loft.sketches,
   "Thin or Shell changed its source Sketch definitions");++cases;
 }
 std::cout<<"Mixed offset dependencies: "<<cases<<" native documents, "<<faces<<" face captures, "<<edges
  <<" manifold edge supports, unchanged source Sketches and read-only references passed"<<std::endl;
}
