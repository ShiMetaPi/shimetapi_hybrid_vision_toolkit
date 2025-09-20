#include "hv_evs_recorder.h"
#include <iostream>
#include <chrono>
#include <thread>
#include <signal.h>
#include <iomanip>
#include <cstdlib>

// 全局变量用于信号处理
hv::HV_EVS_Recorder* g_recorder = nullptr;
bool g_running = true;

// 信号处理函数
void signalHandler(int signal) {
    (void)signal;  // 避免未使用参数警告
    std::cout << "\n接收到停止信号，正在停止录制..." << std::endl;
    g_running = false;
    if (g_recorder) {
        g_recorder->stopRecording();
    }
}

int main(int argc, char* argv[]) {
    // 设置信号处理
    signal(SIGINT, signalHandler);
    signal(SIGTERM, signalHandler);
    
    // 解析命令行参数
    std::string output_filename = "evs_data.raw";
    int recording_duration = 10; // 0表示无限录制
    bool enable_timestamp_analysis = false;
    
    if (argc > 1) {
        output_filename = argv[1];
    }
    if (argc > 2) {
        recording_duration = std::atoi(argv[2]);
    }
    if (argc > 3) {
        enable_timestamp_analysis = (std::string(argv[3]) == "1" || std::string(argv[3]) == "true");
    }
    
    std::cout << "EVS数据录制器示例程序" << std::endl;
    std::cout << "使用方法: " << argv[0] << " [输出文件] [录制时长(秒)] [启用时间戳分析(1/0)]" << std::endl;
    std::cout << "输出文件: " << output_filename << std::endl;
    if (recording_duration > 0) {
        std::cout << "录制时长: " << recording_duration << " 秒" << std::endl;
    } else {
        std::cout << "录制时长: 无限制 (按Ctrl+C停止)" << std::endl;
    }
    std::cout << "时间戳分析: " << (enable_timestamp_analysis ? "启用" : "禁用") << std::endl;
    std::cout << "========================================" << std::endl;
    
    // 创建EVS录制器实例
    // 使用与原始hv_camera相同的设备ID
    const uint16_t VENDOR_ID = 0x1d6b;   // USB设备厂商ID
    const uint16_t PRODUCT_ID = 0x0105;  // USB设备产品ID
    
    hv::HV_EVS_Recorder recorder(VENDOR_ID, PRODUCT_ID);
    g_recorder = &recorder;
    
    // 打开设备
    if (!recorder.open()) {
        std::cerr << "错误: 无法打开EVS设备" << std::endl;
        std::cerr << "请检查:" << std::endl;
        std::cerr << "1. 设备是否已连接" << std::endl;
        std::cerr << "2. USB驱动是否正确安装" << std::endl;
        std::cerr << "3. 设备ID是否正确 (当前: 0x" << std::hex << VENDOR_ID 
                  << ":0x" << PRODUCT_ID << ")" << std::dec << std::endl;
        return -1;
    }
    
    std::cout << "设备打开成功" << std::endl;
    
    // 开始录制
    if (!recorder.startRecording(output_filename, enable_timestamp_analysis)) {
        std::cerr << "错误: 无法开始录制" << std::endl;
        recorder.close();
        return -1;
    }
    
    std::cout << "开始录制EVS数据..." << std::endl;
    
    // 录制循环
    auto start_time = std::chrono::steady_clock::now();
    auto last_stats_time = start_time;
    
    while (g_running && recorder.isRecording()) {
        std::this_thread::sleep_for(std::chrono::milliseconds(1000));
        
        // 检查录制时长
        if (recording_duration > 0) {
            auto current_time = std::chrono::steady_clock::now();
            auto elapsed = std::chrono::duration_cast<std::chrono::seconds>(current_time - start_time);
            if (elapsed.count() >= recording_duration) {
                std::cout << "达到指定录制时长，停止录制" << std::endl;
                break;
            }
        }
        
        // 每5秒输出一次统计信息
        auto current_time = std::chrono::steady_clock::now();
        auto stats_elapsed = std::chrono::duration_cast<std::chrono::seconds>(current_time - last_stats_time);
        if (stats_elapsed.count() >= 5) {
            uint64_t total_bytes, total_frames, avg_transfer_time;
            recorder.getRecordingStats(total_bytes, total_frames, avg_transfer_time);
            
            double mb_recorded = static_cast<double>(total_bytes) / (1024 * 1024);
            double recording_time = std::chrono::duration_cast<std::chrono::seconds>(current_time - start_time).count();
            double data_rate = (recording_time > 0) ? (mb_recorded / recording_time) : 0;
            
            std::cout << "========== 录制统计 ==========" << std::endl;
            std::cout << "录制时间: " << static_cast<int>(recording_time) << " 秒" << std::endl;
            std::cout << "总帧数: " << total_frames << std::endl;
            std::cout << "总数据量: " << std::fixed << std::setprecision(2) << mb_recorded << " MB" << std::endl;
            std::cout << "数据速率: " << std::fixed << std::setprecision(2) << data_rate << " MB/s" << std::endl;
            std::cout << "平均传输时间: " << avg_transfer_time << " μs" << std::endl;
            if (total_frames > 0) {
                std::cout << "平均帧率: " << std::fixed << std::setprecision(1) 
                          << (total_frames / recording_time) << " FPS" << std::endl;
            }
            std::cout << "============================" << std::endl;
            
            last_stats_time = current_time;
        }
    }
    
    // 停止录制
    std::cout << "正在停止录制..." << std::endl;
    recorder.stopRecording();
    
    // 输出最终统计信息
    uint64_t total_bytes, total_frames, avg_transfer_time;
    recorder.getRecordingStats(total_bytes, total_frames, avg_transfer_time);
    
    auto end_time = std::chrono::steady_clock::now();
    auto total_time = std::chrono::duration_cast<std::chrono::seconds>(end_time - start_time);
    
    std::cout << "\n========== 最终统计 ==========" << std::endl;
    std::cout << "录制完成!" << std::endl;
    std::cout << "输出文件: " << output_filename << std::endl;
    std::cout << "总录制时间: " << total_time.count() << " 秒" << std::endl;
    std::cout << "总帧数: " << total_frames << std::endl;
    std::cout << "总数据量: " << std::fixed << std::setprecision(2) 
              << (static_cast<double>(total_bytes) / (1024 * 1024)) << " MB" << std::endl;
    std::cout << "平均数据速率: " << std::fixed << std::setprecision(2) 
              << (static_cast<double>(total_bytes) / (1024 * 1024) / total_time.count()) << " MB/s" << std::endl;
    std::cout << "平均传输时间: " << avg_transfer_time << " μs" << std::endl;
    if (total_time.count() > 0) {
        std::cout << "平均帧率: " << std::fixed << std::setprecision(1) 
                  << (static_cast<double>(total_frames) / total_time.count()) << " FPS" << std::endl;
    }
    std::cout << "============================" << std::endl;
    
    // 关闭设备
    recorder.close();
    
    return 0;
}

