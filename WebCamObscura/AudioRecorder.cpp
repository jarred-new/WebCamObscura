#include "AudioRecorder.h"

#include <Windows.h>
#include <mmdeviceapi.h>
#include <audioclient.h>
#include <avrt.h>
#include <mmreg.h>

#include <algorithm>
#include <chrono>
#include <cstring>
#include <iostream>
#include <vector>

#pragma comment(lib, "ole32.lib")
#pragma comment(lib, "avrt.lib")

namespace
{
    template<typename T>
    void SafeRelease(T*& ptr)
    {
        if (ptr)
        {
            ptr->Release();
            ptr = nullptr;
        }
    }

    void WriteLE16(std::ofstream& file, uint16_t value)
    {
        char bytes[2];

        bytes[0] = static_cast<char>(value & 0xFF);
        bytes[1] = static_cast<char>((value >> 8) & 0xFF);

        file.write(bytes, 2);
    }

    void WriteLE32(std::ofstream& file, uint32_t value)
    {
        char bytes[4];

        bytes[0] = static_cast<char>(value & 0xFF);
        bytes[1] = static_cast<char>((value >> 8) & 0xFF);
        bytes[2] = static_cast<char>((value >> 16) & 0xFF);
        bytes[3] = static_cast<char>((value >> 24) & 0xFF);

        file.write(bytes, 4);
    }
}

AudioRecorder::AudioRecorder()
{
}

AudioRecorder::~AudioRecorder()
{
    Stop();
}

bool AudioRecorder::Start(int micDeviceId, const std::string& filename)
{
    if (m_recording.load())
        return false;

    if (m_thread.joinable())
        m_thread.join();

    m_totalSamples = 0;
    m_recording = true;

    m_thread = std::thread(
        &AudioRecorder::RecordingThread,
        this,
        micDeviceId,
        filename
    );

    return true;
}

void AudioRecorder::Stop()
{
    m_recording = false;

    if (m_thread.joinable())
        m_thread.join();
}

bool AudioRecorder::IsRecording() const
{
    return m_recording.load();
}

double AudioRecorder::GetRecordingTime() const
{
    const uint64_t samples = m_totalSamples.load();

    if (m_sampleRate <= 0)
        return 0.0;

    return static_cast<double>(samples) /
        static_cast<double>(m_sampleRate);
}

bool AudioRecorder::WriteWavHeader(std::ofstream& file)
{
    if (!file)
        return false;

    file.write("RIFF", 4);

    // Placeholder RIFF size.
    WriteLE32(file, 0);

    file.write("WAVE", 4);

    file.write("fmt ", 4);

    WriteLE32(file, 16); // PCM fmt chunk size

    WriteLE16(file, 1);  // PCM

    WriteLE16(file, static_cast<uint16_t>(m_channels));

    WriteLE32(file, static_cast<uint32_t>(m_sampleRate));

    const uint32_t byteRate =
        m_sampleRate *
        m_channels *
        (m_bitsPerSample / 8);

    WriteLE32(file, byteRate);

    const uint16_t blockAlign =
        static_cast<uint16_t>(
            m_channels * (m_bitsPerSample / 8)
            );

    WriteLE16(file, blockAlign);

    WriteLE16(file, static_cast<uint16_t>(m_bitsPerSample));

    file.write("data", 4);

    // Placeholder data size.
    WriteLE32(file, 0);

    return true;
}

void AudioRecorder::FinalizeWav(
    std::ofstream& file,
    uint32_t dataSize)
{
    const std::streampos endPosition = file.tellp();

    // RIFF chunk size.
    file.seekp(4, std::ios::beg);

    WriteLE32(
        file,
        36 + dataSize
    );

    // data chunk size.
    file.seekp(40, std::ios::beg);

    WriteLE32(
        file,
        dataSize
    );

    file.seekp(endPosition);

    file.flush();
}

