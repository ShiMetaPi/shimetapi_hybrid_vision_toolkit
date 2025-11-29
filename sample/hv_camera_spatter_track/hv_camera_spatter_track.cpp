#include <iostream>
#include <string>
#include <chrono>
#include <thread>
#include <atomic>
#include <mutex>
#include <condition_variable>
#include <queue>
#include <numeric>
#include <algorithm>
#include <cmath>
#ifndef M_PI
#define M_PI 3.14159265358979323846
#endif
#include <signal.h>
#include <iomanip>
#include <sstream>
#include <opencv2/opencv.hpp>
#include "hv_camera.h"

// 全局控制标志
std::atomic<bool> g_running(true);
std::atomic<bool> g_display_enabled(true);
std::atomic<bool> g_show_candidates(false);

// 显示数据结构
struct EVSDisplayData {
    cv::Mat evs_frame;
    std::chrono::steady_clock::time_point timestamp;
    
    EVSDisplayData() : timestamp(std::chrono::steady_clock::now()) {}
};


// EVS显示队列类
template<typename T>
class DisplayQueue {
public:
    DisplayQueue() = default;
    
    void push(const T& data) {
        std::lock_guard<std::mutex> lock(mutex_);
        // 容量为1，新数据直接替换旧数据
        if (!queue_.empty()) {
            queue_.pop();
        }
        queue_.push(data);
        cv_.notify_one();
    }
    
    bool pop(T& data, std::chrono::milliseconds timeout = std::chrono::milliseconds(100)) {
        std::unique_lock<std::mutex> lock(mutex_);
        if (cv_.wait_for(lock, timeout, [this] { return !queue_.empty() || !g_running; })) {
            if (!queue_.empty()) {
                data = queue_.front();
                queue_.pop();
                return true;
            }
        }
        return false;
    }
    
private:
    std::queue<T> queue_;
    std::mutex mutex_;
    std::condition_variable cv_;
};

using EVSDisplayQueue = DisplayQueue<EVSDisplayData>;


// 信号处理函数
void signalHandler(int signum) {
    std::cout << "\n接收到停止信号，正在退出..." << std::endl;
    g_running = false;
}



// EVS帧生成器（简化版）
class EVSFrameGenerator {
public:
    EVSFrameGenerator(int width = HV_EVS_WIDTH, int height = HV_EVS_HEIGHT) 
        : width_(width), height_(height) {
        frame_ = cv::Mat::zeros(height, width, CV_8UC3);
    }
    
    void addEvents(const std::vector<Metavision::EventCD>& events) {
        std::lock_guard<std::mutex> lock(mutex_);
        
        // 简单的事件可视化：正事件为白色，负事件为红色
        for (const auto& event : events) {
            if (event.x >= 0 && event.x < width_ && event.y >= 0 && event.y < height_) {
                if (event.p > 0) {
                    frame_.at<cv::Vec3b>(event.y, event.x) = cv::Vec3b(255, 255, 255); // 白色
                } else {
                    frame_.at<cv::Vec3b>(event.y, event.x) = cv::Vec3b(0, 0, 255); // 红色
                }
            }
        }
    }
    
    cv::Mat getFrame() {
        std::lock_guard<std::mutex> lock(mutex_);
        cv::Mat result = frame_.clone();
        
        // 逐渐衰减显示
        frame_ *= 0.95;
        
        return result;
    }
    
private:
    cv::Mat frame_;
    int width_, height_;
    std::mutex mutex_;
};

