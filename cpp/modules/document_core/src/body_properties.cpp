#include <zima/document/body_properties.hpp>
#include <zima/document/part_document.hpp>
#include <zima/document/physical_properties.hpp>
#include <zima/document/metadata.hpp>
#include <nlohmann/json.hpp>
#include <set>
namespace zima::document {
namespace {
using Json=nlohmann::json;
void finite(double x) { if(!std::isfinite(x))throw std::invalid_argument("Body properties require finite values."); }
Json vector(kernel::Vec3 v) {return Json::array({v.x,v.y,v.z});}
kernel::Vec3 vector(const Json& v) {
    if(!v.is_array()||v.size()!=3)throw std::invalid_argument("Invalid body properties vector.");
    kernel::Vec3 out{v.at(0).get<double>(),v.at(1).get<double>(),v.at(2).get<double>()};
    finite(out.x);finite(out.y);finite(out.z);return out;
}
template<class Entries> std::size_t boundary(const Entries& entries,const std::string& anchor) {
    if(anchor.empty())return 0;
    for(std::size_t i=0;i<entries.size();++i) {
        if constexpr(std::is_same_v<typename Entries::value_type,std::string>) {if(entries[i]==anchor)return i+1;}
        else {if(entries[i].id==anchor)return i+1;}
    }
    throw std::invalid_argument("The body properties history anchor is missing.");
}
}
std::vector<BodyPropertiesInput> body_properties_inputs(const PartDocument& doc,
    const std::vector<kernel::BodyResult>& calculated,const BodyProperties& record) {
    if(calculated.empty())throw std::invalid_argument("Regenerate the model before measuring body properties.");
    std::vector<BodyPropertiesInput> out;
    const auto operations=[&](const auto& entries,std::size_t end) {
        std::size_t count{};for(std::size_t i=0;i<end;++i) {
            if(entries[i].kind!=PartHistoryKind::Feature)continue;
            const auto* feature=doc.find_container(entries[i].id);
            if(feature&&feature->feature_kind!=FeatureKind::Sketch)++count;
        }return count;
    };
    if(!record.body_id.empty()) {
        const auto* body=doc.body_history.find(record.body_id);
        if(!body)throw std::invalid_argument("The measured body is missing.");
        const auto count=operations(body->entries,boundary(body->entries,record.after_object_id));
        const auto found=calculated.back().body_boundaries.find(record.body_id);
        if(!count)throw std::invalid_argument("There is no solid at this history position.");
        if(found==calculated.back().body_boundaries.end()||count>found->second.size())
            throw std::invalid_argument("Regenerate the model before measuring body properties.");
        out.push_back({&found->second[count-1],body->scope.translation(),body->scope.rotation_degrees()});
    } else if(!doc.body_history.bodies().empty()) {
        const auto end=boundary(doc.body_history.order(),record.after_object_id);
        for(const auto& id:doc.body_history.available_before(end)) {
            const auto found=calculated.back().body_outputs.find(id);
            if(found==calculated.back().body_outputs.end()) {
                const auto* body=doc.body_history.find(id);
                if(body&&operations(body->entries,body->entries.size())==0&&!body->derived_copy)continue;
                throw std::invalid_argument("Regenerate the model before measuring body properties.");
            }
            out.push_back({&found->second.get()});
        }
    } else {
        std::size_t count{};
        if(doc.history_order.empty()) {
            const auto end=boundary(doc.history,record.after_object_id);
            for(std::size_t i=0;i<end;++i)if(doc.history[i].feature_kind!=FeatureKind::Sketch)++count;
        }else count=operations(doc.history_order,boundary(doc.history_order,record.after_object_id));
        if(!count)throw std::invalid_argument("There is no solid at this history position.");
        if(count>calculated.size())throw std::invalid_argument("Regenerate the model before measuring body properties.");
        out.push_back({&calculated[count-1]});
    }
    return out;
}
BodyProperties evaluate_body_properties(const PartDocument& doc,const std::vector<kernel::BodyResult>& calculated,BodyProperties row) {
    row.volume=0;row.area=0;row.integrals.reset();row.surface_centroid.reset();row.error.clear();row.density_kg_mm3=material_density_kg_mm3(doc);
    try {
        const auto inputs=body_properties_inputs(doc,calculated,row);
        struct Weighted {double volume;kernel::VolumeIntegrals integrals;};std::vector<Weighted> values;
        kernel::Vec3 center{};
        kernel::Vec3 surface_sum{};bool surface_complete=true;
        for(const auto& input:inputs) {
            const auto& body=*input.body;
            if(!body.calculation_errors.empty())throw std::invalid_argument("The measured history contains a failed calculation.");
            const double v=std::abs(body.volume);row.area+=std::abs(body.surface_area);
            if(body.surface_area>0) {
                if(body.surface_centroid) {
                    auto c=kernel::inertia_transform(kernel::inertia_frame(input.rotation),*body.surface_centroid);
                    c.x+=input.translation.x;c.y+=input.translation.y;c.z+=input.translation.z;
                    surface_sum.x+=body.surface_area*c.x;surface_sum.y+=body.surface_area*c.y;surface_sum.z+=body.surface_area*c.z;
                }else surface_complete=false;
            }
            if(v<=0)continue;
            if(!body.volume_integrals)throw std::invalid_argument("Regenerate the model before measuring body properties.");
            auto p=*body.volume_integrals;const auto r=kernel::inertia_frame(input.rotation);
            p.centroid=kernel::inertia_transform(r,p.centroid);
            p.centroid.x+=input.translation.x;p.centroid.y+=input.translation.y;p.centroid.z+=input.translation.z;
            p.inertia=kernel::inertia_rotate(p.inertia,r);
            values.push_back({v,p});row.volume+=v;
            center.x+=v*p.centroid.x;center.y+=v*p.centroid.y;center.z+=v*p.centroid.z;
        }
        if(!(row.volume>0)) {
            if(row.area>0&&surface_complete) {
                row.surface_centroid=kernel::Vec3{surface_sum.x/row.area,surface_sum.y/row.area,surface_sum.z/row.area};
                return row;
            }
            throw std::invalid_argument("There is no solid at this history position.");
        }
        center.x/=row.volume;center.y/=row.volume;center.z/=row.volume;
        kernel::VolumeIntegrals total;total.centroid=center;
        for(const auto& p:values) {
            for(int i=0;i<9;++i)total.inertia[i]+=p.integrals.inertia[i];
            kernel::inertia_shift(total.inertia,p.volume,{p.integrals.centroid.x-center.x,p.integrals.centroid.y-center.y,p.integrals.centroid.z-center.z});
        }
        row.integrals=total;
    }catch(const std::exception& error){row.volume=0;row.area=0;row.integrals.reset();row.error=error.what();}
    return row;
}
void refresh_body_properties(PartDocument& doc,const std::vector<kernel::BodyResult>& calculated) {
    for(auto& row:doc.body_properties)row=evaluate_body_properties(doc,calculated,std::move(row));
}
kernel::ViewerMesh body_properties_origin(const BodyProperties& row,const std::string& label) {
    if(!row.centroid()||!row.error.empty()||!row.visible)return {};
    PartDocument carrier;carrier.document_id=row.id;carrier.name=row.name;
    auto mesh=carrier.origin_viewer_mesh();const auto r=kernel::inertia_frame(row.rotation_degrees);const auto c=*row.centroid();
    for(auto& p:mesh.points)p.label=label;
    const auto point=[&](kernel::Vec3& p){p=kernel::inertia_transform(r,p);p.x+=c.x;p.y+=c.y;p.z+=c.z;};
    const auto transform=[&](auto& geometry) {
        for(auto& p:geometry.vertices)point(p);
        for(auto& e:geometry.edges)for(auto& p:e.points)point(p);
        for(auto& p:geometry.points)point(p.position);
        for(auto& a:geometry.axes){point(a.point);a.direction=kernel::inertia_transform(r,a.direction);}
    };
    transform(mesh);transform(mesh.original_references);return mesh;
}
kernel::ViewerMesh body_properties_origins(const PartDocument& doc,const std::string& label) {
    kernel::ViewerMesh result;
    for(const auto& row:doc.body_properties) {
        try {
            const auto& active=doc.body_history.active_body_id();
            if(!active.empty()&&row.body_id!=active)continue;
            if(!row.body_id.empty()) {
                const auto* body=doc.body_history.find(row.body_id);if(!body)continue;
                if(!active.empty()&&boundary(body->entries,row.after_object_id)>body->cursor)continue;
            }else if(!doc.body_history.bodies().empty()) {
                if(boundary(doc.body_history.order(),row.after_object_id)>doc.body_history.insertion_cursor())continue;
            }else if(!doc.history_order.empty()) {
                if(boundary(doc.history_order,row.after_object_id)>doc.effective_history_cursor())continue;
            }
            auto mesh=body_properties_origin(row,label);
            result.points.insert(result.points.end(),mesh.points.begin(),mesh.points.end());
            result.axes.insert(result.axes.end(),mesh.axes.begin(),mesh.axes.end());
            result.edges.insert(result.edges.end(),mesh.edges.begin(),mesh.edges.end());
            // Analysis origins are read-only display frames. They are not
            // placement inputs: a feature depending on its own measured body
            // would otherwise introduce an implicit dependency cycle.
        }catch(const std::exception&){} // Missing anchors are represented by red tree rows.
    }return result;
}
std::string serialize_body_properties(const std::vector<BodyProperties>& rows) {
    auto out=Json::array();for(const auto& r:rows)out.push_back({{"id",r.id},{"name",r.name},{"body_id",r.body_id},
        {"after_object_id",r.after_object_id},{"rotation_degrees",vector(r.rotation_degrees)},{"visible",r.visible},
        {"volume_mm3",r.volume},{"area_mm2",r.area},{"density_kg_mm3",r.density_kg_mm3?Json(*r.density_kg_mm3):Json(nullptr)},
        {"surface_centroid_mm",r.surface_centroid?vector(*r.surface_centroid):Json(nullptr)},
        {"integrals",r.integrals?Json{{"centroid_mm",vector(r.integrals->centroid)},{"central_inertia_mm5",r.integrals->inertia}}:Json(nullptr)},
        {"error",r.error}});return out.dump();
}
std::vector<BodyProperties> parse_body_properties(const std::string& source) {
    const auto data=Json::parse(source);if(!data.is_array())throw std::invalid_argument("Invalid body properties records.");
    std::vector<BodyProperties> rows;std::set<std::string> ids,names;
    for(const auto& v:data) {
        BodyProperties r;r.id=v.at("id");r.name=v.at("name");r.body_id=v.at("body_id");r.after_object_id=v.at("after_object_id");
        if(r.id.empty()||r.name.empty()||!ids.insert(r.id).second||!names.insert(r.name).second)
            throw std::invalid_argument("Body properties identities and names must be unique.");
        validate_native_metadata_text(r.id);validate_native_metadata_text(r.name);
        r.rotation_degrees=vector(v.at("rotation_degrees"));r.visible=v.at("visible");
        r.volume=v.at("volume_mm3");r.area=v.at("area_mm2");finite(r.volume);finite(r.area);
        if(!v.at("surface_centroid_mm").is_null())r.surface_centroid=vector(v.at("surface_centroid_mm"));
        if(r.volume<0||r.area<0)throw std::invalid_argument("Invalid body properties measures.");
        if(!v.at("density_kg_mm3").is_null()){r.density_kg_mm3=v.at("density_kg_mm3").get<double>();finite(*r.density_kg_mm3);if(*r.density_kg_mm3<=0)throw std::invalid_argument("Invalid material density.");}
        if(!v.at("integrals").is_null()) {
            kernel::VolumeIntegrals p;p.centroid=vector(v.at("integrals").at("centroid_mm"));
            p.inertia=v.at("integrals").at("central_inertia_mm5").get<kernel::Matrix3>();for(double x:p.inertia)finite(x);r.integrals=p;
        }
        r.error=v.at("error");rows.push_back(std::move(r));
    }return rows;
}
} // namespace zima::document
