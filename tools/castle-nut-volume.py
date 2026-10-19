"""Independent section integration for a hex nut with three round-root slots.

Polygon/disk intersections use exact line and circular-sector integrals. No
native document, OCCT shape or generated triangulation enters this calculation.
"""
import math


def clip(poly,nx,ny,limit):
    result=[]
    for a,b in zip(poly,poly[1:]+poly[:1]):
        fa=a[0]*nx+a[1]*ny-limit;fb=b[0]*nx+b[1]*ny-limit
        if fa<=0:result.append(a)
        if (fa<0<fb) or (fb<0<fa):
            t=fa/(fa-fb);result.append((a[0]+t*(b[0]-a[0]),a[1]+t*(b[1]-a[1])))
    return result


def disk_polygon(poly,radius):
    area=0.;r2=radius*radius
    for a,b in zip(poly,poly[1:]+poly[:1]):
        dx=b[0]-a[0];dy=b[1]-a[1];aa=dx*dx+dy*dy
        if aa<1e-24:continue
        bb=2*(a[0]*dx+a[1]*dy);cc=a[0]*a[0]+a[1]*a[1]-r2
        roots=[0.,1.];disc=bb*bb-4*aa*cc
        if disc>0:
            for t in ((-bb-math.sqrt(disc))/(2*aa),(-bb+math.sqrt(disc))/(2*aa)):
                if 0<t<1:roots.append(t)
        roots.sort()
        for lo,hi in zip(roots,roots[1:]):
            p=(a[0]+lo*dx,a[1]+lo*dy);q=(a[0]+hi*dx,a[1]+hi*dy)
            x=a[0]+(lo+hi)*dx/2;y=a[1]+(lo+hi)*dy/2
            cross=p[0]*q[1]-p[1]*q[0]
            # An unsplit tangent segment lies outside the disk even though
            # its midpoint is on the circle. Account for floating roundoff.
            inside=x*x+y*y<r2-8*math.ulp(r2)
            area+=(cross if inside else r2*math.atan2(cross,p[0]*q[0]+p[1]*q[1]))/2
    return abs(area)


def integral(f,a,b,tolerance):
    fa,fb,fm=f(a),f(b),f((a+b)/2)
    initial=(b-a)*(fa+4*fm+fb)/6
    def adapt(a,b,fa,fm,fb,estimate,tol,depth):
        mid=(a+b)/2;fl=f((a+mid)/2);fr=f((mid+b)/2)
        left=(mid-a)*(fa+4*fl+fm)/6;right=(b-mid)*(fm+4*fr+fb)/6
        delta=left+right-estimate
        if abs(delta)<=15*tol:return left+right+delta/15
        if depth==0:raise ArithmeticError('Independent section integration did not converge')
        return adapt(a,mid,fa,fl,fm,left,tol/2,depth-1)+adapt(mid,b,fm,fr,fb,right,tol/2,depth-1)
    return adapt(a,b,fa,fm,fb,initial,tolerance,24)


def volume(v):
    s,m,n,h,rb,c=[v[k] for k in ('across_flats_mm','height_mm','slot_width_mm','slot_floor_mm','bore_mm','mouth_chamfer_mm')]
    rb/=2;r=s/math.sqrt(3);a=s/2;root=n/2
    assert rb>n,'Slot-strip overlaps must remain inside the bore'
    assert 0<h<h+root<m
    poly=[(r*math.cos(i*math.pi/3),r*math.sin(i*math.pi/3)) for i in range(6)]
    tan=math.tan(math.radians(v['chamfer_degrees']));bevel=(r-a)*tan
    crown=v['crown_diameter_mm']/2 if v['crown_diameter_mm'] else r
    crown_start=h-v.get('crown_relief_mm',0)
    def strip(radius,w):
        w=min(w,radius)
        return 2*(w*math.sqrt(max(0,radius*radius-w*w))+radius*radius*math.asin(w/radius))
    def section(z,above_crown):
        outer=min(r,a+min(z,m-z)/tan)
        if above_crown:outer=min(outer,crown)
        bore=rb+max(0,c-z,c-(m-z))
        area=disk_polygon(poly,outer)-math.pi*bore*bore
        if z<=h:return area
        w=root if z>=h+root else math.sqrt(max(0,root*root-(z-h-root)**2))
        for angle in (0,math.pi/3,2*math.pi/3):
            nx,ny=math.cos(angle),math.sin(angle)
            cut=clip(clip(poly,nx,ny,w),-nx,-ny,w)
            area-=disk_polygon(cut,outer)-strip(bore,w)
        return area
    knots=sorted({0.,c,bevel,crown_start,h,h+root,m-bevel,m-c,m})
    knots=[z for z in knots if 0<=z<=m];answer=0.
    for lo,hi in zip(knots,knots[1:]):
        if hi-lo<1e-12:continue
        above=(lo+hi)/2>crown_start
        if lo>=h and hi<=h+root:
            # Remove the square-root endpoint singularity at the slot floor.
            x=math.acos(max(-1,min(1,1-(lo-h)/root)))
            y=math.acos(max(-1,min(1,1-(hi-h)/root)))
            f=lambda t:section(h+root*(1-math.cos(t)),above)*root*math.sin(t)
            answer+=integral(f,x,y,1e-9*max(1,s*s*m))
        else:
            answer+=integral(lambda z:section(z,above),lo,hi,1e-9*max(1,s*s*m))
    return answer
