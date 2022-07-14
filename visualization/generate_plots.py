import matplotlib.pyplot as plt
import matplotlib.patches as mpatches
import os
import yaml
import math
import numpy as np
import argparse
import pickle

# DATA_DIR = "/work/scratch/tj75qeje/mpi-comp-match/output/2"
DATA_DIR = "/work/scratch/tj75qeje/mpi-comp-match/output/measurement_2"
# DATA_DIR = "/work/scratch/tj75qeje/mpi-comp-match/output/measurement_2_no_warmup"

CACHE_FILE = "cache.pkl"
PLTSIZE = (12, 4)
PLTSIZE_BAR_PLT = (12, 4)
PLTSIZE_VIOLIN_PLT = (24, 4)

NORMAL = 1
EAGER = 2
RENDEVOUZ1 = 3
RENDEVOUZ2 = 4

names = {NORMAL: "NORMAL", EAGER: "EAGER", RENDEVOUZ1: "RENDEVOUZ1", RENDEVOUZ2: "RENDEVOUZ2"}
# color sceme by Paul Tol https://personal.sron.nl/~pault/
colors = {NORMAL: "#4477AA", EAGER: "#EE6677", RENDEVOUZ1: "#AA3377", RENDEVOUZ2: "#228833"}

buffer_sizes = [4, 8, 32, 512, 1024, 4906, 16384, 165536, 1048576, 4194304, 16777216]
# buffer_sizes = [4, 8, 32, 512, 1024, 16384, 1048576, 4194304, 16777216]
# buffer_sizes = [512,4194304,16777216]
buffer_sizes_with_full_plot = [1024, 1048576]
nrows = 1

comptime_for_barplots = 10000

# limited set for faster measurement
# buffer_sizes = [8,1024,16384,65536,262144,1048576,4194304,16777216]
# buffer_sizes = [8,32,512,1024,16384]
# nrows = 2

upper = 100
lower = 0


def mean_percentile_range(array, upper, lower):
    # from:
    # https://stackoverflow.com/questions/61391138/numpy-mean-percentile-range-eg-mean-25th-to-50th-percentile
    # find the indexes of the element below 25th and 50th percentile
    idx_under_25 = np.argwhere(array < np.percentile(array, lower))
    idx_under_50 = np.argwhere(array <= np.percentile(array, upper))
    # the values for 25 and 50 are both included

    # find the number of the elements in between 25th and 50th percentile
    diff_num = len(idx_under_50) - len(idx_under_25)

    # find the sum difference
    diff_sum = np.sum(np.take(array, idx_under_50)) - np.sum(np.take(array, idx_under_25))

    # get the mean
    mean = diff_sum / diff_num
    return mean


# get min, may, avg for specified buf_size
def extract_data_calctime(data, buf_size):
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


def get_data_bufsize(data, buf_size, calctime):
    y = []

    # key is calctime, value the dict mapping bufsize and overhead

    for mearsurement in data[calctime]:
        if buf_size in mearsurement:
            y.append(float(mearsurement[buf_size]))
        # print (y)
    if not y:
        # empty
        return 0, 0, 0, 0, [0]
    y_min = np.percentile(y, lower)
    y_max = np.percentile(y, upper)
    y_avg = mean_percentile_range(y, upper, lower)

    y = [yy for yy in y if yy >= y_min and yy <= y_max]
    y_median = np.median(y)

    return y_min, y_max, y_avg, y_median, y


def add_bar(ax,x, data, key, buf_size, comp_time, show_in_legend=True):
    min, max, avg, median, _ = get_data_bufsize(data[key], buf_size, comp_time)
    if show_in_legend:
        label = names[key]
    else:
        label = '_nolegend_'
    container = ax.bar(x, avg, color=colors[key], yerr=[[min], [max]], align='edge', label=label)
    connector, caplines, (vertical_lines,) = container.errorbar.lines
    vertical_lines.set_color(colors[key])
    # add a line showing the medial
    ax.hlines(median, x, x + 1, colors="black")
    return max


