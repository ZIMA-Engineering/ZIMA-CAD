#include "drawing_projection_fixture.hpp"
#include <zima/drawing/drawing_template.hpp>
#include <zima/drawing/drawing_document.hpp>

#include <algorithm>
#include <cmath>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <iterator>
#include <stdexcept>
#include <string>

namespace {
void require(bool condition, const char* message) {
    if (!condition) throw std::runtime_error(message);
}
}

int main() {
    try {
        const auto prepare=[](zima::sketcher::SketchText& t){t.contours={{{t.anchor_x,t.anchor_y},{t.anchor_x+1,t.anchor_y},{t.anchor_x+1,t.anchor_y+1},{t.anchor_x,t.anchor_y+1}}};};
        const auto folder=std::filesystem::current_path()/"Projects/test/template-contract";
        std::filesystem::create_directories(folder);
        {
            using namespace zima::drawing;
            DrawingSheet pens;
            require(drawing_pen_width_mm(pens,DrawingPen::White)==0.5&&drawing_pen_width_mm(pens,DrawingPen::Red)==0.7&&
                drawing_pen_width_mm(pens,DrawingPen::Yellow)==0.25&&drawing_pen_width_mm(pens,DrawingPen::Green)==0.25,
                "Drawing pen widths must follow the agreed colour map");
            const auto path=folder/"red-pen.frmz";
            {std::ofstream out(path);out<<"[Format]\nName=Red pen\nSheetFormat=A4\n[FrameGeometry]\nLine1=0,0,20,0,RED\n";}
            load_frame_template(pens,path);
            require(!pens.frame_lines.empty()&&pens.frame_lines.front().pen==DrawingPen::Red,"Red template pen was lost on import");
            auto sketch=load_template_sketch(path,prepare);
            require(std::ranges::any_of(sketch.viewer_mesh().edges,[](const auto& edge){return edge.color=="#FF0000";}),"Sketcher did not display red template geometry");
        }
        {
            using namespace zima::drawing;
            zima::kernel::ViewerMesh mesh;
            zima::kernel::ViewerEdge edge;edge.points={{0,0,0},{10,0,0}};
            edge.edge_treatment_side_directions={{{0,1,0},{0,1,0}},{{0,-1,0},{0,-1,0}}};
            mesh.edges.push_back(edge);
            auto projected=project_edges(mesh,ProjectionCamera{{1,0,0},{0,1,0},{0,0,1}});
            require(projected.size()==1&&projected.front().tangent,"Smooth boundary was not classified from persisted side directions");
            mesh.edges.front().edge_treatment_side_directions[1][1]={0,0,1};
            require(!project_edges(mesh,ProjectionCamera{{1,0,0},{0,1,0},{0,0,1}}).front().tangent,"Sharp transition was classified as tangent");
            auto drawing=DrawingDocument::create_default();DrawingView view;view.id="tangent-test";view.source_document_id="tangent-source";
            view.projected_edges=projected;view.tangent_edge_style=TangentEdgeStyle::Thin;
            view.display_style=DisplayStyle::HiddenEdges;
            require(drawing_edge_visible(view,projected.front()),"Visible tangent boundary was hidden");
            auto hidden_tangent=projected.front();hidden_tangent.hidden=true;
            require(!drawing_edge_visible(view,hidden_tangent),"Occluded tangent boundary must stay hidden even in hidden-edge mode");
            hidden_tangent.tangent=false;
            require(drawing_edge_visible(view,hidden_tangent),"Ordinary hidden edge disappeared with tangent filtering");
            view.tangent_edge_style=TangentEdgeStyle::Hidden;
            require(!drawing_edge_visible(view,projected.front()),"Tangent hide setting was ignored");
            view.tangent_edge_style=TangentEdgeStyle::Thin;
            drawing.sheets.front().views.push_back(view);drawing.sheets.front().red_line_mm=0.8;
            const auto path=folder/"tangent-pens.drwz";drawing.save(path);
            const auto reopened=DrawingDocument::load(path);
            require(reopened.sheets.front().red_line_mm==0.8&&reopened.sheets.front().views.front().tangent_edge_style==TangentEdgeStyle::Thin&&
                reopened.sheets.front().views.front().projected_edges.front().tangent,"Saved drawing lost tangent boundaries or red width");
        }
        // Signed placement survives import; point-pair lengths normalize without moving either point.
        const auto signed_path=folder/"signed-dimensions.tblz";
        {std::ofstream out(signed_path);out<<R"([TitleBlock]
Name=Signed placement
[Sketch]
Data={"points":{"a":{"x":-10,"y":-5},"b":{"x":-20,"y":-5}},"geometry":{"line":{"type":"segment","points":["a","b"]}},"dimensions":{"coord":{"type":"coordinate_x","points":["a"],"value":-10},"length":{"type":"distance_x","points":["a","b"],"value":-10}}}
)";}
        auto signed_sketch=zima::drawing::load_template_sketch(signed_path,prepare);
        require(signed_sketch.dimensions[0].value==-10 && signed_sketch.dimensions[0].geometry_id=="sketch_axis:y" && signed_sketch.dimensions[1].value==10 && signed_sketch.dimensions[1].first_point_id=="b" && signed_sketch.find_point("a")->x==-10 && signed_sketch.find_point("b")->x==-20,"Signed coordinate/length import changed placement");
        auto anchored=zima::drawing::create_template_sketch(true,"Text anchor");
        zima::sketcher::SketchText text;text.id="text";text.value="Label";text.anchor_x=10;text.anchor_y=20;prepare(text);anchored.add_text(text);
        const auto point_id=anchored.texts.front().anchor_point_id;
        require(anchored.move_point(point_id,15,28),"Template text anchor cannot move");
        require(anchored.texts.front().anchor_x==15 && anchored.texts.front().anchor_y==28 && anchored.texts.front().contours.front().front()==std::array<double,2>{15,28},"Moving a text point detached its glyph contours");
        zima::sketcher::TemplateImage logo;
        logo.id="logo";logo.name="logo.png";logo.data_base64="iVBORw0KGgoAAAANSUhEUgAAAAIAAAABCAYAAAD0In+KAAAADklEQVR4nGP4z8AAQg0AD3oDfnfpf5cAAAAASUVORK5CYII=";
        logo.value_locks={"width"};logo.pixel_width=2;logo.pixel_height=1;logo.width=40;logo.height=20;logo.x=100;logo.y=50;
        for(const auto horizontal:{"left","center","right"})for(const auto vertical:{"bottom","middle","top"}) {
            logo.horizontal=horizontal;logo.vertical=vertical;const auto corners=logo.corners();
            const double anchor_x=std::string(horizontal)=="left"?corners[0][0]:std::string(horizontal)=="right"?corners[1][0]:(corners[0][0]+corners[1][0])/2;
            const double anchor_y=std::string(vertical)=="top"?corners[0][1]:std::string(vertical)=="bottom"?corners[2][1]:(corners[0][1]+corners[2][1])/2;
            require(anchor_x==logo.x&&anchor_y==logo.y,"Image alignment moved the placement anchor");
        }
        for(const auto& entry:std::filesystem::directory_iterator("config/formats")) {
            if(entry.path().extension()!=".frmz"&&entry.path().extension()!=".tblz")continue;
            std::cerr<<"Template import: "<<entry.path().filename()<<'\n';
            auto sketch=zima::drawing::load_template_sketch(entry.path(),prepare);
            require(sketch.drawing_template.has_value()&&!sketch.segments.empty()&&!sketch.texts.empty(),"Template did not open as an editable sketch");
            if(entry.path().extension()==".tblz") {
                require(sketch.dimensions.size()==14,"Title block should retain only unique driving dimensions");
                auto solved=sketch;const auto result=solved.solve();
                require(result.maximum_residual<1e-6,"Simplified title-block constraints do not solve");
                double movement=0;
                for(const auto& point:sketch.points){const auto* after=solved.find_point(point.id);movement=std::max(movement,std::hypot(after->x-point.x,after->y-point.y));}
                std::cerr<<"Title-block solver: residual="<<result.maximum_residual<<", maximum point movement="<<movement<<" mm\n";
                require(movement<1e-6,"Simplifying repeated dimensions moved title-block geometry");
                auto resized=sketch;
                require(resized.set_dimension_value("d1",12),"Shared 10mm master dimension cannot drive its equal lengths");
                require(std::abs(resized.find_point("p192")->x-12)<1e-5&&std::abs(resized.find_point("p192")->y-12)<1e-5,"Master offset did not drive both coordinates");
                const std::array<std::string,7> row_points{"p192","p318","p324","p302","p286","p272","p258"};
                for(std::size_t i=1;i<row_points.size();++i)require(std::abs(resized.find_point(row_points[i])->y-resized.find_point(row_points[i-1])->y-12)<1e-5,"A repeated row did not follow its master height");
                require(resized.set_dimension_value("d1",8),"Shared master cannot shrink again");
                for(std::size_t i=1;i<row_points.size();++i)require(std::abs(resized.find_point(row_points[i])->y-resized.find_point(row_points[i-1])->y-8)<1e-5,"A repeated row did not shrink with its master");
                auto fixed=sketch;fixed.find_point("p192")->fixed=true;const auto before=fixed;
                require(!fixed.set_dimension_value("d1",12)&&fixed.points==before.points&&fixed.dimensions==before.dimensions&&fixed.texts==before.texts,"Conflicting master edit changed fixed geometry or text");
            }
            for(const auto& dimension:sketch.dimensions)
                require(dimension.second_point_id.empty()||dimension.value>=0,"Template retained a negative point-pair length");
            const auto target=folder/entry.path().filename();
            sketch.texts.front().value="Edited &Název";
            sketch.texts.front().color=zima::sketcher::SketchTextColor::Red;
            if(entry.path().extension()==".tblz")sketch.drawing_template->images.push_back(logo);
            zima::drawing::save_template_sketch(sketch,target);
            auto reopened=zima::drawing::load_template_sketch(target,prepare);
            require(reopened.texts.front().value=="Edited &Název"&&reopened.constraints==sketch.constraints&&reopened.dimensions==sketch.dimensions&&reopened.drawing_template->repeat_regions==sketch.drawing_template->repeat_regions,"Template roundtrip lost geometry, constraints or BOM settings");
            require(reopened.texts.front().color==zima::sketcher::SketchTextColor::Red,"Editable template text lost its red pen");
            require(reopened.drawing_template->images==sketch.drawing_template->images,"Template lost embedded image content or placement");
            if(entry.path().extension()==".tblz") {
                auto sheet=zima::drawing::DrawingDocument::create_default().sheets.front();
                zima::drawing::load_title_block_template(sheet,target);
                require(sheet.title_block_images==sketch.drawing_template->images,"Drawing insertion lost embedded logo");
                auto drawing=zima::drawing::DrawingDocument::create_default();drawing.sheets.front()=sheet;
                const auto drawing_path=folder/"embedded-logo.drwz";drawing.save(drawing_path);
                require(zima::drawing::DrawingDocument::load(drawing_path).sheets.front().title_block_images==sheet.title_block_images,"Saved Drawing lost its embedded image");
                require(sheet.repeat_regions.size()==1&&sheet.title_block_circles.size()==2,"Inserted template lost BOM region or circles");
            }
        }
        {
            zima::drawing::DrawingSheet sheet;sheet.repeat_regions={{"BOM",10,20,80,10,"up",12}};
            logo.x=40;logo.y=23;logo.width=4;logo.height=2;logo.horizontal="left";logo.vertical="bottom";sheet.title_block_images={logo};
            sheet.title_block_lines={{{10,20},{90,20}}};
            sheet.title_block_texts={{"&bom.item_number",{80,25}},{"&bom.quantity",{15,25}},{"&Název",{60,25}},{"&Název",{60,5}}};
            sheet.bom_rows={{1,2,"A","A",{}},{2,4,"B","B",{}}};
            sheet.bom_rows[0].parameters["Název"]="First part";sheet.bom_rows[1].parameters["Název"]="Second part";
            zima::drawing::TitleBlockContext context;context.parameters["Název"]="Assembly";
            for(const auto direction:{"up","down","left","right"}) {
                sheet.repeat_regions.front().direction=direction;
                const auto layout=zima::drawing::title_block_layout(sheet,context);
                require(layout.images.size()==2&&layout.images[0].data_base64==logo.data_base64,"BOM lost contained image copies");
                require(layout.lines.size()==2&&layout.texts.size()==7,"BOM did not repeat exactly its contained geometry");
                require(layout.texts[0].text=="1"&&layout.texts[1].text=="2"&&layout.texts[2].text=="2"&&layout.texts[3].text=="4"&&layout.texts[4].text=="First part"&&layout.texts[5].text=="Second part"&&layout.texts[6].text=="Assembly","BOM tokens did not use each source Part's parameters");
                const double dx=layout.lines[1].first.x-layout.lines[0].first.x,dy=layout.lines[1].first.y-layout.lines[0].first.y;
                require(std::abs(dx-(std::string(direction)=="left"?12:std::string(direction)=="right"?-12:0))<1e-9&&std::abs(dy-(std::string(direction)=="up"?12:std::string(direction)=="down"?-12:0))<1e-9,"BOM ignored persisted direction or pitch");
            }
            sheet.repeat_regions.clear();require(zima::drawing::title_block_layout(sheet,context).lines.size()==1,"BOM rendered without a repeat region");
        }
        const auto fixture = zima::drawing::DrawingDocument::load(
            std::filesystem::current_path() / "tests/fixtures/cross_language/drawing.drwz");
        require(fixture.document_id == "drawing-fixture-001" &&
                    fixture.sheets.size() == 1 &&
                    fixture.sheets.front().views.size() == 1 &&
                    fixture.sheets.front().views.front().source_document_id ==
                        "part-fixture-001",
                "Python Drawing fixture lost sheet or view identity");

        // Edit/regenerate/reopen: append a second View to the sole sheet of
        // the Python-produced Drawing fixture, save it, and reopen it to
        // prove the fixture also survives an explicit edit-regenerate-reopen
        // cycle, matching the Part/Assembly coverage in the other contract
        // tests.
        auto edited_fixture_drawing = fixture;
        zima::kernel::ViewerMesh fixture_edit_mesh;
        fixture_edit_mesh.edges.push_back(
            {{{0, 0, 0}, {5, 0, 0}}, {"box", "edge:fixture-edit", ""}});
        auto fixture_edit_view = zima::drawing::DrawingDocument::create_view(
            "part-fixture-001", "part.prtz", fixture_edit_mesh,
            zima::drawing::ViewOrientation::Top);
        const std::string fixture_edit_view_id = fixture_edit_view.id;
        edited_fixture_drawing.sheets.front().views.push_back(
            std::move(fixture_edit_view));
        const auto fixture_edit_drawing_path =
            std::filesystem::temp_directory_path() /
            "zima-cad-fixture-drawing-edit-regenerate-reopen-contract.drwz";
        edited_fixture_drawing.save(fixture_edit_drawing_path);
        const auto reopened_fixture_drawing =
            zima::drawing::DrawingDocument::load(fixture_edit_drawing_path);
        std::filesystem::remove(fixture_edit_drawing_path);
        require(reopened_fixture_drawing.document_id == "drawing-fixture-001" &&
                    reopened_fixture_drawing.sheets.size() == 1 &&
                    reopened_fixture_drawing.sheets.front().views.size() == 2 &&
                    reopened_fixture_drawing.sheets.front().views.back().id ==
                        fixture_edit_view_id &&
                    reopened_fixture_drawing.sheets.front().views.back()
                        .source_document_id == "part-fixture-001" &&
                    reopened_fixture_drawing.sheets.front().views.back()
                        .projected_edges.size() == 1,
                "Edited Python Drawing fixture did not survive "
                "regenerate/save/reopen");

        zima::kernel::ViewerMesh mesh;
        mesh.edges.push_back({{{0, 0, 0}, {10, 0, 0}}, {"box", "edge:x", ""}});
        mesh.edges.push_back({{{0, 0, 10}, {10, 0, 10}}, {"box", "edge:x-top", ""}});
        auto drawing = zima::drawing::DrawingDocument::create_default();
        auto view = zima::drawing::DrawingDocument::create_view(
            "part-1", "part.prtz", mesh, zima::drawing::ViewOrientation::Front);
        require(view.projected_edges.size() == 2 &&
                    std::abs(view.projected_edges.front().points.back().x + 10.0) < 1e-9,
                "Front Drawing projection does not follow viewer convention");
        zima::kernel::ViewerMesh occluded;
        occluded.edges.push_back({{{0, 0, 0}, {10, 0, 0}}, {"box", "edge:hidden", ""}});
        occluded.vertices = {{-1, -1, -1}, {11, -1, -1}, {5, -1, 1}};
        occluded.triangles = {0, 1, 2};
        const auto hidden = zima::drawing::project_edges(
            occluded, zima::drawing::ViewOrientation::Front);
        double hidden_length=0,visible_length=0;
        for(const auto& edge:hidden)if(!edge.silhouette)for(std::size_t i=1;i<edge.points.size();++i)
            (edge.hidden?hidden_length:visible_length)+=std::hypot(edge.points[i].x-edge.points[i-1].x,edge.points[i].y-edge.points[i-1].y);
        require(std::abs(hidden_length-6)<1e-6 && std::abs(visible_length-4)<1e-6,
                "Partial occlusion must split an edge at the actual face boundary");
        const auto cylinder=drawing_cylinder_fixture();
        const auto front=zima::drawing::project_edges(cylinder,zima::drawing::ViewOrientation::Front);
        int sides=0;
        for(const auto& edge:front) {
            require(edge.source.semantic_key!="seam","Periodic surface seam leaked into drawing");
            if(edge.silhouette&&!edge.hidden && std::abs(edge.points.back().y-edge.points.front().y)>19.99) {
                require(std::abs(std::abs(edge.points.front().x)-10)<1e-6,"Cylinder outline has wrong radius");++sides;
            }
        }
        require(sides==2,"Cylinder front view needs two visible silhouette generators");
        for(const auto orientation:{zima::drawing::ViewOrientation::Front,zima::drawing::ViewOrientation::Back,
            zima::drawing::ViewOrientation::Left,zima::drawing::ViewOrientation::Right,zima::drawing::ViewOrientation::Top,
            zima::drawing::ViewOrientation::Bottom,zima::drawing::ViewOrientation::Isometric})
            require(!zima::drawing::project_edges(cylinder,orientation).empty(),"A basic cylinder view lost all curves");
        const auto parent_camera = zima::drawing::standard_camera(
            zima::drawing::ViewOrientation::Front);
        const auto first_angle = zima::drawing::projected_camera(parent_camera,
            zima::drawing::ProjectionDirection::Right,
            zima::drawing::ProjectionMethod::FirstAngle);
        const auto third_angle = zima::drawing::projected_camera(parent_camera,
            zima::drawing::ProjectionDirection::Right,
            zima::drawing::ProjectionMethod::ThirdAngle);
        require(std::abs(first_angle.depth.x - 1.0) < 1e-9 &&
                    std::abs(third_angle.depth.x + 1.0) < 1e-9,
                "First-/third-angle projected cameras did not reverse the view direction");
        view.value_locks={"x","scale"};
        const std::string view_id = view.id;
        drawing.sheets.front().views.push_back(std::move(view));
        zima::drawing::LinearDimension dimension;
        dimension.id = "dimension-1"; dimension.view_id = view_id;
        dimension.first = {"box", "edge:x", ""};
        dimension.second = {"box", "edge:x-top", ""};
        dimension.first_point = {-5, 0}; dimension.second_point = {-5, 10};
        dimension.label_position = {-5, 5}; dimension.measured_value = 10;
        drawing.sheets.front().dimensions.push_back(dimension);
        drawing.sheets.front().bom_rows = {
            {1, 2, "Bracket", "BR-001", "S235JR"},
            {2, 4, "Washer", "WS-002", "A2"},
        };
        zima::drawing::load_frame_template(
            drawing.sheets.front(), "config/formats/ZE-A4.frmz");
        zima::drawing::load_title_block_template(
            drawing.sheets.front(), "config/formats/ZE-TITLE-BLOCK.tblz");
        require(!drawing.sheets.front().frame_lines.empty() &&
                    !drawing.sheets.front().title_block_lines.empty() &&
                    !drawing.sheets.front().title_block_fields.empty() &&
                    drawing.sheets.front().bom_rows.size() == 2 &&
                    drawing.sheets.front().bom_rows.front().designation == "BR-001",
                "C++ Drawing did not embed frame/title-block template geometry");
        auto& title_fields = drawing.sheets.front().title_block_fields;
        const auto drawn_by = std::find_if(title_fields.begin(), title_fields.end(),
            [](const auto& field) { return field.id == "DRAWN_BY"; });
        require(drawn_by != title_fields.end() && drawn_by->editable &&
                    drawn_by->expression == "&Drew" &&
                    drawn_by->alignment == "right" &&
                    drawn_by->vertical_alignment == "middle" &&
                    drawn_by->write_back,
                "Drawing title-block fields lost expression or editability");
        title_fields.front().value = "Ada";
        auto changed_mesh = mesh;
        changed_mesh.edges[1].points = {{0, 0, 20}, {10, 0, 20}};
        drawing.refresh_view(view_id, changed_mesh);
        require(!drawing.sheets.front().dimensions.front().unresolved &&
                    std::abs(drawing.sheets.front().dimensions.front().measured_value - 20.0) < 1e-9,
                "Drawing regeneration did not update an associative linear dimension");
        auto child = zima::drawing::DrawingDocument::create_view(
            "part-1", "part.prtz", mesh, zima::drawing::ViewOrientation::Right);
        child.parent_view_id = view_id;
        child.projection_direction = zima::drawing::ProjectionDirection::Right;
        child.display_style = zima::drawing::DisplayStyle::HiddenEdges;
        child.camera = zima::drawing::projected_camera(
            drawing.find_view(view_id)->camera, child.projection_direction,
            drawing.sheets.front().projection_method);
        child.x = drawing.find_view(view_id)->x + 70.0;
        child.y = drawing.find_view(view_id)->y;
        child.scale = 0.5;
        const std::string child_id = child.id;
        drawing.sheets.front().views.push_back(std::move(child));
        drawing.source_document_id = "part-1";
        drawing.source_path = "part.prtz";
        drawing.source_name = "Hnací hřídel";
        const auto path = std::filesystem::current_path() /
            "zima-cad-cpp-drawing-contract.drwz";
        drawing.save(path);
        std::ifstream persisted(path);
        require(static_cast<bool>(persisted), "Drawing contract file was not written");
        const std::string ini((std::istreambuf_iterator<char>(persisted)), {});
        require(ini.find("[Document]\n") != std::string::npos &&
                    ini.find("format_version=12\n") != std::string::npos &&
                    ini.find("type=drawing\n") != std::string::npos &&
                    ini.find("param.cpp_drawing={") != std::string::npos &&
                    ini.find("[Containers]\n") != std::string::npos &&
                    ini.find("items=\n") != std::string::npos,
                "Drawing persistence is not Python-compatible INI");
        const auto loaded = zima::drawing::DrawingDocument::load(path);
        persisted.close();
        std::filesystem::remove(path);
        require(loaded.document_id == drawing.document_id &&
                    loaded.source_document_id == "part-1" &&
                    loaded.source_path == std::filesystem::path("part.prtz") &&
                    loaded.source_name == "Hnací hřídel" &&
                    loaded.sheets.size() == 1 && loaded.find_view(view_id) != nullptr &&
                    loaded.find_view(child_id) != nullptr &&
                    loaded.find_view(child_id)->parent_view_id == view_id &&
                    loaded.find_view(child_id)->projection_direction ==
                        zima::drawing::ProjectionDirection::Right &&
                    loaded.find_view(child_id)->display_style ==
                        zima::drawing::DisplayStyle::HiddenEdges &&
                    std::abs(loaded.find_view(child_id)->scale - 0.5) < 1e-9 &&
                    !loaded.sheets.front().frame_lines.empty() &&
                    !loaded.sheets.front().title_block_fields.empty() &&
                    loaded.sheets.front().bom_rows.size() == 2 &&
                    loaded.sheets.front().repeat_regions.size()==1 &&
                    loaded.sheets.front().title_block_circles.size()==2 &&
                    loaded.sheets.front().bom_rows[1].material == "A2" &&
                    loaded.sheets.front().title_block_fields.front().value == "Ada" &&
                    loaded.sheets.front().title_block_fields.front().write_back &&
                    std::abs(loaded.sheets.front().dimensions.front().measured_value - 20.0) < 1e-9 &&
                    loaded.find_view(view_id)->projected_edges.front().source.semantic_key == "edge:x",
                "Drawing save/load lost source identity or projected geometry");
        require(loaded.sheets.front().width_mm() == 210.0 &&
                    loaded.sheets.front().height_mm() == 297.0,
                "A4 Drawing orientation is not portrait");
        require(loaded.name == drawing.name &&
                    loaded.sheets.front().name == drawing.sheets.front().name &&
                    loaded.sheets.front().projection_method ==
                        drawing.sheets.front().projection_method &&
                    loaded.find_view(view_id)->value_locks == std::set<std::string>{"x","scale"} &&
                    loaded.find_view(view_id)->name == drawing.find_view(view_id)->name &&
                    loaded.find_view(view_id)->source_path ==
                        drawing.find_view(view_id)->source_path,
                "Drawing metadata, views, or sheets did not round-trip");

        {
            zima::drawing::TitleBlockContext context;context.parameters["name"]="flat";
            context.parameter_values["name"]={{"cs","Česky"},{"ru","Русский"}};
            context.parameter_aliases["Наименование"]="name";context.mass_unit="g";
            auto sheet=drawing.sheets.front();sheet.title_block_locale="ru";sheet.local_parameters["note"]="Local";
            zima::drawing::TitleBlockField field;field.expression="&Наименование [&document.mass_unit] &drawing.note";
            require(zima::drawing::resolve_title_block_text(field,context,sheet)=="Русский [g] Local","Localized title block or mass unit ignored document settings");
        }
        // Title-block token resolution: mirrors Python's title_block.py
        // resolve_title_block_text() unit tests (model/system/local token
        // scopes, Czech-labelled parameter aliases, and BOM row tokens).
        require(zima::drawing::title_block_tokens(
                    "Číslo &document.file_stem.&model.verze / &drawing.edice") ==
                std::vector<std::string>{
                    "document.file_stem", "model.verze", "drawing.edice"},
                "title_block_tokens did not extract tokens in written order");
        require(zima::drawing::title_block_token_scope("model.verze") == "model" &&
                    zima::drawing::title_block_token_scope("drawing.edice") == "drawing" &&
                    zima::drawing::title_block_token_scope("sheet.scale") == "system",
                "title_block_token_scope misclassified a token");
        {
            zima::drawing::TitleBlockField field;
            field.expression = "Číslo &document.file_stem.&model.verze / &drawing.edice";
            zima::drawing::TitleBlockContext context;
            context.file_stem = "ZE0019-0200-0001";
            context.parameters = {{"verze", "03"}};
            context.local_parameters = {{"edice", "A"}};
            zima::drawing::DrawingSheet sheet;
            require(zima::drawing::resolve_title_block_text(field, context, sheet) ==
                        "Číslo ZE0019-0200-0001.03 / A",
                    "resolve_title_block_text did not resolve model/system/local tokens");
        }
        {
            zima::drawing::TitleBlockField field;
            field.expression = "&Název / &Materiál";
            zima::drawing::TitleBlockContext context;
            context.parameters = {{"nazev", "OBJÍMKA"}, {"material", "S235JR"}};
            context.parameter_aliases = {{"Název", "nazev"}, {"Materiál", "material"}};
            zima::drawing::DrawingSheet sheet;
            require(zima::drawing::resolve_title_block_text(field, context, sheet) ==
                        "OBJÍMKA / S235JR",
                    "resolve_title_block_text did not resolve a localized parameter alias");
        }
        {
            zima::drawing::TitleBlockField field;
            field.expression = "&bom.item_number / &bom.quantity";
            zima::drawing::TitleBlockContext context;
            context.has_bom_row = true; context.bom_item_number = 3; context.bom_quantity = 7;
            zima::drawing::DrawingSheet sheet;
            require(zima::drawing::resolve_title_block_text(field, context, sheet) == "3 / 7",
                    "resolve_title_block_text did not resolve BOM row tokens");
        }
        std::cout << "C++ Drawing contracts passed\n";
        return 0;
    } catch (const std::exception& error) {
        std::cerr << error.what() << '\n';
        return 1;
    }
}
