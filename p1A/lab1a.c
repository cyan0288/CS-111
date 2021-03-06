//  NAME: Conner Yang
//  EMAIL: conneryang@g.ucla.edu
//  ID: 905417287
//  
//
//  Created by Conner Yang on 1/19/21.
//  Copyright © 2021 Conner Yang. All rights reserved.
//

#include <stdio.h>
#include <errno.h>
#include <poll.h>
#include <assert.h>
#include <fcntl.h>
#include <termios.h>
#include <sys/wait.h>
#include <sys/types.h>
#include <stdlib.h>
#include <getopt.h>
#include <signal.h>
#include <string.h>
#include <unistd.h>

struct termios original_attributes;
const int SHELL_BUFFER_SIZE = 256;
const int BUFFER_SIZE = 16;
int to_shell[2];
int from_shell[2];
pid_t child_pid;
int readyToExit = 0;    // If this value is 1, exit the while loop in processing input
int isPipeClosed = 0;

void restoreTerminal(void)
{
        // Restore terminal
    if (tcsetattr(0, TCSANOW, &original_attributes) < 0)
    {
        fprintf(stderr, "Error restoring terminal: %s\n", strerror(errno));
    }
    exit(0);
}

void terminal_setup(void)
{
    struct termios tmp;
    if (tcgetattr(0, &tmp) < 0)
    {
        fprintf(stderr, "Error getting original attributes: %s\n", strerror(errno));
        exit(1);
    }
    
    original_attributes = tmp;  // Save original attributes in termios struct var
    
    // Set non-canonical attributes
    tmp.c_iflag = ISTRIP;
    tmp.c_oflag = 0;
    tmp.c_lflag = 0;
    
    // Change occurs immediately on startup
    if (tcsetattr(0, TCSANOW, &tmp) < 0)
    {
        fprintf(stderr, "Error setting non-canonical attribute: %s\n", strerror(errno));
    }
    
    atexit(restoreTerminal);
}

void outputToTerminal(void)
{
    while (1)
    {
        char buffer[BUFFER_SIZE];
        
        ssize_t numBytesRead = read(0, buffer, BUFFER_SIZE);
            // Error checking
        if (numBytesRead < 0)
        {
            fprintf(stderr, "Error reading bytes from terminal: %s\n", strerror(errno));
        }
        
        
        for (ssize_t i = 0; i < numBytesRead; i++)
        {
            char ch = buffer[i];
            
            if (ch == 0x4)
            {
                if (write(1, "^D", 2) < 0)
                {
                    fprintf(stderr, "Error writing to standard output: %s\n", strerror(errno));
                }
                
                    // Restore terminal
                restoreTerminal();
            }
            else if (ch == '\r' || ch == '\n')
            {
                if (write(1, "\r\n", 2) < 0)
                {
                    fprintf(stderr, "Error writing to standard output: %s\n", strerror(errno));
                }
            }
            else
            {
                if (write(1, &ch, 1) < 0)
                {
                    fprintf(stderr, "Error writing to standard output: %s\n", strerror(errno));
                }
            }
        }
    }
}

void signalHandler(int signum)
{
    if (signum == SIGPIPE)
    {
        close(to_shell[1]);
        close(from_shell[0]);
        kill(child_pid, SIGINT);
        //fprintf(stderr, "SHELL EXIT SIGNAL=%d STATUS=%d\n", WTERMSIG(status), WEXITSTATUS(status));
        exit(0);
    }
}

void process_keyboard(void)
{
    char buffer_0[SHELL_BUFFER_SIZE];
    ssize_t numBytesRead = read(0, buffer_0, SHELL_BUFFER_SIZE);
    
    if (numBytesRead < 0)
    {
        fprintf(stderr, "Error reading keyboard input to terminal: %s\n", strerror(errno));
        exit(1);
    }
    
        // For each char read into the buffer
    for (int i = 0; i < numBytesRead; i++)
    {
        char ch = buffer_0[i];
        if (ch == '\r' || ch == '\n')
        {
                // Write to standard output
            if (write(1, "\r\n", 2*sizeof(char)) < 0)
            {
                fprintf(stderr, "Error writing to standard output: %s\n", strerror(errno));
            }
            
                // Write to shell
            if (write(to_shell[1], "\n", 1) < 0)
            {
                fprintf(stderr, "Error writing to shell: %s\n", strerror(errno));
            }
        }
        else if (ch == 0x03)
        {
            if (write(1, "^C", 2) < 0)
            {
                fprintf(stderr, "Error writing to standard output: %s\n", strerror(errno));
            }
            if (kill(child_pid, SIGINT) < 0)
            {
                fprintf(stderr, "Error sending SIGINT signal to shell: %s\n", strerror(errno));
                exit(1);
            }
        }
        else if (ch == 0x04)
        {
            if (write(1, "^D", 2) < 0)
            {
                fprintf(stderr, "Error writing to standard output: %s\n", strerror(errno));
            }
            if (isPipeClosed == 0)
            {
                close(to_shell[1]); // Close pipe to shell
                isPipeClosed = 1;
            }
        }
        else
        {
                // Write to stdout
            if (write(1, &ch, sizeof(char)) < 0)
            {
                fprintf(stderr, "Error writing to standard output: %s\n", strerror(errno));
            }
                // Write to shell
            if (write(to_shell[1], &ch, sizeof(char)) < 0)
            {
                fprintf(stderr, "Error writing to shell: %s\n", strerror(errno));
            }
                
        }
    }
}

