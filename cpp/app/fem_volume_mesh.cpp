#include "fem_volume_mesh.hpp"

#include <zima/kernel/calculated_body_shape.hpp>
#include <BRepBuilderAPI_MakeVertex.hxx>
#include <BRepBuilderAPI_Copy.hxx>
#include <BRepExtrema_DistShapeShape.hxx>
#include <BRepClass3d_SolidClassifier.hxx>
#include <BRep_Tool.hxx>
#include <NCollection_IndexedMap.hxx>
#include <TopExp.hxx>
#include <TopExp_Explorer.hxx>
#include <TopTools_ShapeMapHasher.hxx>
#include <TopoDS.hxx>
#include <gmsh.h>
#include <algorithm>
#include <cmath>
#include <limits>
#include <mutex>
#include <set>
#include <stdexcept>
#include <tuple>
#include <unordered_map>

static_assert(GMSH_API_VERSION_MAJOR==4&&GMSH_API_VERSION_MINOR==15&&GMSH_API_VERSION_PATCH==2);

namespace zima::app {
namespace {
class GmshSession {
public:
    GmshSession(){gmsh::initialize(0,nullptr,false);}
    ~GmshSession(){try{gmsh::finalize();}catch(...){}}
    GmshSession(const GmshSession&)=delete;
    GmshSession& operator=(const GmshSession&)=delete;
};

[[noreturn]] void invalid_mesh() {
    throw std::runtime_error("The FEM generator returned an invalid mesh.");
}
}

FemVolumeMesh generate_fem_volume_mesh(
        const kernel::BodyResult& source,const document::FemDefinition& authored_definition,
        const FemProgressCallback& progress) {
    // Only the selected analysis binds regions. Inspection points sample the
    // solution, while loads for another analysis must not partition its mesh.
    auto definition=authored_definition;
    std::erase_if(definition.conditions,[&](const auto& condition){
        return !document::fem_condition_applies_to_analysis(condition.kind,definition.analysis,document::fem_thermal_expansion(definition));
    });
    report_fem_progress(progress,FemProgressPhase::PreparingGeometry);
    static std::mutex mutex;
    const std::lock_guard lock(mutex);
    const auto& controls=definition.mesh;
    double maximum_size=controls.maximum_size_mm;
    for(const auto& face:source.mesh.triangle_references)
        if(face.sheet_thickness>0)
            maximum_size=std::min(maximum_size,face.sheet_thickness/controls.elements_through_thickness);
    if(maximum_size<controls.minimum_size_mm)
        throw std::invalid_argument("Invalid FEM volume mesh controls.");
    try {
        std::vector<kernel::CalculatedTopologyReference> references;
        std::vector<std::size_t> reference_offsets;
        for(const auto& condition:definition.conditions) {
            reference_offsets.push_back(references.size());
            for(const auto& ref:condition.references) {
                if(condition.projected_sketch||condition.region==document::FemRegionKind::Surface)references.emplace_back(kernel::FaceReference{ref.owner_id,ref.semantic_key,ref.instance_path});
                else if(condition.region==document::FemRegionKind::Curve)references.emplace_back(kernel::EdgeReference{ref.owner_id,ref.semantic_key,ref.instance_path});
                else references.emplace_back(kernel::VertexReference{ref.owner_id,ref.semantic_key,ref.instance_path});
            }
        }
        // Gmsh can modify imported topology caches. The exact source snapshot
        // continues to belong to CAD and is reused unchanged by other views.
        auto native=kernel::read_calculated_body_topology(source,references);
        std::vector<kernel::ProjectedMeshRegion> projections(definition.conditions.size());
        const auto project=[&](std::size_t index) {
            const auto& condition=definition.conditions[index];
            if(!condition.placement.reference_valid)
                throw std::runtime_error("The condition placement is unresolved.");
            const auto request=document::fem_condition_projection_request(condition);
            std::vector<TopoDS_Shape> faces;
            for(std::size_t row=0;row<condition.references.size();++row) {
                const auto& target=native.referenced_shapes[reference_offsets[index]+row];
                faces.insert(faces.end(),target.begin(),target.end());
            }
            return kernel::project_calculated_mesh_region(native.shape,request,faces,
                condition.projected_sketch->surface_seeds,condition.projected_sketch->surface_region_keys);
        };
        for(std::size_t index=0;index<definition.conditions.size();++index) {
            const auto& condition=definition.conditions[index];
            if(condition.projected_sketch&&condition.projected_sketch->project_to_body)
                projections[index]=project(index);
        }
        BRepBuilderAPI_Copy copy(native.shape,true,false);
        auto input=copy.Shape();
        const GmshSession session;
        gmsh::option::setNumber("General.Terminal",0);
        gmsh::option::setNumber("General.NumThreads",1);
        gmsh::option::setNumber("Mesh.MeshSizeMin",controls.minimum_size_mm);
        gmsh::option::setNumber("Mesh.MeshSizeMax",maximum_size);
        gmsh::option::setNumber("Mesh.ElementOrder",static_cast<int>(controls.order));
        gmsh::model::add("fem");
        gmsh::vectorpair imported;
        gmsh::model::occ::importShapesNativePointer(&input,imported,true);
        std::erase_if(imported,[](const auto& entity){return entity.first!=3;});
        if(imported.empty())invalid_mesh();
        struct Region {std::string id;int dimension;std::vector<int> tags;};
        std::vector<Region> regions;
        bool partition_regions=false;
        struct Embedding {int dimension,tag,support_dimension,support;};
        std::vector<Embedding> embeddings;
        std::map<std::tuple<double,double,double>,int> embedded_origins;
        const auto import=[&](const TopoDS_Shape& member,int dimension) {
            gmsh::vectorpair entities;
            gmsh::model::occ::importShapesNativePointer(&member,entities,true);
            if(entities.size()!=1||entities.front().first!=dimension)
                throw std::runtime_error("The condition geometry is unavailable on the FEM source.");
            return entities.front().second;
        };
        const auto bind=[&](const TopoDS_Shape& shape,int dimension) {return import(copy.ModifiedShape(shape),dimension);};
        for(std::size_t index=0;index<definition.conditions.size();++index) {
            const auto& condition=definition.conditions[index];
            if(!condition.placement.reference_valid)
                throw std::runtime_error("The condition placement is unresolved.");
            Region region{condition.id,static_cast<int>(condition.region),{}};
            if(condition.projected_sketch&&!condition.projected_sketch->project_to_body) {
                partition_regions=true;
                for(const auto& shape:kernel::planar_calculated_mesh_region(native.shape,
                        document::fem_condition_planar_request(condition)))
                    region.tags.push_back(import(shape,region.dimension));
            }else if(condition.projected_sketch) {
                // Resolve each authored patch against the unchanged source.
                // Fragment all patches together below: a preceding partition
                // must not split the next condition's single face reference
                // into multiple targets with only one retained-region seed.
                if(!projections[index].faces.empty())partition_regions=true;
                for(const auto& face:projections[index].faces)region.tags.push_back(import(face,2));
                for(const auto& curve:projections[index].curves) {
                    const int edge=import(curve.edge,1);
                    const int face=bind(curve.face,2);
                    embeddings.push_back({1,edge,2,face});
                    region.tags.push_back(edge);
                }
            }else if(condition.references.empty()&&condition.region==document::FemRegionKind::Point) {
                const auto& p=condition.placement;
                const gp_Pnt origin(p.x,p.y,p.z);
                const auto key=std::tuple{p.x,p.y,p.z};
                if(const auto existing=embedded_origins.find(key);existing!=embedded_origins.end())
                    region.tags.push_back(existing->second);
                constexpr double coincidence=1e-7;
                if(region.tags.empty())
                    for(TopExp_Explorer vertex(native.shape,TopAbs_VERTEX);vertex.More();vertex.Next()) {
                        const auto point=TopoDS::Vertex(vertex.Current());
                        if(BRep_Tool::Pnt(point).Distance(origin)<=coincidence)
                            region.tags.push_back(bind(point,0));
                    }
                if(region.tags.empty()) {
                    const auto exact_point=BRepBuilderAPI_MakeVertex(origin).Vertex();
                    NCollection_IndexedMap<TopoDS_Shape,TopTools_ShapeMapHasher> faces;
                    TopExp::MapShapes(native.shape,TopAbs_FACE,faces);
                    std::vector<TopoDS_Shape> supports;
                    for(int i=1;i<=faces.Extent();++i) {
                        BRepExtrema_DistShapeShape distance(exact_point,faces(i));
                        if(distance.IsDone()&&distance.Value()<=coincidence)supports.push_back(faces(i));
                    }
                    int dimension=2;
                    if(supports.empty()) {
                        dimension=3;
                        for(TopExp_Explorer solid(native.shape,TopAbs_SOLID);solid.More();solid.Next()) {
                            BRepClass3d_SolidClassifier classifier(solid.Current(),origin,coincidence);
                            if(classifier.State()==TopAbs_IN)supports.push_back(solid.Current());
                        }
                    }
                    if(supports.size()==1) {
                        const int entity=bind(supports.front(),dimension);
                        const int point=gmsh::model::occ::addPoint(p.x,p.y,p.z);
                        embeddings.push_back({0,point,dimension,entity});
                        embedded_origins.emplace(key,point);
                        region.tags.push_back(point);
                    }
                }
            }else for(std::size_t i=0;i<condition.references.size();++i)
                for(const auto& shape:native.referenced_shapes.at(reference_offsets[index]+i))
                    region.tags.push_back(bind(shape,region.dimension));
            if(region.tags.empty())throw std::runtime_error("The condition geometry is unavailable on the FEM source.");
            std::ranges::sort(region.tags);
            region.tags.erase(std::unique(region.tags.begin(),region.tags.end()),region.tags.end());
            regions.push_back(std::move(region));
        }
        if(partition_regions) {
            // Fragment private mesh inputs together so that planar/projected
            // regions, including internal ones, share the actual mesh nodes.
            // The output map preserves every condition's exact entity group.
            gmsh::vectorpair region_entities;
            for(const auto& region:regions)for(int tag:region.tags) {
                const std::pair entity{region.dimension,tag};
                if(std::ranges::find(region_entities,entity)==region_entities.end())
                    region_entities.push_back(entity);
            }
            gmsh::vectorpair fragments;
            std::vector<gmsh::vectorpair> mapping;
            // Gmsh preserves boundary correspondence only when the lower
            // dimensional inputs precede their owning volumes.
            std::ranges::sort(region_entities);
            gmsh::model::occ::fragment(region_entities,imported,fragments,mapping);
            if(mapping.size()!=imported.size()+region_entities.size())invalid_mesh();
            for(auto& region:regions) {
                std::vector<int> tags;
                for(int tag:region.tags) {
                    const auto found=std::ranges::find(region_entities,std::pair{region.dimension,tag});
                    const auto index=std::size_t(found-region_entities.begin());
                    for(const auto& [dimension,replacement]:mapping[index])
                        if(dimension==region.dimension)tags.push_back(replacement);
                }
                std::ranges::sort(tags);
                tags.erase(std::unique(tags.begin(),tags.end()),tags.end());
                if(tags.empty())invalid_mesh();
                region.tags=std::move(tags);
            }
            imported=std::move(fragments);
            std::erase_if(imported,[](const auto& entity){return entity.first!=3;});
            if(imported.empty())invalid_mesh();
            embeddings.clear();
        }
        gmsh::model::occ::synchronize();
        for(const auto& embedding:embeddings)
            gmsh::model::mesh::embed(embedding.dimension,{embedding.tag},embedding.support_dimension,embedding.support);
        std::vector<int> physical_groups;
        for(const auto& region:regions)
            physical_groups.push_back(gmsh::model::addPhysicalGroup(region.dimension,region.tags));
        report_fem_progress(progress,FemProgressPhase::GeneratingMesh);
        gmsh::model::mesh::generate(3);

        // Curving the midside nodes onto CAD can invert an otherwise valid
        // linear tetrahedron. Check the complete high-order Jacobian before
        // publishing the mesh, and untangle only when that check requires it.
        report_fem_progress(progress,FemProgressPhase::CheckingMesh);
        if(controls.order==document::FemElementOrder::Quadratic) {
            std::vector<std::size_t> cells,connectivity;
            gmsh::model::mesh::getElementsByType(11,cells,connectivity);
            const auto valid=[&] {
                std::vector<double> minimum;
                gmsh::model::mesh::getElementQualities(cells,minimum,"minDetJac");
                return !cells.empty()&&minimum.size()==cells.size()&&
                    std::ranges::all_of(minimum,[](double value){return std::isfinite(value)&&value>0;});
            };
            if(!valid()) {
                report_fem_progress(progress,FemProgressPhase::OptimizingMesh);
                gmsh::model::mesh::optimize("HighOrder");
                report_fem_progress(progress,FemProgressPhase::CheckingMesh);
                if(!valid())throw std::runtime_error("The FEM mesh contains an invalid tetrahedron.");
            }
        }

        report_fem_progress(progress,FemProgressPhase::ReadingMesh);
        FemVolumeMesh result;
        result.order=controls.order;
        auto& viewer=result.surface;
        std::vector<std::size_t> node_tags;
        std::vector<double> coordinates,parameters;
        gmsh::model::mesh::getNodes(node_tags,coordinates,parameters,-1,-1,false,false);
        if(node_tags.empty()||node_tags.size()>std::numeric_limits<std::uint32_t>::max()||
                coordinates.size()!=node_tags.size()*3)invalid_mesh();
        std::unordered_map<std::size_t,std::uint32_t> indices;
        indices.reserve(node_tags.size());
        viewer.vertices.reserve(node_tags.size());
        for(std::size_t i=0;i<node_tags.size();++i) {
            const auto x=coordinates[i*3],y=coordinates[i*3+1],z=coordinates[i*3+2];
            if(!std::isfinite(x)||!std::isfinite(y)||!std::isfinite(z)||
                    !indices.emplace(node_tags[i],static_cast<std::uint32_t>(i)).second)invalid_mesh();
            viewer.vertices.push_back({x,y,z});
        }
        const auto node_index=[&](std::size_t tag) {
            const auto found=indices.find(tag);
            if(found==indices.end())invalid_mesh();
            return found->second;
        };
        const bool quadratic=controls.order==document::FemElementOrder::Quadratic;
        std::vector<int> types;
        std::vector<std::vector<std::size_t>> elements,nodes;
        const std::array<int,3> region_types{15,quadratic?8:1,quadratic?9:2};
        const std::array<std::size_t,3> region_sizes{1,quadratic?3u:2u,quadratic?6u:3u};
        for(std::size_t i=0;i<regions.size();++i) {
            const auto& region=regions[i];
            const auto dimension=static_cast<std::size_t>(region.dimension);
            FemMeshConditionRegion group{static_cast<document::FemRegionKind>(dimension),{},{}};
            gmsh::model::mesh::getNodesForPhysicalGroup(region.dimension,physical_groups[i],node_tags,coordinates);
            if(node_tags.empty())invalid_mesh();
            for(const auto tag:node_tags)group.nodes.push_back(node_index(tag));
            std::ranges::sort(group.nodes);
            group.nodes.erase(std::unique(group.nodes.begin(),group.nodes.end()),group.nodes.end());
            std::set<std::size_t> element_tags;
            for(const auto entity:region.tags) {
                gmsh::model::mesh::getElements(types,elements,nodes,region.dimension,entity);
                if(types.empty())invalid_mesh();
                for(std::size_t block=0;block<types.size();++block) {
                    const auto size=region_sizes[dimension];
                    if(types[block]!=region_types[dimension]||nodes[block].size()!=elements[block].size()*size)invalid_mesh();
                    for(std::size_t e=0;e<elements[block].size();++e) {
                        if(!element_tags.insert(elements[block][e]).second)continue;
                        std::array<std::uint32_t,6> element{};
                        for(std::size_t j=0;j<size;++j) {
                            element[j]=node_index(nodes[block][e*size+j]);
                            if(!std::ranges::binary_search(group.nodes,element[j]))invalid_mesh();
                        }
                        group.elements.push_back(element);
                    }
                }
            }
            if(group.elements.empty()||!result.condition_regions.emplace(region.id,std::move(group)).second)invalid_mesh();
        }

        gmsh::model::mesh::getElements(types,elements,nodes,3);
        const std::size_t tetrahedron_size=quadratic?10:4;
        for(std::size_t block=0;block<types.size();++block) {
            if(types[block]!=(quadratic?11:4)||nodes[block].size()!=elements[block].size()*tetrahedron_size)invalid_mesh();
            for(std::size_t offset=0;offset<nodes[block].size();offset+=tetrahedron_size) {
                std::array<std::uint32_t,10> tetrahedron{};
                for(std::size_t i=0;i<tetrahedron_size;++i)tetrahedron[i]=node_index(nodes[block][offset+i]);
                result.tetrahedra.push_back(tetrahedron);
            }
        }
        if(result.tetrahedra.empty())invalid_mesh();
        std::set<std::array<std::uint32_t,3>> unbound_faces;
        std::set<std::array<std::uint32_t,2>> unbound_edges;
        std::set<std::uint32_t> unbound_nodes;
        const auto ordered=[](auto values){std::ranges::sort(values);return values;};
        for(const auto& [id,group]:result.condition_regions) {
            unbound_nodes.insert(group.nodes.begin(),group.nodes.end());
            for(const auto& element:group.elements) {
                if(group.kind==document::FemRegionKind::Surface)
                    unbound_faces.insert(ordered(std::array{element[0],element[1],element[2]}));
                if(group.kind==document::FemRegionKind::Curve)
                    unbound_edges.insert(ordered(std::array{element[0],element[1]}));
            }
        }
        for(const auto& cell:result.tetrahedra) {
            if(unbound_nodes.empty()&&unbound_faces.empty()&&unbound_edges.empty())break;
            for(std::size_t vertex=0;vertex<tetrahedron_size;++vertex)unbound_nodes.erase(cell[vertex]);
            for(unsigned a=0;a<4;++a)for(unsigned b=a+1;b<4;++b) {
                unbound_edges.erase(ordered(std::array{cell[a],cell[b]}));
                for(unsigned c=b+1;c<4;++c)
                    unbound_faces.erase(ordered(std::array{cell[a],cell[b],cell[c]}));
            }
        }
        if(!unbound_nodes.empty()||!unbound_faces.empty()||!unbound_edges.empty())
            throw std::runtime_error("The condition geometry is unavailable on the FEM source.");

        std::set<std::pair<std::uint32_t,std::uint32_t>> drawn_edges;
        const auto add_edge=[&](std::uint32_t a,std::uint32_t b,
                std::optional<std::uint32_t> midpoint) {
            if(!drawn_edges.emplace(std::min(a,b),std::max(a,b)).second)return;
            kernel::ViewerEdge edge;
            edge.points.push_back(viewer.vertices[a]);
            if(midpoint)edge.points.push_back(viewer.vertices[*midpoint]);
            edge.points.push_back(viewer.vertices[b]);
            viewer.edges.push_back(std::move(edge));
        };
        gmsh::vectorpair exterior;
        gmsh::model::getBoundary(imported,exterior,true,false,false);
        std::map<std::array<std::uint32_t,3>,std::pair<std::size_t,unsigned>> face_cells;
        for(std::size_t i=0;i<result.tetrahedra.size();++i) {
            const auto& cell=result.tetrahedra[i];
            for(unsigned a=0;a<4;++a)for(unsigned b=a+1;b<4;++b)for(unsigned c=b+1;c<4;++c) {
                auto& owner=face_cells[ordered(std::array{cell[a],cell[b],cell[c]})];
                owner.first=i;++owner.second;
            }
        }
        const std::size_t triangle_size=quadratic?6:3;
        for(const auto& [dimension,entity]:exterior) {
            if(dimension!=2)invalid_mesh();
            gmsh::model::mesh::getElements(types,elements,nodes,dimension,entity);
            for(std::size_t block=0;block<types.size();++block) {
                if(types[block]!=(quadratic?9:2)||nodes[block].size()!=elements[block].size()*triangle_size)invalid_mesh();
                for(std::size_t offset=0;offset<nodes[block].size();offset+=triangle_size) {
                    const auto a=node_index(nodes[block][offset]);
                    const auto b=node_index(nodes[block][offset+1]);
                    const auto c=node_index(nodes[block][offset+2]);
                    const auto owner=face_cells.find(ordered(std::array{a,b,c}));
                    if(owner==face_cells.end()||owner->second.second!=1)invalid_mesh();
                    result.surface_elements.insert(result.surface_elements.end(),quadratic?4:1,owner->second.first);
                    if(quadratic) {
                        const auto ab=node_index(nodes[block][offset+3]);
                        const auto bc=node_index(nodes[block][offset+4]);
                        const auto ca=node_index(nodes[block][offset+5]);
                        viewer.triangles.insert(viewer.triangles.end(),{a,ab,ca,ab,b,bc,ca,bc,c,ab,bc,ca});
                        add_edge(a,b,ab);add_edge(b,c,bc);add_edge(c,a,ca);
                    } else {
                        viewer.triangles.insert(viewer.triangles.end(),{a,b,c});
                        add_edge(a,b,{});add_edge(b,c,{});add_edge(c,a,{});
                    }
                }
            }
        }
        if(viewer.triangles.empty())invalid_mesh();
        return result;
    }catch(const std::runtime_error&) {
        throw;
    }catch(const std::invalid_argument&) {
        throw;
    }catch(...) {
        throw std::runtime_error("FEM mesh generation failed.");
    }
}
}
