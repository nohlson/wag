#include <stdio.h>
#include <unistd.h>


int main() {
	int i = 0;
	while (1) {
		if (i % 10 == 0) {
			printf("FillerLog Info\n");
		} else if (i % 2 == 0) {
			printf("Even log info %i\n", i);
		} else {
			printf("Logger event num: %i\n", i);
		}
		fflush(stdout);
		sleep(1);
		i++;

	}



}