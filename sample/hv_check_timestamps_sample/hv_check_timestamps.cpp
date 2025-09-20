#include <iostream>
#include <fstream>
#include <sstream>
#include <vector>
#include <algorithm>

struct BigGap {
    size_t index;
    long long diff;
};

int main(int argc, char* argv[]) {
    /* 1. 检查命令行参数 *******************************/
    if (argc != 2) {
        std::cerr << "Usage: " << argv[0] << " <CSV_filename>\n";
        std::cerr << "Example: " << argv[0] << " data.csv\n";
        return 1;
    }
    
    std::string inputFile = argv[1];

    /* 2. 读取 CSV *********************************************/
    std::ifstream in(inputFile);
    if (!in) {
        std::cerr << "Error: Cannot open file " << inputFile << "\n";
        return 1;
    }

    std::vector<long long> timestamps;
    std::vector<size_t> smallDiffRows;              // 存放"时间戳差值 < 50"的行号
    std::string line;
    size_t row = 0;
    while (std::getline(in, line)) {
        ++row;
        if (line.empty() || line[0] == '%') continue;
        std::stringstream ss(line);
        std::string field;
        int col = 0;
        while (std::getline(ss, field, ',')) {
            ++col;
            if (col == 4) {
                try {
                    long long v = std::stoll(field);
                    timestamps.push_back(v);
                } catch (...) {}
                break;
            }
        }
    }

    if (timestamps.empty()) {
        std::cerr << "Warning: No timestamps found.\n";
        return 0;
    }

    /* 3. 计算差值与大间隔 ************************************/
    std::sort(timestamps.begin(), timestamps.end());
    timestamps.erase(std::unique(timestamps.begin(), timestamps.end()), timestamps.end());

    std::vector<long long> diffs;
    for (size_t i = 1; i < timestamps.size(); ++i) {
        long long diff = timestamps[i] - timestamps[i - 1];
        diffs.push_back(diff);
        if (diff < 50) {
            smallDiffRows.push_back(i + 1);  // 1-based 行号
        }
    }

    std::vector<BigGap> bigGaps;
    for (size_t i = 0; i < diffs.size(); ++i)
        if (diffs[i] > 1000)
            bigGaps.push_back({i + 2, diffs[i]});   // 1-based 行号

    /* 4. 写输出文件 *******************************************/
    const std::string outputFile = "timestamps.txt";
    std::ofstream out(outputFile);
    if (!out) {
        std::cerr << "Error: Cannot create output file " << outputFile << "\n";
        return 1;
    }

    /* 4-a 大间隔信息 */
    if (!bigGaps.empty()) {
        out << "Large gaps (> 1000 s) detected:\n";
        out << "Row,Gap (seconds)\n";
        for (const auto& g : bigGaps)
            out << g.index << "," << g.diff << "\n";
        out << "\n";
    } else {
        out << "No large gaps detected.\n\n";
    }

    /* 4-b 时间戳差值 < 50 的异常事件信息 */
    out << "Rows where timestamp difference < 50:\n";
    if (smallDiffRows.empty()) {
        out << "  (none)\n";
    } else {
        for (size_t r : smallDiffRows) out << "  " << r << "\n";
        out << "Maximum row index among them: " << *std::max_element(smallDiffRows.begin(), smallDiffRows.end()) << "\n";
    }
    out << "\n";

    /* 4-c 常规时间戳列表 */
    out << "Index,Timestamp,Difference (microsecond)\n";
    out << "1," << timestamps[0] << ",N/A\n";
    for (size_t i = 0; i < diffs.size(); ++i)
        out << i + 2 << "," << timestamps[i + 1] << "," << diffs[i] << "\n";

    std::cout << "Done. Output written to: " << outputFile << "\n";
    return 0;
}

