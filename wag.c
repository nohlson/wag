/*
wag file tracker

Copyright 2016 nateohlson
This program is free software: you can redistribute it and/or modify
it under the terms of the GNU General Public License as published by
the Free Software Foundation, either version 3 of the License, or
(at your option) any later version.

This program is distributed in the hope that it will be useful,
but WITHOUT ANY WARRANTY; without even the implied warranty of
MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
GNU General Public License for more details.

You should have received a copy of the GNU General Public License
along with this program.  If not, see <http://www.gnu.org/licenses/>.
*/
#include <stdlib.h>
#include <stdio.h>
#include <stdbool.h>
#include "wag.h"
#include <ncurses.h>
#include <unistd.h>
#include <signal.h>
#include <sys/ioctl.h>
#include <string.h>
#include <pthread.h>
#include <errno.h>
#include <sys/types.h>
#include <sys/inotify.h>
#include <fcntl.h>
#include <time.h>
#include <sys/stat.h>
#include <fcntl.h>

//TODO clean up and describe globals
WINDOW *top;    /* top 'search' window */
WINDOW *bottom; /* bottom 'main' window */
bool dualPane;  /* is the second window visible */
int parent_y, parent_x; /*size of the terminal window itself */
char searchBuffer[1000];    /* buffer for the users search query */
char searchQuery[1000];     /* user's search query */
char searchResult[1000];
FILE * readfd;  /*file descriptor for log file */
char eventBuffer[EVENT_BUF_LEN];    /* event buffer for inotify */
int inFd; /* file descriptor for inotify */
char filename[100]; /* log filename */
int formatCode;
int wd; /* watch descriptor */

char formats[NUM_FORMAT_OPTIONS][50] = {"1. None", "2. {NAME}YYYY-MM-DD.{FILEEXT}", "3. MM-DD-YYY.{FILEEXT}"};

void track(void) {
    char c;
    int length;
    int i;
    int j;
    char info[40];
    struct stat sb;
    while (1) {
        memset(eventBuffer, 0, EVENT_BUF_LEN);
        length = read( inFd, eventBuffer, EVENT_BUF_LEN );
        j = 0;
        while (j < length) {
            struct inotify_event *event = (struct inotify_event *)&eventBuffer[j];
            if( event->mask & IN_MODIFY ) {
                if (stat(filename, &sb) == -1) {
                    perror("stat");
                    exit(EXIT_FAILURE);
                }
                i = 0;
                char tempTrackBuf[500];
                while ((c = fgetc(readfd)) != EOF) {
                    tempTrackBuf[i] = c;
                    i++;
                }
                tempTrackBuf[i] = '\0';
                updateLogBuffer(tempTrackBuf);
            } else if ( event->mask & IN_MOVE_SELF) {
                sprintf(info, "File has moved. Attempting to rotate log...\n");
                updateLogBuffer(info);
                sleep(1); //wait for rotation to occur
                if ((wd = swapFilename(inFd, wd, filename, filename)) < 0) {
                    perror("filename not found");
                    exit(1);
                } else {
                    sprintf(info, "Success.\n");
                    updateLogBuffer(info);
                }

            }
            
            if ( length < 0 ) {
                perror( "read" );
            }
            j += EVENT_SIZE + event->len;
        }
    }
}


void print_last_lines(FILE * fd, int n) {
    int eolcount = 0; 
    int printcount = 0;
    char c;
    fseek(fd, -2, SEEK_END); //go to last byte of file
    char lastLineBuffer[LOG_BUFFER_SIZE];
    memset(lastLineBuffer, 0, sizeof(lastLineBuffer));

    int atStart = 0;
    while (eolcount < n && !atStart) {
        while ((c = fgetc(fd)) != '\n' && !atStart) {
            if (fseek(fd, -2, SEEK_CUR) != 0) {
                atStart = 1;
            }
        }
        if (c == '\n' && !atStart) {
            eolcount++;
            fseek(fd, -2, SEEK_CUR);
        }
    }
    if (fseek(fd, 1, SEEK_CUR) != 0 && !atStart) {
        perror("seek error");
    }
    if (atStart) {
        fseek(fd, 0, SEEK_SET);
    } else {
        fseek(fd, 1, SEEK_CUR); //skip the last newline character and start from next newline
    }
    int j = 0;
    while (printcount < eolcount) {
        while ((c = fgetc(fd)) != EOF) {
            lastLineBuffer[j] = c;
            j++;
            if (c == '\n') {
                updateLogBuffer(lastLineBuffer);
                memset(lastLineBuffer,0,sizeof(lastLineBuffer));
                j = 0;
                printcount++;
                break;
            }
        }
    }
}


