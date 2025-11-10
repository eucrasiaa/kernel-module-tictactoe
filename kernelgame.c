#include <linux/module.h> // Needed for all kernel modules
#include <linux/kernel.h> // Needed for KERN_INFO
#include <linux/init.h>   // Needed for macros like __init and __exit
#include <linux/fs.h>
#include <linux/cdev.h>
#include <linux/device.h>
#include <asm/segment.h>
#include <asm/uaccess.h>
#include <linux/buffer_head.h>
#include <linux/moduleparam.h>
#include <linux/random.h>
#include <linux/string.h> 

#define DEVICE_NAME "wtictactoe"

static int major;

//how should i store error / return codes?
/* error codes and messages */
typedef enum {
    OK = 0, 
    MISSING_PIECE,
    INVALID_PIECE,
    GAME_STARTED,
    INVALID_RESET,
    GAME_NOT_STARTED,
    NOT_PLAYER_TURN,
    OUT_OF_BOUNDS,
    CANNOT_PLACE,
    GAME_OVER,
    INVALID_BOT,
    NOT_CPU_TURN,
    DEV_INVALID_COMMAND
} RETURN_CODES;
//double const feels silly but this makes both:
//    pointer
//    array
// both consts, which is what i want!
static const char * const return_code_messages[] = {
    "OK",
    "MISSING_PIECE",
    "INVALID_PIECE",
    "GAME_STARTED",
    "INVALID_RESET",
    "GAME_NOT_STARTED",
    "NOT_PLAYER_TURN",
    "OUT_OF_BOUNDS",
    "CANNOT_PLACE",
    "GAME_OVER",
    "INVALID_BOT",
    "NOT_CPU_TURN",
    "DEV_INVALID_COMMAND"
};
//class
static struct class* kg_class;
//device
static struct device* kg_device;


static bool doBoardPrint = false;
// static char device_buffer[BUFFER_SIZE]; // static = no malloc needed
#define BUFF_SIZE 128
static char buffer[BUFF_SIZE]; // buffer for read/write operations

// board is a 3x3 array
// static char board[3][3];
static const struct {
    const char *name;
    int arg_count;
} valid_commands[] = {
    { "START", 1 },  //takes in 'X' or 'O'
    { "RESET", 0 },
    { "PLAY", 2 },
    { "BOT",   0 },
    { "BOARD", 0 },
};


static const size_t valid_commands_count = 5; // can safely hardcode!
// game state, (which turn it is (PIECE), which turn it is (PLAYER/BOT), if game has started, if game has ended, who won)
// use struct and also move board into it
static struct game_state {
    char current_piece;   // X or O, ? default !!! THIS IS THE PLAYER PIECE, bot is the opposite then
    char current_player;  // P or B, ? default
    bool game_started;  
    bool game_over;
    char winner;          // 'X', 'O', or 'D', inits to ?
    char board[3][3];
} game = {
    .current_piece = '?',
    .current_player = '?',
    .game_started = false,
    .game_over = false,
    .winner = '?',
    .board = { {'_', '_', '_'}, {'_', '_', '_'}, {'_', '_', '_'} }
};


//debug printk wrapper that adds prefix of [TESTAID]
// 2nd arg is a pass/fail with [PASS][FAIL]

#define TESTAID_PREFIX "[TESTAID] "
#define printk_test(format, ...) printk(KERN_INFO TESTAID_PREFIX format, ##__VA_ARGS__)

// board "printing", just fill it to buffer
static void print_board_to_buffer(void) {
    // format of: 4 x 4. 0,0 = '.', 0,# = #, #,0=#, so row/col nums printed on sides
    // inner 3 x 3 is board. spaces between each cell
    // each row will be 8 long, 7 + \n.

    // TODO: rewrite using snprintf to help with buffer overflow
    int offset = 0;
    int i, j;
    offset += snprintf(buffer + offset, BUFF_SIZE - offset, ". 1 2 3\n");
    for (i = 0; i < 3; i++) {
        offset += snprintf(buffer + offset, BUFF_SIZE - offset, "%d ", i + 1);
        for (j = 0; j < 3; j++) {
            offset += snprintf(buffer + offset, BUFF_SIZE - offset, "%c", game.board[i][j]);
            if (j < 2) {
                offset += snprintf(buffer + offset, BUFF_SIZE - offset, " ");
            }
        }
        offset += snprintf(buffer + offset, BUFF_SIZE - offset, "\n");
    }
}


