from __future__ import annotations

import configparser
import io
from dataclasses import dataclass
from pathlib import Path

from zima_cad.versioned_io import validate_ini_file, write_text_versioned


ROOT = Path(__file__).resolve().parents[1]
MATERIALS = ROOT / "config" / "materials"
TEMPLATE = MATERIALS / "01_oceli" / "konstrukcni" / "ocel.matz"


@dataclass(frozen=True)
class Material:
    path: str
    name: str
    young: float
    poisson: float
    density: float
    expansion: float
    damping: float
    tension: float
    compression: float
    shear_limit: float
    conductivity: float
    emissivity: float
    specific_heat: float
    hardness: str
    condition: str


CATALOG = (
    Material("01_oceli/konstrukcni/S235JR.matz", "S235JR", 210000, .30, 7850, 12e-6, .002, 235, 235, 136, 50, .70, 470, "120 HB", "EN 10025-2, válcovaný stav, reprezentativní hodnoty při 20 °C"),
    Material("01_oceli/konstrukcni/S275JR.matz", "S275JR", 210000, .30, 7850, 12e-6, .002, 275, 275, 159, 50, .70, 470, "135 HB", "EN 10025-2, válcovaný stav, reprezentativní hodnoty při 20 °C"),
    Material("01_oceli/konstrukcni/S355JR.matz", "S355JR", 210000, .30, 7850, 12e-6, .002, 355, 355, 205, 50, .70, 470, "150 HB", "EN 10025-2, válcovaný stav, reprezentativní hodnoty při 20 °C"),
    Material("01_oceli/konstrukcni/S355J2.matz", "S355J2", 210000, .30, 7850, 12e-6, .002, 355, 355, 205, 50, .70, 470, "150 HB", "EN 10025-2, válcovaný stav, reprezentativní hodnoty při 20 °C"),
    Material("01_oceli/nerezove/1.4301_AISI304.matz", "1.4301 / AISI 304", 200000, .30, 7900, 16e-6, .002, 210, 210, 121, 15, .30, 500, "170 HB", "Žíhaný stav, reprezentativní hodnoty při 20 °C"),
    Material("01_oceli/nerezove/1.4404_AISI316L.matz", "1.4404 / AISI 316L", 200000, .30, 8000, 16e-6, .002, 200, 200, 115, 15, .30, 500, "170 HB", "Žíhaný stav, reprezentativní hodnoty při 20 °C"),
    Material("01_oceli/nerezove/1.4571_AISI316Ti.matz", "1.4571 / AISI 316Ti", 200000, .30, 8000, 16.5e-6, .002, 220, 220, 127, 15, .30, 500, "180 HB", "Žíhaný stav, reprezentativní hodnoty při 20 °C"),
    Material("01_oceli/nerezove/1.4016_AISI430.matz", "1.4016 / AISI 430", 200000, .29, 7700, 10.4e-6, .002, 260, 260, 150, 25, .30, 460, "180 HB", "Žíhaný stav, reprezentativní hodnoty při 20 °C"),
    Material("02_nezelezne_kovy/hlinik/EN_AW-5754_H22.matz", "EN AW-5754 H22", 70000, .33, 2670, 23.7e-6, .002, 130, 130, 75, 147, .10, 900, "45 HB", "H22, plech, reprezentativní hodnoty při 20 °C"),
    Material("02_nezelezne_kovy/hlinik/EN_AW-6060_T66.matz", "EN AW-6060 T66", 69000, .33, 2700, 23.4e-6, .002, 160, 160, 92, 200, .10, 901, "70 HB", "T66, lisovaný profil, reprezentativní hodnoty při 20 °C"),
    Material("02_nezelezne_kovy/hlinik/EN_AW-6082_T6.matz", "EN AW-6082 T6", 70000, .33, 2700, 23.1e-6, .002, 250, 250, 144, 180, .10, 900, "95 HB", "T6, lisovaný profil, reprezentativní hodnoty při 20 °C"),
    Material("02_nezelezne_kovy/hlinik/EN_AW-7075_T6.matz", "EN AW-7075 T6", 71700, .33, 2810, 23.6e-6, .002, 503, 503, 290, 130, .10, 960, "150 HB", "T6, reprezentativní hodnoty při 20 °C"),
    Material("02_nezelezne_kovy/med_a_slitiny/med/Cu-ETP_CW004A.matz", "Cu-ETP / CW004A", 117000, .34, 8940, 16.8e-6, .002, 200, 200, 115, 390, .10, 385, "80 HB", "Polotvrdý stav, reprezentativní hodnoty při 20 °C"),
    Material("02_nezelezne_kovy/med_a_slitiny/med/Cu-DHP_CW024A.matz", "Cu-DHP / CW024A", 115000, .34, 8940, 17e-6, .002, 150, 150, 87, 305, .10, 385, "65 HB", "Polotvrdý stav, reprezentativní hodnoty při 20 °C"),
    Material("02_nezelezne_kovy/med_a_slitiny/med/Cu-OF_CW008A.matz", "Cu-OF / CW008A", 117000, .34, 8940, 16.8e-6, .002, 70, 70, 40, 390, .10, 385, "45 HB", "Žíhaný stav, reprezentativní hodnoty při 20 °C"),
    Material("02_nezelezne_kovy/med_a_slitiny/mosaz/CuZn37_CW508L.matz", "CuZn37 / CW508L", 110000, .35, 8440, 20.5e-6, .002, 180, 180, 104, 120, .10, 380, "80 HB", "Polotvrdý stav, reprezentativní hodnoty při 20 °C"),
    Material("02_nezelezne_kovy/med_a_slitiny/mosaz/CuZn39Pb3_CW614N.matz", "CuZn39Pb3 / CW614N", 97000, .34, 8470, 20.5e-6, .002, 200, 200, 115, 120, .10, 380, "100 HB", "Tažená tyč, reprezentativní hodnoty při 20 °C"),
    Material("02_nezelezne_kovy/med_a_slitiny/mosaz/CuZn40Pb2_CW617N.matz", "CuZn40Pb2 / CW617N", 97000, .34, 8430, 20.5e-6, .002, 180, 180, 104, 116, .10, 380, "90 HB", "Kovaný stav, reprezentativní hodnoty při 20 °C"),
    Material("02_nezelezne_kovy/med_a_slitiny/bronz/CuSn7.matz", "CuSn7", 105000, .34, 8800, 18e-6, .002, 180, 180, 104, 60, .20, 380, "90 HB", "Obecný tvářený stav, reprezentativní hodnoty při 20 °C"),
    Material("02_nezelezne_kovy/med_a_slitiny/bronz/CuSn12.matz", "CuSn12", 100000, .34, 8800, 18e-6, .002, 150, 150, 87, 50, .20, 380, "100 HB", "Odlévaný stav, reprezentativní hodnoty při 20 °C"),
    Material("02_nezelezne_kovy/med_a_slitiny/bronz/CuAl10Ni5Fe4.matz", "CuAl10Ni5Fe4", 120000, .32, 7600, 16e-6, .002, 280, 280, 162, 40, .20, 420, "170 HB", "Odlévaný stav, reprezentativní hodnoty při 20 °C"),
    Material("02_nezelezne_kovy/horcik/AZ31B.matz", "AZ31B", 45000, .35, 1770, 26e-6, .003, 200, 200, 115, 96, .12, 1040, "50 HB", "Tvářený stav, reprezentativní hodnoty při 20 °C"),
    Material("02_nezelezne_kovy/horcik/AZ61A.matz", "AZ61A", 45000, .35, 1800, 26e-6, .003, 230, 230, 133, 75, .12, 1040, "60 HB", "Tvářený stav, reprezentativní hodnoty při 20 °C"),
    Material("02_nezelezne_kovy/horcik/AZ91D.matz", "AZ91D", 45000, .35, 1810, 26e-6, .003, 160, 160, 92, 72, .12, 1020, "63 HB", "Tlakový odlitek, reprezentativní hodnoty při 20 °C"),
    Material("02_nezelezne_kovy/titan/Titanium_Grade_2.matz", "Titanium Grade 2", 105000, .34, 4510, 8.6e-6, .002, 275, 275, 159, 16.4, .30, 520, "160 HB", "Komerčně čistý titan, žíhaný stav, reprezentativní hodnoty při 20 °C"),
    Material("02_nezelezne_kovy/titan/Ti-6Al-4V_Grade_5.matz", "Ti-6Al-4V / Grade 5", 114000, .34, 4430, 8.6e-6, .002, 880, 880, 508, 6.7, .30, 560, "334 HB", "Žíhaný stav, reprezentativní hodnoty při 20 °C"),
    Material("02_nezelezne_kovy/titan/Titanium_Grade_12.matz", "Titanium Grade 12", 103000, .34, 4540, 9e-6, .002, 480, 480, 277, 17, .30, 520, "220 HB", "Žíhaný stav, reprezentativní hodnoty při 20 °C"),
    Material("03_litiny/EN-GJL-200.matz", "EN-GJL-200", 110000, .26, 7150, 10.5e-6, .005, 200, 700, 240, 50, .75, 460, "200 HB", "EN 1561, reprezentativní hodnoty při 20 °C"),
    Material("03_litiny/EN-GJL-250.matz", "EN-GJL-250", 120000, .26, 7200, 10.5e-6, .005, 250, 800, 300, 46, .75, 460, "220 HB", "EN 1561, reprezentativní hodnoty při 20 °C"),
    Material("03_litiny/EN-GJL-300.matz", "EN-GJL-300", 130000, .26, 7250, 10.5e-6, .005, 300, 900, 350, 42, .75, 460, "250 HB", "EN 1561, reprezentativní hodnoty při 20 °C"),
    Material("03_litiny/EN-GJS-400-15.matz", "EN-GJS-400-15", 169000, .275, 7100, 11e-6, .003, 250, 250, 145, 36, .75, 500, "150 HB", "EN 1563, feritická tvárná litina, reprezentativní hodnoty při 20 °C"),
    Material("04_plasty/polyamidy/PA66.matz", "PA66", 3000, .39, 1140, 80e-6, .03, 80, 100, 50, .25, .90, 1700, "80 Shore D", "Suchý, nevystužený materiál, reprezentativní hodnoty při 20 °C"),
    Material("04_plasty/polyamidy/PA6-GF30.matz", "PA6-GF30", 9500, .35, 1360, 30e-6, .02, 150, 180, 90, .35, .90, 1400, "85 Shore D", "30 % skelných vláken, kondicionovaný, reprezentativní hodnoty při 20 °C"),
    Material("04_plasty/polyamidy/PA12.matz", "PA12", 1500, .40, 1020, 150e-6, .04, 45, 55, 28, .23, .90, 1700, "72 Shore D", "Nevystužený materiál, reprezentativní hodnoty při 20 °C"),
    Material("04_plasty/polyolefiny/PE-UHMW_1000.matz", "PE-UHMW 1000", 800, .46, 940, 180e-6, .06, 20, 18, 12, .40, .95, 1900, "65 Shore D", "Lisovaný polotovar, reprezentativní hodnoty při 20 °C"),
    Material("04_plasty/polyolefiny/PP-H.matz", "PP-H", 1500, .42, 910, 150e-6, .05, 32, 45, 20, .22, .95, 1900, "70 Shore D", "Homopolymer, reprezentativní hodnoty při 20 °C"),
    Material("04_plasty/polyolefiny/PP-C.matz", "PP-C", 1100, .42, 900, 130e-6, .05, 25, 35, 16, .20, .95, 1900, "65 Shore D", "Kopolymer, reprezentativní hodnoty při 20 °C"),
    Material("04_plasty/technicke_plasty/POM-C.matz", "POM-C", 2800, .35, 1410, 110e-6, .025, 65, 90, 40, .31, .90, 1460, "85 Shore D", "Kopolymer, reprezentativní hodnoty při 20 °C"),
    Material("04_plasty/technicke_plasty/PET.matz", "PET", 2800, .38, 1380, 70e-6, .03, 55, 80, 35, .24, .90, 1200, "80 Shore D", "Technický polotovar, reprezentativní hodnoty při 20 °C"),
    Material("04_plasty/technicke_plasty/PC.matz", "PC", 2400, .37, 1200, 65e-6, .04, 60, 75, 38, .20, .90, 1200, "80 Shore D", "Nevystužený materiál, reprezentativní hodnoty při 20 °C"),
    Material("04_plasty/technicke_plasty/ABS.matz", "ABS", 2200, .39, 1050, 90e-6, .05, 40, 55, 25, .18, .90, 1300, "75 Shore D", "Nevystužený materiál, reprezentativní hodnoty při 20 °C"),
    Material("05_sklo_keramika/Soda_Lime_Glass.matz", "Soda-lime glass", 70000, .23, 2500, 9e-6, .001, 45, 1000, 35, 1.0, .90, 840, "550 HV", "Žíhané sodnovápenaté sklo, reprezentativní hodnoty při 20 °C"),
    Material("05_sklo_keramika/Borosilicate_3.3.matz", "Borosilicate glass 3.3", 64000, .20, 2230, 3.3e-6, .001, 40, 1000, 33, 1.2, .90, 830, "480 HV", "Žíhané borosilikátové sklo, reprezentativní hodnoty při 20 °C"),
    Material("05_sklo_keramika/Fused_Silica.matz", "Fused silica", 72000, .17, 2200, .55e-6, .001, 50, 1100, 40, 1.4, .90, 740, "600 HV", "Tavený křemen, reprezentativní hodnoty při 20 °C"),
    Material("05_sklo_keramika/Alumina_96.matz", "Alumina 96 %", 300000, .22, 3750, 7.5e-6, .001, 250, 2000, 200, 25, .80, 880, "1400 HV", "Keramika Al2O3 96 %, reprezentativní hodnoty při 20 °C"),
    Material("06_stavebni/beton/C20-25.matz", "Concrete C20/25", 30000, .20, 2400, 10e-6, .05, 2.2, 20, 4, 1.8, .90, 880, "N/A", "EN 206, normální beton, reprezentativní hodnoty při 20 °C"),
    Material("06_stavebni/beton/C25-30.matz", "Concrete C25/30", 31000, .20, 2400, 10e-6, .05, 2.6, 25, 5, 1.8, .90, 880, "N/A", "EN 206, normální beton, reprezentativní hodnoty při 20 °C"),
    Material("06_stavebni/beton/C30-37.matz", "Concrete C30/37", 33000, .20, 2400, 10e-6, .05, 2.9, 30, 6, 1.8, .90, 880, "N/A", "EN 206, normální beton, reprezentativní hodnoty při 20 °C"),
    Material("06_stavebni/beton/C35-45.matz", "Concrete C35/45", 34000, .20, 2400, 10e-6, .05, 3.2, 35, 7, 1.8, .90, 880, "N/A", "EN 206, normální beton, reprezentativní hodnoty při 20 °C"),
)


