#!/usr/bin/env python3

# Compute average hash rate from cgminer log.

import argparse
import csv
import fileinput
import json
import re
import sys

description = "Parse log files with data for each nonce and report average hash rates."

full_description = """This program parses a miner log file which has entries like this:
[2014-04-17 14:34:19] Hash rate audit: HFB 0: 1366238059.669550 Search Difficulty: 35 Zeros: 41

What this means is that device "HFB 0" found a nonce with a hash
collision of 41 zero bits.  The value "1366238059.669550" is the
number of seconds since the Unix epoch.  The "search difficulty" is
the minimum number of leading zero bits in a hash that the board was
asked to report.

In cgminer, this sort of audit line is enabled with the
--hfa-hash-audit option.

The "Nominal Hash Rate" is the rate that would be seen if the board
functioned perfectly and is easily calculated from the clock speed of
the device.  The "Effective Hash Rate" is the rate of useful work that
the device actually delivers.  This rate will be lower due to hardware
errors which cause nonces to be missed.  Note that the Effective Hash
Rate is a property of the device itself.  The credit received at a
mining pool will be lower due to interaction inefficiencies.

The purpose of this program is to make a good estimate of the
Effective Hash Rate a device delivers by looking at the nonces the
device actually produces.

For example:
> $ ./average-hash-rate.py some-log-file
> Collisions > 39 bits (672 cases of 1196), Effective Hash Rate: 404.19 Gh/s

This says that the program considered cases of nonces which result in
hash collisions of 39 leading zero bits or more.  On average, it takes
2^39 hash attempts to find one such nonce.  "On average" means that as
the number of attempts approaches infinity, the probability that the
average number of hashes attempted per nonce falls within a particular
interval around 2^39 will approach 1.0.  The more samples are taken,
the closer the result will approach the ideal.

In the example above the program decided to consider the case of
collisions with 39 or more leading zero bits.  It counted the number
of nonces and multiplied that number by 2^39 to find the total number
of hashes which were then averaged over the time elapsed.

"(672 cases of 1196)" means there were 672 cases of nonces with hash
collisions with 39 or more leading zero bits.  The total number of
nonces observed was 1196.

This example uses the same set of data as the previous example, but it
reports what it finds more completely:
> $ ./average-hash-rate.py --verbose some-log-file
> Elapsed Time: 914.01 secs
> Maximum Search Difficulty: 39
> Collisions > 35 bits (1196 cases of 1196), Effective Hash Rate (*): 44.96 Gh/s
> Collisions > 36 bits (940 cases of 1196), Effective Hash Rate (*): 70.67 Gh/s
> Collisions > 37 bits (826 cases of 1196), Effective Hash Rate (*): 124.21 Gh/s
> Collisions > 38 bits (759 cases of 1196), Effective Hash Rate (*): 228.26 Gh/s
> Collisions > 39 bits (672 cases of 1196), Effective Hash Rate: 404.19 Gh/s <=
> Collisions > 40 bits (348 cases of 1196), Effective Hash Rate: 418.63 Gh/s
> Collisions > 41 bits (166 cases of 1196), Effective Hash Rate: 399.38 Gh/s
> Collisions > 42 bits (80 cases of 1196), Effective Hash Rate: 384.95 Gh/s
> Collisions > 43 bits (42 cases of 1196), Effective Hash Rate: 404.19 Gh/s
> Collisions > 44 bits (28 cases of 1196), Effective Hash Rate: 538.93 Gh/s
> Collisions > 45 bits (16 cases of 1196), Effective Hash Rate: 615.92 Gh/s
> Collisions > 46 bits (7 cases of 1196), Effective Hash Rate: 538.93 Gh/s
> Collisions > 47 bits (4 cases of 1196), Effective Hash Rate: 615.92 Gh/s
> Collisions > 48 bits (2 cases of 1196), Effective Hash Rate: 615.92 Gh/s
> (* These cases may be inaccurate due to changes in the difficulty from the pool.)

The Elapsed Time is computed from the time the first nonce is observed
to the time the last nonce is observed.

The Maximum Search Difficulty is the highest search difficulty seen in
the set of data.  Mining pools change the search difficulty.  In the
case above, the mining pool probably started with a difficulty of 35
bits and then changed it to 39 bits.  This meant that nonces with 35
leading zero bits in their hash were only reported for a time, which
means they cannot be used to reliably estimate the Effective Hash
Rate.

The rate calculated is most accurate with the most samples.  The lower
the number of zero bits the more samples are available.  In this case,
nonces with 39 leading zero bit hashes were reported continuously so
the program decided that would provide the most accurate number.  This
is the value reported by default.  In the verbose listing above, this
value is indicated with "<=".  Also, note the increasing variations
and reduced sample sizes for the higher values.  The cases marked with
"(*)" are considered suspect data as these types of nonces were not
reported during the entire sample period.

In cgminer, the --hfa-set-search-difficulty argument can be used to
reduce the search difficulty.  This may be used to force many more
nonces to be reported by the device in a particular period of time.
This increases the accuracy of the measurement of Effective Hash Rate.

The "--zerobits" option allows the user to choose which case to
report.  For example, in the case the user wishes to see the Effective
Hash Rate if only cases of 45 leading zero bits and above are
considered:
> $ ./average-hash-rate.py --zerobits 45 some-log-file
> Collisions > 45 bits (16 cases of 1196), Effective Hash Rate: 615.92 Gh/s

The use of specific nonces that were found by the hardware to estimate
the Effective Hash Rate has some desirable properties.  The result is
unambiguous and every honest observer should report the same values.
The use of data in a log file makes it possible to show other people
where the values come from.  However, this approach also involves
several sources of measurement uncertainty.

Nonces are discovered unpredictably.  This is why a substantial number
of samples are required to infer an accurate Effective Hash Rate.

When mining begins, there is necessarily a certain amount of time to
ramp up nonce production.  When mining stops, there may be a certain
amount of mining that occurs (unproductively) after the last nonce
reported. These two sources of error are discorrelated and will
hopefully cancel out.  The longer the miner runs, the less significant
will be this source of error.

If this approach is used with a mining pool, there may be reduced
performance due to network delays, discoveries of new blocks, and so
forth.

If a mining pool is not being used, some caution is appropriate.  For
example, cgminer's --benchmark mode is designed to feed the same
challenge to the board every time which means that nonces will not be
produced randomly.  Random production of nonces is a basic assumption
of hash auditing.

Exporting: This program also may be used to simply extract the data
from the log file and print it to standard output in a more widely
accessible form.  The "--export" switch will output the data in JSON.
The "--tabs", "--spaces", and "--csv" switches modify the export
format in the expected way.
"""

