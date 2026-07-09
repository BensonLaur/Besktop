#pragma once

#include <string>

namespace besktop {

enum class LogLevel {
    Info,
    Warning,
    Error,
};

void Log(LogLevel level, const std::wstring& message);
void LogInfo(const std::wstring& message);
void LogWarning(const std::wstring& message);
void LogError(const std::wstring& message);

std::wstring GetLogFilePath();

} // namespace besktop
