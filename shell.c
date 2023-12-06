// Header includes
#include <errno.h>      // stderror
#include <signal.h>     // signal
#include <unistd.h>     // fork, getpid, ...
#include <stdio.h>      // input, output
#include <sys/wait.h>   // wait() call
#include <string.h>     // string manipulations
#include <stdlib.h>     // malloc(), free()
#include <stdbool.h>    // boolean
#include <ctype.h>      // isspace

// Function forward declarations
void RunShell(void);
void ExecuteCommands(void);
bool CheckExceptions(void);

void GetInputCommands(void);
void ParseInputToCommandList(const char* input_str);
int CountByDelimiter(const char* str, const char delim);

void InstallHandler(void);
void SignalHandler(int sig_num);

void SaveAndTerminateChild(pid_t pid, int stat_idx);
void SaveStatisticsFromStat(pid_t pid, int stat_idx);
void SaveStatisticsFromStatus(pid_t pid, int stat_idx);
void PrintStatistics(void);

void FreeMemories(void);

// Struct that stores the command information
typedef struct Command
{
    char    ***list;        // list of command strings
    int     *num_words_arr; // list of number of words in each command
    int     num_commands;   // number of commands
    bool    is_valid;       // the validity of the input string
} Command;

// Struct that stores the running statistics of a process
typedef struct Stats
{
    int     pid;          // stat 1
    char    cmd[50];      // stat 2
    char    state;        // stat 3
    int     ppid;         // stat 4
    float   user;         // stat 14
    float   sys;          // stat 15
    int     excode;       // stat 52   
    int     vctx;         // status
    int     nvctx;        // status
    char    exsig[50];    // wtermsig
} Stats; 

// Global variables
Command command;                // a struct with the information about all the commands
Stats stats[5];                 // an array of all process statistics up to 5
bool is_waiting_input = false;  // a flag to check if the shell is currently waiting for an input
bool sigint_received = false;   // a flag to check if an interrupt signal is received
int num_returns = 0;            // the number of finished child processes
int pfd[4][2];                  // an array of pipes 
int children_pids[5];           // an array of the process ids of all child processes

// Main function
int main()
{    
    InstallHandler();
    RunShell();
}

// --------------------------------------LOCAL FUNCTION DEFINITIONS-------------------------------------------

/**
* @brief Continuously retrieve a command from the user
*        and run the command until the user enters "exit"
*        Retrieve and display the running statistics of all child processes.
*/
void RunShell()
{
    while (1)
    {
        // Get user input and parse the string into an array of commands with word strings
        GetInputCommands();
        
        // Handle exceptions
        if (CheckExceptions()) continue;

        // Create child processes to execute the commands
        ExecuteCommands();

        // Parent Process 
        // Send SIGUSR1 signal to all child processes
        for (int i = 0; i < command.num_commands; i++)
        {
            kill(children_pids[i], SIGUSR1);
        }

        // Close all pipes
        for (int i = 0; i < command.num_commands - 1; i++)
        {
            close(pfd[i][0]);
            close(pfd[i][1]);
        }

        // Wait for all child processes to end
        // Child processes should continue although the parent is interrupted
        int ret = 0, ret_order = 0;
        siginfo_t info;

        while (((ret = waitid(P_ALL, 0, &info, WNOWAIT | WEXITED | WSTOPPED)) == 0)
            || (ret == -1 && errno == EINTR))
        {
            if (ret == 0)   
            {
                SaveAndTerminateChild(info.si_pid, ret_order++);
            }
        }

        // Print statistics of the child processes and free memories
        if (ret_order != 0) PrintStatistics();
        FreeMemories();
    }
}

/**
* @brief Create a child process to execute the commands entered by the user in an order. 
*        The commands are executed once the process receives SIGUSR1 signal from the parent.
*/
void ExecuteCommands(void)
{
    // Create pipes 
    for (int i = 0; i < command.num_commands - 1; i++)
    {
        pipe(pfd[i]);
    }

    int num_commands = command.num_commands;
    for (int cmd_idx = 0; cmd_idx < num_commands; cmd_idx++)
    {
        pid_t child_pid;
        // Start of the child process
        if ((child_pid = fork()) == 0)
        {
            // connect necessary pipes
            if (num_commands > 1)
            {
                if (cmd_idx == 0)
                {
                    // if this is the first command, 
                    // connect the write end of the first pipe to the stdout
                    dup2(pfd[0][1], 1);

                    for (int i = 0; i < command.num_commands - 1; i++)
                    {
                        close(pfd[i][0]);
                        close(pfd[i][1]);
                    }
                }
                else if (cmd_idx == command.num_commands - 1)
                {
                    // if this is the last command, 
                    // connect the read end of the last pipe to the stdin
                    dup2(pfd[cmd_idx - 1][0], 0);

                    for (int i = 0; i < command.num_commands - 1; i++)
                    {
                        close(pfd[i][0]);
                        close(pfd[i][1]);
                    }
                }
                else
                {
                    dup2(pfd[cmd_idx - 1][0], 0);
                    dup2(pfd[cmd_idx][1], 1);

                    for (int i = 0; i < command.num_commands - 1; i++)
                    {
                        close(pfd[i][0]);
                        close(pfd[i][1]);
                    }
                }
            }

            // Suspend until receive SIGUSR1 signal
            sigset_t set;
            sigfillset(&set);
            sigdelset(&set, SIGUSR1);
            sigsuspend(&set);

            // Switch to a new process to execute the command
            if (execvp(command.list[cmd_idx][0], command.list[cmd_idx]) == -1)
            {
                fprintf(stderr, "Shell: '%s': %s\n", command.list[cmd_idx][0], strerror(errno));
                exit(EXIT_FAILURE);
            }
        }
        // End of the child process
        // In the parent process, save the child process's pid
        children_pids[cmd_idx] = child_pid;
    }
}

