#pragma once
#include <zima/ui/operation_activity.hpp>
#include <QApplication>
#include <QEventLoop>
#include <chrono>
#include <future>
#include <thread>
#include <type_traits>
#include <utility>

namespace zima::ui {
// Call on the UI thread. The worker must own its inputs and must not access
// widgets. Keep paints/timers alive while blocking competing user commands.
template <typename Function>
auto run_background_task(Function&& function) {
    OperationActivity::InputBlock input_block;
    using Result = std::invoke_result_t<std::decay_t<Function>>;
    auto future = std::async(std::launch::async, std::forward<Function>(function));
    using namespace std::chrono_literals;
    while (future.wait_for(0ms) != std::future_status::ready) {
        QApplication::processEvents(QEventLoop::AllEvents, 16);
        std::this_thread::sleep_for(4ms);
    }
    QApplication::processEvents(QEventLoop::ExcludeUserInputEvents, 1);
    if constexpr (std::is_void_v<Result>) future.get();
    else return future.get();
}
} // namespace zima::ui
