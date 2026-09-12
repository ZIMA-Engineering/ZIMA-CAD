#pragma once
#include <zima/assembly/assembly_document.hpp>
#include <zima/kernel/occt_kernel.hpp>
#include <fstream>
#include <cmath>

namespace zima::test {
struct StlGeometry {
    double signed_volume{};
    std::vector<kernel::Vec3> vertices;
};
inline StlGeometry read_stl(const std::filesystem::path& path) {
    const auto check=[](bool value){if(!value)throw std::runtime_error("Invalid binary STL geometry");};
    std::ifstream input(path,std::ios::binary);input.seekg(80);unsigned char n[4]{};input.read(reinterpret_cast<char*>(n),4);
    const std::uint32_t count=n[0]|(std::uint32_t(n[1])<<8)|(std::uint32_t(n[2])<<16)|(std::uint32_t(n[3])<<24);
    check(count>0&&count<1000000&&std::filesystem::file_size(path)==84+50ull*count);
    StlGeometry result;
    for(std::uint32_t i=0;i<count;++i) {
        float v[12];input.read(reinterpret_cast<char*>(v),48);input.ignore(2);check(bool(input));
        for(float value:v)check(std::isfinite(value));
        const kernel::Vec3 a{v[3],v[4],v[5]},b{v[6],v[7],v[8]},c{v[9],v[10],v[11]};
        const kernel::Vec3 normal{(b.y-a.y)*(c.z-a.z)-(b.z-a.z)*(c.y-a.y),
            (b.z-a.z)*(c.x-a.x)-(b.x-a.x)*(c.z-a.z),(b.x-a.x)*(c.y-a.y)-(b.y-a.y)*(c.x-a.x)};
        check(normal.x*v[0]+normal.y*v[1]+normal.z*v[2]>0);
        result.signed_volume+=(a.x*(b.y*c.z-b.z*c.y)+a.y*(b.z*c.x-b.x*c.z)+a.z*(b.x*c.y-b.y*c.x))/6.;
        result.vertices.insert(result.vertices.end(),{a,b,c});
    }
    return result;
}
inline assembly::AssemblyDocument nested_stl_fixture(const kernel::OcctKernel& kernel,const std::filesystem::path& directory) {
    const kernel::BodySnapshot body=kernel.make_box({10,20,30});
    auto leaf=assembly::AssemblyDocument::create_default();leaf.name="STL leaf assembly";
    auto part=assembly::AssemblyDocument::create_part_occurrence("Box",document::PartDocument::create_default().document_id,directory/"absent-source.prtz",body);
    part.placement={7,11,13,90,0,0};leaf.components.push_back(part);
    // An unavailable hidden/suppressed body must never enter export or be required.
    auto hidden=assembly::AssemblyDocument::create_part_occurrence("Hidden",part.source_document_id,part.source_path,{});hidden.visible=false;
    auto suppressed=assembly::AssemblyDocument::create_part_occurrence("Suppressed",part.source_document_id,part.source_path,{});suppressed.suppressed=true;
    auto dependent=assembly::AssemblyDocument::create_part_occurrence("Dependency suppressed",part.source_document_id,part.source_path,{});
    leaf.dependencies.push_back({"stl-dependency",dependent.occurrence_id,suppressed.occurrence_id,assembly::ComponentDependencyKind::DerivedCopyReference});
    leaf.components.insert(leaf.components.end(),{hidden,suppressed,dependent});
    auto middle=assembly::AssemblyDocument::create_default();middle.name="STL middle assembly";
    auto inner=assembly::AssemblyDocument::create_assembly_occurrence("Inner",leaf.document_id,directory/"absent-leaf.asmz",leaf);
    inner.placement={100,200,300,0,90,0};middle.components.push_back(inner);
    auto top=assembly::AssemblyDocument::create_default();top.name="STL repeated hierarchy";
    auto first=assembly::AssemblyDocument::create_assembly_occurrence("First",middle.document_id,directory/"absent-middle.asmz",middle);
    first.placement={1000,2000,3000,0,0,90};
    auto second=first;second.occurrence_id=assembly::AssemblyDocument::create_default().document_id;second.name="Second";second.placement={-1000,-2000,-3000,0,0,0};
    top.components={first,second};return top;
}
inline void check_nested_stl(const std::filesystem::path& path) {
    const auto geometry=read_stl(path);
    if(geometry.vertices.size()!=72||std::abs(geometry.signed_volume-12000)>1e-5)throw std::runtime_error("Nested STL changed triangle count or signed volume");
    // Independent quarter-turn arithmetic: Rx, then Ry, then each parent's Rz.
    // No application placement, mesh, or kernel transform is used as the oracle.
    std::vector<kernel::Vec3> expected;
    for(double x:{0.,10.})for(double y:{0.,20.})for(double z:{0.,30.}) {
        expected.push_back({789+z,2113+y,3293-x});
        expected.push_back({-887+y,-1789-z,-2707-x});
    }
    std::vector<bool> seen(expected.size());
    for(const auto& point:geometry.vertices) {
        bool found=false;
        for(std::size_t i=0;i<expected.size();++i)if(std::hypot(std::hypot(point.x-expected[i].x,point.y-expected[i].y),point.z-expected[i].z)<1e-5){seen[i]=true;found=true;break;}
        if(!found)throw std::runtime_error("Nested STL lost or duplicated an occurrence transformation");
    }
    for(bool present:seen)if(!present)throw std::runtime_error("Nested STL omitted an expected corner");
}
}
