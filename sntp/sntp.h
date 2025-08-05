#pragma once

#include <chrono>
#include <memory>
#include <string>
#include <vector>

class TimeSyncManager;
// 时区转换器
class TimeZoneConverter {
private:
    friend class NetworkTimeService;
    TimeSyncManager& syncManager_;

private:
    explicit TimeZoneConverter(TimeSyncManager& syncManager)
        : syncManager_(syncManager) {}

public:
    // 获取UTC时间
    std::chrono::system_clock::time_point getUTCTime() const;

    // 转换为指定时区的时间字符串
    std::chrono::system_clock::time_point toTimeZone(int offsetHours, int offsetMinutes = 0) const;

    // 常用时区的便捷方法
    std::chrono::system_clock::time_point toUTC() const {
        return toTimeZone(0, 0);
    }

    std::chrono::system_clock::time_point toBeijingTime() const {
        return toTimeZone(8, 0);  // UTC+8
    }

    std::chrono::system_clock::time_point toNewYorkTime() const {
        return toTimeZone(-5, 0);  // UTC-5 (EST)
    }

    std::chrono::system_clock::time_point toLondonTime() const {
        return toTimeZone(0, 0);  // UTC+0
    }

    std::chrono::system_clock::time_point toTokyoTime() const {
        return toTimeZone(9, 0);  // UTC+9
    }
};

// 网络时间服务
class NetworkTimeService {
private:
    // 使用智能指针封装
    std::unique_ptr<TimeSyncManager> syncManager_;
    std::unique_ptr<TimeZoneConverter> converter_;

public:
    NetworkTimeService();
    ~NetworkTimeService();  // 显式声明析构函数，确保资源释放

    // 初始化服务
    bool initialize(const std::vector<std::string>& ntpServers = {},
        bool enableBackgroundSync = false,
        std::chrono::seconds syncInterval = std::chrono::seconds(3600));

    // 获取时区转换器
    TimeZoneConverter& getConverter() const;

    // 手动同步时间
    bool syncNow();
};