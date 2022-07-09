import matplotlib.pyplot as plt
import os
import yaml
import math
import numpy as np
import argparse
import pickle

DATA_DIR = "/work/scratch/tj75qeje/mpi-comp-match/output//2"
# DATA_DIR = "/home/tj75qeje/mpi-comp-match/IMB-ASYNC/output"
# DATA_DIR = "/home/tj75qeje/mpi-comp-match/IMB-ASYNC/output_inside"

CACHE_FILE="cache.pkl"
PLTSIZE=(18, 12)


NORMAL = 1
EAGER = 2
RENDEVOUZ1 = 3
RENDEVOUZ2 = 4

names = {NORMAL: "NORMAL", EAGER: "EAGER", RENDEVOUZ1: "RENDEVOUZ1", RENDEVOUZ2: "RENDEVOUZ2"}
# color sceme by Paul Tol https://personal.sron.nl/~pault/
colors = {NORMAL: "#4477AA", EAGER: "#EE6677", RENDEVOUZ1: "#AA3377", RENDEVOUZ2: "#228833"}

buffer_sizes = [4, 8, 32, 512, 1024, 4096, 16384, 65536, 262144, 1048576, 4194304, 16777216]
nrows = 3

# limited set for faster measurement
# buffer_sizes = [8,1024,16384,65536,262144,1048576,4194304,16777216]
# buffer_sizes = [8,32,512,1024,16384]
nrows = 2

upper = 95
lower = 5


def mean_percentile_range(array, upper, lower):
    # from:
    # https://stackoverflow.com/questions/61391138/numpy-mean-percentile-range-eg-mean-25th-to-50th-percentile
    # find the indexes of the element below 25th and 50th percentile
    idx_under_25 = np.argwhere(array < np.percentile(array, lower))
    idx_under_50 = np.argwhere(array <= np.percentile(array, upper))
    # the alues for 25 and 50 are both included

    # find the number of the elements in between 25th and 50th percentile
    diff_num = len(idx_under_50) - len(idx_under_25)

    # find the sum difference
    diff_sum = np.sum(np.take(array, idx_under_50)) - np.sum(np.take(array, idx_under_25))

    # get the mean
    mean = diff_sum / diff_num
    return mean


# get min, may, avg for specified buf_size
def extract_data(data, buf_size):
    x = []
    y_min = []
    y_max = []
    y_avg = []

    # key is calctime, value the dict mapping bufsize and overhead
    for calctime, value in data.items():
        x.append(float(calctime))
        y = []
        for mearsurement in value:
            if buf_size in mearsurement:
                y.append(float(mearsurement[buf_size]))
        # print (y)
        y_min.append(np.percentile(y, lower))
        y_max.append(np.percentile(y, upper))
        y_avg.append(mean_percentile_range(y, upper, lower))

    order = np.argsort(x)
    x_sort = np.array(x)[order]
    y_min_sort = np.array(y_min)[order]
    y_max_sort = np.array(y_max)[order]
    y_avg_sort = np.array(y_avg)[order]

    return x_sort, y_min_sort, y_max_sort, y_avg_sort


def get_plot(data, plot_name,scaling=True,fill=False,normal=True,eager=True,rendevouz1=True,rendevouz2=True):
    ftsize = 16
    plt.rcParams.update({'font.size': ftsize})
    # plt.rcParams.update({'font.size': 18, 'hatch.linewidth': 0.0075})
    figsz = PLTSIZE

    ncols = math.ceil(len(buffer_sizes) * 1.0 / nrows)

    fig, axs = plt.subplots(nrows=nrows, ncols=ncols, figsize=figsz, sharex=True, sharey='row')

    # reshape axis to have 1d-array we can iterate over
    for ax, buf_size in zip(axs.reshape(-1), buffer_sizes):

        # plt.title("Upper:No buffer corruption, lower buffer corruption due to datarace")
        if buf_size < 1024:
            ax.set_title(("%dB" % buf_size), fontsize=ftsize)
        elif buf_size < 1048576:
            ax.set_title(("%dKiB" % (buf_size / 1024)), fontsize=ftsize)
        else:
            ax.set_title(("%dMiB" % (buf_size / 1048576)), fontsize=ftsize)

        # ax.set_xlabel("calculation time")
        # ax.set_ylabel("communication overhead in seconds")

        # get the data
        if normal:
            max_y = add_line_plot(NORMAL,ax, buf_size, data, fill)
        if eager:
            _ = add_line_plot(EAGER,ax, buf_size, data, fill)
        if normal:
            _ = add_line_plot(RENDEVOUZ1,ax, buf_size, data, fill)
        if normal:
            _ = add_line_plot(RENDEVOUZ2,ax, buf_size, data, fill)

        # locator = plt.MaxNLocator(nbins=7)
        # ax.xaxis.set_major_locator(locator)
        ax.locator_params(axis='x', tight=True, nbins=4)

        ax.legend(loc='upper right')

        if scaling:
            ax.set_ylim(0, max_y * 1.05)

        # convert to seconds easy comparision with y axis
        xtics = [0, 5000, 10000]
        labels = [0, 0.005, 0.01]
        ax.set_xticks(xtics)
        ax.set_xticklabels(labels)

    plt.setp(axs[-1, :], xlabel='calculation time (s)')
    plt.setp(axs[:, 0], ylabel='communication overhead (s)')
    plt.tight_layout()
    output_format = "pdf"
    plt.savefig(plot_name + "." + output_format, bbox_inches='tight')


