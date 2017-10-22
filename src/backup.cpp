// (C) unresolved-external@singu-lair.com released under the MIT license (see LICENSE)

#include "backup.h"

#include <Windows.h>

#include <algorithm>
#include <codecvt>
#include <ctime>
#include <regex>

std::wstring backup(const std::wstring& path)
{
    Sleep(3000);

    char tsbuffer[24];

    time_t t;
    time(&t);

    tm timeinfo;
    localtime_s(&timeinfo, &t);
    size_t count = strftime(tsbuffer, sizeof(tsbuffer), "%Y-%m-%d %H-%M-%S", &timeinfo);

    static std::wstring_convert<std::codecvt_utf8_utf16<wchar_t>, wchar_t> conv16;

    std::wstring backup_path = path + L"\\Auto Backup (" + conv16.from_bytes(tsbuffer) + L")\\";

    if (!CreateDirectoryW(backup_path.c_str(), nullptr) && GetLastError() != ERROR_ALREADY_EXISTS)
        return {};

    std::wstring pattern(path + L"\\gw???.???");
    std::vector<std::wstring> files;

    WIN32_FIND_DATAW find_data;
    HANDLE find = FindFirstFileW(pattern.c_str(), &find_data);
    if (find == INVALID_HANDLE_VALUE)
        return {};

    files.emplace_back(find_data.cFileName);

    while (FindNextFileW(find, &find_data) != 0)
        files.emplace_back(find_data.cFileName);

    FindClose(find);

    std::sort(files.begin(), files.end());

    for (const auto& file : files)
        MoveFileW((path + L"\\" + file).c_str(), (backup_path + L"\\" + file).c_str());

    return backup_path;
}