class ParticleDetector {
public:
    ParticleDetector(int width = HV_EVS_WIDTH, int height = HV_EVS_HEIGHT)
        : width_(width), height_(height), decay_(0.94f), thresh_high_(5.0f), thresh_low_(0.0f),
          min_area_(100), max_area_(90000), kernel_size_(3), w_on_(1.0f), w_off_(1.0f),
          merge_dist_(8.0f), next_id_(1), max_missed_(5), match_dist_(20.0f), k_high_(2.0), k_low_(1.0),
          min_w_(10), min_h_(10), max_w_(300), max_h_(300), occupancy_thr_(0.15f),
          iou_merge_thr_(0.4f), match_iou_thr_(0.2f), nms_iou_thr_(0.5f), min_confirmations_(2),
          max_size_change_ratio_(2.0f), min_circularity_(0.25f), min_solidity_(0.6f), max_aspect_ratio_(3.0f),
          min_speed_pxps_(20.0f), min_speed_frames_(2) {
        accum_on_ = cv::Mat::zeros(height_, width_, CV_32F);
        accum_off_ = cv::Mat::zeros(height_, width_, CV_32F);
    }
    void setDecay(float d) { decay_ = d; }
    void setThresholds(float high_t, float low_t) { thresh_high_ = high_t; thresh_low_ = low_t; }
    void setAreaRange(int min_a, int max_a) { min_area_ = min_a; max_area_ = max_a; }
    void setKernelSize(int k) { kernel_size_ = std::max(1, k); }
    void setPolarityWeights(float w_on, float w_off) { w_on_ = w_on; w_off_ = w_off; }
    void setMergeDist(float d) { merge_dist_ = d; }
    void setTrackParams(int max_missed, float match_dist) { max_missed_ = max_missed; match_dist_ = match_dist; }
    void setAdaptiveK(double kh, double kl) { k_high_ = kh; k_low_ = kl; }
    void setSizeLimits(int min_w, int min_h, int max_w, int max_h) { min_w_ = min_w; min_h_ = min_h; max_w_ = max_w; max_h_ = max_h; }
    void setOccupancyThreshold(float occ) { occupancy_thr_ = occ; }
    void setIouThresholds(float merge_iou, float match_iou, float nms_iou) { iou_merge_thr_ = merge_iou; match_iou_thr_ = match_iou; nms_iou_thr_ = nms_iou; }
    void setConfirmation(int min_conf, float max_size_ratio) { min_confirmations_ = min_conf; max_size_change_ratio_ = max_size_ratio; }
    void setShapeThresholds(float min_circ, float min_sol, float max_ar) { min_circularity_ = min_circ; min_solidity_ = min_sol; max_aspect_ratio_ = max_ar; }
    void setSpeedThreshold(float min_speed, int min_frames) { min_speed_pxps_ = min_speed; min_speed_frames_ = std::max(1, min_frames); }
    void addEvents(const std::vector<Metavision::EventCD>& events) {
        std::lock_guard<std::mutex> lock(mutex_);
        for (const auto& e : events) {
            if (e.x < width_ && e.y < height_) {
                if (e.p > 0) {
                    float& v = accum_on_.at<float>(e.y, e.x);
                    v = std::min(v + 1.0f, 255.0f);
                } else {
                    float& v = accum_off_.at<float>(e.y, e.x);
                    v = std::min(v + 1.0f, 255.0f);
                }
            }
        }
    }
    void detect() {
        std::lock_guard<std::mutex> lock(mutex_);
        accum_on_ *= decay_;
        accum_off_ *= decay_;
        cv::Mat combined = w_on_ * accum_on_ + w_off_ * accum_off_;
        cv::Scalar m, s;
        cv::meanStdDev(combined, m, s);
        float high_t = thresh_high_;
        float low_t = thresh_low_ > 0.0f ? thresh_low_ : thresh_high_ * 0.6f;
        if (high_t <= 0.0f) {
            high_t = static_cast<float>(m[0] + k_high_ * s[0]);
            low_t = static_cast<float>(m[0] + k_low_ * s[0]);
        }
        cv::Mat mask_high, mask_low;
        cv::threshold(combined, mask_high, high_t, 255.0, cv::THRESH_BINARY);
        cv::threshold(combined, mask_low, low_t, 255.0, cv::THRESH_BINARY);
        mask_high.convertTo(bin_high_, CV_8U);
        mask_low.convertTo(bin_low_, CV_8U);
        cv::Mat kernel = cv::getStructuringElement(cv::MORPH_RECT, cv::Size(std::max(1, kernel_size_), std::max(1, kernel_size_)));
        cv::morphologyEx(bin_high_, bin_high_, cv::MORPH_OPEN, kernel);
        cv::morphologyEx(bin_high_, bin_high_, cv::MORPH_CLOSE, kernel);
        std::vector<cv::Rect> boxes;
        cv::Mat labels, stats, centroids;
        int n = cv::connectedComponentsWithStats(bin_high_, labels, stats, centroids, 8, CV_32S);
        bool using_low = false;
        if (n <= 1) {
            cv::Mat kernel_low = cv::getStructuringElement(cv::MORPH_RECT, cv::Size(1, 1));
            cv::morphologyEx(bin_low_, bin_low_, cv::MORPH_OPEN, kernel_low);
            int nl = cv::connectedComponentsWithStats(bin_low_, labels, stats, centroids, 8, CV_32S);
            if (nl > 1) { n = nl; using_low = true; }
        }
        
        for (int i = 1; i < n; ++i) {
            int x = stats.at<int>(i, cv::CC_STAT_LEFT);
            int y = stats.at<int>(i, cv::CC_STAT_TOP);
            int w = stats.at<int>(i, cv::CC_STAT_WIDTH);
            int h = stats.at<int>(i, cv::CC_STAT_HEIGHT);
            int area = w * h;
            if (area < min_area_ || area > max_area_) continue;
            if (w < min_w_ || h < min_h_ || w > max_w_ || h > max_h_) continue;
            cv::Rect box(x, y, w, h);
            cv::Rect clipped = box & cv::Rect(0, 0, combined.cols, combined.rows);
            if (clipped.width <= 0 || clipped.height <= 0) continue;
            cv::Mat roi_low = bin_low_(clipped);
            int nonzero = cv::countNonZero(roi_low);
            float occ = static_cast<float>(nonzero) / static_cast<float>(clipped.width * clipped.height);
            if (occ < occupancy_thr_) continue;
            cv::Mat roi_mask = using_low ? bin_low_(clipped) : bin_high_(clipped);
            std::vector<std::vector<cv::Point>> contours;
            cv::findContours(roi_mask, contours, cv::RETR_EXTERNAL, cv::CHAIN_APPROX_SIMPLE);
            if (contours.empty()) continue;
            size_t li = 0; double la = 0.0;
            for (size_t k = 0; k < contours.size(); ++k) {
                double a = cv::contourArea(contours[k]);
                if (a > la) { la = a; li = k; }
            }
            double per = cv::arcLength(contours[li], true);
            double circ = per > 0.0 ? (4.0 * M_PI * la) / (per * per) : 0.0;
            std::vector<cv::Point> hull;
            cv::convexHull(contours[li], hull);
            double hull_area = cv::contourArea(hull);
            double sol = hull_area > 0.0 ? la / hull_area : 0.0;
            float ar = static_cast<float>(std::max(w, h)) / static_cast<float>(std::min(w, h));
            if (circ < min_circularity_ || sol < min_solidity_ || ar > max_aspect_ratio_) continue;
            boxes.emplace_back(box);
        }
        boxes = nonMaximumSuppression(boxes, nms_iou_thr_);
        mergeNearbyBoxes(boxes);
        last_boxes_ = boxes;
        updateTracks(boxes);
    }
    void overlay(cv::Mat& img, int display_fps) {
        std::lock_guard<std::mutex> lock(mutex_);
        for (const auto& t : tracks_) {
            if (!t.confirmed) continue;
            cv::rectangle(img, t.bbox, cv::Scalar(0,255,0), 2);
            double speed = std::sqrt(t.velocity.x * t.velocity.x + t.velocity.y * t.velocity.y) * display_fps;
            std::ostringstream oss;
            oss << "ID " << t.id << " v=" << std::fixed << std::setprecision(1) << speed;
            cv::putText(img, oss.str(), cv::Point(t.bbox.x, std::max(0, t.bbox.y - 5)),
                        cv::FONT_HERSHEY_SIMPLEX, 0.5, cv::Scalar(0,255,0), 1);
        }
        if (g_show_candidates) {
            for (const auto& b : last_boxes_) {
                cv::rectangle(img, b, cv::Scalar(0,255,255), 1);
            }
        }
        std::ostringstream cnt;
        cnt << tracks_.size() << " particles";
        cv::putText(img, cnt.str(), cv::Point(10, 60), cv::FONT_HERSHEY_SIMPLEX, 0.7, cv::Scalar(0,255,0), 2);
    }
private:
    static double iou(const cv::Rect& a, const cv::Rect& b) {
        int x1 = std::max(a.x, b.x);
        int y1 = std::max(a.y, b.y);
        int x2 = std::min(a.x + a.width, b.x + b.width);
        int y2 = std::min(a.y + a.height, b.y + b.height);
        int inter = std::max(0, x2 - x1) * std::max(0, y2 - y1);
        int ua = a.width * a.height + b.width * b.height - inter;
        return ua > 0 ? static_cast<double>(inter) / ua : 0.0;
    }
    static std::vector<cv::Rect> nonMaximumSuppression(const std::vector<cv::Rect>& boxes, float iou_thr) {
        std::vector<cv::Rect> result;
        std::vector<cv::Rect> sorted = boxes;
        std::vector<int> idx(sorted.size());
        std::iota(idx.begin(), idx.end(), 0);
        std::vector<bool> suppressed(sorted.size(), false);
        for (size_t i = 0; i < sorted.size(); ++i) {
            if (suppressed[i]) continue;
            cv::Rect bi = sorted[i];
            result.push_back(bi);
            for (size_t j = i + 1; j < sorted.size(); ++j) {
                if (suppressed[j]) continue;
                if (iou(bi, sorted[j]) > iou_thr) suppressed[j] = true;
            }
        }
        return result;
    }
    void mergeNearbyBoxes(std::vector<cv::Rect>& boxes) {
        bool merged = true;
        while (merged) {
            merged = false;
            for (size_t i = 0; i < boxes.size(); ++i) {
                for (size_t j = i + 1; j < boxes.size(); ++j) {
                    cv::Point2f ci(boxes[i].x + boxes[i].width * 0.5f, boxes[i].y + boxes[i].height * 0.5f);
                    cv::Point2f cj(boxes[j].x + boxes[j].width * 0.5f, boxes[j].y + boxes[j].height * 0.5f);
                    double dist = cv::norm(ci - cj);
                    if (dist < merge_dist_ || iou(boxes[i], boxes[j]) > iou_merge_thr_) {
                        int x = std::min(boxes[i].x, boxes[j].x);
                        int y = std::min(boxes[i].y, boxes[j].y);
                        int r = std::max(boxes[i].x + boxes[i].width, boxes[j].x + boxes[j].width);
                        int b = std::max(boxes[i].y + boxes[i].height, boxes[j].y + boxes[j].height);
                        boxes[i] = cv::Rect(x, y, r - x, b - y);
                        boxes.erase(boxes.begin() + j);
                        merged = true;
                        break;
                    }
                }
                if (merged) break;
            }
        }
    }
    struct Track {
        int id;
        cv::Rect bbox;
        cv::Point2f center;
        cv::Point2f velocity;
        cv::Point2f direction;
        int age;
        int missed;
        int confirmations;
        bool confirmed;
        int speed_ok_frames;
    };
    void updateTracks(const std::vector<cv::Rect>& boxes) {
        for (auto& t : tracks_) t.missed++;
        std::vector<cv::Point2f> box_centers;
        box_centers.reserve(boxes.size());
        for (const auto& b : boxes) box_centers.emplace_back(b.x + b.width * 0.5f, b.y + b.height * 0.5f);
        std::vector<bool> matched(boxes.size(), false);
        for (const auto& b : boxes) {
            cv::Point2f c(b.x + b.width * 0.5f, b.y + b.height * 0.5f);
            int best = -1; double best_d = match_dist_;
            for (size_t i = 0; i < tracks_.size(); ++i) {
                double iouv = iou(b, tracks_[i].bbox);
                if (iouv > match_iou_thr_) { best = static_cast<int>(i); break; }
                double d = cv::norm(c - tracks_[i].center);
                if (d < best_d) { best_d = d; best = static_cast<int>(i); }
            }
            if (best >= 0) {
                cv::Point2f vel = c - tracks_[best].center;
                float wr = static_cast<float>(b.width) / static_cast<float>(tracks_[best].bbox.width);
                float hr = static_cast<float>(b.height) / static_cast<float>(tracks_[best].bbox.height);
                if (wr < max_size_change_ratio_ && hr < max_size_change_ratio_) {
                    tracks_[best].bbox = b;
                }
                tracks_[best].center = c;
                tracks_[best].velocity = vel;
                cv::Point2f dir = vel;
                float n = std::sqrt(dir.x * dir.x + dir.y * dir.y);
                if (n > 1e-3f) {
                    dir.x /= n; dir.y /= n; tracks_[best].direction = dir;
                }
                tracks_[best].age++;
                tracks_[best].missed = 0;
                tracks_[best].confirmations = std::min(tracks_[best].confirmations + 1, 1000);
                double speed = std::sqrt(vel.x * vel.x + vel.y * vel.y) * 1.0;
                if (speed >= min_speed_pxps_) tracks_[best].speed_ok_frames++; else tracks_[best].speed_ok_frames = 0;
                if (tracks_[best].confirmations >= min_confirmations_ && tracks_[best].speed_ok_frames >= min_speed_frames_) tracks_[best].confirmed = true;
                // 标记匹配
                for (size_t bi = 0; bi < boxes.size(); ++bi) {
                    if (!matched[bi]) {
                        cv::Point2f cc = box_centers[bi];
                        if (std::abs(cc.x - c.x) < 1e-5f && std::abs(cc.y - c.y) < 1e-5f) { matched[bi] = true; break; }
                    }
                }
            } else {
                Track t; t.id = next_id_++; t.bbox = b; t.center = c; t.velocity = cv::Point2f(0,0); t.direction = cv::Point2f(0,0); t.age = 1; t.missed = 0; t.confirmations = 1; t.confirmed = false; t.speed_ok_frames = 0;
                tracks_.push_back(t);
            }
        }
        tracks_.erase(std::remove_if(tracks_.begin(), tracks_.end(), [&](const Track& t){ return t.missed > max_missed_; }), tracks_.end());
    }
    int width_, height_;
    cv::Mat accum_on_;
    cv::Mat accum_off_;
    cv::Mat bin_high_;
    cv::Mat bin_low_;
    float decay_;
    float thresh_high_;
    float thresh_low_;
    int min_area_;
    int max_area_;
    int kernel_size_;
    float w_on_;
    float w_off_;
    float merge_dist_;
    std::vector<Track> tracks_;
    int next_id_;
    int max_missed_;
    float match_dist_;
    double k_high_;
    double k_low_;
    int min_w_;
    int min_h_;
    int max_w_;
    int max_h_;
    float occupancy_thr_;
    float iou_merge_thr_;
    float match_iou_thr_;
    float nms_iou_thr_;
    int min_confirmations_;
    float max_size_change_ratio_;
    float min_circularity_;
    float min_solidity_;
    float max_aspect_ratio_;
    float min_speed_pxps_;
    int min_speed_frames_;
    std::vector<cv::Rect> last_boxes_;
    std::mutex mutex_;
};

