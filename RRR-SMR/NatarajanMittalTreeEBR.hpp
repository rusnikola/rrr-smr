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

#ifndef _NATARAJAN_MITTAL_TREE
#define _NATARAJAN_MITTAL_TREE

#include <atomic>
#include <thread>
#include <iostream>
#include <string>
#include <vector>
#include <climits>
#include "EBR.hpp"

template<typename T, size_t N = 1>
class NatarajanMittalTreeEBR {
private:

    struct Node {
        const T *key;
        std::atomic<Node*> left;
        std::atomic<Node*> right;
        T *myArray[0];

        Node(const T *k, Node *l, Node *r, size_t payloadSize) : key(k), left(l), right(r) {
            size_t arraySize = payloadSize / sizeof(T *);

            for (size_t i = 0; i < arraySize; i++) {
                myArray[i] = (T *) 0xABCDEFAB;
            }
        }
    };

    struct SeekRecord {
        Node *ancestor;
        Node *successor;
        Node *parent;
        Node *leaf;
        alignas(128) char pad[0];
    };

    const int maxThreads;
    const size_t payloadSize;

    Node *R[N];
    Node *S[N];

    SeekRecord *records;

    EBR<Node> ebr {maxThreads, false, false};

    #define NT_TAG 1UL
    #define NT_FLG 2UL
    #define NT_KEY_NULL ((const T *) nullptr)

    static inline Node *unmarkPtr(Node *n) {
        return (Node *) ((size_t) n & ~(NT_FLG | NT_TAG));
    }

    static inline Node *markPtr(Node *n, size_t flags) {
        return (Node *) ((size_t) n | flags);
    }

    static inline size_t checkPtr(Node *n, size_t flags) {
        return (size_t) n & flags;
    }

    static inline bool keyIsLess(const T *k1, const T *k2) {
        return (k2 == NT_KEY_NULL) || (*k1 < *k2);
    }

    static inline bool keyIsEqual(const T *k1, const T *k2) {
        return (k2 != NT_KEY_NULL) && (*k1 == *k2);
    }

public:

    NatarajanMittalTreeEBR(const int maxThreads, const size_t payloadBytes) : maxThreads{maxThreads}, payloadSize{payloadBytes} {
        for (size_t i = 0; i < N; i++) {
            void* buffer = malloc(sizeof(Node) + payloadSize);
            R[i] = new(buffer) Node(NT_KEY_NULL, nullptr, nullptr, payloadSize);
            buffer = malloc(sizeof(Node) + payloadSize);
            S[i] = new(buffer) Node(NT_KEY_NULL, nullptr, nullptr, payloadSize);
            buffer = malloc(sizeof(Node) + payloadSize);
            R[i]->right.store(new(buffer) Node(NT_KEY_NULL, nullptr, nullptr, payloadSize));
            R[i]->left.store(S[i]);
            buffer = malloc(sizeof(Node) + payloadSize);
            S[i]->right.store(new(buffer) Node(NT_KEY_NULL, nullptr, nullptr, payloadSize));
            buffer = malloc(sizeof(Node) + payloadSize);
            S[i]->left.store(new(buffer) Node(NT_KEY_NULL, nullptr, nullptr, payloadSize));
        }
        records = new SeekRecord[maxThreads]{};
    }

    ~NatarajanMittalTreeEBR() {
        delete[] records;
    }

    std::string className() { return "NatarajanMittalTreeEBR"; }

    void seek(const T *key, int tid, size_t treeIndex = 0)
    {
        SeekRecord* seekRecord = &records[tid];
        seekRecord->ancestor = R[treeIndex];
        seekRecord->successor = R[treeIndex]->left.load();
        seekRecord->parent = seekRecord->successor;
        seekRecord->leaf = unmarkPtr(S[treeIndex]->left.load());

        Node* parentField = seekRecord->parent->left.load();
        Node* currentField = seekRecord->leaf->left.load();
        Node* current = unmarkPtr(currentField);

        while (current != nullptr) {
            if (!checkPtr(parentField, NT_TAG)) {
                seekRecord->ancestor = seekRecord->parent;
                seekRecord->successor = seekRecord->leaf;
            }

            seekRecord->parent = seekRecord->leaf;
            seekRecord->leaf = current;
            parentField = currentField;

            currentField = keyIsLess(key, current->key) ?
                current->left.load() : current->right.load();
            current = unmarkPtr(currentField);
        }
        return;
    }

