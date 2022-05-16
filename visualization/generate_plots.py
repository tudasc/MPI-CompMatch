import matplotlib.pyplot as plt
import os
import yaml
import math
import numpy as np

DATA_DIR = "/work/scratch/tj75qeje/mpi-comp-match/output"
#DATA_DIR = "/home/tj75qeje/mpi-comp-match/IMB-ASYNC/output"
#DATA_DIR = "/home/tj75qeje/mpi-comp-match/IMB-ASYNC/output_inside"

ORIG = 1
MODIFIED = 2

#buffer_sizes = [4, 8, 32, 512, 1024, 4096, 16384, 65536, 262144, 1048576, 4194304, 16777216]
#nrows = 3

# limited set for faster measurement
buffer_sizes = [8,1024,16384,65536,262144,1048576,4194304,16777216]
nrows = 2

upper=95
lower=5

def mean_percentile_range(array,upper,lower):
    # from:
    #https://stackoverflow.com/questions/61391138/numpy-mean-percentile-range-eg-mean-25th-to-50th-percentile
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
        y=[]
        for mearsurement in value:
            if buf_size in mearsurement:
                y.append(float(mearsurement[buf_size]))
        #print (y)
        y_min.append(np.percentile(y,lower))
        y_max.append(np.percentile(y,upper))
        y_avg.append(mean_percentile_range(y,upper,lower))

    order = np.argsort(x)
    x_sort = np.array(x)[order]
    y_min_sort = np.array(y_min)[order]
    y_max_sort = np.array(y_max)[order]
    y_avg_sort = np.array(y_avg)[order]

    return x_sort, y_min_sort, y_max_sort, y_avg_sort


def get_plot(data, scaling, name):
    ftsize = 12
    plt.rcParams.update({'font.size': ftsize})
    # plt.rcParams.update({'font.size': 18, 'hatch.linewidth': 0.0075})
    figsz = (12, 20)

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
        x, y_min, y_max, y_avg = extract_data(data[ORIG],buf_size)

        orig_avg_time=y_avg[-1]

        max_y = np.max(y_max)# axis scaling

        ax.plot(x, y_avg, label="original",color="blue")
        ax.fill_between(x,y_min,y_max,facecolor="blue",alpha=0.5)

        x, y_min, y_max, y_avg = extract_data(data[MODIFIED], buf_size)

        modified_avg_time = y_avg[-1]

        print("%d : Up to %f%% improvement (avg)" % (buf_size,100*(orig_avg_time-modified_avg_time)/orig_avg_time))

        ax.plot(x, y_avg, label="modified",color="orange")
        ax.fill_between(x,y_min,y_max,facecolor="orange",alpha=0.5)

        # locator = plt.MaxNLocator(nbins=7)
        # ax.xaxis.set_major_locator(locator)
        ax.locator_params(axis='x', tight=True, nbins=4)

        ax.legend(loc='upper right')

        if scaling:
            ax.set_ylim(0, max_y * 1.05)

        # convert to seconds easy comparision with y axis
        xtics=[0,5000,10000]
        labels = [0, 0.005, 0.01]
        ax.set_xticks(xtics)
        ax.set_xticklabels(labels)

    plt.setp(axs[-1, :], xlabel='calculation time (s)')
    plt.setp(axs[:, 0], ylabel='communication overhead (s)')
    plt.tight_layout()
    output_format = "pdf"
    plt.savefig(name + "." + output_format, bbox_inches='tight')


def main():
    print("Read Data ...")
    data_NOwarmup = {}
    data_NOwarmup[ORIG] = {}
    data_NOwarmup[MODIFIED] = {}

    data = {}
    data[ORIG] = {}
    data[MODIFIED] = {}

    # read input data
    with os.scandir(DATA_DIR) as dir:
        for entry in dir:
            if entry.is_file():
                # original or modified run
                mode = -1
                if "orig" in entry.name:
                    mode = ORIG
                elif "modified" in entry.name:
                    mode = MODIFIED
                else:
                    print("Error readig file:")
                    print(entry.name)
                    exit(-1)

                # get calctime
                calctime = int(entry.name.split("_")[-1].split(".")[0])
                #print(calctime)

                # read data
                run_data = {}
                with open(DATA_DIR + "/" + entry.name, "r") as stream:
                    try:
                        run_data = yaml.safe_load(stream)
                    except yaml.YAMLError as exc:
                        print(exc)

                # only care about the overhead
                run_data = run_data["async_persistentpt2pt"]["over_full"]

                if "NOwarmup" in entry.name:
                    if calctime not in data_NOwarmup[mode]:
                        data_NOwarmup[mode][calctime] = []
                    data_NOwarmup[mode][calctime].append(run_data)
                else:
                    if calctime not in data[mode]:
                        data[mode][calctime] = []
                    data[mode][calctime].append(run_data)

    print("generating plots ...")

    get_plot(data, True, "overhead_scaled")
    get_plot(data, False, "overhead")

    get_plot(data_NOwarmup, True, "noWarmup_scaled")
    get_plot(data_NOwarmup, False, "noWarmup")

    print("done")


if __name__ == "__main__":
    main()
