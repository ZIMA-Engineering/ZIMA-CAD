#pragma once
#include <zima/commands/dispatcher.hpp>
#include <zima/workspace/native_documents.hpp>
#include <zima/workspace/model_calculation.hpp>
#include <functional>
#include <optional>

namespace zima::command_host {
using commands::Json;
using commands::Result;
struct Settings {
    workspace::NativeTemplateSettings templates;
    std::map<std::string,std::string> units;
};
struct Interaction {
    bool editing{};
    bool template_document{};
    std::string active_occurrence, active_sketch;
    Json selection=nullptr, hover=nullptr, camera=nullptr;
    Json pointer={{"inside_view",false}};
};
enum class ChangeKind { Open, New, Save, Regenerate, History, Model, Activate, Close, Copy, Directory, Metadata };
struct Change { ChangeKind kind; std::string document_id; bool clear_selection{}; };
enum class Activity { Read, Write, Export };
struct Options {
    std::function<Settings()> settings;
    std::function<Interaction()> interaction;
    std::function<std::string(const char*)> translate;
    // Must finish (and propagate failures) before returning. Task captures no Workspace.
    // A GUI can wait with its event loop; a command-line host executes directly.
    std::function<void(std::function<void()>)> run_io;
    std::function<void(Activity,const std::filesystem::path&)> progress;
    std::function<void()> fit;
};
[[nodiscard]] Json documents(const workspace::Workspace& workspace);
[[nodiscard]] Json model_tree(const workspace::Workspace& workspace,
    const std::string& document_id, std::size_t limit=2000);

// Synchronous, Workspace-owner-thread command execution. No Qt or window callbacks
// implement model operations. Optional adapters supply interaction, I/O scheduling
// and view-only actions. Changes tell the caller which presentation to refresh.
class Host {
public:
    Host(workspace::Workspace&,const kernel::OcctKernel&,std::filesystem::path& working_directory,Options={});
    Host(const Host&)=delete;
    Host& operator=(const Host&)=delete;
    [[nodiscard]] Result execute_text(std::string_view text);
    [[nodiscard]] Result execute(const Json& request);
    [[nodiscard]] const std::optional<Change>& change() const { return change_; }
private:
    workspace::Workspace& workspace_;
    const kernel::OcctKernel& kernel_;
    std::filesystem::path& directory_;
    Options options_;
    commands::Dispatcher dispatcher_;
    bool executing_{};
    std::optional<Change> change_;
    void register_commands();
    void register_primitive_commands();
    void register_document_commands();
    void register_body_commands();
    void register_history_commands();
    void register_reference_commands();
    void register_sketch_commands();
    void register_sketch_curve_commands();
    void register_sketch_relation_commands();
    void register_sketch_dimension_commands();
    void register_sketch_reference_commands();
    void register_sketch_text_commands();
    void register_sketch_spline_commands();
    void register_import_commands();
    void register_export_commands();
    void register_metadata_commands();
    void register_engineering_metadata_commands();
    void register_component_commands();
    void add_sketch_query(commands::Command,std::function<Json(const sketcher::Sketch&,const Json&)>);
    void add_sketch_command(commands::Command,std::function<Json(sketcher::Sketch&,const Json&)>);
    [[nodiscard]] Interaction interaction() const;
    [[nodiscard]] std::string tr(const char*) const;
    [[nodiscard]] Result target(const Json&) const;
    [[nodiscard]] Result run(const std::function<Result()>&);
    void io(std::function<void()> task) const;
    void activate(const std::string& id);
};
} // namespace zima::command_host
