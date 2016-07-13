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

#define EVENT_SIZE  (sizeof(struct inotify_event))
#define EVENT_BUF_LEN   (1024 * ( EVENT_SIZE + 16 ))
#define NUM_FORMAT_OPTIONS 3

//TODO clean up and describe globals
int top_size;
int bottom_size = 3;
WINDOW * top;
WINDOW * bottom;
int dualPane;
int parent_y, parent_x;
char logBuffer[10000];
char searchBuffer[1000];
char searchTerm[1000];
FILE * readfd;
char eventBuffer[EVENT_BUF_LEN];
int fd;
int logBufferReadPosIndex = 0;
int logBufferWritePosIndex = 0;
char filename[100];
int formatCode;

char formats[NUM_FORMAT_OPTIONS][50] = {"1. None", "2. {NAME}YYYY-MM-DD.{FILEEXT}", "3. MM-DD-YYY.{FILEEXT}"};
int half;

void track();
void print_last_lines(FILE * fd, int n);
bool updateLogBuffer(char * string);
bool updateLogBufferC(char c);
void search();
void drawSearchWindow();
void toggleSearchWindow();
void drawMainWindow();
void gracefulExit();
void fullWinRefresh();
void winchHandler(int nil);
void intHandler(int nil);
void onboarding();

void track() {
    char c;
    int length;
    int i;
    while (1) {
        struct inotify_event *event = (struct inotify_event *)&eventBuffer[0];
        if( event->len == 0) {
            i = 0;
            char tempTrackBuf[500];
            while ((c = fgetc(readfd)) != EOF) {
                tempTrackBuf[i] = c;
                i++;
            }
            tempTrackBuf[i] = '\0';
            updateLogBuffer(tempTrackBuf);
        }
        length = read( fd, eventBuffer, EVENT_BUF_LEN );
        if ( length < 0 ) {
            perror( "read" );
        }
    }
}


void print_last_lines(FILE * fd, int n) {
    int eolcount = 0; 
    int printcount = 0;
    char c;
    fseek(fd, -2, SEEK_END); //go to last byte of file
    char lastLineBuffer[500];
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
    logBufferWritePosIndex += sprintf(logBuffer + logBufferWritePosIndex, "%s", string);
    if (strstr(string, "\n") != NULL) { //if the new string has a newline, flush butter to screen
        fullWinRefresh();
    }
    return true;
}

bool updateLogBufferC(char c) {
    logBufferWritePosIndex += sprintf(logBuffer + logBufferWritePosIndex, "%c", c);
    if (c == '\n') {
        fullWinRefresh();
    }
    return true;
}


void search() {
    char cur = 0;
    int breakOut = 0;
    //maintain the search "state"
    //TODO figure out ESC key to escape from searching
    while (1) {
        int i = 0;
        while ((cur = wgetch(top)) != 10) {
            if (cur == 127) {
                i--;
                wmove(top, half-2, 8 + i);
                wdelch(top);
                wrefresh(top);
            } else if (cur != -10 && cur != -102) { //special case for change in screen size during typing. needs to be rethought
                searchBuffer[i] = cur;
                wmove(top, half-2, 8 + i);
                wprintw(top, "%c", cur);
                wrefresh(top);
                i++;
            }
        }

        searchBuffer[i] = '\0';

        sprintf(searchTerm, "%s", searchBuffer);
        drawSearchWindow();

        wrefresh(top);
        //after search state, wating for input
        if (!breakOut) {
            while (1) {
                cur = wgetch(top);
                if (cur == 127) {
                        memset(searchBuffer, 0, sizeof(searchBuffer));
                        memset(searchTerm, 0, sizeof(searchTerm));
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


void drawSearchWindow() {
    half = parent_y/2;
    top = newwin(half, parent_x, 0, 0);
    scrollok(top, TRUE);
    wprintw(top, searchTerm);
    mvwhline(top, half-1, 0, ACS_HLINE, parent_x);
    mvwhline(top, half-3, 0, ACS_HLINE, parent_x);
    mvwprintw(top, half-2, 0, "Search: ");
    wmove(top, half-2, 8);
    wprintw(top, "%s", searchBuffer);
    wrefresh(top);
}


void toggleSearchWindow(){
    updateLogBuffer("Search Toggle\n");
    if (dualPane) {
        dualPane = 0;
        updateLogBuffer("Dual Pane set to False\n");
        memset(searchTerm, 0, sizeof(searchTerm));
        delwin(top);
        drawMainWindow();
    } else {
        updateLogBuffer("Dual Pane set to True\n");
        dualPane = 1;
        //clear search buffer
        memset(searchBuffer,0,sizeof(searchBuffer));
        drawSearchWindow();
        search();
    }
}

void drawMainWindow() {
    if (logBufferWritePosIndex != logBufferReadPosIndex) {
        char drawBuf[500];
        memset(drawBuf, 0, sizeof(drawBuf));
        int k = 0;
        for (int i = logBufferReadPosIndex; i < logBufferWritePosIndex; i++) {
            drawBuf[k] = logBuffer[i];
            k++;
        }

        wprintw(bottom, drawBuf);
        logBufferReadPosIndex = logBufferWritePosIndex;
    }
    wrefresh(bottom);
}

void gracefulExit() {
    delwin(bottom);
    delwin(top);
    endwin();
    memset(searchBuffer,0,sizeof(searchBuffer));
    memset(logBuffer,0,sizeof(logBuffer));
    exit(1);   
}

//full app refresh
void fullWinRefresh() {
    drawMainWindow();
    if (dualPane) {
        drawSearchWindow();
    }
}

//signal handlers
void winchHandler(int nil) {
    updateLogBuffer("Screen Resize\n");
    int new_y, new_x;
    endwin();
    refresh();
    clear();
    getmaxyx(stdscr, new_y, new_x);
    parent_y = new_y;
    parent_x = new_x;
    mvwin(bottom, new_y - bottom_size, 0);
    wclear(bottom);
    fullWinRefresh();
}

void intHandler(int nil) {
    gracefulExit();
}

void onboarding() {
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
    delwin(onboard);
    endwin();
}

int main(int argc, char * argv[]) {
    dualPane = 0; // starts off with one pane
    int wd;
    int nLines = 25;
    char startupMessages[200];
    char choice;

    //initialize buffers
    memset(logBuffer, 0, sizeof(logBuffer));
    memset(searchBuffer, 0, sizeof(searchBuffer));

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
    if ((fd = inotify_init()) < 0 ) {
        perror("inotify_init error");
    }

    //add watch for the filename
    wd = inotify_add_watch( fd, filename, IN_MODIFY);

    //open file for reading and print last lines
    readfd = fopen(filename, "r");
    print_last_lines(readfd, nLines);

    // //create thread for tracking file
    pthread_t trackThread;
    pthread_create(&trackThread, NULL, (void * (*) (void *))track, NULL);

    //mainloop
    sprintf(startupMessages, "Format code %i selected\n", formatCode);
    updateLogBuffer(startupMessages);
    sprintf(startupMessages, "Begun tracking file %s\n", filename);
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