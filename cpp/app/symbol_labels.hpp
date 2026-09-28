#pragma once
#include <zima/symbols/definition.hpp>
#include <QObject>
namespace zima::app {
inline bool is_surface_texture(const symbols::Definition& d) {
    return d.id=="ze:surface-texture:iso1302-1978"||d.id=="ze:surface-texture:iso21920"||d.id=="ze:general-surface-texture:iso21920";
}
inline QString symbol_field_name(const symbols::Definition& definition,const std::string& key) {
        if(!definition.id.starts_with("ze:"))return QString::fromStdString(key);
        if(key=="Arrow size")return QObject::tr("Rozměr svaru na straně šipky");
        if(key=="Other size")return QObject::tr("Rozměr svaru na opačné straně");
        if(key=="Arrow length")return QObject::tr("Délka / počet / rozteč na straně šipky");
        if(key=="Other length")return QObject::tr("Délka / počet / rozteč na opačné straně");
        if(key=="Text")return QObject::tr("Text");
        if(key=="Specification")return QObject::tr("Drsnost");
        if(key=="Tolerance")return QObject::tr("Hodnota tolerance");
        if(key=="Datum")return QObject::tr("Základna");
        if(key=="Primary datum")return QObject::tr("Primární základna");
        if(key=="Secondary datum")return QObject::tr("Sekundární základna");
        if(key=="Tertiary datum")return QObject::tr("Terciární základna");
        if(key=="External edges")return QObject::tr("Vnější hrany");
        if(key=="Internal edges")return QObject::tr("Vnitřní hrany");
        if(key=="All edges")return QObject::tr("Všechny hrany");
        if(key=="Exception")return QObject::tr("Výjimka");
        return QString::fromStdString(key);
    }
inline QString symbol_variant_name(const symbols::Definition& d,const std::string& key) {
    if(const auto separator=key.find('/');separator!=std::string::npos) {
        const auto kind=key.substr(0,separator),side=key.substr(separator+1);
        const std::map<std::string,QString> labels{
            {"FILLET",QObject::tr("Koutový svar")},{"SQUARE-BUTT",QObject::tr("Tupý I svar")},
            {"V-BUTT",QObject::tr("Tupý V svar")},{"BEVEL-BUTT",QObject::tr("Tupý půl-V svar")},
            {"STRAIGHTNESS",QObject::tr("Přímost")},{"FLATNESS",QObject::tr("Rovinnost")},
            {"CIRCULARITY",QObject::tr("Kruhovitost")},{"CYLINDRICITY",QObject::tr("Válcovitost")},
            {"LINE-PROFILE",QObject::tr("Profil čáry")},{"SURFACE-PROFILE",QObject::tr("Profil plochy")},
            {"PARALLELISM",QObject::tr("Rovnoběžnost")},{"PERPENDICULARITY",QObject::tr("Kolmost")},
            {"ANGULARITY",QObject::tr("Sklon")},{"POSITION",QObject::tr("Poloha")},
            {"COAXIALITY",QObject::tr("Souosost")},{"SYMMETRY",QObject::tr("Souměrnost")},
            {"CIRCULAR-RUNOUT",QObject::tr("Kruhové házení")},{"TOTAL-RUNOUT",QObject::tr("Celkové házení")}};
        if(const auto found=labels.find(kind);found!=labels.end()) {
            if(!d.reference_line_layout)return found->second;
            const auto suffix=side=="arrow_side"?QObject::tr("ISO 2553 A — strana šipky"):
                side=="other_side"?QObject::tr("ISO 2553 A — opačná strana"):QObject::tr("ISO 2553 A — obě strany");
            return found->second+" — "+suffix;
        }
    }
    if(is_surface_texture(d)&&(key=="any_process"||key=="material_removal"||key=="no_material_removal")) {
        const auto label=key=="material_removal"?QObject::tr("Úběr materiálu požadován"):key=="no_material_removal"?QObject::tr("Úběr materiálu nepřípustný"):QObject::tr("Způsob výroby neurčen");
        return d.id=="ze:general-surface-texture:iso21920"?QObject::tr("Celková drsnost — %1").arg(label):label;
    }
    return QString::fromStdString(key);
}
inline QString symbol_sketch_name(const symbols::Definition& d,const sketcher::Sketch& s) {
    if(is_surface_texture(d)) {
        if(s.name=="Base")return QObject::tr("Základní značka");
        if(s.name=="Material removal required")return QObject::tr("Úběr materiálu požadován");
        if(s.name=="Material removal prohibited")return QObject::tr("Úběr materiálu nepřípustný");
        if(s.name=="General any process")return QObject::tr("Celková drsnost — %1").arg(QObject::tr("Způsob výroby neurčen"));
        if(s.name=="General material removal")return QObject::tr("Celková drsnost — %1").arg(QObject::tr("Úběr materiálu požadován"));
        if(s.name=="General no material removal")return QObject::tr("Celková drsnost — %1").arg(QObject::tr("Úběr materiálu nepřípustný"));
    }
    return QString::fromStdString(s.name);
}
}