char *wstrtok(char *str, const char *delim) {
    printk(KERN_INFO "wstrtok called with str: %s and delim: %s\n", str, delim);
    static char *saveptr;
    char *token;

    if (str)
        saveptr = str;
    else if (!saveptr)
        return NULL;

    token = saveptr;
    while (*saveptr && !strchr(delim, *saveptr))
        saveptr++;

    if (*saveptr) {
        *saveptr = '\0';
        saveptr++;
    } else {
        saveptr = NULL;
    }
    printk(KERN_INFO "wstrtok returning token: %s\n", token);
    return token;
}

// helper for processing inputs 
// already passed to kernel space so can safely just play w/ it


// check if a game has been won
static int check_win(char piece) {

    // draw check:
    int empty_cells = 0;
    int i = 0;
    int j = 0;
    for (i = 0; i < 3; i++) {
        for (j = 0; j < 3; j++) {
            if (game.board[i][j] == '_') {
                empty_cells++;
            }
        }
    }
    if (empty_cells == 0) {
        game.game_over = true;
        game.winner = 'D';
        printk(KERN_INFO "Game is a draw!\n");
        printk_test("[PASS] GAME DRAW\n");
        return 2;
    }

    // check rows
    for (i = 0; i < 3; i++) {
        if (game.board[i][0] == piece && game.board[i][1] == piece && game.board[i][2] == piece) {
            return 1;
        }
    }
    // check columns
    for (j = 0; j < 3; j++) {
        if (game.board[0][j] == piece && game.board[1][j] == piece && game.board[2][j] == piece) {
            return 1;
        }
    }
    // check diagonals
    if (game.board[0][0] == piece && game.board[1][1] == piece && game.board[2][2] == piece) {
        return 1;
    }
    if (game.board[0][2] == piece && game.board[1][1] == piece && game.board[2][0] == piece) {
        return 1;
    }
    return 0;
}
// START
// validate args first because error depends on them!
// MISSING_PIECE
// function arg is passed parsed_command, so 
static RETURN_CODES validate_start_command(const char parsed_command[3][6], const int numTokens){
    // validate arguments 
    // if game started, return GAME_STARTED
    if (game.game_started) {
        printk_test("[FAIL] GAME ALREADY STARTED\n");
        return GAME_STARTED;
    }
    // MISSING_PIECE -> if 0 args
    if (numTokens == 1) { // command = 1, only START passed
        printk_test("[FAIL] MISSING_PIECE\n");
        return MISSING_PIECE;
    }
    // INVALID PIECE -> if arg not X or O
    if (parsed_command[1][0] != 'X' && parsed_command[1][0] != 'O') {
        printk_test("[FAIL] INVALID PIECE\n");
        return INVALID_PIECE;
    }
    // otherwise, initialize game and set player piece to
    game.current_piece = parsed_command[1][0];
    game.current_player = 'P';
    game.game_started = true;
    printk(KERN_INFO "Game started with player piece: %c\n", game.current_piece);
    printk_test("[PASS] GAME STARTED\n");

    return OK;
}

