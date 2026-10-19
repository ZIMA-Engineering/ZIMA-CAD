"""Independent axial-section volume of the selected straight-knurl nut model.

Uses tabulated dimensions and exact polygon/disk intersections, without native
geometry, tessellation or cached model properties. The selected knurl depth is
pitch/4, matching the explicitly documented CAD execution.
"""
import importlib.util
import math
from pathlib import Path

spec=importlib.util.spec_from_file_location('sections',Path(__file__).with_name('castle-nut-volume.py'))
sections=importlib.util.module_from_spec(spec);spec.loader.exec_module(sections)


def volume(v):
    h,k,R,r,f,c,t=[v[key] for key in ('height_mm','head_height_mm','outside_mm',
        'hub_diameter_mm','root_radius_mm','chamfer_mm','knurl_pitch_mm')]
    R/=2;r/=2;rb=v['bore_mm']/2;mouth=v['mouth_chamfer_mm']
    assert 0<f<=h-k+1e-9 and r+f<R and 0<c<k
    teeth=round(2*math.pi*R/t)
    triangle=[(R-t/4,0),(R+t/4,t/2),(R+t/4,-t/2)]
    assert teeth>=2 and 2*math.atan2(t/2,R+t/4)<2*math.pi/teeth
    # Added material at the concave quarter-circle shoulder, integrated as
    # 2*pi*x dA; unlike a convex outside fillet, this increases blank volume.
    root=2*math.pi*(r*f*f*(1-math.pi/4)+f**3*(5/6-math.pi/4))
    answer=math.pi*r*r*h+math.pi*(R*R-r*r)*k+root
    answer-=math.pi*c*c*(R-c/3)
    groove_area=sections.disk_polygon(triangle,R)
    groove_volume=groove_area*(k-c)+sections.integral(
        lambda z:sections.disk_polygon(triangle,R-z),0,c,1e-10)
    answer-=teeth*groove_volume
    clearance=v['clearance_depth_mm']
    if clearance:
        rc=v['clearance_diameter_mm']/2
        assert clearance>=mouth and clearance<=h-mouth and rc>=rb+mouth
        answer-=math.pi*(rb*rb*(h-clearance)+rc*rc*clearance)
        mouths=1
    else:
        answer-=math.pi*rb*rb*h;mouths=2
    answer-=mouths*math.pi*(rb*mouth*mouth+mouth**3/3)
    assert answer>0
    return answer
