/*
 * main.cpp
 *
 *  Created on: Apr 23, 2016
 *      Author: pramalhe
 *
 *  Modified and adopted for RRR-SMR by Md Amit Hasan Arovi
 */

#include <thread>
#include <string>
#include <regex>
#include "BenchmarkLists.hpp"

int extractPercentage(const std::string& percentStr) {
    std::regex pattern(R"(^([0-9]{1,2}|100)%$)");
    std::smatch match;
    
    if (std::regex_match(percentStr, match, pattern)) {
        return std::stoi(match.str(1));
    }
    
    return -1;
}

int main(int argc, char* argv[]) {
    if (argc < 7) {
        cerr << "Usage: ./bench <list|queue|tree> <test_length_seconds> <P|R> <payload_size> <element_size> <recycling_percentage> [num_threads]\n\n"
         << "Arguments:\n"
         << "  <list|queue|tree>        : The data structure to test\n"
         << "  <test_length_seconds>    : Duration of the test in seconds\n"
         << "  <P|R>                    : 'P' for pairwise, 'R' for random operations\n"
         << "  <payload_size>           : Payload size (e.g., 0B, 128B, 1KB, 64KB)\n"
         << "  <element_size>           : Number of elements (e.g., 4096)\n"
         << "  <recycling_percentage>   : Percentage in format like '50%'\n"
         << "  [num_threads]            : (Optional) Number of threads to run (e.g., 64)\n\n"
         << "Examples:\n"
         << "  ./bench list 60 P 128B 4096 50%           # Uses default thread list\n"
         << "  ./bench queue 30 R 0B 256 10% 32          # Uses 32 threads only\n"
         << endl;
         return 1;
    }

    string ds = argv[1]; // "list", or "queue"
    int testLengthSeconds = stoi(argv[2]); // Test length in seconds
    string pairwise = argv[3]; // "P" for pairwise (remove operation followed by insert operation), "R" for non-pairwise (randomly select remove or insert operation)
    string payloadSize = argv[4]; // "0B (queue)", "128B (list)", or "64KB (queue, list)"
    int elementSize = stoi(argv[5]); // Key range: 256 (queue), 4096 (list)
    string percentageStr = argv[6]; 
    int recyclingPercentage = extractPercentage(percentageStr); // Recycling percentage (0 to 100)
    
     // Validate recycling percentage
     if (recyclingPercentage == -1) {
        cerr << "Error: Invalid recycling percentage format. Use a number between 0% and 100% (e.g., '50%')." << endl;
        return 1;
    }

    // Convert payload size to bytes
    size_t payloadBytes = 0;
    if (payloadSize == "0B") {
        payloadBytes = 0;
    } else if (payloadSize == "128B") {
        payloadBytes = 128;
    } else if (payloadSize == "64KB") {
        payloadBytes = 64 * 1024;
    } else if(payloadSize == "1KB"){
        payloadBytes = 1024;
    } else {
        cerr << "Invalid payload size. Use 0B, 128B, 1KB or 64KB." << endl;
        return 1;
    }

    // Determine if the test is pairwise or Random(non-pairwise)
    bool isPairwise = (pairwise == "P");

    int userThreadCount = -1;
    if (argc >= 8) {
        try {
            userThreadCount = stoi(argv[7]);
            if (userThreadCount <= 0) throw std::invalid_argument("Thread count must be positive");
        } catch (...) {
            cerr << "Invalid thread count provided in argument 7." << endl;
            return 1;
        }
    }

    BenchmarkLists::allThroughputTests(ds, testLengthSeconds, isPairwise, payloadBytes, elementSize, recyclingPercentage, userThreadCount);

    return 0;
}

