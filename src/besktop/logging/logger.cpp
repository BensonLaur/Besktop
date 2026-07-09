#include "besktop/logging/logger.h"

#include <windows.h>

#include <filesystem>
#include <fstream>
#include <iterator>
#include <mutex>
#include <string>

namespace {

std::mutex& LogMutex()
{
    static std::mutex mutex;
    return mutex;
}

std::wstring BuildLogFilePath()
{
    wchar_t localAppData[MAX_PATH]{};
    const DWORD length = GetEnvironmentVariableW(
        L"LOCALAPPDATA",
        localAppData,
        static_cast<DWORD>(std::size(localAppData)));

    if (length > 0 && length < std::size(localAppData)) {
        std::filesystem::path logDirectory(localAppData);
        logDirectory /= L"Besktop";
        logDirectory /= L"logs";
        std::error_code error;
        std::filesystem::create_directories(logDirectory, error);
        if (!error) {
            return (logDirectory / L"besktop.log").wstring();
        }
    }

    return L"besktop.log";
}

std::string WideToUtf8(const std::wstring& value)
{
    if (value.empty()) {
        return {};
    }

    const int size = WideCharToMultiByte(
        CP_UTF8,
        0,
        value.c_str(),
        static_cast<int>(value.size()),
        nullptr,
        0,
        nullptr,
        nullptr);
    if (size <= 0) {
        return {};
    }

    std::string result(static_cast<size_t>(size), '\0');
    WideCharToMultiByte(
        CP_UTF8,
        0,
        value.c_str(),
        static_cast<int>(value.size()),
        result.data(),
        size,
        nullptr,
        nullptr);
    return result;
}

const wchar_t* LevelName(besktop::LogLevel level)
{
    switch (level) {
    case besktop::LogLevel::Warning:
        return L"WARNING";
    case besktop::LogLevel::Error:
        return L"ERROR";
    case besktop::LogLevel::Info:
    default:
        return L"INFO";
    }
}

std::wstring Timestamp()
{
    SYSTEMTIME time{};
    GetLocalTime(&time);

    wchar_t buffer[32]{};
    swprintf_s(
        buffer,
        L"%04u-%02u-%02u %02u:%02u:%02u.%03u",
        time.wYear,
        time.wMonth,
        time.wDay,
        time.wHour,
        time.wMinute,
        time.wSecond,
        time.wMilliseconds);
    return buffer;
}

} // namespace

namespace besktop {

std::wstring GetLogFilePath()
{
    static const std::wstring path = BuildLogFilePath();
    return path;
}

void Log(LogLevel level, const std::wstring& message)
{
    try {
        const std::lock_guard<std::mutex> lock(LogMutex());
        std::ofstream stream(
            std::filesystem::path(GetLogFilePath()),
            std::ios::app | std::ios::binary);
        if (!stream) {
            return;
        }

        const std::wstring line =
            Timestamp() + L" [" + LevelName(level) + L"] " + message + L"\r\n";
        stream << WideToUtf8(line);
        stream.flush();
    } catch (...) {
        // Logging must never affect the desktop experience.
    }
}

void LogInfo(const std::wstring& message)
{
    Log(LogLevel::Info, message);
}

void LogWarning(const std::wstring& message)
{
    Log(LogLevel::Warning, message);
}

void LogError(const std::wstring& message)
{
    Log(LogLevel::Error, message);
}

} // namespace besktop
