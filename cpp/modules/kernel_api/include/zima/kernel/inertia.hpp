#pragma once
#include <zima/kernel/geometry_kernel.hpp>
#include <cmath>
#include <numbers>
namespace zima::kernel {
using Matrix3 = std::array<double,9>;
inline Matrix3 inertia_frame(Vec3 degrees) {
    const double k=std::numbers::pi/180, cx=std::cos(degrees.x*k),sx=std::sin(degrees.x*k),
        cy=std::cos(degrees.y*k),sy=std::sin(degrees.y*k),cz=std::cos(degrees.z*k),sz=std::sin(degrees.z*k);
    return {cz*cy,cz*sy*sx-sz*cx,cz*sy*cx+sz*sx,
        sz*cy,sz*sy*sx+cz*cx,sz*sy*cx-cz*sx,-sy,cy*sx,cy*cx};
}
inline Vec3 inertia_transform(const Matrix3& r,Vec3 p) {
    return {r[0]*p.x+r[1]*p.y+r[2]*p.z,r[3]*p.x+r[4]*p.y+r[5]*p.z,r[6]*p.x+r[7]*p.y+r[8]*p.z};
}
inline Matrix3 inertia_rotate(const Matrix3& tensor,const Matrix3& r,bool into_frame=false) {
    Matrix3 out{};
    for(int i=0;i<3;++i)for(int j=0;j<3;++j)for(int a=0;a<3;++a)for(int b=0;b<3;++b)
        out[3*i+j]+=(into_frame?r[3*a+i]:r[3*i+a])*tensor[3*a+b]*(into_frame?r[3*b+j]:r[3*j+b]);
    return out;
}
// Parallel-axis theorem; signed weights also support subtractive checks.
inline void inertia_shift(Matrix3& tensor,double volume,Vec3 delta) {
    const double d[]{delta.x,delta.y,delta.z},s=delta.x*delta.x+delta.y*delta.y+delta.z*delta.z;
    for(int i=0;i<3;++i)for(int j=0;j<3;++j)tensor[3*i+j]+=volume*((i==j?s:0)-d[i]*d[j]);
}
} // namespace zima::kernel