def number(value: float) -> str:
    return f"{value:.6E}"


def main() -> None:
    template = configparser.ConfigParser(interpolation=None)
    template.optionxform = str
    template.read(TEMPLATE, encoding="utf-8-sig")
    descriptions = dict(template["ParameterDescriptions"])

    for material in CATALOG:
        target = MATERIALS / material.path
        target.parent.mkdir(parents=True, exist_ok=True)
        config = configparser.ConfigParser(interpolation=None)
        config.optionxform = str
        config["Material"] = {"Name": material.name}
        config["Properties"] = {
            "YOUNG_MODULUS": number(material.young),
            "POISSON_RATIO": number(material.poisson),
            "SHEAR_MODULUS": number(material.young / (2 * (1 + material.poisson))),
            "MASS_DENSITY": number(material.density * 1e-9),
            "THERMAL_EXPANSION_COEFFICIENT": number(material.expansion),
            "THERM_EXPANSION_REF_TEMPERATURE": "20",
            "STRUCTURAL_DAMPING_COEFFICIENT": number(material.damping),
            "STRESS_LIMIT_FOR_TENSION": number(material.tension),
            "STRESS_LIMIT_FOR_COMPRESSION": number(material.compression),
            "STRESS_LIMIT_FOR_SHEAR": number(material.shear_limit),
            "THERMAL_CONDUCTIVITY": number(material.conductivity * 1000),
            "EMISSIVITY": number(material.emissivity),
            "SPECIFIC_HEAT": number(material.specific_heat * 1_000_000),
            "HARDNESS": material.hardness,
            "CONDITION": material.condition,
            "SHEETMETAL_K_FACTOR": "0.318309886184",
        }
        config["PropertyUnits"] = dict(template["PropertyUnits"])
        config["ParameterDescriptions"] = descriptions
        buffer = io.StringIO()
        config.write(buffer)
        write_text_versioned(
            target,
            buffer.getvalue().rstrip() + "\n",
            validator=validate_ini_file,
        )


if __name__ == "__main__":
    main()
