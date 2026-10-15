"""Complete factory material property descriptions without changing physical data."""
import argparse
from pathlib import Path

ROOT=Path(__file__).resolve().parents[1]/'config/materials'
RUSSIAN={
 'MATERIAL_NAME':'Материал','YOUNG_MODULUS':'Модуль Юнга','POISSON_RATIO':'Коэффициент Пуассона',
 'SHEAR_MODULUS':'Модуль сдвига','MASS_DENSITY':'Плотность',
 'THERMAL_EXPANSION_COEFFICIENT':'Коэффициент теплового расширения',
 'THERM_EXPANSION_REF_TEMPERATURE':'Опорная температура теплового расширения',
 'STRUCTURAL_DAMPING_COEFFICIENT':'Коэффициент конструкционного демпфирования',
 'STRESS_LIMIT_FOR_TENSION':'Предельное напряжение при растяжении',
 'STRESS_LIMIT_FOR_COMPRESSION':'Предельное напряжение при сжатии',
 'STRESS_LIMIT_FOR_SHEAR':'Предельное напряжение при сдвиге',
 'THERMAL_CONDUCTIVITY':'Теплопроводность','EMISSIVITY':'Степень черноты',
 'SPECIFIC_HEAT':'Удельная теплоёмкость','HARDNESS':'Твёрдость',
 'CONDITION':'Состояние материала','SHEETMETAL_K_FACTOR':'Коэффициент K листового материала',
}
def main():
    parser=argparse.ArgumentParser(description=__doc__);parser.add_argument('--check',action='store_true');args=parser.parse_args()
    count=0
    for path in ROOT.rglob('*.matz'):
        raw=path.read_bytes();text=raw.decode('utf-8-sig');section='';properties={'MATERIAL_NAME'};descriptions={}
        for line in text.splitlines():
            line=line.strip()
            if not line or line.startswith(('#',';')):continue
            if line.startswith('['):section=line;continue
            key,_,value=line.partition('=');key=key.strip();value=value.strip()
            if section=='[Properties]':properties.add(key)
            if section=='[ParameterDescriptions]':descriptions[key]=value
        additions=[]
        for key in sorted(properties):
            for language in ['cs','en','de','fr']:assert descriptions.get(key+'\\'+language),(path,key,language)
            if not descriptions.get(key+'\\ru'):
                assert key in RUSSIAN,(path,key)
                additions.append(key+'\\ru = '+RUSSIAN[key])
        if args.check:assert not additions,path
        elif additions:
            assert '[ParameterDescriptions]' in text and text.rfind('[ParameterDescriptions]')>text.rfind('[PropertyUnits]')
            newline='\r\n' if b'\r\n' in raw else '\n'
            suffix=('' if raw.endswith(b'\n') else newline)+newline.join(additions)+newline
            path.write_bytes(raw+suffix.encode('utf-8'))
        count+=1
    print(f'{count} factory materials: cs/en/de/fr/ru descriptions verified')
if __name__=='__main__':main()
