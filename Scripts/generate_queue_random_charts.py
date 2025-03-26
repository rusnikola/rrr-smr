import os
import re
import glob
import numpy as np
import matplotlib.pyplot as plt
import cairosvg

# Base directory setup
DATA_DIR = os.path.abspath(os.path.join(os.path.dirname(__file__), "..", "Data"))
QUEUE_RESULTS_DIR = os.path.join(DATA_DIR, "queue_output_results", "RecyclingPercentage")
PAIRWISE_RESULTS_DIR = os.path.join(QUEUE_RESULTS_DIR, "Pairwise")
RANDOM_RESULTS_DIR = os.path.join(QUEUE_RESULTS_DIR, "Random")
BASE_QUEUE_CHARTS_DIR = os.path.join(DATA_DIR, "queue_charts", "RecyclingPercentage")

# Ensure chart output base dir exists
os.makedirs(BASE_QUEUE_CHARTS_DIR, exist_ok=True)

# Config
recycling_percentages = ["10%", "20%", "30%", "40%"]
workloads = ["0B", "64KB"]
hatches = ['-', '\\', '/', '*', 'O', '.']
colors = ['gold', 'g', 'm', 'c', 'r', 'teal']
benchmarks = [
    "Queue-HP", "Queue-ABA-HP", "ModQueue-ABA-HP",
    "Queue-EBR", "Queue-ABA-EBR", "ModQueue-ABA-EBR"
]

pattern = re.compile(
    r"Threads,\s*MSQueueHP,\s*MSQueueABAHP,\s*ModQueueHP,\s*MSQueueEBR,\s*MSQueueABAEBR,\s*ModQueueEBR,"
    r"\s*MSQueueHP_Memory_Usage,\s*MSQueueABAHP_Memory_Usage,\s*ModQueueHP_Memory_Usage,"
    r"\s*MSQueueEBR_Memory_Usage,\s*MSQueueABAEBR_Memory_Usage,\s*ModQueueEBR_Memory_Usage"
)

x_labels = recycling_percentages
x = np.arange(len(x_labels))
bar_width = 0.13

def plot_summary_bar_chart(data_dict, ylabel, chart_name, output_subfolder):
    fig, ax = plt.subplots(figsize=(16, 10))

    for i, bm in enumerate(benchmarks):
        ax.bar(x + i * bar_width, data_dict[bm], bar_width, color=colors[i], hatch=hatches[i])

    ax.set_xlabel("Recycling Percentage", fontsize=25, fontweight='bold', labelpad=5)
    ax.set_ylabel(ylabel, fontsize=25, fontweight='bold', labelpad=25)
    ax.set_xticks(x + bar_width * (len(benchmarks) - 1) / 2)
    ax.set_xticklabels(x_labels, fontsize=22)

    ax.ticklabel_format(axis='y', style='sci', scilimits=(0, 0), useMathText=True)
    ax.yaxis.offsetText.set_fontsize(22)
    for tick in ax.get_yticklabels():
        tick.set_fontsize(22)

    output_chart_dir = os.path.join(BASE_QUEUE_CHARTS_DIR, output_subfolder)
    os.makedirs(output_chart_dir, exist_ok=True)

    svg_path = os.path.join(output_chart_dir, f"queue_{chart_name}.svg")
    pdf_path = os.path.join(output_chart_dir, f"queue_{chart_name}.pdf")

    plt.savefig(svg_path, format="svg", bbox_inches="tight")
    plt.close(fig)
    cairosvg.svg2pdf(url=svg_path, write_to=pdf_path)

# Loop through Pairwise and Random modes
for mode_dir, mode_label, mode_prefix in [
    (PAIRWISE_RESULTS_DIR, "Pairwise", "P"),
    (RANDOM_RESULTS_DIR, "Random", "R")
]:
    for workload in workloads:
        throughput_data = {bm: [] for bm in benchmarks}
        memory_data = {bm: [] for bm in benchmarks}

        for perc in recycling_percentages:
            search_pattern = f"{mode_prefix}_{workload}_*_{perc}.txt"
            matching_files = glob.glob(os.path.join(mode_dir, search_pattern))

            if not matching_files:
                print(f"No matching file for {perc} and workload {workload}.")
                for bm in benchmarks:
                    throughput_data[bm].append(0)
                    memory_data[bm].append(0)
                continue

            file_path = matching_files[0]
            with open(file_path, "r") as file:
                data_started = False
                for line in file:
                    if not data_started:
                        if pattern.search(line):
                            data_started = True
                        continue

                    values = [int(x.strip()) for x in line.split(",") if x.strip().isdigit()]
                    if len(values) >= 13 and values[0] == 32:
                        throughput_data[benchmarks[0]].append(values[1])
                        throughput_data[benchmarks[1]].append(values[2])
                        throughput_data[benchmarks[2]].append(values[3])
                        throughput_data[benchmarks[3]].append(values[4])
                        throughput_data[benchmarks[4]].append(values[5])
                        throughput_data[benchmarks[5]].append(values[6])

                        memory_data[benchmarks[0]].append(values[7])
                        memory_data[benchmarks[1]].append(values[8])
                        memory_data[benchmarks[2]].append(values[9])
                        memory_data[benchmarks[3]].append(values[10])
                        memory_data[benchmarks[4]].append(values[11])
                        memory_data[benchmarks[5]].append(values[12])
                        break

        plot_summary_bar_chart(throughput_data, "Throughput, ops/sec", f"rp_throughput_{mode_label}_{workload}", mode_label)
        plot_summary_bar_chart(memory_data, "Not-Yet-Reclaimed Memory, bytes", f"rp_memory_{mode_label}_{workload}", mode_label)

relative_path = os.path.relpath(BASE_QUEUE_CHARTS_DIR, DATA_DIR)
print(f"Summary charts saved in {os.path.basename(DATA_DIR)}/{relative_path}!")

