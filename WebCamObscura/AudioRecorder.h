#pragma once

#include <atomic>
#include <cstdint>
#include <fstream>
#include <mutex>
#include <string>
#include <thread>

class AudioRecorder
{
public:
    AudioRecorder();
    ~AudioRecorder();

    AudioRecorder(const AudioRecorder&) = delete;
    AudioRecorder& operator=(const AudioRecorder&) = delete;

    bool Start(int micDeviceId, const std::string& filename);
    void Stop();

    bool IsRecording() const;
    double GetRecordingTime() const;

private:
    void RecordingThread(int micDeviceId, std::string filename);

    bool WriteWavHeader(std::ofstream& file);
    void FinalizeWav(std::ofstream& file, uint32_t dataSize);

private:
    std::atomic<bool> m_recording{ false };

    std::thread m_thread;

    std::atomic<uint64_t> m_totalSamples{ 0 };

    int m_sampleRate = 48000;
    int m_channels = 1;
    int m_bitsPerSample = 16;

    std::mutex m_mutex;
};