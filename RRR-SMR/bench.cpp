/*
 * main.cpp
 *
 *  Created on: Apr 23, 2016
 *      Author: pramalhe
 *
 *  Modified and adopted for RRR-SMR by MD Amit Hasan Arovi
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
		cerr << "Usage: ./bench <list|queue> <test_length_seconds> <P|R> <payload_size> <element_size> <recycling_percentage>\n\n"
		 << "  - P : Pairwise operations (remove followed by insert)\n"
		 << "  - R : Random operations (non-pairwise; randomly selects remove or insert)\n\n"
		 << "Example: "
		 << "  ./bench list 60 P 128B 4096 50%\n"
		 << endl;
    	 return 1;
    }

    string ds = argv[1]; // "list", or "queue"
    int testLengthSeconds = stoi(argv[2]); // Test length in seconds
    string pairwise = argv[3]; // "P" for pairwise (remove operation followed by insert operation), "R" for non-pairwise (randomly select remove or insert operation)
    string payloadSize = argv[4]; // "0B (queue)", "128B (list)", or "64KB (queue, list)"
    int elementSize = stoi(argv[5]); // Key range: 128 (queue), 4096 (list)
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

    BenchmarkLists::allThroughputTests(ds, testLengthSeconds, isPairwise, payloadBytes, elementSize, recyclingPercentage);

    return 0;
}