def add_line_plot(key,ax, buf_size, data, fill):
    x, y_min, y_max, y_avg = extract_data(data[key], buf_size)
    max_y = np.max(y_max)  # axis scaling
    ax.plot(x, y_avg, label=names[key], color=colors[key])
    if (fill):
        ax.fill_between(x, y_min, y_max, facecolor=colors[key], alpha=0.5)
    return max_y


def read_data():
    print("Read Data ...")
    data_NOwarmup = {NORMAL: {}, EAGER: {}, RENDEVOUZ1: {}, RENDEVOUZ2: {}}

    data = {NORMAL: {}, EAGER: {}, RENDEVOUZ1: {}, RENDEVOUZ2: {}}

    # read input data
    with os.scandir(DATA_DIR) as dir:
        for entry in dir:
            if entry.is_file():
                # original or modified run
                mode = -1
                if "normal" in entry.name:
                    mode = NORMAL
                elif "eager" in entry.name:
                    mode = EAGER
                elif "rendevouz1" in entry.name:
                    mode = RENDEVOUZ1
                elif "rendevouz2" in entry.name:
                    mode = RENDEVOUZ2
                else:
                    print("Error readig file:")
                    print(entry.name)
                    exit(-1)

                # get calctime
                calctime = int(entry.name.split("_")[-1].split(".")[0])
                # print(calctime)

                # read data
                run_data = {}
                with open(DATA_DIR + "/" + entry.name, "r") as stream:
                    try:
                        run_data = yaml.safe_load(stream)
                    except yaml.YAMLError as exc:
                        print(exc)

                # only care about the overhead
                # if there is data available
                if run_data is not None:
                    run_data = run_data["async_persistentpt2pt"]["over_full"]
                    if calctime not in data[mode]:
                        data[mode][calctime] = []
                    data[mode][calctime].append(run_data)

    return data, data_NOwarmup


def print_stat(data, key, buf_size, base_val):
    measurement_count = 0
    sum = 0
    values=[]
    # only print stats for maximum comp time
    comp_time = max(data[key],key=int)
    for measurement in data[key][comp_time]:
        if buf_size in measurement:
            measurement_count += 1
            values.append(float(measurement[buf_size]))

    avg = 1

    if measurement_count > 0:
        avg = mean_percentile_range(values,upper,lower)
        if base_val == -1:
            print("%s: %d measurements, 0%% improvement (avg)" % (names[key], measurement_count))
        else:
            improvement = 100*(base_val-avg)/base_val
            print("%s: %d measurements, %f%% improvement (avg)" % (names[key], measurement_count, improvement))
    else:
        print("%s: 0 measurements" % names[key])
    return measurement_count, avg


def print_statistics(data):
    for buf_size in buffer_sizes:
        print("")
        print(buf_size)
        measurement_count, avg = print_stat(data, NORMAL, buf_size, -1)
        measurement_count, _ = print_stat(data, EAGER, buf_size, avg)
        measurement_count, _ = print_stat(data, RENDEVOUZ1, buf_size, avg)
        measurement_count, _ = print_stat(data, RENDEVOUZ2, buf_size, avg)


def main():
    parser = argparse.ArgumentParser(description='Generates Visualization')
    parser.add_argument('--cache', help='use the content of the cache file named %s. do NOT set this argument, if you want to re-write the cache'%CACHE_FILE)
    args = parser.parse_args()
    if args.cache:
        with open(CACHE_FILE, 'rb') as f:
            data = pickle.load(f)
    else:
        data, data_NOwarmup = read_data()
        with open(CACHE_FILE, 'wb') as f:
            pickle.dump(data, f)


    print_statistics(data)

    print("generating plots ...")

    get_plot(data, "overhead_scaled",scaling=True, fill=False)
    get_plot(data, "overhead_scaled_filled",scaling=True, fill=True)
    get_plot(data, "overhead",scaling=False, fill=False)

    # get_plot(data_NOwarmup, True, "noWarmup_scaled")
    # get_plot(data_NOwarmup, False, "noWarmup")

    print("done")


if __name__ == "__main__":
    main()
