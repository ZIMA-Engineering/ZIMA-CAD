#include "sketch_command_support.hpp"

namespace zima::command_host {
using namespace sketch_commands;
namespace {
Json solution(const sketcher::SolveResult& value) {
    const char* status=value.status==sketcher::SolveStatus::Solved?"solved":value.status==sketcher::SolveStatus::UnderConstrained?"under_constrained":value.status==sketcher::SolveStatus::Conflicting?"conflicting":"invalid";
    return {{"status",status},{"remaining_degrees_of_freedom",value.remaining_degrees_of_freedom},{"maximum_residual",value.maximum_residual}};
}
}
void Host::register_sketch_relation_commands() {
    add_sketch_command({"sketch.constraint.create",tr("Create a native Sketch relation using explicitly ordered points and geometry."),{{"kind",true},{"points",false,Type::Array},{"geometry",false,Type::Array}}},[](Sketch& s,const Json& a) {
        using Kind=sketcher::ConstraintKind;
        const auto kind=a["kind"].get<std::string>();const auto points=ids(a,"points"),geometry=ids(a,"geometry");
        const auto shape=[&](std::size_t p,std::size_t g) {if(points.size()!=p||geometry.size()!=g)invalid("The constraint requires a different number of points or geometry references.");};
        std::string id;
        if(kind=="horizontal"||kind=="vertical") {
            const auto relation=kind=="horizontal"?Kind::Horizontal:Kind::Vertical;
            if(points.empty()){shape(0,1);id=s.add_segment_constraint(geometry[0],relation);}
            else {shape(2,0);id=s.add_point_pair_constraint(points[0],points[1],relation);}
        } else if(kind=="coincident") {
            shape(2,0);return Json{{"point",s.merge_points(points[0],points[1])},{"kind",kind}};
        } else if(kind=="point_reference") {
            shape(2,0);id=s.add_point_reference_constraint(points[0],points[1]);
        } else if(kind=="parallel"||kind=="perpendicular"||kind=="equal_length") {
            shape(0,2);id=s.add_segment_pair_constraint(geometry[0],geometry[1],kind=="parallel"?Kind::Parallel:kind=="perpendicular"?Kind::Perpendicular:Kind::EqualLength);
        } else if(kind=="equal_radius") {shape(0,2);id=s.add_equal_radius_constraint(geometry[0],geometry[1]);
        } else if(kind=="concentric") {shape(0,2);id=s.add_concentric_constraint(geometry[0],geometry[1]);
        } else if(kind=="point_on_circle") {shape(1,1);id=s.add_point_on_circle_constraint(points[0],geometry[0]);
        } else if(kind=="point_on_line") {shape(1,1);id=s.add_point_on_line_constraint(points[0],geometry[0]);
        } else if(kind=="midpoint") {shape(1,1);id=s.add_midpoint_constraint(points[0],geometry[0]);
        } else if(kind=="midpoint_on_line") {shape(0,2);id=s.add_midpoint_on_line_constraint(geometry[0],geometry[1]);
        } else if(kind=="symmetric") {shape(2,1);id=s.add_symmetric_constraint(points[0],points[1],geometry[0]);
        } else if(kind=="tangent") {
            if(points.size()>1||geometry.size()!=2)invalid("Tangency requires two curves and at most one contact point.");
            id=s.add_tangent_constraint(geometry[0],geometry[1],points.empty()?std::string{}:points[0]);
        } else invalid("Unknown Sketch constraint kind.");
        return Json{{"constraint",id},{"kind",kind}};
    });
    add_sketch_command({"sketch.constraint.delete",tr("Remove a Sketch relation and solve its remaining constraints."),{{"constraint",true}}},[](Sketch& s,const Json& a) {
        const auto id=a["constraint"].get<std::string>();s.remove_constraint(id);return Json{{"constraint",id}};
    });
    add_sketch_command({"sketch.solve",tr("Explicitly solve a Sketch without calculating a solid body."),{{"iterations",false,Type::Integer}}},[](Sketch& s,const Json& a) {
        const auto result=s.solve(integer(a,"iterations",100,1,10000));
        if(result.status==sketcher::SolveStatus::Invalid||result.status==sketcher::SolveStatus::Conflicting)
            throw workspace::SketchOperationError("constraint_conflict","The Sketch constraints could not be solved; no change was committed.");
        return solution(result);
    });
    add_sketch_query({"sketch.solve_status",tr("Inspect constraint residual and free degrees of freedom on a temporary Sketch copy."),{{"iterations",false,Type::Integer}}},[](const Sketch& s,const Json& a) {
        auto draft=s;return solution(draft.solve(integer(a,"iterations",100,1,10000)));
    });
}
}