// RESET
static RETURN_CODES validate_reset_command(const char parsed_command[3][6], const int numTokens){
    // if any args, invalid!
    if (numTokens > 1) { // command = 1, so if more than that, invalid
        printk_test("[FAIL] INVALID RESET ARGUMENTS\n");
        return INVALID_RESET;
    }
    if (game.game_started == false) {
        printk_test("[FAIL] INVALID RESET, GAME NOT STARTED\n");
        return INVALID_RESET;
    }
    // its a valid reset, so clear game state
    game.current_piece = '?';
    game.current_player = '?';
    game.game_started = false;
    game.game_over = false;
    game.winner = '?';
    // reset board
    unsigned int i, j;
    for (i = 0; i < 3; i++) {
        for (j = 0; j < 3; j++) {
            game.board[i][j] = '_';
        }
    }
    printk(KERN_INFO "Game reset successfully\n");
    printk_test("[PASS] GAME RESET\n");
    return OK;
}


// PLAY
static RETURN_CODES validate_play_command(const char parsed_command[3][6], const int numTokens){
    // validate arguments
    // GAME_NOT_STARTED if not started
    if (game.game_started == false) {
        printk_test("[FAIL] GAME NOT STARTED\n");
        return GAME_NOT_STARTED;
    }
    // if game over, return GAME_OVER
    if (game.game_over) {
        printk_test("[FAIL] GAME OVER\n");
        return GAME_OVER;
    }

    // must be your turn to play
    if (game.current_player != 'P') {
        printk_test("[FAIL] NOT PLAYER TURN\n");
        return NOT_PLAYER_TURN;
    }

    // if missing args, return OUT_OF_BOUNDS
    if (numTokens < 3) { // command + 2 args = 3
        printk_test("[FAIL] NOT ENOUGH ARGUMENTS\n");
        return OUT_OF_BOUNDS;
    }

    // validate row and col are 1-3
    int row = parsed_command[1][0] - '1'; // ascii and stuff so this should work
    int col = parsed_command[2][0] - '1';
    if (row < 0 || row > 2 || col < 0 || col > 2) {
        printk_test("[FAIL] OUT OF BOUNDS\n");
        return OUT_OF_BOUNDS;
    }
    


    // if cell occupied, return CANNOT_PLACE
    if (game.board[row][col] != '_') {
        printk_test("[FAIL] CANNOT PLACE\n");
        return CANNOT_PLACE;
    }


    // otherwise, place piece and update game state
    game.board[row][col] = game.current_piece;
    // switch turn to bot
    game.current_player = 'B';
    printk(KERN_INFO "Player placed %c at (%d, %d)\n", game.current_piece, row + 1, col + 1);
    printk_test("[PASS] PLAYER MOVE ACCEPTED\n");
    // check if player won
    int winStatus = -1;
    winStatus = check_win(game.current_piece);
    if (winStatus == 1) {
        game.game_over = true;
        game.winner = game.current_piece;
        printk(KERN_INFO "Player %c wins!\n", game.current_piece);
        printk_test("[PASS] PLAYER WINS\n");
        return GAME_OVER;
    }
    else if (winStatus == 2) {
        game.game_over = true;
        game.winner = 'D';
        printk(KERN_INFO "Game is a draw!\n");
        printk_test("[PASS] GAME DRAW\n");
        return GAME_OVER;
    }
    // game isnt over!
    game.current_player = 'B';
    printk(KERN_INFO "Switched turn to: %c\n", game.current_player);

    game.current_piece = (game.current_piece == 'X') ? 'O' : 'X';

    return OK;
}




