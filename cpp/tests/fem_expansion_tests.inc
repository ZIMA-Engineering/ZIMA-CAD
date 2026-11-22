void expansion_restraint(app::FemVolumeMesh& mesh,document::FemDefinition& def,unsigned node,std::array<bool,3> fixed) {
    if(!fixed[0]&&!fixed[1]&&!fixed[2])return;
    document::FemCondition c;c.id="restraint-"+std::to_string(node);c.name=c.id;
    c.kind=document::FemConditionKind::Constraint;c.restrained=fixed;def.conditions.push_back(c);
    mesh.condition_regions[c.id]={document::FemRegionKind::Point,{node},{}};
}
void expansion() {
    constexpr app::FemElasticMaterial elastic{210000,.3};
    for(bool quadratic:{false,true})for(bool reversed:{false,true})for(bool curved:{false,true}) {
        if(curved&&!quadratic)continue;
        auto mesh=tetra(quadratic);
        if(curved)mesh.surface.vertices[5].z+=.07;
        if(reversed)for(auto& p:mesh.surface.vertices)std::swap(p.x,p.y);
        for(double size:{1.,1000.})for(double rise:{-10.,0.,30.})for(int restriction:{0,1,2}) {
            auto scaled=mesh;const kernel::Vec3 origin{17,-9,23};
            for(auto& p:scaled.surface.vertices)p={origin.x+size*p.x,origin.y+size*p.y,origin.z+size*p.z};
            auto def=definition();def.thermal->expansion=true;
            for(unsigned i=0;i<mesh.surface.vertices.size();++i) {
                const auto p=mesh.surface.vertices[i];
                expansion_restraint(scaled,def,i,{restriction>0||p.x==0,restriction==2||p.y==0,restriction==2||p.z==0});
            }
            // Loads authored for the mechanical analysis do not contaminate
            // displacement explicitly reported as caused by heating.
            document::FemCondition force;force.id="inactive-force";force.name=force.id;
            force.kind=document::FemConditionKind::Force;force.force_N=1e12;def.conditions.push_back(force);
            std::vector<double> temperature(scaled.surface.vertices.size(),20+rise);
            app::FemThermalStrain strain{{1.2e-5,20},temperature};
            const auto result=app::solve_fem_linear_statics(scaled,def,elastic,{},&strain);
            const double e=strain.material.coefficient_per_K*rise;
            for(unsigned i=0;i<result.displacement.size();++i) {
                const auto p=mesh.surface.vertices[i],u=result.displacement[i];
                near(u.x,restriction?0:e*size*p.x,1e-8,"Thermal expansion X");
                near(u.y,restriction==2?0:e*size*p.y*(restriction?1+elastic.poisson:1),1e-8,"Thermal expansion Y");
                near(u.z,restriction==2?0:e*size*p.z*(restriction?1+elastic.poisson:1),1e-8,"Thermal expansion Z");
            }
            const auto stress=app::fem_element_stresses(scaled,result.displacement,elastic,&strain).front();
            const double expected=restriction==2?-elastic.young_MPa*e/(1-2*elastic.poisson):restriction?-elastic.young_MPa*e:0;
            near(stress.components_MPa[0],expected,1e-5,"Thermal restrained normal stress");
            near(stress.components_MPa[1],restriction==2?expected:0,1e-5,"Thermal lateral stress");
            near(stress.components_MPa[2],restriction==2?expected:0,1e-5,"Thermal lateral stress");
            near(result.maximum_von_mises_MPa,restriction==1?std::abs(expected):0,1e-5,"Thermal von Mises stress");
            kernel::Vec3 balance{};
            for(auto reaction:result.reaction){balance.x+=reaction.x;balance.y+=reaction.y;balance.z+=reaction.z;}
            near(std::hypot(balance.x,balance.y,balance.z),0,1e-3,"Thermal force equilibrium");
            check(nlohmann::json(def).get<document::FemDefinition>()==def,"Expansion setting persistence");
        }
    }
    auto mesh=tetra(false);auto def=definition();std::vector<double> t(4,40);
    app::FemThermalStrain strain{{1e-5,20},t};
    rejects([&]{(void)app::solve_fem_linear_statics(mesh,def,elastic,{},&strain);});
    for(unsigned i=0;i<4;++i)expansion_restraint(mesh,def,i,{true,true,true});
    t[0]=std::numeric_limits<double>::quiet_NaN();rejects([&]{(void)app::solve_fem_linear_statics(mesh,def,elastic,{},&strain);});
    t[0]=-300;rejects([&]{(void)app::solve_fem_linear_statics(mesh,def,elastic,{},&strain);});
    t[0]=20;strain.temperature_C=std::span<const double>(t).first(3);rejects([&]{(void)app::solve_fem_linear_statics(mesh,def,elastic,{},&strain);});
}
void expansion_gradient() {
    auto mesh=tetra(true);auto def=definition();constexpr app::FemElasticMaterial elastic{1000,.25};
    expansion_restraint(mesh,def,0,{true,true,true});expansion_restraint(mesh,def,1,{false,true,true});
    expansion_restraint(mesh,def,2,{false,false,true});
    std::vector<double> temperature;for(auto p:mesh.surface.vertices)temperature.push_back(30+20*p.x);
    app::FemThermalStrain strain{{1e-5,20},temperature};
    const auto result=app::solve_fem_linear_statics(mesh,def,elastic,{},&strain);
    for(unsigned i=0;i<result.displacement.size();++i) {
        const auto p=mesh.surface.vertices[i],u=result.displacement[i];
        near(u.x,1e-4*p.x+1e-4*(p.x*p.x-p.y*p.y-p.z*p.z),1e-10,"Linear temperature free expansion X");
        near(u.y,1e-4*p.y+2e-4*p.x*p.y,1e-10,"Linear temperature free expansion Y");
        near(u.z,1e-4*p.z+2e-4*p.x*p.z,1e-10,"Linear temperature free expansion Z");
    }
    near(result.maximum_von_mises_MPa,0,1e-7,"Compatible thermal gradient creates stress");
    const kernel::Vec3 p{.2,.3,.1};const auto hit=app::locate_fem_point(mesh,p);
    const auto u=app::fem_probe_displacement(mesh,*hit,result.displacement);
    near(u.x,1.4e-5,1e-10,"Heating-induced measurement X");
    near(u.y,4.2e-5,1e-10,"Heating-induced measurement Y");near(u.z,1.4e-5,1e-10,"Heating-induced measurement Z");
    near(app::fem_probe_scalar(mesh,*hit,temperature),34,1e-10,"Heating measurement temperature");
    def.conditions.clear();mesh.condition_regions.clear();
    for(unsigned i=0;i<mesh.surface.vertices.size();++i)expansion_restraint(mesh,def,i,{true,true,true});
    for(unsigned i=0;i<temperature.size();++i)temperature[i]=20+mesh.surface.vertices[i].x*mesh.surface.vertices[i].x;
    const auto fixed=app::solve_fem_linear_statics(mesh,def,elastic,{},&strain);
    double virtual_work{};
    for(unsigned i=0;i<temperature.size();++i)virtual_work-=fixed.reaction[i].x*mesh.surface.vertices[i].x*mesh.surface.vertices[i].x;
    // Independent exact simplex integral: integral(2*x^3) = 1/60.
    near(virtual_work,elastic.young_MPa/(1-2*elastic.poisson)*1e-5/60,1e-12,"Cubic T10 thermal load integration");
}
void expansion_units() {
    document::MaterialData m;m.properties["THERMAL_EXPANSION_COEFFICIENT"]="1.2e-5";
    m.properties["THERM_EXPANSION_REF_TEMPERATURE"]="20";
    m.units["THERMAL_EXPANSION_COEFFICIENT"]="1/C";m.units["THERM_EXPANSION_REF_TEMPERATURE"]="C";
    auto value=app::fem_expansion_material(m);near(value.coefficient_per_K,1.2e-5,1e-15,"Native expansion unit");near(value.reference_C,20,1e-12,"Celsius reference");
    m.units["THERMAL_EXPANSION_COEFFICIENT"]="1/K";m.units["THERM_EXPANSION_REF_TEMPERATURE"]="K";
    m.properties["THERM_EXPANSION_REF_TEMPERATURE"]="293.15";
    value=app::fem_expansion_material(m);near(value.coefficient_per_K,1.2e-5,1e-15,"Kelvin expansion unit");near(value.reference_C,20,1e-12,"Kelvin reference");
    m.units["THERMAL_EXPANSION_COEFFICIENT"]="1/F";m.units["THERM_EXPANSION_REF_TEMPERATURE"]="F";
    m.properties["THERM_EXPANSION_REF_TEMPERATURE"]="68";value=app::fem_expansion_material(m);
    near(value.coefficient_per_K,2.16e-5,1e-15,"Fahrenheit expansion unit");near(value.reference_C,20,1e-12,"Fahrenheit reference");
    m.properties["THERMAL_EXPANSION_COEFFICIENT"]="junk";check(!std::isfinite(app::fem_expansion_material(m).coefficient_per_K),"Invalid expansion accepted");
    m.units["THERM_EXPANSION_REF_TEMPERATURE"]="m";check(!std::isfinite(app::fem_expansion_material(m).reference_C),"Invalid reference unit accepted");
}
