import os
import re
import glob
import numpy as np
import matplotlib.pyplot as plt
import cairosvg

#
# This script processes list data and generates charts
# (recycling percentage tests)
#

DATA_DIR = os.path.abspath(os.path.join(os.path.dirname(__file__), "..", "Data"))
LIST_RESULTS_DIR = os.path.join(DATA_DIR, "list_output_results")
LIST_CHARTS_DIR = os.path.join(DATA_DIR, "list_charts", "RecyclingPercentage")

os.makedirs(LIST_CHARTS_DIR, exist_ok=True)

recycling_percentages = ["10%", "20%", "30%", "40%"]
workloads = ["128B", "64KB", "1KB"] # 1KB is for the lightweight test
hatches = ['-', '\\', '/', '*']
colors = ['gold', 'g', 'm', 'c']
benchmarks = ["List-HP", "List-ABA-HP (New)", "List-EBR", "List-ABA-EBR (New)"]

pattern = re.compile(r"Threads,\s*HarrisMichaelListHP,\s*DWHarrisMichaelListHP,\s*HarrisMichaelListEBR,\s*DWHarrisMichaelListEBR,\s*HarrisMichaelListHP_Memory_Usage,\s*DWHarrisMichaelListHP_Memory_Usage,\s*HarrisMichaelListEBR_Memory_Usage,\s*DWHarrisMichaelListEBR_Memory_Usage")

x_labels = recycling_percentages
x = np.arange(len(x_labels))
bar_width = 0.2


def plot_summary_bar_chart(data_dict, ylabel, chart_name):
    fig, ax = plt.subplots(figsize=(16, 10))

    for i, bm in enumerate(benchmarks):
        ax.bar(x + i * bar_width, data_dict[bm], bar_width, label=bm,
               color=colors[i], hatch=hatches[i])

    ax.set_xlabel("Recycling Percentage", fontsize=25, fontweight='bold', labelpad=5)
    ax.set_ylabel(ylabel, fontsize=25, fontweight='bold', labelpad=25)
    ax.set_xticks(x + bar_width * (len(benchmarks) - 1) / 2)
    ax.set_xticklabels(x_labels, fontsize=22)

    ax.ticklabel_format(axis='y', style='sci', scilimits=(0, 0), useMathText=True)
    ax.yaxis.offsetText.set_fontsize(22)

    for tick in ax.get_yticklabels():
        tick.set_fontsize(22)

    
    svg_path = os.path.join(LIST_CHARTS_DIR, f"list_{chart_name}.svg")
    pdf_path = os.path.join(LIST_CHARTS_DIR, f"list_{chart_name}.pdf")

    plt.savefig(svg_path, format="svg", bbox_inches="tight")
    plt.close(fig)
    cairosvg.svg2pdf(url=svg_path, write_to=pdf_path)


for workload in workloads:
    matching_files = glob.glob(os.path.join(LIST_RESULTS_DIR, "RecyclingPercentage", f"*_{workload}_*.txt"))
    if not matching_files:
        continue

    throughput_data = {bm: [] for bm in benchmarks}
    memory_data = {bm: [] for bm in benchmarks}

    for perc in recycling_percentages:
        matching_files = glob.glob(os.path.join(LIST_RESULTS_DIR, "RecyclingPercentage", f"*_{workload}_*_{perc}.txt"))
        if not matching_files:
            print(f"No matching file for {perc} and workload {workload}.")
            for bm in benchmarks:
                throughput_data[bm].append(0)
                memory_data[bm].append(0)
            continue

        file_path = matching_files[0]  # Assume one match per percentage + workload

        with open(file_path, "r") as file:
            data_started = False
            for line in file:
                if not data_started:
                    if pattern.search(line):
                        data_started = True
                    continue

                values = [int(x.strip()) for x in line.split(",") if x.strip().isdigit()]
                if len(values) == 9 and values[0] == 64:
                    throughput_data[benchmarks[0]].append(values[1])
                    throughput_data[benchmarks[1]].append(values[2])
                    throughput_data[benchmarks[2]].append(values[3])
                    throughput_data[benchmarks[3]].append(values[4])

                    memory_data[benchmarks[0]].append(values[5])
                    memory_data[benchmarks[1]].append(values[6])
                    memory_data[benchmarks[2]].append(values[7])
                    memory_data[benchmarks[3]].append(values[8])
                    break

    plot_summary_bar_chart(throughput_data, "Throughput, ops/sec", f"rp_throughput_{workload}")
    plot_summary_bar_chart(memory_data, "Not-Yet-Reclaimed Memory, bytes", f"rp_memory_{workload}")

relative_path = os.path.relpath(LIST_CHARTS_DIR, DATA_DIR)
print(f"Summary charts saved in {os.path.basename(DATA_DIR)}/{relative_path}!")