parser = argparse.ArgumentParser(formatter_class=argparse.RawDescriptionHelpFormatter,
                                 description=description,
                                 epilog=full_description)
parser.add_argument("logfile", help="Logfile to parse.")
parser.add_argument("--verbose", action="store_true")
parser.add_argument("--zerobits", type=int, help="Compute rate for cases of this many zerobits and above.")
parser.add_argument("--export", action="store_true", help="Parse log file and export data in JSON.")
parser.add_argument("--tabs", action="store_true", help="When exporting, use tabs for delimiters.")
parser.add_argument("--spaces", action="store_true", help="When exporting, use spaces for delimiters.")
parser.add_argument("--csv", action="store_true", help="When exporting, use commas for delimiters.")
args = parser.parse_args()

roughmatch_fast = re.compile(r'.*\[\d{4}-\d{2}-\d{2} \d{2}:\d{2}:\d{2}\] Hash rate audit: (.*)$')
finematch_fast = re.compile(r'^(HFB \d+): (\d+\.\d{6}) Search Difficulty: (\d+) Zeros: (\d+)$')

if args.export:
    if args.tabs:
        print("Device\tEpochal Time\tSearch Difficulty\tZeros")
    elif args.spaces:
        print("Device Epochal-Time Search-Difficulty Zeros")
    elif args.csv:
        print("Device,Epochal Time,Search Difficulty,Zeros")
    for line in fileinput.input(files=(args.logfile)):
        roughmatch = roughmatch_fast.match(line)
        if roughmatch:
            finematch = finematch_fast.match(roughmatch.group(1))
            if not finematch:
                raise Exception("Bad line: %s" % (line))
            if args.tabs:
                print("%s\t%s\t%s\t%s" %
                      (finematch.group(1), finematch.group(2), finematch.group(3), finematch.group(4)))
            elif args.spaces:
                device_space_escaped = re.sub(r' ', r'\ ', finematch.group(1))
                print("%s %s %s %s" %
                      (device_space_escaped, finematch.group(2), finematch.group(3), finematch.group(4)))
            elif args.csv:
                print("%s,%s,%s,%s" %
                      (finematch.group(1), finematch.group(2), finematch.group(3), finematch.group(4)))
            else:
                a = {'Device': finematch.group(1),
                     'Epochal Time': finematch.group(2),
                     'Search Difficulty': finematch.group(3),
                     'Zeros': finematch.group(4)}
                print(json.dumps(a))
    sys.exit(0)

