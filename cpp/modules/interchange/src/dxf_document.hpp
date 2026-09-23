#pragma once
#include <iomanip>
#include <ostream>
#include <sstream>

namespace zima::interchange::dxf_detail {
inline void pair(std::ostream& out,int code,const auto& value) {out<<std::setw(3)<<code<<'\n'<<value<<'\n';}
inline std::string handle(unsigned value) {std::ostringstream out;out<<std::uppercase<<std::hex<<value;return out.str();}
inline void point(std::ostream& out,int code,double x,double y,double z=0) {pair(out,code,x);pair(out,code+10,y);pair(out,code+20,z);}
inline void section(std::ostream& out,const char* name) {pair(out,0,"SECTION");pair(out,2,name);}
inline void record(std::ostream& out,const char* type,unsigned id,unsigned owner) {
    pair(out,0,type);pair(out,5,handle(id));pair(out,330,handle(owner));
}
inline void table(std::ostream& out,const char* name,unsigned id,int count) {
    pair(out,0,"TABLE");pair(out,2,name);pair(out,5,handle(id));pair(out,330,"0");pair(out,100,"AcDbSymbolTable");pair(out,70,count);
}
inline void symbol(std::ostream& out,const char* type,unsigned id,unsigned owner,const char* subclass,const char* name) {
    record(out,type,id,owner);pair(out,100,"AcDbSymbolTableRecord");pair(out,100,subclass);pair(out,2,name);
}
// Fixed infrastructure handles occupy 1..1E; exported entities start at 20.
// Every model entity owns a handle and refers to the Model_Space block record.
inline void prologue(std::ostream& out,unsigned next_handle) {
    section(out,"HEADER");pair(out,9,"$ACADVER");pair(out,1,"AC1015");
    pair(out,9,"$DWGCODEPAGE");pair(out,3,"ANSI_1252");pair(out,9,"$HANDSEED");pair(out,5,handle(next_handle));
    pair(out,9,"$INSUNITS");pair(out,70,4);pair(out,9,"$MEASUREMENT");pair(out,70,1);
    pair(out,9,"$LUNITS");pair(out,70,2);pair(out,9,"$INSBASE");point(out,10,0,0);pair(out,0,"ENDSEC");
    section(out,"CLASSES");pair(out,0,"ENDSEC");section(out,"TABLES");
    table(out,"VPORT",1,1);symbol(out,"VPORT",2,1,"AcDbViewportTableRecord","*Active");pair(out,70,0);
    pair(out,10,0.);pair(out,20,0.);pair(out,11,1.);pair(out,21,1.);point(out,16,0,0,1);point(out,17,0,0);pair(out,40,1000.);pair(out,41,1.);pair(out,0,"ENDTAB");
    table(out,"LTYPE",3,3);
    unsigned id=4;for(const auto* name:{"ByBlock","ByLayer","Continuous"}) {
        symbol(out,"LTYPE",id++,3,"AcDbLinetypeTableRecord",name);pair(out,70,0);pair(out,3,"");pair(out,72,65);pair(out,73,0);pair(out,40,0.);
    }pair(out,0,"ENDTAB");
    table(out,"LAYER",7,3);id=8;for(const auto* name:{"0","PROFILE","CONSTRUCTION"}) {
        symbol(out,"LAYER",id++,7,"AcDbLayerTableRecord",name);pair(out,70,0);pair(out,62,7);pair(out,6,"Continuous");pair(out,370,-3);
    }pair(out,0,"ENDTAB");
    table(out,"STYLE",0xB,1);symbol(out,"STYLE",0xC,0xB,"AcDbTextStyleTableRecord","Standard");
    pair(out,70,0);pair(out,40,0.);pair(out,41,1.);pair(out,50,0.);pair(out,71,0);pair(out,42,2.5);pair(out,3,"txt");pair(out,4,"");pair(out,0,"ENDTAB");
    table(out,"VIEW",0xD,0);pair(out,0,"ENDTAB");table(out,"UCS",0xE,0);pair(out,0,"ENDTAB");
    table(out,"APPID",0xF,1);symbol(out,"APPID",0x10,0xF,"AcDbRegAppTableRecord","ACAD");pair(out,70,0);pair(out,0,"ENDTAB");
    table(out,"DIMSTYLE",0x11,1);pair(out,100,"AcDbDimStyleTable");
    pair(out,0,"DIMSTYLE");pair(out,105,"12");pair(out,330,"11");pair(out,100,"AcDbSymbolTableRecord");pair(out,100,"AcDbDimStyleTableRecord");
    pair(out,2,"Standard");pair(out,70,0);pair(out,40,1.);pair(out,41,2.5);pair(out,140,2.5);pair(out,340,"C");pair(out,0,"ENDTAB");
    table(out,"BLOCK_RECORD",0x13,2);
    for(unsigned index=0;index<2;++index) {
        symbol(out,"BLOCK_RECORD",0x14+index,0x13,"AcDbBlockTableRecord",index?"*Paper_Space":"*Model_Space");pair(out,340,handle(0x1D+index));
    }pair(out,0,"ENDTAB");pair(out,0,"ENDSEC");section(out,"BLOCKS");
    for(unsigned index=0;index<2;++index) {
        const auto* name=index?"*Paper_Space":"*Model_Space";
        record(out,"BLOCK",0x16+index*2,0x14+index);pair(out,100,"AcDbEntity");pair(out,8,"0");pair(out,100,"AcDbBlockBegin");
        pair(out,2,name);pair(out,70,0);point(out,10,0,0);pair(out,3,name);pair(out,1,"");
        record(out,"ENDBLK",0x17+index*2,0x14+index);pair(out,100,"AcDbEntity");pair(out,8,"0");pair(out,100,"AcDbBlockEnd");
    }pair(out,0,"ENDSEC");section(out,"ENTITIES");
}
inline void epilogue(std::ostream& out) {
    pair(out,0,"ENDSEC");section(out,"OBJECTS");
    record(out,"DICTIONARY",0x1A,0);pair(out,100,"AcDbDictionary");pair(out,281,1);
    pair(out,3,"ACAD_GROUP");pair(out,350,"1B");pair(out,3,"ACAD_LAYOUT");pair(out,350,"1C");
    record(out,"DICTIONARY",0x1B,0x1A);pair(out,100,"AcDbDictionary");pair(out,281,1);
    record(out,"DICTIONARY",0x1C,0x1A);pair(out,100,"AcDbDictionary");pair(out,281,1);
    pair(out,3,"Model");pair(out,350,"1D");pair(out,3,"Layout1");pair(out,350,"1E");
    for(unsigned index=0;index<2;++index) {
        record(out,"LAYOUT",0x1D+index,0x1C);pair(out,100,"AcDbPlotSettings");pair(out,1,"");pair(out,4,"A3");pair(out,6,"");
        pair(out,40,0.);pair(out,41,0.);pair(out,42,0.);pair(out,43,0.);pair(out,44,420.);pair(out,45,297.);
        pair(out,142,1.);pair(out,143,1.);pair(out,70,index?0:1024);pair(out,72,1);pair(out,74,5);pair(out,147,1.);
        pair(out,100,"AcDbLayout");pair(out,1,index?"Layout1":"Model");pair(out,70,1);pair(out,71,index);
        pair(out,10,0.);pair(out,20,0.);pair(out,11,420.);pair(out,21,297.);point(out,12,0,0);point(out,14,0,0);point(out,15,0,0);
        pair(out,146,0.);point(out,13,0,0);point(out,16,1,0);point(out,17,0,1);pair(out,76,1);pair(out,330,handle(0x14+index));
    }pair(out,0,"ENDSEC");pair(out,0,"EOF");
}
}
