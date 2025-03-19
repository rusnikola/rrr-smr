/******************************************************************************
 * Copyright (c) 2016-2017, Pedro Ramalhete, Andreia Correia
 * Copyright (c) 2024-2025, MD Amit Hasan Arovi, Ruslan Nikolaev
 * All rights reserved.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions are met:
 *     * Redistributions of source code must retain the above copyright
 *       notice, this list of conditions and the following disclaimer.
 *     * Redistributions in binary form must reproduce the above copyright
 *       notice, this list of conditions and the following disclaimer in the
 *       documentation and/or other materials provided with the distribution.
 *     * Neither the name of Concurrency Freaks nor the
 *       names of its contributors may be used to endorse or promote products
 *       derived from this software without specific prior written permission.
 *
 * THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS "AS IS" AND
 * ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE IMPLIED
 * WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE ARE
 * DISCLAIMED. IN NO EVENT SHALL <COPYRIGHT HOLDER> BE LIABLE FOR ANY
 * DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES
 * (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES;
 * LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND
 * ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT
 * (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE OF THIS
 * SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
 ******************************************************************************
 */
#ifndef _BENCHMARK_LISTS_H_
#define _BENCHMARK_LISTS_H_

#include <atomic>
#include <chrono>
#include <thread>
#include <string>
#include <vector>
#include <algorithm>
#include <random>
#include "HarrisMichaelLinkedListEBR.hpp" 
#include "DWHarrisMichaelLinkedListEBR.hpp"
#include "HarrisMichaelLinkedListHP.hpp" 
#include "DWHarrisMichaelLinkedListHP.hpp"
#include "MSQueueEBR.hpp" 
#include "MSQueueABAEBR.hpp"
#include "ModQueueABAEBR.hpp" 
#include "MSQueueHP.hpp"
#include "MSQueueABAHP.hpp" 
#include "ModQueueABAHP.hpp"


using namespace std;
using namespace chrono;

class BenchmarkLists {

private:
    struct UserData  {
        long long seq;
        UserData(long long lseq) {
            this->seq = lseq;
        }
        UserData() {
            this->seq = -2;
        }
        UserData(const UserData &other) : seq(other.seq) { }

        bool operator < (const UserData& other) const {
            return seq < other.seq;
        }
        bool operator == (const UserData& other) const {
            return seq == other.seq;
        }
        long long getSeq() const {
            return seq;
    	}
    };

    struct Result {
        nanoseconds nsEnq = 0ns;
        nanoseconds nsDeq = 0ns;
        long long numEnq = 0;
        long long numDeq = 0;
        long long totOpsSec = 0;

        Result() { }

        Result(const Result &other) {
            nsEnq = other.nsEnq;
            nsDeq = other.nsDeq;
            numEnq = other.numEnq;
            numDeq = other.numDeq;
            totOpsSec = other.totOpsSec;
        }

        bool operator < (const Result& other) const {
            return totOpsSec < other.totOpsSec;
        }
    };

    int numThreads;
    size_t payloadBytes;

public:
    BenchmarkLists(int numThreads, size_t payloadBytes) : numThreads(numThreads), payloadBytes(payloadBytes) {}

