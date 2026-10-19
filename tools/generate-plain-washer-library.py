"""Author historical plain-washer candidates from their Z=0 seating plane."""
import argparse
import importlib.util
import json
from pathlib import Path

ROOT=Path(__file__).resolve().parents[1]
spec=importlib.util.spec_from_file_location('washers',ROOT/'tools/generate-spring-washer-library.py')
washers=importlib.util.module_from_spec(spec);spec.loader.exec_module(washers)
caps=washers.caps


def historical_variants():
    source=json.loads((ROOT/'tools/data/fastener-source-tables.json').read_text(encoding='utf-8'))
    result=[]
    for standard,topic,columns,skip in (
            ('021702','01-HEXAGON',(1,2,3),2),
            ('021721','01-HEXAGON',(5,6,7),2),
            ('021703','02-CAP-HEAD',(1,2,3),1)):
        table=source['06-WASHERS/'+topic]['tables'][0]
        for row in table[skip:]:
            if row[columns[0]].strip() in ('','–','—','-'):continue
            d=washers.number(row[0].lstrip('M'));bore,outside,thickness=[washers.number(row[c]) for c in columns]
            forms=['A'] if standard=='021702' else ['']
            # The archived illustrated type-B range starts with the 1.2 mm
            # washer section (M6); do not invent chamfered small thin sizes.
            if standard=='021702' and thickness>=1.2:forms.append('B')
            for form in forms:
                names=dict(zip(caps.LANGUAGES,('Plochá podložka','Plain washer','Flache Unterlegscheibe','Rondelle plate','Плоская шайба')))
                if form:names={lang:text+' '+form for lang,text in names.items()}
                result.append(dict(filename=f'FW-CSN{standard}'+('-'+form if form else '')+f'-M{d:05.1f}.prtz',
                    standard='ČSN 02 '+standard[2:],family=topic,nominal_mm=d,bore_mm=bore,outside_mm=outside,
                    thickness_mm=thickness,width_mm=(outside-bore)/2,form=form,chamfer_mm=.2*thickness if form=='B' else 0,
                    names=names,density_kg_mm3=7.85e-6,material='ČSN 11 423'))
    assert len({v['filename'].lower() for v in result})==len(result)
    return result


def variants():
    reviewed=json.loads((ROOT/'tools/data/plain-washer-iso-reviewed-dimensions.json').read_text(encoding='utf-8'))
    result=[]
    for family in reviewed['families']:
        standard=family['standard'];small=standard=='ISO 7092';coarse=standard=='ISO 7091';chamfer=standard=='ISO 7090'
        titles=(('Podložka se sražením','Chamfered plain washer','Unterlegscheibe mit Fase','Rondelle plate chanfreinée','Плоская шайба с фаской') if chamfer else
            ('Plochá podložka, malá řada','Plain washer, small series','Unterlegscheibe, kleine Reihe','Rondelle plate, série étroite','Плоская шайба, уменьшенная серия') if small else
            ('Plochá podložka, třída C','Plain washer, grade C','Unterlegscheibe, Produktklasse C','Rondelle plate, grade C','Плоская шайба, класс точности C') if coarse else
            ('Plochá podložka','Plain washer','Flache Unterlegscheibe','Rondelle plate','Плоская шайба'))
        for size in family['sizes']:
            v=dict(size);d=v['nominal_mm'];t=v['thickness_mm']
            v.update(filename='FW-'+standard.replace(' ','')+f'-M{d:05.1f}.prtz',standard=standard,
                family='02-CAP-HEAD' if small else '01-HEXAGON',width_mm=(v['outside_mm']-v['bore_mm'])/2,
                form='',chamfer_mm=t*family['chamfer_depth_fraction'],names=dict(zip(caps.LANGUAGES,titles)),
                density_kg_mm3=7.85e-6,material='Steel '+family['hardness_class'],hardness_class=family['hardness_class'])
            result.append(v)
    # Retain genuinely out-of-range historic sizes under their own CSN identity.
    # They must never acquire an ISO name without an applicable ISO definition.
    result.extend(v for v in historical_variants() if v['nominal_mm']>(36 if v['standard']=='ČSN 02 1703' else 64))
    assert len({v['filename'].casefold() for v in result})==len(result)
    return result