/**
* @brief Check for invalid inputs and exit commands
* 
* @return bool true if there is an exception and the current shell should refresh,
*              false if there is no exception and the shell can continue the execution
*/
bool CheckExceptions()
{
    if (!command.is_valid) return true;
    if (command.num_commands > 5)
    {
        printf("Shell: The maximum allowed number of commands is 5\n");
        return true;
    }

    // Check if the first argument of the first command is "exit"
    if (strcmp(command.list[0][0], "exit") == 0)
    {
        // If "exit" is the only argument, terminate the program
        if (command.num_words_arr[0] == 2)
        {
            printf("Shell: Terminated\n");
            exit(0);
        }
        // If "exit" contains other arguments, the command is invalid
        else
        {
            printf("Shell: \"exit\" with other arguments!!!\n");
            return true;
        }
    }

    return false;
}

//-----------------------------------COMMAND RELATED FUNCTION DEFINITIONS-----------------------------------

/**
 * @brief Prompt the user the input a string,
 *        check validity and parse the input into a list of commands
 */
void GetInputCommands()
{
    is_waiting_input = true;

    // Display a prompt with the process id
    printf("\n## Shell [%d] ##\t", (int)getpid());

    // retrieve the user input as a string
    char input_str[1025];
    fgets(input_str, 1025, stdin);
    if (sigint_received)
    {
        command.is_valid = false;
        sigint_received = false;
        return;
    }

    // Determine the number of commands (i.e. elements of the list)
    command.num_commands = CountByDelimiter(input_str, '|');

    // Check the validity of the input
    if (!command.is_valid) return;

    // If valid, parse the input into a list of commands
    ParseInputToCommandList(input_str);

    is_waiting_input = false;
}

/**
 * @brief Parse an input string into a list of commands 
 * 
 * @param input_str the string to parse into commands
 */
void ParseInputToCommandList(const char *input_str)
{
    // Create a local copy of the input string
    char* input = strdup(input_str);

    // Allocate memory for the array of command strings
    command.list = malloc(sizeof(char**) * command.num_commands);

    // Allocate memory for the array of number of words
    command.num_words_arr = malloc(sizeof(int) * command.num_commands);

    // Tokenise the input string using '|' as delimiter
    int command_idx = 0;  
    char* command_saveptr = NULL;
    char* word_saveptr = NULL;
    char* command_str = strtok_r(input, "|", &command_saveptr);
 
    while (command.is_valid && command_str)
    {
        // Allocate memory for the words in the current command
        // Add extra one word for the NULL character at the end
        int num_words = CountByDelimiter(command_str, ' ') + 1;
        command.list[command_idx] = malloc(sizeof(char*) * num_words);

        // Store the number of words in the current command
        command.num_words_arr[command_idx] = num_words;

        // Further tokenise each element based on spaces 
        char* word = strtok_r(command_str, " ", &word_saveptr);
        int word_idx = 0;
        while (word)
        {
            // Copy each word into the array
            command.list[command_idx][word_idx] = strdup(word);
            word = strtok_r(NULL, " ", &word_saveptr);
            word_idx++;
        }

        // Add null at the end for exec command
        command.list[command_idx][word_idx] = (char*)NULL;

        // Move to next delimiter
        command_str = strtok_r(NULL, "|", &command_saveptr);

        if (command_idx == command.num_commands - 1)
        {
            // Remove the newline character at the end
            int len = strlen(command.list[command_idx][word_idx-1]);
            command.list[command_idx][word_idx-1][len-1] = '\0';
        }

        command_idx++;
    }     
    
    // Free temporary string memory
    free(input);
}