def add_violin(ax,x, data, key, buf_size, comp_time, show_in_legend=True):
    _, max, _, _, y = get_data_bufsize(data[key], buf_size, comp_time)

    violin_parts = ax.violinplot([y], [x*2], widths=[1.5], quantiles=[lower / 100, upper / 100], showmeans=True,showmedians=True, showextrema=False)
    for pc in violin_parts['bodies']:
        pc.set_color(colors[key])

    return (mpatches.Patch(color=colors[key]), names[key]), max


def get_bar_plot(data, buffer_sizes, comp_time, plot_name, scaling=True, fill=False, normal=True, eager=True,
                 rendevouz1=True, rendevouz2=True, scaling_key=NORMAL,split_point=5):
    ftsize = 16
    plt.rcParams.update({'font.size': ftsize})
    # plt.rcParams.update({'font.size': 18, 'hatch.linewidth': 0.0075})
    figsz = PLTSIZE_BAR_PLT

    local_split_point=split_point
    num_bars = 1  # the space in between two different buffer length
    if normal:
        num_bars += 1
    if eager:
        num_bars += 1
    if rendevouz1:
        num_bars += 1
    if rendevouz2:
        num_bars += 1

    y_pos = range(num_bars * len(buffer_sizes))
    y_scale = 0

    fig, axs = plt.subplots(nrows=1, ncols=2, figsize=figsz, sharex=False, sharey=False)

    current_bar = 0
    show_in_legend = True;


    i=0
    for ax in axs:
        x_tics_labels = []
        x_tics = []
        # reshape axis to have 1d-array we can iterate over
        while i < len(buffer_sizes):
            if current_bar >= local_split_point * num_bars:
                local_split_point = len(buffer_sizes)
                show_in_legend=True
                break

            buf_size=buffer_sizes[i]
            i+=1

            x_tics.append(num_bars / 2 + current_bar)
            if eager:
                max_y = add_bar(ax,current_bar, data, EAGER, buf_size, comp_time, show_in_legend)
                current_bar += 1
                if scaling_key == EAGER:
                    y_scale = max(max_y, y_scale)
            if rendevouz1:
                max_y = add_bar(ax,current_bar, data, RENDEVOUZ1, buf_size, comp_time, show_in_legend)
                current_bar += 1
                if scaling_key == RENDEVOUZ1:
                    y_scale = max(max_y, y_scale)
            if rendevouz2:
                max_y = add_bar(ax,current_bar, data, RENDEVOUZ2, buf_size, comp_time, show_in_legend)
                current_bar += 1
                if scaling_key == RENDEVOUZ2:
                    y_scale = max(max_y, y_scale)
            if normal:
                max_y = add_bar(ax,current_bar, data, NORMAL, buf_size, comp_time, show_in_legend)
                current_bar += 1
                if scaling_key == NORMAL:
                    y_scale = max(max_y, y_scale)

            current_bar += 1
            show_in_legend = False

            if buf_size < 1024:
                x_tics_labels.append("%d\nB" % buf_size)
            elif buf_size < 1048576:
                x_tics_labels.append("%d\nKiB" % (buf_size / 1024))
            else:
                x_tics_labels.append("%d\nMiB" % (buf_size / 1048576))

        ax.set_xlabel("Buffer Size")
        if ax == axs[0]:
            ax.set_ylabel("communication overhead in seconds")
        else:
            ax.yaxis.tick_right()

        # locator = plt.MaxNLocator(nbins=7)
        # ax.xaxis.set_major_locator(locator)
        # ax.locator_params(axis='x', tight=True, nbins=4)

        if ax==axs[1]:
            ax.legend(loc='upper left')

        if scaling:
            ax.set_ylim(0, y_scale * 1.05)

        # convert to seconds easy comparision with y axis

        ax.set_xticks(x_tics)
        ax.set_xticklabels(x_tics_labels)

    plt.tight_layout()
    # plt.tight_layout()
    output_format = "pdf"
    plt.savefig(plot_name + "." + output_format, bbox_inches='tight')