// BOT
static RETURN_CODES validate_bot_command(const char parsed_command[3][6], const int numTokens){
    // no arguments
    if (numTokens > 1) { // command = 1, so if more than that, invalid
        printk_test("[FAIL] INVALID BOT ARGUMENTS\n");
        return INVALID_BOT;
    }
    // game not started
    if (game.game_started == false) {
        printk_test("[FAIL] GAME NOT STARTED\n");
        return GAME_NOT_STARTED;
    }
    // not bots turn
    if (game.current_player != 'B') {
        printk_test("[FAIL] NOT BOT TURN\n");
        return NOT_CPU_TURN;
    }
    //game is over
    if (game.game_over) {
        printk_test("[FAIL] GAME OVER\n");
        return GAME_OVER;
    }
    // make a move:
    // just keep randomly trying until empty cell
    int row, col;
    do {
        row = get_random_u64() % 3; // get random number between 0 and 2
        col = get_random_u64() % 3;
    } while (game.board[row][col] != '_');
    game.board[row][col] = game.current_piece;
    printk(KERN_INFO "Bot placed %c at (%d, %d)\n", game.current_piece, row + 1, col + 1);
    printk_test("[PASS] BOT MOVE ACCEPTED\n");
    // check if bot won
    int winStatus = -1;
    winStatus = check_win(game.current_piece);
    if (winStatus == 1) {
        game.game_over = true;
        game.winner = game.current_piece;
        printk(KERN_INFO "Bot %c wins!\n", game.current_piece);
        printk_test("[PASS] BOT WINS\n");
        return GAME_OVER;
    }
    if (winStatus == 2) {
        game.game_over = true;
        game.winner = 'D';
        printk(KERN_INFO "Game is a draw!\n");
        printk_test("[PASS] GAME DRAW\n");
        return GAME_OVER;
    }
    // game isnt over, swap back
    game.current_player = 'P';
    game.current_piece = (game.current_piece == 'X') ? 'O' : 'X';
    return OK;
    
}

// BOARD
static RETURN_CODES validate_board_command(const char parsed_command[3][6], const int numTokens){
    // no validation just let it run
    // otherwise, just print the board to buffer and return OK
    print_board_to_buffer();
    doBoardPrint = true;
    printk_test("[PASS] BOARD PRINTED\n");
    return OK;
}


