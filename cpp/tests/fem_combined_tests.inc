void combined() {
    constexpr app::FemElasticMaterial elastic{210000,.3};unsigned cases{};
    for(bool quadratic:{false,true})for(bool curved:{false,true})for(bool reverse:{false,true})
    for(double power:{-.01,0.,.03})for(double force:{-1.,0.,2.})for(bool outward:{false,true})
    for(double pressure_value:{0.,2.5})for(bool fully_fixed:{false,true}) {
        if(curved&&!quadratic)continue;++cases;
        auto mesh=tetra(quadratic);if(curved)mesh.surface.vertices[5].z+=.07;
        if(reverse)for(auto& p:mesh.surface.vertices)std::swap(p.x,p.y);
        auto def=definition();def.analysis=document::FemAnalysisKind::ThermalMechanical;
        if(fully_fixed)for(unsigned i=0;i<mesh.surface.vertices.size();++i)expansion_restraint(mesh,def,i,{true,true,true});
        else {
            expansion_restraint(mesh,def,0,{true,true,true});expansion_restraint(mesh,def,1,{false,true,true});
            expansion_restraint(mesh,def,2,{false,false,true});
            // Reverse the fixture's constraints with its axes as well.
            if(reverse)def.conditions[1].restrained={true,false,true};
        }
        auto heat=source("heat",power);def.conditions.push_back(heat);
        mesh.condition_regions[heat.id]={document::FemRegionKind::Surface,{},{{0,1,2,4,5,6}}};
        auto load=heat;load.id="force";load.name=load.id;load.kind=document::FemConditionKind::Force;load.force_N=force;
        load.heat_W=100;
        load.region=document::FemRegionKind::Point;load.references.clear();def.conditions.push_back(load);
        mesh.condition_regions[load.id]={document::FemRegionKind::Point,{3},{{3}}};
        auto pressure=heat;pressure.id="pressure";pressure.name=pressure.id;pressure.kind=document::FemConditionKind::Pressure;
        pressure.heat_W=100;
        pressure.pressure_MPa=pressure_value;pressure.pressure_reversed=outward;def.conditions.push_back(pressure);
        mesh.condition_regions[pressure.id]={document::FemRegionKind::Surface,{},{{1,2,3,5,8,9}}};
        const auto temperature=app::solve_fem_steady_thermal(mesh,def,{.05});
        app::FemThermalStrain strain{{1.2e-5,20},temperature.temperature_C};
        const auto result=app::solve_fem_linear_statics(mesh,def,elastic,{},&strain);
        auto heat_only=def;heat_only.analysis=document::FemAnalysisKind::SteadyStateThermal;heat_only.thermal->expansion=true;
        const auto thermal=app::solve_fem_linear_statics(mesh,heat_only,elastic,{},&strain);
        auto force_only=def;force_only.analysis=document::FemAnalysisKind::LinearElasticStatic;
        std::erase_if(force_only.conditions,[](const auto& c){return c.kind!=document::FemConditionKind::Constraint&&c.kind!=document::FemConditionKind::Force;});
        auto pressure_only=def;pressure_only.analysis=document::FemAnalysisKind::LinearElasticStatic;
        std::erase_if(pressure_only.conditions,[](const auto& c){return c.kind!=document::FemConditionKind::Constraint&&c.kind!=document::FemConditionKind::Pressure;});
        const auto f=app::solve_fem_linear_statics(mesh,force_only,elastic),p=app::solve_fem_linear_statics(mesh,pressure_only,elastic);
        for(unsigned i=0;i<result.displacement.size();++i) {
            const auto a=result.displacement[i],b=thermal.displacement[i],c=f.displacement[i],d=p.displacement[i];
            near(a.x,b.x+c.x+d.x,1e-9,"Combined displacement X superposition");
            near(a.y,b.y+c.y+d.y,1e-9,"Combined displacement Y superposition");
            near(a.z,b.z+c.z+d.z,1e-9,"Combined displacement Z superposition");
            const auto ar=result.reaction[i],br=thermal.reaction[i],cr=f.reaction[i],dr=p.reaction[i];
            near(ar.x,br.x+cr.x+dr.x,1e-5,"Combined reaction X superposition");
            near(ar.y,br.y+cr.y+dr.y,1e-5,"Combined reaction Y superposition");
            near(ar.z,br.z+cr.z+dr.z,1e-5,"Combined reaction Z superposition");
        }
        // Compare tensors, never sums of von Mises scalars. Curved T10 needs
        // the same high-order output quadrature for all three component fields.
        std::vector<double> reference(mesh.surface.vertices.size(),20);
        const app::FemThermalStrain zero{{1.2e-5,20},reference};
        const auto a=app::fem_element_stresses(mesh,result.displacement,elastic,&strain).front();
        const auto b=app::fem_element_stresses(mesh,thermal.displacement,elastic,&strain).front();
        const auto c=app::fem_element_stresses(mesh,f.displacement,elastic,&zero).front();
        const auto d=app::fem_element_stresses(mesh,p.displacement,elastic,&zero).front();
        for(unsigned i=0;i<6;++i)near(a.components_MPa[i],b.components_MPa[i]+c.components_MPa[i]+d.components_MPa[i],1e-5,"Combined stress tensor superposition");
        check(document::fem_thermal_expansion(def),"Combined mode lost implicit expansion");
        check(nlohmann::json(def).get<document::FemDefinition>()==def,"Combined definition persistence");
        rejects([&]{(void)app::solve_fem_linear_statics(mesh,def,elastic);});
        const auto heat_again=app::solve_fem_steady_thermal(mesh,heat_only,{.05});
        check(temperature.temperature_C==heat_again.temperature_C,"Mechanical loads affected heat conduction");
    }
    std::cout<<"Combined thermal/force/pressure: "<<cases<<" superposition configurations passed\n";
}
void distance_results() {
    for(bool reverse:{false,true})for(double offset:{0.,1e9})for(double span:{0.,1.,1e6})for(double extension:{-1e-6,0.,1e-6}) {
        kernel::Vec3 a{offset,7,9},b{offset+span,7,9},u{.1,.2,.3},v{.1+extension,.2,.3};
        if(reverse){std::swap(a,b);std::swap(u,v);}
        const auto result=app::fem_distance_result(a,b,u,v);
        near(result.original_mm,span,1e-10,"Distance original");
        near(result.deformed_mm,std::abs(span+extension),1e-9,"Distance deformed");
        near(result.change_mm,span?extension:std::abs(extension),1e-14,"Distance cancellation");
    }
    const kernel::Vec3 a{2,3,4},b{5,7,9};
    const auto rotation=app::fem_distance_result(a,b,{-5,-1,0},{-12,-2,0});
    near(rotation.change_mm,0,1e-14,"Rigid rotation changed distance");
    near(app::fem_distance_result(a,b,{4,5,6},{4,5,6}).change_mm,0,1e-14,"Rigid translation changed distance");
    for(bool quadratic:{false,true}) {
        const auto mesh=tetra(quadratic);std::vector<kernel::Vec3> displacement;
        for(auto p:mesh.surface.vertices)displacement.push_back({.001*p.x,.002*p.y,-.003*p.z});
        const kernel::Vec3 first{.1,.2,.1},second{.3,.2,.1};
        const auto p=app::locate_fem_point(mesh,first),q=app::locate_fem_point(mesh,second);
        const auto measured=app::fem_distance_result(first,second,app::fem_probe_displacement(mesh,*p,displacement),app::fem_probe_displacement(mesh,*q,displacement));
        near(measured.change_mm,.0002,1e-14,"Distance mesh interpolation");
    }
}