// 显示管理器
class DisplayManager {
public:
    DisplayManager(EVSDisplayQueue& evs_queue) 
        : evs_display_queue_(evs_queue),
          evs_generator_(HV_EVS_WIDTH, HV_EVS_HEIGHT),
          last_evs_push_(std::chrono::steady_clock::now()),
          display_fps_(30) {}
    
    void addEvents(const std::vector<Metavision::EventCD>& events) {
        if (g_display_enabled) {
            // 始终处理最新EVS事件
            evs_generator_.addEvents(events);
            
            // 频率控制：只有时间间隔足够时才推入队列，确保推入的是时间上最新的一帧
            auto now = std::chrono::steady_clock::now();
            if (std::chrono::duration_cast<std::chrono::milliseconds>(
                now - last_evs_push_).count() >= (1000 / display_fps_)) {
                
                std::lock_guard<std::mutex> lock(evs_data_mutex_);
                EVSDisplayData evs_data;
                evs_data.evs_frame = evs_generator_.getFrame();
                evs_data.timestamp = now;
                evs_display_queue_.push(evs_data);
                last_evs_push_ = now;
            }
        }
    }
    
    
    void setDisplayFPS(int fps) {
        display_fps_ = fps;
    }
    
private:
    EVSDisplayQueue& evs_display_queue_;
    EVSFrameGenerator evs_generator_;
    std::mutex evs_data_mutex_;
    std::chrono::steady_clock::time_point last_evs_push_;
    int display_fps_;
};