bool updateLogBuffer(char * string) {
    int stringLen = strlen(string);
    if (logBuffer.writePos + stringLen < logBuffer.size) {
        logBuffer.writePos += sprintf(logBuffer.buffer + logBuffer.writePos, "%s", string);
        logBuffer.writePos = MOD(logBuffer.writePos, logBuffer.size);
    } else {
        int i = 0;
        int k = logBuffer.writePos;
        while (i < stringLen) {
            logBuffer.buffer[MOD(k, logBuffer.size)] = string[i];
            i++;
            k++;
        }
        logBuffer.writePos = MOD(k, logBuffer.size);
    }
    if (strstr(string, "\n") != NULL) { //if the new string has a newline, flush butter to screen
        fullWinRefresh();
    }
    return true;
}

bool updateLogBufferC(char c) {
    logBuffer.writePos += sprintf(logBuffer.buffer + logBuffer.writePos, "%c", c);
    logBuffer.writePos = MOD(logBuffer.writePos, logBuffer.size);
    if (c == '\n') {
        fullWinRefresh();
    }
    return true;
}

int searchFile(char * fileName, char * search) {
    char line[512];
}

void search(void) {
    char cur = 0;
    int breakOut = 0;
    //maintain the search "state"
    //TODO figure out ESC key to escape from searching
    while (1) {
        int i = 0;
        while ((cur = wgetch(top)) != 10) {
            if (cur == 127) { //delete char
                i--;
                searchBuffer[i] = '\0'; //delete the char from the buffer
                wmove(top, (parent_y / 2) - 2, 8 + i);
                wdelch(top);
                wrefresh(top);
            } else if (cur != -10 && cur != -102) { //special case for change in screen size during typing. needs to be rethought
                searchBuffer[i] = cur;
                wmove(top, (parent_y / 2)-2, 8 + i);
                wprintw(top, "%c", cur);
                wrefresh(top);
                i++;
            }
        }

        searchBuffer[i] = '\0';

        sprintf(searchQuery, "%s", searchBuffer);

        //search file
        FILE *fpsearch =fopen(filename,"r");
        char tmp[500]={0x0};

        int lineNumber = 0;
        int found = 0;
        int stopSearch = 0;

        while (fpsearch != NULL && fgets(tmp, sizeof(tmp), fpsearch) != NULL && !stopSearch) {
            lineNumber++;
            if (strstr(tmp, searchQuery)) {
                found = 1;
                sprintf(searchResult, "%s", tmp);
                drawSearchWindow();
                //printf("line number: %d %s", counter, tmp);
                while ((cur = wgetch(top)) != 'n') {
                    if (cur == 's') {
                        breakOut = 1;
                        stopSearch = 1;
                        break;
                    }
                }
            }
        }
        if (fpsearch != NULL) {
            fclose(fpsearch);
        }

        //print out if search came up empty
        if (!found) {
            sprintf(searchResult, "Query not found\n");
            drawSearchWindow();
        }

        //after search state, wating for input
        if (!breakOut) {
            while (1) {
                cur = wgetch(top);
                if (cur == 127) {
                        memset(searchBuffer, 0, sizeof(searchBuffer));
                        memset(searchQuery, 0, sizeof(searchQuery));
                        drawSearchWindow();
                        break;
                } else if (cur == 's') {
                        toggleSearchWindow();
                        breakOut = 1;
                        break;
                }
            }
        }
        if (breakOut) {
            break;
        }
    }
}

void drawSearchWindow(void) {
    int half = parent_y/2;
    top = newwin(half, parent_x, 0, 0);
    scrollok(top, TRUE);
    wprintw(top, searchResult);
    mvwhline(top, half-1, 0, ACS_HLINE, parent_x);
    mvwhline(top, half-3, 0, ACS_HLINE, parent_x);
    mvwprintw(top, half-2, 0, "Search: ");
    wmove(top, half-2, 8);
    wprintw(top, "%s", searchBuffer);
    wrefresh(top);
}