static int process_command(const char *command) {
    // passes it to the correct helper based on if its
    // S        TART RESET PLAY BOT BOARD
    // START [X|O]
    // what would be the most C style way to do this chained string compare?
    // char 2d array of 2 command + arg, to store parsed arguemnt as
    
    //  max = 5 + \n, and arg   
    //  will never be more than 2 args (col, row for play, piece from start)
    // so if cant fit in here, it was an invalid command so its fine to use
 // it should be 3 elements longn, each element with a max of 6 chars each!
    char parsed_command[3][6]; // 3 commands, each with max 5 chars + null terminator
    // make a copy of command to tokenize
    char command_copy[128];
    strncpy(command_copy, command, sizeof(command_copy) - 1);
    command_copy[sizeof(command_copy) - 1] = '\0'; // double check
    
    //strtok beloved
    int i = 0, j = 0;
    char *token = NULL;
    int num_tokens = 0;

    // init to null so if we break early we stay safe
    for (i = 0; i < 3; i++){
        parsed_command[i][0] = '\0';
    }
    
    // first token: should be the command
    token = wstrtok(command_copy, " \n");
    if (!token) {
        printk(KERN_ERR "empty\n");
        printk_test("[FAIL] EMPTY COMMAND\n");
        return DEV_INVALID_COMMAND;
    }

    if (strlen(token) > 5) {
        printk(KERN_ERR "too long to be valid: %s\n", token);
        printk_test("[FAIL] TOO LONG\n");
        return DEV_INVALID_COMMAND;
    }
    strncpy(parsed_command[0], token, 5); 
    parsed_command[0][5] = '\0';
    num_tokens = 1; // we caught the initial command

    // if BOARD is teh command, we can skip to the execution
    // b/c BOARD dont gaf abt any args LMFAO 
    printk(KERN_INFO "caught command: %s\n", parsed_command[0]);


    if (strncmp(parsed_command[0], "BOARD", 5) == 0) {
        printk(KERN_INFO "BOARD! so skip other arg checks\n");
        // TODO double checck if this is nessessary since we  did it earlier 
        parsed_command[1][0] = '\0';
        parsed_command[2][0] = '\0';
        i = num_tokens;
    } else {
        // parse up to two more arguments 
        for (j = 1; j <= 2; j++) {
            token = wstrtok(NULL, " \n");
            if (!token || token[0] == '\0') {
                printk(KERN_INFO "no more tokens, stopping at arg #%d\n", j);
                parsed_command[j][0] = '\0';
            } else {
                if (strlen(token) > 1) { // only will be 'X' 'O', or '1-3' for row, col. so if its logner than 1, its invalid
                    printk(KERN_ERR "Argument %d too long: %s\n", j, token);
                    printk_test("[FAIL] TOO LONG\n");
                    return DEV_INVALID_COMMAND;
                }
                // nullcheck!
                // if (parsed_command[j][0] == '\0') {
                //     printk(KERN_ERR "Parsed command %d is empty\n", j);
                //     // isnt actually an error.
                //     // ensure its empty and break
                //     parsed_command[j][0] = '\0';
                //     break;
                // }
                strncpy(parsed_command[j], token, 1);
                parsed_command[j][1] = '\0';
                printk(KERN_INFO "handled arg #%d: %s\n", j, parsed_command[j]);
                num_tokens++;
            }
        }
        // check  for MORE than 2 args, will always be invalid if this happens
        // why is this hitting when passed "PLAY 1 2" ? 
        // if next token is just \n, its because of entering command works
        token = wstrtok(NULL, " \n");
        // why is this entering if wstrtok is just returning ' ' or '\n' ?
        // just see whats beign returned
        if(token == '\n') {
            printk(KERN_INFO "Extra token is newline, ignoring\n");
        }
        if (token == ' ') {
            printk(KERN_INFO "Extra token is space, ignoring\n");
        }
        
        if (token != NULL && token[0] != '\0') {
            printk(KERN_ERR "Too many arguments, extra token: %s\n", token);
            printk_test("[FAIL] TOO MANY ARGUMENTS\n");
            return DEV_INVALID_COMMAND;
        }

        // if (token) {
        //     printk(KERN_ERR "Extra token after expected args: %s\n", token);
        //     return DEV_INVALID_COMMAND;
        // }

        i = num_tokens; // num of tokens
        // will be used later to quick validate if too many args were passed
    }
    // its printing correctly parsed_command
    printk(KERN_INFO "Command: %s, Argument 1: %s, Argument 2: %s", parsed_command[0], parsed_command[1], parsed_command[2]);
    

    // ! CHECKPOINT: WE HAVE PARSED PASSED ARG, NOW IN parsed_command
    // ! NOW: validate [0] matches a valid command
    if (strncmp(parsed_command[0], "START", 5) == 0 || 
        strncmp(parsed_command[0], "RESET", 5) == 0 ||
        strncmp(parsed_command[0], "PLAY", 4) == 0 ||
        strncmp(parsed_command[0], "BOT", 3) == 0 ||
        strncmp(parsed_command[0], "BOARD", 5) == 0) {
        printk(KERN_INFO "Command is valid: %s\n", parsed_command[0]);
    } else {
        printk(KERN_ERR "Invalid command: %s\n", parsed_command[0]);
        printk_test("[FAIL] INVALID COMMAND\n");
        return DEV_INVALID_COMMAND;
    }

    // validate number of args based on command
    // use the valid_commands array to check if the number of args is correct for the command
    int expected_args = 0;
    // print num_tokens

    // TODO REMOVE THIS CODE LATER:
    //      because some errors are based on invalid arguments, we cant break early just off that alone!
    printk(KERN_INFO "Number of tokens: %d\n", num_tokens);
    for (i = 0; i < valid_commands_count; i++) {
        printk(KERN_INFO "Checking against valid command: %s with arg count %d\n", valid_commands[i].name, valid_commands[i].arg_count);
        if (strncmp(parsed_command[0], valid_commands[i].name, strlen(valid_commands[i].name)) == 0) {
            printk(KERN_INFO "Found matching command: %s, expected arg count: %d\n", valid_commands[i].name, valid_commands[i].arg_count);
            expected_args = valid_commands[i].arg_count;
            break;
        }
    }

    // validate number of args based on command
    // num_tokens is counting command, so -1 to get just args
    printk(KERN_INFO "Expected args: %d, Actual args: %d\n", expected_args, num_tokens - 1);
    if (i < valid_commands_count) {
        if (num_tokens - 1 != expected_args) {
            printk(KERN_ERR "Invalid number of arguments for command: %s\n", parsed_command[0]);
            printk_test("[FAIL] INVALID ARGUMENT COUNT\n");
            // ! DONT BREAK HERE LET IT KEEP RUNNING
            // return DEV_INVALID_COMMAND;
        }
    }
    printk_test("[PASS] VALID COMMAND AND ARG COUNT\n");
    // ! CAN PASS TO REPSECTIVE HANDLER FUNCTION NOW. 
    // !: MUST STILL: validate arguments, return correct error codes, update game state, etc
    int result = 0;
    // based on command, call the appropriate handler function
    // validate_start_command


    // print the gamestate for debugging
    printk(KERN_INFO "Current game state:\n");
    printk(KERN_INFO "Current piece: %c\n", game.current_piece);
    printk(KERN_INFO "Current player: %c\n", game.current_player);
    printk(KERN_INFO "Game started: %d\n", game.game_started);
    printk(KERN_INFO "Game over: %d\n", game.game_over);
    printk(KERN_INFO "Winner: %c\n", game.winner);
    printk(KERN_INFO "Current board state:\n"); 
    
    if(strncmp(parsed_command[0], "START", 5) == 0) {
        result = validate_start_command(parsed_command, num_tokens);
    } else if (strncmp(parsed_command[0], "RESET", 5) == 0) {
        result = validate_reset_command(parsed_command, num_tokens);
    } else if (strncmp(parsed_command[0], "PLAY", 4) == 0) {
        result = validate_play_command(parsed_command, num_tokens);
    } else if (strncmp(parsed_command[0], "BOT", 3) == 0) {
        result = validate_bot_command(parsed_command, num_tokens);
    } else if (strncmp(parsed_command[0], "BOARD", 5) == 0) {
        result = validate_board_command(parsed_command, num_tokens);
    }
    return result; // placeholder, should return appropriate code based on command processing
}



