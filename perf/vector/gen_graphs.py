import argparse
import matplotlib.pyplot as plt
import re
import numpy as np
import pandas as pd

def main():
    parser = argparse.ArgumentParser(description="Plot data from log files with x/y labels.")
    parser.add_argument('--categories', nargs='+', required=True, help='Benchmark X-axis Categories for each plot.')
    parser.add_argument('--ylabels', nargs='+', required=True, help='Performance Counter Y-axis label for each plot.')
    parser.add_argument('--logfiles', nargs='+', required=True, help='Path to log file with X Y data.')
    parser.add_argument('--counters', nargs='+', required=True, help='Key to performance counter')
    parser.add_argument('--title', required=True, help='Title for each graph')
    parser.add_argument('--xlabel', required=True, help='X-axis label')
    parser.add_argument('--output', required=True, help="Output file path of Excel table")
    parser.add_argument('--plot', required=True, help="Output file path of Figures")

    args = parser.parse_args()

    num_counters = len(args.counters)
    num_categories = len(args.categories)
    values = np.empty((num_counters, len(args.logfiles)))

    for i in range(len(args.logfiles)):
        logfile = args.logfiles[i]
        with open(logfile, 'r') as file:
            content = file.read()

            for j in range(num_counters):
                match = re.findall(args.counters[j] + r'=(\d+(?:\.\d+)?)', content)
                if not match:
                    raise ValueError(f"Counter '{args.counters[j]}' not found in {logfile}")
                values[j][i] = float(match[-1])  # Grab the last match

    # Save data to Excel
    spreadsheet_output = {"Benchmarks": args.categories}
    for i in range(num_counters):
        spreadsheet_output[args.ylabels[i]] = values[i]
    df = pd.DataFrame(spreadsheet_output)
    df.to_excel(args.output, index=False, engine='openpyxl')

    # Plotting
    fig, ax1 = plt.subplots()

    x = np.arange(num_categories)
    width = 0.35

    rects1 = ax1.bar(x - width/2, values[0], width, label=args.ylabels[0], color='blue')
    ax1.set_ylabel(args.ylabels[0], color='blue')
    ax1.tick_params(axis='y', labelcolor='blue')

    ax2 = ax1.twinx()
    rects2 = ax2.bar(x + width/2, values[1], width, label=args.ylabels[1], color='red')
    ax2.set_ylabel(args.ylabels[1], color='red')
    ax2.tick_params(axis='y', labelcolor='red')

    ax1.set_xticks(x)
    ax1.set_xticklabels(args.categories)
    ax1.set_xlabel(args.xlabel)
    plt.title(args.title)

    # Optional: Add legends for clarity
    fig.legend(loc='upper right', bbox_to_anchor=(1, 1), bbox_transform=ax1.transAxes)

    plt.tight_layout()
    plt.savefig(args.plot)
    plt.show()

if __name__ == "__main__":
    main()

