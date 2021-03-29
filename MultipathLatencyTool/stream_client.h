#pragma once

#include <Windows.h>
#include <winrt/Windows.Networking.Connectivity.h>
#include <wlanapi.h>

#include <wil/resource.h>

#include <array>
#include <atomic>
#include <fstream>
#include <memory>
#include <optional>
#include <vector>

#include "latencyStatistics.h"
#include "sockaddr.h"
#include "threadpool_io.h"
#include "threadpool_timer.h"

using namespace winrt;
using namespace Windows::Networking::Connectivity;

namespace multipath {

class StreamClient
{
public:
    StreamClient(ctl::ctSockaddr targetAddress, unsigned long receiveBufferCount, HANDLE completeEvent);

    void RequestSecondaryWlanConnection();

    void Start(unsigned long sendBitRate, unsigned long sendFrameRate, unsigned long duration);
    void Stop();

    void PrintStatistics();
    void DumpLatencyData(std::ofstream& file);

    // not copyable or movable
    StreamClient(const StreamClient&) = delete;
    StreamClient& operator=(const StreamClient&) = delete;
    StreamClient(StreamClient&&) = delete;
    StreamClient& operator=(StreamClient&&) = delete;

    ~StreamClient() = default;

private:
    NetworkInformation::NetworkStatusChanged_revoker m_networkInformationEventRevoker{};
    // The client must keep this handle open to keep the secondary STA port active
    wil::unique_wlan_handle m_wlanHandle;

    enum class Interface
    {
        Primary,
        Secondary
    };

    static constexpr size_t c_sendBufferSize = 1024; // 1KB send buffer
    using SendBuffer = std::array<char, c_sendBufferSize>;

    struct SendState
    {
        long long m_sequenceNumber;
        long long m_sendTimestamp;
    };

    static constexpr size_t c_receiveBufferSize = 1024; // 1KB receive buffer
    struct ReceiveState
    {
        std::array<char, c_receiveBufferSize> m_buffer{};
        long long m_receiveTimestamp{};
    };

    enum class AdapterStatus
    {
        Disabled,
        Connecting,
        Ready
    };

    struct SocketState
    {
        SocketState(Interface interface);
        ~SocketState() noexcept;

        void Setup(const ctl::ctSockaddr& targetAddress, int numReceivedBuffers, int interfaceIndex = 0);
        void Cancel();

        bool DoServerHandshake();

        wil::critical_section m_lock{500};
        wil::unique_socket m_socket;
        std::unique_ptr<ctl::ctThreadIocp> m_threadpoolIo;

        // whether the this socket is the primary or secondary
        const Interface m_interface;

        // the contexts used for each posted receive
        std::vector<ReceiveState> m_receiveStates;
        long long m_corruptFrames = 0;
        std::atomic<AdapterStatus> m_adapterStatus{AdapterStatus::Disabled};

        // All interfaces are sending the same data, stored in a shared buffer
        static constexpr const SendBuffer s_sharedSendBuffer = []() {
            // initialize the send buffer
            SendBuffer sharedSendBuffer{};
            for (size_t i = 0; i < sharedSendBuffer.size(); ++i)
            {
                sharedSendBuffer[i] = static_cast<char>(i);
            }
            return sharedSendBuffer;
        }();
    };

    void SetupSecondaryInterface();

    void TimerCallback() noexcept;

    void SendDatagrams() noexcept;
    void SendDatagram(SocketState& socketState) noexcept;
    void SendCompletion(SocketState& socketState, const SendState& sendState) noexcept;

    void InitiateReceive(SocketState& socketState, ReceiveState& receiveState);
    void ReceiveCompletion(SocketState& socketState, ReceiveState& receiveState, DWORD messageSize) noexcept;

    ctl::ctSockaddr m_targetAddress{};

    SocketState m_primaryState{Interface::Primary};
    SocketState m_secondaryState{Interface::Secondary};

    // The number of datagrams to send on each timer callback
    long long m_frameRate = 0;
    unsigned long m_receiveBufferCount = 1;

    FILETIME m_tickInterval{};
    // Initialize to -1 as the first datagram has sequence number 0
    long long m_finalSequenceNumber = -1;
    std::unique_ptr<ThreadpoolTimer> m_threadpoolTimer{};
    std::atomic<bool> m_running = false;

    long long m_sequenceNumber = 0;

    std::vector<LatencyData> m_latencyData;

    HANDLE m_completeEvent = nullptr;
};
} // namespace multipath