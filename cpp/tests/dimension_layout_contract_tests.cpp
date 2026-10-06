#include "../app/dimension_properties_fields.hpp"
#include "../common/technical_font.hpp"
#include "../app/drawing_annotation_layout.hpp"
#include <QApplication>
#include <QDialogButtonBox>
#include <QImage>
#include <QKeyEvent>
#include <QMouseEvent>
#include <QPainter>
#include <QPushButton>
#include <QTemporaryDir>
#include <QVariantAnimation>
#include <iostream>
#include <source_location>
#include <zima/viewer/dimension_text_layer.hpp>
#include <zima/drawing_render/crop_path.hpp>
#include <zima/drawing_render/sheet_renderer.hpp>
#include <zima/drawing/detail_view.hpp>
#include <zima/assembly/assembly_document.hpp>
#include <zima/document/dimension_layout_json.hpp>
#include <zima/drawing/measurement_dimension.hpp>
#include <zima/drawing/dimension_text.hpp>
#include <zima/sketcher/sketch.hpp>
#include <zima/document/object_annotation_frames.hpp>
#include <zima/document/viewer_packet_json.hpp>
#include <zima/drawing/model_annotations.hpp>
#include <zima/kernel/mirror_geometry.hpp>
#include <zima/viewer/annotation_arrow.hpp>
#include <zima/viewer/dimension_presentation.hpp>
#include <zima/viewer/mesh_view.hpp>
using namespace zima;
void require(bool b, const char *m) {
    if (!b)
        throw std::runtime_error(m);
}
void near(double a, double b, std::source_location where=std::source_location::current()) { if(std::abs(a-b)>=1e-6)throw std::runtime_error("Geometric measure changed at line "+std::to_string(where.line())+": "+std::to_string(a)+" vs "+std::to_string(b)); }
template <class F> void rejects(F f) {
    bool rejected = false;
    try {
        f();
    } catch (const std::exception &) {
        rejected = true;
    }
    require(rejected, "Invalid presentation accepted");
}
void flush() { QApplication::processEvents(); }
void verify_occurrence_bounds() {
    kernel::ViewerMesh mesh;
    auto frame = kernel::annotation_frame({10,20,30}, {0,0,90});
    mesh.annotation_frames[{"shared", "first"}] = frame;
    const auto point = [&](std::string owner, std::string path, kernel::Vec3 value) {
        kernel::ViewerPoint p;
        p.reference = {owner, "point", path}; p.position = value;
        mesh.points.push_back(p);
    };
    point("shared", "first", frame.world({1,2,3}));
    point("shared", "second", {100,200,300});
    point("other", "second", {-10,-20,-30});
    point("shared", "first", frame.world({4,5,6}));
    point("", "first", {1e9,1e9,1e9});
    point("shared", "first", {std::numeric_limits<double>::quiet_NaN(),0,0});
    kernel::ViewerAxis axis;
    axis.reference = {"axis", "axis", "first"};
    axis.point = {7,8,9}; axis.direction = {0,0,2}; axis.display_length = 10;
    mesh.original_references.axes.push_back(axis);
    axis.reference = {"shared", "axis", "first"}; axis.display_length = 1e6;
    mesh.axes.push_back(axis);
    const auto bounds = kernel::object_envelopes(mesh);
    const auto& first = bounds.at({"shared", "first"});
    near(first.minimum.x,1); near(first.minimum.y,2); near(first.minimum.z,3);
    near(first.maximum.x,4); near(first.maximum.y,5); near(first.maximum.z,6);
    require(bounds.at({"shared", "second"}).minimum == kernel::Vec3{100,200,300},
            "Repeated source occurrences shared bounds");
    require(bounds.at({"other", "second"}).minimum == kernel::Vec3{-10,-20,-30},
            "Different owners shared bounds");
    require(bounds.at({"axis", "first"}).minimum == kernel::Vec3{7,8,4} &&
            bounds.at({"axis", "first"}).maximum == kernel::Vec3{7,8,14},
            "Axis-only occurrence lost its display extent");
    require(!bounds.contains({"", "first"}), "Invalid reference acquired bounds");
    mesh.points.front().position = frame.world({-8,2,3});
    near(kernel::object_envelopes(mesh).at({"shared", "first"}).minimum.x,-8);
}
void mouse(QWidget *w, QEvent::Type type, QPointF p, Qt::MouseButton button,
           Qt::MouseButtons buttons) {
    QMouseEvent event(type, p, w->mapToGlobal(p.toPoint()), button, buttons, Qt::NoModifier);
    QApplication::sendEvent(w, &event);
    flush();
}
Q_NEVER_INLINE void verify_vertical_dimension_clearance() {
        {
            // Near-vertical outside labels used a fixed 17 px extension even
            // when the ISO text mask extended back across the endpoint arrow.
            QImage proof(900,600,QImage::Format_ARGB32_Premultiplied);proof.fill(QColor("#eef0f2"));
            QPainter painter(&proof);
            bool collision=false;
            for(int pixels:{12,24,36})for(double sign:{-1.,1.})for(double side:{-1.,1.})for(double tilt:{0.,.15,-.15}) {
                auto font=zima::technical_font();font.setPixelSize(pixels);
                kernel::ViewerDimension d;d.witness_first={-20,0,0};d.witness_second={-20,80,0};
                d.line_first={0,0,0};d.line_second={0,80,0};d.label_position=kernel::Vec3{side*70,40,0};
                const auto project=[&](kernel::Vec3 p){return QPointF(p.x+std::sin(tilt)*p.y+.3*p.z,sign*std::cos(tilt)*p.y);};
                const QString text="80,000 mm";
                const auto layout=viewer::dimension_text_presentation(d,project,font,text,2,1.5);
                require(layout.valid&&layout.oblique&&layout.outside,"Vertical label fixture is not an outside dimension");
                QTransform transform;transform.translate(layout.text_baseline.x(),layout.text_baseline.y());transform.rotate(layout.text_angle);
                QPainterPath box;box.addRect(viewer::dimension_text_box(font,text,2));box=transform.map(box);
                for(const auto& [tip,direction]:layout.arrows) {
                    QPainterPath arrow;arrow.addPolygon(viewer::annotation_arrow(tip,direction,10));arrow.closeSubpath();
                    collision|=box.intersects(arrow);
                }
                if(pixels==24&&tilt==0) {
                    painter.save();painter.translate(side<0?225:675,sign<0?180:420);
                    painter.setPen(QPen(Qt::black,1.5));painter.setBrush(Qt::black);
                    for(const auto& curve:layout.curves)painter.drawPolyline(curve);
                    for(const auto& [tip,direction]:layout.arrows)painter.drawPolygon(viewer::annotation_arrow(tip,direction,10));
                    const std::array labels{viewer::DimensionTextLabel{text,layout.text_baseline,layout.text_angle,font,Qt::black}};
                    viewer::paint_dimension_text_layer(painter,labels,2,[](QPainter& p,const QPainterPath& area){p.fillPath(area,QColor("#eef0f2"));});
                    painter.restore();
                }
            }
            painter.end();require(proof.save("build/vertical-dimension-arrow-clearance.png"),"Cannot save vertical dimension proof");
            require(!collision,"Vertical/near-vertical ISO label mask covers its endpoint arrow");
        }
        {
            viewer::MeshView view;view.resize(900,700);view.set_active_sketch_owner("vertical-sketch");
            auto font=zima::technical_font();font.setPixelSize(24);view.setFont(font);view.show();flush();
            for(double side:{-1.,1.}) {
                kernel::ViewerDimension d;d.reference={"vertical-sketch","dimension:vertical",{}};
                d.witness_second={0,80,0};d.line_first={15,0,0};d.line_second={15,80,0};
                d.label_position=kernel::Vec3{15+side*20,40,0};d.value=80;d.display_text_override="80,000 mm";
                kernel::ViewerMesh mesh;mesh.vertices={d.witness_first,d.witness_second,d.line_first,d.line_second};mesh.dimensions={d};
                view.set_mesh(mesh);view.set_view_direction({.2,.1,1});
                for(auto* animation:view.findChildren<QVariantAnimation*>())animation->setCurrentTime(animation->duration());
                view.fit_all();flush();
                const viewer::ViewerCandidate candidate{viewer::CandidateKind::Dimension,0,0,"vertical-sketch","dimension:vertical",{}};
                const auto a=view.dimension_handle_position(candidate,1),b=view.dimension_handle_position(candidate,2);
                require(a&&b,"Vertical Sketch dimension has no endpoint grips");
                const auto labeled=view.grabFramebuffer();
                require(labeled.save(side<0?"build/sketch-vertical-dimension-left.png":"build/sketch-vertical-dimension-right.png"),
                    "Cannot capture Sketch vertical dimension");
                mesh.dimensions.front().display_text_override=" ";view.set_mesh(mesh);flush();
                const auto bare=view.grabFramebuffer();const auto direction=viewer::dimension_screen_unit(*b-*a);
                for(int end:{0,1})for(int distance=2;distance<=8;++distance) {
                    const auto point=((end?*b:*a)+direction*(end?distance:-distance))*labeled.devicePixelRatio();
                    const int x=qRound(point.x()),y=qRound(point.y());
                    require(labeled.valid(x,y)&&labeled.pixel(x,y)==bare.pixel(x,y),
                        "ISO text mask erased the vertical Sketch dimension arrow in the real View");
                }
            }
        }
    std::cout << "Vertical ISO label/arrow clearance passed" << std::endl;
}
void verify_model_dimension_units() {
    kernel::ViewerDimension source;source.value=25.4;source.reference={"part","length",""};
    source.witness_first={0,0,0};source.witness_second={25.4,0,0};
    source.line_first={0,8,0};source.line_second={25.4,8,0};source.plane_normal={0,0,1};
    viewer::MeshView view;view.resize(900,600);view.show();
    kernel::ViewerMesh mesh;mesh.dimensions.push_back(source);view.set_mesh(mesh);
    view.set_selection_contract({viewer::CandidateKind::Dimension});view.fit_all();flush();
    viewer::ViewerCandidate candidate;candidate.kind=viewer::CandidateKind::Dimension;
    candidate.owner_id="part";candidate.semantic_key="length";candidate.geometry_index=0;
    for(const auto& [unit,scale]:std::array<std::pair<const char*,double>,4>{{{"mm",1},{"cm",10},{"m",1000},{"in",25.4}}})
        for(const bool radians:{false,true}) {
            viewer::DimensionDisplayUnits units{scale,unit,radians?180./std::numbers::pi:1.,radians?"rad":"°"};
            view.set_dimension_display_units(units);view.set_dimension_decimal_places(6);flush();
            const auto expected=kernel::dimension_number(25.4/scale,6)+unit;
            require(view.dimension_label_text(view.mesh().dimensions.front()).toStdString()==expected,
                "Model dimension text did not follow View units");
            require(view.candidate_dimension_value(candidate)==25.4&&view.dimension_source(candidate)->value==25.4&&
                view.mesh().dimensions.front().reference==source.reference&&view.mesh().dimensions.front().witness_second==source.witness_second,
                "Changing View units modified native values, references or geometry");
            const auto label=view.candidate_dimension_label_position(candidate);
            require(label.has_value(),"Converted dimension label lost its screen position");
            for(int handle=0;handle<3;++handle)require(view.dimension_handle_position(candidate,handle).has_value(),
                "Converted dimension lost a presentation grip");
            const auto picked=view.selection_candidates_at(*label);
            require(std::ranges::any_of(picked,[](const auto& hit){return hit.kind==viewer::CandidateKind::Dimension&&hit.owner_id=="part"&&hit.semantic_key=="length";}),
                "Converted dimension label is not offered by the common picker");
            for(const auto kind:{kernel::ViewerDimensionKind::Radius,kernel::ViewerDimensionKind::Diameter,kernel::ViewerDimensionKind::Angular}) {
                auto d=source;d.kind=kind;d.value=kind==kernel::ViewerDimensionKind::Angular?90:25.4;
                d.label_prefix=kind==kernel::ViewerDimensionKind::Radius?"R":kind==kernel::ViewerDimensionKind::Diameter?"⌀":"";
                d.unit_suffix=kind==kernel::ViewerDimensionKind::Angular?"°":"mm";
                const auto expected=d.label_prefix+kernel::dimension_number(d.value/(kind==kernel::ViewerDimensionKind::Angular?units.degrees_per_unit:scale),6)+
                    (kind==kernel::ViewerDimensionKind::Angular?units.angle_suffix:unit);
                require(view.dimension_label_text(d).toStdString()==expected,"Radius, diameter or angular label uses native units");
                require(d.value==(kind==kernel::ViewerDimensionKind::Angular?90:25.4),"Formatting changed a source dimension");
            }
            auto count=source;count.label_only=true;count.unit_suffix.clear();count.label_prefix="N = ";count.value=4;
            require(view.dimension_label_text(count)=="N = 4","Unit conversion scaled a Pattern count");
            auto thread=source;thread.display_text_override="M10 × 1.5";
            require(view.dimension_label_text(thread)==QString::fromUtf8("M10 × 1.5"),"Units modified a thread catalog designation");
            auto styled=source;kernel::DimensionTextStyle style;style.prefix="A ";style.decimals=5;style.suffix="mm";
            styled.source_text_style=style;styled.display_text_override=kernel::dimension_text(styled,style);
            require(view.dimension_label_text(styled).toStdString()=="A "+kernel::dimension_number(25.4/scale,5)+unit,
                "Generated style text bypassed nominal unit conversion or its explicit precision");
            style.tolerance_mode="symmetric";style.symmetric_tolerance="0.0005";styled.source_text_style=style;
            styled.display_text_override=kernel::dimension_text(styled,style);
            const std::string tolerance_label=std::string(unit)=="mm"?"A 25,4mm ±0,0005":std::string(unit)=="cm"?"A 2,54cm ±0,00005":
                std::string(unit)=="m"?"A 0,0254m ±0,0000005":"A 25,4mm ±0,0005 (≈1in)";
            require(QString(view.dimension_label_text(styled)).replace(QChar(0x1e),' ').toStdString()==tolerance_label,
                "Converted tolerance is neither exact nor explicitly secondary");
            require(styled.source_text_style==style,"Rendering rewrote the authoritative tolerance");
            style.tolerance_mode.clear();style.suffix=" custom text";styled.source_text_style=style;
            styled.display_text_override=kernel::dimension_text(styled,style);
            require(view.dimension_label_text(styled).toStdString()==styled.display_text_override,"Units rewrote an authored custom suffix");
            styled.source_text_style->text_override="Literal 25.4mm";styled.display_text_override="Literal 25.4mm";
            require(view.dimension_label_text(styled)=="Literal 25.4mm","Units rewrote a literal text override");
        }
    rejects([&]{view.set_dimension_display_units({0,"in",1,"°"});});
    rejects([&]{view.set_dimension_display_units({1,"mm",-1,"rad"});});
    view.close();
}
void verify_converted_tolerance_layout() {
    auto* app=QCoreApplication::instance();const auto previous=app->property("zimaStackedTolerances");
    app->setProperty("zimaStackedTolerances",true);
    viewer::MeshView view;view.resize(1000,700);
    auto font=zima::technical_font();font.setPixelSize(28);view.setFont(font);
    kernel::ViewerDimension d;d.value=25.4;d.reference={"units-sketch","dimension:test",{}};
    d.witness_second={25.4,0,0};d.line_first={0,8,0};d.line_second={25.4,8,0};
    kernel::DimensionTextStyle style;style.value_unit="in";style.suffix="in";style.decimals=4;
    style.tolerance_mode="deviations";style.upper_tolerance="0.0005";style.lower_tolerance="0.0010";
    d.source_text_style=style;
    auto text=view.dimension_label_text(d);auto runs=viewer::dimension_text_runs(font,text);
    require(runs.size()==3&&runs[0].text=="25,4mm"&&runs[1].text=="+0,01270"&&runs[2].text=="-0,02540",
        "Exact converted deviations did not use the converted style when stacking");
    require(d.source_text_style==style,"Stacking changed persisted deviation strings");
    d.value=10;style.value_unit.clear();style.suffix.clear();style.upper_tolerance="0.01";style.lower_tolerance="0.02";
    d.source_text_style=style;view.set_dimension_display_units({25.4,"in",1,"°"});view.set_dimension_decimal_places(3);
    text=view.dimension_label_text(d);runs=viewer::dimension_text_runs(font,text);
    require(runs.size()==4&&runs[0].text=="10 [mm]"&&runs[1].text=="+0,01"&&runs[2].text=="-0,02"&&runs[3].text==QString::fromUtf8("(≈0,394in)"),
        "Approximate secondary text disrupted the authoritative stacked specification");
    const QFontMetricsF metrics(font);
    for(std::size_t i=0;i<3;++i)require(runs[3].baseline.x()>runs[i].baseline.x()+runs[i].scale*metrics.horizontalAdvance(runs[i].text),
        "Approximate indication overlaps a deviation");
    QImage proof(1000,360,QImage::Format_RGB32);proof.fill(Qt::white);
    {
        QPainter painter(&proof);
        const std::array labels{viewer::DimensionTextLabel{text,{40,100},0,font,Qt::black}};
        viewer::paint_dimension_text_layer(painter,labels,2,[](QPainter& p,const QPainterPath& path){p.fillPath(path,Qt::white);});
    }
    style.tolerance_mode="basic";style.upper_tolerance.clear();style.lower_tolerance.clear();d.source_text_style=style;
    text=view.dimension_label_text(d);runs=viewer::dimension_text_runs(font,text,true);
    require(runs.size()==2&&runs[0].text=="10 [mm]","Basic dimension lost its authoritative nominal");
    const auto frame=viewer::dimension_text_box(font,viewer::dimension_primary_text(text),0,true);
    const auto box=viewer::dimension_text_box(font,text,0,true);
    require(runs[1].baseline.x()>frame.right()&&box.right()>runs[1].baseline.x()+metrics.horizontalAdvance(runs[1].text),
        "Basic frame includes approximate value or selection bounds omit it");
    {
        QPainter painter(&proof);
        const std::array labels{viewer::DimensionTextLabel{text,{40,250},0,font,Qt::black,true}};
        viewer::paint_dimension_text_layer(painter,labels,2,[](QPainter& p,const QPainterPath& path){p.fillPath(path,Qt::white);});
    }
    const int top=qRound(250+frame.top()),main_x=qRound(40+frame.center().x()),secondary_x=qRound(40+runs[1].baseline.x()+10);
    bool main_frame=false;
    for(int y=top-1;y<=top+1;++y) {
        main_frame|=proof.pixelColor(main_x,y)!=QColor(Qt::white);
        require(proof.pixelColor(secondary_x,y)==QColor(Qt::white),"Basic frame was painted across the approximate indication");
    }
    require(main_frame,"Authoritative basic nominal has no painted frame");
    require(proof.save("build/converted-tolerance-layout.png"),"Cannot save converted tolerance proof");
    kernel::ViewerMesh mesh;mesh.vertices={d.witness_first,d.witness_second,d.line_first,d.line_second};mesh.dimensions={d};
    view.set_mesh(mesh);view.set_selection_contract({viewer::CandidateKind::Dimension});view.show();view.fit_all();flush();
    const viewer::ViewerCandidate candidate{viewer::CandidateKind::Dimension,0,0,"units-sketch","dimension:test",{}};
    const auto position=view.candidate_dimension_label_position(candidate);require(position.has_value(),"Secondary dimension lost its pick position");
    require(std::ranges::any_of(view.selection_candidates_at(*position),[](const auto& hit){return hit.kind==viewer::CandidateKind::Dimension&&hit.owner_id=="units-sketch";}),
        "Shared picker omitted a label with an approximate secondary value");
    for(int handle=0;handle<3;++handle)require(view.dimension_handle_position(candidate,handle).has_value(),"Secondary indication removed a dimension grip");
    app->setProperty("zimaStackedTolerances",previous);
}
void verify_annotation_units() {
    kernel::ViewerDimension d;d.value=25.4;d.reference={"source","length",{}};
    kernel::DimensionTextStyle style;style.value_unit="in";style.suffix="in";style.decimals=4;
    style.keep_trailing_zeros=true;style.tolerance_mode="symmetric";style.symmetric_tolerance="0.0005";
    d.source_text_style=style;d.display_text_override=kernel::dimension_text(d,style);
    const auto before=document::dimension_geometry_json(d);
    const auto reopened=document::dimension_geometry_from_json(before);
    require(document::dimension_geometry_json(reopened)==before,"Native viewer packet lost annotation-unit metadata");
    for(const auto unit:{viewer::DimensionDisplayUnits{},viewer::DimensionDisplayUnits{25.4,"in",1,"°"},viewer::DimensionDisplayUnits{10,"cm",1,"°"}}) {
        const auto expected=unit.length_suffix=="mm"?"25,40000mm ±0,01270":unit.length_suffix=="cm"?"2,540000cm ±0,001270":"1,0000in ±0,0005";
        require(viewer::dimension_unit_label(reopened,2,unit)==expected,"Exact presentation changed a manufacturing limit");
        require(document::dimension_geometry_json(reopened)==before,"Display conversion changed the persisted specification");
    }
    auto future=reopened;future.value=10;
    require(viewer::dimension_unit_label(future,3,{})=="0,3937in ±0,0005 (≈10mm)","A future dimension value silently changed the original rounded specification");
    auto metric=d;metric.value=10;metric.source_text_style=kernel::DimensionTextStyle{};
    metric.source_text_style->tolerance_mode="symmetric";metric.source_text_style->symmetric_tolerance="0.01";
    metric.display_text_override.clear();
    require(viewer::dimension_unit_label(metric,3,{25.4,"in",1,"°"})=="10mm ±0,01 (≈0,394in)","Repeating inch conversion changed acceptance limits");
    metric.value=10.1234;metric.source_text_style->decimals=4;
    require(viewer::dimension_unit_label(metric,0,{})=="10,1234mm ±0,01","Global display precision changed a manufacturing nominal");
    metric.kind=kernel::ViewerDimensionKind::Angular;metric.value=90;metric.source_text_style->suffix="°";
    require(viewer::dimension_unit_label(metric,3,{1,"mm",180./std::numbers::pi,"rad"})=="90° ±0,01 (≈1,571rad)","Angular specification was rounded into a different interval");
    require(kernel::dimension_text(reopened,drawing::sheet_dimension_style(style))=="1,0000in ±0,0005","Sheet formatting stripped or rescaled inch specification");
    app::DimensionTextFields fields(style,nullptr);fields.show();flush();
    require(fields.findChild<QLabel*>("dimensionAnnotationUnits")->text()=="in","Properties show the wrong tolerance unit");
    require(fields.value()==style,"Unchanged properties lost annotation units or trailing zeros");
    fields.findChild<QCheckBox*>("dimensionTrailingZeros")->setChecked(false);
    require(!fields.value().keep_trailing_zeros&&fields.value().value_unit=="in","Trailing-zero edit changed the specification unit");
    auto dimension=drawing::make_drawing_dimension("view");dimension.style=style;
    const auto drawing_packet=drawing::serialize_drawing_dimensions({dimension});
    const auto drawing_reopened=drawing::deserialize_drawing_dimensions(drawing_packet);
    require(drawing_reopened.front().style==style,"Drawing reopen lost annotation-unit metadata");
    auto sketch=sketcher::Sketch::create_default();const auto circle=sketch.add_circle(0,0,25.4);
    auto radius=sketch.create_circle_radius_dimension(circle);radius.value_unit="in";radius.suffix="in";
    radius.keep_trailing_zeros=true;radius.annotation_decimals=4;radius.tolerance_mode="symmetric";radius.symmetric_tolerance="0.0005";
    sketch.apply_dimension(radius);
    const auto serialized=sketch.serialized();const auto restored=sketcher::Sketch::from_serialized(serialized);
    require(restored.serialized()==serialized&&restored.dimensions.front()==radius,"Sketch reopen changed explicit manufacturing annotation");
    const auto mesh=restored.viewer_mesh();
    const auto shown=std::ranges::find_if(mesh.dimensions,[](const auto& item){return item.source_text_style&&item.source_text_style->value_unit=="in";});
    require(shown!=mesh.dimensions.end()&&viewer::dimension_unit_label(*shown,2,{})=="R25,40000mm ±0,01270","Sketch View lost its explicit nominal/tolerance units or precision");
    auto drawing_value=d;drawing_value.display_text_override.clear();
    require(drawing::drawing_dimension_text(drawing_reopened.front(),drawing_value)=="1.0000in ±.0005","Drawing dimension formatter lost specification units");
    {
        const auto previous=QCoreApplication::instance()->property("zimaStackedTolerances");
        QCoreApplication::instance()->setProperty("zimaStackedTolerances",true);
        kernel::DimensionTextStyle inch;inch.value_unit="in";inch.suffix="\"";inch.decimals=3;
        inch.keep_trailing_zeros=true;inch.tolerance_mode="deviations";inch.upper_tolerance="0.001";inch.lower_tolerance="0.000";
        kernel::ViewerDimension nominal;nominal.kind=kernel::ViewerDimensionKind::Diameter;nominal.value=.475*25.4;
        const auto text=QString::fromStdString(kernel::dimension_text(nominal,inch,true));
        auto font=zima::technical_font();font.setPixelSize(36);
        const auto stacked=viewer::dimension_render_text(inch,text);const auto runs=viewer::dimension_text_runs(font,stacked);
        require(runs.size()==3&&runs[0].text==QString::fromUtf8("⌀.475\"")&&runs[1].text=="+.001"&&runs[2].text=="-.000",
            "Inch drawing lost decimal point, signed zero deviation or decimal places");
        require(runs[1].baseline.x()==runs[2].baseline.x(),"Inch deviation decimal points are misaligned");
        QImage proof(900,280,QImage::Format_RGB32);proof.fill(Qt::white);
        {QPainter painter(&proof);const std::array labels{viewer::DimensionTextLabel{stacked,{60,110},0,font,Qt::black}};
            viewer::paint_dimension_text_layer(painter,labels,2,[](QPainter& p,const QPainterPath& path){p.fillPath(path,Qt::white);});}
        require(proof.save("build/inch-tolerance-layout.png"),"Cannot save inch tolerance proof");
        inch.text_override="Literal 0,475";
        require(kernel::dimension_text(nominal,inch,true)==inch.text_override,"Inch formatting rewrote user text");
        QCoreApplication::instance()->setProperty("zimaStackedTolerances",previous);
    }
    auto invalid=restored.serialized_json();invalid["dimensions"][0]["value_unit"]="rad";
    rejects([&]{static_cast<void>(sketcher::Sketch::from_serialized_json(invalid));});
    QTemporaryDir files;require(files.isValid(),"Cannot create annotation native test directory");
    kernel::DimensionLayout native_layout;native_layout.text_style=style;
    auto part=document::PartDocument::create_default();kernel::store_dimension_layout(part.dimension_layouts,d.reference,native_layout);
    const auto part_path=files.filePath("annotation.prtz").toStdString();part.save(part_path);
    require(document::PartDocument::load(part_path).dimension_layouts==part.dimension_layouts,"Part native file lost explicit annotation units");
    auto assembly=assembly::AssemblyDocument::create_default();kernel::store_dimension_layout(assembly.dimension_layouts,d.reference,native_layout);
    const auto assembly_path=files.filePath("annotation.asmz").toStdString();assembly.save(assembly_path);
    require(assembly::AssemblyDocument::load(assembly_path).dimension_layouts==assembly.dimension_layouts,"Assembly native file lost explicit annotation units");
    auto drawing_doc=drawing::DrawingDocument::create_default();drawing::DrawingView view;view.id="view";view.name="Tolerance";view.source_document_id=part.document_id;
    drawing_doc.sheets.front().views={view};drawing_doc.sheets.front().dimensions={dimension};
    const auto drawing_path=files.filePath("annotation.drwz").toStdString();drawing_doc.save(drawing_path);
    require(drawing::DrawingDocument::load(drawing_path).sheets.front().dimensions.front().style==style,"Drawing native file lost explicit annotation units");
    fields.set_angular_quantity(true);
    require(fields.value().value_unit=="deg"&&fields.findChild<QLabel*>("dimensionAnnotationUnits")->text()==QString::fromUtf8("°"),"Changing dimension kind retained length units");
    fields.close();
}
int main(int argc, char **argv) {
    QApplication app(argc, argv);
    try {
        // Polar grips change presentation only; circle radii and measured rays stay fixed.
        for(auto kind:{kernel::ViewerDimensionKind::Radius,kernel::ViewerDimensionKind::Diameter,kernel::ViewerDimensionKind::Angular}){
            kernel::ViewerDimension source;source.kind=kind;source.plane_normal={0,0,1};
            source.witness_first={0,0,0};source.witness_second={10,0,0};
            source.line_first={10,0,0};source.line_second={0,10,0};source.label_position=kernel::Vec3{15,0,0};source.value=90;source.sweep_degrees=90;
            kernel::DimensionLayout initial;initial.text_along=3;initial.text_outward=2;
            auto shown=kernel::layout_dimension(source,{},initial);
            const auto grip=kernel::Vec3{18,0,0};
            auto layout=kernel::dragged_dimension_layout(shown,{},initial,0,-6,16,true,grip);
            auto moved=kernel::layout_dimension(source,{},layout);
            near(moved.label_position->x,12);near(moved.label_position->y,16);near(moved.value,90);
            if(kind==kernel::ViewerDimensionKind::Angular){
                near(moved.line_first.x,20);near(moved.line_second.y,20);
                for(int handle:{1,2}){
                    const double along=handle==1?5:0,outward=handle==2?5:0;
                    auto arrow=kernel::layout_dimension(source,{},kernel::dragged_dimension_layout(shown,{},initial,handle,along,outward,true));
                    near(arrow.line_first.x,15);near(arrow.line_second.y,15);
                    near(arrow.label_position->x,shown.label_position->x*1.5);near(arrow.label_position->y,shown.label_position->y*1.5);
                }
                kernel::ModelEnvelope envelope;envelope.include({20,20,0});envelope.include({-20,-20,0});
                auto automatic=initial;automatic.envelope_offset=8;
                const auto outside=kernel::layout_dimension(source,envelope,automatic);
                const auto clamped=kernel::layout_dimension(source,envelope,
                    kernel::dragged_dimension_layout(outside,envelope,automatic,1,-30,0,true));
                near(kernel::dimension_dot(kernel::dimension_unit(*outside.label_position),kernel::dimension_unit(*clamped.label_position)),1);
                require(clamped.line_first.x>=std::hypot(20.,20.),"Angular grip crossed its configured envelope");
            }else{
                near(moved.witness_second.x,6);near(moved.witness_second.y,8);
            }
            require(document::dimension_layout_from_json(document::dimension_layout_json(layout))==layout,"Polar grip layout persistence changed");
        }
        verify_annotation_units();
        verify_converted_tolerance_layout();
        verify_occurrence_bounds();
        verify_model_dimension_units();
        {
            kernel::ViewerDimension count;count.label_only=true;count.unit_suffix.clear();count.value=4;
            kernel::DimensionLayout saved;
            QWidget owner;owner.setProperty("zimaDocumentUnits",QVariantMap{{"Length","in"},{"Angle","rad"}});
            app::DimensionPropertiesDialog dialog(count,{},[&](auto value){saved=std::move(value);},&owner);
            dialog.findChild<QSpinBox*>("dimensionDecimals")->setValue(2);
            dialog.buttons()->button(QDialogButtonBox::Ok)->click();
            require(saved.text_style&&saved.text_style->value_unit.empty(),"Editing a count label attached physical units");
        }

        {
            kernel::DimensionTextStyle style;style.tolerance_mode="deviations";
            style.upper_tolerance="0.2";style.lower_tolerance="0.1";
            app::DimensionTextFields fields(style,nullptr);fields.show();flush();
            auto* basic=fields.findChild<QCheckBox*>("dimensionBasic");
            auto* tolerance=fields.findChild<QComboBox*>("sketchDimensionToleranceMode");
            require(basic&&tolerance,"Shared basic dimension control missing");
            basic->setChecked(true);flush();
            require(fields.value().tolerance_mode=="basic"&&!tolerance->isEnabled()&&
                !fields.findChild<QLineEdit*>("sketchUpperDeviation")->isEnabled(),"Basic dimension still permits tolerances");
            style=fields.value();
            require(document::dimension_text_style_from_json(document::dimension_text_style_json(style))==style,"Basic style persistence changed");
            basic->setChecked(false);flush();
            require(tolerance->isEnabled()&&fields.value().tolerance_mode=="deviations"&&fields.value().upper_tolerance=="0.2","Basic toggle destroyed pending tolerances");
            app::DimensionTextFields reopened(style,nullptr);
            require(reopened.findChild<QCheckBox*>("dimensionBasic")->isChecked()&&!reopened.findChild<QComboBox*>("sketchDimensionToleranceMode")->isEnabled(),"Reopened basic dimension lost its state");
            QImage proof(800,500,QImage::Format_RGB32);proof.fill(Qt::white);QPainter painter(&proof);
            QFont font=zima::technical_font();font.setPixelSize(28);
            int index=0;
            for(auto kind:{kernel::ViewerDimensionKind::Linear,kernel::ViewerDimensionKind::Angular,kernel::ViewerDimensionKind::Radius,kernel::ViewerDimensionKind::Diameter}) {
                kernel::ViewerDimension d;d.kind=kind;d.value=kind==kernel::ViewerDimensionKind::Angular?90:25;
                d.witness_second={100,0,0};d.line_first={0,30,0};d.line_second={100,30,0};
                if(kind==kernel::ViewerDimensionKind::Angular){d.line_first={60,0,0};d.line_second={0,60,0};d.sweep_degrees=90;}
                auto current=style;current.value_unit=kind==kernel::ViewerDimensionKind::Angular?"deg":"mm";current.suffix=kind==kernel::ViewerDimensionKind::Angular?"°":"";d.source_text_style=current;
                const auto text=QString::fromStdString(kernel::dimension_text(d,current));
                require(!text.contains("0,2")&&!text.contains("0,1")&&!text.contains(QChar(0x00b1)),"Basic text includes a deviation");
                const auto box=viewer::dimension_text_box(font,text,0,true);
                const auto plain=viewer::dimension_text_box(font,text,0);
                require(box.contains(plain)&&box.width()>plain.width()&&box.height()>plain.height(),"Frame does not surround the complete value and unit");
                const auto layout=viewer::dimension_text_presentation(d,[](kernel::Vec3 p){return QPointF(p.x,-p.y);},font,text,2,1.5);
                require(layout.valid,"Basic dimension presentation failed");
                const QPointF baseline(90+400*(index%2),130+240*(index/2));
                const std::array labels{viewer::DimensionTextLabel{text,baseline,0,font,Qt::black,true,1.5}};
                viewer::paint_dimension_text_layer(painter,labels,2,[](QPainter& p,const QPainterPath& path){p.fillPath(path,Qt::white);});
                ++index;
            }
            painter.end();
            require(proof.save("build/basic-dimensions-proof.png"),"Cannot save basic dimension proof");
            const auto border=viewer::dimension_text_box(font,"25",0,true).translated(90,130);
            require(qGray(proof.pixel(qRound(border.center().x()),qRound(border.top())))<128,"Basic rectangle was not painted");
        }
        verify_vertical_dimension_clearance();
        {
            kernel::DimensionTextStyle style;style.tolerance_mode="deviations";style.suffix.clear();
            style.upper_tolerance="0.2";style.lower_tolerance="0.15";
            kernel::ViewerDimension dimension;dimension.value=30;
            const auto literal=QString::fromStdString(kernel::dimension_text(dimension,style));
            app.setProperty("zimaStackedTolerances",false);
            require(viewer::dimension_render_text(style,literal)==literal,"Inline layout changed text");
            app.setProperty("zimaStackedTolerances",true);
            const auto stacked=viewer::dimension_render_text(style,literal);
            QFont font; font.setPixelSize(35);
            const auto runs=viewer::dimension_text_runs(font,stacked);
            require(runs.size()==3 && runs[1].baseline.y()<runs[2].baseline.y(),"Deviations are not stacked");
            require(runs[0].baseline.y()==runs[2].baseline.y(),"Nominal and lower deviation baseline differ");
            require(runs[0].scale==1&&runs[1].scale==.75&&runs[2].scale==.75,"Stacked tolerance text must be 75 percent of nominal height");
            require(viewer::dimension_text_box(font,stacked,5).height()<80,"Stacked 3.5 mm text with padding exceeds the 8 mm guide spacing");
            require(viewer::dimension_text_box(font,stacked,0).height()>viewer::dimension_text_box(font,literal,0).height(),"Stack bounds omit upper deviation");
            require(viewer::dimension_text_width(font,stacked)<viewer::dimension_text_width(font,literal),"Stack did not reduce width");
            QImage spacing_proof(500,210,QImage::Format_RGB32);spacing_proof.fill(QColor("#202020"));
            {QPainter painter(&spacing_proof);painter.setPen(QPen(QColor("#FFD400"),2.5));
                for(int y:{90,170}){painter.drawLine(40,y,450,y);painter.drawLine(40,y-8,40,y+8);painter.drawLine(450,y-8,450,y+8);}
                const std::array<viewer::DimensionTextLabel,2> labels{{{stacked,{160,80},0,font,QColor("#FFD400")},{stacked,{160,160},0,font,QColor("#FFD400")}}};
                viewer::paint_dimension_text_layer(painter,labels,5,[](QPainter& p,const QPainterPath& path){p.fillPath(path,QColor("#202020"));});
            }
            spacing_proof.save("build/stacked-tolerance-spacing.png");
            style.text_override="custom +0,2 /-0,15";
            require(viewer::dimension_render_text(style,QString::fromStdString(style.text_override))==QString::fromStdString(style.text_override),"Literal override was interpreted as tolerance");
            app.setProperty("zimaStackedTolerances",false);
            drawing::DrawingView view;
            drawing::ProjectedTriangle t;t.points={drawing::Point2{0,0},{10,0},{0,10}};view.projected_triangles.push_back(t);
            std::swap(t.points[1],t.points[2]);view.projected_triangles.push_back(t);
            const auto body=drawing_render::projected_body_path(view,{},1);
            require(body.contains({2,-2})&&!body.contains({20,-2}),"Crop body mask lost overlap or includes empty space");
            auto doc=drawing::DrawingDocument::create_default();auto& sheet=doc.sheets.front();
            view.id="parent";view.name="Parent";view.source_document_id="model";view.x=105;view.y=150;view.section_id="section";
            drawing::ProjectedEdge contour;contour.points={{-20,-3},{20,-3}};
            drawing::ProjectedEdge hatch;hatch.points={{-20,0},{20,0}};hatch.hatch=true;
            view.projected_edges={contour,hatch};view.section_hatch_crops["section"]={drawing::ViewCropShape::Circle,{0,0},{{5,5}}};
            sheet.views={view};
            QImage image(840,1188,QImage::Format_RGB32);image.fill(Qt::white);
            {drawing_render::SheetRenderer renderer;renderer.set_render_sheet(&sheet);QPainter painter(&image);renderer.paint_sheet(painter,4,{},true);}
            const int y=int((sheet.height_mm()-view.y)*4);
            require(qGray(image.pixel(420,y))<128&&qGray(image.pixel(460,y))>240&&qGray(image.pixel(460,y+12))<128,"Hatch region clipped ordinary contour or retained exterior hatching");
            auto detail=view;detail.id="detail";detail.name="X";detail.parent_view_id=view.id;detail.detail_view=true;detail.scale=2;
            detail.crop=drawing::ViewCrop{drawing::ViewCropShape::Ellipse,{0,0},{{8,6}}};
            drawing::refresh_detail_view(detail,view);sheet.views.push_back(detail);
            require(detail.section_hatch_crops.size()==1&&detail.projected_edges.size()==2,"Detail lost hatching or section region");
            const auto anchor=detail.crop->anchor;view.projected_edges.front().points.front().x=-25;
            drawing::refresh_detail_view(detail,view);require(detail.projected_edges.front().points.front().x==-25&&detail.crop->anchor==anchor,"Detail refresh lost associativity or boundary");
            QTemporaryDir saved;const auto path=std::filesystem::u8path(saved.filePath("clipping.drwz").toStdString());doc.save(path);
            const auto loaded=drawing::DrawingDocument::load(path);
            require(loaded.sheets.front().views.front().section_hatch_crops.size()==1&&loaded.sheets.front().views.back().detail_view,"Drawing-only hatch region or detail failed to persist");
        }
        // Zero coordinates retain the same definition plane and projected
        // measurement direction as nonzero coordinates, including in assemblies.
        QImage zero_proof(1200,1200,QImage::Format_ARGB32);zero_proof.fill(Qt::white);
        QPainter zero_painter(&zero_proof);int zero_cell=0;
        for(const auto rotation:{kernel::Vec3{0,0,37},kernel::Vec3{90,0,23},kernel::Vec3{25,40,61}}) {
            const auto frame=kernel::annotation_frame({3,7,11},rotation);
            for(int axis:{0,1})for(double value:{0.,12.,-12.}) {
                kernel::ViewerDimension d;
                d.reference={"zero","dimension:coordinate",{}};
                d.measurement_direction=kernel::dimension_scale(frame.axes[axis],value<0?-1.:1.);
                d.plane_normal=frame.axes[2];d.witness_first=frame.origin;
                d.witness_second=kernel::dimension_add(frame.origin,kernel::dimension_scale(frame.axes[axis],value));
                const auto side=kernel::dimension_cross(d.plane_normal,*d.measurement_direction);
                d.line_first=kernel::dimension_add(d.witness_first,kernel::dimension_scale(side,8));
                d.line_second=kernel::dimension_add(d.witness_second,kernel::dimension_scale(side,8));d.value=std::abs(value);
                const auto project=[&](kernel::Vec3 p){const auto delta=kernel::dimension_sub(p,frame.origin);return QPointF(kernel::dimension_dot(delta,frame.axes[0])*5,-kernel::dimension_dot(delta,frame.axes[1])*5);};
                const auto shown=viewer::dimension_presentation(d,project,30);
                require(shown.valid,"Zero coordinate disappeared in its own plane");
                for(const auto& arrow:shown.arrows)near(axis==0?arrow.second.y():arrow.second.x(),0);
                auto layout=kernel::dragged_dimension_layout(d,{}, {},0,3,4);
                const auto moved=kernel::layout_dimension(d,{},layout);
                near(kernel::dimension_dot(kernel::dimension_sub(moved.line_first,d.line_first),side),4);
                near(kernel::dimension_dot(kernel::dimension_sub(*moved.label_position,d.line_first),*d.measurement_direction),std::abs(value)/2+3);
                near(kernel::dimension_dot(kernel::dimension_sub(*moved.label_position,frame.origin),d.plane_normal),0);
                require(document::dimension_geometry_from_json(document::dimension_geometry_json(d)).measurement_direction==d.measurement_direction,"Drawing lost measurement direction");
                kernel::BodyResult packet;packet.mesh.dimensions={d};
                const auto loaded=document::load_body_result(document::serialize_body_result(packet));
                require(loaded.mesh.dimensions.front().measurement_direction==d.measurement_direction,"Snapshot lost measurement direction");
                if(value==0) {
                    viewer::MeshView view;view.resize(600,400);view.show();flush();
                    kernel::ViewerMesh mesh;mesh.dimensions={d};
                    mesh.edges.push_back({{frame.world({-15,-15,0}),frame.world({15,-15,0}),frame.world({15,15,0}),frame.world({-15,15,0}),frame.world({-15,-15,0})},{"frame","edge",{}}});
                    view.set_mesh(mesh);view.set_view_direction(d.plane_normal);
                    for(auto* animation:view.findChildren<QVariantAnimation*>())animation->setCurrentTime(animation->duration());
                    view.fit_all();flush();view.confirm_reference(d.reference.owner_id,d.reference.semantic_key,{},viewer::CandidateKind::Dimension);
                    const auto selected=view.confirmed_candidate();require(selected.has_value(),"Zero dimension cannot be confirmed in View");
                    require(view.dimension_handle_position(*selected,0).has_value(),"Zero dimension has no View text grip");
                    zero_painter.drawImage(QRect((zero_cell%2)*600,(zero_cell/2)*400,600,400),view.grabFramebuffer());++zero_cell;
                }
            }
        }
        zero_painter.end();require(zero_proof.save("build/zero-dimension-planes.png"),"Cannot save zero-dimension proof");
        {
            kernel::ViewerDimension angle;angle.kind=kernel::ViewerDimensionKind::Angular;
            angle.line_first=angle.line_second={10,0,0};angle.sweep_degrees=0;
            const auto shown=viewer::dimension_presentation(angle,[](kernel::Vec3 p){return QPointF(p.x*5,-p.y*5);},25);
            require(shown.valid,"Zero angle disappeared");
            near(QLineF(shown.curves[0].back(),shown.handles[1]).length(),1.5);
            near(QLineF(shown.curves[1].back(),shown.handles[2]).length(),1.5);
            for(const auto& arrow:shown.arrows)near(arrow.second.x(),0);
        }
        {
            viewer::MeshView view;view.resize(900,700);view.show();flush();
            // Exercise the real View paint path, including Sketcher, instead of
            // testing only the paper renderer's text-clearance helper.
            for(bool sketch:{false,true})for(int pixels:{12,24})for(double angle:{0.,.7,1.5707963267948966}) {
                QFont font("Arial");font.setPixelSize(pixels);view.setFont(font);
                kernel::ViewerDimension d;d.reference={"clearance","dimension:test",{}};
                d.witness_second={80*std::cos(angle),80*std::sin(angle),0};
                d.line_first={-10*std::sin(angle),10*std::cos(angle),0};
                d.line_second=kernel::dimension_add(d.line_first,d.witness_second);
                d.value=80;d.display_text_override="45,831mm g";
                kernel::ViewerMesh mesh;mesh.vertices={d.witness_first,d.witness_second,d.line_first,d.line_second};mesh.dimensions={d};
                view.set_active_sketch_owner(sketch?"clearance":"");view.set_mesh(mesh);view.set_view_direction({0,0,1});
                for(auto* animation:view.findChildren<QVariantAnimation*>())animation->setCurrentTime(animation->duration());
                view.fit_all();flush();
                const viewer::ViewerCandidate candidate{viewer::CandidateKind::Dimension,0,0,"clearance","dimension:test",{}};
                const auto first=view.dimension_handle_position(candidate,1),last=view.dimension_handle_position(candidate,2);
                require(first&&last,"Dimension clearance fixture has no line grips");
                const auto labeled=view.grabFramebuffer();
                if(!sketch&&pixels==24&&angle==0.)require(labeled.save("build/view-dimension-clearance.png"),"Cannot save View dimension clearance proof");
                mesh.dimensions[0].display_text_override=" ";view.set_mesh(mesh);flush();
                const auto bare=view.grabFramebuffer();const double ratio=labeled.devicePixelRatio();
                const auto direction=*last-*first;const double length=QLineF(*first,*last).length();
                const QPointF normal(-direction.y()/length,direction.x()/length);
                for(int sample=35;sample<=65;++sample)for(double offset:{-.5,0.,.5}) {
                    const auto point=(*first+direction*(sample/100.)+normal*offset)*ratio;
                    const auto x=qRound(point.x()),y=qRound(point.y());
                    if(!labeled.valid(x,y))continue;
                    if(labeled.pixel(x,y)!=bare.pixel(x,y)) {
                        labeled.save("build/view-dimension-clearance-failed.png");
                        throw std::runtime_error("View text background erases its dimension line: sketch="+std::to_string(sketch)+", font="+std::to_string(pixels)+", angle="+std::to_string(angle));
                    }
                }
            }
        }
        {
            QWidget owner;owner.resize(900,700);
            viewer::MeshView view(&owner);view.setGeometry(0,0,900,700);
            owner.show();view.show();flush();
            for(double sign:{1.,-1.}) {
                document::Placement placement;placement.x=sign*7;placement.y=sign*3;placement.z=sign*4;
                const auto dimensions=document::container_placement_dimensions("placement-grip",placement,{});
                require(dimensions.size()==3,"Placement grip fixture lost its coordinate dimensions");
                for(const auto& source:dimensions) {
                    const auto span=kernel::dimension_sub(source.witness_second,source.witness_first);
                    const auto offset=kernel::dimension_sub(source.line_first,source.witness_first);
                    near(kernel::dimension_dot(source.plane_normal,span),0);
                    near(kernel::dimension_dot(source.plane_normal,offset),0);
                    near(kernel::dimension_dot(source.plane_normal,source.plane_normal),1);
                    kernel::DimensionLayout layout;layout.text_along=4;layout.text_outward=2;
                    const auto moved=kernel::layout_dimension(source,{},layout);
                    require(moved.label_position.has_value(),"Placement dimension ignores requested purple-grip text movement");
                    require(moved.witness_first==source.witness_first&&moved.witness_second==source.witness_second&&moved.value==source.value,"Moving placement text changed the measured geometry");
                    require(moved.line_first==source.line_first&&moved.line_second==source.line_second,"Placement text movement reversed the witness side");
                    kernel::DimensionLayout saved;int commits=0;
                    view.set_dimension_layout_resolver([&](const auto&)->std::optional<kernel::DimensionLayout>{return saved;});
                    view.set_dimension_layout_commit([&](const auto& ref,auto value){require(ref==source.reference,"Placement grip changed identity");saved=value;++commits;});
                    kernel::ViewerMesh mesh;mesh.vertices={source.witness_first,source.witness_second,source.line_first,source.line_second};mesh.dimensions={source};
                    view.set_mesh(mesh);view.set_view_direction(source.plane_normal);
                    for(auto* animation:view.findChildren<QVariantAnimation*>())animation->setCurrentTime(animation->duration());
                    view.fit_all();flush();
                    for(int grip=0;grip<3;++grip) {
                        view.confirm_reference(source.reference.owner_id,source.reference.semantic_key,{},viewer::CandidateKind::Dimension);
                        const auto selected=view.confirmed_candidate();require(selected.has_value(),"Placement dimension cannot be selected");
                        const auto before=view.dimension_handle_position(*selected,grip);require(before.has_value(),"Placement purple grip is missing");
                        const auto count=commits;
                        mouse(&view,QEvent::MouseButtonPress,*before,Qt::LeftButton,Qt::LeftButton);
                        mouse(&view,QEvent::MouseMove,*before+QPointF(35,-30),Qt::NoButton,Qt::LeftButton);
                        const auto after=view.dimension_handle_position(*selected,grip);
                        require(after&&QLineF(*before,*after).length()>10,"Placement purple grip did not move on screen");
                        require(commits==count,"Placement drag committed before release");
                        mouse(&view,QEvent::MouseButtonRelease,*before+QPointF(35,-30),Qt::LeftButton,Qt::NoButton);
                        require(commits==count+1&&view.dimension_source(*selected)==source,"Placement drag changed geometry or did not persist");
                    }
                }
            }
        }
        {
            kernel::ViewerDimension d;
            d.witness_second = {20, 0, 0};
            d.line_first = {0, 8, 0};
            d.line_second = {20, 8, 0};
            d.value = 20;
            const auto front = [](kernel::Vec3 p) { return QPointF(p.x * 10, -p.y * 10); };
            const auto iso = [](kernel::Vec3 p) {
                return QPointF((p.x - p.y) * 7, (-p.x - p.y) * 3.5 - p.z * 7);
            };
            auto centered = viewer::dimension_presentation(d, front, 50);
            near(QLineF(centered.curves[0].back(),centered.handles[1]).length(),1.5);
            near(QLineF(centered.curves[1].back(),centered.handles[2]).length(),1.5);
            require(centered.curves[0].back().y()<centered.handles[1].y(),"Witness overrun is on the wrong side of the arrow");
            for(double zoom:{.5,2.,10.}) {
                const auto paper=viewer::dimension_presentation(d,[&](kernel::Vec3 p){return QPointF(p.x*zoom,-p.y*zoom);},5,2.5*zoom,.75*zoom,false,{},1.5*zoom);
                near(QLineF(paper.curves[0].back(),paper.handles[1]).length()/zoom,1.5);
            }
            require(centered.valid && !centered.oblique && !centered.outside,
                    "Normal dimension did not center its label");
            near(centered.handles[0].x(), 100);
            near(centered.text_baseline.y(), centered.handles[0].y() - 3);
            for(double x:{1.,10.,19.}) {
                auto wide=d;wide.label_position=kernel::Vec3{x,8,0};
                const auto shown=viewer::dimension_presentation(wide,front,400);
                require(shown.valid&&!shown.outside,"Wide dimension text was forced outside the arrows");
                near(shown.handles[0].x(),x*10);
            }
            auto slanted = viewer::dimension_presentation(d, iso, 50);
            require(slanted.oblique && slanted.outside && slanted.text_angle == 0,
                    "Oblique dimension did not use horizontal outside text");
            require(slanted.handles[0].x() >
                        std::max(slanted.handles[1].x(), slanted.handles[2].x()),
                    "Oblique value stayed between arrows");
            d.label_position = kernel::Vec3{-20, 8, 0};
            auto left = viewer::dimension_presentation(d, iso, 50);
            require(left.handles[0].x() < std::min(left.handles[1].x(), left.handles[2].x()),
                    "Left outside label switched sides");
            // Regress the red/green parallel marks in the user's screenshot:
            // both the automatic and dragged leader continue the measured line.
            for (auto kind : {kernel::ViewerDimensionKind::Linear,
                              kernel::ViewerDimensionKind::Radius,
                              kernel::ViewerDimensionKind::Diameter}) {
                auto sample = d;
                sample.kind = kind;
                for (double side : {-1., 1.}) {
                    sample.label_position = kernel::Vec3{side * 40, 23, 0};
                    for (bool oblique : {false, true}) {
                        const auto shown = viewer::dimension_presentation(
                            sample, [&](kernel::Vec3 p) { return oblique ? iso(p) : front(p); }, 50);
                        const auto index = kind == kernel::ViewerDimensionKind::Linear ? 3 : 1;
                        const auto leader = shown.curves.at(index);
                        const auto measured = shown.curves.at(index - 1);
                        const auto u = measured.back() - measured.front();
                        const auto v = leader.back() - leader.front();
                        near(u.x() * v.y() - u.y() * v.x(), 0);
                        require(QLineF(leader.front(), leader.back()).length() >= 16.9,
                                "Leader collapsed at outside arrow");
                        if (oblique)
                            near(shown.text_angle, 0);
                    }
                }
            }
            for (double x : {2., 8., 10., 12., 18.}) {
                auto inside = d;
                inside.label_position = kernel::Vec3{x, 8, 0};
                const auto shown = viewer::dimension_presentation(inside, iso, 50);
                const double lo = std::min(shown.handles[1].x(), shown.handles[2].x());
                const double hi = std::max(shown.handles[1].x(), shown.handles[2].x());
                require(shown.outside && (shown.handles[0].x() + 25 < lo ||
                                         shown.handles[0].x() - 25 > hi),
                        "Dragging between witnesses placed isometric text inside");
            }
            d.arrows_reversed = true;
            auto reversed = viewer::dimension_presentation(d, iso, 50);
            require(reversed.arrows[0].second == -left.arrows[0].second &&
                        reversed.handles == left.handles,
                    "Arrow reversal moved grips or failed");
            drawing::DrawingView drawing_view;
            drawing_view.camera.horizontal = {1, 0, 0};
            drawing_view.camera.vertical = {0, 1, 0};
            drawing_view.scale = 1;
            drawing::ModelAnnotation annotation;
            annotation.kind = drawing::ModelAnnotationKind::Dimension;
            annotation.model_dimension = d;
            annotation.model_layout.arrows_reversed = true;
            {
                auto styled=annotation;
                kernel::DimensionTextStyle style;style.suffix="mm";
                styled.model_layout.text_style=style;
                require(drawing::project_model_annotation(drawing_view,styled).text=="20","Model dimension layout leaked millimetres into the drawing");
                style.text_override="20 mm REF";styled.model_layout.text_style=style;
                require(drawing::project_model_annotation(drawing_view,styled).text=="20 mm REF","Drawing changed authored dimension text");
            }
            const auto paper = app::model_annotation_layout(drawing_view, annotation, {}, 12.5);
            const auto expected = viewer::dimension_presentation(
                d, [](kernel::Vec3 p) { return QPointF(p.x, -p.y); }, 12.5, 2.5, .75);
            near(paper.handles.at("text").x(), expected.handles[0].x());
            near(paper.handles.at("text").y(), -expected.handles[0].y());
            require(paper.text_angle == expected.text_angle &&
                        paper.arrows.size() == expected.arrows.size(),
                    "Drawing and model presentation diverged");
            kernel::DimensionLayout layout;
            layout.arrows_reversed = true;
            layout.line_offset = 4;
            require(document::dimension_layout_from_json(document::dimension_layout_json(layout)) ==
                        layout,
                    "Presentation state did not round-trip");
            auto moved = kernel::layout_dimension(d, {}, layout);
            require(moved.value == d.value && moved.witness_first == d.witness_first &&
                        moved.witness_second == d.witness_second,
                    "Appearance changed measuring geometry");
            QImage proof(1000, 600, QImage::Format_ARGB32);
            proof.fill(Qt::white);
            QPainter painter(&proof);
            painter.setRenderHint(QPainter::Antialiasing);
            painter.setFont(QFont("Arial", 12));
            for (int panel = 0; panel < 6; ++panel) {
                auto sample = d;
                sample.arrows_reversed = false;
                sample.label_position.reset();
                if (panel == 2)
                    sample.label_position = kernel::Vec3{-20, 8, 0};
                if (panel >= 3) {
                    sample.kind = panel == 3   ? kernel::ViewerDimensionKind::Radius
                                  : panel == 4 ? kernel::ViewerDimensionKind::Diameter
                                               : kernel::ViewerDimensionKind::Angular;
                    if (panel == 5) {
                        sample.line_first = {15, 0, 0};
                        sample.line_second = {0, 15, 0};
                        sample.sweep_degrees = 90;
                    }
                }
                const QString label = panel == 3   ? "R20"
                                      : panel == 4 ? QString::fromUtf8("⌀40")
                                      : panel == 5 ? QString::fromUtf8("90°")
                                                   : "20 mm";
                const auto project = [&](kernel::Vec3 p) { return panel == 0 ? front(p) : iso(p); };
                const auto result = viewer::dimension_presentation(
                    sample, project, painter.fontMetrics().horizontalAdvance(label));
                require(result.valid, "Presentation sample invalid");
                if (panel >= 3)
                    require(result.oblique && result.text_angle == 0,
                            "Radius/diameter/angle text rotated in isometry");
                painter.save();
                painter.translate(140 + (panel % 3) * 330, 210 + (panel / 3) * 285);
                painter.setPen(QPen(Qt::black, 1));
                painter.setBrush(Qt::black);
                for (const auto &curve : result.curves)
                    painter.drawPolyline(curve);
                for (const auto &[tip, direction] : result.arrows)
                    painter.drawPolygon(viewer::annotation_arrow(tip, direction, 10));
                painter.save();
                painter.translate(result.text_baseline);
                painter.rotate(result.text_angle);
                painter.drawText(QPointF{}, label);
                painter.restore();
                painter.setBrush(QColor("#D05CFF"));
                for (auto grip : result.handles)
                    painter.drawEllipse(grip, 3, 3);
                painter.restore();
            }
            painter.end();
            require(proof.save(QCoreApplication::applicationDirPath() +
                               "/dimension-presentation-proof.png"),
                    "Cannot save visual proof");
        }
        {
            drawing::ModelAnnotationSource source;
            source.document_id="part";
            source.envelope.include({-20,-10,-5});source.envelope.include({20,10,5});
            source.axes.push_back({{0,0,0},{0,0,1},10,{"cylinder","axis:primary",{}}});
            drawing::DrawingView view;view.camera={{1,0,0},{0,1,0},{0,0,1}};view.scale=1;
            drawing::refresh_model_annotations(view,std::span(&source,1));
            require(view.model_annotations.size()==1,"Model axis annotation missing");
            auto cross=app::model_annotation_layout(view,view.model_annotations[0],{});
            require(cross.curves.size()==4 && cross.centers.size()==1,"End-on model axis has no cross");
            near(cross.curves[0].back().x(),-22);near(cross.curves[1].back().x(),22);
            near(cross.curves[2].back().y(),-12);near(cross.curves[3].back().y(),12);
            auto corner_axis=view.model_annotations[0];
            corner_axis.model_envelope={};corner_axis.model_envelope.include({0,0,0});corner_axis.model_envelope.include({40,20,10});
            auto corner_cross=app::model_annotation_layout(view,corner_axis,{});
            near(corner_cross.curves[0].back().x(),-2);near(corner_cross.curves[1].back().x(),42);
            near(QLineF(corner_cross.curves[0].back(),corner_cross.curves[1].back()).length(),44);
            const auto saved=drawing::deserialize_model_annotations(drawing::serialize_model_annotations(view.model_annotations));
            view.camera={{0,0,1},{0,1,0},{-1,0,0}};
            auto side=app::model_annotation_layout(view,saved[0],{});
            require(side.curves.size()==1,"Rotating saved axis did not replace cross with line");
            near(QLineF(side.curves[0].front(),side.curves[0].back()).length(),14);
            kernel::ViewerDimension d;d.witness_second={10,0,0};d.line_first={0,4,0};d.line_second={10,4,0};
            const auto end_on=[](kernel::Vec3 p){return QPointF(p.z,p.y);};
            require(!viewer::dimension_presentation(d,end_on,10).valid,"End-on dimension stayed visible");
            const auto edge_on_plane=[](kernel::Vec3 p){return QPointF(p.x,p.z);};
            require(viewer::dimension_presentation(d,edge_on_plane,10).valid,"Visible length hidden by edge-on plane");
        }
        kernel::ModelEnvelope bounds;
        {
            kernel::ViewerDimension label;label.label_only=true;
            label.label_position=kernel::Vec3{12,18,9};label.display_text_override="Unbend";
            for(double zoom:{0.1,1.0,20.0}) {
                const auto project=[&](kernel::Vec3 p){return QPointF(p.x*zoom,p.y*zoom);};
                const auto layout=viewer::dimension_presentation(label,project,40);
                require(layout.valid&&layout.curves.empty()&&layout.arrows.empty(),"State label drew measuring geometry");
                near(QLineF(layout.handles[0],project(*label.label_position)).length(),0);
                near(layout.text_baseline.x(),12*zoom-20);
            }
        }
        {
            kernel::ViewerDimension handle;
            handle.rotation_handle=true;handle.kind=kernel::ViewerDimensionKind::Angular;
            handle.witness_first={12,18,0};handle.line_second={42,58,0};
            for(double zoom:{0.1,1.0,20.0}) {
                const auto project=[&](kernel::Vec3 p){return QPointF(p.x*zoom,p.y*zoom);};
                const auto layout=viewer::dimension_presentation(handle,project,40);
                require(layout.valid && layout.curves.size()==1 && layout.arrows.empty(),"Hinge control is not one radial arm");
                near(QLineF(layout.curves.front().front(),layout.handles[0]).length(),70);
                near(QLineF(layout.curves.front().front(),project(handle.witness_first)).length(),0);
            }
        }
        bounds.include({0, 0, 0});
        bounds.include({20, 10, 5});
        kernel::ViewerDimension source;
        source.reference = {"feature", "parameter:length", {}};
        source.witness_second = {20, 0, 0};
        source.line_first = {0, 4, 0};
        source.line_second = {20, 4, 0};
        source.plane_normal = {0, 0, 1};
        source.value = 20;
        for (int plane = 0; plane < 4; ++plane) {
            const auto shown = kernel::layout_dimension(source, bounds, {plane, 8., 0, 0});
            require(shown.reference == source.reference &&
                        shown.witness_first == source.witness_first &&
                        shown.witness_second == source.witness_second && shown.value == 20,
                    "Presentation changed source references");
            const auto side =
                kernel::dimension_unit(kernel::dimension_cross(shown.plane_normal, {1, 0, 0}));
            double support = -1e99;
            for (auto c : bounds.corners())
                support = std::max(support, kernel::dimension_dot(c, side));
            near(kernel::dimension_dot(shown.line_first, side) - support, 8);
            near(kernel::dimension_dot(kernel::dimension_sub(shown.line_second, shown.line_first),
                                       {1, 0, 0}),
                 20);
        }
        const auto shown = kernel::layout_dimension(source, bounds, {0, 8., 0, 0});
        auto drag = kernel::dragged_dimension_layout(shown, bounds, {0, 8., 0, 0}, 1, 0, 12);
        near(*drag.envelope_offset, 20);
        auto moved = kernel::layout_dimension(source, bounds, drag);
        near(moved.line_first.y, 30);
        kernel::ViewerDimension angle = source;
        angle.kind = kernel::ViewerDimensionKind::Angular;
        angle.line_first = {4, 0, 0};
        angle.line_second = {0, 4, 0};
        angle.sweep_degrees = 90;
        angle.value = 90;
        {
            const auto project=[](kernel::Vec3 p){return QPointF(p.x*10,-p.y*10);};
            for(const auto position:{kernel::Vec3{2,2,0},kernel::Vec3{15,-12,0}}) {
                auto d=angle;d.label_position=position;
                const auto planar=viewer::dimension_presentation(d,project,40,10,3,false,{},1.5,false);
                require(planar.valid&&planar.curves.size()==(position.y<0?4:3)&&planar.arrows.size()==2,
                    "Planar angular dimension has a text shelf or leader");
                const auto arc_center=project(d.witness_first);
                near(QLineF(planar.handles[0],arc_center).length(),QLineF(project(d.line_first),arc_center).length());
                const double text_angle=planar.text_angle*std::numbers::pi/180;
                const auto baseline_middle=planar.text_baseline+QPointF(std::cos(text_angle),std::sin(text_angle))*20;
                near(QLineF(baseline_middle,planar.handles[0]).length(),3);
                if(position.y<0) {
                    const auto& extension=planar.curves[2];
                    require(extension.size()>2,"Outside angular text has a straight extension");
                    for(const auto p:extension)near(QLineF(p,arc_center).length(),40);
                    near(std::abs(viewer::dimension_screen_dot(extension.back()-planar.handles[0],
                        QPointF(std::cos(text_angle),std::sin(text_angle)))),20);
                }
                auto radial=d;radial.label_position=kernel::dimension_add(d.witness_first,
                    kernel::dimension_scale(kernel::dimension_sub(position,d.witness_first),3));
                const auto farther=viewer::dimension_presentation(radial,project,40,10,3,false,{},1.5,false);
                near(QLineF(planar.handles[0],farther.handles[0]).length(),0);
                near(QLineF(planar.text_baseline,farther.text_baseline).length(),0);
                const auto spatial=viewer::dimension_presentation(d,project,40);
                require(spatial.curves.size()>3,"3D angular dimension lost its existing text shelf");
            }
        }
        rejects([&] { kernel::layout_dimension(angle, bounds, {1, 8., 0, 0}); });
        auto arc = kernel::layout_dimension(angle, bounds, {0, 8., 0, 0});
        near(arc.value, 90);
        near(arc.line_first.x, std::hypot(20., 10.) + 8);
        auto frame = kernel::annotation_frame({4, 7, 2}, {0, 0, 45});
        frame.include(frame.world({0, 0, 0}));
        frame.include(frame.world({20, 10, 5}));
        near(frame.maximum.x - frame.minimum.x, 20);
        near(frame.maximum.y - frame.minimum.y, 10);
        kernel::ViewerMesh mesh;
        mesh.vertices = {frame.world({0, 0, 0}), frame.world({20, 0, 0}), frame.world({20, 10, 5})};
        mesh.triangles = {0, 1, 2};
        mesh.triangle_references = {kernel::FaceReference{"feature", "face", {}}};
        mesh.annotation_frames[{}] = frame;
        mesh.annotation_frames[{"feature", {}}] = frame;
        mesh.dimensions = {source};
        mesh.dimensions.front().measurement_direction=kernel::Vec3{1,0,0};
        mesh.dimensions[0].line_second = {1e6, 1e6, 1e6};
        auto geometric = kernel::model_envelope(mesh);
        require(geometric.maximum.x < 100 && geometric.maximum.y < 100,
                "Dimension inflated geometry bounds");
        kernel::ViewerPoint point;
        point.reference = {"point", "point", {}};
        point.position = {2, 3, 4};
        mesh.points.push_back(point);
        const auto frames = kernel::object_envelopes(mesh);
        require(frames.at({"point", {}}).minimum == frames.at({"point", {}}).maximum,
                "Point frame acquired volume");
        auto packet = kernel::BodyResult{};
        packet.mesh = mesh;
        const auto loaded_packet =
            document::load_body_result(document::serialize_body_result(packet));
        require(loaded_packet.mesh.annotation_frames == mesh.annotation_frames,
                "Snapshot lost local oriented frames");
        auto assembly = assembly::AssemblyDocument::create_default();
        auto occurrence =
            assembly::AssemblyDocument::create_part_occurrence("Part", "part", {}, packet);
        occurrence.placement.rotation_z = 90;
        assembly.components.push_back(occurrence);
        auto second = occurrence;
        second.occurrence_id = "second";
        second.placement.x = 100;
        assembly.components.push_back(second);
        auto scene = assembly.build_scene();
        require(scene.dimensions.size()==2,"Assembly lost occurrence dimensions");
        for(const auto& d:scene.dimensions) {
            require(d.measurement_direction.has_value(),"Assembly lost measurement direction");
            near(d.measurement_direction->x,0);near(d.measurement_direction->y,1);
        }
        auto parent=assembly::AssemblyDocument::create_default();
        auto nested=assembly::AssemblyDocument::create_assembly_occurrence("Nested",assembly.document_id,{},assembly);
        nested.placement.rotation_x=90;parent.components.push_back(nested);
        const auto nested_scene=parent.build_scene();
        require(nested_scene.dimensions.size()==2,"Nested Assembly lost dimensions");
        for(const auto& d:nested_scene.dimensions) {
            require(d.measurement_direction.has_value(),"Nested Assembly lost direction");
            near(d.measurement_direction->x,0);near(d.measurement_direction->y,0);near(d.measurement_direction->z,1);
        }
        const auto first_path = assembly::InstancePath{}.child(occurrence.occurrence_id).encoded(),
                   second_path = assembly::InstancePath{}.child(second.occurrence_id).encoded();
        const auto a = scene.annotation_frames.at({"feature", first_path}),
                   b = scene.annotation_frames.at({"feature", second_path});
        near(b.origin.x - a.origin.x, 100);
        near(a.axes[0].x, -frame.axes[0].y);
        near(a.maximum.x - a.minimum.x, 20);
        const auto reflected = kernel::mirrored_viewer_mesh(mesh, {{0, 0, 0}, {1, 0, 0}});
        near(reflected.annotation_frames.at({}).origin.x, -frame.origin.x);
        near(reflected.annotation_frames.at({}).axes[0].x, -frame.axes[0].x);
        QTemporaryDir dir;
        auto part = document::PartDocument::create_default();
        kernel::store_dimension_layout(part.dimension_layouts, source.reference, {2, 14., 3, 4});
        part.save((dir.path() + "/part.prtz").toStdString());
        require(document::PartDocument::load((dir.path() + "/part.prtz").toStdString())
                        .dimension_layouts == part.dimension_layouts,
                "Part lost presentation on reopen");
        kernel::store_dimension_layout(assembly.dimension_layouts,
                                       {assembly.document_id, "mate:angle", {}}, {0, 12., 2, 1});
        assembly.save((dir.path() + "/assembly.asmz").toStdString());
        require(assembly::AssemblyDocument::load((dir.path() + "/assembly.asmz").toStdString())
                        .dimension_layouts == assembly.dimension_layouts,
                "Assembly lost presentation on reopen");
        drawing::ModelAnnotationSource annotation_source;
        annotation_source.document_id = part.document_id;
        annotation_source.dimensions = {source};
        annotation_source.envelope = bounds;
        annotation_source.layouts = part.dimension_layouts;
        drawing::DrawingView view;
        view.camera = {{1, 0, 0}, {0, 1, 0}, {0, 0, 1}};
        drawing::refresh_model_annotations(view, std::span(&annotation_source, 1));
        auto other = view;
        view.model_annotations[0].view_layout = kernel::DimensionLayout{1, 20., 5, 6};
        auto projected = drawing::project_model_annotation(view, view.model_annotations[0]);
        require(other.model_annotations[0].view_layout == std::nullopt &&
                    part.dimension_layouts[0].layout.plane_quarter_turns == 2,
                "Drawing layout leaked into model or another view");
        require(projected.value == 20, "Drawing changed measured value");
        require(drawing::deserialize_model_annotations(drawing::serialize_model_annotations(
                    view.model_annotations)) == view.model_annotations,
                "Drawing lost local override");
        annotation_source.dimensions[0].value = 30;
        drawing::refresh_model_annotations(view, std::span(&annotation_source, 1));
        require(view.model_annotations[0].value == 30 &&
                    view.model_annotations[0].view_layout->plane_quarter_turns == 1,
                "Refresh discarded drawing override or live value");
        QWidget owner;
        owner.resize(900, 700);
        viewer::MeshView viewer(&owner);
        viewer.setGeometry(0, 0, 900, 700);
        owner.show();
        viewer.show();
        flush();
        kernel::DimensionLayout persisted{0, 8., 0, 0};
        int commits = 0;
        mesh = {};
        mesh.vertices = {{0, 0, 0}, {20, 0, 0}, {20, 10, 5}};
        mesh.triangles = {0, 1, 2};
        mesh.triangle_references = {kernel::FaceReference{}};
        mesh.dimensions = {source};
        viewer.set_dimension_layout_resolver(
            [&](const auto &) -> std::optional<kernel::DimensionLayout> { return persisted; });
        viewer.set_dimension_layout_commit([&](const auto &reference, auto value) {
            require(reference == source.reference, "Drag committed wrong owner");
            persisted = value;
            ++commits;
        });
        viewer.set_mesh(mesh);
        viewer.set_view_direction({0, 0, 1});
        // Establish the normal view before sampling grip coordinates. Otherwise
        // the 850 ms camera animation races the synthetic mouse events.
        for(auto* animation:viewer.findChildren<QVariantAnimation*>())
            animation->setCurrentTime(animation->duration());
        viewer.fit_all();
        flush();
        viewer.confirm_reference("feature", "parameter:length", {},
                                 viewer::CandidateKind::Dimension);
        auto candidate = viewer.confirmed_candidate();
        require(candidate.has_value(), "Dimension confirmation failed");
        auto handle = viewer.dimension_handle_position(*candidate, 0);
        require(handle.has_value(), "Dimension text grip missing");
        mouse(&viewer, QEvent::MouseButtonPress, *handle, Qt::LeftButton, Qt::LeftButton);
        mouse(&viewer, QEvent::MouseMove, *handle + QPointF(35, -20), Qt::NoButton, Qt::LeftButton);
        require(commits == 0, "Dragging committed before release");
        QKeyEvent escape(QEvent::KeyPress, Qt::Key_Escape, Qt::NoModifier);
        QApplication::sendEvent(&viewer, &escape);
        mouse(&viewer, QEvent::MouseButtonRelease, *handle + QPointF(35, -20), Qt::LeftButton,
              Qt::NoButton);
        require(commits == 0, "Escape committed drag");
        mouse(&viewer, QEvent::MouseButtonPress, *handle, Qt::LeftButton, Qt::LeftButton);
        mouse(&viewer, QEvent::MouseMove, *handle + QPointF(35, -20), Qt::NoButton, Qt::LeftButton);
        mouse(&viewer, QEvent::MouseButtonRelease, *handle + QPointF(35, -20), Qt::LeftButton,
              Qt::NoButton);
        if (commits != 1)
            std::cerr << "Grip commits=" << commits
                      << " confirmed=" << viewer.confirmed_candidate().has_value()
                      << " grip=" << handle->x() << "," << handle->y() << "\n";
        require(commits == 1 && (persisted.text_along != 0 || persisted.text_outward != 0),
                "3D grip did not persist model-space text movement");
        require(viewer.dimension_source(*candidate) == source, "Dragging mutated source geometry");
        viewer.confirm_reference("feature", "parameter:length", {},
                                 viewer::CandidateKind::Dimension);
        handle = viewer.dimension_handle_position(*viewer.confirmed_candidate(), 0);
        mouse(&viewer, QEvent::MouseButtonPress, *handle, Qt::LeftButton, Qt::LeftButton);
        mouse(&viewer, QEvent::MouseButtonPress, *handle, Qt::RightButton,
              Qt::LeftButton | Qt::RightButton);
        mouse(&viewer, QEvent::MouseButtonRelease, *handle, Qt::RightButton, Qt::LeftButton);
        mouse(&viewer, QEvent::MouseButtonRelease, *handle, Qt::LeftButton, Qt::NoButton);
        require(persisted.arrows_reversed && commits == 2,
                "RMB during LMB grip did not persist reversed arrows");
        for(auto kind:{kernel::ViewerDimensionKind::Radius,kernel::ViewerDimensionKind::Diameter}) {
            persisted={};
            auto radial_source=source;radial_source.kind=kind;
            radial_source.label_position=kernel::Vec3{28,0,0};
            mesh.dimensions={radial_source};viewer.set_mesh(mesh);
            viewer.set_active_sketch_owner("feature");
            viewer.confirm_reference("feature","parameter:length",{},viewer::CandidateKind::Dimension);
            const auto selected=*viewer.confirmed_candidate();
            const auto before=*viewer.dimension_handle_position(selected,0);
            int menus=0;viewer.set_context_menu_callback([&](const auto&,const auto&){++menus;});
            mouse(&viewer,QEvent::MouseButtonPress,before,Qt::RightButton,Qt::RightButton);
            mouse(&viewer,QEvent::MouseButtonRelease,before,Qt::RightButton,Qt::NoButton);
            require(menus==0,"Grip opened dimension context menu");
            mouse(&viewer,QEvent::MouseButtonPress,before,Qt::LeftButton,Qt::LeftButton);
            mouse(&viewer,QEvent::MouseMove,before+QPointF(25,-50),Qt::NoButton,Qt::LeftButton);
            const auto after=*viewer.dimension_handle_position(selected,0);
            if(QLineF(before,after).length()<=15)std::cerr<<"Radial kind="<<int(kind)<<" before="<<before.x()<<","<<before.y()<<" after="<<after.x()<<","<<after.y()<<" commits="<<commits<<"\n";
            require(QLineF(before,after).length()>15,"Normal radial text grip did not move");
            mouse(&viewer,QEvent::MouseButtonRelease,after,Qt::LeftButton,Qt::NoButton);
            require(viewer.dimension_source(selected)==radial_source,"Radial drag changed measuring data");
            if(kind==kernel::ViewerDimensionKind::Radius) {
                viewer.confirm_reference("feature","parameter:length",{},viewer::CandidateKind::Dimension);
                mouse(&viewer,QEvent::MouseButtonPress,after,Qt::LeftButton,Qt::LeftButton);
                for(int step=0;step<2;++step) {
                    mouse(&viewer,QEvent::MouseButtonPress,after,Qt::RightButton,Qt::LeftButton|Qt::RightButton);
                    mouse(&viewer,QEvent::MouseButtonRelease,after,Qt::RightButton,Qt::LeftButton);
                }
                mouse(&viewer,QEvent::MouseButtonRelease,after,Qt::LeftButton,Qt::NoButton);
                if(!persisted.radius_center_line_hidden)std::cerr<<"cycle flags="<<persisted.arrows_reversed<<","<<persisted.radius_center_line_hidden<<" commits="<<commits<<" selected="<<viewer.confirmed_candidate().has_value()<<"\n";
                require(persisted.arrows_reversed&&persisted.radius_center_line_hidden,"Radius cycle did not hide center line");
                const auto short_grip=*viewer.dimension_handle_position(selected,0);
                mouse(&viewer,QEvent::MouseButtonPress,short_grip,Qt::LeftButton,Qt::LeftButton);
                mouse(&viewer,QEvent::MouseMove,short_grip+QPointF(-60,0),Qt::NoButton,Qt::LeftButton);
                const auto dragged_short=*viewer.dimension_handle_position(selected,0);
                require(QLineF(short_grip,dragged_short).length()>35,"Re-grabbed shortened radius did not move");
                mouse(&viewer,QEvent::MouseButtonRelease,dragged_short,Qt::LeftButton,Qt::NoButton);
                require(document::dimension_layout_from_json(document::dimension_layout_json(persisted))==persisted,"Radius presentation lost on save");
                auto displayed=kernel::layout_dimension(radial_source,{},persisted);
                const auto front=[](kernel::Vec3 p){return QPointF(p.x*10,-p.y*10);};
                kernel::BodyResult packet;packet.mesh.dimensions={displayed};
                const auto reloaded=document::load_body_result(document::serialize_body_result(packet));
                require(reloaded.mesh.dimensions.front().radius_center_line_hidden &&
                        reloaded.mesh.dimensions.front().arrows_reversed &&
                        reloaded.mesh.dimensions.front().label_position==displayed.label_position,
                        "Viewer packet lost radius presentation");
                const auto presentation=viewer::dimension_presentation(displayed,front,50);
                require(presentation.curves.size()==2 && presentation.curves[0].front()==front(displayed.witness_second),
                        "Shortened radius leader did not start at measured arrow");
                for(const auto delta:{QPointF(-120,20),QPointF(240,-40),QPointF(-450,40)}) {
                    viewer.confirm_reference("feature","parameter:length",{},viewer::CandidateKind::Dimension);
                    const auto candidate=*viewer.confirmed_candidate();
                    const auto text=*viewer.dimension_handle_position(candidate,0);
                    mouse(&viewer,QEvent::MouseButtonPress,text,Qt::LeftButton,Qt::LeftButton);
                    mouse(&viewer,QEvent::MouseMove,text+delta,Qt::NoButton,Qt::LeftButton);
                    const auto moved=*viewer.dimension_handle_position(candidate,0);
                    require(QLineF(moved,text+delta).length()<.01,"Polar radius text grip did not follow the cursor");
                    mouse(&viewer,QEvent::MouseButtonRelease,text+delta,Qt::LeftButton,Qt::NoButton);
                    const auto placed=kernel::layout_dimension(radial_source,{},persisted);
                    const auto radial=kernel::dimension_sub(placed.witness_second,placed.witness_first);
                    const auto label=kernel::dimension_sub(*placed.label_position,placed.witness_first);
                    near(kernel::dimension_dot(kernel::dimension_cross(radial,label),placed.plane_normal),0);
                    const auto original=kernel::dimension_sub(radial_source.witness_second,radial_source.witness_first);
                    near(kernel::dimension_dot(radial,radial),kernel::dimension_dot(original,original));
                }
                viewer.grab().save("build/radius-free-label-view.png");
                kernel::cycle_dimension_presentation(persisted,kind);
                require(!persisted.arrows_reversed&&!persisted.radius_center_line_hidden,"Radius cycle did not return to full line");
            }
        }
        {
            auto angular=source;angular.kind=kernel::ViewerDimensionKind::Angular;
            angular.witness_first={};angular.witness_second={0,20,0};angular.plane_normal={0,0,1};
            angular.line_first={20,0,0};angular.line_second={0,20,0};angular.sweep_degrees=90;angular.value=90;
            angular.label_position.reset();persisted={};mesh.dimensions={angular};
            viewer.set_active_sketch_owner("feature");viewer.set_mesh(mesh);viewer.set_view_direction({0,0,1});
            for(auto* animation:viewer.findChildren<QVariantAnimation*>())animation->setCurrentTime(animation->duration());
            viewer.fit_all();flush();
            viewer.confirm_reference("feature","parameter:length",{},viewer::CandidateKind::Dimension);
            const auto c=*viewer.confirmed_candidate();
            const auto text=*viewer.dimension_handle_position(c,0),rim=*viewer.dimension_handle_position(c,1);
            const QPointF delta(30,-25);
            mouse(&viewer,QEvent::MouseButtonPress,text,Qt::LeftButton,Qt::LeftButton);
            mouse(&viewer,QEvent::MouseMove,text+delta,Qt::NoButton,Qt::LeftButton);
            require(QLineF(*viewer.dimension_handle_position(c,0),text+delta).length()<.1,"Sketch angular text grip did not follow cursor");
            require(QLineF(*viewer.dimension_handle_position(c,1),rim).length()>1,"Angular text grip did not resize arrow arc");
            mouse(&viewer,QEvent::MouseButtonRelease,text+delta,Qt::LeftButton,Qt::NoButton);
            for(int handle:{1,2}){
                const auto before=kernel::layout_dimension(angular,{},persisted);
                const auto grip=*viewer.dimension_handle_position(c,handle),target=grip+QPointF(22,-17);
                mouse(&viewer,QEvent::MouseButtonPress,grip,Qt::LeftButton,Qt::LeftButton);
                mouse(&viewer,QEvent::MouseMove,target,Qt::NoButton,Qt::LeftButton);
                mouse(&viewer,QEvent::MouseButtonRelease,target,Qt::LeftButton,Qt::NoButton);
                const auto after=kernel::layout_dimension(angular,{},persisted);
                const auto a=kernel::dimension_unit(*before.label_position),b=kernel::dimension_unit(*after.label_position);
                near(kernel::dimension_dot(a,b),1);near(after.sweep_degrees,90);
            }
            viewer.set_active_sketch_owner({});
        }
        for(const auto direction:{kernel::Vec3{0,-1,0},kernel::Vec3{0,0,1}}) {
            auto radius=source;radius.kind=kernel::ViewerDimensionKind::Radius;
            radius.witness_second={-8.55,0,5.18};radius.line_first={};radius.line_second=radius.witness_second;
            radius.plane_normal={0,-1,0};radius.label_position=kernel::Vec3{-20,0,12};
            persisted={};persisted.radius_center_line_hidden=true;persisted.arrows_reversed=true;
            mesh.dimensions={radius};viewer.set_mesh(mesh);viewer.set_view_direction(direction);
            for(auto* animation:viewer.findChildren<QVariantAnimation*>())animation->setCurrentTime(animation->duration());
            viewer.fit_all();flush();viewer.confirm_reference("feature","parameter:length",{},viewer::CandidateKind::Dimension);
            const auto selected=*viewer.confirmed_candidate();const auto grip=*viewer.dimension_handle_position(selected,0);
            mouse(&viewer,QEvent::MouseButtonPress,grip,Qt::LeftButton,Qt::LeftButton);
            mouse(&viewer,QEvent::MouseMove,grip+QPointF(-60,20),Qt::NoButton,Qt::LeftButton);
            const auto after=*viewer.dimension_handle_position(selected,0);
            require(QLineF(grip,after).length()>15,"XZ/edge-on radius grip cannot move");
            mouse(&viewer,QEvent::MouseButtonRelease,after,Qt::LeftButton,Qt::NoButton);
            const auto placed=kernel::layout_dimension(radius,{},persisted);
            const auto ray=kernel::dimension_sub(placed.witness_second,placed.witness_first);
            const auto original=kernel::dimension_sub(radius.witness_second,radius.witness_first);
            near(kernel::dimension_dot(ray,ray),kernel::dimension_dot(original,original));
            near(kernel::dimension_dot(ray,placed.plane_normal),0);
        }

        {
            auto radius=source;radius.kind=kernel::ViewerDimensionKind::Radius;
            radius.radius_center_line_hidden=true;radius.arrows_reversed=true;
            radius.witness_first={0,0,0};radius.witness_second={20,0,0};
            radius.line_first=radius.witness_first;radius.line_second=radius.witness_second;
            radius.plane_normal={0,0,1};
            for(bool oblique:{false,true})for(double x:{-30.,-1.,0.,1.,10.,19.,20.,21.,40.}) {
                radius.label_position=kernel::Vec3{x,3,0};
                const auto project=[&](kernel::Vec3 p){return oblique?QPointF(p.x*5+p.y*2,-p.y*4):QPointF(p.x*5,-p.y*5);};
                const auto shown=viewer::dimension_presentation(radius,project,40);
                require(shown.valid,"Shortened radius disappeared while crossing centre or rim");
                const auto radial=project(radius.witness_second)-project(radius.witness_first);
                const auto radial_leader=shown.curves[0].back()-shown.curves[0].front();
                near(radial.x()*radial_leader.y()-radial.y()*radial_leader.x(),0);
                // With this projected horizontal radius, moving x crosses both
                // the centre and rim without changing the selected plane.
                near(shown.handles[0].x(),project(*radius.label_position).x());
                require(shown.arrows.size()==1 && shown.arrows[0].first==project(radius.witness_second) &&
                        shown.curves[0].front()==shown.arrows[0].first,
                        "Moving radius text moved arrow or disconnected leader");
                require(shown.curves[0].back()==shown.curves[1].front() || shown.curves[0].back()==shown.curves[1].back(),
                        "Radius leader does not meet text support");
                const auto leader=shown.curves[0].back()-shown.curves[0].front();
                const auto arrow_direction=shown.arrows[0].second;
                near(leader.x()*arrow_direction.y()-leader.y()*arrow_direction.x(),0);
                near(QLineF(shown.curves[1].front(),shown.curves[1].back()).length(),40);
                if(oblique)near(shown.text_angle,0);
            }
        }
        {
            // A source label may carry an old offset normal to its circle.
            // Switching radius mode must never reveal that offset in the View.
            const auto frame=kernel::annotation_frame({7,11,13},{23,41,17});
            const auto center=frame.origin,normal=frame.axes[2];
            auto radius=source;radius.kind=kernel::ViewerDimensionKind::Radius;
            radius.witness_first=center;radius.witness_second=frame.world({20,0,0});
            radius.line_first=center;radius.line_second=radius.witness_second;radius.plane_normal=normal;
            kernel::DimensionLayout layout;
            for(int mode=0;mode<3;++mode) {
                for(double x:{-30.,0.,10.,20.,40.}) {
                    radius.label_position=frame.world({x,4,9});
                    auto shown=kernel::layout_dimension(radius,{},layout);
                    near(kernel::dimension_dot(kernel::dimension_sub(*shown.label_position,center),normal),0);
                    auto dragged=kernel::dragged_dimension_layout(shown,{},layout,0,-8,6);
                    auto moved=kernel::layout_dimension(radius,{},dragged);
                    near(kernel::dimension_dot(kernel::dimension_sub(*moved.label_position,center),normal),0);
                    const auto project=[](kernel::Vec3 p){return QPointF(p.x*5+p.z*2,-p.y*5+p.z);};
                    auto dirty=radius;dirty.radius_center_line_hidden=layout.radius_center_line_hidden;dirty.arrows_reversed=layout.arrows_reversed;
                    auto planar=dirty;planar.label_position=frame.world({x,4,0});
                    const auto actual=viewer::dimension_presentation(dirty,project,40),expected=viewer::dimension_presentation(planar,project,40);
                    require(actual.valid && expected.valid,"Rotated radius disappeared");
                    near(QLineF(actual.handles[0],expected.handles[0]).length(),0);
                    if(layout.radius_center_line_hidden) {
                        const auto radial=project(radius.witness_second)-project(center);
                        const auto leader=actual.curves[0].back()-actual.curves[0].front();
                        near(radial.x()*leader.y()-radial.y()*leader.x(),0);
                    }
                }
                kernel::cycle_dimension_presentation(layout,kernel::ViewerDimensionKind::Radius);
            }
        }
        {
            QImage proof(400,180,QImage::Format_ARGB32);proof.fill(Qt::white);
            QFont font("Arial");font.setPixelSize(24);
            std::vector<viewer::DimensionTextLabel> labels={
                {"R20.000 mm",{80,100},0,font,Qt::red},
                {"R20.000 mm",{80,100},0,font,Qt::blue}};
            QPainter painter(&proof);
            painter.setPen(QPen(Qt::green,1));
            for(int x=-180;x<400;x+=5)painter.drawLine(x,0,x+180,180);
            viewer::paint_dimension_text_layer(painter,labels,5,
                [](QPainter& p,const QPainterPath& mask){p.fillPath(mask,Qt::white);});
            painter.end();
            const auto box=viewer::dimension_text_box(font,labels[0].text,5).translated(labels[0].baseline);
            require(proof.pixelColor(QPoint(qRound(box.center().x()),qRound(box.top()+2)))==QColor(Qt::white),
                    "Hatching remained inside text clearance");
            int blue=0,red=0,green=0;
            for(int y=box.top()+1;y<box.bottom()-1;++y)for(int x=box.left()+1;x<box.right()-1;++x) {
                const auto c=proof.pixelColor(x,y);
                if(c.blue()>150 && c.red()<100)++blue;
                if(c.red()>150 && c.blue()<100)++red;
                if(c.green()>150 && c.red()<100 && c.blue()<100)++green;
            }
            require(blue>20 && red==0 && green==0,"Dimension text layer lost order or hatch masking");
            const auto tight=QFontMetricsF(font).tightBoundingRect(labels[0].text);
            near(box.top(),labels[0].baseline.y()+tight.top()-5);
            require(proof.save(QCoreApplication::applicationDirPath()+"/dimension-text-mask-proof.png"),"Cannot save text-mask proof");
        }

        {
            // Seven configurations from the user's koty.bmp: two full-radius
            // text positions in either arrow mode, three shortened positions.
            QImage proof(1380,870,QImage::Format_ARGB32);proof.fill(Qt::white);QPainter painter(&proof);painter.setRenderHint(QPainter::Antialiasing);
            kernel::ViewerDimension radius;radius.kind=kernel::ViewerDimensionKind::Radius;radius.value=20;
            radius.witness_first={};radius.witness_second={20,0,0};radius.line_second=radius.witness_second;radius.plane_normal={0,0,1};radius.label_position=kernel::Vec3{35,0,0};
            kernel::ModelEnvelope envelope;envelope.include({-30,-25,0});envelope.include({30,25,0});
            kernel::DimensionLayout attached;attached.envelope_offset=8;
            near(kernel::layout_dimension(radius,envelope,attached).label_position->x,38);
            for(int mode=0;mode<3;++mode)for(int row=0;row<(mode==2?3:2);++row){
                kernel::DimensionLayout placement;placement.radius_rotation_degrees=45;
                for(int i=0;i<mode;++i)kernel::cycle_dimension_presentation(placement,radius.kind);
                placement.text_along=(row==0?40.:row==1?-20.:8.)-35;
                auto shown=kernel::layout_dimension(radius,{},placement);
                const QPointF center(180+mode*460,220+row*(mode==2?265:410));
                const auto project=[&](kernel::Vec3 p){return center+QPointF(5*p.x,-5*p.y+2*p.z);};
                painter.setFont(QFont("Arial",15));const auto presentation=viewer::dimension_presentation(shown,project,QFontMetricsF(painter.font()).horizontalAdvance("R20mm"),11,5);
                require(presentation.valid,"A radius state from koty.bmp disappeared");
                require(presentation.handles[1]==project(shown.witness_second),"Arrow grip detached from radius");
                const auto ray=presentation.handles[1]-center;
                const auto leader=presentation.curves[mode==2?0:1].back()-presentation.curves[mode==2?0:1].front();
                near(ray.x()*leader.y()-ray.y()*leader.x(),0);
                if(mode<2)require(presentation.curves.front().front()==center&&presentation.curves.front().back()==presentation.handles[1],"Full radius lost centre-to-arc line");
                else require(presentation.curves.front().front()==presentation.handles[1],"Short radius leader lost arrow attachment");
                near(presentation.text_angle,0);
                for(int grip:{0,1}){
                    const auto moved=kernel::layout_dimension(radius,{},kernel::dragged_dimension_layout(shown,{},placement,grip,-4,5));
                    near(moved.value,20);near(kernel::dimension_dot(kernel::dimension_sub(*moved.label_position,moved.witness_first),moved.plane_normal),0);
                    if(grip==0)require(moved.witness_second!=shown.witness_second,"Text point did not rotate radius arrow");
                    else {require(moved.witness_second!=shown.witness_second,"Arrow point did not move around radius");near(std::sqrt(kernel::dimension_dot(moved.witness_second,moved.witness_second)),20);}
                }
                painter.setPen(QPen(Qt::black,2));painter.setBrush(Qt::NoBrush);painter.drawArc(QRectF(center.x()-100,center.y()-100,200,200),-10*16,115*16);painter.drawEllipse(center,4,4);
                painter.setPen(QPen(QColor("#9B7A00"),2));for(const auto& curve:presentation.curves)painter.drawPolyline(curve);
                painter.setBrush(QColor("#9B7A00"));for(const auto& [tip,direction]:presentation.arrows)painter.drawPolygon(viewer::annotation_arrow(tip,direction,11));
                painter.setFont(QFont("Arial",15));painter.drawText(presentation.text_baseline,"R20mm");
                painter.setPen(Qt::NoPen);painter.setBrush(QColor("#D05CFF"));painter.drawEllipse(presentation.handles[0],4,4);painter.drawEllipse(presentation.handles[1],4,4);
                painter.setPen(Qt::black);painter.setFont(QFont("Arial",12));painter.drawText(QPointF(mode*460+20,row*(mode==2?265:410)+35),QString("Rezim %1 / poloha %2").arg(mode).arg(row+1));
            }
            painter.end();require(proof.save("build/radius-seven-states-proof.png"),"Cannot save seven radius states");
        }
        {
            QImage proof(900,480,QImage::Format_ARGB32);proof.fill(Qt::white);QPainter painter(&proof);
            for(int row=0;row<4;++row) {
                const double zoom=1<<row,stroke=.5*zoom;QFont font("Arial");font.setPixelSize(int(3.5*zoom));
                const QString text=QStringLiteral("25 g");const double gap=viewer::dimension_text_clearance(font,text,.5*zoom,stroke,.75*zoom);
                const double y=70+row*110;const QPointF baseline(60,y-gap);
                painter.setPen(QPen(Qt::black,stroke));painter.drawLine(QPointF(40,y),QPointF(500,y));
                const std::array<viewer::DimensionTextLabel,1> label{{{text,baseline,0,font,Qt::black}}};
                viewer::paint_dimension_text_layer(painter,label,.5*zoom,[](QPainter& p,const QPainterPath& mask){p.fillPath(mask,Qt::white);});
                const auto box=viewer::dimension_text_box(font,text,.5*zoom).translated(baseline);
                require(box.bottom()<y-stroke/2,"Text mask intersects its dimension line");
                int dark=255;
                for(int dy=-1;dy<=1;++dy){dark=std::min(dark,proof.pixelColor(65,int(y)+dy).lightness());require(proof.pixelColor(65,int(y)+dy)==proof.pixelColor(450,int(y)+dy),"Dimension line was erased below its text");}
                require(dark<255,"Dimension stroke was not painted");
            }
            painter.end();require(proof.save("build/drawing-dimension-clearance.png"),"Cannot save dimension clearance proof");
        }
        viewer.set_context_menu_callback({});
        viewer.set_dimension_frame_visible(true);
        viewer.grab().save("build/dimension-layout-view.png");
        int dialog_commits = 0;
        app::DimensionPropertiesDialog dialog(
            source, persisted, [&](auto) { ++dialog_commits; }, &owner);
        dialog.show();
        flush();
        owner.grab().save("build/dimension-layout-properties.png");
        require(dialog.windowType() == Qt::SubWindow && dialog.parentWidget() == &owner,
                "Presentation dialog escaped owning application");
        require(dialog.findChild<QDialogButtonBox *>()->button(QDialogButtonBox::Apply) == nullptr,
                "Presentation dialog exposes Apply");
        dialog.findChild<QCheckBox*>("dimensionBasic")->setChecked(true);
        dialog.findChild<QDialogButtonBox *>()->button(QDialogButtonBox::Cancel)->click();
        require(dialog_commits == 0, "Cancel committed presentation");
        app::DimensionPropertiesDialog confirm(
            source, persisted, [&](auto value) {
                require(value.arrows_reversed==persisted.arrows_reversed &&
                            value.text_outward==persisted.text_outward,
                        "Properties discarded arrow direction or text attachment");
                require(value.text_style&&kernel::dimension_is_basic(*value.text_style),"Properties OK lost basic dimension style");
                ++dialog_commits;
            }, &owner);
        confirm.show();
        confirm.findChild<QCheckBox*>("dimensionBasic")->setChecked(true);
        flush();
        mouse(&viewer, QEvent::MouseButtonRelease, {850, 650}, Qt::MiddleButton, Qt::NoButton);
        require(dialog_commits == 0, "Short MMB confirmed presentation");
        mouse(&viewer, QEvent::MouseButtonDblClick, {850, 650}, Qt::MiddleButton, Qt::MiddleButton);
        require(dialog_commits == 1, "MMB over View did not confirm presentation");
        app::DimensionPropertiesDialog angular_dialog(angle, {0, 8., 0, 0}, [](auto) {}, &owner);
        require(!angular_dialog.findChild<QComboBox *>("dimensionProjectionPlane")->isEnabled(),
                "Angular presentation offers an invalid plane");
        std::cout << "Oriented frames, occurrences, immutable measurements, persistence, per-view "
                     "overrides and native grip transactions passed\n";
        return 0;
    } catch (const std::exception &e) {
        std::cerr << e.what() << '\n';
        return 1;
    }
}
