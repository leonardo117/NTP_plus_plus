#include <iostream>
#include <iomanip>
#include "sntp.h"

int main() {
    // 创建网络时间服务实例
    NetworkTimeService timeService;
    
    // 初始化服务，使用默认NTP服务器，启用后台同步（每30分钟一次）
    std::cout << "初始化网络时间服务..." << std::endl;
    std::vector<std::string> ntpServers = {};
    bool initialized = timeService.initialize(ntpServers, true,  std::chrono::seconds(1800));
    
    if (!initialized) {
        std::cerr << "网络时间服务初始化失败!" << std::endl;
        return 1;
    }
    
    // 获取时区转换器
    TimeZoneConverter& converter = timeService.getConverter();
    
    // 测试不同时区的时间转换
    std::cout << "\n=== 当前时间 ===" << std::endl;
    auto printTime = [](const std::string& zone, const std::chrono::system_clock::time_point& time) {
        std::time_t t = std::chrono::system_clock::to_time_t(time);
        std::cout << std::left << std::setw(15) << zone 
                  << std::put_time(std::gmtime(&t), "%Y-%m-%d %H:%M:%S") << std::endl;
    };
    
    printTime("UTC:", converter.getUTCTime());
    printTime("北京(UTC+8):", converter.toBeijingTime());
    printTime("伦敦(UTC+0):", converter.toLondonTime());
    printTime("东京(UTC+9):", converter.toTokyoTime());
    printTime("纽约(UTC-5):", converter.toNewYorkTime());
    
    // 手动触发一次同步
    std::cout << "\n手动触发时间同步..." << std::endl;
    bool syncSuccess = timeService.syncNow();
    std::cout << "同步结果: " << (syncSuccess ? "成功" : "失败") << std::endl;
    
    // 等待用户输入，保持程序运行以观察后台同步
    std::cout << "\n程序正在运行，按Enter键退出..." << std::endl;
    std::cin.get();
    
    return 0;
}