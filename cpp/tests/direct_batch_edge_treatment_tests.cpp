#include "profile_request_fixture.hpp"

#include <zima/kernel/occt_kernel.hpp>
#include <zima/kernel/calculated_body_shape.hpp>

#include <BRepAlgoAPI_Cut.hxx>
#include <BRepCheck_Analyzer.hxx>
#include <BRepGProp.hxx>
#include <GProp_GProps.hxx>
#include <TopExp.hxx>
#include <TopTools_IndexedMapOfShape.hxx>

#include <algorithm>
#include <array>
#include <cmath>
#include <iostream>
#include <set>
#include <stdexcept>
#include <string>
#include <tuple>
#include <vector>

namespace {

void require(bool condition,const char* message) {
    if(!condition)throw std::runtime_error(message);
}

double material_difference(const TopoDS_Shape& left,
        const TopoDS_Shape& right) {
    BRepAlgoAPI_Cut cut(left,right);
    cut.SetRunParallel(true);
    cut.SetFuzzyValue(1.0e-7);
    cut.Build();
    require(cut.IsDone()&&!cut.Shape().IsNull(),
        "Direct batch comparison Boolean failed");
    GProp_GProps properties;
    BRepGProp::VolumeProperties(cut.Shape(),properties);
    return std::abs(properties.Mass());
}

zima::kernel::FeatureGroupRequest feature(
        const zima::test::ProfilePrism& profile) {
    zima::kernel::FeatureGroupRequest result;
    result.children.push_back(
        static_cast<zima::kernel::ExtrusionRequest>(profile));
    return result;
}

std::array<int,4> topology_counts(const TopoDS_Shape& shape) {
    std::array<int,4> result{};
    for(std::size_t index=0;index<result.size();++index) {
        TopTools_IndexedMapOfShape members;
        TopExp::MapShapes(shape,std::array{TopAbs_SOLID,TopAbs_FACE,
            TopAbs_EDGE,TopAbs_VERTEX}[index],members);
        result[index]=members.Extent();
    }
    return result;
}

} // namespace