def get_violin_plot(data, buffer_sizes, comp_time, plot_name, scaling=True, fill=False, normal=True, eager=True,
                    rendevouz1=True, rendevouz2=True, scaling_key=NORMAL,split_point=5):
    ftsize = 16
    plt.rcParams.update({'font.size': ftsize})
    # plt.rcParams.update({'font.size': 18, 'hatch.linewidth': 0.0075})
    figsz = PLTSIZE_VIOLIN_PLT

    num_violins = 1  # the space in between two different buffer length
    if normal:
        num_violins += 1
    if eager:
        num_violins += 1
    if rendevouz1:
        num_violins += 1
    if rendevouz2:
        num_violins += 1

    y_pos = range(num_violins *2* len(buffer_sizes))
    y_scale =0

    fig, axs = plt.subplots(nrows=1, ncols=2, figsize=figsz, sharex=False, sharey=False)
    local_split_point=split_point

    current_bar = 0
    show_in_legend = True
    i = 0
    for ax in axs:
        legend_labels = []
        x_tics_labels = []
        x_tics = []
        # reshape axis to have 1d-array we can iterate over
        while i < len(buffer_sizes):
            if current_bar >= local_split_point * num_violins:
                local_split_point = len(buffer_sizes)
                show_in_legend = True
                break

            buf_size = buffer_sizes[i]
            i += 1
            x_tics.append(num_violins / 2 + current_bar*2)
            if eager:
                label,max_y = add_violin(ax,current_bar, data, EAGER, buf_size, comp_time, show_in_legend)
                current_bar += 1
                if show_in_legend:
                    legend_labels.append(label)
                if scaling_key == EAGER:
                    y_scale = max(max_y, y_scale)
            if rendevouz1:
                label,max_y = add_violin(ax,current_bar, data, RENDEVOUZ1, buf_size, comp_time, show_in_legend)
                current_bar += 1
                if show_in_legend:
                    legend_labels.append(label)
                if scaling_key == RENDEVOUZ1:
                    y_scale = max(max_y, y_scale)
            if rendevouz2:
                label,max_y = add_violin(ax,current_bar, data, RENDEVOUZ2, buf_size, comp_time, show_in_legend)
                current_bar += 1
                if show_in_legend:
                    legend_labels.append(label)
                if scaling_key == RENDEVOUZ2:
                    y_scale = max(max_y, y_scale)
            if normal:
                label,max_y = add_violin(ax,current_bar, data, NORMAL, buf_size, comp_time, show_in_legend)
                current_bar += 1
                if show_in_legend:
                    legend_labels.append(label)
                if scaling_key == NORMAL:
                    y_scale = max(max_y, y_scale)
            current_bar += 1
            show_in_legend = False

            if buf_size < 1024:
                x_tics_labels.append("%d\nB" % buf_size)
            elif buf_size < 1048576:
                x_tics_labels.append("%d\nKiB" % (buf_size / 1024))
            else:
                x_tics_labels.append("%d\nMiB" % (buf_size / 1048576))

        ax.set_xlabel("Buffer Size")
        if ax == axs[0]:
            ax.set_ylabel("communication overhead in seconds")
        else:
            ax.yaxis.tick_right()

        # locator = plt.MaxNLocator(nbins=7)
        # ax.xaxis.set_major_locator(locator)
        # ax.locator_params(axis='x', tight=True, nbins=4)
        if ax == axs[1]:
            ax.legend(*zip(*legend_labels), loc='upper left')

        if scaling:
           ax.set_ylim(0, y_scale * 1.05)

        # convert to seconds easy comparision with y axis

        ax.set_xticks(x_tics)
        ax.set_xticklabels(x_tics_labels)

    plt.tight_layout()
    output_format = "pdf"
    plt.savefig(plot_name + "." + output_format, bbox_inches='tight')