void toggleSearchWindow(void){
    if (dualPane) {
        dualPane = false;
        memset(searchResult, 0, sizeof(searchResult));
        delwin(top);
        refresh();
        fullWinRefresh();
        refillMain();
    } else {
        dualPane = true;
        //clear search buffer
        memset(searchBuffer,0,sizeof(searchBuffer));
        drawSearchWindow();
        search();
    }
}

void drawMainWindow(void) {
    wrefresh(bottom);
    if (logBuffer.writePos != logBuffer.readPos) {
        char drawBuf[logBuffer.size];
        memset(drawBuf, 0, sizeof(drawBuf));
        int k = 0; int i = logBuffer.readPos;
        while ((i % logBuffer.size)  != (MOD(logBuffer.writePos, logBuffer.size))) {
            drawBuf[k] = logBuffer.buffer[MOD(i, logBuffer.size)];
            k++;
            i++;
        }
        wprintw(bottom, drawBuf);
        logBuffer.readPos = logBuffer.writePos;
    }
    wrefresh(bottom);
}

void gracefulExit(void) {
    delwin(bottom);
    delwin(top);
    endwin();
    memset(searchBuffer,0,sizeof(searchBuffer));
    memset(logBuffer.buffer,0,logBuffer.size);
    exit(1); 
}

void fullWinRefresh(void) {
    drawMainWindow();
    if (dualPane) {
        drawSearchWindow();
    }
}

void refillMain(void) {
    int new_y, new_x;
    getmaxyx(stdscr, new_y, new_x);
    int j = logBuffer.readPos -1;
    int newlineCount = 0;
    while (newlineCount < new_y && j != logBuffer.readPos && logBuffer.buffer[j] != '\0') {
        if (logBuffer.buffer[j] == '\n') {
            newlineCount++;
            // wprintw(bottom, "%i, %i\n", newlineCount, j);
            // wrefresh(bottom);
        }
        j = MOD((j-1), LOG_BUFFER_SIZE);
    }

    logBuffer.readPos = MOD(j + 1, logBuffer.size);
    werase(bottom);
    fullWinRefresh();
}

void winchHandler(int nil) {
    //updateLogBuffer("Screen Resize\n");
    int new_y, new_x;
    endwin();
    refresh();
    clear();
    getmaxyx(stdscr, new_y, new_x);
    parent_y = new_y;
    parent_x = new_x;
    mvwin(bottom, new_y, 0);
    wclear(bottom);
    refillMain();
}

void intHandler(int nil) {
    gracefulExit();
}