/**
 * @brief A helper function to count the number of words/commands separated by the pipe symbol.
 *        Repeated delimiters are regarded as a single delimiter.
 *        Additionally checks and updates the validity of the input string.
 *        A command string is invalid if it contains consecutive pipe symbols
 *        or have a pipe symbol at the first or last character.
 *
 * @param str the input string to separate
 * @return int the number of separated words/commands
**/
int CountByDelimiter(const char* str, const char delim)
{
    int count = 0;          // the number of words / commands separated by the delimiter
    int i = 0;              // the current index of the character in the string
    bool in_word = false;   // whether the last character was in a word / command
    bool is_valid = true;   // validity of the command

    // Check if the input starts or ends with the pipe symbol
    // End index is string length - 2 since the last character is newline character
    if (str[0] == '|' || str[strlen(str) - 2] == '|')
    {
        printf("Shell: should not have | symbol as the first or last character\n");
        is_valid = false;
    }

    // Check each character in the string
    while (str[i] && is_valid)
    {
        // If current character is a delimiter or is the end of the string,
        // add the count if applicable (not consecutive delimiters)
        if (str[i] == delim || str[i] == '\0' || str[i] == '\n')//i == strlen(str) - 1) 
        {
            // If currently counting the number of commands,
            // check if two pipe symbols appear consecutively
            if (delim == '|' && !in_word)
            {
                printf("Shell: should not have two | symbols without in-between command\n");
                is_valid = false;
            }
            else if (in_word)
            {
                in_word = false;
                count++;
            }
        }
        else in_word = true;
        i++;    // increment the character index
    }

    // If currently counting the number of words in a command,
    // a command is invalid if it contains no word
    if (delim == ' ' && count == 0)
    {
        printf("Shell: should not have two | symbols without in-between command\n");
        is_valid = false;
    }

    // Update validity of the command
    command.is_valid = is_valid;
    return count;
}


//----------------------------POST EXECUTION FUNCTION DEFINITIONS----------------------------

/**
 * @brief Read proc file of a process and save the desired information
 *        into the array of statistics 'stats' then terminate the process
 * 
 * @param pid       the process id of the child process to save data and terminate
 * @param stat_idx  the index of statistics array to save the data at 
 */
void SaveAndTerminateChild(pid_t pid, int stat_idx)
{
    // Save statistics from the proc file
    SaveStatisticsFromStat(pid, stat_idx);
    SaveStatisticsFromStatus(pid, stat_idx);

    // Terminate the process
    int status;
    waitpid(pid, &status, 0);

    // Save exit signal if terminated by a signal
    if (WIFSIGNALED(status))
    {
        strcpy(stats[stat_idx].exsig, strsignal(WTERMSIG(status)));
    }
}

/**
 * @brief Read process id, current state, exit code, parent id,
 *        time spent in user mode, and time spent in kernel mode
 *        from /proc/pid/stat and save to statistics array 'stats'
 * 
 * @param id        the process id of the child process to search for
 * @param stat_idx  the index of statistics array to save the data at 
 */
void SaveStatisticsFromStat(pid_t id, int stat_idx)
{
    // local variables for saving the stat data
    int             z;          // temp
    unsigned long   h;          // temp
    int             pid;        // stat 1
    char            cmd[50];    // stat 2
    char            state;      // stat 3
    int             ppid;       // stat 4
    unsigned long   user;       // stat 14
    unsigned long   sys;        // stat 15
    int             excode;     // stat 52   

    // Open proc/pid/stat file
    char str[50];
    sprintf(str, "/proc/%d/stat", (int)id);
    FILE *file = fopen(str, "r");

    // Return if file is not found
    if (file == NULL) 
    {
    	printf("Error opening proc/%d/stat file\n", id);
    	exit(0);
    }
   
    // Read data from the file until the 15th variable
    fscanf(file, "%d %s %c %d %d %d %d %d %u %lu %lu %lu %lu %lu %lu",
            &pid, cmd, &state, &ppid, &z, &z, &z, &z,
    	    (unsigned *)&z, &h, &h, &h, &h, &user, &sys);

    // Read the exit code from the last variable
    int val = 0;
    for (int i = 16; i <= 52; i++)
    {
        fscanf(file, "%d ", &val);
        if (i == 52) excode = val;
    }

    // Close the file when finish reading
    fclose(file);

    // Modify values to fit the syntax
    // Remove parenthesis around the command string
    char cmd_substr[50];
    strncpy(cmd_substr, cmd + 1, strlen(cmd)-2); 
    cmd_substr[strlen(cmd)-2] = '\0';  

    // Divide the exit code by 256 to get the actual exit code
    excode /= 256;
    
    // Save the retrieved data in a structure    
    Stats stat = {.pid = pid, .state = state, .excode = excode, .ppid = ppid,
                    .user = user*1.0f/sysconf(_SC_CLK_TCK), .sys = sys*1.0f/sysconf(_SC_CLK_TCK), .exsig = "" };
    strncpy(stat.cmd, cmd_substr, strlen(cmd_substr));  
    stats[stat_idx] = stat; 
}

