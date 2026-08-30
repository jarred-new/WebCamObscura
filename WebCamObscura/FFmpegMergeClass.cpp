#include "FFmpegMergeClass.h"

#include <vector>

FFmpegMergeClass::FFmpegMergeClass()
{
}

FFmpegMergeClass::~FFmpegMergeClass()
{
}

bool FFmpegMergeClass::MergeAudioVideo(const std::string& videoFile, const std::string& audioFile, const std::string& outputFile)
{
    const std::wstring ffmpegPath = GetFFmpegPath();
    const std::wstring videoPath(videoFile.begin(), videoFile.end());
    const std::wstring audioPath(audioFile.begin(), audioFile.end());
    const std::wstring outputPath(outputFile.begin(), outputFile.end());
    const std::wstring command =
        L"\"" + ffmpegPath + L"\" -y -i \"" + videoPath +
        L"\" -i \"" + audioPath + L"\" -c copy \"" + outputPath + L"\"";

    std::vector<wchar_t> commandLine(command.begin(), command.end());
    commandLine.push_back(L'\0');

    STARTUPINFOW startupInfo{};
    startupInfo.cb = sizeof(startupInfo);
    PROCESS_INFORMATION processInfo{};

    if (!CreateProcessW(
        nullptr,
        commandLine.data(),
        nullptr,
        nullptr,
        FALSE,
        CREATE_NO_WINDOW,
        nullptr,
        nullptr,
        &startupInfo,
        &processInfo)) {
        return false;
    }

    std::thread([
        processInfo,
        videoPath,
        audioPath
    ]() mutable {
        WaitForSingleObject(processInfo.hProcess, INFINITE);
        DWORD exitCode = 1;
        GetExitCodeProcess(processInfo.hProcess, &exitCode);
        CloseHandle(processInfo.hProcess);
        CloseHandle(processInfo.hThread);
        if (exitCode == 0) {
            DeleteFileW(videoPath.c_str());
            DeleteFileW(audioPath.c_str());
        }
    }).detach();

    return true;
}

std::wstring FFmpegMergeClass::GetApplicationPath()
{
    wchar_t buffer[MAX_PATH];

    // Get the full path of the executable
    DWORD length = GetModuleFileNameW(NULL, buffer, MAX_PATH);

    if (length == 0 || length == MAX_PATH) {
        // Handle error or overflow if path is longer than MAX_PATH
        return L"";
    }

    std::wstring fullPath(buffer, length);

    // Find the last trailing backslash to remove the file name
    size_t lastBackslash = fullPath.find_last_of(L"\\/");
    if (lastBackslash != std::wstring::npos) {
        return fullPath.substr(0, lastBackslash);
    }

    return fullPath;
}

std::wstring FFmpegMergeClass::GetFFmpegPath()
{
    return this->GetApplicationPath() + L"\\ffmpeg\\bin\\ffmpeg.exe";
}
