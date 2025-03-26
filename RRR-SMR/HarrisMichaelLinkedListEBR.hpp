/******************************************************************************
 * Copyright (c) 2016, Pedro Ramalhete, Andreia Correia
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

#ifndef _TIM_HARRIS_MAGED_MICHAEL_LINKED_LIST_EBR_H_
#define _TIM_HARRIS_MAGED_MICHAEL_LINKED_LIST_EBR_H_

#include <atomic>
#include <thread>
#include <iostream>
#include <string>
#include <vector>
#include "EBR.hpp"

template<typename T, size_t N = 1> 
class HarrisMichaelLinkedListEBR {

private:

    struct Node {
        T* key;
        std::atomic<Node*> next;
        T* myArray[0];

        Node(T* key, size_t payloadSize) : key{key}, next{nullptr} {
            size_t arraySize = payloadSize / sizeof(T*);

            for (size_t i = 0; i < arraySize; i++) {
                myArray[i] = key;
            }
        }
    };

    struct ListHead {
        alignas(128) std::atomic<Node*> list;
    };

    ListHead head[N];
    alignas(128) char pad[0];

    const int maxThreads;
    const size_t payloadSize;
    EBR<Node> ebr {maxThreads, payloadSize >= 1024, payloadSize >= 1024};

public:

    HarrisMichaelLinkedListEBR(const int maxThreads, const size_t payloadBytes) : maxThreads{maxThreads}, payloadSize{payloadBytes} {
        for (size_t i = 0; i < N; i++) {
            void* buffer = malloc(sizeof(Node) + payloadSize);
            head[i].list.store(new(buffer) Node(nullptr, payloadSize));
        }
    }

    ~HarrisMichaelLinkedListEBR() {
    }

    std::string className() { return "HarrisMichaelLinkedListEBR"; }

    bool insert(T* key, const int tid, size_t listIndex = 0)
    {
        Node *curr, *next;
        std::atomic<Node*> *prev;
        void* buffer = malloc(sizeof(Node) + payloadSize);
        Node* newNode = new(buffer) Node(key, payloadSize);
        ebr.read_lock(tid);
        while (true) {
            if (find(key, &prev, &curr, &next, tid, listIndex)) {
                free(newNode);
                ebr.read_unlock(tid);
                return false;
            }
            newNode->next.store(curr, std::memory_order_relaxed);
            Node *tmp = curr;
            if (prev->compare_exchange_strong(tmp, newNode)) {
                ebr.read_unlock(tid);
                return true;
            }
        }
    }

    bool remove(T* key, const int tid, size_t listIndex = 0)
    {
        Node *curr, *next;
        std::atomic<Node*> *prev;
        ebr.read_lock(tid);
        ebr.take_snapshot(tid);
        while (true) {
            if (!find(key, &prev, &curr, &next, tid, listIndex)) {
                ebr.read_unlock(tid);
                return false;
            }
            Node *tmp = next;
            if (!curr->next.compare_exchange_strong(tmp, getMarked(next))) {
                continue;
            }

            tmp = curr;
            if(prev->compare_exchange_strong(tmp, next)){ /* Unlink */
                ebr.smr_retire(curr, tid);
                ebr.read_unlock(tid);
            } else {
                ebr.read_unlock(tid);
            }                   
            
            return true;
        }
    }
    
    bool move(T* key, const int tid, size_t list_from = 0, size_t list_to = 0){
        bool ok = remove(key, tid, list_from);
        if (!ok){
            return false;
        }
         return insert(key, tid, list_to);
    }
    
    bool contains (T* key, const int tid, size_t listIndex = 0)
    {
        Node *curr, *next;
        std::atomic<Node*> *prev;
        ebr.read_lock(tid);
        bool isContains = find(key, &prev, &curr, &next, tid, listIndex);
        ebr.read_unlock(tid);
        return isContains;
    }
    
    long long calculate_space(const int tid){
        size_t arraySize = payloadSize / sizeof(T*);
        size_t nodeSize = sizeof(Node) + (arraySize * sizeof(T*));
        return ebr.cal_space(nodeSize, tid);
    }


private:

    bool find (T* key, std::atomic<Node*> **par_prev, Node **par_curr, Node **par_next, const int tid, size_t listIndex = 0)
    {
        std::atomic<Node*> *prev;
        Node *curr, *next;

     try_again:
        prev = &head[listIndex].list;
        curr = prev->load();
        while (true) {
        if (curr == nullptr) break;
            next = curr->next.load();
            if (prev->load() != curr) goto try_again;
            if (getUnmarked(next) == next) {
                if (curr->key != nullptr && !(*curr->key < *key)) {
                    *par_curr = curr;
                    *par_prev = prev;
                    *par_next = next;
                    return (*curr->key == *key);
                }
                prev = &curr->next;
            } else {
                Node *tmp = curr;
                next = getUnmarked(next);
                if (!prev->compare_exchange_strong(tmp, next)) {
                    goto try_again;
                }
                ebr.smr_retire(curr, tid);
            }
            curr = next;
        }
        *par_curr = curr;
        *par_prev = prev;
        *par_next = next;
        return false;
    }
    
    

    bool isMarked(Node * node) {
        return ((size_t) node & 0x1);
    }

    Node * getMarked(Node * node) {
        return (Node*)((size_t) node | 0x1);
    }

    Node * getUnmarked(Node * node) {
        return (Node*)((size_t) node & (~0x1));
    }
};

#endif /* _TIM_HARRIS_MAGED_MICHAEL_LINKED_LIST_EBR_H_ */
