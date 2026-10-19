"""Native 45-degree socket-mouth cutter linked to the actual hexagon rim.

The cutter is clipped at the existing broach floor when its virtual cone apex
would otherwise deepen the opening. All contacts use persisted point sources.
"""
import math


def source_point(native,owner,x,z):
    result=[]
    for ref in native.run('reference.list',kind='point',owner=owner)['items']:
        point=native.run('reference.get',kind='point',owner=ref['owner'],key=ref['key'],instance_path=ref.get('instance_path',''))['position']
        if abs(point[0]-x)<1e-6 and abs(point[1])<1e-6 and abs(point[2]-z)<1e-6:result.append(ref)
    assert result,('Socket rim point missing',owner,x,z)
    return min(result,key=lambda r:len(r['key']))


def attach(native,sk,ref):
    return native.run('sketch.reference.create',sketch=sk,kind='point',owner=ref['owner'],key=ref['key'],instance_path=ref.get('instance_path',''))['reference']


def build(native,v,opening,side,existing=None):
    k=v['head_height_mm'];radius=v['drive_width_mm']/math.sqrt(3);depth=v['drive_depth_mm']
    rim=source_point(native,opening,radius,-k)
    clipped=radius>depth+1e-8
    entity=lambda sk,g:native.run('sketch.entity.get',sketch=sk,entity=g)['entity']
    if existing:
        sk=native.run('feature.get',container=existing)['sketch']
        items=native.run('sketch.entities',sketch=sk)['items']
        segments=[entity(sk,i['id']) for i in items if i['kind']=='segment']
        axis=next(s for s in segments if s['centerline']);edges=[s for s in segments if not s['construction']]
        assert len(edges)==3,'Only the existing triangular factory entrance is updated in place'
        for item in items:
            if item['kind']=='dimension':native.run('sketch.dimension.delete',sketch=sk,dimension=item['id'])
            elif item['kind']=='constraint':native.run('sketch.constraint.delete',sketch=sk,constraint=item['id'])
        for item in items:
            if item['kind']=='external_reference':native.run('sketch.reference.delete',sketch=sk,reference=item['id'])
        # Keep original visible top/slope identities and the owning feature/axis.
        top,slope,closing=edges;first=top['first'];outer=top['second'];lower=slope['second']
        native.run('sketch.point.move',sketch=sk,point=first,position=[0,k])
        if first!=axis['second']:
            native.run('sketch.point.move',sketch=sk,point=axis['second'],position=[0,k])
            native.run('sketch.constraint.create',sketch=sk,kind='coincident',points=[first,axis['second']])
        native.run('sketch.point.move',sketch=sk,point=outer,position=[radius,k])
        native.run('sketch.point.move',sketch=sk,point=lower,position=[max(0,radius-depth),k-min(radius,depth)])
        edge_ids=[top['id'],slope['id'],closing['id']]
        if clipped:
            native.run('sketch.geometry.delete',sketch=sk,geometry=closing['id'])
            low=native.run('sketch.segment.create',sketch=sk,first=[radius-depth,k-depth],second=[0,k-depth])['geometry']
            close=native.run('sketch.segment.create',sketch=sk,first=[0,k-depth],second=[0,k])['geometry'];edge_ids=[top['id'],slope['id'],low,close]
        axis_id=axis['id'];origin=axis['first']
    else:
        sk=native.run('sketch.create',name='Socket entrance profile',plane='XZ')['sketch']
        axis_id=native.run('sketch.segment.create',sketch=sk,first=[0,0],second=[0,k],construction=True)['geometry']
        native.run('sketch.segment.centerline',sketch=sk,segment=axis_id,centerline=True)
        origin=entity(sk,axis_id)['first']
        points=[[0,k],[radius,k],[radius-depth,k-depth],[0,k-depth]] if clipped else [[0,k],[radius,k],[0,k-radius]]
        edge_ids=[native.run('sketch.segment.create',sketch=sk,first=p,second=points[(i+1)%len(points)])['geometry'] for i,p in enumerate(points)]
        outer=entity(sk,edge_ids[0])['second']
    native.run('sketch.constraint.create',sketch=sk,kind='point_reference',points=[origin,'sketch_origin'])
    native.run('sketch.constraint.create',sketch=sk,kind='vertical',geometry=[axis_id])
    native.run('sketch.constraint.create',sketch=sk,kind='horizontal',geometry=[edge_ids[0]])
    native.run('sketch.constraint.create',sketch=sk,kind='vertical',geometry=[edge_ids[-1]])
    reference=attach(native,sk,rim)
    native.run('sketch.constraint.create',sketch=sk,kind='point_reference',points=[outer,reference])
    if clipped:
        floor=attach(native,sk,source_point(native,opening,radius,-k+depth))
        bottom=entity(sk,edge_ids[-1])['first']
        native.run('sketch.constraint.create',sketch=sk,kind='horizontal',geometry=[edge_ids[2]])
        native.run('sketch.constraint.create',sketch=sk,kind='horizontal',points=[bottom,floor])
        # Directed top edge points outwards; the descending cutter edge points
        # towards the axis. Their included angle is 135 degrees (45 to the rim).
        native.run('sketch.dimension.create',sketch=sk,kind='angle_between',geometry=[edge_ids[0],edge_ids[1]],value=135)
    else:native.run('sketch.constraint.create',sketch=sk,kind='equal_length',geometry=[edge_ids[0],edge_ids[-1]])
    solved=native.run('sketch.solve_status',sketch=sk)
    assert solved['remaining_degrees_of_freedom']==0 and solved['maximum_residual']<1e-8,solved
    slope_end=entity(sk,entity(sk,edge_ids[1])['second'])
    expected_x=max(0,radius-depth) if clipped else 0
    assert abs(slope_end['x']-expected_x)<1e-8 and abs(slope_end['y']-(k-min(radius,depth)))<1e-8,slope_end
    if existing:return existing
    rotation=side(operation='revolution');rotation.update(rotation_extent='full',angle_degrees=360)
    return native.run('feature.create',sketch=sk,combine='subtract',parameters=dict(axis_segment_id=axis_id,symmetric=False,sides=[rotation,side(operation='none')]))['container']