// called when cat-d
static ssize_t kg_read(struct file *filp, char __user *buf, size_t count, loff_t *pos)
{
    // arguments: *buf is the user-space buffer to fill, so data i copy to it is printed when cat-d
    // count = max to read 
    // so put wanted output into buf, dont forget to copy_to_user!
    // ...your read logic...
    printk(KERN_INFO "kg_read called\n"); 

    int bytes_read;

    if (*pos >= sizeof(buffer))
        return 0;
    bytes_read = sizeof(buffer) - *pos;
    if (bytes_read > count)
        bytes_read = count;

    // return the text of "Hello! this is read\n"
    // fill buffer with current board state and copy only the valid bytes to user
    // print_board_to_buffer(); // fill buffer with current board state

    // if doBoardPrint, call print_board_to_buffer() to update the buffer with the current board state before copying to user
    if (doBoardPrint) {
        memset(buffer, 0, BUFF_SIZE); // clear buffer before printing
        print_board_to_buffer();
        doBoardPrint = false; // reset flag after printing
    }
    size_t buf_len = strnlen(buffer, BUFF_SIZE);

    if (*pos >= buf_len)
        return 0;

    bytes_read = buf_len - *pos;
    if (bytes_read > count)
        bytes_read = count;

    if (copy_to_user(buf, buffer + *pos, bytes_read))
        return -EFAULT;

    *pos += bytes_read;
    printk(KERN_INFO "tictactoe: read %d bytes\n", bytes_read);
    printk(KERN_INFO "tictactoe buffer:\n%s", buffer);


    
    return bytes_read;

    return 0; // return number of bytes read or negative error
}
// called when echo-ds
static ssize_t kg_write(struct file *filp, const char __user *buf, size_t count, loff_t *pos)
{
    // ...your write logic...
    printk(KERN_INFO "kg_write called\n");
    char command[128]; // Buffer to hold the command
    unsigned long bytes_not_copied;

    if (count > sizeof(command) - 1) {
        printk(KERN_ERR "input too long\n");
        count = sizeof(command) - 1;
    }

    bytes_not_copied = copy_from_user(command, buf, count);
    if (bytes_not_copied) {
        printk(KERN_ERR "Failed to copy %lu bytes from user space\n", bytes_not_copied);
        return -EFAULT;
    }
    // null term it? check if needed
    command[count - bytes_not_copied] = '\0';

    // temporary to validate command being read
    //TODO remove later
    // ! plz. im so serious
    strncpy(buffer, command, sizeof(buffer) - 1);
    buffer[sizeof(buffer) - 1] = '\0'; // double check

    printk(KERN_INFO "kg_write received command: %s\n", command);
    printk(KERN_INFO,"calling process_command and handling\n");

    //process command here
    // if error occurs, replace buffer with error message
    // if error occured, write to buffer!
    // ! for testing, just call it
    int result = process_command(command);
    printk(KERN_INFO "process_command returned: %s\n", return_code_messages[result]);
    strncpy(buffer, return_code_messages[result], sizeof(buffer) - 1);
    buffer[sizeof(buffer) - 1] = '\0'; 
    return count;
}




