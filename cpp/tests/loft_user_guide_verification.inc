// Optional verification on an isolated native user fixture; the input is read-only.
void verify_user_loft_guides(k::OcctKernel& kernel,const std::filesystem::path& path) {
 const auto part=d::PartDocument::load(path);
 const auto feature=std::ranges::find_if(part.history,[](const auto& f){return f.feature_kind==d::FeatureKind::Loft;});
 check(feature!=part.history.end(),"User fixture has no Loft");
 const auto anchors=d::loft_profile_anchor_mesh(*feature,feature->loft.sketches.size()).points;
 check(feature->loft.profiles.size()==3&&anchors.size()==12,"Expected three four-point profiles");
 const auto original=d::loft_request(part,*feature);
 unsigned failures=0;
 for(unsigned count:{1u,2u,4u})for(bool reversed:{false,true}) {
  auto request=original;request.thin=false;request.make_solid=true;request.guides.clear();request.guide_owners.clear();
  std::vector<std::array<k::Vec3,3>> curves;
  for(unsigned i=0;i<count;++i) {
   const auto a=anchors[i].position,m=anchors[4+i].position,b=anchors[8+i].position;
   const k::Vec3 control{2*m.x-(a.x+b.x)/2,2*m.y-(a.y+b.y)/2,2*m.z-(a.z+b.z)/2};
   curves.push_back({a,control,b});
   k::ExtrusionRequest::BSplineCurve curve;curve.start=reversed?b:a;curve.end=reversed?a:b;
   curve.control_points={curve.start,control,curve.end};curve.degree=2;curve.knots={0,0,0,1,1,1};curve.weights={1,1,1};
   k::ExtrusionRequest guide;guide.open_profile_end_id="end";guide.direction={0,1,0};
   guide.outer_profile=k::ExtrusionRequest::CurvedProfile{{curve}};
   guide.outer_edge_source_ids={"guide-edge"};guide.outer_vertex_source_ids={"start"};
   request.guides.push_back(std::move(guide));request.guide_owners.push_back("user-guide-"+std::to_string(i));
  }
  try {
  const auto body=kernel.evaluate_history({{feature->id,request}}).back();
  check(body.calculation_errors.empty(),"User profile Loft failed with side guides");
  const auto shape=k::read_calculated_body_shape(body);check(BRepCheck_Analyzer(shape).IsValid(),"User guided Loft is invalid");
  TopoDS_Compound skin;BRep_Builder builder;builder.MakeCompound(skin);
  for(TopExp_Explorer it(shape,TopAbs_FACE);it.More();it.Next())builder.Add(skin,it.Current());
  double maximum=0;
  for(const auto& curve:curves)for(unsigned j=0;j<=40;++j) {
   const double t=j/40.,u=1-t;
   const gp_Pnt p(u*u*curve[0].x+2*u*t*curve[1].x+t*t*curve[2].x,
      u*u*curve[0].y+2*u*t*curve[1].y+t*t*curve[2].y,
      u*u*curve[0].z+2*u*t*curve[1].z+t*t*curve[2].z);
   BRepExtrema_DistShapeShape distance(BRepBuilderAPI_MakeVertex(p).Vertex(),skin);
   check(distance.IsDone(),"Guide distance check failed");maximum=std::max(maximum,distance.Value());
  }
  std::cout<<"User guide count="<<count<<" reversed="<<reversed<<" maximum error="<<maximum<<std::endl;
  check(maximum<1e-5,"Calculated Loft does not follow its side guides");
  const auto packet=d::serialize_body_result(body);const auto restored=d::load_body_result(packet);
  check(d::serialize_body_result(restored)==packet,"Guided Loft packet changed on reopen");
  } catch(const std::exception& error) {
   ++failures;std::cout<<"User guide count="<<count<<" reversed="<<reversed<<" FAILED: "<<error.what()<<std::endl;
  }
 }
 check(failures==0,"User guided Loft matrix contains failing cases");
}