def get_comp_time_plot(data, buffer_sizes, plot_name, scaling=True, fill=False, normal=True, eager=True,
                       rendevouz1=True, rendevouz2=True):
    ftsize = 16
    plt.rcParams.update({'font.size': ftsize})
    # plt.rcParams.update({'font.size': 18, 'hatch.linewidth': 0.0075})
    figsz = PLTSIZE

    ncols = math.ceil(len(buffer_sizes) * 1.0 / nrows)

    if nrows > 1:
        sharey = 'row'
    else:
        sharey = False

    fig, axs = plt.subplots(nrows=nrows, ncols=ncols, figsize=figsz, sharex=True, sharey=sharey)

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
            max_y = add_line_plot(NORMAL, ax, buf_size, data, fill)
        if eager:
            _ = add_line_plot(EAGER, ax, buf_size, data, fill)
        if normal:
            _ = add_line_plot(RENDEVOUZ1, ax, buf_size, data, fill)
        if normal:
            _ = add_line_plot(RENDEVOUZ2, ax, buf_size, data, fill)

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
    if nrows > 1:
        plt.setp(axs[-1, :], xlabel='calculation time (s)')
        plt.setp(axs[:, 0], ylabel='communication overhead (s)')
    else:
        plt.setp(axs, xlabel='calculation time (s)')
        plt.setp(axs[0], ylabel='communication overhead (s)')
        plt.setp(axs[-1], ylabel='communication overhead (s)', )
        axs[-1].yaxis.set_label_position('right')
        axs[-1].yaxis.tick_right()
    plt.tight_layout()
    output_format = "pdf"
    plt.savefig(plot_name + "." + output_format, bbox_inches='tight')


def add_line_plot(key, ax, buf_size, data, fill):
    x, y_min, y_max, y_avg = extract_data_calctime(data[key], buf_size)
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

                try:
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
                except ValueError:
                    print("could not read %s" % entry.name)

    return data, data_NOwarmup


def print_stat(data, key, buf_size, base_val):
    measurement_count = 0
    sum = 0
    values = []
    # only print stats for maximum comp time
    comp_time = max(data[key], key=int)
    for measurement in data[key][comp_time]:
        if buf_size in measurement:
            measurement_count += 1
            values.append(float(measurement[buf_size]))

    avg = 1

    if measurement_count > 0:
        avg = mean_percentile_range(values, upper, lower)
        if base_val == -1:
            print("%s: %d measurements, 0%% improvement (avg)" % (names[key], measurement_count))
        else:
            improvement = 100 * (base_val - avg) / base_val
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
    parser.add_argument('--cache',
                        help='use the content of the cache file named %s. do NOT set this argument, if you want to re-write the cache' % CACHE_FILE)
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

    # get_comp_time_plot(data, buffer_sizes_with_full_plot,"overhead_scaled",scaling=True, fill=False,eager=False)
    # get_comp_time_plot(data, buffer_sizes_with_full_plot,"overhead_scaled_filled",scaling=True, fill=True,eager=False)
    # get_comp_time_plot(data, buffer_sizes_with_full_plot,"overhead",scaling=False, fill=False,eager=False)

    get_bar_plot(data, buffer_sizes, comptime_for_barplots, "overhead_bars")
    get_bar_plot(data, buffer_sizes, comptime_for_barplots, "overhead_bars_scaled", scaling=True)
    get_violin_plot(data, buffer_sizes, comptime_for_barplots, "overhead_violins")
    get_violin_plot(data, buffer_sizes, comptime_for_barplots, "overhead_violins_scaled", scaling=True)

    # get_plot(data_NOwarmup, True, "noWarmup_scaled")
    # get_plot(data_NOwarmup, False, "noWarmup")

    print("done")


if __name__ == "__main__":
    main()
