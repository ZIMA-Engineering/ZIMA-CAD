"""Capture the reviewed key/keyway inputs shared by MECH and native key authoring."""
from html.parser import HTMLParser
from pathlib import Path
import json
import re

ROOT = Path(__file__).resolve().parents[1]
TABLES = ROOT / 'config/lib/03-ENGINEERING-TABLES/08-KEYS-KEYWAYS'

class Tables(HTMLParser):
    def __init__(self):
        super().__init__(); self.tables=[]; self.row=None; self.cell=None; self.active=False
    def handle_starttag(self, tag, attrs):
        if tag=='table': self.tables.append([]); self.active=True
        elif tag=='tr' and self.active: self.row=[]
        elif tag in ('td','th') and self.row is not None: self.cell=''
    def handle_data(self, data):
        if self.cell is not None: self.cell+=data
    def handle_endtag(self, tag):
        if tag in ('td','th') and self.cell is not None: self.row.append(self.cell.strip()); self.cell=None
        elif tag=='tr' and self.row is not None: self.tables[-1].append(self.row); self.row=None
        elif tag=='table': self.active=False

def tables(chapter):
    reader=Tables();reader.feed((TABLES/chapter/'0000-index/index_en.html').read_text(encoding='utf-8'))
    return reader.tables

def number(text):
    return float(text.replace(',','.').replace('−','-').replace('–','-'))

def interval(text):
    if text in ('–','-','—',''): return None
    values=re.findall(r'\d+(?:\.\d+)?',text)
    if len(values)!=2: raise ValueError(text)
    return [float(v) for v in values]

def din_woodruff():
    # Manufacturer DIN 6888 sheet, visually checked against its merged cells.
    # (b, h, disc, assignment I, assignment II, shaft A/B, hub A/B).
    # The sheet's 2 x 2.7 is a typo: MBO confirms 2 x 3.7, disc 10.
    # The automotive-only 2.5 x 3.7 row is not a general-purpose offering.
    data=[
        (1,1.4,4,(3,4),(6,8),1,1,.6,.6),
        (1.5,2.6,7,(4,6),(8,10),2,2,.8,.8),
        (2,2.6,7,(6,8),(10,12),1.8,1.8,1,1),
        (2,3.7,10,(6,8),(10,12),2.9,2.9,1,1),
        (3,3.7,10,(8,10),(12,17),2.5,2.8,1.4,1.1),
        (3,5,13,(8,10),(12,17),3.8,4.1,1.4,1.1),
        (3,6.5,16,None,(12,17),5.3,5.6,1.4,1.1),
        (4,5,13,(10,12),(17,22),3.5,4.1,1.7,1.1),
        (4,6.5,16,(10,12),(17,22),5,5.6,1.7,1.1),
        (4,7.5,19,None,(17,22),6,6.6,1.7,1.1),
        (5,6.5,16,(12,17),(22,30),4.5,5.4,2.2,1.3),
        (5,7.5,19,(12,17),(22,30),5.5,6.4,2.2,1.3),
        (5,9,22,None,(22,30),7,7.9,2.2,1.3),
        (6,7.5,19,(17,22),(30,38),5.1,6,2.6,1.7),
        (6,9,22,(17,22),(30,38),6.6,7.5,2.6,1.7),
        (6,10,25,(17,22),(30,38),7.6,8.5,2.6,1.7),
        (6,11,28,None,(30,38),8.6,9.5,2.6,1.7),
        (8,9,22,(22,30),(38,None),6.2,7.5,3,1.7),
        (8,11,28,(22,30),(38,None),8.2,9.5,3,1.7),
        (8,13,32,None,(38,None),10.2,11.5,3,1.7),
        (10,11,28,(30,38),(38,None),7.8,9.1,3.4,2.1),
        (10,13,32,(30,38),(38,None),9.8,11.1,3.4,2.1),
        (10,16,45,None,(38,None),12.8,14.1,3.4,2.1)]
    result=[]
    for series in ('A','B'):
        for assignment in ('I','II'):
            rows=[]
            for b,h,disc,group1,group2,ta,tb,ha,hb in data:
                domain=group1 if assignment=='I' else group2
                if domain is None: continue
                shaft_tolerance=.2 if (b==5 and h==9) or (b==6 and h>=10) or b>=8 else .1
                hub_tolerance=(.2 if b>=6 else .1) if series=='A' else (.2 if b==10 and h>=13 else .1)
                rows.append(dict(id=f'{b:g}x{h:g}x{disc:g}',diameter_over=domain[0],diameter_through=domain[1],
                    width=b,height=h,shaft_depth=ta if series=='A' else tb,hub_depth=ha if series=='A' else hb,
                    bottom_radius=.1 if b<=5 else .2,disc_diameter=disc,
                    shaft_dimension_upper=shaft_tolerance,shaft_dimension_lower=0,
                    hub_dimension_upper=hub_tolerance,hub_dimension_lower=0,
                    shaft_width_fit='P9',hub_width_fit='P9',retaining=None))
            result.append(dict(id=f'din6888-{series.lower()}-{assignment.lower()}',type='woodruff',
                source='https://wilh-wernecke.de/docs/Datenblatt_Scheibenfedern-DIN-6888.pdf',
                review='manufacturer-dimensional-table',rows=rows))
    return result