    bool cleanup(const T *key, int tid)
    {
        SeekRecord *seekRecord = &records[tid];
        Node *ancestor = seekRecord->ancestor;
        Node *successor = seekRecord->successor;
        Node *parent = seekRecord->parent;
        Node *leaf = seekRecord->leaf;

        std::atomic<Node*> *successorAddr =
            keyIsLess(key, ancestor->key) ? &ancestor->left : &ancestor->right;

        std::atomic<Node*> *childAddr, *siblingAddr;
        if (keyIsLess(key, parent->key)) {
            childAddr = &parent->left;
            siblingAddr = &parent->right;
        } else {
            childAddr = &parent->right;
            siblingAddr = &parent->left;
        }

        Node *child = childAddr->load();
        if (!checkPtr(child, NT_FLG)) {
            child = siblingAddr->load();
            siblingAddr = childAddr;
        }

        // tag the sibling edge
        std::atomic<size_t> *_siblingAddr = (std::atomic<size_t> *) siblingAddr;
        Node *node = (Node *) (_siblingAddr->fetch_or(NT_TAG) & (~NT_TAG));
        // the previous value is untagged if necessary
        bool ret = successorAddr->compare_exchange_strong(successor, node);
        // reclaim the deleted edge
        if (ret) {
            while (successor != parent) {
                Node *left = successor->left;
                Node *right = successor->right;
                ebr.smr_retire(successor,tid);
                if (checkPtr(left, NT_FLG)) {
                    ebr.smr_retire(unmarkPtr(left), tid);
                    successor = unmarkPtr(right);
                } else {
                    ebr.smr_retire(unmarkPtr(right), tid);
                    successor = unmarkPtr(left);
                }
            }
            ebr.smr_retire(unmarkPtr(child), tid);
            ebr.smr_retire(successor, tid);
        }
        return ret;
    }

    bool insert(const T *key, int tid, size_t treeIndex = 0)
    {
        SeekRecord *seekRecord = &records[tid];
        bool ret = false;

        void *buffer = malloc(sizeof(Node) + payloadSize);
        Node *newLeaf = new(buffer) Node(key, nullptr, nullptr, payloadSize);

        ebr.read_lock(tid);
        while (true) {
            seek(key, tid, treeIndex);
            Node *leaf = seekRecord->leaf;
            Node *parent = seekRecord->parent;
            if (!keyIsEqual(key, leaf->key)) {
                std::atomic<Node*> *childAddr = keyIsLess(key, parent->key) ?
                                &parent->left : &parent->right;

                Node *newLeft, *newRight;
                if (keyIsLess(key, leaf->key)) {
                    newLeft = newLeaf;
                    newRight = leaf;
                } else {
                    newLeft = leaf;
                    newRight = newLeaf;
                }

                const T *newKey = leaf->key;
                if (newKey != NT_KEY_NULL && *newKey < *key) {
                    newKey = key;
                }
                buffer = malloc(sizeof(Node) + payloadSize);
                Node *newInternal = new(buffer) Node(newKey, newLeft, newRight, payloadSize);

                Node* tmpOld = leaf;
                if (childAddr->compare_exchange_strong(tmpOld, newInternal)) {
                    ret = true;
                    break;
                } else {
                    free(newInternal);
                    Node* child = childAddr->load();
                    if (unmarkPtr(child) == leaf && checkPtr(child, NT_TAG | NT_FLG)) {
                        cleanup(key, tid);
                    }
                }
            } else {
                free(newLeaf);
                ret = false;
                break;
            }
        }
        ebr.read_unlock(tid);
        return ret;
    }

    long long calculate_space(const int tid)
    {
        size_t arraySize = payloadSize / sizeof(T*);
        size_t nodeSize = sizeof(Node) + (arraySize * sizeof(T*));
        return ebr.cal_space(nodeSize, tid);
    }

    bool move(const T *key, const int tid, size_t list_from = 0, size_t list_to = 0)
    {
        bool ok = remove(key, tid, list_from);
        if (!ok) {
            return false;
        }
         return insert(key, tid, list_to);
    }

    bool remove(const T *key, int tid, size_t treeIndex = 0)
    {
        SeekRecord* seekRecord = &records[tid];
        Node *leaf = nullptr; // injection

        ebr.read_lock(tid);
        ebr.take_snapshot(tid);
        while (true) {
            seek(key, tid, treeIndex);
            Node *parent = seekRecord->parent;
            std::atomic<Node*>* childAddr = keyIsLess(key, parent->key) ?
                            &parent->left : &parent->right;

            if (!leaf) { // injection
                leaf = seekRecord->leaf;

                if (!keyIsEqual(key, leaf->key)) {
                    ebr.read_unlock(tid);
                    return false;
                }

                Node *tmpOld = leaf;
                if (childAddr->compare_exchange_strong(tmpOld, markPtr(tmpOld, NT_FLG))) {
                    if (cleanup(key, tid)) {
                        ebr.read_unlock(tid);
                        return true;
                    }
                } else {
                    Node *child = childAddr->load();
                    if (unmarkPtr(child) == leaf && checkPtr(child, NT_TAG | NT_FLG)) {
                        cleanup(key, tid);
                    }
                    leaf = nullptr; // failed: reset injection
                }
            } else {
                if (seekRecord->leaf != leaf) {
                    ebr.read_unlock(tid);
                    return true;
                } else {
                    if (cleanup(key, tid)) {
                        ebr.read_unlock(tid);
                        return true;
                    }
                }
            }
        }
    }
};
#endif
