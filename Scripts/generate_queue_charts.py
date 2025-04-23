import os
import re
import glob
import numpy as np
import matplotlib.pyplot as plt
import cairosvg
import shutil

#
# This script processes queue data and generates charts
# (main tests with 50- and 90- percent recycling)
#

# Paths
DATA_DIR = os.path.abspath(os.path.join(os.path.dirname(__file__), "..", "Data"))
QUEUE_RESULTS_DIR = os.path.join(DATA_DIR, "queue_output_results")
QUEUE_CHARTS_BASE_DIR = os.path.join(DATA_DIR, "queue_charts")
PAIRWISE_CHARTS_DIR = os.path.join(QUEUE_CHARTS_BASE_DIR, "Pairwise")
RANDOM_CHARTS_DIR = os.path.join(QUEUE_CHARTS_BASE_DIR, "Random")

# Setup folders
for path in [PAIRWISE_CHARTS_DIR, RANDOM_CHARTS_DIR]:
    os.makedirs(path, exist_ok=True)
    # Clean old contents
    for item in glob.glob(os.path.join(path, "*")):
        if os.path.isfile(item):
            os.remove(item)
        elif os.path.isdir(item):
            shutil.rmtree(item)

# Files
txt_files = glob.glob(os.path.join(QUEUE_RESULTS_DIR, "*.txt"))
if not txt_files:
    print(f"No .txt files found in {QUEUE_RESULTS_DIR}.")
    exit(1)

# Config
hatches = ['-', '\\', '/', '*', 'O', '.']
colors = ['gold', 'g', 'm', 'c', 'r', 'teal']
benchmarks = ["Queue-HP", "Queue-ABA-HP", "ModQueue-ABA-HP", "Queue-EBR", "Queue-ABA-EBR", "ModQueue-ABA-EBR"]

pattern = re.compile(
    r"Threads,\s*MSQueueHP,\s*MSQueueABAHP,\s*ModQueueHP,\s*MSQueueEBR,\s*MSQueueABAEBR,\s*ModQueueEBR,"
    r"\s*MSQueueHP_Memory_Usage,\s*MSQueueABAHP_Memory_Usage,\s*ModQueueHP_Memory_Usage,"
    r"\s*MSQueueEBR_Memory_Usage,\s*MSQueueABAEBR_Memory_Usage,\s*ModQueueEBR_Memory_Usage"
)

# Chart plotting
def plot_bar_chart(data, ylabel, output_name, output_dir):
    fig, ax = plt.subplots(figsize=(16, 10))
    for i in range(len(benchmarks)):
        ax.bar(index + i * bar_width, data[:, i + 1], bar_width,
               label=benchmarks[i], color=colors[i], hatch=hatches[i])

    ax.set_xlabel("Threads", fontsize=25, fontweight='bold', labelpad=5)
    ax.set_ylabel(ylabel, fontsize=25, fontweight='bold', labelpad=25)
    ax.set_xticks(index + total_group_width / 2 - bar_width / 2)
    ax.set_xticklabels(threads, fontsize=22)

    ax.ticklabel_format(axis='y', style='sci', scilimits=(0, 0), useMathText=True)
    ax.yaxis.offsetText.set_fontsize(22)
    for tick in ax.get_yticklabels():
        tick.set_fontsize(22)

    legend = ax.legend(ncol=2, prop={'size': 17, 'weight': 'bold'}, frameon=True, fancybox=False,
                       edgecolor='black', handletextpad=1.5)
    frame = legend.get_frame()
    frame.set_linestyle('--')
    frame.set_linewidth(1.5)
    for text in legend.get_texts():
        text.set_fontsize(17)
        text.set_fontweight('bold')

    svg_path = os.path.join(output_dir, f"{output_name}.svg")
    pdf_path = os.path.join(output_dir, f"{output_name}.pdf")

    plt.savefig(svg_path, format="svg", bbox_inches="tight")
    plt.close(fig)
    cairosvg.svg2pdf(url=svg_path, write_to=pdf_path)

# Process each file
for file_path in txt_files:
    try:
        file_name = os.path.splitext(os.path.basename(file_path))[0]  # e.g., P_0B_128_30%
        parts = file_name.split("_")

        if len(parts) != 4:
            print(f"Skipping file with unexpected format: {file_name}")
            continue

        mode, workload, _, perc = parts
        perc_clean = perc.replace("%", "p")  # sanitize for Overleaf

        output_dir = PAIRWISE_CHARTS_DIR if mode == "P" else RANDOM_CHARTS_DIR

        data_started = False
        throughput_data = []
        memory_data = []

        with open(file_path, "r") as file:
            for line in file:
                if not data_started:
                    if pattern.search(line):
                        data_started = True
                    continue

                values = [int(x.strip()) for x in line.split(",") if x.strip().isdigit()]
                if len(values) >= 13:
                    throughput_data.append([values[0]] + values[1:7])
                    memory_data.append([values[0]] + values[7:13])

        if not throughput_data:
            print(f"No data parsed from: {file_name}")
            continue

        throughput_data = np.array(throughput_data)
        memory_data = np.array(memory_data)
        threads = throughput_data[:, 0]

        num_benchmarks = len(benchmarks)
        bar_width = 0.2
        space_between_threads = 0.1
        total_group_width = num_benchmarks * bar_width + space_between_threads
        index = np.arange(len(threads)) * total_group_width

        output_prefix = f"queue_{mode}_{workload}_{perc_clean}"
        plot_bar_chart(throughput_data, "Throughput, ops/sec", f"{output_prefix}_throughput", output_dir)
        plot_bar_chart(memory_data, "Not-Yet-Reclaimed Memory, bytes", f"{output_prefix}_memory", output_dir)

        print(f"Charts saved in {os.path.basename(output_dir)} for: {output_prefix}")

    except Exception as e:
        print(f"Error processing {file_path}: {e}")