def main():
    rows=[]
    retaining={(number(r[0]),number(r[1])):r for r in tables('02-PARALLEL')[0][2:]}
    for r in tables('01-KEY-SIZES')[0][1:]:
        lo,hi,b,h,t,tu,tl,t1,t1u,t1l,_,radius=map(number,r)
        row=dict(id=f'{b:g}x{h:g}',diameter_over=lo,diameter_through=hi,width=b,height=h,
            shaft_depth=t,hub_depth=t1,bottom_radius=radius,disc_diameter=0,
            shaft_dimension_upper=-tl,shaft_dimension_lower=-tu,
            hub_dimension_upper=t1u,hub_dimension_lower=t1l,
            shaft_width_fit='P9',hub_width_fit='P9',retaining=None)
        if (b,h) in retaining:
            data=retaining[b,h];row['plain_lengths']=interval(data[8])
            if data[7].startswith('M'):
                row['retaining']=dict(thread=data[7],thread_depth=number(data[6]),
                    key_counterbore_diameter=number(data[3]),key_clearance_diameter=number(data[4]),
                    key_counterbore_depth=number(data[5]),two_lengths=interval(data[9]),one_lengths=interval(data[10]))
        rows.append(row)
    woodruff=[]
    for r in tables('04-WOODRUFF')[0][1:]:
        lo,hi,b,h=map(number,r[:4]);disc=number(r[6]);depth=number(r[5][2:]);hub=number(r[8][2:])
        woodruff.append(dict(id=f'{b:g}x{h:g}x{disc:g}',diameter_over=lo,diameter_through=hi,
            width=b,height=h,shaft_depth=depth,hub_depth=hub,bottom_radius=0,disc_diameter=disc,
            shaft_dimension_upper=0,shaft_dimension_lower=0,hub_dimension_upper=0,hub_dimension_lower=0,
            shaft_width_fit='P9',hub_width_fit='N8',shaft_dimension_fit='h11',hub_dimension_fit='B11',retaining=None))
    # Public manufacturer table, DIN 6885-1 high form WITH back clearance.
    # The other hub-depth column (interference) and set-screw recommendation
    # have different meanings and must not become retaining-key data.
    din=[]
    data=[(6,8,2,2,1.2,1.),(8,10,3,3,1.8,1.4),(10,12,4,4,2.5,1.8),
          (12,17,5,5,3.,2.3),(17,22,6,6,3.5,2.8),(22,30,8,7,4.,3.3),
          (30,38,10,8,5.,3.3),(38,44,12,8,5.,3.3),(44,50,14,9,5.5,3.8),
          (50,58,16,10,6.,4.3),(58,65,18,11,7.,4.4)]
    for lo,hi,b,h,t,t1 in data:
        deviation=.1 if b<=6 else .2
        length_ranges={2:(6,20),3:(6,36),4:(8,45),5:(10,56),6:(14,70),8:(18,90),
                       10:(22,110),12:(28,140),14:(36,160),16:(45,180),18:(50,200)}
        din.append(dict(id=f'{b}x{h}',diameter_over=lo,diameter_through=hi,width=b,height=h,
            shaft_depth=t,hub_depth=t1,bottom_radius=.08 if b<=4 else .16 if b<=8 else .25,disc_diameter=0,
            plain_lengths=length_ranges[b],
            key_edge_radius=.16 if b<=4 else .25 if b<=8 else .4,
            radius_source='https://www.mbo-osswald.de/en/shop/parallel-key-din-6885-form-a-round-ended-high-sized-version-a-'+f'{b}x{h}x{ {2:10,3:10,4:12,5:16,6:18,8:25,10:45,12:36,14:40,16:50,18:56}[b] }'+'-steel-bright',
            shaft_dimension_upper=0,shaft_dimension_lower=-deviation,
            hub_dimension_upper=deviation,hub_dimension_lower=0,
            shaft_width_fit='P9',hub_width_fit='P9',retaining=None))
    result=dict(schema=1,catalogues=[
        dict(id='zima-csn-parallel',type='parallel',source='ZIMA historical CSN 02 2507 / 02 2562 key/keyway tables; retaining keys CSN 02 2570 / 02 2575',
             review='historical-dimensional-data',rows=rows),
        dict(id='zima-woodruff',type='woodruff',source='ZIMA historical Woodruff key/keyway tables',
             review='historical-dimensional-data',rows=woodruff),
        dict(id='din6885-1-maedler',type='parallel',source='https://smarthost.maedler.de/datenblaetter/K42_861.pdf',
             review='manufacturer-dimensional-table',rows=din)]+din_woodruff())
    for catalogue in result['catalogues']:
        identifiers=[r['id'] for r in catalogue['rows']]
        if len(set(identifiers))!=len(identifiers): raise ValueError('Duplicate row identity')
    target=ROOT/'resources/data/mech/keyways.json';target.parent.mkdir(parents=True,exist_ok=True)
    target.write_text(json.dumps(result,ensure_ascii=False,indent=2)+'\n',encoding='utf-8')
    print({c['id']:len(c['rows']) for c in result['catalogues']})

if __name__=='__main__': main()
