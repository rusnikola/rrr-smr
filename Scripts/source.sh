#!/bin/bash

cd "$(dirname "$0")/.." || { echo "Failed to navigate to PLDI directory"; exit 1; }

DATA_DIR="Data"
RRR_SMR_DIR="RRR-SMR"

mkdir -p "$DATA_DIR"

# Define benchmark categories
categories=("list" "queue" "tree")

for category in "${categories[@]}"; do
    output_folder="$DATA_DIR/${category}_output_results"
    mkdir -p "$output_folder"
    rm -rf "$output_folder"/*  # Remove only the contents
done

cd "$RRR_SMR_DIR" || { echo "Failed to enter $RRR_SMR_DIR directory"; exit 1; }

if make -n clean &>/dev/null; then
    make clean
else
    echo "Skipping make clean: No 'clean' target found."
fi

make all || { echo "Make failed"; exit 1; }

cd .. || { echo "Failed to return to parent directory"; exit 1; }

commands=(
    'stdbuf -oL ./RRR-SMR/bench list 60 P 128B 4096 50%'
    'stdbuf -oL ./RRR-SMR/bench list 60 P 128B 4096 90%'
    'stdbuf -oL ./RRR-SMR/bench list 60 P 64KB 4096 50%'
    'stdbuf -oL ./RRR-SMR/bench list 60 P 64KB 4096 90%'
    'stdbuf -oL ./RRR-SMR/bench queue 20 P 0B 256 50%'
    'stdbuf -oL ./RRR-SMR/bench queue 20 P 0B 256 90%'
    'stdbuf -oL ./RRR-SMR/bench queue 20 R 0B 256 50%'
    'stdbuf -oL ./RRR-SMR/bench queue 20 R 0B 256 90%'
    'stdbuf -oL ./RRR-SMR/bench queue 20 P 64KB 256 50%'
    'stdbuf -oL ./RRR-SMR/bench queue 20 P 64KB 256 90%'
    'stdbuf -oL ./RRR-SMR/bench queue 20 R 64KB 256 50%'
    'stdbuf -oL ./RRR-SMR/bench queue 20 R 64KB 256 90%'
    'stdbuf -oL ./RRR-SMR/bench list 60 P 128B 4096 10% 64'
    'stdbuf -oL ./RRR-SMR/bench list 60 P 128B 4096 20% 64'
    'stdbuf -oL ./RRR-SMR/bench list 60 P 128B 4096 30% 64'
    'stdbuf -oL ./RRR-SMR/bench list 60 P 128B 4096 40% 64'
    'stdbuf -oL ./RRR-SMR/bench list 60 P 128B 4096 60% 64'
    'stdbuf -oL ./RRR-SMR/bench list 60 P 64KB 4096 10% 64'
    'stdbuf -oL ./RRR-SMR/bench list 60 P 64KB 4096 20% 64'
    'stdbuf -oL ./RRR-SMR/bench list 60 P 64KB 4096 30% 64'
    'stdbuf -oL ./RRR-SMR/bench list 60 P 64KB 4096 40% 64'
    'stdbuf -oL ./RRR-SMR/bench list 60 P 64KB 4096 60% 64'
    'stdbuf -oL ./RRR-SMR/bench queue 20 P 0B 256 10% 32'
    'stdbuf -oL ./RRR-SMR/bench queue 20 P 0B 256 20% 32'
    'stdbuf -oL ./RRR-SMR/bench queue 20 P 0B 256 30% 32'
    'stdbuf -oL ./RRR-SMR/bench queue 20 P 0B 256 40% 32'
    'stdbuf -oL ./RRR-SMR/bench queue 20 P 0B 256 60% 32'
    'stdbuf -oL ./RRR-SMR/bench queue 20 R 0B 256 10% 32'
    'stdbuf -oL ./RRR-SMR/bench queue 20 R 0B 256 20% 32'
    'stdbuf -oL ./RRR-SMR/bench queue 20 R 0B 256 30% 32'
    'stdbuf -oL ./RRR-SMR/bench queue 20 R 0B 256 40% 32'
    'stdbuf -oL ./RRR-SMR/bench queue 20 R 0B 256 60% 32'
    'stdbuf -oL ./RRR-SMR/bench queue 20 P 64KB 256 10% 32'
    'stdbuf -oL ./RRR-SMR/bench queue 20 P 64KB 256 20% 32'
    'stdbuf -oL ./RRR-SMR/bench queue 20 P 64KB 256 30% 32'
    'stdbuf -oL ./RRR-SMR/bench queue 20 P 64KB 256 40% 32'
    'stdbuf -oL ./RRR-SMR/bench queue 20 P 64KB 256 60% 32'
    'stdbuf -oL ./RRR-SMR/bench queue 20 R 64KB 256 10% 32'
    'stdbuf -oL ./RRR-SMR/bench queue 20 R 64KB 256 20% 32'
    'stdbuf -oL ./RRR-SMR/bench queue 20 R 64KB 256 30% 32'
    'stdbuf -oL ./RRR-SMR/bench queue 20 R 64KB 256 40% 32'
    'stdbuf -oL ./RRR-SMR/bench queue 20 R 64KB 256 60% 32'
    'stdbuf -oL ./RRR-SMR/bench tree 60 P 128B 65536 50%'
    'stdbuf -oL ./RRR-SMR/bench tree 60 P 128B 65536 90%'
    'stdbuf -oL ./RRR-SMR/bench tree 60 P 64KB 65536 50%'
    'stdbuf -oL ./RRR-SMR/bench tree 60 P 64KB 65536 90%'
)

for cmd in "${commands[@]}"; do
    category=$(echo "$cmd" | awk '{print $4}')
    output_folder="$DATA_DIR/${category}_output_results"
    mkdir -p "$output_folder"

    recycling_perc=$(echo "$cmd" | awk '{print $9}')
    filename="$(echo "$cmd" | awk '{print $6 "_" $7 "_" $8 "_" $9}').txt"

    if [[ "$recycling_perc" =~ ^(10%|20%|30%|40%|60%)$ ]]; then
        if [[ "$category" == "queue" ]]; then
            mode=$(echo "$cmd" | awk '{print $6}')  # P or R
            if [[ "$mode" == "P" ]]; then
                output_folder="$output_folder/RecyclingPercentage/Pairwise"
            elif [[ "$mode" == "R" ]]; then
                output_folder="$output_folder/RecyclingPercentage/Random"
            fi
        else
            output_folder="$output_folder/RecyclingPercentage"
        fi
        mkdir -p "$output_folder"
    fi

    output_file="$output_folder/$filename"

    echo "Running: $cmd"
    eval "$cmd 2>&1 | tee \"$output_file\""

    echo "Completed: $cmd"
    echo "-----------------------------"
done

echo "All benchmarks completed!"

