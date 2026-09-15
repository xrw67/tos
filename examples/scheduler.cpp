#include "tos/base/scheduler.h"

#include <future>
#include <iostream>
#include <utility>

#include "tos/base/thread_pool.h"

int main() {
    tos::ThreadPool pool(1, 4);
    tos::Scheduler scheduler(pool);
    std::promise<const char*> completed;
    std::future<const char*> future = completed.get_future();
    auto scheduled = scheduler.ScheduleAfter(
        tos::Millisecond, [&completed] { completed.set_value("scheduler ready"); });
    if (!scheduled) {
        return 1;
    }
    std::cout << future.get() << '\n';
    return scheduler.Shutdown() && pool.Shutdown() ? 0 : 1;
}
