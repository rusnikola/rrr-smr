/*
 * Copyright (c) 2024-2025, MD Amit Hasan Arovi, Ruslan Nikolaev
 * All Rights Reserved.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions
 * are met:
 * 1. Redistributions of source code must retain the above copyright
 *    notice, this list of conditions and the following disclaimer.
 * 2. Redistributions in binary form must reproduce the above copyright
 *    notice, this list of conditions and the following disclaimer in the
 *    documentation and/or other materials provided with the distribution.
 *
 * THIS SOFTWARE IS PROVIDED BY THE AUTHOR ``AS IS'' AND ANY EXPRESS
 * OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE IMPLIED
 * WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE ARE
 * DISCLAIMED. IN NO EVENT SHALL THE AUTHOR OR CONTRIBUTORS BE LIABLE
 * FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL
 * DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR
 * SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION)
 * HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT
 * LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY
 * OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF
 * SUCH DAMAGE.
 */

#ifndef _MODQUEUE_ABA_HP_H_
#define _MODQUEUE_ABA_HP_H_

#include <atomic>
#include <thread>
#include <forward_list>
#include <set>
#include <iostream>
#include <string>
#include "HazardPointers.hpp"
#include "HalfAtomic.hpp"

template<typename T, size_t N = 1> 
class ModQueueABAHP {

private:
    struct Node {
        std::atomic<Node *> next;
        size_t value;
        size_t myArray[0];

        Node(size_t payloadSize) {
            size_t arraySize = payloadSize / sizeof(size_t);

            for (size_t i = 0; i < arraySize; i++) {
                myArray[i] = i;
            }
        }
    };

    struct Queue {
        alignas(128) AtomicNode<Node> Head;
        alignas(128) AtomicNode<Node> Tail;
    };

    Queue Q[N];
    alignas(128) char pad[0];

    const int maxThreads;
    const size_t payloadSize;

    const int kHpTail = 0;
    const int kHpHead = 0;
    const int kHpNext = 1;

    HazardPointers<Node> hp {2, maxThreads, payloadSize >= 1024};

public:

    ModQueueABAHP (const int maxThreads, const size_t payloadBytes) : maxThreads{maxThreads}, payloadSize{payloadBytes} {
        for (size_t i = 0; i < N; i++) {
            void* buffer = malloc(sizeof(Node) + payloadSize);
            Node* node = new(buffer) Node(payloadSize);
            node->next.store((Node *) 0x1, std::memory_order_relaxed);
            Q[i].Head.half.tag.store(0, std::memory_order_relaxed);
            Q[i].Head.half.ptr.store(node, std::memory_order_relaxed);
            Q[i].Tail.half.tag.store(0, std::memory_order_relaxed);
            Q[i].Tail.half.ptr.store(node, std::memory_order_relaxed);
        }
    }

    ~ModQueueABAHP () {
        for (size_t i = 0; i < N; i++) {
            while (remove(nullptr, 0, i)); // Drain the queue
        }
    }

    std::string className() { return "ModQueueABAHP"; }

    size_t get_tail_tag(const int tid, size_t listIndex) {
        AtomicNode<Node>* tail = &Q[listIndex].Tail;
        AbaPtr<Node> curr, new_tail;
        while (true) {
            curr.tag = tail->half.tag.load();
            curr.ptr = hp.protectPtr(kHpTail, tail->half.ptr.load(), tid);
            if (tail->half.tag.load() != curr.tag)
                continue;
            Node* next = curr.ptr->next.load();
            if (tail->half.tag.load() == curr.tag) {
                if ((size_t) next & 0x1) { // nullptr
                    return (size_t) next;
                } else {
                    new_tail.ptr = next;
                    new_tail.tag = curr.tag + 1;
                    tail->full.compare_exchange_strong(curr, new_tail);
                }
            }
        }
    }

    // fresh enqueue (no need to find the max tag)
    void do_enqueue(T* key, const int tid, size_t listIndex, Node* node) {
        AbaPtr<Node> curr, new_node, new_tail, new_tmp;
        AtomicNode<Node>* tail = &Q[listIndex].Tail;

        while (true) {
            curr.tag = tail->half.tag.load();
            curr.ptr = hp.protectPtr(kHpTail, tail->half.ptr.load(), tid);
            if (tail->half.tag.load() != curr.tag)
                continue;
            Node* next = curr.ptr->next.load();
            if (tail->half.tag.load() == curr.tag) {           
                if ((size_t) next & 0x1) { // nullptr
                    node->next.store((Node* )((size_t) next + 0x2), std::memory_order_relaxed);
                    if (curr.ptr->next.compare_exchange_strong(next, node)) {
                        break;
                    }
                } else {
                    new_tail.ptr = next;
                    new_tail.tag = curr.tag + 1;
                    tail->full.compare_exchange_strong(curr, new_tail);
                }
            }
        }
        new_tmp.tag = curr.tag + 1;
        new_tmp.ptr = node;
        tail->full.compare_exchange_strong(curr, new_tmp);
    }

