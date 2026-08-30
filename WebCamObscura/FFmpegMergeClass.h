#pragma once

#include <windows.h>
#include <cstdio>
#include <string>
#include <cstdlib>
#include <thread>

class FFmpegMergeClass
{
public:
	FFmpegMergeClass();
	~FFmpegMergeClass();
	FFmpegMergeClass(const FFmpegMergeClass&) = delete;
	FFmpegMergeClass& operator=(const FFmpegMergeClass&) = delete;
	bool MergeAudioVideo(
		const std::string& videoFile,
		const std::string& audioFile,
		const std::string& outputFile,
		const bool showFFmpegConsole
	);

private:
	std::wstring GetApplicationPath();
	std::wstring GetFFmpegPath();
};

