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

#ifndef _DW_TIM_HARRIS_MAGED_MICHAEL_LINKED_LIST_HP_H_
#define _DW_TIM_HARRIS_MAGED_MICHAEL_LINKED_LIST_HP_H_

#include <atomic>
#include <thread>
#include <forward_list>
#include <set>
#include <iostream>
#include <string>
#include "HazardPointers.hpp"
#include "HalfAtomic.hpp"

template<typename T, size_t N = 1> 
class DWHarrisMichaelLinkedListHP {

private:

    struct Node {
        AtomicNode<Node> next;
        T* key;
        T* myArray[0];

        Node(T* key, size_t payloadSize) : key{key} {
            next.half.ptr = nullptr;
            next.half.tag = 0;

            size_t arraySize = payloadSize / sizeof(T*);

            for (size_t i = 0; i < arraySize; i++) {
                myArray[i] = key;
            }
        }
    };

    struct ListHead {
        alignas(128) AtomicNode<Node> list;
    };

    ListHead head[N];
    alignas(128) char pad[0];

    const int maxThreads;
    const size_t payloadSize;
    
    const int kHp0 = 0;
    const int kHp1 = 1;
    const int kHp2 = 2;

    HazardPointers<Node> hp {3, maxThreads, payloadSize >= 1024};

public:

    DWHarrisMichaelLinkedListHP(const int maxThreads, const size_t payloadBytes) : maxThreads{maxThreads}, payloadSize{payloadBytes} {
        for (size_t i = 0; i < N; i++) {
            head[i].list.half.tag.store(0);
            void* buffer = malloc(sizeof(Node) + payloadSize);
            head[i].list.half.ptr.store(new(buffer) Node(nullptr, payloadSize));
        }
    }

    ~DWHarrisMichaelLinkedListHP() {
    }

    std::string className() { return "DWHarrisMichaelLinkedListHP"; }

    bool do_add(T* key, const int tid, size_t listIndex, Node* newNode)
    {
        AbaPtr<Node> curr, next;
        AtomicNode<Node> *prev;
        
        while (true) {
            if (find(key, &prev, &curr, &next, tid, listIndex)) {
                return false;
            }
            newNode->next.half.ptr.store(curr.ptr, std::memory_order_relaxed);
            AbaPtr<Node> old_tmp;
            old_tmp.ptr = curr.ptr;
            old_tmp.tag = curr.tag;
            AbaPtr<Node> new_tmp;
            new_tmp.ptr = newNode;
            new_tmp.tag = curr.tag + 1;
            if (prev->full.compare_exchange_strong(old_tmp, new_tmp)) {
                return true;
            }
        }
    }

    bool do_add_full(T* key, const int tid, size_t listIndex, Node* newNode)
    {
        AbaPtr<Node> curr, next, o, n;
        AtomicNode<Node> *prev;
        
        while (true) {
            if (find(key, &prev, &curr, &next, tid, listIndex)) {
                return false;
            }

            o.tag = newNode->next.half.tag.load(std::memory_order_relaxed);
            o.ptr = newNode->next.half.ptr.load(std::memory_order_relaxed);
            do {
                n.ptr = curr.ptr;
                n.tag = o.tag + 1;
            } while (!newNode->next.full.compare_exchange_strong(o, n));
            AbaPtr<Node> old_tmp;
            old_tmp.ptr = curr.ptr;
            old_tmp.tag = curr.tag;
            AbaPtr<Node> new_tmp;
            new_tmp.ptr = newNode;
            new_tmp.tag = curr.tag + 1;
            if (prev->full.compare_exchange_strong(old_tmp, new_tmp)) {
                return true;
            }
        }
    }

    bool insert(T* key, const int tid, size_t listIndex = 0) {
        void* buffer = malloc(sizeof(Node) + payloadSize);
        Node* newNode = new(buffer) Node(key, payloadSize);
        if (!do_add(key, tid, listIndex, newNode)) {
            free(newNode);
            hp.clear(tid);
            return false;
        }
        hp.clear(tid);
        return true;
    }

    std::pair<bool, Node*> do_remove(T* key, const int tid, size_t listIndex) {
        AbaPtr<Node> curr, next;
        AtomicNode<Node>* prev;
        hp.take_snapshot(tid);
        while (true) {

            if (!find(key, &prev, &curr, &next, tid, listIndex)) {
                return {false, nullptr};
            }


            AbaPtr<Node> tmp_old, tmp_new;
            tmp_old.ptr = next.ptr;
            tmp_old.tag = next.tag;
            tmp_new.ptr = getMarked(next.ptr);
            tmp_new.tag = next.tag + 1;
            if (!curr.ptr->next.full.compare_exchange_strong(tmp_old, tmp_new)) {
                continue;
            }

            AbaPtr<Node> old_tag;
            old_tag.ptr = curr.ptr;
            old_tag.tag = curr.tag;

            AbaPtr<Node> new_tag;
            new_tag.ptr = next.ptr;
            new_tag.tag = curr.tag + 1;
            if (!prev->full.compare_exchange_strong(old_tag, new_tag)) {
                prune(curr.ptr, tid, listIndex);
            }

            return {true, curr.ptr};
        }
    }