/**
 * @brief Read the number of voluntary and nonvoluntary context switches
 *        from /proc/pid/status and save to statistics array 'stats'
 * 
 * @param id        the process id of the child process to search for
 * @param stat_idx  the index of statistics array to save the data at 
 */
void SaveStatisticsFromStatus(pid_t id, int stat_idx)
{
    // Retrieve data from proc/pid/status
    char str[50];    
    sprintf(str, "/proc/%d/status", (int)id);
    FILE *file = fopen(str, "r");

    // Return if file cannot be found
    if (file == NULL) 
    {
    	printf("Error opening proc/status file\n");
    	exit(0);
    }

    // line 37, 38
    int vctx, nvctx;
    char line[100];
    while (fgets(line, sizeof(line), file) != NULL)
    {
        char *tail, *key, *value;

        tail = strchr(line, '\n');
        if (tail != NULL) *tail = '\0'; 
        tail = strchr(line, ':');
        if (tail == NULL) continue;

        tail[0] = '\0';
        key = strdup(line);
        if (key == NULL) continue; 

        if (strcmp(key, "voluntary_ctxt_switches") != 0 && strcmp(key, "nonvoluntary_ctxt_switches") != 0) 
        {
            free(key);
            continue;
        }

        tail[0] = '\0';
        key = strdup(line);
        if (key == NULL)
        {
            continue;
        }
        tail += 1;
            while ((tail[0] != '\0') && (isspace((int) tail[0]) != 0))
                tail++;
            value = strdup(tail);

        if (strcmp(key, "voluntary_ctxt_switches") == 0)
        {
            vctx = atoi(value);
        }
        else
        {
            nvctx = atoi(value);
        }
        free(key);
    }
    
    fclose(file);

    // Save the statistics information
    stats[stat_idx].vctx = vctx;
    stats[stat_idx].nvctx = nvctx;   
}

/**
* @brief Display the running statistics of all child processes in their order of termination. 
*        Should inform if the process is terminated by a signal.
*/
void PrintStatistics()
{
    for (int i = 0; i < command.num_commands; i++)
    {
        // Skip unexecuted error
        if (stats[i].excode == 1 && strlen(stats[i].exsig) == 0) continue;

        // Not signal-terminated process
        if (strlen(stats[i].exsig) == 0)
        {
            printf("(PID)%d (CMD)%s (STATE)%c (EXCODE)%d (PPID)%d "
                "(USER)%.2f (SYS)%.2f (VCTX)%d (NVCTX)%d\n",
                stats[i].pid, stats[i].cmd, stats[i].state, stats[i].excode, stats[i].ppid,
                stats[i].user, stats[i].sys, stats[i].vctx, stats[i].nvctx);
        }
        // Signal-terminated process
        else
        {
            printf("(PID)%d (CMD)%s (STATE)%c (EXSIG)%s (PPID)%d "
                "(USER)%.2f (SYS)%.2f (VCTX)%d (NVCTX)%d\n",
                stats[i].pid, stats[i].cmd, stats[i].state, stats[i].exsig, stats[i].ppid,
                stats[i].user, stats[i].sys, stats[i].vctx, stats[i].nvctx);
        }        
    }
}

//----------------------------SIGNAL RELATED FUNCTION DEFINITIONS----------------------------

/**
* @brief Install SIGINT and SIGUSR1 handlers
*/
void InstallHandler()
{
    // Block SIGUSR1 to prevent child processes receiving the signal
    // before they start waiting for the signal
    sigset_t set;
    sigemptyset(&set);
    sigaddset(&set, SIGUSR1);
    sigprocmask(SIG_BLOCK, &set, NULL);

    // Assign SIGINT handler
    struct sigaction sa = { .sa_handler = SignalHandler };
    sigaction(SIGINT, &sa, NULL);

    // Assign SIGUSR1 handler
    sigaction(SIGUSR1, &sa, NULL);
}

/**
* @brief Handler function of SIGINT and SIGUSR1.
*        Sets the SIGINT flag if the signal is SIGINT; does nothing if SIGUSR1
* 
* @param int the signal number to handle
*/
void SignalHandler(int sig_num)
{
    if (sig_num == SIGINT)
    {
        if (is_waiting_input) sigint_received = true;
    }
    else if (sig_num == SIGUSR1)
    {
        // empty
    }
}

//-------------------------------------------------------------------------------------------

/**
* @brief Free the dynamic memories
*/
void FreeMemories()
{
    for (int i = 0; i < command.num_commands; i++)
    {
        for (int j = 0; j < command.num_words_arr[i]; j++)
        {
            free(command.list[i][j]);
        }
        free(command.list[i]);
    }
    free(command.num_words_arr);
    free(command.list);
}
