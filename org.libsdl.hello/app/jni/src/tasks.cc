#include "tasks.h"

void start_task_system(TaskSystem *task_system) {
    // start a thread to listen to the task queue and execute the tasks
    task_system->worker_thread = std::thread([task_system]() {
        while (true) {
            std::function<void()> task;
            {
                // wait for a task to be available
               std::unique_lock<std::mutex> lock(task_system->tasks_mutex);
               task_system->tasks_condition_variable.wait(lock, [task_system]() {
                   return task_system->request_stop || !task_system->tasks.empty();
               });
               if (task_system->request_stop && task_system->tasks.empty()) {
                   break; // all tasks are executed
               }
               task = task_system->tasks.front();
               task_system->tasks.pop();
            }
            task(); // execute the task
        }
    });
}

void stop_task_system(TaskSystem *task_system) {
    // wait all tasks to be executed
    {
        std::lock_guard<std::mutex> lock(task_system->tasks_mutex);
        if (task_system->request_stop) {
            return;
        }
        task_system->request_stop = true;
    }
    task_system->tasks_condition_variable.notify_all();
    if (task_system->worker_thread.joinable()) {
        task_system->worker_thread.join(); // wait for the worker thread to finish
    }
}

void push_task(TaskSystem *task_system, std::function<void()> &&task) {
    {
        std::lock_guard<std::mutex> lock(task_system->tasks_mutex);
        if (task_system->request_stop) {
            return;
        }
        task_system->tasks.emplace(std::move(task));
    }
    task_system->tasks_condition_variable.notify_one();
}
