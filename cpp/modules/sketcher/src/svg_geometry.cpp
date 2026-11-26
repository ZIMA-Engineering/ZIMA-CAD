#include <zima/sketcher/svg_geometry.hpp>
#include <QByteArray>
#include <QCryptographicHash>
#include <cctype>
#include <QXmlStreamReader>
#include <QMap>
#include <algorithm>
#include <array>
#include <charconv>
#include <cmath>
#include <numbers>
#include <stdexcept>
#include <string_view>
namespace zima::sketcher {
namespace {
using P=std::array<double,2>;
constexpr double pi=std::numbers::pi;
[[noreturn]] void invalid(){throw std::runtime_error("SVG contains invalid vector geometry.");}
[[noreturn]] void unsupported(){throw std::runtime_error("SVG extrusion supports filled vector paths and shapes; this image uses unsupported painting.");}
P add(P a,P b){return {a[0]+b[0],a[1]+b[1]};}
P sub(P a,P b){return {a[0]-b[0],a[1]-b[1]};}
P mul(P a,double s){return {a[0]*s,a[1]*s};}
struct Matrix {
    double a{1},b{},c{},d{1},e{},f{};
    P point(P p)const{return {a*p[0]+c*p[1]+e,b*p[0]+d*p[1]+f};}
    Matrix operator*(const Matrix& o)const{return {a*o.a+c*o.b,b*o.a+d*o.b,a*o.c+c*o.d,b*o.c+d*o.d,a*o.e+c*o.f+e,b*o.e+d*o.f+f};}
};
struct Numbers {
    std::string text;std::size_t pos{};
    void space(){while(pos<text.size()&&(std::isspace(static_cast<unsigned char>(text[pos]))||text[pos]==','))++pos;}
    bool end(){space();return pos==text.size();}
    bool number(){space();return pos<text.size()&&(std::isdigit(static_cast<unsigned char>(text[pos]))||text[pos]=='+'||text[pos]=='-'||text[pos]=='.');}
    double take(){space();if(pos==text.size())invalid();if(text[pos]=='+')++pos;double n{};const auto r=std::from_chars(text.data()+pos,text.data()+text.size(),n);if(r.ec!=std::errc{}||r.ptr==text.data()+pos||!std::isfinite(n))invalid();pos=static_cast<std::size_t>(r.ptr-text.data());return n;}
    bool flag(){space();if(pos==text.size()||(text[pos]!='0'&&text[pos]!='1'))invalid();return text[pos++]=='1';}
    P pair(){const auto x=take();return {x,take()};}
};
Matrix transform(const QString& input) {
    Numbers n{input.toStdString()};Matrix out;
    while(!n.end()) {
        const auto start=n.pos;while(n.pos<n.text.size()&&std::isalpha(static_cast<unsigned char>(n.text[n.pos])))++n.pos;
        const auto name=n.text.substr(start,n.pos-start);n.space();if(n.pos==n.text.size()||n.text[n.pos++]!='(')invalid();
        std::vector<double> values;while(n.number()){values.push_back(n.take());if(values.size()>6)invalid();}n.space();if(n.pos==n.text.size()||n.text[n.pos++]!=')')invalid();
        Matrix m;
        if(name=="matrix"&&values.size()==6)m={values[0],values[1],values[2],values[3],values[4],values[5]};
        else if(name=="translate"&&(values.size()==1||values.size()==2)){m.e=values[0];m.f=values.size()==2?values[1]:0;}
        else if(name=="scale"&&(values.size()==1||values.size()==2)){m.a=values[0];m.d=values.size()==2?values[1]:m.a;}
        else if(name=="rotate"&&(values.size()==1||values.size()==3)){const auto angle=values[0]*pi/180;m.a=m.d=std::cos(angle);m.b=std::sin(angle);m.c=-m.b;if(values.size()==3){m.e=values[1]-m.a*values[1]-m.c*values[2];m.f=values[2]-m.b*values[1]-m.d*values[2];}}
        else if(name=="skewX"&&values.size()==1)m.c=std::tan(values[0]*pi/180);
        else if(name=="skewY"&&values.size()==1)m.b=std::tan(values[0]*pi/180);
        else invalid();
        out=out*m;
    }
    if(!std::isfinite(out.a+out.b+out.c+out.d+out.e+out.f)||std::abs(out.a*out.d-out.b*out.c)<1e-15)invalid();return out;
}
struct Path {
    std::vector<kernel::BSplineGeometry> loops;kernel::BSplineGeometry active;P current{},first{};bool started{};
    void cubic(P a,P b,P c,P d,std::array<double,4> w={1,1,1,1}) {
        if(active.poles.empty()){active.degree=3;active.poles.push_back({a[0],a[1],0});active.weights.push_back(w[0]);}
        for(const auto& [p,weight]:std::array{std::pair{b,w[1]},std::pair{c,w[2]},std::pair{d,w[3]}}){active.poles.push_back({p[0],p[1],0});active.weights.push_back(weight);}
        if(active.poles.size()>300000)throw std::runtime_error("SVG vector geometry exceeds the curve budget.");current=d;
    }
    void line(P p){if(std::hypot(current[0]-p[0],current[1]-p[1])>1e-13)cubic(current,add(current,mul(sub(p,current),1./3)),add(current,mul(sub(p,current),2./3)),p);current=p;}
    void close(){if(!started)return;line(first);if(active.poles.size()>=4){const auto spans=(active.poles.size()-1)/3;active.knots.insert(active.knots.end(),4,0);for(std::size_t i=1;i<spans;++i)active.knots.insert(active.knots.end(),3,double(i)/spans);active.knots.insert(active.knots.end(),4,1);active.validate();loops.push_back(std::move(active));}active={};current=first;started=false;}
    void move(P p){close();current=first=p;started=true;}
    void quadratic(P q,P end){cubic(current,add(current,mul(sub(q,current),2./3)),add(end,mul(sub(q,end),2./3)),end);}
    void arc(double rx,double ry,double angle,bool large,bool sweep,P end) {
        rx=std::abs(rx);ry=std::abs(ry);if(end==current)return;if(rx==0||ry==0){line(end);return;}
        const auto begin=current;const double phi=angle*pi/180,co=std::cos(phi),si=std::sin(phi);
        const auto delta=mul(sub(begin,end),.5);const double x=co*delta[0]+si*delta[1],y=-si*delta[0]+co*delta[1];
        const double factor=x*x/(rx*rx)+y*y/(ry*ry);if(factor>1){rx*=std::sqrt(factor);ry*=std::sqrt(factor);}
        const double term=std::max(0.,(rx*rx*ry*ry-rx*rx*y*y-ry*ry*x*x)/(rx*rx*y*y+ry*ry*x*x));
        const double sign=large==sweep?-1:1,cx=sign*std::sqrt(term)*rx*y/ry,cy=-sign*std::sqrt(term)*ry*x/rx;
        const P center{co*cx-si*cy+(begin[0]+end[0])/2,si*cx+co*cy+(begin[1]+end[1])/2};
        const P u{(x-cx)/rx,(y-cy)/ry},v{(-x-cx)/rx,(-y-cy)/ry};
        const double start=std::atan2(u[1],u[0]);double span=std::atan2(u[0]*v[1]-u[1]*v[0],u[0]*v[0]+u[1]*v[1]);
        if(!sweep&&span>0)span-=2*pi;if(sweep&&span<0)span+=2*pi;
        const int count=std::max(1,static_cast<int>(std::ceil(std::abs(span)/(pi/2))));
        const auto ellipse=[&](double t,double divisor=1.){return add(center,{(co*rx*std::cos(t)-si*ry*std::sin(t))/divisor,(si*rx*std::cos(t)+co*ry*std::sin(t))/divisor});};
        for(int i=0;i<count;++i){const double a=start+span*i/count,b=start+span*(i+1)/count,w=std::cos((b-a)/2);const P p=current,q=ellipse((a+b)/2,w),r=i+1==count?end:ellipse(b);const double cw=(1+2*w)/3;
            cubic(p,mul(add(p,mul(q,2*w)),1/(1+2*w)),mul(add(r,mul(q,2*w)),1/(1+2*w)),r,{1,cw,cw,1});}
    }
};
Path parse_path(const QString& input) {
    Numbers n{input.toStdString()};Path p;char command{},previous{};P last_cubic{},last_quad{};
    while(!n.end()) {
        if(std::isalpha(static_cast<unsigned char>(n.text[n.pos])))command=n.text[n.pos++];else if(!command)invalid();
        const char type=static_cast<char>(std::toupper(static_cast<unsigned char>(command)));const bool relative=std::islower(static_cast<unsigned char>(command));
        const auto point=[&] {const auto q=n.pair();return relative?add(p.current,q):q;};
        if(type=='Z'){p.close();command=0;previous=type;continue;}
        if(type!='M'&&!p.started){if(previous=='Z')p.move(p.current);else invalid();}if(!n.number())invalid();
        if(type=='M'){const auto q=point();p.move(q);command=relative?'l':'L';}
        else if(type=='L')p.line(point());
        else if(type=='H'){double v=n.take();p.line({relative?p.current[0]+v:v,p.current[1]});}
        else if(type=='V'){double v=n.take();p.line({p.current[0],relative?p.current[1]+v:v});}
        else if(type=='C'){const auto a=point(),b=point(),c=point();p.cubic(p.current,a,b,c);last_cubic=b;}
        else if(type=='S'){const auto a=(previous=='C'||previous=='S')?sub(mul(p.current,2),last_cubic):p.current,b=point(),c=point();p.cubic(p.current,a,b,c);last_cubic=b;}
        else if(type=='Q'){const auto q=point(),end=point();p.quadratic(q,end);last_quad=q;}
        else if(type=='T'){const auto q=(previous=='Q'||previous=='T')?sub(mul(p.current,2),last_quad):p.current,end=point();p.quadratic(q,end);last_quad=q;}
        else if(type=='A'){const double rx=n.take(),ry=n.take(),angle=n.take();const bool large=n.flag(),sweep=n.flag();p.arc(rx,ry,angle,large,sweep,point());}
        else invalid();previous=type;
    }
    p.close();return p;
}
double attr(const QXmlStreamAttributes& a,const char* name,double fallback=0) {
    if(!a.hasAttribute(QLatin1String(name)))return fallback;Numbers n{a.value(QLatin1String(name)).toString().toStdString()};const double v=n.take();if(!n.end())unsupported();return v;
}
struct Style {
    Matrix matrix;QString fill{"black"},color{"black"},stroke{"none"},visibility{"visible"};
    bool evenodd{},hidden{};double opacity{1},fill_opacity{1};
};
Style style(const Style& parent,const QXmlStreamAttributes& a) {
    Style result=parent;
    QMap<QString,QString> properties;
    for(const auto* key:{"fill","color","stroke","fill-rule","display","visibility","opacity","fill-opacity","clip-path","mask","filter","marker-start","marker-mid","marker-end","vector-effect","transform"})
        if(a.hasAttribute(QLatin1String(key)))properties[QLatin1String(key)]=a.value(QLatin1String(key)).toString().trimmed();
    for(const auto& declaration:a.value("style").toString().split(';',Qt::SkipEmptyParts)) {
        const auto colon=declaration.indexOf(':');if(colon<0)invalid();
        properties[declaration.left(colon).trimmed()]=declaration.mid(colon+1).trimmed();
    }
    for(auto it=properties.cbegin();it!=properties.cend();++it) {
        const auto& key=it.key();const auto& value=it.value();
        if(key=="fill")result.fill=value;
        else if(key=="color")result.color=value;
        else if(key=="stroke")result.stroke=value;
        else if(key=="fill-rule"){if(value!="evenodd"&&value!="nonzero")unsupported();result.evenodd=value=="evenodd";}
        else if(key=="display"){if(value=="none")result.hidden=true;}
        else if(key=="visibility"){if(value!="visible"&&value!="hidden"&&value!="collapse")unsupported();result.visibility=value;}
        else if(key=="opacity"||key=="fill-opacity") {
            Numbers n{value.toStdString()};const double v=std::clamp(n.take(),0.,1.);if(!n.end())unsupported();
            if(key=="opacity")result.opacity=parent.opacity*v;else result.fill_opacity=v;
        }
        else if(key=="clip-path"||key=="mask"||key=="filter"||key=="marker"||key.startsWith("marker-")||key=="vector-effect") {if(value!="none")unsupported();}
        else if(key=="transform")result.matrix=parent.matrix*transform(value);
        else if(key!="color"&&key!="stroke-width"&&key!="stroke-linejoin"&&key!="stroke-linecap"&&key!="stroke-miterlimit"&&key!="stroke-opacity")unsupported();
    }
    if(a.hasAttribute("class"))unsupported();return result;
}
bool filled_paint(const Style& style) {
    auto paint=style.fill.toLower().trimmed();if(paint=="currentcolor")paint=style.color.toLower().trimmed();
    if(paint=="none"||paint=="transparent")return false;
    if(paint.startsWith("url("))unsupported();
    if(paint.startsWith("rgba(")||paint.startsWith("hsla(")) {
        if(!paint.endsWith(')'))invalid();auto alpha=paint.mid(paint.lastIndexOf(',')+1);alpha.chop(1);alpha=alpha.trimmed();
        const bool percent=alpha.endsWith('%');if(percent)alpha.chop(1);bool ok=false;const double value=alpha.toDouble(&ok);if(!ok)unsupported();return value>0;
    }
    if(paint.startsWith('#')&&(paint.size()==5||paint.size()==9)) {
        bool ok=false;const auto alpha=paint.right(paint.size()==5?1:2).toUInt(&ok,16);if(!ok)invalid();return alpha>0;
    }
    return true;
}
}
std::vector<SvgFill> exact_svg_fills(const TemplateImage& image) {
    image.validate();if(image.format!="svg")unsupported();
    const auto decoded=QByteArray::fromBase64Encoding(QByteArray::fromStdString(image.data_base64),QByteArray::AbortOnBase64DecodingErrors);if(!decoded)invalid();
    const auto source_hash=QCryptographicHash::hash(decoded.decoded,QCryptographicHash::Sha256).toHex().toStdString();
    QXmlStreamReader xml(decoded.decoded);std::vector<Style> stack;std::vector<SvgFill> result;std::array<double,4> box{};bool root=false;std::size_t budget=0;
    while(!xml.atEnd()) {
        xml.readNext();if(xml.isDTD()||xml.isEntityReference())unsupported();
        if(xml.isEndElement()){if(stack.empty())invalid();stack.pop_back();continue;}if(!xml.isStartElement())continue;
        const auto tag=xml.name().toString();const auto a=xml.attributes();
        if(!root) {
            if(tag!="svg")invalid();root=true;
            if(a.hasAttribute("viewBox")){Numbers n{a.value("viewBox").toString().toStdString()};for(auto& v:box)v=n.take();if(!n.end())invalid();}
            else box={0,0,attr(a,"width"),attr(a,"height")};
            if(box[2]<=0||box[3]<=0)invalid();stack.push_back(style({},a));continue;
        }
        if(tag=="title"||tag=="desc"||tag=="metadata"||tag=="defs"){xml.skipCurrentElement();continue;}
        if(stack.empty())invalid();const auto state=style(stack.back(),a);
        if(state.hidden||state.opacity==0){xml.skipCurrentElement();continue;}
        if(tag=="g"){stack.push_back(state);continue;}
        Path path;
        if(tag=="path")path=parse_path(a.value("d").toString());
        else if(tag=="circle"||tag=="ellipse") {
            const double cx=attr(a,"cx"),cy=attr(a,"cy"),rx=tag=="circle"?attr(a,"r"):attr(a,"rx"),ry=tag=="circle"?rx:attr(a,"ry");if(rx<0||ry<0)invalid();
            if(rx>0&&ry>0){path.move({cx+rx,cy});path.arc(rx,ry,0,false,true,{cx-rx,cy});path.arc(rx,ry,0,false,true,{cx+rx,cy});path.close();}
        } else if(tag=="rect") {
            const double x=attr(a,"x"),y=attr(a,"y"),w=attr(a,"width"),h=attr(a,"height");if(w<0||h<0)invalid();
            double rx=attr(a,"rx",attr(a,"ry")),ry=attr(a,"ry",rx);if(rx<0||ry<0)invalid();rx=std::min(rx,w/2);ry=std::min(ry,h/2);
            if(w>0&&h>0){path.move({x+rx,y});path.line({x+w-rx,y});path.arc(rx,ry,0,false,true,{x+w,y+ry});path.line({x+w,y+h-ry});path.arc(rx,ry,0,false,true,{x+w-rx,y+h});path.line({x+rx,y+h});path.arc(rx,ry,0,false,true,{x,y+h-ry});path.line({x,y+ry});path.arc(rx,ry,0,false,true,{x+rx,y});path.close();}
        } else if(tag=="polygon"||tag=="polyline") {Numbers n{a.value("points").toString().toStdString()};if(!n.end()){path.move(n.pair());while(!n.end())path.line(n.pair());path.close();}}
        else if(tag=="line"){if(state.stroke!="none")unsupported();xml.skipCurrentElement();continue;}
        else unsupported();
        if(state.stroke!="none")unsupported();
        if(state.visibility=="visible"&&state.fill_opacity>0&&filled_paint(state)&&!path.loops.empty()) {
            if(state.fill.startsWith("url("))unsupported();
            SvgFill fill;fill.even_odd=state.evenodd;
            fill.source_key="svg:"+image.id+":"+source_hash+":"+std::to_string(result.size());
            const auto corners=image.corners();
            for(auto& curve:path.loops) {
                budget+=curve.poles.size();if(budget>300000)throw std::runtime_error("SVG vector geometry exceeds the curve budget.");
                for(auto& pole:curve.poles){const auto point=state.matrix.point({pole.x,pole.y});const double u=(point[0]-box[0])/box[2],v=(point[1]-box[1])/box[3];pole={corners[0][0]+u*(corners[1][0]-corners[0][0])+v*(corners[3][0]-corners[0][0]),corners[0][1]+u*(corners[1][1]-corners[0][1])+v*(corners[3][1]-corners[0][1]),0};}
                curve.validate();fill.boundaries.push_back(std::move(curve));
            }
            result.push_back(std::move(fill));
        }
        xml.skipCurrentElement();
    }
    if(xml.hasError()||!root||!stack.empty())invalid();if(result.empty())throw std::runtime_error("SVG contains no filled vector region.");return result;
}
namespace {
// Roots of y(t)-query.y in homogeneous cubic Bezier coordinates. Splitting at
// derivative roots bounds each monotonic polynomial interval before bisection.
int winding(const kernel::BSplineGeometry& curve,P point) {
    int result=0;
    for(std::size_t i=0;i+3<curve.poles.size();i+=3) {
        std::array<double,4> q;for(int k=0;k<4;++k)q[k]=(curve.poles[i+k].y-point[1])*curve.weights[i+k];
        const double a=-q[0]+3*q[1]-3*q[2]+q[3],b=3*q[0]-6*q[1]+3*q[2],c=-3*q[0]+3*q[1],d=q[0];
        const auto value=[&](double t){return ((a*t+b)*t+c)*t+d;};
        std::vector<double> splits{0,1};
        if(std::abs(a)>1e-20){const double discriminant=4*b*b-12*a*c;if(discriminant>=0)for(double root:{(-2*b-std::sqrt(discriminant))/(6*a),(-2*b+std::sqrt(discriminant))/(6*a)})if(root>0&&root<1)splits.push_back(root);}
        else if(std::abs(b)>1e-20){const double root=-c/(2*b);if(root>0&&root<1)splits.push_back(root);}
        std::ranges::sort(splits);
        for(std::size_t k=1;k<splits.size();++k){double low=splits[k-1],high=splits[k];const double lv=value(low),hv=value(high);if(!((lv<=0&&hv>0)||(hv<=0&&lv>0)))continue;
            for(int it=0;it<60;++it){const double middle=(low+high)/2;if((value(middle)>0)==(lv>0))low=middle;else high=middle;}
            const double t=(low+high)/2,u=1-t;const std::array bernstein{u*u*u,3*u*u*t,3*u*t*t,t*t*t};double x=0,w=0;for(int j=0;j<4;++j){const double weight=bernstein[j]*curve.weights[i+j];x+=weight*curve.poles[i+j].x;w+=weight;}
            if(x/w>point[0])result+=hv>lv?1:-1;
        }
    }
    return result;
}
int orientation(const kernel::BSplineGeometry& curve) {
    // Positive weights keep rational Bezier curves inside their pole hulls.
    // Adaptive numerical integration is used only to determine winding sign.
    double area=0;
    for(std::size_t i=0;i+3<curve.poles.size();i+=3) {
        const auto integrand=[&](double t){const double u=1-t;std::array b{u*u*u,3*u*u*t,3*u*t*t,t*t*t},db{-3*u*u,3*u*u-6*u*t,6*u*t-3*t*t,3*t*t};double x=0,y=0,w=0,dx=0,dy=0;for(int k=0;k<4;++k){const double v=curve.weights[i+k];x+=b[k]*v*curve.poles[i+k].x;y+=b[k]*v*curve.poles[i+k].y;w+=b[k]*v;dx+=db[k]*v*curve.poles[i+k].x;dy+=db[k]*v*curve.poles[i+k].y;}return (x*dy-y*dx)/(w*w);};
        const auto integrate=[&](const auto& self,double a,double b,double fa,double fm,double fb,double estimate,int depth)->double {const double m=(a+b)/2,l=integrand((a+m)/2),r=integrand((m+b)/2),left=(m-a)*(fa+4*l+fm)/6,right=(b-m)*(fm+4*r+fb)/6;const double sum=left+right;if(depth==18||std::abs(sum-estimate)<1e-11*(1+std::abs(sum)))return sum+(sum-estimate)/15;return self(self,a,m,fa,l,fm,left,depth+1)+self(self,m,b,fm,r,fb,right,depth+1);};
        const double a=integrand(0),m=integrand(.5),b=integrand(1);area+=integrate(integrate,0,1,a,m,b,(a+4*m+b)/6,0);
    }
    if(std::abs(area)<1e-14)invalid();return area>0?1:-1;
}
}
std::vector<kernel::BSplineGeometry> nonzero_fill_boundaries(const std::vector<kernel::BSplineGeometry>& curves) {
    std::vector<kernel::BSplineGeometry> result;
    for(std::size_t i=0;i<curves.size();++i){int outside=0;const auto p=curves[i].poles.front();for(std::size_t j=0;j<curves.size();++j)if(j!=i)outside+=winding(curves[j],{p.x,p.y});const int inside=outside+orientation(curves[i]);if((outside==0)!=(inside==0))result.push_back(curves[i]);}
    return result;
}
}