start_time = None
stop_time = None
zeros_count = {}
max_search_difficulty = 0
for line in fileinput.input(files=(args.logfile)):
    roughmatch = roughmatch_fast.match(line)
    if roughmatch:
        finematch = finematch_fast.match(roughmatch.group(1))
        if not finematch:
            raise Exception("Bad line: %s" % (line))
        device = finematch.group(1)
        time = float(finematch.group(2))
        search_difficulty = int(finematch.group(3))
        zeros = int(finematch.group(4))
        if start_time == None:
            start_time = time
        stop_time = time
        if search_difficulty > zeros:
            raise ("Search difficulty %d must not be larger than size of collision %d."
                   % (search_difficulty, zeros))
        if search_difficulty > max_search_difficulty:
            max_search_difficulty = search_difficulty
        if zeros in zeros_count:
            zeros_count[zeros] = zeros_count[zeros] + 1
        else:
            zeros_count[zeros] = 1

if start_time is not None and stop_time is not None:
    elapsed_time = stop_time - start_time
    zero_cases = sorted(zeros_count.keys())
    least_bits = zero_cases[0]
    most_bits = zero_cases[-1]

    best_choice = None
    for case in range(max_search_difficulty,most_bits+1):
        if case in zeros_count:
            best_choice = case
            break;

    zero_count_totals = {} # Cases of zero bits and above.
    for case in range(least_bits, most_bits+1):
        total_cases = 0
        for bits in range(case, most_bits+1):
            if bits in zeros_count:
                total_cases = total_cases + zeros_count[bits]
        zero_count_totals[case] = total_cases

    if args.verbose:
        # Verbose: tell all
        print("Elapsed Time: %.2f secs" % (elapsed_time))
        print("Maximum Search Difficulty: %d" % (max_search_difficulty))
        for case in range(least_bits, most_bits+1):
            total_hashes = 2**case * zero_count_totals[case]
            average_hash_rate = float(total_hashes) / elapsed_time
            average_hash_rate_Ghs = average_hash_rate / 1000000000.0
            if case < max_search_difficulty:
                caution_flag = ' (*)'
            else:
                caution_flag = ''
            if best_choice == case:
                best_choice_flag = ' <='
            else:
                best_choice_flag = ''
            print("Collisions > %d bits (%d cases of %d), Effective Hash Rate%s: %.2f Gh/s%s" %
                  (case, zero_count_totals[case], zero_count_totals[least_bits], caution_flag,
                   average_hash_rate_Ghs, best_choice_flag))
        if max_search_difficulty > least_bits:
            print("(* These cases may be inaccurate due to changes in the difficulty from the pool.)")
    elif args.zerobits:
        # Report the particular case the user cares about.
        if args.zerobits not in zero_count_totals:
            print("There are no cases of collisions with %d zero bits or above." % (args.zerobits))
        else:
            total_hashes = 2**args.zerobits * zero_count_totals[args.zerobits]
            average_hash_rate = float(total_hashes) / elapsed_time
            average_hash_rate_Ghs = average_hash_rate / 1000000000.0
            if best_choice == case:
                best_choice_flag = ' <='
            else:
                best_choice_flag = ''
            print("Collisions > %d bits (%d cases of %d), Effective Hash Rate: %.2f Gh/s%s" %
                  (args.zerobits, zero_count_totals[args.zerobits], zero_count_totals[least_bits],
                   average_hash_rate_Ghs, best_choice_flag))
            if max_search_difficulty > args.zerobits:
                print("(This case may be inaccurate due to changes in the difficulty from the pool.)")
    else:
        # Default: Report the case that looks most representative.
        total_hashes = 2**best_choice * zero_count_totals[best_choice]
        average_hash_rate = float(total_hashes) / elapsed_time
        average_hash_rate_Ghs = average_hash_rate / 1000000000.0
        print("Collisions > %d bits (%d cases of %d), Effective Hash Rate: %.2f Gh/s" %
              (best_choice, zero_count_totals[best_choice], zero_count_totals[least_bits],
               average_hash_rate_Ghs))
