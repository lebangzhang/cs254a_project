import argparse
import matplotlib.pyplot as plt
import re
import numpy as np

def main():
	parser = argparse.ArgumentParser(description="Plot data from log files with x/y labels.")
	parser.add_argument('--categories', nargs='+', required=True, help='Benchmark X-axis Categories for each plot.')
	parser.add_argument('--ylabels', nargs='+', required=True, help='Performance Counter Y-axis label for each plot.')
	parser.add_argument('--logfiles', nargs='+', required=True, help='Path to log file with X Y data.')
	parser.add_argument('--counters', nargs='+', required=True, help='Key to performance counter')
	parser.add_argument('--titles', nargs='+', required=True, help='Title for each graph')
	parser.add_argument('--xlabel', required=True, help='X-axis label')
	
	args = parser.parse_args()

	print(args.categories)
	values = np.empty((len(args.counters), len(args.logfiles)))
	
	for i in range(len(args.logfiles)):
		logfile = args.logfiles[i]
		with open(logfile, 'r') as file:
			content = file.read()
			
			for j in range(len(args.counters)):
				
				match = re.findall(args.counters[j] + r'=(\d+(?:\.\d+)?)', content)
				values[j][i] = float(match[-1])
				
	
	for i in range(len(args.counters)):
		plt.figure(i)
		plt.xlabel(args.xlabel)
		plt.ylabel(args.ylabels[i])
		plt.title(args.titles[i])
		plt.bar(args.categories, values[i])
		
	plt.show()
if __name__ == "__main__":
	main()

