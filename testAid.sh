#!/bin/bash

# does a few calls to module, and greps dmesg to check for expected output.
# example of "echo "PLAY X > /dev/wtictactoe"
# parse output to get what the 
# [11910.066318] kern game init called - will :3
# [11910.066327] registered with major number 247
# [11910.067622] device created
# [11912.363754] kg_open called
# [11912.363793] kg_write called
# [11912.363796] kg_write received command: PLAY X


# [11912.363799] wstrtok called with str: PLAY X
#                 and delim:  

# [11912.363801] wstrtok returning token: PLAY
# [11912.363802] caught command: PLAY
# [11912.363803] wstrtok called with str: (null) and delim:  

# [11912.363805] wstrtok returning token: X
# [11912.363806] handled arg #1: X
# [11912.363808] wstrtok called with str: (null) and delim:  

# [11912.363810] wstrtok returning token: 
# [11912.363810] handled arg #2: 
# [11912.363811] wstrtok called with str: (null) and delim:  

# [11912.363812] Command: PLAY, Argument 1: X, Argument 2: 
# [11912.363813] process_command returned: OK
# [11912.363868] kg_release called

#invalid example:
# [12009.695179] wstrtok called with str: ekfnejnfj
#                 and delim:  

# [12009.695181] wstrtok returning token: ekfnejnfj
# [12009.695182] too long to be valid: ekfnejnfj
# [12009.695183] process_command returned: DEV_INVALID_COMMAND

# run a few commands, valid commands are "START (X|O)" "PLAY (1-3) (1-3)", "RESET", "BOT", "BOARD (any argument ignored)", 

# valid command test list:

VALID_COMMANDS=(
    "START X"
    "START O"
    "PLAY 1 1"
    "PLAY 2 3"
    "RESET"
    "BOT"
    "BOARD"
    "BOARD ignored arguments here"
)
INVALID_COMMANDS=(
    "START Y"
    "PLAY 0 0"
    "PLAY 4 4"
    "PLAY 1"
    "PLAY 1 2 3"
    "UNKNOWNCOMMAND"
    "RESET argument"
    "BOT argument"
    "START"
)


# adding custom log identifiers to aid in this test
LOG_PREFIX="[TESTAID]"
# will then have a [PASS] or [FAIL], + reason for fail. use these to check each test case resolves correctly.
# valid commands:
for cmd in "${VALID_COMMANDS[@]}"; do
    echo "$cmd" > /dev/wtictactoe
    # wait a moment for the kernel to process the command
    sleep 0.5
    # check dmesg for the expected output using fixed-string matching to avoid regex pitfalls
    if dmesg | tail -n 20 | grep -Fq "$LOG_PREFIX [PASS]"; then
        #echo with green color and [PASS] + (valid) + command
        echo -e "\e[32m[PASS]\e[0m (valid) $cmd"
    else
        #echo with red color and [FAIL] + (valid) + command
        echo -e "\e[31m[FAIL]\e[0m (valid) $cmd"
    fi
done

# invalid commands:
for cmd in "${INVALID_COMMANDS[@]}"; do
    echo "$cmd" > /dev/wtictactoe
    # wait a moment for the kernel to process the command
    sleep 0.5
    # check dmesg for the expected output using fixed-string matching to avoid regex pitfalls
    if dmesg | tail -n 20 | grep -Fq "$LOG_PREFIX [FAIL]"; then
        #echo with green color and [PASS] + (invalid) + command
        echo -e "\e[32m[PASS]\e[0m (invalid) $cmd"
    else
        #echo with red color and [FAIL] + (invalid) + command
        echo -e "\e[31m[FAIL]\e[0m (invalid) $cmd"
    fi
done