    template<typename L, size_t N = 1>
    std::pair<long long, long long> benchmark(const seconds testLengthSeconds, const int numRuns, const int numElements, size_t payloadBytes, const string& ds, bool isPairwise, int recyclingPercentage) {
        long long ops[numThreads][numRuns];
        long long mem[numThreads][numRuns];
        atomic<bool> quit = { false };
        atomic<bool> startFlag = { false };
        L* list = nullptr;
        string className;
        
        UserData* udarray[numElements];
        
        for (size_t i = 0; i < numElements; ++i) {
        	 udarray[i] = new UserData(i);
    	}    
        
        size_t barray[numElements];
		        
        auto rw_lambda = [this,&quit,&startFlag,&list,&udarray,&numElements, &barray, &ds, &isPairwise, &recyclingPercentage](long long *ops, const int tid) {
            long long numOps = 0;
            size_t listIndex = 0;

            uint64_t r = rand();
			std::mt19937_64 gen_k(r);
			std::mt19937_64 gen_p(r+1);
	
			auto sliceSize = (unsigned int) (numElements/numThreads);		
            while (!startFlag.load()) { }
            while (!quit.load(std::memory_order_acquire)) {
                r = gen_k();
				auto seedSize = (unsigned int) (r%sliceSize);
				auto ix = ((tid * sliceSize) + seedSize);
				int op = gen_p()%100;
				if(isPairwise){
					if(op < recyclingPercentage){
						listIndex = (barray[ix] + 1) % N;
				        if(list->move(udarray[ix], tid, barray[ix], listIndex)){
					        barray[ix] = listIndex;
					        numOps += 1;
				        }
				        numOps += 1;
					} else {
						if(ds == "queue"){
							list->insert(udarray[ix], tid, barray[ix]);
							list->remove(udarray[ix], tid, barray[ix]);
                			numOps += 2;
						} else{
							list->remove(udarray[ix], tid, barray[ix]);
							list->insert(udarray[ix], tid, barray[ix]);
                			numOps += 2;
						}
					}
				} else{
					if(op < recyclingPercentage){
						listIndex = (barray[ix] + 1) % N;
				        if(list->move(udarray[ix], tid, barray[ix], listIndex)){
					        barray[ix] = listIndex;
					        numOps += 1;
				        }
				        numOps += 1;
					} else {
						op = gen_p()%100;
						if(op < 50){
							list->remove(udarray[ix], tid, barray[ix]);
						} else {
							list->insert(udarray[ix], tid, barray[ix]);
						}
                		numOps += 1;
					}
				}
            }
            *ops = numOps;
        };

        for (int irun = 0; irun < numRuns; irun++) {
            list = new L(numThreads, payloadBytes);
        	
            for (size_t i = 0; i < numElements; i++) barray[i] = 0;
            for (int i = 0; i < numElements; i++) list->insert(udarray[i], 0);
            
            if (irun == 0) {
            	cout<<endl<<endl << "######### Benchmark:   " << list->className() << "   #########  \n\n";
            	className = list->className();
            }
            thread rwThreads[numThreads];
            for (int tid = 0; tid < numThreads; tid++) rwThreads[tid] = thread(rw_lambda, &ops[tid][irun], tid);
            startFlag.store(true);
            
            this_thread::sleep_for(testLengthSeconds);
            quit.store(true, std::memory_order_release);
            for (int tid = 0; tid < numThreads; tid++) rwThreads[tid].join();
            quit.store(false);
            startFlag.store(false);
            for (int tid = 0; tid < numThreads; tid++) mem[tid][irun] = list->calculate_space(tid);
            
            
            if(ds == "list"){
            	for (int i = 0; i < numElements; i++) {
					list->remove(udarray[i], 0, barray[i]);
				}
			}
			
            delete list;
        }

        for (int i = 0; i < numElements; i++) delete udarray[i];

        vector<long long> agg(numRuns);
        for (int irun = 0; irun < numRuns; irun++) {
            agg[irun] = 0;
            for (int tid = 0; tid < numThreads; tid++) {
                agg[irun] += ops[tid][irun];
            }
        }
        
        vector<long long> mem_agg(numRuns);
        for (int irun = 0; irun < numRuns; irun++) {
            long long agg = 0;
            for (int tid = 0; tid < numThreads; tid++) {
                agg += mem[tid][irun];
            }
            mem_agg[irun] = (long long) agg / 1000;
        }

        sort(agg.begin(),agg.end());
        auto maxops = agg[numRuns-1] / testLengthSeconds.count();
        auto minops = agg[0] / testLengthSeconds.count();
        auto medianops = agg[numRuns/2] / testLengthSeconds.count();
        auto delta = (long)(100.*(maxops-minops) / ((double)medianops));
        
        sort(mem_agg.begin(),mem_agg.end());
        auto mem_maxops = mem_agg[numRuns-1];
        auto mem_minops = mem_agg[0];
        auto mem_medianops = mem_agg[numRuns/2];
        auto mem_delta = (mem_medianops == 0) ? 0 : (long)(100. * (mem_maxops - mem_minops) / ((double)mem_medianops));
        
        for (int irun = 0; irun < numRuns; irun++) {
        	std::cout << "\n\n#### RUN " << (irun + 1) << " RESULT: ####" << "\n";
        	std::cout << "\n----- Benchmark=" << className <<   "   numElements=" << numElements << "   numThreads=" << numThreads << "   testLength=" << testLengthSeconds.count() << "s -----\n";
        	
        	std::cout << "Ops/sec = " << (agg[irun] / testLengthSeconds.count()) << "\n";
        	std::cout << "memory_usage (Bytes) = " << mem_agg[irun] << "\n";
        }
        
        std::cout << "\n\n###### MEDIAN RESULT FOR ALL " << numRuns << " RUNS: ######" << "\n";
      	std::cout << "\n----- Benchmark=" << className <<   "   numElements=" << numElements << "   numThreads=" << numThreads << "   testLength=" << testLengthSeconds.count() << "s -----\n";
        std::cout << "Ops/sec = " << medianops << "   delta = " << delta << "%   min = " << minops << "   max = " << maxops << "\n";
        std::cout << "memory_usage (Bytes) = " << mem_medianops << "   delta = " << mem_delta << "%   min = " << mem_minops << "   max = " << mem_maxops << "\n";
         return {medianops, mem_medianops};
    }

public:

    static void allThroughputTests(const string& ds, int testLengthSeconds, bool isPairwise, size_t payloadBytes, int numElements, int recyclingPercentage) {
        vector<int> threadList = { 1, 16, 32, 64, 96, 128 };
        //vector<int> threadList = { 1, 2, 4, 6, 8 }; //for laptop
        const int numRuns = 5;
        const seconds testLength = seconds(testLengthSeconds);
        long long ops[8][threadList.size()];
	    long long mem[8][threadList.size()];

        if(ds == "list"){
        	const int LHP = 0;
		    const int DWLHP = 1;
		    const int LEBR = 2;
		    const int DWLEBR = 3;
		    
            for (int ithread = 0; ithread < threadList.size(); ithread++) {
                auto nThreads = threadList[ithread];
                BenchmarkLists bench(nThreads, payloadBytes);
                
                auto result1 = bench.benchmark<HarrisMichaelLinkedListHP<UserData, 2>, 2>(testLength, numRuns, numElements, payloadBytes, ds, isPairwise, recyclingPercentage);
                ops[LHP][ithread] = result1.first;
                mem[LHP][ithread] = result1.second;
                
                auto result2 = bench.benchmark<DWHarrisMichaelLinkedListHP<UserData, 2>, 2>(testLength, numRuns, numElements, payloadBytes, ds, isPairwise, recyclingPercentage);
                ops[DWLHP][ithread] = result2.first;
                mem[DWLHP][ithread] = result2.second;
                
                auto result3 = bench.benchmark<HarrisMichaelLinkedListEBR<UserData, 2>, 2>(testLength, numRuns, numElements, payloadBytes, ds, isPairwise, recyclingPercentage);
                ops[LEBR][ithread] = result3.first;
                mem[LEBR][ithread] = result3.second;
                
                auto result4 = bench.benchmark<DWHarrisMichaelLinkedListEBR<UserData, 2>, 2>(testLength, numRuns, numElements, payloadBytes, ds, isPairwise, recyclingPercentage);
                ops[DWLEBR][ithread] = result4.first;
                mem[DWLEBR][ithread] = result4.second;
            }
        } else if(ds == "queue") {

		    const int MSQUEUEHP = 0;
			const int MSQUEUEABAHP = 1;
			const int MODQUEUEABAHP = 2;
			const int MSQUEUEEBR = 3;
			const int MSQUEUEABAEBR = 4;
			const int MODQUEUEABAEBR = 5;
		    
            for (int ithread = 0; ithread < threadList.size(); ithread++) {
                auto nThreads = threadList[ithread];
                BenchmarkLists bench(nThreads, payloadBytes);

                auto result1 = bench.benchmark<MSQueueHP<UserData, 2>, 2>(testLength, numRuns, numElements, payloadBytes, ds, isPairwise, recyclingPercentage);
                ops[MSQUEUEHP][ithread] = result1.first;
                mem[MSQUEUEHP][ithread] = result1.second;
                
                auto result2 = bench.benchmark<MSQueueABAHP<UserData, 2>, 2>(testLength, numRuns, numElements, payloadBytes, ds, isPairwise, recyclingPercentage);
                ops[MSQUEUEABAHP][ithread] = result2.first;
                mem[MSQUEUEABAHP][ithread] = result2.second;
                
                auto result3 = bench.benchmark<ModQueueABAHP<UserData, 2>, 2>(testLength, numRuns, numElements, payloadBytes, ds, isPairwise, recyclingPercentage);
                ops[MODQUEUEABAHP][ithread] = result3.first;
                mem[MODQUEUEABAHP][ithread] = result3.second;
                
                auto result4 = bench.benchmark<MSQueueEBR<UserData, 2>, 2>(testLength, numRuns, numElements, payloadBytes, ds, isPairwise, recyclingPercentage);
                ops[MSQUEUEEBR][ithread] = result4.first;
                mem[MSQUEUEEBR][ithread] = result4.second;
                
                auto result5 = bench.benchmark<MSQueueABAEBR<UserData, 2>, 2>(testLength, numRuns, numElements, payloadBytes, ds, isPairwise, recyclingPercentage);
                ops[MSQUEUEABAEBR][ithread] = result5.first;
                mem[MSQUEUEABAEBR][ithread] = result5.second;
                
                auto result6 = bench.benchmark<ModQueueABAEBR<UserData, 2>, 2>(testLength, numRuns, numElements, payloadBytes, ds, isPairwise, recyclingPercentage);
                ops[MODQUEUEABAEBR][ithread] = result6.first;
                mem[MODQUEUEABAEBR][ithread] = result6.second;
            }
		}
		
		cout<<"\n\nFINAL RESULTS (FOR CHARTS):"<<endl<<endl;
        cout << "\nResults in ops per second for numRuns=" << numRuns << ",  length=" << testLength.count() << "s \n";
        std::cout << "\nNumber of elements: " << numElements << "\n\n";
            int classSize;
            if(ds == "list"){
            	classSize = 4;
            	cout << "Threads, HarrisMichaelListHP, DWHarrisMichaelListHP, HarrisMichaelListEBR, DWHarrisMichaelListEBR, HarrisMichaelListHP_Memory_Usage, DWHarrisMichaelListHP_Memory_Usage, HarrisMichaelListEBR_Memory_Usage, DWHarrisMichaelListEBR_Memory_Usage\n";
            } else if(ds == "queue") {
            	classSize = 6;
            	cout << "Threads, MSQueueHP, MSQueueABAHP, ModQueueHP, MSQueueEBR, MSQueueABAEBR, ModQueueEBR, MSQueueHP_Memory_Usage, MSQueueABAHP_Memory_Usage, ModQueueHP_Memory_Usage, MSQueueEBR_Memory_Usage, MSQueueABAEBR_Memory_Usage, ModQueueEBR_Memory_Usage\n";
			} 
			
            for (int ithread = 0; ithread < threadList.size(); ithread++) {
                auto nThreads = threadList[ithread];
                cout << nThreads << ", ";
                for (int il = 0; il < classSize; il++) {
                    cout << ops[il][ithread] << ", ";
                }
                for (int il = 0; il < classSize; il++) {
                    cout << mem[il][ithread] << ", ";
                }
                cout << "\n";
            }
    }
};

#endif
