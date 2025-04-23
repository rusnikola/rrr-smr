/*
 * Copyright (c) 2024-2025, Md Amit Hasan Arovi, Ruslan Nikolaev
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

#ifndef _DW_NATARAJAN_MITTAL_TREE_EBR
#define _DW_NATARAJAN_MITTAL_TREE_EBR

#include <atomic>
#include <thread>
#include <iostream>
#include <string>
#include <vector>
#include <climits>
#include "EBR.hpp"
#include "HalfAtomic.hpp"

template<typename T, size_t N = 1>
class DWNatarajanMittalTreeEBR {
private:

    struct Node;

    struct Block {
        Node *left;
        Node *right;

        Block() {};
        Block(Node* l, Node* r) : left(l), right(r) {};
    };

    struct Node {
        AtomicNode<Block> blk;
        const T *key;
        T *myArray[0];

        Node(Block *b, size_t payloadSize) {
            size_t arraySize = payloadSize / sizeof(T*);

            for (size_t i = 0; i < arraySize; i++) {
                myArray[i] = (T *) 0xABCDEFAB;
            }
            blk.half.ptr = b;
            blk.half.tag = 0;
        }

        Node(const T *k, Block *b, size_t payloadSize) : key(k) {
            size_t arraySize = payloadSize / sizeof(T*);

            for (size_t i = 0; i < arraySize; i++) {
                myArray[i] = (T *) 0xABCDEFAB;
            }
            blk.half.ptr = b;
            blk.half.tag = 0;
        }

        inline void ReInit(Block *b) {
            AbaPtr<Block> o, n;
            o.tag = blk.half.tag.load(std::memory_order_relaxed);
            o.ptr = blk.half.ptr.load(std::memory_order_relaxed);
            do {
                n.ptr = b;
                n.tag = o.tag + 1;
            } while (!blk.full.compare_exchange_strong(o, n));
        }
    };

    struct SeekRecord {
        AbaPtr<Block> ancestor_blk;
        AbaPtr<Block> parent_blk;

        Node *ancestor;
        const T *ancestor_key;
        Node *parent;
        const T *parent_key;
        Node *leaf;
        const T *leaf_key;
        Node *successor;
        Node *left;
        Node *right;

        Block *freeblk; // spare block
        alignas(128) char pad[0];
    };

    const int maxThreads;
    const size_t payloadSize;

    Node *R[N];
    Node *S[N];

    SeekRecord *records;

    EBR<Node> ebr {maxThreads, false, false};

    // Left and Right flag bits
    #define NT_LFLG 1UL
    #define NT_RFLG 2UL
    // A special key that is used for sentinel nodes
    #define NT_KEY_NULL ((const T *) nullptr)

    static inline Block *unmarkPtr(Block *b) {
        return (Block *) ((size_t) b & ~(NT_LFLG | NT_RFLG));
    }

    static inline Block *markPtr(Block *b, size_t flags) {
        return (Block *) ((size_t) b | flags);
    }

    static inline size_t checkPtr(Block *b, size_t flags) {
        return (size_t) b & flags;
    }

    static inline Node *getLeft(Block *_b) {
        Block *b = unmarkPtr(_b);
        if (!b) return nullptr;
        return b->left;
    }

    static inline Node *getRight(Block *_b) {
        Block *b = unmarkPtr(_b);
        if (!b) return nullptr;
        return b->right;
    }

    static inline bool keyIsLess(const T *k1, const T *k2) {
        return (k2 == NT_KEY_NULL) || (*k1 < *k2);
    }

    static inline bool keyIsEqual(const T *k1, const T *k2) {
        return (k2 != NT_KEY_NULL) && (*k1 == *k2);
    }

public:

    DWNatarajanMittalTreeEBR(const int maxThreads, const size_t payloadBytes) : maxThreads{maxThreads}, payloadSize{payloadBytes} {
        for (size_t i = 0; i < N; i++) {
            void *buffer1 = malloc(sizeof(Node) + payloadSize);
            void *buffer2 = malloc(sizeof(Node) + payloadSize);
            Block* sb = new Block(new(buffer1) Node(NT_KEY_NULL, nullptr, payloadSize), new(buffer2) Node(NT_KEY_NULL, nullptr, payloadSize));
            buffer1 = malloc(sizeof(Node) + payloadSize);
            S[i] = new(buffer1) Node(NT_KEY_NULL, sb, payloadSize);
            buffer1 = malloc(sizeof(Node) + payloadSize);
            Block* rb = new Block(S[i], new(buffer1) Node(NT_KEY_NULL, nullptr, payloadSize));
            buffer1 = malloc(sizeof(Node) + payloadSize);
            R[i] = new(buffer1) Node(NT_KEY_NULL, rb, payloadSize);
        }
        records = new SeekRecord[maxThreads]{};
        for (size_t j = 0; j < maxThreads; j++) {
            records[j].freeblk = new Block();
        }
    }

    ~DWNatarajanMittalTreeEBR() {
        for (size_t j = 0; j < maxThreads; j++) {
            delete records[j].freeblk;
        }
        delete[] records;
    }

    std::string className() { return "DWNatarajanMittalTreeEBR"; }

    void seek(const T *key, int tid, size_t treeIndex = 0)
    {
        SeekRecord *seekRecord = &records[tid];
        AbaPtr<Block> leaf_blk;
        Node *current, *left, *right;
        size_t tag;

start_over:
        seekRecord->ancestor = R[treeIndex];
        seekRecord->ancestor_blk.tag = R[treeIndex]->blk.half.tag.load();
        seekRecord->ancestor_blk.ptr = R[treeIndex]->blk.half.ptr.load();
        seekRecord->ancestor_key = R[treeIndex]->key;
        seekRecord->parent = S[treeIndex];
        seekRecord->successor = seekRecord->parent;
        tag = S[treeIndex]->blk.half.tag.load();
start_over_parent:
        seekRecord->parent_blk.tag = tag;
        seekRecord->parent_blk.ptr = S[treeIndex]->blk.half.ptr.load();
        seekRecord->parent_key = S[treeIndex]->key;
        seekRecord->leaf = unmarkPtr(seekRecord->parent_blk.ptr)->left;
        seekRecord->left = seekRecord->leaf;
        seekRecord->right = unmarkPtr(seekRecord->parent_blk.ptr)->right;
        tag = S[treeIndex]->blk.half.tag.load();
        if (seekRecord->parent_blk.tag != tag)
            goto start_over_parent;
        tag = seekRecord->leaf->blk.half.tag.load();
start_over_leaf:
        leaf_blk.tag = tag;
        leaf_blk.ptr = seekRecord->leaf->blk.half.ptr.load();
        seekRecord->leaf_key = seekRecord->leaf->key;
        tag = S[treeIndex]->blk.half.tag.load();
        if (seekRecord->parent_blk.tag != tag)
            goto start_over_parent;
        left = getLeft(leaf_blk.ptr);
        right = getRight(leaf_blk.ptr);
        tag = seekRecord->leaf->blk.half.tag.load();
        if (leaf_blk.tag != tag)
           goto start_over_leaf;

        current = left;

        while (current != nullptr) {
            if (!checkPtr(seekRecord->parent_blk.ptr, NT_RFLG | NT_LFLG)) {
                seekRecord->ancestor = seekRecord->parent;
                seekRecord->ancestor_key = seekRecord->parent_key;
                seekRecord->ancestor_blk = seekRecord->parent_blk;
                seekRecord->successor = seekRecord->leaf;
            }

            // Check if was moved
            if (checkPtr(leaf_blk.ptr, NT_LFLG | NT_RFLG)) {
                if (seekRecord->ancestor->blk.half.tag.load() !=
                        seekRecord->ancestor_blk.tag) {
                    goto start_over;
                }
            }

            seekRecord->parent = seekRecord->leaf;
            seekRecord->parent_key = seekRecord->leaf_key;
            seekRecord->parent_blk = leaf_blk;
            seekRecord->leaf = current;
            seekRecord->left = left;
            seekRecord->right = right;

            tag = seekRecord->leaf->blk.half.tag.load();
            do {
                leaf_blk.tag = seekRecord->leaf->blk.half.tag.load();
                leaf_blk.ptr = seekRecord->leaf->blk.half.ptr.load();
                seekRecord->leaf_key = seekRecord->leaf->key;
                if (seekRecord->parent_blk.tag != seekRecord->parent->blk.half.tag.load())
                    goto start_over;
                left = getLeft(leaf_blk.ptr);
                right = getRight(leaf_blk.ptr);
                tag = seekRecord->leaf->blk.half.tag.load();
            } while (leaf_blk.tag != tag);

            if (keyIsLess(key, seekRecord->leaf_key)) {
                current = left;
            } else {
                current = right;
            }
        }
    }

    bool cleanup(const T *key, int tid)
    {
        SeekRecord *seekRecord = &records[tid];
        Node *ancestor = seekRecord->ancestor;
        Node *parent = seekRecord->parent;
        Node *sibling;
        Block *freeblk = seekRecord->freeblk;

        sibling = checkPtr(seekRecord->parent_blk.ptr, NT_LFLG) ?
                      seekRecord->right : seekRecord->left;

        if (keyIsLess(key, seekRecord->ancestor_key)) {
            freeblk->left = sibling;
            freeblk->right = unmarkPtr(seekRecord->ancestor_blk.ptr)->right;
        } else {
            freeblk->left = unmarkPtr(seekRecord->ancestor_blk.ptr)->left;
            freeblk->right = sibling;
        }

        AbaPtr<Block> tmpNew, tmpOld;
        tmpOld = seekRecord->ancestor_blk;
        tmpNew.tag = seekRecord->ancestor_blk.tag + 1;
        tmpNew.ptr = (Block *)((size_t) freeblk | ((size_t) tmpOld.ptr & (NT_LFLG | NT_RFLG)));

        if (ancestor->blk.full.compare_exchange_strong(tmpOld, tmpNew)) {
            seekRecord->freeblk = unmarkPtr(tmpOld.ptr);
            return true;
        }

        return false;
    }

    bool do_insert(const T *key, int tid, size_t treeIndex, Node *newLeaf, Node *newInternal, Block *newBlock)
    {
        SeekRecord* seekRecord = &records[tid];

        while (true) {
            seek(key, tid, treeIndex);
            Node *leaf = seekRecord->leaf;
            Node *parent = seekRecord->parent;

            if (!keyIsEqual(key, seekRecord->leaf_key)) {
                if (checkPtr(seekRecord->parent_blk.ptr, NT_LFLG | NT_RFLG)) {
                    cleanup(key, tid);
                    continue;
                }

                Block *freeblk = seekRecord->freeblk; // retrieve the new block
                Node *newLeft, *newRight;
                if (keyIsLess(key, seekRecord->leaf_key)) {
                    newBlock->left = newLeaf;
                    newBlock->right = leaf;
                } else {
                    newBlock->left = leaf;
                    newBlock->right = newLeaf;
                }

                const T *newKey = seekRecord->leaf_key;
                if (newKey != NT_KEY_NULL && *newKey < *key) {
                    newKey = key;
                }
                newInternal->key = newKey;

                Block *ptr =  seekRecord->parent_blk.ptr;
                if (keyIsLess(key, seekRecord->parent_key)) {
                    freeblk->left = newInternal;
                    freeblk->right = ptr->right;
                } else {
                    freeblk->right = newInternal;
                    freeblk->left = ptr->left;
                }

                AbaPtr<Block> tmpNew;
                tmpNew.tag = seekRecord->parent_blk.tag + 1;
                tmpNew.ptr = freeblk;

                if (parent->blk.full.compare_exchange_strong(seekRecord->parent_blk, tmpNew)) {
                    seekRecord->freeblk = seekRecord->parent_blk.ptr; // recycle the old block
                    return true;
                } else {
                    // a conflicting sibling operation
                    if (seekRecord->parent_blk.tag == tmpNew.tag && unmarkPtr(seekRecord->parent_blk.ptr) == ptr) {
                        cleanup(key, tid);
                    }
                }
            } else {
                break;
            }
        }
        return false;
    }

    long long calculate_space(const int tid)
    {
        size_t arraySize = payloadSize / sizeof(T*);
        size_t nodeSize = sizeof(Node) + (arraySize * sizeof(T*));
        return ebr.cal_space(nodeSize, tid);
    }

    bool do_remove(const T *key, int tid, size_t treeIndex, Node **n1, Node **n2, Block **b)
    {
        SeekRecord* seekRecord = &records[tid];
        Node *leaf = nullptr; // injection

        ebr.take_snapshot(tid);
        while (true) {
            seek(key, tid, treeIndex);
            Node *parent = seekRecord->parent;

            if (!leaf) { // injection
                leaf = seekRecord->leaf;
                if (!keyIsEqual(key, seekRecord->leaf_key)) {
                    return false;
                }

                if (checkPtr(seekRecord->parent_blk.ptr, NT_LFLG | NT_RFLG)) {
                    cleanup(key, tid);
                    leaf = nullptr; // failed: reset injection
                    continue;
                }

                AbaPtr<Block> tmpNew;
                tmpNew.tag = seekRecord->parent_blk.tag + 1;
                tmpNew.ptr = markPtr(seekRecord->parent_blk.ptr,
                  keyIsLess(key, seekRecord->parent_key) ? NT_LFLG : NT_RFLG);

                if (parent->blk.full.compare_exchange_strong(seekRecord->parent_blk, tmpNew)) {
                    seekRecord->parent_blk = tmpNew;
                    *n1 = leaf;
                    *n2 = parent;
                    *b = unmarkPtr(seekRecord->parent_blk.ptr);
                    if (cleanup(key, tid)) {
                        return true;
                    }
                } else {
                    // a conflicting sibling operation
                    if (seekRecord->parent_blk.tag == tmpNew.tag && unmarkPtr(seekRecord->parent_blk.ptr) == unmarkPtr(tmpNew.ptr)) {
                        cleanup(key, tid);
                    }
                    leaf = nullptr; // failed: reset injection
                }
            } else {
                if (!checkPtr(seekRecord->parent_blk.ptr, NT_LFLG | NT_RFLG)) {
                    return true;
                } else {
                    if (cleanup(key, tid)) {
                        return true;
                    }
                }
            }
        }
    }

    bool insert(const T *key, int tid, size_t treeIndex = 0)
    {
        Block *newBlock = new Block;
        void *buffer = malloc(sizeof(Node) + payloadSize);
        Node *newInternal = new(buffer) Node(newBlock, payloadSize);
        buffer = malloc(sizeof(Node) + payloadSize);
        Node *newLeaf = new(buffer) Node(key, nullptr, payloadSize);
        ebr.read_lock(tid);
        if (do_insert(key, tid, treeIndex, newLeaf, newInternal, newBlock)) {
            ebr.read_unlock(tid);
            return true;
        } else {
            ebr.read_unlock(tid);
            free(newLeaf);
            free(newInternal);
            delete newBlock;
            return false;
        }
    }

    bool remove(const T *key, int tid, size_t treeIndex = 0)
    {
        Node *n1 = nullptr, *n2 = nullptr;
        Block *b = nullptr;
        ebr.read_lock(tid);
        if (do_remove(key, tid, treeIndex, &n1, &n2, &b)) {
            ebr.smr_retire(n1, tid);
            ebr.smr_retire(n2, tid);
            ebr.smr_retire_meta(b, tid);
            ebr.read_unlock(tid);
            return true;
        } else {
            ebr.read_unlock(tid);
            return false;
        }
    }

    // A copy-free move
    bool move(const T *key, const int tid, size_t list_from = 0, size_t list_to = 0)
    {
        Node *n1 = nullptr, *n2 = nullptr;
        Block *b = nullptr;
        ebr.read_lock(tid);
        bool ok = do_remove(key, tid, list_from, &n1, &n2, &b);
        if (!ok) {
            ebr.read_unlock(tid);
            return false;
        }
        n2->ReInit(b);
        ok = do_insert(key, tid, list_to, n1, n2, b);
        if (!ok) {
            ebr.smr_retire(n1, tid);
            ebr.smr_retire(n2, tid);
            ebr.smr_retire_meta(b, tid);
        }
        ebr.read_unlock(tid);
        return ok;
    }
};
#endif
