#include <chrono>
#include <cstdint>
#include <iostream>
#include <mutex>
#include <optional>
#include <string>
#include <vector>
#include <3rd/asio.hpp>
#include "sntp.h"

// NTP时间戳转换
class NTPTimestamp {
public:
    static constexpr uint64_t EPOCH_OFFSET = 2208988800ULL; // 1900-1970的秒数

    static std::chrono::system_clock::time_point fromNTP(uint64_t ntpTime) {
        uint32_t seconds = (ntpTime >> 32) & 0xFFFFFFFF;
        uint32_t fraction = ntpTime & 0xFFFFFFFF;

        auto unixSeconds = seconds - EPOCH_OFFSET;
        auto microseconds = (fraction * 1000000ULL) >> 32;

        return std::chrono::system_clock::time_point(
            std::chrono::seconds(unixSeconds) +
            std::chrono::microseconds(microseconds)
        );
    }

    static uint64_t toNTP(std::chrono::system_clock::time_point tp) {
        auto unixTime = tp.time_since_epoch();
        auto seconds_since_epoch = std::chrono::duration_cast<std::chrono::seconds>(unixTime);
        auto micro_since_epoch = std::chrono::duration_cast<std::chrono::microseconds>(unixTime);

        uint64_t ntp_seconds = seconds_since_epoch.count() + EPOCH_OFFSET;
        uint64_t micro_part = micro_since_epoch.count() % 1000000;

        uint64_t ntp_fraction = (micro_part << 32) / 1000000;

        return (ntp_seconds << 32) | ntp_fraction;
    }
};

// 简化的NTP客户端
class SimpleNTPClient {
private:
    // NTP数据包结构 (48字节)
#pragma pack(push, 1) // 确保紧凑对齐
    struct NTPPacket {
        uint8_t li_vn_mode;             // Leap Indicator(2), Version Number(3), Mode(3)
        uint8_t stratum;                // Stratum level of the local clock.
        uint8_t poll;                   // Maximum interval between successive messages.
        uint8_t precision;              // Precision of the local clock.
        uint32_t rootDelay;             // Total round trip delay time.
        uint32_t rootDispersion;        // Max error relative to primary reference.
        uint32_t refId;                 // Reference clock identifier.
        uint64_t refTimestamp;          // Reference timestamp.
        uint64_t originTimestamp;       // Originate timestamp.
        uint64_t receiveTimestamp;      // Receive timestamp.
        uint64_t transmitTimestamp;     // Transmit timestamp.
    };
#pragma pack(pop)

    // 字节序转换辅助函数
    // 将32位无符号整数从网络字节序（大端）转换为主机字节序
    static uint32_t ntohl(uint32_t value) {
        return ((value & 0x000000FF) << 24) |
            ((value & 0x0000FF00) << 8) |
            ((value & 0x00FF0000) >> 8) |
            ((value & 0xFF000000) >> 24);
    }

    // 将64位无符号整数从网络字节序（大端）转换为主机字节序
    static uint64_t ntohll(uint64_t value) {
        return ((uint64_t)ntohl(value & 0xFFFFFFFF) << 32) | ntohl(value >> 32);
    }

    // 将32位无符号整数从主机字节序转换到网络字节序（大端）
    static uint32_t htonl(uint32_t value) {
        return ntohl(value); // 相同的操作
    }

    // 将64位无符号整数从主机字节序转换到网络字节序（大端）
    static uint64_t htonll(uint64_t value) {
        return ntohll(value); // 相同的操作
    }

public:
    static std::optional<std::chrono::system_clock::time_point> getTimeFromServer(const std::string& server, int port = 123, int timeoutMs = 5000) {
        try {
            asio::io_context io_context;

            // 1. 解析服务器地址
            asio::ip::udp::resolver resolver(io_context);
            asio::ip::udp::resolver::results_type endpoints = resolver.resolve(
                asio::ip::udp::v4(), server, std::to_string(port)
            );

            if (endpoints.empty()) {
                std::cerr << "Error: Could not resolve NTP server address: " << server << std::endl;
                return std::nullopt;
            }
            asio::ip::udp::endpoint receiver_endpoint = *endpoints.begin();

            // 2. 创建UDP socket
            asio::ip::udp::socket socket(io_context);
            socket.open(asio::ip::udp::v4());

            // 3. 构建NTP请求包
            NTPPacket request{}; // 使用 {} 初始化为全零
            // 设置 li_vn_mode
            // LI = 0 (no warning)
            // VN = 3 or 4 (let's use 3 for broad compatibility)
            // Mode = 3 (client)
            // 00 011 011 -> 0x1B
            request.li_vn_mode = 0x1B;

            // 在发送前，将本地时间戳放入发送时间戳字段
            // 这对于计算往返延迟很重要，但对于仅获取时间不是必须的
            // 不过，这是一个好习惯
            auto now = std::chrono::system_clock::now();
            request.transmitTimestamp = NTPTimestamp::toNTP(now);
            // 将字段从主机字节序转换到网络字节序 (big-endian)
            request.transmitTimestamp = htonll(request.transmitTimestamp);

            // 4. 发送请求
            socket.send_to(asio::buffer(&request, sizeof(request)), receiver_endpoint);

            // 5. 异步接收响应，并设置超时
            NTPPacket response{};
            asio::ip::udp::endpoint sender_endpoint;
            std::optional<std::error_code> receive_error;

            socket.async_receive_from(
                asio::buffer(&response, sizeof(response)), sender_endpoint,
                [&](const std::error_code& error, std::size_t /*bytes_transferred*/) {
                    receive_error = error;
                }
            );

            // 运行io_context，但有超时限制
            // run_for() 会运行事件循环直到超时或所有工作完成
            io_context.run_for(std::chrono::milliseconds(timeoutMs));

            // 如果超时后io_context仍在运行 (意味着接收操作未完成)
            if (!io_context.stopped()) {
                // 取消挂起的异步操作
                socket.cancel();
                // 再次运行以确保取消操作的处理器被调用
                io_context.run();
                std::cerr << "Error: Timeout waiting for NTP response from " << server << std::endl;
                return std::nullopt;
            }

            if (receive_error && *receive_error && *receive_error != asio::error::operation_aborted) {
                std::cerr << "Error receiving NTP response: " << receive_error->message() << std::endl;
                return std::nullopt;
            }

            // 6. 解析NTP响应时间戳
            // 服务器的发送时间戳 (Transmit Timestamp) 是我们需要的
            // 它需要从网络字节序转换回主机字节序
            uint64_t server_transmit_ts = ntohll(response.transmitTimestamp);

            // 如果服务器返回的时间戳为0，说明它可能未同步或出错了
            if (server_transmit_ts == 0) {
                std::cerr << "Error: Invalid NTP response (Transmit Timestamp is zero)." << std::endl;
                return std::nullopt;
            }

            return NTPTimestamp::fromNTP(server_transmit_ts);

        }
        catch (const std::exception& e) {
            std::cerr << "An exception occurred: " << e.what() << std::endl;
            return std::nullopt;
        }
    }
};

