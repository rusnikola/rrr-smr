#!/bin/bash

cd "$(dirname "$0")/.." || { echo "Failed to navigate to PLDI directory"; exit 1; }

DATA_DIR="Data"
RRR_SMR_DIR="RRR-SMR"

mkdir -p "$DATA_DIR"


categories=("list" "queue")

for category in "${categories[@]}"; do
    output_folder="$DATA_DIR/${category}_output_results"
    mkdir -p "$output_folder"
    rm -rf "$output_folder"/*  
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
    'stdbuf -oL ./RRR-SMR/bench queue 20 P 0B 128 50%'
    'stdbuf -oL ./RRR-SMR/bench queue 20 P 0B 128 90%'
    'stdbuf -oL ./RRR-SMR/bench queue 20 R 0B 128 50%'
    'stdbuf -oL ./RRR-SMR/bench queue 20 R 0B 128 90%'
    'stdbuf -oL ./RRR-SMR/bench queue 20 P 64KB 128 50%'
    'stdbuf -oL ./RRR-SMR/bench queue 20 P 64KB 128 90%'
    'stdbuf -oL ./RRR-SMR/bench queue 20 R 64KB 128 50%'
    'stdbuf -oL ./RRR-SMR/bench queue 20 R 64KB 128 90%'
)


for cmd in "${commands[@]}"; do
    category=$(echo "$cmd" | awk '{print $4}')
    output_folder="$DATA_DIR/${category}_output_results"
    
    mkdir -p "$output_folder"  
    
    output_file="$output_folder/$(echo "$cmd" | awk '{print $6 "_" $7 "_" $8 "_" $9}').txt"
    
    echo "Running: $cmd"
    eval "$cmd 2>&1 | tee "$output_file""
    
    echo "Completed: $cmd"
    echo "-----------------------------"
done

echo "All benchmarks completed!"
