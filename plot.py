#!/usr/bin/env python3
import plotext as plt
import argparse

class Data_Feed:
    def __init__(self,file_name,size=100):
        self.file = open(file_name)
        self.history = [self.get_value()] * size

    def get_value(self):
        self.file.seek(0)
        return int(self.file.readline())/1000

    def get_history(self):
        self.history.pop(0)
        self.history.append(self.get_value())
        return self.history

def main(history=100,delay=5):
    plt.theme('clear')
    f1 = Data_Feed('/sys/class/thermal/thermal_zone0/temp',history)
    f2 = Data_Feed('/sys/class/thermal/thermal_zone1/temp',history)

    try:
        while True:
            plt.cld()
            plt.plot(f1.get_history(),label="t1",marker="braille",color="red") # braille, fhd, hd 
            plt.plot(f2.get_history(),label="t2",marker="braille",color='blue')
            plt.clt()
            plt.show()
            plt.sleep(delay)
    except: pass

if __name__ == '__main__':
    parser = argparse.ArgumentParser(description='''
        Plot temperatures of 2 CPU Cores''')
    parser.add_argument('--points', '-p', dest='history', default=100, type=int,
        help="Number of history points")
    parser.add_argument('--delay', '-d', dest='delay', default=5, type=float,
        help="Delay time")
    args = parser.parse_args()
    main(args.history,args.delay)


# import os,fcntl,sys
# orig_flags = fcntl.fcntl(sys.stdin, fcntl.F_GETFL)
# fcntl.fcntl(sys.stdin, fcntl.F_SETFL, orig_flags | os.O_NONBLOCK)
# sys.stdin.read(1)