// 时间同步管理器
class TimeSyncManager {
private:
    mutable std::mutex mutex_;
    std::chrono::steady_clock::time_point steadyBase_;
    std::chrono::system_clock::time_point systemBase_;
    std::atomic<bool> initialized_{ false };

    // 可选：后台同步线程
    std::unique_ptr<std::thread> syncThread_;
    std::atomic<bool> stopSync_{ false };
    std::chrono::seconds syncInterval_{ 3600 }; // 默认每小时同步一次

    std::vector<std::string> ntpServers_ = {
        "pool.ntp.org",
        "ntp.ntsc.ac.cn",
        "ntp.cnnic.cn"
    };

public:
    TimeSyncManager() = default;

    ~TimeSyncManager() {
        stopBackgroundSync();
    }

    // 初始化并同步时间
    bool initialize(const std::vector<std::string>& servers = {}) {
        if (!servers.empty()) {
            ntpServers_ = servers;
        }

        return syncTime();
    }

    // 同步时间
    bool syncTime() {
        std::optional<std::chrono::system_clock::time_point> ntpTime;

        // 尝试从多个服务器获取时间
        for (const auto& server : ntpServers_) {
            ntpTime = SimpleNTPClient::getTimeFromServer(server);
            if (ntpTime.has_value()) {
                break;
            }
        }

        if (!ntpTime.has_value()) {
            return false;
        }

        std::lock_guard<std::mutex> lock(mutex_);
        steadyBase_ = std::chrono::steady_clock::now();
        systemBase_ = ntpTime.value();
        std::time_t t = std::chrono::system_clock::to_time_t(ntpTime.value());
        std::cout << "Current time: " << t << std::endl;
        initialized_ = true;

        return true;
    }

    // 获取当前网络时间
    std::chrono::system_clock::time_point getCurrentTime() const {
        if (!initialized_) {
            throw std::runtime_error("TimeSyncManager not initialized");
        }

        std::lock_guard<std::mutex> lock(mutex_);
        auto elapsed = std::chrono::steady_clock::now() - steadyBase_;
        auto systemElapsed = std::chrono::duration_cast<std::chrono::system_clock::duration>(elapsed);
        return systemBase_ + systemElapsed;
    }

    // 启动后台同步
    void startBackgroundSync(std::chrono::seconds interval = std::chrono::seconds(3600)) {
        stopBackgroundSync();

        syncInterval_ = interval;
        stopSync_ = false;

        syncThread_ = std::make_unique<std::thread>([this]() {
            while (!stopSync_) {
                std::this_thread::sleep_for(syncInterval_);
                if (!stopSync_) {
                    syncTime();
                }
            }
            });
    }

    // 停止后台同步
    void stopBackgroundSync() {
        if (syncThread_ && syncThread_->joinable()) {
            stopSync_ = true;
            syncThread_->join();
            syncThread_.reset();
        }
    }

    bool isInitialized() const {
        return initialized_;
    }
};

// ==================================================================
// 时间转换器
std::chrono::system_clock::time_point TimeZoneConverter::getUTCTime() const {
    return syncManager_.getCurrentTime();
}

std::chrono::system_clock::time_point TimeZoneConverter::toTimeZone(int offsetHours, int offsetMinutes/*= 0*/) const {
    auto utcTime = getUTCTime();
    auto offset = std::chrono::hours(offsetHours) + std::chrono::minutes(offsetMinutes);
    return utcTime + offset;
}

// ==================================================================
// 封装
NetworkTimeService::NetworkTimeService()
    : syncManager_(std::make_unique<TimeSyncManager>()) {
    // 构造函数中初始化成员
    converter_ = std::unique_ptr<TimeZoneConverter>(new TimeZoneConverter(*syncManager_));
}

NetworkTimeService::~NetworkTimeService() = default;

bool NetworkTimeService::initialize(const std::vector<std::string>& ntpServers,
    bool enableBackgroundSync,
    std::chrono::seconds syncInterval) {
    if (!syncManager_->initialize(ntpServers)) {
        return false;
    }

    if (enableBackgroundSync) {
        syncManager_->startBackgroundSync(syncInterval);
    }

    return true;
}

TimeZoneConverter& NetworkTimeService::getConverter() const {
    return *converter_;
}

bool NetworkTimeService::syncNow() {
    return syncManager_->syncTime();
}