void process_shell(void)
{
    char buffer_shell[SHELL_BUFFER_SIZE];
    ssize_t numBytesRead = read(from_shell[0], buffer_shell, SHELL_BUFFER_SIZE);
    for (int i = 0; i < numBytesRead; i++)
    {
        char ch = buffer_shell[i];
        if (ch == '\n')
        {
            if (write(1, "\r\n", 2) < 0)
            {
                fprintf(stderr, "Error writing to standard output: %s\n", strerror(errno));
            }
        }
        else
        {
            if (write(1, &ch, 1) < 0)
            {
                fprintf(stderr, "Error writing to standard output: %s\n", strerror(errno));
            }
        }
    }
}

int main(int argc, char * argv[]) {
        // Collect options
    int option;
    int shell_opt_flag = 0;
    int debug_opt_flag = 0;
    
    static struct option options[] = {
      {"shell", no_argument, NULL, 's'},
      {"debug", no_argument, NULL, 'd'},
      {0, 0, 0, 0}
    };
    
    while (1)
    {
        option = getopt_long(argc, argv, "i:o:sc", options, NULL);
        if (option == -1) break;
        switch (option)
        {
          case 's':
                shell_opt_flag = 1;
                break;
          case 'd':
                debug_opt_flag = 1;
                break;
          default:
                fprintf(stderr, "Correct options: --shell and --debug: %s\n", strerror(errno));
                exit(1);
        }
    }
    
    terminal_setup();
        // Process options
    if (shell_opt_flag == 0)
    {
        if (debug_opt_flag == 1)
            fprintf(stderr, "There is an error if this line is printed in shell mode");
        outputToTerminal();
    }
    else
    {
            // This code is executed if the shell option is provided by the user
        
            // First, create pipes and file descriptors
        if (pipe(to_shell) < 0)
        {
            fprintf(stderr, "Error creating pipe from terminal to shell: %s\n", strerror(errno));
        }
        
        if (pipe(from_shell) < 0)
        {
            fprintf(stderr, "Error creating pipe from shell to terminal: %s\n", strerror(errno));
        }
        
        signal(SIGPIPE, signalHandler);
        
            // Then, fork a new process to execute program
        child_pid = fork();
            // Check for error
        if (child_pid < 0)
        {
            fprintf(stderr, "Error forking new process: %s\n", strerror(errno));
            exit(1);
        }
        
            // Child process
        else if (child_pid == 0)
        {
                // Causes 0 to point to read end of the to_shell pipe
            close(to_shell[1]);
            close(0);
            dup(to_shell[0]);
            close(to_shell[0]);
            
                // Causes 1 to point to write end of the from_shell pipe
            close(from_shell[0]);
            close(1);
            dup(from_shell[1]);
            
                // Causes 2 to point to same thing as well
            close(2);
            dup(from_shell[1]);
            close(from_shell[1]);
            
            char* arguments_exec[2] = {"/bin/bash", NULL};
            if (execvp(arguments_exec[0], arguments_exec) == -1)
            {
                fprintf(stderr, "Error executing new process:%s\n", strerror(errno));
                exit(1);
            }
            exit(5);
            
        }
        
        
            // Parent process
        else if (child_pid > 0)
        {
            close(from_shell[1]);
            close(to_shell[0]);
            
                // Create poll API's to avoid read syscalls blocking each other
            struct pollfd pollfds[2];
            pollfds[0].fd = 0;
            pollfds[0].events = POLLIN + POLLHUP + POLLERR;
            pollfds[1].fd = from_shell[0];
            pollfds[1].events = POLLIN + POLLHUP + POLLERR;
            
            
            while (1)
            {
                int pollRet = poll(pollfds, 2, -1);
                if (pollRet < 0)
                {
                    fprintf(stderr, "Error with polling: %s\n", strerror(errno));
                    exit(1);
                }
                
                    // If input events occured
                if (pollRet > 0)
                {
                        // Standard output
                    if (pollfds[0].revents & POLLIN)
                    {
                        process_keyboard();
                    }
                    
                        // Output to pipe -> shell
                    if (pollfds[1].revents & POLLIN)
                    {
                        process_shell();
                    }
                    
                        // Error checking
                    if (pollfds[0].revents & (POLLHUP | POLLERR))
                    {
                        readyToExit = 1;
                        //fprintf(stderr, "Error with poll for stdin fd:%s\n", strerror(errno));
                        if (debug_opt_flag)
                        {
                            fprintf(stderr, "STDIN poll error");
                        }
                        //exit(1);
                    }
                    if (pollfds[1].revents & (POLLHUP | POLLERR))
                    {
                        readyToExit = 1;
                        if (debug_opt_flag)
                        {
                            fprintf(stderr, "Shell poll error");
                        }
                        //exit(1);
                    }
                }
                if (readyToExit == 1)
                    break;
            }
            
            if (isPipeClosed == 0)
            {
                close(to_shell[1]);
                isPipeClosed = 1;
            }
            
            int status = 0;
            if (waitpid(child_pid, &status, 0) < 0)
            {
                fprintf(stderr, "Error waiting for child process:%s\n", strerror(errno));
                exit(1);
            }
            
            fprintf(stderr, "SHELL EXIT SIGNAL=%d STATUS=%d\n", WTERMSIG(status), WEXITSTATUS(status));
            exit(0);
        }
        
        
    }
}