// 显示线程函数
void displayWorkerThread(EVSDisplayQueue& evs_display_queue,
                        ParticleDetector& detector,
                        int display_fps) {
    cv::namedWindow("EVS Events", cv::WINDOW_AUTOSIZE);
    
    cv::moveWindow("EVS Events", 100, 100);
    
    EVSDisplayData evs_data;
    bool evs_updated = false;
    bool has_evs_data = false;
    
    // 显示帧率控制
    const auto frame_duration = std::chrono::milliseconds(std::max(1, 1000 / std::max(1, display_fps)));
    
    
    while (g_running) {
        auto loop_start = std::chrono::steady_clock::now();
        
        evs_updated = evs_display_queue.pop(evs_data, std::chrono::milliseconds(16));
        
        // 记录是否有数据
        if (evs_updated) has_evs_data = true;
        
        // 处理EVS显示
        if (evs_updated && !evs_data.evs_frame.empty()) {
            cv::Mat evs_display = evs_data.evs_frame.clone();
            detector.detect();
            detector.overlay(evs_display, display_fps);
            
            // 在EVS窗口添加状态信息
            cv::putText(evs_display, "EVS - Display", cv::Point(10, 30), 
                       cv::FONT_HERSHEY_SIMPLEX, 0.7, cv::Scalar(255, 255, 0), 2);
            
            cv::imshow("EVS Events", evs_display);
        } else if (!has_evs_data) {
            // 只有在从未收到过EVS数据时才显示无信号提示
            cv::Mat evs_no_signal = cv::Mat::zeros(HV_EVS_HEIGHT, HV_EVS_WIDTH, CV_8UC3);
            cv::putText(evs_no_signal, "EVS No Signal", cv::Point(50, HV_EVS_HEIGHT/2), 
                       cv::FONT_HERSHEY_SIMPLEX, 1, cv::Scalar(0, 0, 255), 2);
            cv::imshow("EVS Events", evs_no_signal);
        }
        
        // 处理键盘输入
        int key = cv::waitKey(1) & 0xFF;
        if (key == 27 || key == 'q') { // ESC或q键退出
            g_running = false;
            break;
        } else if (key == 'd') { // d键切换显示
            g_display_enabled = !g_display_enabled;
            std::cout << "显示 " << (g_display_enabled ? "开启" : "关闭") << std::endl;
        } else if (key == 'c') {
            g_show_candidates = !g_show_candidates;
            std::cout << "候选框显示 " << (g_show_candidates ? "开启" : "关闭") << std::endl;
        }
        
        // 帧率控制：确保显示线程以稳定的30FPS运行
        auto loop_end = std::chrono::steady_clock::now();
        auto elapsed = std::chrono::duration_cast<std::chrono::milliseconds>(loop_end - loop_start);
        if (elapsed < frame_duration) {
            std::this_thread::sleep_for(frame_duration - elapsed);
        }
    }
    
    cv::destroyAllWindows();
}