static int kg_release(struct inode *inode, struct file *filp) {
    printk(KERN_INFO "kg_release called\n");
    return 0; // success
}
// open
static int kg_open(struct inode *inode, struct file *filp) {
    printk(KERN_INFO "kg_open called\n");
    // how do i get write to work now that it can open?
    return 0; // success
} 

/**
 * Structure to represent what happens when you read and write to your driver.
 *
 *  Note: you have to pass this in when registering your character driver.
 *        initially, this is not done for you.
 */
static struct file_operations char_driver_ops = {
  .owner  = THIS_MODULE,
  .read   = kg_read,
  .write  = kg_write,
  .open  = kg_open,
  .release = kg_release,
};

// You only need to modify the name here.
static struct file_system_type kernel_game_driver = {
  .name     = "tictactoegame",
  .fs_flags = O_RDWR
};


/**
 * Initializes and Registers your Module. 
 * You should be registering a character device,
 * and initializing any memory for your project.
 * Note: this is all kernel-space!
 * 
 */
static int __init kernel_game_init(void) {
  printk(KERN_INFO "kern game init called - will :3\n");
  // -- register your character device here --

  major = register_chrdev(0, DEVICE_NAME, &char_driver_ops);
  if (major < 0) {
      printk(KERN_ALERT "FAIL TO REGISTER\n");
      return major;
  }
  printk(KERN_INFO "registered with major number %d\n", major);
  
  // class thing?
  kg_class = class_create(THIS_MODULE, "wtictactoe_class");
  kg_device = device_create(kg_class, NULL, MKDEV(major, 0), NULL, DEVICE_NAME);
  printk(KERN_INFO "device created\n");

  // board should be "_" initially
  // done in declaration
    // unsigned int i, j;
    // for (i = 0; i < 3; i++) {
    //     for (j = 0; j < 3; j++) {
    //         game.board[i][j] = '_';
    //     }
    // }

  return register_filesystem(&kernel_game_driver);
}

/**
 * Cleans up memory and unregisters your module.
 *  - cleanup: freeing memory.
 *  - unregister: remove your entry from /dev.
 */
static void __exit kernel_game_exit(void) {
  printk(KERN_INFO "kern game exit called - will :3\n");
  // -- cleanup memory --
  /// class and device
  device_destroy(kg_class, MKDEV(major, 0));
  class_destroy(kg_class);

  unregister_filesystem(&kernel_game_driver);
  unregister_chrdev(major, DEVICE_NAME);
  

  // -- unregister your device driver here --
  
  return;
}

module_init(kernel_game_init);  // defines entry point function to the module, called when loaded
module_exit(kernel_game_exit);  // 

MODULE_LICENSE("GPL");
MODULE_AUTHOR("Will Capitos");
MODULE_DESCRIPTION("a tic tac toe game implemented as a linux kernel module!");
