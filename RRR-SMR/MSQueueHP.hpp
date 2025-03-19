/******************************************************************************
 * Copyright (c) 2014-2016, Pedro Ramalhete, Andreia Correia
 * Copyright (c) 2024-2025, MD Amit Hasan Arovi, Ruslan Nikolaev
 * All rights reserved.
 *
 * Based on the original MichaelScottQueue.hpp but has been fully re-written
 * by MD Amit Hasan Arovi and Ruslan Nikolaev for the RRR-SMR work.
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

#ifndef _MSQUEUE_HP_H_
#define _MSQUEUE_HP_H_

#include <atomic>
#include <thread>
#include <forward_list>
#include <set>
#include <iostream>
#include <string>
#include "HazardPointers.hpp"

template <typename T, size_t N = 1>
class MSQueueHP {
private:
    struct Node {
        size_t value;
        std::atomic<Node*> next;
        size_t* myArray;
        
        Node(size_t payloadSize) {
            size_t arraySize = payloadSize / sizeof(size_t);
            myArray = new size_t[arraySize];

            for (size_t i = 0; i < arraySize; i++) {
                myArray[i] = i;
            }
        }
        
         ~Node() {
            delete[] myArray;
        }
    };

    struct Queue {
        std::atomic<Node*> Head;
        std::atomic<Node*> Tail;
    };

    Queue Q[N];
    
    const int maxThreads;
    const size_t payloadSize;
    HazardPointers<Node> hp {2, maxThreads};
    const int kHpTail = 0;
    const int kHpHead = 0;
    const int kHpNext = 1;

public:
    MSQueueHP(const int maxThreads, const size_t payloadBytes) : maxThreads{maxThreads}, payloadSize{payloadBytes} {
        for (size_t i = 0; i < N; ++i) {
            Node* node = new Node(payloadSize);
            node->next.store(nullptr);
            Q[i].Head.store(node);
            Q[i].Tail.store(node);
        }
    }

    ~MSQueueHP() {
        for (size_t i = 0; i < N; i++) {
            while (remove(nullptr, 0, i)); // Drain the queue
        }
    }
    
    std::string className() { return "MSQueueHP"; }

    void insert(T* key, const int tid, size_t listIndex = 0) {
        Node* node = new Node(payloadSize);
        node->value = key->getSeq();
        node->next.store(nullptr);
        Node* tail;
        while (true) {
            tail = hp.protect(kHpTail, Q[listIndex].Tail, tid);
            Node* next = tail->next.load();

            if (tail == Q[listIndex].Tail.load()) {
                if (next == nullptr) {
                    if (tail->next.compare_exchange_strong(next, node)) {
                        break;
                    }
                } else {
                    Q[listIndex].Tail.compare_exchange_strong(tail, next);
                }
            }
        }
        Q[listIndex].Tail.compare_exchange_strong(tail, node);
        hp.clear(tid);
    }

    bool remove(T* key, const int tid, size_t listIndex = 0) {
        Node* head;
        hp.take_snapshot(tid);
        while (true) {
            head = hp.protect(kHpHead, Q[listIndex].Head, tid);
            Node* tail = Q[listIndex].Tail.load();
            Node* next = hp.protectPtr(kHpNext, head->next.load(), tid);

            if (head == Q[listIndex].Head.load()) {
                if (head == tail) {
                    if (next == nullptr) {
                        hp.clear(tid);
                        return false;
                    }
                    Q[listIndex].Tail.compare_exchange_strong(tail, next);
                } else {
                    if (Q[listIndex].Head.compare_exchange_strong(head, next)) {
                        hp.retire(head, tid);
                        hp.clear(tid);
                        break;
                    }
                }
            }
        }
        return true;
    }

    bool move(T* key, const int tid, size_t list_from = 0, size_t list_to = 0){
        if (remove(key, tid, list_from)){
            insert(key, tid, list_to);
            return true;
        } else {
            return false;
        }
    }

    long long calculate_space(const int tid){
        size_t arraySize = payloadSize / sizeof(size_t);
        size_t nodeSize = sizeof(Node) + (arraySize * sizeof(size_t));
        return hp.cal_space(nodeSize, tid);
    }
};
#endif