int main(int argc, char* argv[]) {
    std::cout << "=== HV相机实时事件显示与检测 (EVS) ===" << std::endl;
    std::cout << "控制说明:" << std::endl;
    std::cout << "  d - 开启/关闭显示" << std::endl;
    std::cout << "  q/ESC - 退出程序" << std::endl;
    std::cout << "  Ctrl+C - 强制退出" << std::endl;
    
    // 解析命令行参数
    int display_fps = 30;
    float decay = 0.94f;
    float thresh_high = 5.0f;
    float thresh_low = 0.0f;
    int min_area = 100;
    int max_area = 90000;
    int kernel_size = 3;
    float w_on = 1.0f;
    float w_off = 1.0f;
    float merge_dist = 8.0f;
    int max_missed = 5;
    float match_dist = 20.0f;
    double k_high = 2.0;
    double k_low = 1.0;
    int min_w = 10;
    int min_h = 10;
    int max_w = 300;
    int max_h = 300;
    float occupancy_thr = 0.15f;
    float iou_merge_thr = 0.4f;
    float match_iou_thr = 0.2f;
    float nms_iou_thr = 0.5f;
    int min_confirmations = 2;
    float max_size_change_ratio = 2.0f;
    float min_circularity = 0.25f;
    float min_solidity = 0.6f;
    float max_aspect_ratio = 3.0f;
    float min_speed_pxps = 20.0f;
    int min_speed_frames = 2;
    
    if (argc >= 2) {
        display_fps = std::atoi(argv[1]);
    }
    if (argc >= 3) {
        decay = std::atof(argv[2]);
    }
    if (argc >= 4) {
        thresh_high = std::atof(argv[3]);
    }
    if (argc >= 5) {
        min_area = std::atoi(argv[4]);
    }
    if (argc >= 6) {
        max_area = std::atoi(argv[5]);
    }
    if (argc >= 7) {
        kernel_size = std::atoi(argv[6]);
    }
    if (argc >= 8) {
        w_on = std::atof(argv[7]);
    }
    if (argc >= 9) {
        w_off = std::atof(argv[8]);
    }
    if (argc >= 10) {
        merge_dist = std::atof(argv[9]);
    }
    if (argc >= 11) {
        max_missed = std::atoi(argv[10]);
    }
    if (argc >= 12) {
        match_dist = std::atof(argv[11]);
    }
    if (argc >= 13) {
        k_high = std::atof(argv[12]);
    }
    if (argc >= 14) {
        k_low = std::atof(argv[13]);
    }
    if (argc >= 15) {
        min_w = std::atoi(argv[14]);
    }
    if (argc >= 16) {
        min_h = std::atoi(argv[15]);
    }
    if (argc >= 17) {
        max_w = std::atoi(argv[16]);
    }
    if (argc >= 18) {
        max_h = std::atoi(argv[17]);
    }
    if (argc >= 19) {
        occupancy_thr = std::atof(argv[18]);
    }
    if (argc >= 20) {
        iou_merge_thr = std::atof(argv[19]);
    }
    if (argc >= 21) {
        match_iou_thr = std::atof(argv[20]);
    }
    if (argc >= 22) {
        nms_iou_thr = std::atof(argv[21]);
    }
    if (argc >= 23) {
        min_confirmations = std::atoi(argv[22]);
    }
    if (argc >= 24) {
        max_size_change_ratio = std::atof(argv[23]);
    }
    if (argc >= 25) {
        min_circularity = std::atof(argv[24]);
    }
    if (argc >= 26) {
        min_solidity = std::atof(argv[25]);
    }
    if (argc >= 27) {
        max_aspect_ratio = std::atof(argv[26]);
    }
    if (argc >= 28) {
        min_speed_pxps = std::atof(argv[27]);
    }
    if (argc >= 29) {
        min_speed_frames = std::atoi(argv[28]);
    }
    
    std::cout << "\n配置信息:" << std::endl;
    std::cout << "显示帧率: " << display_fps << " FPS" << std::endl;
    std::cout << "检测参数: decay=" << decay << ", thresh_high=" << thresh_high << ", thresh_low=" << thresh_low
              << ", min_area=" << min_area << ", max_area=" << max_area
              << ", kernel=" << kernel_size << ", w_on=" << w_on << ", w_off=" << w_off
              << ", merge_dist=" << merge_dist << ", max_missed=" << max_missed
              << ", match_dist=" << match_dist << ", k_high=" << k_high << ", k_low=" << k_low
              << ", min_w=" << min_w << ", min_h=" << min_h << ", max_w=" << max_w << ", max_h=" << max_h
              << ", occupancy_thr=" << occupancy_thr << ", iou_merge_thr=" << iou_merge_thr
              << ", match_iou_thr=" << match_iou_thr << ", nms_iou_thr=" << nms_iou_thr
              << ", min_confirmations=" << min_confirmations << ", max_size_change_ratio=" << max_size_change_ratio
              << ", min_circularity=" << min_circularity << ", min_solidity=" << min_solidity << ", max_aspect_ratio=" << max_aspect_ratio
              << ", min_speed_pxps=" << min_speed_pxps << ", min_speed_frames=" << min_speed_frames
              << std::endl;
    
    // 注册信号处理函数
    signal(SIGINT, signalHandler);
    signal(SIGTERM, signalHandler);
    
    // 创建组件实例
    const uint16_t VENDOR_ID = 0x1d6b;
    const uint16_t PRODUCT_ID = 0x0105;
    
    hv::HV_Camera camera(VENDOR_ID, PRODUCT_ID);
    EVSDisplayQueue evs_display_queue;
    DisplayManager display_manager(evs_display_queue);
    ParticleDetector particle_detector(HV_EVS_WIDTH, HV_EVS_HEIGHT);
    particle_detector.setDecay(decay);
    particle_detector.setThresholds(thresh_high, thresh_low);
    particle_detector.setAreaRange(min_area, max_area);
    particle_detector.setKernelSize(kernel_size);
    particle_detector.setPolarityWeights(w_on, w_off);
    particle_detector.setMergeDist(merge_dist);
    particle_detector.setTrackParams(max_missed, match_dist);
    particle_detector.setAdaptiveK(k_high, k_low);
    particle_detector.setSizeLimits(min_w, min_h, max_w, max_h);
    particle_detector.setOccupancyThreshold(occupancy_thr);
    particle_detector.setIouThresholds(iou_merge_thr, match_iou_thr, nms_iou_thr);
    particle_detector.setConfirmation(min_confirmations, max_size_change_ratio);
    particle_detector.setShapeThresholds(min_circularity, min_solidity, max_aspect_ratio);
    particle_detector.setSpeedThreshold(min_speed_pxps, min_speed_frames);
    
    display_manager.setDisplayFPS(display_fps);
    
    // 打开相机
    std::cout << "\n正在打开相机..." << std::endl;
    if (!camera.open()) {
        std::cerr << "错误: 无法打开HV相机" << std::endl;
        std::cerr << "请确保：" << std::endl;
        std::cerr << "1. 相机已正确连接到USB端口" << std::endl;
        std::cerr << "2. 相机驱动已正确安装" << std::endl;
        std::cerr << "3. 相机没有被其他程序占用" << std::endl;
        return -1;
    }
    
    std::cout << "相机打开成功！" << std::endl;
    
    // 启动显示线程
    std::thread display_thread(displayWorkerThread, 
                              std::ref(evs_display_queue),
                              std::ref(particle_detector),
                              display_fps);
    
    // 绑定事件回调函数
    auto event_callback = [&](const std::vector<Metavision::EventCD>& events) {
        try {
            // 显示处理
            display_manager.addEvents(events);
            particle_detector.addEvents(events);
        } catch (const std::exception& e) {
            std::cerr << "事件处理错误: " << e.what() << std::endl;
        }
    };
    
    // 启动事件采集
    std::cout << "正在启动事件采集..." << std::endl;
    if (!camera.startEventCapture(event_callback)) {
        std::cerr << "错误: 无法启动事件采集" << std::endl;
        g_running = false;
        display_thread.join();
        camera.close();
        return -1;
    }
    
    std::cout << "\n系统启动完成！按 'q' 退出" << std::endl;
    
    // 主循环：等待用户操作或程序退出
    while (g_running) {
        std::this_thread::sleep_for(std::chrono::milliseconds(100));
    }
    
    // 清理资源
    std::cout << "\n正在停止采集..." << std::endl;
    camera.stopEventCapture();
    
    // 等待显示线程结束
    display_thread.join();
    
    // 关闭相机
    camera.close();
    std::cout << "相机已关闭" << std::endl;
    
    std::cout << "\n=== 程序结束 ===" << std::endl;
    
    return 0;
}