int main() {
    try {
        using namespace zima;
        std::vector<kernel::HistoryOperation> history{
            {"alias-base",feature(test::ProfilePrism{100,100,20}),
                kernel::BooleanOperation::Add}};
        const std::array<double,4> y_positions{8.,30.,52.,74.};
        std::array<std::string,4> tool_owners;
        for(std::size_t index=0;index<tool_owners.size();++index) {
            tool_owners[index]="alias-tool-"+std::to_string(index);
            test::ProfilePrism tool{10,14,10};
            tool.translation={95,y_positions[index],5};
            history.push_back({tool_owners[index],feature(tool),
                kernel::BooleanOperation::Add});
        }

        kernel::OcctKernel serial_kernel;
        auto serial=serial_kernel.evaluate_history(history);
        require(serial.size()==history.size()&&
                serial.back().calculation_errors.empty(),
            "Sequential treatment fixture Add history failed");
        std::array<std::vector<kernel::EdgeReference>,4> routes;
        for(const auto& edge:serial.back().mesh.edges) {
            for(std::size_t index=0;index<tool_owners.size();++index) {
                if(edge.reference.owner_id==tool_owners[index]&&
                   edge.reference.semantic_key.starts_with(
                       "boolean:add:intersection:between:"))
                    routes[index].push_back(edge.reference);
            }
        }
        for(std::size_t index=0;index<routes.size();++index) {
            require(routes[index].size()==4,
                "Sequential treatment fixture did not expose one intersection loop per tool");
            history.push_back({"alias-fillet-"+std::to_string(index),
                kernel::FilletRequest{routes[index],0.5},
                kernel::BooleanOperation::Add});
            serial=serial_kernel.evaluate_history(history);
            require(serial.size()==history.size()&&
                    serial.back().calculation_errors.empty(),
                "Sequential Fillet reference did not calculate");
        }

        kernel::HistoryOutputPolicy policy;
        policy.final_boundary_first=true;
        auto retained_policy=policy;
        retained_policy.input_boundary_consumers.insert(
            tool_owners.back());
        kernel::OcctKernel retained_kernel;
        const auto retained=retained_kernel.evaluate_history_recovering(
            history,{},retained_policy);
        require(retained.size()==history.size()&&
                !retained.at(tool_owners.size()-1).mesh.edges.empty(),
            "Direct batch did not publish a retained consumer input boundary");
        require(std::ranges::any_of(
                    retained.at(tool_owners.size()-1).mesh.edges,
                    [&](const auto& edge) {
                        return edge.reference.owner_id==tool_owners.at(2);
                    }),
            "Retained direct input boundary lost its exact preceding edge identities");
        kernel::OcctKernel direct_kernel;
        const auto direct=direct_kernel.evaluate_history_recovering(
            history,{},policy);
        require(direct.size()==history.size()&&
                direct.back().calculation_errors.empty(),
            "Direct batch did not resolve all four sequential Fillets");
        const auto serial_shape=kernel::read_calculated_body_shape(
            serial.back());
        const auto direct_shape=kernel::read_calculated_body_shape(
            direct.back());
        require(BRepCheck_Analyzer(serial_shape,true,true,false).IsValid()&&
                BRepCheck_Analyzer(direct_shape,true,true,false).IsValid(),
            "Sequential Fillet comparison produced an invalid body");
        require(material_difference(serial_shape,direct_shape)<=1.0e-7&&
                material_difference(direct_shape,serial_shape)<=1.0e-7,
            "Direct batch changed the four-Fillet material result");
        const auto reference_keys=[](const kernel::BodyResult& body) {
            std::set<std::string> result;
            const auto append=[&](const auto& bindings) {
                for(const auto& binding:bindings)
                    result.insert(binding.reference.owner_id+'\x1f'+
                        binding.reference.semantic_key+'\x1f'+
                        binding.reference.instance_path);
            };
            append(body.kernel_faces);
            append(body.kernel_edges);
            append(body.kernel_vertices);
            return result;
        };
        const auto serial_references=reference_keys(serial.back());
        const auto direct_references=reference_keys(direct.back());
        require(serial_references==direct_references,
            "Direct batch changed the four-Fillet stable reference set");

        // Exercise a treatment whose persisted input is an edge generated by
        // an earlier treatment.  Direct Add canonical names must not hide the
        // exact older alias recovered from structural face ancestry.
        std::vector<kernel::EdgeReference> generated_treatment_edges;
        for(const auto& edge:serial.back().mesh.edges)
            if(edge.reference.semantic_key.starts_with("fillet:edge:from:")&&
               std::ranges::find(generated_treatment_edges,
                   edge.reference)==generated_treatment_edges.end())
                generated_treatment_edges.push_back(edge.reference);
        std::ranges::sort(generated_treatment_edges,
            [](const auto& left,const auto& right) {
                return std::tie(left.owner_id,left.semantic_key,
                           left.instance_path)<
                    std::tie(right.owner_id,right.semantic_key,
                           right.instance_path);
            });
        std::vector<kernel::HistoryOperation> chained_history;
        std::vector<kernel::BodyResult> chained_serial;
        for(const auto& reference:generated_treatment_edges) {
            try {
                auto candidate=history;
                candidate.push_back({"alias-fillet-descendant",
                    kernel::FilletRequest{{reference},0.2},
                    kernel::BooleanOperation::Add});
                kernel::OcctKernel candidate_kernel;
                auto calculated=candidate_kernel.evaluate_history(candidate);
                if(calculated.size()==candidate.size()&&
                   calculated.back().calculation_errors.empty()) {
                    chained_history=std::move(candidate);
                    chained_serial=std::move(calculated);
                    break;
                }
            } catch(const std::exception&) {
                // Not every generated edge accepts a second physical Fillet.
                // Continue in stable-reference order until the fixture finds
                // one valid dependent treatment.
            }
        }
        require(!chained_history.empty(),
            "Sequential fixture exposed no fillet-generated edge that accepts a dependent Fillet");
        kernel::OcctKernel chained_direct_kernel;
        const auto chained_direct=
            chained_direct_kernel.evaluate_history_recovering(
                chained_history,{},policy);
        require(chained_direct.size()==chained_history.size()&&
                chained_direct.back().calculation_errors.empty(),
            "Direct batch did not resolve a Fillet on a prior Fillet edge");
        const auto chained_serial_shape=kernel::read_calculated_body_shape(
            chained_serial.back());
        const auto chained_direct_shape=kernel::read_calculated_body_shape(
            chained_direct.back());
        require(material_difference(chained_serial_shape,
                    chained_direct_shape)<=1.0e-7&&
                material_difference(chained_direct_shape,
                    chained_serial_shape)<=1.0e-7,
            "Direct batch changed dependent Fillet material");
        require(reference_keys(chained_serial.back())==
                    reference_keys(chained_direct.back()),
            "Direct batch changed dependent Fillet stable references");

        auto resized_history=history;
        auto& resized_group=std::get<kernel::FeatureGroupRequest>(
            resized_history.at(2).primitive);
        auto& resized_tool=std::get<kernel::ExtrusionRequest>(
            resized_group.children.front());
        auto& resized_profile=std::get<
            kernel::ExtrusionRequest::PolygonProfile>(
                resized_tool.outer_profile);
        resized_profile.vertices.at(2).y+=2.;
        resized_profile.vertices.at(3).y+=2.;
        kernel::OcctKernel resized_kernel;
        const auto resized=resized_kernel.evaluate_history_recovering(
            resized_history,{},policy);
        require(resized.size()==resized_history.size()&&
                resized.back().calculation_errors.empty(),
            "Resized direct batch did not resolve all four sequential Fillets");
        require(reference_keys(resized.back())==direct_references,
            "Resizing a direct Add tool changed its stable reference set");
        require(material_difference(
                kernel::read_calculated_body_shape(resized.back()),
                direct_shape)>1.0,
            "Direct Add dimension test did not change the calculated material");

        std::vector<kernel::HistoryOperation> subtract_history{
            {"subtract-base",feature(test::ProfilePrism{100,100,20}),
                kernel::BooleanOperation::Add}};
        for(std::size_t index=0;index<4;++index) {
            test::ProfilePrism cut{10,10,30};
            cut.translation={10.+20.*static_cast<double>(index),10.,-5.};
            subtract_history.push_back({
                "subtract-tool-"+std::to_string(index),feature(cut),
                kernel::BooleanOperation::Subtract});
        }
        kernel::OcctKernel serial_subtract_kernel;
        const auto serial_subtract=serial_subtract_kernel.evaluate_history(
            subtract_history);
        kernel::OcctKernel direct_subtract_kernel;
        const auto direct_subtract_result=
            direct_subtract_kernel.evaluate_history_recovering(
                subtract_history,{},policy);
        require(serial_subtract.size()==subtract_history.size()&&
                direct_subtract_result.size()==subtract_history.size()&&
                serial_subtract.back().calculation_errors.empty()&&
                direct_subtract_result.back().calculation_errors.empty(),
            "Direct multi-tool Subtract fixture did not calculate");
        for(const auto& operation:subtract_history)
            require(std::ranges::any_of(
                        direct_subtract_result.back().mesh.
                            original_references.triangle_references,
                        [&](const auto& reference) {
                            return reference.owner_id==operation.owner_id;
                        }),
                "Direct multi-tool Subtract lost preceding source reference geometry");
        const auto serial_subtract_shape=kernel::read_calculated_body_shape(
            serial_subtract.back());
        const auto direct_subtract_shape=kernel::read_calculated_body_shape(
            direct_subtract_result.back());
        require(material_difference(serial_subtract_shape,
                    direct_subtract_shape)<=1.0e-7&&
                material_difference(direct_subtract_shape,
                    serial_subtract_shape)<=1.0e-7&&
                topology_counts(serial_subtract_shape)==
                    topology_counts(direct_subtract_shape),
            "Direct multi-tool Subtract changed material or topology");

        auto resized_subtract_history=subtract_history;
        auto& resized_subtract_group=
            std::get<kernel::FeatureGroupRequest>(
                resized_subtract_history.at(2).primitive);
        auto& resized_subtract_tool=
            std::get<kernel::ExtrusionRequest>(
                resized_subtract_group.children.front());
        auto& resized_subtract_profile=
            std::get<kernel::ExtrusionRequest::PolygonProfile>(
                resized_subtract_tool.outer_profile);
        resized_subtract_profile.vertices.at(1).x+=2.;
        resized_subtract_profile.vertices.at(2).x+=2.;
        kernel::OcctKernel resized_subtract_kernel;
        const auto resized_subtract=
            resized_subtract_kernel.evaluate_history_recovering(
                resized_subtract_history,{},policy);
        require(resized_subtract.size()==resized_subtract_history.size()&&
                resized_subtract.back().calculation_errors.empty(),
            "Resized direct multi-tool Subtract did not calculate");
        require(reference_keys(resized_subtract.back())==
                    reference_keys(direct_subtract_result.back()),
            "Resizing a direct Subtract tool changed its stable reference set");
        require(material_difference(direct_subtract_shape,
                    kernel::read_calculated_body_shape(
                        resized_subtract.back()))>1.0,
            "Direct Subtract dimension test did not change the material");
        std::cout<<"Exact direct references matched serial across four "
            "sequential Fillets; Add and Subtract identities survived tool "
            "dimension edits\n";
        return 0;
    } catch(const std::exception& error) {
        std::cerr<<error.what()<<'\n';
        return 1;
    }
}