void AudioRecorder::RecordingThread(
    int micDeviceId,
    std::string filename)
{
    HRESULT hr = CoInitializeEx(
        nullptr,
        COINIT_MULTITHREADED
    );

    if (FAILED(hr))
    {
        m_recording = false;
        return;
    }

    IMMDeviceEnumerator* enumerator = nullptr;
    IMMDeviceCollection* collection = nullptr;
    IMMDevice* device = nullptr;
    IAudioClient* audioClient = nullptr;
    IAudioCaptureClient* captureClient = nullptr;

    do
    {
        hr = CoCreateInstance(
            __uuidof(MMDeviceEnumerator),
            nullptr,
            CLSCTX_ALL,
            __uuidof(IMMDeviceEnumerator),
            reinterpret_cast<void**>(&enumerator)
        );

        if (FAILED(hr))
            break;

        hr = enumerator->EnumAudioEndpoints(
            eCapture,
            DEVICE_STATE_ACTIVE,
            &collection
        );

        if (FAILED(hr))
            break;

        UINT deviceCount = 0;

        hr = collection->GetCount(&deviceCount);

        if (FAILED(hr))
            break;

        if (micDeviceId < 0 ||
            static_cast<UINT>(micDeviceId) >= deviceCount)
        {
            break;
        }

        hr = collection->Item(
            static_cast<UINT>(micDeviceId),
            &device
        );

        if (FAILED(hr))
            break;

        hr = device->Activate(
            __uuidof(IAudioClient),
            CLSCTX_ALL,
            nullptr,
            reinterpret_cast<void**>(&audioClient)
        );

        if (FAILED(hr))
            break;

        WAVEFORMATEX* mixFormat = nullptr;

        hr = audioClient->GetMixFormat(&mixFormat);

        if (FAILED(hr))
            break;

        // Get device parameters. We will write 16-bit PCM WAV.
        // If the device provides float samples we convert them to
        // 16-bit PCM on the fly.
        m_sampleRate = mixFormat->nSamplesPerSec;
        m_channels = mixFormat->nChannels;
        m_bitsPerSample = mixFormat->wBitsPerSample;

        // Decide whether we need to convert float -> 16-bit PCM.
        // Commonly devices provide IEEE float (32-bit). Treat any
        // non-PCM 32-bit format as float for conversion purposes.
        m_convertFloatTo16 = (mixFormat->wFormatTag == WAVE_FORMAT_IEEE_FLOAT) ||
            (mixFormat->wBitsPerSample == 32 && mixFormat->wFormatTag != WAVE_FORMAT_PCM);

        // We always write 16-bit PCM to file for compatibility.
        if (m_convertFloatTo16) {
            m_bitsPerSample = 16;
        }

        // Remember the device's original bits per sample so we can
        // compute incoming buffer sizes after freeing the mixFormat.
        const int deviceBitsPerSample = mixFormat->wBitsPerSample;

        const REFERENCE_TIME bufferDuration =
            10000000; // 1 second

        hr = audioClient->Initialize(
            AUDCLNT_SHAREMODE_SHARED,
            0,
            bufferDuration,
            0,
            mixFormat,
            nullptr
        );

        CoTaskMemFree(mixFormat);

        if (FAILED(hr))
            break;

        hr = audioClient->GetService(
            __uuidof(IAudioCaptureClient),
            reinterpret_cast<void**>(&captureClient)
        );

        if (FAILED(hr))
            break;

        std::ofstream wav(
            filename,
            std::ios::binary
        );

        if (!wav)
            break;

        /*
         * For this implementation we assume PCM.
         *
         * If the device gives IEEE float or another format,
         * you should convert it before writing a normal PCM WAV.
         */

        if (!WriteWavHeader(wav))
            break;

        hr = audioClient->Start();

        if (FAILED(hr))
            break;

        uint32_t totalDataBytes = 0;

        while (m_recording.load())
        {
            UINT32 packetLength = 0;

            hr = captureClient->GetNextPacketSize(
                &packetLength
            );

            if (FAILED(hr))
                break;

            while (packetLength > 0)
            {
                BYTE* data = nullptr;

                UINT32 numFrames = 0;

                DWORD flags = 0;

                hr = captureClient->GetBuffer(
                    &data,
                    &numFrames,
                    &flags,
                    nullptr,
                    nullptr
                );

                if (FAILED(hr))
                    break;

                const uint32_t deviceBytesPerSample =
                    static_cast<uint32_t>(m_channels * (deviceBitsPerSample / 8));

                // Number of bytes in the capture buffer as provided by the device
                const uint32_t deviceByteCount = numFrames * deviceBytesPerSample;

                if (!(flags & AUDCLNT_BUFFERFLAGS_SILENT))
                {
                    if (m_convertFloatTo16) {
                        // Convert float32 samples to int16 PCM.
                        const float* src = reinterpret_cast<const float*>(data);
                        const size_t sampleCount = static_cast<size_t>(numFrames) * static_cast<size_t>(m_channels);
                        std::vector<int16_t> conv;
                        conv.reserve(sampleCount);
                        for (size_t i = 0; i < sampleCount; ++i) {
                            float s = src[i];
                            // Clamp and scale
                            if (s > 1.0f) s = 1.0f;
                            if (s < -1.0f) s = -1.0f;
                            conv.push_back(static_cast<int16_t>(s * 32767.0f));
                        }

                        const uint32_t outBytes = static_cast<uint32_t>(conv.size() * sizeof(int16_t));
                        wav.write(reinterpret_cast<const char*>(conv.data()), outBytes);
                        totalDataBytes += outBytes;
                    }
                    else {
                        wav.write(reinterpret_cast<const char*>(data), deviceByteCount);
                        totalDataBytes += deviceByteCount;
                    }
                }
                else
                {
                    // Silent buffer - write zeros matching output sample size.
                    const uint32_t silenceBytes = m_channels * (m_bitsPerSample / 8) * numFrames;
                    std::vector<char> silence(silenceBytes);
                    wav.write(silence.data(), silence.size());
                    totalDataBytes += static_cast<uint32_t>(silence.size());
                }

                // Update sample count (frame count)
                m_totalSamples += numFrames;

                captureClient->ReleaseBuffer(numFrames);

                hr = captureClient->GetNextPacketSize(
                    &packetLength
                );

                if (FAILED(hr))
                    break;
            }

            std::this_thread::sleep_for(
                std::chrono::milliseconds(5)
            );
        }

        audioClient->Stop();

        FinalizeWav(
            wav,
            totalDataBytes
        );

    } while (false);

    SafeRelease(captureClient);
    SafeRelease(audioClient);
    SafeRelease(device);
    SafeRelease(collection);
    SafeRelease(enumerator);

    CoUninitialize();

    m_recording = false;
}