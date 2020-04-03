#!/usr/bin/env python3
import re
import os
import time
import pathlib
import argparse
import subprocess
import urllib.request
from termcolor import colored

def fetch_fib_number(index):
    resp = urllib.request.urlopen(f"http://www.protocol5.com/Fibonacci/{index}.htm")
    html = [h.strip().decode('utf-8') for h in resp.readlines()]
    represents = []
    for line in html:
        match = re.search(r"<li><h4>.+?</h4><div>(.+?)</div></li>", line)
        if match:
            represents.append(match.group(1))
    return {
        "base2": represents[0],
        "base3": represents[1],
        "base5": represents[2],
        "base6": represents[3],
        "base8": represents[4],
        "base10": represents[5],
        "base16": represents[6],
        "base36": represents[7],
        "base63404": represents[8]
    }

def fetch_myfib_number(port, index):
    resp = urllib.request.urlopen(f"http://localhost:{port}/fib/{index}")
    html = [h.strip().decode('utf-8') for h in resp.readlines()]
    represents = []
    represents.append(html[0].strip('\']'))
    return {
        "base16": represents[0]
    }
    
def main(port, index, base):
    start_time = time.time()
    result = fetch_myfib_number(port, index)[base]
    duration = time.time() - start_time
    print(f"Calculate {index}th fib number takes {duration} seconds")
    expect = fetch_fib_number(args.index)[base].upper()
    if result == expect:
        print(colored("Pass", "green"))
        exit(0)
    else:
        print(colored("Failed", "red"))
        exit(1)

if __name__ == '__main__':
    parser = argparse.ArgumentParser()
    parser.add_argument("port", type=str, help="Server port number")
    parser.add_argument("index", type=str, help="Fibonacci number index")
    parser.add_argument("-b", action='store', dest="base", help="Fibonacci number base (default: base10)")
    args = parser.parse_args()

    PORT = int(args.port)
    FIB_INDEX = int(args.index)
    FIB_NUMBER_BASE = args.base

    if FIB_INDEX < 0:
        print("Fibonacci index is less than 0")
        exit(1)

    if not FIB_NUMBER_BASE:
        FIB_NUMBER_BASE = "base10"

    main(PORT, FIB_INDEX, FIB_NUMBER_BASE)