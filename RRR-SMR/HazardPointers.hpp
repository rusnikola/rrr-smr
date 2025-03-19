/******************************************************************************
 * Copyright (c) 2014-2017, Pedro Ramalhete, Andreia Correia
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

#ifndef _HAZARD_POINTERS_H_
#define _HAZARD_POINTERS_H_

#include <atomic>
#include <iostream>
#include <vector>

template<typename T>
class HazardPointers {

private:
    static const int      HP_MAX_THREADS = 128;
    static const int      HP_MAX_HPS = 5;   
    static const int      HP_THRESHOLD_R = 32; 
    static const int      MAX_RETIRED = HP_MAX_THREADS * HP_MAX_HPS;

    const int             maxHPs;
    const int             maxThreads;

    typedef struct hp_node_s {
        struct hp_node_s* next;
        struct hp_node_s* prev;
        T* node;

        hp_node_s(T * _node = nullptr): node(_node) {}
    } hp_node_t;

    typedef struct retired_node_controller {
        std::atomic<T*> hp[HP_MAX_HPS];
        hp_node_t *retiredList;
        size_t retiredCount;
        ssize_t sum;
        size_t count;
        ssize_t space;
        alignas(128) char pad[0];
    } retired_node_controller_t;

    retired_node_controller_t *rnc;

public:
    HazardPointers(int maxHPs = HP_MAX_HPS, int maxThreads = HP_MAX_THREADS) : maxHPs{maxHPs}, maxThreads{maxThreads} {
        rnc = static_cast<retired_node_controller_t*>(aligned_alloc(128, sizeof(retired_node_controller_t) * HP_MAX_THREADS));
        for (int it = 0; it < HP_MAX_THREADS; it++) {
            for (int ihp = 0; ihp < HP_MAX_HPS; ihp++) {
                rnc[it].hp[ihp].store(nullptr, std::memory_order_relaxed);
            }
            rnc[it].retiredList = nullptr;
            rnc[it].retiredCount = 0;
            rnc[it].sum = 0;
            rnc[it].count = 0;
            rnc[it].space = 0;
        }
    }

    ~HazardPointers() {
        for (int tid = 0; tid < maxThreads; tid++) {
            hp_node_t* current_node = rnc[tid].retiredList;
            
            while (current_node != nullptr) {
                auto obj = current_node->node;
                bool canDelete = true;
                for (int tidx = 0; tidx < maxThreads && canDelete; tidx++) {
                    for (int ihp = 0; ihp < maxHPs; ihp++) {
                        if (rnc[tidx].hp[ihp].load() == obj) {
                            canDelete = false;
                            break;
                        }
                    }
                }
                hp_node_t *next = current_node->next;
                if (canDelete) {
                    delete obj;
                    if (current_node->prev) {
                       current_node->prev->next = current_node->next;
                    } else {
                       rnc[tid].retiredList = current_node->next;
                    }
                    if (current_node->next) {
                       current_node->next->prev = current_node->prev;
                    }
                    delete current_node;
                }
                current_node = next;
            }
        }
        free(rnc);
    }

    /**
     * Progress Condition: wait-free bounded (by maxHPs)
     */
    inline void clear(const int tid) {
        for (int ihp = 0; ihp < maxHPs; ihp++) {
            rnc[tid].hp[ihp].store(nullptr, std::memory_order_release);
        }
    }

    /**
     * Progress Condition: wait-free population oblivious
     */
    inline void clearOne(int ihp, const int tid) {
        rnc[tid].hp[ihp].store(nullptr, std::memory_order_release);
    }

    /**
     * Progress Condition: lock-free
     */
    inline T* protect(int index, const std::atomic<T*>& atom, const int tid) {
        T* n = nullptr;
        T* ret;
        while ((ret = atom.load()) != n) {
            rnc[tid].hp[index].store((T*)((size_t) ret & ~3ULL));
            n = ret;
        }
        return ret;
    }

    /**
     * This returns the same value that is passed as ptr, which is sometimes useful
     * Progress Condition: wait-free population oblivious
     */
    inline T* protectPtr(int index, T* ptr, const int tid) {
        rnc[tid].hp[index].store(ptr);
        return ptr;
    }

    /**
     * This returns the same value that is passed as ptr, which is sometimes useful
     * Progress Condition: wait-free population oblivious
     */
    inline T* protectPtrRelease(int index, T* ptr, const int tid) {
        rnc[tid].hp[index].store(ptr, std::memory_order_release);
        return ptr;
    }

    void retire(T* ptr, const int tid) {
        rnc[tid].space++;
        hp_node_t* n = new hp_node_t(ptr);
        if (!n) {
            std::cerr << "ran out of memory!\n";
            exit(1);
        }
        // add to the list
        n->prev = nullptr;
        n->next = rnc[tid].retiredList;
        if (n->next) {
           n->next->prev = n;
        }
        rnc[tid].retiredList = n;
        rnc[tid].retiredCount++;
        if (rnc[tid].retiredCount < HP_THRESHOLD_R) return;
        hp_node_t* current_node = rnc[tid].retiredList;

        while (current_node != nullptr) {
            auto obj = current_node->node;
            bool canDelete = true;
            for (int tidx = 0; tidx < maxThreads && canDelete; tidx++) {
                for (int ihp = 0; ihp < maxHPs; ihp++) {
                    if (rnc[tidx].hp[ihp].load() == obj) {
                        canDelete = false;
                        break;
                    }
                }
            }
            hp_node_t *next = current_node->next;
            if (canDelete) {
                rnc[tid].space--;
                delete obj;
                if (current_node->prev) {
                   current_node->prev->next = current_node->next;
                } else {
                   rnc[tid].retiredList = current_node->next;
                }
                if (current_node->next) {
                   current_node->next->prev = current_node->prev;
                }
                rnc[tid].retiredCount--;
                delete current_node;
            }
            current_node = next;
        }
    }

    inline void take_snapshot(const int tid){
        rnc[tid].sum += rnc[tid].space;
        rnc[tid].count++;
    }

    inline long long cal_space(size_t size, const int tid){
        return (long long) (rnc[tid].sum * (ssize_t) size * 1000) / ((ssize_t) rnc[tid].count);
    }
};

#endif /* _HAZARD_POINTERS_H_ */
