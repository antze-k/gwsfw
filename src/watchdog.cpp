// (C) unresolved-external@singu-lair.com released under the MIT license (see LICENSE)

#include "watchdog.h"

#include <Windows.h>

#include <codecvt>
#include <regex>

struct watchdog_t::internal_t
{
    watchdog_t* watchdog;
    HANDLE dir = INVALID_HANDLE_VALUE;
    std::atomic_bool reading = false;

    static const int buffer_size = 65536;

    OVERLAPPED ol = {0};
    char buffer[buffer_size];
    unsigned long bytes = 0;
    char buffer_copy[buffer_size];

    static const int screenshots_max = 999;
    bool screenshots[screenshots_max] = {0};
    int screenshots_count = 0;

    inline internal_t(watchdog_t* w) : watchdog(w) { ol.hEvent = this; }

    static void CALLBACK completion_routine(DWORD dwErrorCode, DWORD dwNumberOfBytesTransfered, LPOVERLAPPED lpOverlapped);

    bool io_open(const std::wstring& folder)
    {
        if (dir != INVALID_HANDLE_VALUE)
            return true;

        dir = CreateFileW(folder.c_str(), FILE_LIST_DIRECTORY,
                FILE_SHARE_DELETE | FILE_SHARE_READ | FILE_SHARE_WRITE, nullptr, OPEN_EXISTING, FILE_FLAG_BACKUP_SEMANTICS | FILE_FLAG_OVERLAPPED, nullptr);
        return dir != INVALID_HANDLE_VALUE;
    }

    void io_cancel()
    {
        if (dir == INVALID_HANDLE_VALUE)
            return;

        CancelIo(dir);
        CloseHandle(dir);
        dir = INVALID_HANDLE_VALUE;
    }

    void begin_read()
    {
        static const DWORD dwNotificationFlags = FILE_NOTIFY_CHANGE_LAST_WRITE | FILE_NOTIFY_CHANGE_CREATION | FILE_NOTIFY_CHANGE_FILE_NAME;
        reading.store(dir != INVALID_HANDLE_VALUE && 0 != ReadDirectoryChangesW(dir, buffer, buffer_size, FALSE, dwNotificationFlags, &bytes, &ol, &completion_routine));
    }
};


void CALLBACK watchdog_t::internal_t::completion_routine(DWORD dwErrorCode, DWORD dwNumberOfBytesTransfered, LPOVERLAPPED lpOverlapped)
{
    internal_t* self = reinterpret_cast<internal_t*>(lpOverlapped->hEvent);

    if (dwErrorCode == ERROR_OPERATION_ABORTED)
    {
        self->reading.store(false);
        return;
    }

    if (dwNumberOfBytesTransfered >= offsetof(FILE_NOTIFY_INFORMATION, FileName))
    {
        memcpy(self->buffer_copy, self->buffer, dwNumberOfBytesTransfered);
        self->begin_read();
        self->watchdog->process_changes();
        return;
    }

    self->begin_read();
}


watchdog_t::watchdog_t() : m_in(new internal_t(this)) {}
watchdog_t::~watchdog_t() { stop(); delete m_in; }


bool watchdog_t::start(const std::wstring& screenshots_folder)
{
    std::lock_guard<std::mutex> here(m_control_mutex);

    if (m_thread) return true;  // already started
    m_should_run.store(true);
    m_active_folder = screenshots_folder;
    if ((m_thread = new std::thread(std::bind(&watchdog_t::run, this))) == nullptr) { m_should_run.store(false);  m_active_folder.clear(); return false; }
    m_thread->detach();
    return true;
}


void watchdog_t::stop()
{
    std::lock_guard<std::mutex> here(m_control_mutex);

    if (!m_thread) return;  // not running
    
    m_should_run.store(false);
    while (m_running.load()) { std::this_thread::sleep_for(std::chrono::milliseconds(1)); }  // wait for the thread to quit

    delete m_thread;
    m_thread = nullptr;
}


void watchdog_t::run()
{
    m_running.store(true);

    if (m_in->io_open(m_active_folder) && process_dir())
    {
        process_dir();
        m_in->begin_read();
        while (m_should_run.load() && m_in->reading.load())
            SleepEx(1, TRUE);  // alertable state needed
    }

    m_in->io_cancel();
    while (m_in->reading.load())
        SleepEx(1, TRUE);  // alertable state needed

    m_running.store(false);
}


bool watchdog_t::process_dir()
{
    std::wstring pattern(m_active_folder + L"\\gw???.???");

    WIN32_FIND_DATAW find_data;
    HANDLE find = FindFirstFileW(pattern.c_str(), &find_data);
    if (find == INVALID_HANDLE_VALUE && GetLastError() != ERROR_FILE_NOT_FOUND)
        return false;

    if (find != INVALID_HANDLE_VALUE)
    {
        process_file(find_data.cFileName, true);

        while (FindNextFileW(find, &find_data) != 0)
            process_file(find_data.cFileName, true);

        update_user_data();

        if (GetLastError() != ERROR_NO_MORE_FILES)
        {
            FindClose(find);
            return false;
        }
    }
    else
    {
        update_user_data();
    }

    FindClose(find);
    return true;
}


void watchdog_t::process_file(const std::wstring& filename, bool exists)
{
    static std::wstring_convert<std::codecvt_utf8_utf16<wchar_t>, wchar_t> conv16;
    static std::regex regex_valid_filename("gw(\\d\\d\\d)\\.(bmp|jpg)", std::regex_constants::ECMAScript);

    std::string utf8_filename = conv16.to_bytes(filename);

    std::smatch m;
    if (std::regex_match(utf8_filename, m, regex_valid_filename))
        process_mark(std::stoi(m.str(1))-1, exists);
}


void watchdog_t::process_mark(int index, bool mark)
{
    if (index < 0 || index >= internal_t::screenshots_max)
        return;

    m_in->screenshots_count += (mark?1:0)-(m_in->screenshots[index]?1:0);
    m_in->screenshots[index] = mark;
}


void watchdog_t::process_changes()
{
    BYTE* pBase = (BYTE*)m_in->buffer_copy;

    for (;;)
    {
        FILE_NOTIFY_INFORMATION& fni = (FILE_NOTIFY_INFORMATION&)*pBase;

        process_file(std::wstring(fni.FileName, fni.FileNameLength/sizeof(wchar_t)),
            fni.Action == FILE_ACTION_ADDED || fni.Action == FILE_ACTION_MODIFIED || fni.Action == FILE_ACTION_RENAMED_NEW_NAME);

        if (!fni.NextEntryOffset)
            break;

        pBase += fni.NextEntryOffset;
    };

    update_user_data();
}


void watchdog_t::update_user_data()
{
    user_data_mutex.lock();
    user_data_changed.store(true);
    user_data.screenshots = m_in->screenshots_count;
    user_data.percentage = double(m_in->screenshots_count) * 100.0 / double(internal_t::screenshots_max);
    user_data.full = (m_in->screenshots_count == internal_t::screenshots_max);
    user_data_mutex.unlock();
}
