import os
import re
import glob
import numpy as np
import matplotlib.pyplot as plt
import cairosvg
import shutil

# Paths
DATA_DIR = os.path.abspath(os.path.join(os.path.dirname(__file__), "..", "Data"))
LIST_RESULTS_DIR = os.path.join(DATA_DIR, "list_output_results")
LIST_CHARTS_DIR = os.path.join(DATA_DIR, "list_charts")

os.makedirs(LIST_CHARTS_DIR, exist_ok=True)

# Clean old charts
for item in glob.glob(os.path.join(LIST_CHARTS_DIR, "*")):
    if os.path.isfile(item):
        os.remove(item)
    elif os.path.isdir(item):
        shutil.rmtree(item)

# Files
txt_files = glob.glob(os.path.join(LIST_RESULTS_DIR, "*.txt"))
if not txt_files:
    print(f"No .txt files found in {LIST_RESULTS_DIR}.")
    exit(1)

# Config
hatches = ['-', '\\', '/', '*']
colors = ['gold', 'g', 'm', 'c']
benchmarks = ["List-HP", "List-ABA-HP (New)", "List-EBR", "List-ABA-EBR (New)"]
pattern = re.compile(
    r"Threads,\s*HarrisMichaelListHP,\s*DWHarrisMichaelListHP,\s*HarrisMichaelListEBR,\s*DWHarrisMichaelListEBR,"
    r"\s*HarrisMichaelListHP_Memory_Usage,\s*DWHarrisMichaelListHP_Memory_Usage,"
    r"\s*HarrisMichaelListEBR_Memory_Usage,\s*DWHarrisMichaelListEBR_Memory_Usage"
)

# Chart plotting
def plot_bar_chart(data, ylabel, output_name):
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

    legend = ax.legend(prop={'size': 16, 'weight': 'bold'}, frameon=True, fancybox=False,
                       edgecolor='black', handletextpad=1.5)
    frame = legend.get_frame()
    frame.set_linestyle('--')
    frame.set_linewidth(1.5)
    for text in legend.get_texts():
        text.set_fontsize(17)
        text.set_fontweight('bold')

    svg_path = os.path.join(LIST_CHARTS_DIR, f"{output_name}.svg")
    pdf_path = os.path.join(LIST_CHARTS_DIR, f"{output_name}.pdf")

    plt.savefig(svg_path, format="svg", bbox_inches="tight")
    plt.close(fig)
    cairosvg.svg2pdf(url=svg_path, write_to=pdf_path)

# Process each file
for file_path in txt_files:
    file_name = os.path.splitext(os.path.basename(file_path))[0]  # e.g., P_128B_4096_50%
    parts = file_name.split("_")

    if len(parts) != 4:
        print(f"Skipping file with unexpected format: {file_name}")
        continue

    _, workload, _, recycling = parts
    recycling = recycling.replace("%", "p")  # sanitize

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
            if len(values) == 9:
                throughput_data.append(values[:5])      # Threads + 4 benchmarks
                memory_data.append([values[0]] + values[5:])  # Threads + 4 memory columns

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

    output_prefix = f"list_{workload}_{recycling}"
    plot_bar_chart(throughput_data, "Throughput, ops/sec", f"{output_prefix}_throughput")
    plot_bar_chart(memory_data, "Not-Yet-Reclaimed Memory, bytes", f"{output_prefix}_memory")

    print(f"Charts saved for: {output_prefix}")

