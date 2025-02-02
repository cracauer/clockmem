# clockmem
Utility to lock files into RAM or displaying how much of a file is in RAM

clockmem does three things:
- lock files into RAM using mlock(2).  You might have to raise ulimits
- display how much of a file is currently in RAM
- just lock anonymous memory, aka stealing from the system's free RAM

## Usage

Usage: -v <level> -o [l|i] -t [files]
Usage: -a <bytes> # just mmap and lock n bytes anonymously

At this time there are two major modes:
-o l (default) = lock all pages in all files
(last fractual page might be omitted)

-o i = do not lock, but print a count how many pages in the
       file are already in memory.  Can be used with -t.
       Use -v 1 to print per file, -v 0 only gives total

-o m = just mmap the file read-only and wait

-t = touch all pages in all files after mapping (bring into mem)

-v 0 = silent (unless requested)
-v 1 = not per file
-v 2 = per file noise
-v 3 = more noise
-v 4 = most noise

## Examples

$ clockmem /boot/kernel/kernel

Lock the file into memory and wait for tty input

$ clockmem -o i -v2 myfile1 myfile2

Prints how much of the files is in memory.

$ clockmem -a $((8 * 1024 * 1024 * 1024))

Just take 8 GB of RAM away from the system by locking it into this
process'es address space and wait for input.

## Notes

Be careful with the mlock option.  It can violently push other things
out of RAM.  You might have to raise ulimits to lock as much memory as
you wish.  If you use the systemd OOM killer you will probably lose
valuable process groups instead of just the benchmark.  You should
switch back to the kernel OOM killer.