    bool remove(T* key, const int tid, size_t listIndex = 0) {
        auto result = do_remove(key, tid, listIndex);
        if (result.first) hp.retire(result.second, tid);
        hp.clear(tid);
        return result.first;
    }

    bool move(T* key, const int tid, size_t list_from = 0, size_t list_to = 0) {
        auto result = do_remove(key, tid, list_from);
        if (!result.first) {
            hp.clear(tid);
            return false;
        }
        if (!do_add_full(key, tid, list_to, result.second)) {
            hp.retire(result.second, tid);
            hp.clear(tid);
            return false;
        }
        hp.clear(tid);
        return true;
    }

    bool contains (T* key, const int tid, size_t listIndex = 0)
    {
        AbaPtr<Node> curr, next;
        AtomicNode<Node>* prev;
        bool isContains = find(key, &prev, &curr, &next, tid, listIndex);
        hp.clear(tid);
        return isContains;
    }

    long long calculate_space(const int tid){
        size_t arraySize = payloadSize / sizeof(T*);
        size_t nodeSize = sizeof(Node) + (arraySize * sizeof(T*));
        return hp.cal_space(nodeSize, tid);
    }


private:

    void prune (Node *node, const int tid, size_t listIndex)
    {
        AtomicNode<Node>* prev;
        AbaPtr<Node> curr, next;

    try_again:
        prev = &head[listIndex].list;
        curr.tag = prev->half.tag.load();
        curr.ptr = prev->half.ptr.load();

        hp.protectPtr(kHp1, curr.ptr, tid);
        if (prev->half.tag.load() != curr.tag) goto try_again;
        while (true) {

            if (curr.ptr == nullptr) break;
            next.tag = curr.ptr->next.half.tag.load();
            next.ptr = curr.ptr->next.half.ptr.load();
            hp.protectPtr(kHp0, getUnmarked(next.ptr), tid);
            if (curr.ptr->next.half.tag.load() != next.tag) goto try_again;
            if (prev->half.tag.load() != curr.tag) goto try_again;
            if (getUnmarked(next.ptr) == next.ptr) {
                prev = &curr.ptr->next;
                hp.protectPtrRelease(kHp2, curr.ptr, tid);
            } else {
                AbaPtr<Node> tmp, tmp1;
                tmp.ptr = curr.ptr;
                tmp.tag = curr.tag;
                next.ptr = getUnmarked(next.ptr);
                tmp1.ptr = next.ptr;
                tmp1.tag = curr.tag + 1;
                if (!prev->full.compare_exchange_strong(tmp, tmp1)) {
                    goto try_again;
                }
                if (curr.ptr == node) {
                    break;
                }
            }
            curr = next;
            hp.protectPtrRelease(kHp1, next.ptr, tid);
        }
    }

    bool find (T* key, AtomicNode<Node>** par_prev, AbaPtr<Node>* par_curr, AbaPtr<Node>* par_next, const int tid, size_t listIndex)
    {
        AtomicNode<Node>* prev;
        AbaPtr<Node> curr, next;
        T *curr_key;

    try_again:
        prev = &head[listIndex].list;
        curr.tag = prev->half.tag.load();
        curr.ptr = prev->half.ptr.load();

        hp.protectPtr(kHp1, curr.ptr, tid);
        if (prev->half.tag.load() != curr.tag) goto try_again;
        while (true) {
            if (curr.ptr == nullptr) break;
            next.tag = curr.ptr->next.half.tag.load();
            next.ptr = curr.ptr->next.half.ptr.load();
            curr_key = curr.ptr->key;
            hp.protectPtr(kHp0, getUnmarked(next.ptr), tid);
            if (curr.ptr->next.half.tag.load() != next.tag) goto try_again;
            if (prev->half.tag.load() != curr.tag) goto try_again;
            if (getUnmarked(next.ptr) == next.ptr) {
                if (curr_key != nullptr && !(*curr_key < *key)) {
                    *par_curr = curr;
                    *par_prev = prev;
                    *par_next = next;
                    return (*curr_key == *key);
                }
                prev = &curr.ptr->next;
                hp.protectPtrRelease(kHp2, curr.ptr, tid);
            } else {
                AbaPtr<Node> tmp, tmp1;
                tmp.ptr = curr.ptr;
                tmp.tag = curr.tag;
                next.ptr = getUnmarked(next.ptr);
                tmp1.ptr = next.ptr;
                tmp1.tag = curr.tag + 1;
                if (!prev->full.compare_exchange_strong(tmp, tmp1)) {
                    goto try_again;
                }
            }
            curr = next;
            hp.protectPtrRelease(kHp1, next.ptr, tid);
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

#endif /* _DW_TIM_HARRIS_MAGED_MICHAEL_LINKED_LIST_HP_H_ */
