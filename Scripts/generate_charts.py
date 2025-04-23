import subprocess
import os

#
# The main script which runs all other scripts to generate charts
# (you can comment out those that you do not need)
#

SCRIPTS_DIR = os.path.dirname(os.path.abspath(__file__))


chart_scripts = [
    os.path.join(SCRIPTS_DIR, "generate_list_charts.py"),
    os.path.join(SCRIPTS_DIR, "generate_list_random_charts.py"),
    os.path.join(SCRIPTS_DIR, "generate_queue_charts.py"),
    os.path.join(SCRIPTS_DIR, "generate_queue_random_charts.py"),
    os.path.join(SCRIPTS_DIR, "generate_tree_charts.py")
]

for script in chart_scripts:
    script_name = os.path.basename(script)
    print(f"\nRunning script: {script_name}...\n")
    try:
        subprocess.run(["python3", script], check=True)
        print(f"\nCompleted {script_name}!\n")
    except subprocess.CalledProcessError as e:
        print(f"Error executing {script}: {e}")

