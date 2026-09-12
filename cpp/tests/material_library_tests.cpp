#include <zima/document/material_library.hpp>
#include <zima/document/part_document.hpp>
#include <fstream>
#include <iostream>
#include <stdexcept>
namespace fs=std::filesystem;using namespace zima;
namespace {
void require(bool yes,const char* text){if(!yes)throw std::runtime_error(text);}
void write(const fs::path& path,const std::string& data){std::ofstream out(path,std::ios::binary);out.write(data.data(),static_cast<std::streamsize>(data.size()));if(!out)throw std::runtime_error("Cannot create fixture");}
void verify(const fs::path& directory) {
    std::size_t count=0;
    for(const auto& entry:fs::recursive_directory_iterator("config/materials"))if(entry.is_regular_file() && entry.path().extension()==".matz") {
        const auto data=document::load_material_library(entry.path());
        require(!data.properties.at("MATERIAL_NAME").empty() && data.properties.contains("MASS_DENSITY"),"Bundled material lost its name or density");
        require(data.descriptions.at("MASS_DENSITY").at("cs")=="Hustota","Bundled Czech description was lost");++count;
    }
    require(count>=62,"Bundled material coverage incomplete");
    const auto unicode=directory/fs::path(u8"Český materiál.MATZ");
    const std::string material="[Material]\r\nName = Český materiál\r\n[Properties]\r\nMASS_DENSITY = 2700\r\nCONDITION = A=B;C # literal\r\n[PropertyUnits]\r\nMASS_DENSITY = kg/m^3\r\n[ParameterDescriptions]\r\nMASS_DENSITY\\cs = Hustota\r\nMASS_DENSITY\\en = Mass density\r\n";
    write(unicode,std::string("\xef\xbb\xbf")+material);
    const auto data=document::load_material_library(unicode);
    require(data.properties.at("MATERIAL_NAME")=="Český materiál" && data.properties.at("CONDITION")=="A=B;C # literal" && data.descriptions.at("MASS_DENSITY").at("en")=="Mass density","Unicode, literal text or language maps were changed");
    const auto rejected=[&](const std::string& input) {
        write(unicode,input);bool failed=false;try{static_cast<void>(document::load_material_library(unicode));}catch(const std::exception&){failed=true;}require(failed,"Invalid material library accepted");
    };
    rejected("");rejected("[Material]\nName=Missing properties");rejected(material+"[Properties]\nX=1\n");
    rejected(material+"MASS_DENSITY\\cs=Duplicate\n");rejected(material+"[Unknown]\nValue=1\n");
    rejected(material+"broken line\n");rejected(material+std::string(1,'\0'));rejected(material+"BAD="+std::string(1,static_cast<char>(0xff)));
    auto invalid=material;invalid.replace(invalid.find("2700"),4,"-1");rejected(invalid);
    invalid=material;invalid.replace(invalid.find("kg/m^3"),6,"MPa");rejected(invalid);
    rejected("[Material]\nName=Al\n[Properties]\nMATERIAL_NAME=Duplicate\n");
    rejected(std::string(16*1024*1024+1,'x'));
    bool missing=false;try{static_cast<void>(document::load_material_library(directory/"missing.matz"));}catch(const std::exception&){missing=true;}require(missing,"Missing library accepted");
    const auto wrong=directory/"wrong.txt";write(wrong,material);bool extension=false;try{static_cast<void>(document::load_material_library(wrong));}catch(const std::exception&){extension=true;}require(extension,"Wrong extension accepted");
    std::cout<<count<<" bundled materials, Unicode/CRLF/BOM, descriptions, literal text and invalid libraries passed\n";
}
}
int main(){try{const auto parent=fs::canonical(fs::temp_directory_path());const auto directory=parent/("zima-material-library-"+document::PartDocument::create_default().document_id);fs::create_directory(directory);verify(directory);require(directory.parent_path()==parent,"Unsafe cleanup");fs::remove_all(directory);return 0;}catch(const std::exception& e){std::cerr<<e.what()<<'\n';return 1;}}
