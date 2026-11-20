// Native Gmsh/OCCT integration probe; not a product command or analysis solver.
#include <gmsh.h>
#include <BRepAlgoAPI_Cut.hxx>
#include <BRepBuilderAPI_Copy.hxx>
#include <BRepBuilderAPI_MakePolygon.hxx>
#include <BRepCheck_Analyzer.hxx>
#include <BRepFilletAPI_MakeFillet.hxx>
#include <BRepGProp.hxx>
#include <BRepOffsetAPI_ThruSections.hxx>
#include <BRepPrimAPI_MakeBox.hxx>
#include <BRepPrimAPI_MakeCylinder.hxx>
#include <BRepTools.hxx>
#include <GProp_GProps.hxx>
#include <TopExp.hxx>
#include <TopoDS.hxx>
#include <TopTools_IndexedMapOfShape.hxx>
#include <gp_Ax2.hxx>
#include <algorithm>
#include <chrono>
#include <cmath>
#include <iostream>
#include <limits>
#include <map>
#include <set>
#include <sstream>
#include <stdexcept>
#include <string>

namespace {
void require(bool value, const std::string& error) {
    if (!value) throw std::runtime_error(error);
}
std::string archive(const TopoDS_Shape& shape) {
    std::ostringstream result;
    BRepTools::Write(shape, result);
    return result.str();
}
TopoDS_Shape twisted_strip() {
    BRepOffsetAPI_ThruSections loft(true, false, 1e-7);
    for (int station = 0; station <= 8; ++station) {
        const double a = station * std::acos(-1.) / 16.;
        BRepBuilderAPI_MakePolygon polygon;
        for (const auto [x, y] : {std::pair{-4., -.5}, {4., -.5}, {4., .5}, {-4., .5}})
            polygon.Add(gp_Pnt(x * std::cos(a) - y * std::sin(a),
                              x * std::sin(a) + y * std::cos(a), 3. * station));
        polygon.Close();
        loft.AddWire(polygon.Wire());
    }
    loft.Build();
    require(loft.IsDone(), "Twisted fixture loft failed");
    return loft.Shape();
}
TopoDS_Shape rounded_box() {
    auto box = BRepPrimAPI_MakeBox(12., 8., 3.).Shape();
    BRepFilletAPI_MakeFillet fillet(box);
    TopTools_IndexedMapOfShape edges;
    TopExp::MapShapes(box, TopAbs_EDGE, edges);
    for (int i = 1; i <= edges.Extent(); ++i) fillet.Add(.6, TopoDS::Edge(edges(i)));
    fillet.Build();
    require(fillet.IsDone(), "Rounded fixture failed");
    return fillet.Shape();
}

std::size_t run(const std::string& name, const TopoDS_Shape& source,
               double size, int order, double allowed_relative_error,
               double maximum_z_span = std::numeric_limits<double>::infinity()) {
    const auto start = std::chrono::steady_clock::now();
    require(BRepCheck_Analyzer(source).IsValid(), "Invalid source: " + name);
    const auto original = archive(source);
    GProp_GProps properties;
    BRepGProp::VolumeProperties(source, properties);
    const auto exact_volume = properties.Mass();
    require(exact_volume > 0, "Non-solid source: " + name);
    // Gmsh may add triangulations/caches to imported topology. Private deep copy
    // protects the source; it is transient mesher input, not a second CAD Body.
    BRepBuilderAPI_Copy copy(source, true, false);
    auto mesh_input = copy.Shape();
    gmsh::model::add(name);
    gmsh::option::setNumber("General.NumThreads", 1);
    gmsh::option::setNumber("Mesh.MeshSizeMin", size);
    gmsh::option::setNumber("Mesh.MeshSizeMax", size);
    gmsh::option::setNumber("Mesh.MeshSizeFromCurvature", 16);
    gmsh::option::setNumber("Mesh.ElementOrder", order);
    gmsh::option::setNumber("Mesh.Algorithm3D", 1);
    gmsh::vectorpair imported;
    gmsh::model::occ::importShapesNativePointer(&mesh_input, imported, true);
    require(!imported.empty(), "No imported solid");

    // Reimporting each exact copied subshape returns its already bound tag.
    // No mesh-neighbour, bounding-box or nearest-face identity inference.
    TopTools_IndexedMapOfShape faces;
    TopExp::MapShapes(source, TopAbs_FACE, faces);
    std::set<int> face_tags;
    for (int i = 1; i <= faces.Extent(); ++i) {
        const auto face = copy.ModifiedShape(faces(i));
        gmsh::vectorpair binding;
        gmsh::model::occ::importShapesNativePointer(&face, binding, true);
        require(binding.size() == 1 && binding[0].first == 2,
                "Missing/ambiguous native face binding");
        require(face_tags.insert(binding[0].second).second, "Collapsed native face identity");
    }
    gmsh::model::occ::synchronize();
    gmsh::vectorpair entities;
    gmsh::model::getEntities(entities, 2);
    require(entities.size() == face_tags.size(), "Unexpected mesher face count");
    gmsh::model::mesh::generate(3);

    std::size_t boundary_elements = 0;
    for (int face : face_tags) {
        std::vector<int> types;
        std::vector<std::vector<std::size_t>> elements, nodes;
        gmsh::model::mesh::getElements(types, elements, nodes, 2, face);
        std::size_t count = 0;
        for (const auto& list : elements) count += list.size();
        require(count > 0, "Source face has no boundary elements");
        boundary_elements += count;
    }
    std::vector<int> types;
    std::vector<std::vector<std::size_t>> elements, element_nodes;
    gmsh::model::mesh::getElements(types, elements, element_nodes, 3);
    require(!types.empty(), "No volume elements");
    std::vector<std::size_t> node_tags;
    std::vector<double> xyz, param;
    gmsh::model::mesh::getNodes(node_tags, xyz, param);
    std::map<std::size_t, double> node_z;
    for (std::size_t i = 0; i < node_tags.size(); ++i) node_z.emplace(node_tags[i], xyz.at(3 * i + 2));
    double volume = 0, minimum_jacobian = std::numeric_limits<double>::max();
    double largest_element_z_span = 0;
    std::size_t count = 0;
    for (std::size_t i = 0; i < types.size(); ++i) {
        require(types[i] == (order == 1 ? 4 : 11), "Unexpected element family/order");
        const std::size_t nodes_per_element = order == 1 ? 4 : 10;
        require(element_nodes[i].size() == nodes_per_element * elements[i].size(),
                "Invalid volume connectivity");
        for (std::size_t element = 0; element < elements[i].size(); ++element) {
            double low = std::numeric_limits<double>::max(), high = -low;
            for (std::size_t node = 0; node < nodes_per_element; ++node) {
                const auto z = node_z.at(element_nodes[i][nodes_per_element * element + node]);
                low = std::min(low, z); high = std::max(high, z);
            }
            largest_element_z_span = std::max(largest_element_z_span, high - low);
        }
        std::vector<double> local, weights, jacobians, determinants, coordinates;
        gmsh::model::mesh::getIntegrationPoints(types[i], "Gauss4", local, weights);
        gmsh::model::mesh::getJacobians(types[i], local, jacobians, determinants, coordinates);
        require(!weights.empty() && determinants.size() == elements[i].size() * weights.size(),
                "Invalid element integration data");
        for (std::size_t j = 0; j < determinants.size(); ++j) {
            const double det = determinants[j];
            require(std::isfinite(det) && det > 0, "Inverted or degenerate element");
            minimum_jacobian = std::min(minimum_jacobian, det);
            volume += det * weights[j % weights.size()];
        }
        count += elements[i].size();
    }
    const double relative_error = std::abs(volume - exact_volume) / exact_volume;
    require(relative_error < allowed_relative_error, "Mesh volume mismatch: " + name);
    require(largest_element_z_span < maximum_z_span, "Mesh did not refine through thickness");
    require(archive(source) == original, "Mesher modified source CAD topology");
    const auto seconds = std::chrono::duration<double>(std::chrono::steady_clock::now() - start).count();
    std::cout.precision(15);
    std::cout << "{\"case\":\"" << name << "\",\"size\":" << size << ",\"order\":" << order
              << ",\"nodes\":" << node_tags.size() << ",\"tetrahedra\":" << count
              << ",\"mapped_faces\":" << face_tags.size() << ",\"boundary_elements\":" << boundary_elements
              << ",\"cad_volume\":" << exact_volume << ",\"mesh_volume\":" << volume
              << ",\"relative_volume_error\":" << relative_error << ",\"minimum_sampled_jacobian\":"
              << minimum_jacobian << ",\"maximum_element_z_span\":" << largest_element_z_span
              << ",\"source_unchanged\":true,\"seconds\":" << seconds << "}\n";
    gmsh::model::remove();
    return count;
}
}
int main() {
    try {
        gmsh::initialize(0, nullptr, false, false);
        gmsh::option::setNumber("General.Terminal", 0);
        run("box", BRepPrimAPI_MakeBox(20., 10., 5.).Shape(), 3., 1, 1e-10);
        const auto plate = BRepPrimAPI_MakeBox(30., 12., 2.).Shape();
        const auto cutter = BRepPrimAPI_MakeCylinder(gp_Ax2(gp_Pnt(15., 6., -1.), gp_Dir(0, 0, 1)), 3., 4.).Shape();
        const auto holed = BRepAlgoAPI_Cut(plate, cutter).Shape();
        run("plate_hole", holed, 1.25, 2, .001);
        run("plate_hole_refined", holed, .8, 2, .001);
        run("small_fillets", rounded_box(), .9, 2, .003);
        const auto thin = BRepPrimAPI_MakeBox(12., 6., 1.).Shape();
        const auto coarse = run("thin_coarse", thin, 2., 2, 1e-10);
        const auto fine = run("thin_refined", thin, .45, 2, 1e-10, .75);
        require(fine > coarse * 2, "Refinement did not increase volume resolution");
        run("twisted_volume", twisted_strip(), .6, 2, .003);
        gmsh::finalize();
        return 0;
    } catch (const std::exception& error) {
        std::cerr << "Probe failed: " << error.what() << '\n';
    } catch (const std::string& error) {
        std::cerr << "Gmsh failed: " << error << '\n';
    } catch (...) {
        std::cerr << "Unexpected mesher exception\n";
    }
    if (gmsh::isInitialized()) gmsh::finalize();
    return 1;
}
