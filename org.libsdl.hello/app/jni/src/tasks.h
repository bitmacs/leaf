#pragma once

#include <condition_variable>
#include <functional>
#include <mutex>
#include <queue>
#include <thread>

struct TaskSystem {
    std::thread worker_thread;
    std::queue<std::function<void()>> tasks;
    std::mutex tasks_mutex;
    std::condition_variable tasks_condition_variable;
    bool request_stop;
};

void start_task_system(TaskSystem *task_system);
void stop_task_system(TaskSystem *task_system);

void push_task(TaskSystem *task_system, std::function<void()> &&task);
