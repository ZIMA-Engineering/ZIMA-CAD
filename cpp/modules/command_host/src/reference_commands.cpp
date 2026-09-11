#include <zima/command_host/host.hpp>
#include <zima/workspace/reference_sources.hpp>
#include <set>
#include <tuple>
#include <algorithm>

namespace zima::command_host {
namespace {
using Frame=workspace::ReferenceFrame;
using Geometry=kernel::ViewerReferenceGeometry;
using Key=std::tuple<std::string,std::string,std::string,std::string>;
Json vec(kernel::Vec3 p){return Json::array({p.x,p.y,p.z});}
std::size_t size_arg(const Json& args,const char* key,std::size_t fallback,std::size_t maximum,bool positive=false) {
    if(!args.contains(key))return fallback;
    if(args[key]<0 || args[key].get<std::uint64_t>()>maximum || (positive && args[key]==0))
        throw workspace::ReferenceQueryError("invalid_arguments","Reference pagination is outside the supported range.");
    return args[key].get<std::size_t>();
}
void kind_check(const std::string& kind,bool optional) {
    if((optional && kind.empty()) || kind=="face" || kind=="edge" || kind=="point" || kind=="axis")return;
    throw workspace::ReferenceQueryError("invalid_arguments","Reference kind must be face, edge, point or axis.");
}
template<class Ref> Key key(const char* kind,const Ref& ref,const Frame& frame) {
    return {kind,ref.owner_id,ref.semantic_key,frame.path(ref.instance_path)};
}
Json identity(const Key& key) {
    return {{"kind",std::get<0>(key)},{"owner",std::get<1>(key)},{"key",std::get<2>(key)},{"instance_path",std::get<3>(key)}};
}
std::uint64_t revision(const workspace::Workspace& live,const std::string& id) {
    if(const auto* part=live.open_part(id))return part->session.revision();
    if(const auto* assembly=live.open_assembly(id))return assembly->session.revision();
    throw workspace::ReferenceQueryError("unsupported_document","Reference queries require an open Part or Assembly.");
}
Json surface(const kernel::SurfaceGeometry& source,const Frame& frame,const std::string& local) {
    using Kind=kernel::SurfaceGeometry::Kind;
    return {{"kind",source.kind==Kind::Plane?"plane":source.kind==Kind::Cylinder?"cylinder":"cone"},
        {"origin",vec(frame.surface_point(local,source.origin))},{"axis",vec(frame.surface_direction(local,source.axis))},
        {"radial",vec(frame.surface_direction(local,source.radial))},{"radius_mm",source.radius},
        {"semi_angle_radians",source.semi_angle},{"axial_min_mm",source.axial_min},
        {"axial_max_mm",source.axial_max},{"reversed",source.reversed}};
}
Json details(const Geometry& geometry,const Frame& frame,const Key& wanted,std::size_t limit) {
    const auto& kind=std::get<0>(wanted);Json result=identity(wanted);std::size_t count=0;bool found=false;
    result["coordinate_system"]="document";result["length_unit"]="mm";
    result["source_occurrence_visible"]=frame.visible;result["source_occurrence_suppressed"]=frame.suppressed;
    if(kind=="face") {
        Json triangles=Json::array();
        for(std::size_t i=0;i<geometry.triangle_references.size();++i) {
            const auto& ref=geometry.triangle_references[i];if(key("face",ref,frame)!=wanted)continue;
            if(!found) {result["surface"]=ref.surface?surface(*ref.surface,frame,ref.instance_path):Json(nullptr);result["area_mm2"]=ref.measured_area?Json(*ref.measured_area):Json(nullptr);found=true;}
            ++count;if(count>limit)continue;
            if(i*3+2>=geometry.triangles.size())throw workspace::ReferenceQueryError("invalid_reference_geometry","The persisted reference geometry is incomplete.");
            Json triangle=Json::array();for(std::size_t j=0;j<3;++j) {
                const auto index=geometry.triangles[i*3+j];if(index>=geometry.vertices.size())throw workspace::ReferenceQueryError("invalid_reference_geometry","The persisted reference geometry is incomplete.");
                triangle.push_back(vec(frame.point(geometry.vertices[index])));
            }
            triangles.push_back(std::move(triangle));
        }
        result["triangles"]=std::move(triangles);result["triangle_count"]=count;
    } else if(kind=="edge") {
        Json segments=Json::array();std::size_t used=0,spline_used=0,segment_count=0;
        for(const auto& edge:geometry.edges) {
            if(key("edge",edge.reference,frame)!=wanted)continue;
            found=true;count+=edge.points.size();++segment_count;
            if(segments.size()>=limit)continue;
            Json segment={{"point_count",edge.points.size()},{"measured_length_mm",edge.measured_length?Json(*edge.measured_length):Json(nullptr)},
                {"parameter_seam",edge.parameter_seam},{"infinite",edge.infinite}};
            Json points=Json::array();for(const auto& p:edge.points)if(used<limit){points.push_back(vec(frame.point(p)));++used;}
            segment["points"]=std::move(points);segment["exact_spline"]=nullptr;
            if(edge.exact_spline) {
                const auto& spline=*edge.exact_spline;
                segment["spline_pole_count"]=spline.poles.size();
                const auto spline_size=spline.poles.size()+spline.knots.size()+spline.weights.size();
                if(spline_size<=limit-spline_used) {
                    spline_used+=spline_size;
                    Json poles=Json::array();for(const auto& p:spline.poles)poles.push_back(vec(frame.point(p)));
                    segment["exact_spline"]={{"degree",spline.degree},{"poles",std::move(poles)},{"weights",spline.weights},{"knots",spline.knots}};
                } else segment["spline_omitted_by_limit"]=true;
            }
            segments.push_back(std::move(segment));
        }
        result["segments_truncated"]=segment_count>segments.size();result["segment_count"]=segment_count;
        result["segments"]=std::move(segments);result["point_count"]=count;
    } else if(kind=="point") {
        for(const auto& point:geometry.points)if(key("point",point.reference,frame)==wanted) {
            found=true;result["position"]=vec(frame.point(point.position));break;
        }
    } else {
        for(const auto& axis:geometry.axes)if(key("axis",axis.reference,frame)==wanted) {
            found=true;result["point"]=vec(frame.point(axis.point));result["direction"]=vec(frame.direction(axis.direction));break;
        }
    }
    if(!found)return nullptr;
    result["sample_limit"]=limit;result["samples_truncated"]=count>limit;return result;
}
}
void Host::register_reference_commands() {
    dispatcher_.add({"reference.list",tr("List persisted original faces, edges, points and axes without calculation."),
        {{"owner",false},{"kind",false},{"instance_path",false},{"document",false},
         {"offset",false,commands::ArgumentType::Integer},{"limit",false,commands::ArgumentType::Integer}},false},[this](const Json& args) {
        try {
            const auto id=args.value("document",workspace_.active_document_id());const auto rev=revision(workspace_,id);
            const auto kind=args.value("kind",std::string{});kind_check(kind,true);
            const auto offset=size_arg(args,"offset",0,100000000);const auto limit=size_arg(args,"limit",500,5000,true);
            std::set<Key> seen;Json items=Json::array();std::size_t total=0;
            workspace::visit_original_references(workspace_,id,[&](const Geometry& geometry,const Frame& frame) {
                const auto add=[&](const char* type,const auto& ref) {
                    if(!ref.valid() || (!kind.empty() && kind!=type) || (args.contains("owner") && args["owner"]!=ref.owner_id))return;
                    auto value=key(type,ref,frame);
                    if((args.contains("instance_path") && args["instance_path"]!=std::get<3>(value)) || !seen.insert(value).second)return;
                    if(total>=offset && items.size()<limit)items.push_back(identity(value));++total;
                };
                for(const auto& ref:geometry.triangle_references)add("face",ref);
                for(const auto& edge:geometry.edges)add("edge",edge.reference);
                for(const auto& point:geometry.points)add("point",point.reference);
                for(const auto& axis:geometry.axes)add("axis",axis.reference);
                return true;
            });
            const bool more=offset<total && items.size()<total-offset;
            return Result::success({{"document",id},{"revision",rev},{"items",std::move(items)},
                {"offset",offset},{"total",total},{"more",more},{"next_offset",more?Json(offset+limit):Json(nullptr)}});
        } catch(const workspace::ReferenceQueryError& error){return Result::failure(error.code,tr(error.what()));}
    });
    dispatcher_.add({"reference.get",tr("Read one original reference and its bounded persisted geometry in document coordinates."),
        {{"kind",true},{"owner",true},{"key",true},{"instance_path",false},{"document",false},{"limit",false,commands::ArgumentType::Integer}},false},[this](const Json& args) {
        try {
            const auto id=args.value("document",workspace_.active_document_id());const auto rev=revision(workspace_,id);
            const auto kind=args["kind"].get<std::string>();kind_check(kind,false);
            const Key wanted{kind,args["owner"].get<std::string>(),args["key"].get<std::string>(),args.value("instance_path",std::string{})};
            const auto limit=size_arg(args,"limit",256,10000,true);Json result=nullptr;
            workspace::visit_original_references(workspace_,id,[&](const Geometry& geometry,const Frame& frame) {
                result=details(geometry,frame,wanted,limit);return result.is_null();
            });
            if(result.is_null())return Result::failure("reference_not_found",tr("The exact original reference was not found in the stored document state."));
            result["document"]=id;result["revision"]=rev;return Result::success(std::move(result));
        } catch(const workspace::ReferenceQueryError& error){return Result::failure(error.code,tr(error.what()));}
    });
}
}
