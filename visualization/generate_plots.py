import matplotlib.pyplot as plt
import os
import yaml
import math
import numpy as np

DATA_DIR="/home/tj75qeje/mpi-comp-match/IMB-ASYNC/output"

ORIG=1
MODIFIED=2

buffer_sizes=[4,8,32,512,1024,4096,16384,65536,262144,1048576,4194304,16777216]
buffer_sizes=[4,8,32,512,1024,4096,16384,65536,262144,1048576,4194304,16777216]

nrows=3

def get_plot(data,scaling):
    ftsize = 12
    plt.rcParams.update({'font.size': ftsize})
    # plt.rcParams.update({'font.size': 18, 'hatch.linewidth': 0.0075})
    figsz = (12, 20)



    ncols=math.ceil(len(buffer_sizes)*1.0/nrows)

    fig, axs = plt.subplots(nrows=nrows, ncols=ncols, figsize=figsz, sharex=False,sharey=False)

    # reshape axis to have 1d-array we can iterate over
    for ax,buf_size in zip(axs.reshape(-1),buffer_sizes):

        # plt.title("Upper:No buffer corruption, lower buffer corruption due to datarace")
        if buf_size < 1024:
            ax.set_title(("%dB"%buf_size), fontsize=ftsize)
        elif buf_size < 1048576:
            ax.set_title(("%dKiB" % (buf_size/1024)), fontsize=ftsize)
        else:
            ax.set_title(("%dMiB" % (buf_size/1048576)), fontsize=ftsize)

        #ax.set_xlabel("calculation time")
        #ax.set_ylabel("communication overhead in seconds")

        # get the data

        x=[]
        y=[]
        for key,value in data[ORIG].items():
            if buf_size in value:
                x.append(float(key))
                y.append(float(value[buf_size]))
        order = np.argsort(x)
        x_sort=np.array(x)[order]
        y_sort = np.array(y)[order]

        max_y=np.max(y_sort)

        ax.plot(x_sort,y_sort,label="Orig")

        x = []
        y = []
        for key, value in data[MODIFIED].items():
            if buf_size in value:
                x.append(int(float(key)))
                y.append(float(value[buf_size]))

        order = np.argsort(x)
        x_sort=np.array(x)[order]
        y_sort = np.array(y)[order]

        ax.plot(x_sort,y_sort, label="modified")

        #locator = plt.MaxNLocator(nbins=7)
        #ax.xaxis.set_major_locator(locator)
        ax.locator_params(axis='x', tight=True, nbins=4)

        ax.legend(loc='upper right')

        if scaling:
            ax.set_ylim(0,max_y*1.2)

    plt.setp(axs[-1, :], xlabel='calculation time (us)')
    plt.setp(axs[:, 0], ylabel='communication overhead (s)')
    plt.tight_layout()
    output_name = "overhead"
    if scaling:
        output_name="overhead_scaled"
    output_format="pdf"
    plt.savefig(output_name + "." + output_format, bbox_inches='tight')


def main():
    data={}
    data[ORIG]={}
    data[MODIFIED]={}

# read input data
    with os.scandir(DATA_DIR) as dir:
        for entry in dir:
            if entry.is_file():
                # original or modified run
                mode=-1
                if "orig" in entry.name:
                    mode=ORIG
                elif "modified" in entry.name:
                    mode=MODIFIED
                else:
                    print("Error readig file:")
                    print(entry.name)
                    exit(-1)

                # get calctime
                calctime=entry.name.split("_")[-1].split(".")[0]
                #print(calctime)

                # read data
                run_data={}
                with open(DATA_DIR+"/"+entry.name, "r") as stream:
                    try:
                        run_data = yaml.safe_load(stream)
                    except yaml.YAMLError as exc:
                        print(exc)
                # only care about the overhead
                run_data=run_data["async_persistentpt2pt"]["over_full"]
                data[mode][calctime]=run_data


    get_plot(data,True)
    get_plot(data, False)

if __name__ == "__main__":
    main()