    // non-fresh enqueue (need to find the max tag)
    void do_enqueue(T* key, const int tid, size_t listIndex, Node* node, size_t tag) {
        AbaPtr<Node> curr, new_node, new_tail, new_tmp;
        AtomicNode<Node>* tail = &Q[listIndex].Tail;
             
        while (true) {
            curr.tag = tail->half.tag.load();
            curr.ptr = hp.protectPtr(kHpTail, tail->half.ptr.load(), tid);
            if (tail->half.tag.load() != curr.tag)
                continue;
            Node* next = curr.ptr->next.load();
            if (tail->half.tag.load() == curr.tag) {           
                if ((size_t) next & 0x1) { // nullptr
                    if (tag < (size_t) next + 0x2) {
                        node->next.store((Node* )((size_t) next + 0x2), std::memory_order_relaxed);
                    } else {
                        node->next.store((Node* ) tag, std::memory_order_relaxed);
                    }
                    if (curr.ptr->next.compare_exchange_strong(next, node)) {
                        break;
                    }
                } else {
                    new_tail.ptr = next;
                    new_tail.tag = curr.tag + 1;
                    tail->full.compare_exchange_strong(curr, new_tail);
                }
            }
        }
        new_tmp.tag = curr.tag + 1;
        new_tmp.ptr = node;
        tail->full.compare_exchange_strong(curr, new_tmp);
    }

    void insert(T* key, const int tid, size_t listIndex = 0) {
        void* buffer = malloc(sizeof(Node) + payloadSize);
        Node* node = new(buffer) Node(payloadSize);
        node->value = key->getSeq();
        do_enqueue(key, tid, listIndex, node);
        hp.clear(tid);
    }

    std::pair<bool, Node*> do_dequeue(T* key, const int tid, size_t listIndex = 0) {
        AbaPtr<Node> curr_head, curr_tail, next, new_tail, new_head;
        AtomicNode<Node>* tail = &Q[listIndex].Tail;
        AtomicNode<Node>* head = &Q[listIndex].Head;
        hp.take_snapshot(tid);
        while (true) {
            curr_head.tag = head->half.tag.load();
            curr_head.ptr = hp.protectPtr(kHpHead, head->half.ptr.load(), tid);
            if (head->half.tag.load() != curr_head.tag)
                continue;
            curr_tail.tag = tail->half.tag.load();
            curr_tail.ptr = tail->half.ptr.load();
            next.ptr = curr_head.ptr->next.load();
            if (!((size_t) next.ptr & 0x1)) {
                hp.protectPtr(kHpNext, next.ptr, tid);
            }
            if (head->half.tag.load() == curr_head.tag) {
                if (curr_head.ptr == curr_tail.ptr) {
                    if ((size_t) next.ptr & 0x1) { // nullptr
                        return {false, nullptr};
                    }
                    next.tag = curr_tail.tag + 1;
                    tail->full.compare_exchange_strong(curr_tail, next);
                } else {
                    next.tag = curr_head.tag + 1;
                    if (head->full.compare_exchange_strong(curr_head, next)) {
                        break;
                    }
                }
            }
        }
        return {true, curr_head.ptr};
    }

    bool remove(T* key, const int tid, size_t listIndex = 0) {
        auto result = do_dequeue(key, tid, listIndex);
        
        if (result.first) hp.retire(result.second, tid);
        
        hp.clear(tid);
        return result.first;
    }

    bool move(T* key, const int tid, size_t list_from = 0, size_t list_to = 0) {
        auto result = do_dequeue(key, tid, list_from);
        if (!result.first) {
            hp.clear(tid);
            return false;
        }
        do_enqueue(key, tid, list_to, result.second, get_tail_tag(tid, list_from));
        hp.clear(tid);
        return true;
    }

    long long calculate_space(const int tid){
        size_t arraySize = payloadSize / sizeof(size_t);
        size_t nodeSize = sizeof(Node) + (arraySize * sizeof(size_t));
        return hp.cal_space(nodeSize, tid);
    }
};
#endif
