# About

The program `wfc` counts the frequencies of words in
a text document and writes the words along with their
frequencies in a file.
The lines of the output file are ordered according
to the word frequencies in descending order, i.e.,
the most frequent word is on the top.

The program `wfc` makes use of parallelism by
letting child processes parse different parts
of the input file.

This is my solution of an assignment in the course
[CS511](https://web.stevens.edu/compsci/graduate/masters/courses/viewer.php?course=CS511&type=syl) (Concurrent Programming) at Stevens Institute of
Technology in the Fall of 2012.

# Installation

The program can be compiled by running `make` inside the folder
where `wfc.cpp` and `Makefile` are located.

# Usage

The general usage syntax is:
    wfc [-p <parallelism>] [-i <input file>] [-o <output file>]
where
* `parallelism` is the number of child processes to fork.
  If this parameter is unspecified, `4` will be used as the
  parallelism.
* `input file` is the path to the input file. If this parameter
  is unspecified, `test_in.txt` will be used as the input file.
* `output file` is the path to the output file. If this parameter
  is unspecified, `test_out.txt` will be used as the output file.

Make sure that you are allowed to allocate enough shared memory
for the input file.
The maximum shared memory size must set to a value that is slightly
higher than the input file size.
You can set the maximum shared memory size with the command
`sysctl -w kernel.shmmax=size` where size is the desired size in
bytes.
For example, `sysctl -w kernel.shmmax=2147483648` sets the
maximum shared memory segment size to 2 GiB.

# Copyright

(Copyright) 2012 Fabian Foerg

