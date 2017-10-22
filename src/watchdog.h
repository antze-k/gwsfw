// (C) unresolved-external@singu-lair.com released under the MIT license (see LICENSE)

#pragma once

#include <atomic>
#include <mutex>
#include <string>
#include <thread>

class watchdog_t
{
public:
    watchdog_t();
    ~watchdog_t();

    bool start(const std::wstring& screenshots_folder);
    void stop();

private:
    void run();
    bool process_dir();
    void process_file(const std::wstring& filename, bool exists);
    void process_mark(int index, bool mark);
    void process_changes();

    void update_user_data();

public:
    struct user_data_t
    {
        int screenshots = 0;
        double percentage = 0.0;
        bool full = false;
    };

    user_data_t user_data;
    std::atomic_bool user_data_changed = false;
    std::mutex user_data_mutex;

private:
    std::mutex m_control_mutex;
    std::wstring m_active_folder;
    std::atomic_bool m_should_run = false;
    std::atomic_bool m_running = false;
    std::thread* m_thread = nullptr;

    struct internal_t;
    internal_t* m_in = nullptr;
};