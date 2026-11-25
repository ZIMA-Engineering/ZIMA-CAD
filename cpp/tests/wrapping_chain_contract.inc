static void wrapping_chain_contract() {
    using namespace k::wrap_detail;
    const auto directory=std::filesystem::temp_directory_path()/"zima-wrap-chain-contract";std::filesystem::create_directories(directory);
    BRepBuilderAPI_MakeWire outline;
    outline.Add(BRepBuilderAPI_MakeEdge(GC_MakeArcOfCircle(gp_Pnt(10,0,0),gp_Pnt(10/std::sqrt(2.),10/std::sqrt(2.),0),gp_Pnt(0,10,0)).Value()));
    outline.Add(BRepBuilderAPI_MakeEdge(GC_MakeArcOfCircle(gp_Pnt(0,10,0),gp_Pnt(-5/std::sqrt(2.),5+5/std::sqrt(2.),0),gp_Pnt(-5,5,0)).Value()));
    for(const auto& points:{std::pair{gp_Pnt(-5,5,0),gp_Pnt(-5,-10,0)},std::pair{gp_Pnt(-5,-10,0),gp_Pnt(10,-10,0)},std::pair{gp_Pnt(10,-10,0),gp_Pnt(10,0,0)}})
        outline.Add(BRepBuilderAPI_MakeEdge(points.first,points.second));
    require(outline.IsDone(),"Chain fixture wire failed");
    const auto solid=BRepPrimAPI_MakePrism(BRepBuilderAPI_MakeFace(outline.Wire()).Face(),gp_Vec(0,0,30)).Shape();
    const auto file=directory/"two-arcs-line.step";STEPControl_Writer writer;
    require(writer.Transfer(solid,STEPControl_AsIs)==IFSelect_RetDone&&writer.Write(file.string().c_str())==IFSelect_RetDone,"Chain fixture write failed");
    k::OcctKernel engine;k::StepRequest step;step.source_path=file.string();const k::HistoryOperation base{"chain-body",step};
    const auto input=engine.evaluate_history({base}).back();valid(input);std::array<k::FaceReference,3> targets;
    for(const auto& face:input.mesh.triangle_references) {
        const auto s=face.surface?face.surface:face.contour_surface;if(!s)continue;
        if(s->kind==k::SurfaceGeometry::Kind::Cylinder)targets[std::abs(s->radius-10)<1e-6?0:1]=face;
        if(s->kind==k::SurfaceGeometry::Kind::Plane&&std::abs(s->origin.x+5)<1e-6&&std::abs(s->axis.x)>.99)targets[2]=face;
    }
    require(std::ranges::all_of(targets,[](const auto& face){return face.valid();}),"Chain fixture target supports missing");
    const double first_length=2.5*std::numbers::pi+3,second_length=2.5*std::numbers::pi,flat_length=8;
    const double upper=2.5*std::numbers::pi+second_length+flat_length,width=upper+3;
    k::WrappingRequest request;request.sketch_id="chain-sketch";request.target=targets[0];request.continuations={targets[1],targets[2]};
    request.origin={10/std::sqrt(2.),10/std::sqrt(2.),15};request.x_axis={-1/std::sqrt(2.),1/std::sqrt(2.),0};request.y_axis={0,0,1};request.normal={1/std::sqrt(2.),1/std::sqrt(2.),0};
    request.height=.8;request.tolerance=.001;
    const auto world=[&](double u,double v){return add(request.origin,add(scale(request.x_axis,u),scale(request.y_axis,v)));};
    k::ExtrusionRequest profile;profile.outer_profile=k::ExtrusionRequest::PolygonProfile{{world(-3,-4),world(upper,-4),world(upper,4),world(-3,4)}};
    profile.outer_edge_source_ids={"bottom","right","top","left"};profile.outer_vertex_source_ids={"a","b","c","d"};profile.profile_region_id="region";profile.outer_boundary_id="loop";profile.direction=request.normal;request.profiles={profile};
    k::ViewerImage artwork;artwork.reference={"chain-sketch","artwork",{}};artwork.data_base64="embedded-chain-artwork";
    artwork.corners={world(-3,4),world(upper,4),world(upper,-4),world(-3,-4)};request.images={artwork};
    const auto charts=k::viewer_wrap_charts(input.mesh,{targets[0],targets[1],targets[2]},request.origin,request.x_axis,request.y_axis,false,false,.001,[](k::Vec3 point){return point;});
    require(charts.size()==3,"Viewer did not form the three-support chain");
    require(length(sub(charts[1].map.point(2.5*std::numbers::pi,0),{0,10,15}))<1e-6&&
            length(sub(charts[2].map.point(5*std::numbers::pi,0),{-5,5,15}))<1e-6&&
            length(sub(charts[2].map.point(upper,0),{-5,-3,15}))<1e-6,"Developed chain lost lengths at a support transition");
    const double original_volume=precise_volume(input);
    for(auto output:{k::WrappingResult::Curves,k::WrappingResult::Surface,k::WrappingResult::Relief,k::WrappingResult::Split})
    for(bool inward:{false,true}) {
        request.output=output;request.inward=inward;k::HistoryOperation operation{"chain-wrap",request};
        operation.operation=inward&&output==k::WrappingResult::Relief?k::BooleanOperation::Subtract:k::BooleanOperation::Add;
        const auto result=engine.evaluate_history({base,operation}).back();valid(result);
        const double expected=output==k::WrappingResult::Relief?original_volume+(inward?-1.:1.)*8*(width*.8+(inward?-1.:1.)*.8*.8/2*(first_length/10+second_length/5)):original_volume;
        require(std::abs(precise_volume(result)-expected)<.01,"Multi-support Wrap changed lengths or relief material");
        if(output==k::WrappingResult::Curves) {
            require(result.mesh.images.size()==1&&!result.mesh.images.front().surface_vertices.empty()&&
                result.mesh.images.front().data_base64==artwork.data_base64,"Chained wrap lost embedded artwork");
            std::array<bool,3> occupied{};
            const auto& image=result.mesh.images.front();
            for(std::size_t i=0;i<image.surface_uv.size();++i) {
                const auto uv=image.surface_uv[i];const double u=-3+uv[0]*width,v=4-8*uv[1];
                const auto chart=u<2.5*std::numbers::pi?0:u<5*std::numbers::pi?1:2;occupied[chart]=true;
                const auto error=length(sub(image.surface_vertices[i],charts[chart].map.point(u,v)));
                if(error>operation.mesh_deflection+width/4096.)std::cerr<<"Artwork error="<<error<<" allowed="<<operation.mesh_deflection+width/4096.<<" u="<<u<<" v="<<v<<" chart="<<chart<<'\n';
                require(error<=operation.mesh_deflection+width/4096.,"Artwork changed its source coordinates across a support junction");
            }
            require(std::ranges::all_of(occupied,[](bool value){return value;}),"Artwork omitted one chained support");
            // Curve results are authored references, not edges of the solid.
            // Their length is measured from native geometry at calculation;
            // display chords would introduce a deflection-dependent deficit.
            double length_sum=0;
            for(const auto& edge:result.mesh.original_references.edges)if(edge.reference.owner_id=="chain-wrap") {
                require(edge.measured_length.has_value(),"Wrapped curve lost its exact source length");
                length_sum+=*edge.measured_length;
            }
            require(std::abs(length_sum-2*(width+8))<request.tolerance,"Wrapped boundary contains a missing or duplicated piece");
        }
        if(output==k::WrappingResult::Relief) {
            const auto result_shape=k::read_calculated_body_shape(result);
            for(const auto sample:{gp_Pnt(0,10+(inward?-.4:.4),15),gp_Pnt(-5+(inward?.4:-.4),5,15)}) {
                BRepClass3d_SolidClassifier classify(result_shape,sample,1e-6);
                require(classify.State()==(inward?TopAbs_OUT:TopAbs_IN),"Relief retained an internal wall at a support junction");
            }
        }
        auto duplicate=request;duplicate.continuations.insert(duplicate.continuations.begin(),targets[0]);duplicate.continuations.push_back(targets[2]);
        operation.primitive=duplicate;const auto repeated=engine.evaluate_history({base,operation}).back();valid(repeated);
        require(std::abs(precise_volume(repeated)-precise_volume(result))<1e-7,"Repeated target changed the Wrap calculation");
        std::cout<<"Wrap chain output="<<int(output)<<" inward="<<inward<<" passed\n";
    }
    // A hole crossing the radius change must remain a hole in both chart
    // pieces. A separate island on the flat must remain independent material.
    auto filled=request;filled.output=k::WrappingResult::Relief;
    const double seam=2.5*std::numbers::pi;
    filled.profiles.front().inner_profiles.push_back(k::ExtrusionRequest::PolygonProfile{{world(seam-1,-1),world(seam+1,-1),world(seam+1,1),world(seam-1,1)}});
    filled.profiles.front().inner_boundary_ids={"hole"};
    filled.profiles.front().inner_edge_source_ids={{"ha","hb","hc","hd"}};
    filled.profiles.front().inner_vertex_source_ids={{"hp","hq","hr","hs"}};
    auto island=profile;island.outer_profile=k::ExtrusionRequest::PolygonProfile{{world(upper+1,-1),world(upper+3,-1),world(upper+3,1),world(upper+1,1)}};
    island.outer_edge_source_ids={"ia","ib","ic","id"};island.outer_vertex_source_ids={"ip","iq","ir","is"};
    island.profile_region_id="island";island.outer_boundary_id="island-loop";filled.profiles.push_back(island);
    for(bool inward:{false,true}) {
        filled.inward=inward;k::HistoryOperation operation{"chain-filled",filled};
        operation.operation=inward?k::BooleanOperation::Subtract:k::BooleanOperation::Add;
        const auto result=engine.evaluate_history({base,operation}).back();valid(result);
        const double sign=inward?-1.:1.,h=filled.height;
        const double outer=8*(width*h+sign*h*h/2*(first_length/10+second_length/5));
        const double hole=4*h+sign*h*h/2*(2./10+2./5),separate=4*h;
        require(std::abs(precise_volume(result)-(original_volume+sign*(outer-hole+separate)))<.01,"Chained Wrap lost a hole or separate filled island");
        const auto shape=k::read_calculated_body_shape(result);
        for(double side:{-.4,.4}) {
            BRepClass3d_SolidClassifier probe(shape,gp_Pnt(0,10+side,15),1e-6);
            require(probe.State()==(side<0?TopAbs_IN:TopAbs_OUT),"Chained Wrap filled or removed material inside a Sketch hole");
        }
    }
    for(const auto continuation:{std::vector<k::FaceReference>{},std::vector<k::FaceReference>{targets[1]},std::vector<k::FaceReference>{targets[2],targets[1]}}) {
        request.continuations=continuation;request.output=k::WrappingResult::Relief;
        bool rejected=false;
        try {rejected=!engine.evaluate_history({base,{"chain-wrap",request}}).back().calculation_errors.empty();}
        catch(const std::exception&){rejected=true;}
        require(rejected,"Unselected or disconnected continuation silently changed the Wrap domain");
    }
    std::filesystem::remove(file);std::filesystem::remove(directory);
}