void onboarding(void) {
    // onboarding window
    char onboardChar;
    char prompt[] = "File: ";
    char invalidFilenameMessage[] = "Invalid filename. Please re-enter...";
    char item[50];
    int k = 0; 
    //create onboarding window
    WINDOW * onboard = newwin(parent_y, parent_x, 0, 0);
    //move cursor to middle of screen to write prompt
    //TODO: coordinate positions in the following section need to be captured in variables, too hard to understand
    wmove(onboard, parent_y/2, parent_x/2 -6);
    wprintw(onboard, prompt);
    wrefresh(onboard);
    //take input for filename
    while (1) {
        while ((onboardChar = wgetch(onboard)) != 10) {
            if (onboardChar == 127) {
                k--;
                wmove(onboard, parent_y/2, parent_x/2 + strlen(prompt) + k - 6);
                wdelch(onboard);
                wrefresh(onboard);
            } else if (onboardChar != -10 && onboardChar != -102) { //special case for change in screen size during typing. needs to be rethought
                filename[k] = onboardChar;
                wmove(onboard, parent_y/2, parent_x/2 + strlen(prompt) + k - 6);
                wprintw(onboard, "%c", onboardChar);
                wrefresh(onboard);
                k++;
            }
        }
        //validate input and filename
        if (access(filename, F_OK) == -1) {
            //file doesn't exist
            memset(filename, 0, sizeof(filename));
            k = 0;
            wclear(onboard);
            wmove(onboard, parent_y/2, parent_x/2 -17);
            wprintw(onboard, invalidFilenameMessage);
            wrefresh(onboard);
            sleep(3);
            wclear(onboard);
            wmove(onboard, parent_y/2, parent_x/2 -6);
            wprintw(onboard, prompt);
            wrefresh(onboard);
        } else {
            filename[k] = '\0';
            break;
        }
    }
    wclear(onboard);
    /*
    keypad(onboard, TRUE);
    k = 0;
    mvwprintw(onboard, 0, 0, "Please select file name format:");
    //find format type
    for (int i = 0; i < NUM_FORMAT_OPTIONS; i++) {
        if (i==0) {
            wattron(onboard, A_STANDOUT);
        } else {
            wattroff(onboard, A_STANDOUT);
        }
        sprintf(item, "%-7s", formats[i]);
        mvwprintw(onboard, i+1, 2, "%s", item);
    }
    wrefresh(onboard);
    while ((onboardChar = wgetch(onboard)) != 10) {
        sprintf(item, "%-7s", formats[k]);
        mvwprintw(onboard, k+1, 2, "%s", item);
        switch (onboardChar) {
            case 3:
                k--;
                k = (k<0) ? NUM_FORMAT_OPTIONS-1 : k;
                break;
            case 2:
                k++;
                k = (k>=NUM_FORMAT_OPTIONS) ? 0: k;
                break;
        }
        wattron(onboard, A_STANDOUT);
        sprintf(item, "%-7s", formats[k]);
        mvwprintw(onboard, k+1, 2, "%s", item);
        wattroff(onboard, A_STANDOUT);
    }
    mvwprintw(onboard, 20, 20, "%i", k);
    formatCode = k;
    */
    delwin(onboard);
    endwin();
}

int swapFilename(int fd, int wd, char * oldFilename, char * newFilename) {
    if (*oldFilename != 0 && fcntl(wd, F_GETFD) != -1) {
        if (inotify_rm_watch(fd, wd) == -1) {
            perror("inotify rm watch error");
        }
    }
    readfd = fopen(filename, "r");
    print_last_lines(readfd, NUM_INIT_LINES);
    return inotify_add_watch(fd, newFilename, IN_MODIFY|IN_MOVE_SELF);
}

int main(int argc, char * argv[]) {
    dualPane = false; // starts off with one pane
    char startupMessages[200];
    char choice;

    //initialize buffers
    memset(searchBuffer, 0, sizeof(searchBuffer));
    memset(logBuffer.buffer, 0, sizeof(logBuffer.buffer));
    logBuffer.size = LOG_BUFFER_SIZE;
    logBuffer.readPos = 0;
    logBuffer.writePos = 0;

    //ncurses init
    initscr();
    noecho();
    timeout(0);
    curs_set(FALSE);
    getmaxyx(stdscr, parent_y, parent_x); 

    //onboard to set filename and get formatting
    onboarding();

    // new bottom 'main' window
    bottom = newwin(parent_y, parent_x, 0, 0);
    scrollok(bottom, TRUE);

    //signal handlers
    signal(SIGWINCH, winchHandler);
    signal(SIGINT, intHandler);

    /*creating the INOTIFY instance*/
    if ((inFd = inotify_init()) < 0 ) {
        perror("inotify_init error");
    }

    //add watch for the filename
    wd = inotify_add_watch( inFd, filename, IN_MODIFY|IN_MOVE_SELF);

    //open file for reading and print last lines
    readfd = fopen(filename, "r");
    print_last_lines(readfd, NUM_INIT_LINES);

    // //create thread for tracking file
    pthread_t trackThread;
    pthread_create(&trackThread, NULL, (void * (*) (void *))track, NULL);

    //mainloop
    // sprintf(startupMessages, "Format code %i selected\n", formatCode);
    updateLogBuffer(startupMessages);
    // sprintf(startupMessages, "Began tracking file %s\n", filename);
    updateLogBuffer(startupMessages);
    while (1) {
        choice = wgetch(bottom);
        if (choice == 's') {
            toggleSearchWindow();
        } else if (choice == 'q') {
            gracefulExit();
        }
    }

    return 0;
}