## Student Information
- Name: Will C

## Project Description
this project is a kernel module, using the character device and file operations to create a tic-tac-toe game interatable with using the /dev/wtictactoe file. write to it with echo "command" > /dev/wtictactoe
read from it with cat /dev/wtictactoe to see the result 
you play against a bot, so take turns with it. play nice!

## How to Compile and Run the Proof-of-Concept Userspace Program
1. use the included makefile to compile the kernel module
2. just "make" will do it
3. then "sudo insmod kernelgame.ko" to insert the compiled kernel module into the kernel
4. to remove the module, use "sudo rmmod kernelgame"
5. MODULE IS NAMED wtictactoe !!!

## Known Project Issues
sometimes newlines arent printed correctly but it should work most of the time?
also. TONs of logging to printk so easy to backtrace
a lot was written on my IPAD ssh'd into a chromebox so thats why the formatting can be a little odd, and also the inconsistant git commits. sorry!

## LLM/AI Prompts Used
NONE!!!


## Sources Used
1. https://tldp.org/LDP/lkmpg/2.4/html/c577.htm
    function headers and such for the file_operations struct
2. https://linux-kernel-labs.github.io/refs/heads/master/labs/device_drivers.html
    character device driver and information about it
3. https://linux-kernel-labs.github.io/refs/heads/master/labs/filesystems_part1.html
    filesystem stuff, mostly in the pursuit of getting it to appear in /dev/ 
4. https://cplusplus.com/reference/cstdio/snprintf/
    for the baord formatting, 
5. https://github.com/torvalds/linux/blob/master/drivers/char/random.c 
    i didnt know the function for random kernel so i just referenced what i was actually importing 
