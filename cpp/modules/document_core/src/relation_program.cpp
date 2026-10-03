#include <zima/document/relation_program.hpp>
#include <algorithm>
#include <charconv>
#include <cmath>
#include <functional>
#include <iomanip>
#include <locale>
#include <numbers>
#include <optional>
#include <set>
#include <sstream>
#include <vector>

namespace zima::document {
RelationError::RelationError(int l, int c, const std::string& m, std::string d)
    : std::invalid_argument("Line " + std::to_string(l) + ": " + m + (d.empty() ? "" : " " + d)),
      line(l), column(c), message(m), detail(std::move(d)) {}
namespace {
bool letter(char c) { return (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') || c == '_'; }
bool digit(char c) { return c >= '0' && c <= '9'; }
struct Token { std::string text; int column{}; bool quoted{}; std::size_t end{}; };
struct Expr {
    std::string op;
    RelationValue value;
    std::vector<std::shared_ptr<Expr>> args;
    int line{}, column{};
    std::size_t argument_begin{}, call_close{}, call_end{};
};
using Node = std::shared_ptr<Expr>;
[[noreturn]] void fail(const Expr& e, const std::string& message, const std::string& detail = {}) {
    throw RelationError(e.line, e.column, message, detail);
}
std::vector<Token> lex(const std::string& text, int line) {
    std::vector<Token> out;
    for (std::size_t i = 0; i < text.size();) {
        if (text[i] == ' ' || text[i] == '\t' || text[i] == '\r') { ++i; continue; }
        if (text[i] == '#') break;
        const int column = static_cast<int>(i + 1);
        const auto start = i;
        if (text[i] == '"') {
            ++i; std::string value; bool closed = false;
            while (i < text.size()) {
                char c = text[i++];
                if (c == '"') { closed = true; break; }
                if (c == '\\') {
                    if (i == text.size()) break;
                    c = text[i++];
                    if (c == 'n') c = '\n'; else if (c == 't') c = '\t'; else if (c == 'r') c = '\r';
                    else if (c != '\\' && c != '"') throw RelationError(line, column, "Invalid text escape.");
                }
                value += c;
            }
            if (!closed) throw RelationError(line, column, "Unterminated text.");
            out.push_back({value, column, true});
        } else if (letter(text[i])) {
            while (i < text.size() && (letter(text[i]) || digit(text[i]) || text[i] == '.')) ++i;
            out.push_back({text.substr(start, i - start), column});
        } else if (digit(text[i]) || text[i] == '.') {
            double value{};
            const auto parsed = std::from_chars(text.data() + i, text.data() + text.size(), value);
            if (parsed.ec != std::errc{} || parsed.ptr == text.data() + i || !std::isfinite(value))
                throw RelationError(line, column, "Invalid number.");
            i = static_cast<std::size_t>(parsed.ptr - text.data());
            out.push_back({text.substr(start, i - start), column});
        } else {
            std::string op(1, text[i++]);
            if (i < text.size() && text[i] == '=' && (op == "=" || op == "!" || op == "<" || op == ">")) op += text[i++];
            if (std::string("=<>!+-*/%^&(),").find(op.front()) == std::string::npos)
                throw RelationError(line, column, "Unexpected character.", op);
            out.push_back({op, column});
        }
        out.back().end=i;
    }
    return out;
}
const std::map<std::string, std::pair<int, int>> functions{
    {"abs",{1,1}},{"sqrt",{1,1}},{"sin",{1,1}},{"cos",{1,1}},{"tan",{1,1}},
    {"sind",{1,1}},{"cosd",{1,1}},{"tand",{1,1}},{"asin",{1,1}},{"acos",{1,1}},{"atan",{1,1}},
    {"asind",{1,1}},{"acosd",{1,1}},{"atand",{1,1}},{"atan2",{2,2}},{"atan2d",{2,2}},
    {"min",{2,32}},{"max",{2,32}},{"round",{1,2}},{"floor",{1,1}},{"ceil",{1,1}},
    {"exp",{1,1}},{"ln",{1,1}},{"log",{1,1}},{"log10",{1,1}}};
int precedence(const std::string& op) {
    if (op == "or") return 1;
    if (op == "and") return 2;
    if (op == "==" || op == "!=" || op == "<" || op == ">" || op == "<=" || op == ">=") return 3;
    if (op == "&") return 4;
    if (op == "+" || op == "-") return 5;
    if (op == "*" || op == "/" || op == "%") return 6;
    if (op == "^") return 8;
    return 0;
}
class ExpressionParser {
    const std::vector<Token>& tokens_; std::size_t pos_; int line_; int depth_{};
    Node expression(int minimum = 1) {
        if (++depth_ > 128) throw RelationError(line_, 1, "Expression nesting is too deep.");
        if (pos_ >= tokens_.size()) throw RelationError(line_, 1, "Expected an expression.");
        const auto token = tokens_[pos_++];
        auto left = std::make_shared<Expr>(); left->line = line_; left->column = token.column;
        if (token.quoted) { left->op = "literal"; left->value.data = token.text; }
        else if (token.text == "+" || token.text == "-" || token.text == "not") {
            left->op = "unary" + token.text; left->args = {expression(token.text == "not" ? 3 : 7)};
        } else if (token.text == "(") {
            left = expression(); require(")");
        } else if (digit(token.text.front()) || token.text.front() == '.') {
            double value{}; std::from_chars(token.text.data(), token.text.data() + token.text.size(), value);
            left->op = "literal"; left->value = {value, {}, true};
        } else if (letter(token.text.front())) {
            if (pos_ < tokens_.size() && tokens_[pos_].text == "(") {
                left->argument_begin=tokens_[pos_++].end; left->op = "call:" + token.text;
                if (pos_ < tokens_.size() && tokens_[pos_].text != ")") {
                    do {
                        if (left->args.size() >= 32) fail(*left, "Too many function arguments.");
                        left->args.push_back(expression());
                        if (pos_ >= tokens_.size() || tokens_[pos_].text != ",") break;
                        ++pos_;
                    } while (true);
                }
                require(")");
                left->call_close=tokens_[pos_-1].column-1;left->call_end=tokens_[pos_-1].end;
                const auto fn = functions.find(token.text);
                if (fn == functions.end()) fail(*left, "Unknown function.", token.text);
                if (left->args.size() < fn->second.first || left->args.size() > fn->second.second)
                    fail(*left, "Wrong function argument count.", token.text);
            } else if (token.text == "pi" || token.text == "e") {
                left->op = "literal"; left->value = {token.text == "pi" ? std::numbers::pi : std::numbers::e, {}, true};
            } else if (token.text == "true" || token.text == "false") {
                left->op = "literal"; left->value.data = token.text == "true";
            } else { left->op = "name"; left->value.data = token.text; }
        } else fail(*left, "Expected an expression.");
        while (pos_ < tokens_.size() && !tokens_[pos_].quoted) {
            const auto op = tokens_[pos_]; const int rank = precedence(op.text);
            if (rank < minimum) break;
            ++pos_; auto right = expression(rank + (op.text == "^" ? 0 : 1));
            left = std::make_shared<Expr>(Expr{op.text, {}, {left, right}, line_, op.column});
        }
        --depth_; return left;
    }
    void require(const std::string& token) {
        if (pos_ >= tokens_.size() || tokens_[pos_].quoted || tokens_[pos_++].text != token)
            throw RelationError(line_, 1, "Expected closing parenthesis.");
    }
public:
    ExpressionParser(const std::vector<Token>& tokens, std::size_t start, int line) : tokens_(tokens), pos_(start), line_(line) {}
    Node parse() {
        auto result = expression();
        if (pos_ != tokens_.size()) throw RelationError(line_, tokens_[pos_].column, "Unexpected token.", tokens_[pos_].text);
        return result;
    }
};
struct Guard { Node condition; bool expected{}; };
struct Assignment {
    std::string target, source;
    Node expression;
    std::vector<Guard> guards;
    std::map<int, int> branches;
};
void names(const Node& node, std::set<std::string>& result) {
    if (node->op == "name") result.insert(std::get<std::string>(node->value.data));
    for (const auto& child : node->args) names(child, result);
}
double number(const RelationValue& v, const Expr& e) {
    if (const auto p = std::get_if<double>(&v.data)) return *p;
    fail(e, "A numeric value is required.");
}
bool truth(const RelationValue& v, const Expr& e) {
    if (const auto p = std::get_if<bool>(&v.data)) return *p;
    fail(e, "A condition must be true or false.");
}
std::array<int,3> compatible(const RelationValue& a, const RelationValue& b, const Expr& e) {
    if (a.units == b.units) return a.units;
    if (a.literal) return b.units;
    if (b.literal) return a.units;
    fail(e, "Incompatible units.");
}
RelationValue evaluate(const Node& node, const std::function<RelationValue(const std::string&)>& lookup, int decimals,double degrees_per_angle_unit=1) {
    const auto& e = *node;
    if (e.op == "literal") return e.value;
    if (e.op == "name") return lookup(std::get<std::string>(e.value.data));
    auto a = evaluate(e.args.front(), lookup, decimals,degrees_per_angle_unit);
    if (e.op == "unarynot") return { !truth(a,e) };
    if (e.op == "unary+" || e.op == "unary-") { a.data = (e.op == "unary-" ? -1.0 : 1.0) * number(a,e); return a; }
    if (e.op == "and" && !truth(a,e)) return {false};
    if (e.op == "or" && truth(a,e)) return {true};
    RelationValue result;
    if (e.op.starts_with("call:")) {
        std::vector<RelationValue> args{a};
        for (std::size_t i=1;i<e.args.size();++i) args.push_back(evaluate(e.args[i],lookup,decimals,degrees_per_angle_unit));
        const auto fn=e.op.substr(5); double x=number(a,e), y=args.size()>1?number(args[1],e):0, r{};
        result.units=a.units; result.literal=a.literal;
        const auto scalar=[&] { if(a.units!=std::array<int,3>{}) fail(e,"A dimensionless value is required."); };
        if(fn=="abs") r=std::abs(x);
        else if(fn=="sqrt") {
            for(auto& power:result.units){if(power%2)fail(e,"Incompatible units.");power/=2;}
            r=std::sqrt(x);
        } else if(fn=="min"||fn=="max") {
            r=x;
            for(std::size_t i=1;i<args.size();++i){result.units=compatible(result,args[i],e);result.literal&=args[i].literal;r=fn=="min"?std::min(r,number(args[i],e)):std::max(r,number(args[i],e));}
        } else if(fn=="round") {
            if(args.size()>1&&(args[1].units!=std::array<int,3>{}||y!=std::floor(y)||y < -12||y>12))fail(e,"Invalid rounding precision.");
            const auto factor=std::pow(10.,y);r=std::round(x*factor)/factor;
        } else if(fn=="floor") r=std::floor(x);
        else if(fn=="ceil") r=std::ceil(x);
        else if(fn=="sin"||fn=="cos"||fn=="tan"||fn=="sind"||fn=="cosd"||fn=="tand") {
            if(a.units!=std::array<int,3>{}&&a.units!=std::array<int,3>{0,1,0})fail(e,"An angle is required.");
            if(a.units==std::array<int,3>{0,1,0})x*=degrees_per_angle_unit*std::numbers::pi/180.;
            else if(fn.ends_with('d'))x*=std::numbers::pi/180.;
            if(fn=="tan"||fn=="tand") {if(std::abs(std::cos(x))<1e-15)fail(e,"Undefined tangent.");r=std::tan(x);}
            else r=fn=="sin"||fn=="sind"?std::sin(x):std::cos(x);
            result.units={};result.literal=false;
        } else if(fn=="atan2"||fn=="atan2d") {
            static_cast<void>(compatible(a,args[1],e));r=std::atan2(x,y);result.units={0,1,0};result.literal=false;
            r*=180./std::numbers::pi/degrees_per_angle_unit;
        } else {
            scalar();result.units={};result.literal=false;
            if(fn=="asin"||fn=="asind")r=std::asin(x);
            else if(fn=="acos"||fn=="acosd")r=std::acos(x);
            else if(fn=="atan"||fn=="atand")r=std::atan(x);
            else if(fn=="exp")r=std::exp(x);
            else if(fn=="log"||fn=="ln")r=std::log(x);
            else r=std::log10(x);
            if(fn.starts_with("a")){result.units={0,1,0};r*=180./std::numbers::pi/degrees_per_angle_unit;}
        }
        result.data=r;
    } else {
        auto b=evaluate(e.args[1],lookup,decimals,degrees_per_angle_unit);
        if(e.op=="&") {
            auto left=relation_value_text(a,decimals),right=relation_value_text(b,decimals);
            if(left.size()+right.size()>1024*1024)fail(e,"Invalid relation source size or content.");
            return {left+right};
        }
        if(e.op=="and"||e.op=="or")return {truth(b,e)};
        if(e.op=="=="||e.op=="!="||e.op=="<"||e.op==">"||e.op=="<="||e.op==">=") {
            if(a.data.index()!=b.data.index())fail(e,"Incompatible value types.");
            if(std::holds_alternative<double>(a.data))static_cast<void>(compatible(a,b,e));
            return {e.op=="=="?a.data==b.data:e.op=="!="?a.data!=b.data:e.op=="<"?a.data<b.data:e.op==">"?a.data>b.data:e.op=="<="?a.data<=b.data:a.data>=b.data};
        }
        const double x=number(a,e),y=number(b,e); double r{};
        result.literal=a.literal&&b.literal;
        if(e.op=="+"||e.op=="-"||e.op=="%") {
            result.units=compatible(a,b,e);
            if(e.op=="%"&&y==0)fail(e,"Division by zero.");
            r=e.op=="+"?x+y:e.op=="-"?x-y:std::fmod(x,y);
        } else if(e.op=="*"||e.op=="/") {
            if(e.op=="/"&&y==0)fail(e,"Division by zero.");
            for(int i=0;i<3;++i)result.units[i]=a.units[i]+(e.op=="*"?1:-1)*b.units[i];
            r=e.op=="*"?x*y:x/y;
        } else {
            if(b.units!=std::array<int,3>{})fail(e,"A dimensionless value is required.");
            if(a.units!=std::array<int,3>{}&&(y!=std::floor(y)||std::abs(y)>16))fail(e,"Invalid quantity exponent.");
            for(int i=0;i<3;++i)result.units[i]=a.units[i]==0?0:a.units[i]*static_cast<int>(y);
            r=std::pow(x,y);
        }
        result.data=r;
    }
    for(const auto power:result.units)if(std::abs(power)>64)fail(e,"Invalid quantity exponent.");
    if(!std::isfinite(number(result,e)))fail(e,"The calculation has no finite real result.");
    return result;
}
} // namespace
struct RelationProgramData { std::vector<Assignment> assignments; std::vector<Node> conditions; std::string source; };

RelationProgram::RelationProgram(const std::string& source) {
    if(source.size()>1024*1024||source.find('\0')!=std::string::npos)throw RelationError(1,1,"Invalid relation source size or content.");
    auto data=std::make_shared<RelationProgramData>();data->source=source;
    struct Block { int id{},branch{};bool otherwise{};std::vector<Node> conditions; };
    std::vector<Block> blocks;int next_block=0,line=0;std::istringstream input(source);std::string text;
    while(std::getline(input,text)) {
        ++line;auto tokens=lex(text,line);if(tokens.empty())continue;
        if(tokens.size()>512)throw RelationError(line,1,"Expression nesting is too deep.");
        const auto& first=tokens.front();
        if(!first.quoted&&(first.text=="if"||first.text=="elseif")) {
            auto condition=ExpressionParser(tokens,1,line).parse();
            data->conditions.push_back(condition);
            if(first.text=="if") {
                if(blocks.size()>=64)throw RelationError(line,1,"Conditional nesting is too deep.");
                blocks.push_back({++next_block,0,false,{condition}});
            } else {
                if(blocks.empty()||blocks.back().otherwise)throw RelationError(line,1,"Unexpected conditional keyword.");
                ++blocks.back().branch;blocks.back().conditions.push_back(condition);
            }
            continue;
        }
        if(!first.quoted&&(first.text=="else"||first.text=="endif")) {
            if(tokens.size()!=1||blocks.empty()||(first.text=="else"&&blocks.back().otherwise))throw RelationError(line,1,"Unexpected conditional keyword.");
            if(first.text=="endif")blocks.pop_back();else{++blocks.back().branch;blocks.back().otherwise=true;}
            continue;
        }
        if(tokens.size()<3||first.quoted||!letter(first.text.front())||
           !std::ranges::all_of(first.text,[](char c){return letter(c)||digit(c);})||tokens[1].text!="="||tokens[1].quoted||
           first.text=="pi"||first.text=="e"||first.text=="true"||first.text=="false"||first.text=="and"||first.text=="or"||first.text=="not")
            throw RelationError(line,1,"Expected target = expression.");
        Assignment assignment{first.text,text,ExpressionParser(tokens,2,line).parse()};
        for(const auto& block:blocks) {
            assignment.branches[block.id]=block.branch;
            for(std::size_t i=0;i<block.conditions.size();++i)assignment.guards.push_back({block.conditions[i],!block.otherwise&&i+1==block.conditions.size()});
        }
        for(const auto& other:data->assignments)if(other.target==assignment.target) {
            const bool exclusive=std::ranges::any_of(assignment.branches,[&](const auto& branch){const auto found=other.branches.find(branch.first);return found!=other.branches.end()&&found->second!=branch.second;});
            if(!exclusive)throw RelationError(line,1,"The target is assigned more than once.",first.text);
        }
        if(data->assignments.size()>=4096)throw RelationError(line,1,"Too many relations.");
        data->assignments.push_back(std::move(assignment));
    }
    if(!blocks.empty())throw RelationError(line,1,"Missing endif.");
    data_=std::move(data);
}

void RelationProgram::validate(const RelationInputs& inputs) const {
    std::map<std::string,std::set<std::string>> graph;
    std::map<std::string,const Expr*> locations;
    for(const auto& a:data_->assignments){graph[a.target];locations[a.target]=a.expression.get();}
    for(const auto& condition:data_->conditions) {
        std::set<std::string> dependencies;names(condition,dependencies);
        for(const auto& name:dependencies)if(!graph.contains(name)&&!inputs.contains(name))fail(*condition,"Unknown name.",name);
    }
    for(const auto& a:data_->assignments) {
        const auto found=inputs.find(a.target);
        if((found!=inputs.end()&&!found->second.writable)||(a.target.size()>1&&a.target[0]=='d'&&std::ranges::all_of(a.target.substr(1),digit)&&found==inputs.end()))fail(*a.expression,"The target is not an editable dimension or parameter.",a.target);
        auto& dependencies=graph[a.target];names(a.expression,dependencies);
        for(const auto& guard:a.guards)names(guard.condition,dependencies);
        for(const auto& name:dependencies)if(!graph.contains(name)&&!inputs.contains(name))fail(*a.expression,"Unknown name.",name);
    }
    std::map<std::string,int> state;std::map<std::string,bool> physical;
    std::function<bool(const std::string&,int)> visit=[&](const std::string& name,int depth) {
        if(depth>256)fail(*locations.at(name),"Relation dependencies are too deep.");
        if(state[name]==1)fail(*locations.at(name),"Circular relation dependency.",name);
        if(state[name]==2)return physical[name];
        state[name]=1;bool calculated=false;
        for(const auto& dependency:graph[name]) {
            if(graph.contains(dependency))calculated|=visit(dependency,depth+1);
            else calculated|=inputs.at(dependency).calculated;
        }
        if(name.size()>1&&name[0]=='d'&&std::ranges::all_of(name.substr(1),digit)&&calculated)
            fail(*locations.at(name),"Calculated geometry cannot drive a dimension.",name);
        state[name]=2;return physical[name]=calculated;
    };
    for(const auto& [name,deps]:graph)static_cast<void>(visit(name,0));
}

std::map<std::string,RelationValue> RelationProgram::evaluate(const RelationInputs& inputs,int decimals,bool dimensions_only,double degrees_per_angle_unit) const {
    validate(inputs);
    if(decimals<0||decimals>12)throw RelationError(1,1,"Invalid rounding precision.");
    if(!(degrees_per_angle_unit>0)||!std::isfinite(degrees_per_angle_unit))throw RelationError(1,1,"Incompatible units.");
    std::map<std::string,std::vector<const Assignment*>> assignments;
    for(const auto& a:data_->assignments)assignments[a.target].push_back(&a);
    std::map<std::string,RelationValue> values,output;
    std::function<RelationValue(const std::string&)> lookup=[&](const std::string& name)->RelationValue {
        if(values.contains(name))return values.at(name);
        const auto definitions=assignments.find(name);
        if(definitions!=assignments.end())for(const auto* a:definitions->second) {
            bool selected=true;
            for(const auto& guard:a->guards)if(truth(document::evaluate(guard.condition,lookup,decimals,degrees_per_angle_unit),*guard.condition)!=guard.expected){selected=false;break;}
            if(!selected)continue;
            auto value=document::evaluate(a->expression,lookup,decimals,degrees_per_angle_unit);
            if(const auto target=inputs.find(name);target!=inputs.end()&&name.size()>1&&name[0]=='d'&&std::ranges::all_of(name.substr(1),digit)) {
                static_cast<void>(number(value,*a->expression));
                // Unit-free formulas, like numeric literals, use the target's
                // document unit. Quantified expressions must still match it.
                value.units=value.units==std::array<int,3>{}?target->second.value.units:
                    compatible(target->second.value,value,*a->expression);value.literal=false;
            }
            output[name]=value;return values[name]=value;
        }
        const auto original=inputs.find(name);
        if(original==inputs.end())fail(*definitions->second.front()->expression,"No active assignment or previous value.",name);
        return values[name]=original->second.value;
    };
    for(const auto& [name,rows]:assignments)if(!dimensions_only||(name.size()>1&&name[0]=='d'&&std::ranges::all_of(name.substr(1),digit)))static_cast<void>(lookup(name));
    return output;
}

namespace {
// Static quantity inference deliberately visits inactive/short-circuited paths.
// It does not execute division, roots or transcendental functions on model data.
RelationValue quantity_type(const Node& node,const std::function<RelationValue(const std::string&)>& lookup,double degrees_per_angle_unit) {
    const auto& e=*node;
    if(e.op=="literal")return e.value;
    if(e.op=="name")return lookup(std::get<std::string>(e.value.data));
    auto a=quantity_type(e.args.front(),lookup,degrees_per_angle_unit);
    if(e.op=="unarynot"){static_cast<void>(truth(a,e));return {false};}
    if(e.op=="unary+"||e.op=="unary-"){static_cast<void>(number(a,e));return a;}
    std::vector<RelationValue> args{a};
    for(std::size_t i=1;i<e.args.size();++i)args.push_back(quantity_type(e.args[i],lookup,degrees_per_angle_unit));
    RelationValue result{0.,a.units,a.literal};
    if(e.op.starts_with("call:")) {
        for(const auto& value:args)static_cast<void>(number(value,e));
        const auto fn=e.op.substr(5);
        if(fn=="sqrt")for(auto& power:result.units){if(power%2)fail(e,"Incompatible units.");power/=2;}
        else if(fn=="min"||fn=="max")for(std::size_t i=1;i<args.size();++i){result.units=compatible(result,args[i],e);result.literal&=args[i].literal;}
        else if(fn=="round") {if(args.size()>1&&args[1].units!=std::array<int,3>{})fail(e,"Invalid rounding precision.");}
        else if(fn=="sin"||fn=="cos"||fn=="tan"||fn=="sind"||fn=="cosd"||fn=="tand") {
            if(a.units!=std::array<int,3>{}&&a.units!=std::array<int,3>{0,1,0})fail(e,"An angle is required.");
            result.units={};result.literal=false;
        } else if(fn=="atan2"||fn=="atan2d") {
            static_cast<void>(compatible(a,args[1],e));result.units={0,1,0};result.literal=false;
        } else if(fn!="abs"&&fn!="floor"&&fn!="ceil"&&fn!="sqrt"&&fn!="min"&&fn!="max"&&fn!="round") {
            if(a.units!=std::array<int,3>{})fail(e,"A dimensionless value is required.");
            result.units=fn.starts_with('a')?std::array<int,3>{0,1,0}:std::array<int,3>{};result.literal=false;
        }
    } else {
        const auto& b=args[1];
        if(e.op=="&")return {std::string{}};
        if(e.op=="and"||e.op=="or"){static_cast<void>(truth(a,e));static_cast<void>(truth(b,e));return {false};}
        if(e.op=="=="||e.op=="!="||e.op=="<"||e.op==">"||e.op=="<="||e.op==">=") {
            if(a.data.index()!=b.data.index())fail(e,"Incompatible value types.");
            if(std::holds_alternative<double>(a.data))static_cast<void>(compatible(a,b,e));return {false};
        }
        static_cast<void>(number(a,e));static_cast<void>(number(b,e));result.literal=a.literal&&b.literal;
        if(e.op=="+"||e.op=="-"||e.op=="%")result.units=compatible(a,b,e);
        else if(e.op=="*"||e.op=="/")for(int i=0;i<3;++i)result.units[i]=a.units[i]+(e.op=="*"?1:-1)*b.units[i];
        else {
            if(b.units!=std::array<int,3>{})fail(e,"A dimensionless value is required.");
            if(a.units!=std::array<int,3>{}) {
                std::set<std::string> dependencies;names(e.args[1],dependencies);
                if(!dependencies.empty())fail(e,"Cannot safely convert relation units.");
                const double exponent=number(document::evaluate(e.args[1],lookup,3,degrees_per_angle_unit),e);
                if(exponent!=std::floor(exponent)||std::abs(exponent)>16)fail(e,"Invalid quantity exponent.");
                for(int i=0;i<3;++i)result.units[i]=a.units[i]*static_cast<int>(exponent);
            }
        }
    }
    for(const auto power:result.units)if(std::abs(power)>64)fail(e,"Invalid quantity exponent.");
    return result;
}
bool dimension_name(const std::string& name) {
    return name.size()>1&&name[0]=='d'&&std::ranges::all_of(name.substr(1),digit);
}
std::string conversion_number(double value) {
    std::ostringstream out;out.imbue(std::locale::classic());out<<std::setprecision(17)<<value;return out.str();
}
}
RelationUnitConversion RelationProgram::convert_units(const RelationInputs& inputs,const std::array<double,3>& factors,const std::set<std::string>& fixed_unit_names,double degrees_per_angle_unit) const {
    validate(inputs);
    if(!(degrees_per_angle_unit>0)||!std::isfinite(degrees_per_angle_unit))throw RelationError(1,1,"Cannot safely convert relation units.");
    for(const double factor:factors)if(!(factor>0)||!std::isfinite(factor))throw RelationError(1,1,"Cannot safely convert relation units.");
    std::map<std::string,std::vector<const Assignment*>> assignments;
    for(const auto& a:data_->assignments)assignments[a.target].push_back(&a);
    std::map<std::string,RelationValue> types;
    std::function<RelationValue(const std::string&)> lookup=[&](const std::string& name)->RelationValue {
        if(types.contains(name))return types.at(name);
        if(!assignments.contains(name))return inputs.at(name).value;
        std::optional<RelationValue> result;
        for(const auto* row:assignments.at(name)) {
            auto type=quantity_type(row->expression,lookup,degrees_per_angle_unit);
            if(dimension_name(name)) {
                const auto& target=inputs.at(name).value;static_cast<void>(number(type,*row->expression));
                type.units=type.units==std::array<int,3>{}?target.units:compatible(target,type,*row->expression);type.literal=false;
            }
            if(result&&(result->units!=type.units||result->data.index()!=type.data.index()||result->literal!=type.literal))
                fail(*row->expression,"Cannot safely convert relation units.",name);
            result=type;
            // A conditional parameter can fall back to its stored input value.
            // Different quantities there cannot be inferred from current values.
            if(!row->guards.empty()&&!dimension_name(name))if(const auto input=inputs.find(name);input!=inputs.end())
                if(input->second.value.units!=type.units||input->second.value.data.index()!=type.data.index())
                    fail(*row->expression,"Cannot safely convert relation units.",name);
        }
        return types[name]=*result;
    };
    for(const auto& [name,rows]:assignments)static_cast<void>(lookup(name));
    for(const auto& condition:data_->conditions)static_cast<void>(truth(quantity_type(condition,lookup,degrees_per_angle_unit),*condition));
    const auto factor_for=[&](const RelationValue& type,const Expr& location) {
        double scale=1;for(int i=0;i<3;++i)scale*=std::pow(factors[i],type.units[i]);
        if(!(scale>0)||!std::isfinite(scale))fail(location,"Cannot safely convert relation units.");return scale;
    };
    std::vector<std::size_t> line_offsets{0};
    for(std::size_t i=0;i<data_->source.size();++i)if(data_->source[i]=='\n')line_offsets.push_back(i+1);
    struct Edit {std::size_t offset{},length{};std::string text;int priority{};};std::vector<Edit> edits;
    std::set<std::pair<int,int>> visited;
    std::function<void(const Node&)> convert_names=[&](const Node& node) {
        if(!visited.emplace(node->line,node->column).second)return;
        if(node->op=="name") {
            const auto& name=std::get<std::string>(node->value.data);const double factor=fixed_unit_names.contains(name)?1.:factor_for(lookup(name),*node);
            if(factor!=1)edits.push_back({line_offsets.at(node->line-1)+node->column-1,name.size(),"("+name+" / "+conversion_number(factor)+")"});
        }
        if(factors[1]!=1&&node->op.starts_with("call:")) {
            const auto fn=node->op.substr(5);const auto line=line_offsets.at(node->line-1);
            const auto factor=conversion_number(factors[1]);
            if(fn=="sin"||fn=="cos"||fn=="tan"||fn=="sind"||fn=="cosd"||fn=="tand") {
                if(quantity_type(node->args.front(),lookup,degrees_per_angle_unit).units==std::array<int,3>{0,1,0}) {
                    // The surrounding converted expression still works in old
                    // numbers. Restore the new typed angle basis at the call.
                    edits.push_back({line+node->argument_begin,0,"("});
                    edits.push_back({line+node->call_close,0,") * "+factor});
                }
            } else if(fn=="asin"||fn=="acos"||fn=="atan"||fn=="asind"||fn=="acosd"||fn=="atand"||fn=="atan2"||fn=="atan2d") {
                // Inverse functions return a typed angle in the new document
                // unit. Recover old numbers before enclosing arithmetic.
                edits.push_back({line+node->column-1,0,"("});
                edits.push_back({line+node->call_end,0," / "+factor+")",1});
            }
        }
        for(const auto& arg:node->args)convert_names(arg);
    };
    RelationUnitConversion converted{data_->source,{}};
    for(const auto& condition:data_->conditions)convert_names(condition);
    for(const auto& a:data_->assignments) {
        convert_names(a.expression);const auto type=lookup(a.target);const double factor=fixed_unit_names.contains(a.target)?1.:factor_for(type,*a.expression);
        if(std::holds_alternative<double>(type.data))converted.outputs[a.target]={type.units,factor,a.expression->line};
        if(factor!=1) {
            const auto tokens=lex(a.source,a.expression->line);const auto start=line_offsets.at(a.expression->line-1);
            edits.push_back({start+tokens[2].column-1,0,"("});
            edits.push_back({start+tokens.back().end,0,") * "+conversion_number(factor)});
        }
    }
    std::sort(edits.begin(),edits.end(),[](const Edit& a,const Edit& b){return a.offset!=b.offset?a.offset>b.offset:a.length!=b.length?a.length>b.length:a.priority<b.priority;});
    for(const auto& edit:edits)converted.source.replace(edit.offset,edit.length,edit.text);
    // Conversion must produce a valid program within ordinary parser limits.
    RelationProgram(converted.source).validate(inputs);
    return converted;
}
std::map<std::string,std::string> RelationProgram::target_expressions() const {
    std::map<std::string,std::string> result;
    for(const auto& a:data_->assignments){if(!result[a.target].empty())result[a.target]+='\n';result[a.target]+=a.source;}
    return result;
}
std::string relation_value_text(const RelationValue& value,int decimals) {
    if(const auto p=std::get_if<std::string>(&value.data))return *p;
    if(const auto p=std::get_if<bool>(&value.data))return *p?"true":"false";
    std::ostringstream stream;stream.imbue(std::locale::classic());stream<<std::fixed<<std::setprecision(std::clamp(decimals,0,12))<<std::get<double>(value.data);
    auto text=stream.str();if(text.find('.')!=std::string::npos){while(text.back()=='0')text.pop_back();if(text.back()=='.')text.pop_back();}return text;
}
std::string quote_relation_text(const std::string& text) {
    std::string result="\"";
    for(char c:text){if(c=='"'||c=='\\'){result+='\\';result+=c;}else if(c=='\n')result+="\\n";else if(c=='\r')result+="\\r";else if(c=='\t')result+="\\t";else result+=c;}
    return result+'"';
}
} // namespace zima::document
