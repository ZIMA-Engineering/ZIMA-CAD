#pragma once

#include <QObject>
#include <QPointer>
#include <QString>
#include <cstdint>
#include <functional>
#include <vector>

class QWidget;
class QTimer;

namespace zima::ui {

// Presentation of explicit work; this object never calculates or commits a model.
class OperationActivity final : public QObject {
public:
    explicit OperationActivity(QWidget* window, QWidget* view);
    ~OperationActivity() override;
    using Token = std::uint64_t;
    [[nodiscard]] Token begin(const QString& message, QWidget* confirmation_indicator = nullptr);
    bool update_message(Token token, const QString& message);
    void end(Token token);
    [[nodiscard]] bool active() const { return !frames_.empty(); }
    [[nodiscard]] QWidget* overlay() const;
    void set_view(QWidget* view);
    [[nodiscard]] static OperationActivity* find(QWidget* child);
    [[nodiscard]] static QWidget* create_confirmation_indicator(QWidget* parent);
    std::function<void()> started;
    std::function<void()> finished;

    class Scope final {
    public:
        Scope(OperationActivity* activity, const QString& message, QWidget* indicator = nullptr);
        ~Scope();
        Scope(const Scope&) = delete;
        Scope& operator=(const Scope&) = delete;
    private:
        QPointer<OperationActivity> activity_;
        Token token_{};
    };
    // Block competing input only while a background task is actually running;
    // validation prompts in an OK handler must remain interactive.
    class InputBlock final {
    public:
        InputBlock();
        ~InputBlock();
        InputBlock(const InputBlock&) = delete;
        InputBlock& operator=(const InputBlock&) = delete;
    };
protected:
    bool eventFilter(QObject* watched, QEvent* event) override;

private:
    struct Frame { Token token; QString message; QPointer<QWidget> indicator; };
    void refresh();
    void place_overlay();
    QPointer<QWidget> window_;
    QPointer<QWidget> view_;
    QWidget* overlay_{};
    QTimer* appearance_timer_{};
    QTimer* animation_timer_{};
    std::vector<Frame> frames_;
    Token next_token_{};
    int angle_{};
};

} // namespace zima::ui
