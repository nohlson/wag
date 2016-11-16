#include <stdint.h>
//file containing function prototypes and data structures


#define EVENT_SIZE  (sizeof(struct inotify_event))
#define EVENT_BUF_LEN   (1024 * ( EVENT_SIZE + 16 ))
#define NUM_FORMAT_OPTIONS 3
#define LOG_BUFFER_SIZE 10000
#define NUM_INIT_LINES 100

#define MOD(x, y)	(x % y < 0 ? x % y + y : x % y)

struct circbuffer {
	char		buffer[LOG_BUFFER_SIZE];	/*Actual buffer of buffer size*/
	uint32_t	readPos;					/*read index of buffer*/
	uint32_t	writePos;					/*write index of buffer*/
	uint32_t	size;

};

struct circbuffer logBuffer;

int main(int argc, char * argv[]);

int swapFilename(int fd, int wd, char * oldFilename, char * newFilename);

void onboarding(void);

void intHandler(int nil);

void winchHandler(int nil);

void refillMain(void);

void fullWinRefresh(void);

void gracefulExit(void);

void drawMainWindow(void);

void toggleSearchWindow(void);

void drawSearchWindow(void);

void search(void);

int searchFile(char * fileName, char * search);

bool updateLogBufferC(char c);

bool updateLogBuffer(char * string);

void print_last_lines(FILE * fd, int n);

void track(void);