def author(native,v,target):
    native.run('new',type='part',name=target.stem)
    sk=native.run('sketch.create',name='Annular profile',plane='XY')['sketch']
    centers=set();diameters=[];circles=[]
    for diameter in (v['outside_mm'],v['bore_mm']):
        circle=native.run('sketch.circle.create',sketch=sk,center=[0,0],radius_mm=diameter/2)['geometry']
        center=native.run('sketch.entity.get',sketch=sk,entity=circle)['entity']['center']
        if center not in centers:
            native.run('sketch.constraint.create',sketch=sk,kind='point_reference',points=[center,'sketch_origin']);centers.add(center)
        diameters.append(native.run('sketch.dimension.create',sketch=sk,kind='diameter',geometry=[circle],value=diameter)['dimension']);circles.append(circle)
    annulus=native.run('feature.create',sketch=sk,parameters=dict(symmetric=False,
        sides=[caps.side(v['thickness_mm']),caps.side(operation='none')]))['container']
    chamfer=None
    if v['chamfer_mm']:
        edge=caps.circular_edge(native,annulus,v['thickness_mm'],v['outside_mm']/2)
        chamfer=native.run('chamfer.create',mode='equal_distance',distance_a_mm=v['chamfer_mm'],routes=[dict(edges=[edge])])['container']
    native.run('document.material.set',properties=[dict(key='MATERIAL_NAME',value=v['material']),
        dict(key='MASS_DENSITY',value=str(v['density_kg_mm3']),unit='kg/mm^3',
            descriptions=dict(zip(caps.LANGUAGES,('Hustota','Mass density','Massendichte','Masse volumique','Плотность'))))])
    params=native.run('document.parameters.get')['parameters']
    values=dict(name=v['names'],standard={'':v['standard']},material={'':v['material']},
        stock={'':f'M{v["nominal_mm"]:g}'},quantity={'':'-'})
    for p in params:
        if p['key'] in values:p['values']=values[p['key']]
    native.run('document.parameters.set',parameters=params);native.run('document.settings.set',precision=dict(decimal_places=6))
    native.run('document.relations.set',relations='mass = model.mass\n');native.run('regenerate')
    assert not native.run('history.list')['calculation_errors']
    native.run('save_as',path=str(target));native.run('close',discard=True);native.run('open',path=str(target))
    ids=dict(sketch=sk,circles=circles,diameters=diameters,annulus=annulus,chamfer=chamfer)
    washers.relations(native,target,ids);native.run('regenerate');assert not native.run('history.list')['calculation_errors']
    native.run('save');native.run('close');return ids


def main():
    p=argparse.ArgumentParser(description=__doc__);p.add_argument('--stage',type=Path,required=True);p.add_argument('--filename')
    p.add_argument('--cli',type=Path,default=ROOT/'build/cpp-windows-release/zima-cad-cli.exe');args=p.parse_args()
    stage=args.stage.resolve();stage.mkdir(parents=True,exist_ok=True);rows=variants()
    (stage/'manifest.json').write_text(json.dumps(dict(variants=rows),ensure_ascii=False,indent=2)+'\n',encoding='utf-8')
    for v in rows:
        if args.filename and args.filename!=v['filename']:continue
        target=stage/v['filename'];evidence=stage/'evidence'/target.stem
        if target.exists():
            assert (evidence/'identities.json').is_file(),target
            continue
        native=caps.studs.api.Native(args.cli.resolve(),evidence,
            settings_extra='\n[Paths]\nTemplates='+str(ROOT/'config/templates').replace('\\','/')+'\n')
        try:ids=author(native,v,target)
        finally:native.close()
        (evidence/'identities.json').write_text(json.dumps(ids,indent=2)+'\n',encoding='utf-8')
        print('Authored and reopened '+target.name,flush=True)


if __name__=='__main__':main()
