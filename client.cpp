#include <sys/socket.h>
#include <sys/un.h>
#include <sys/types.h>
#include <sys/time.h>
#include <stdio.h>
#include <stdlib.h>
#include <math.h>
#include <unistd.h>
#include <signal.h>
#include <string.h>
#include <errno.h>
#include <pthread.h>
#include <stdexcept>
#include <string>
#include <vector>

using namespace std;

static int concurrency = 10;
static int iterations = 10000;
static pthread_t *threads;
static string filename;
static string request;


int
connectToUnixServer(const string &filename) {
	int fd, ret;
	struct sockaddr_un addr;
	
	if (filename.size() > sizeof(addr.sun_path) - 1) {
		throw runtime_error("filename too long");
	}
	
	fd = socket(PF_UNIX, SOCK_STREAM, 0);
	if (fd == -1) {
		perror("socket()");
		exit(1);
	}
	
	addr.sun_family = AF_UNIX;
	memcpy(addr.sun_path, filename.c_str(), filename.size());
	addr.sun_path[filename.size()] = '\0';
	
	ret = connect(fd, (const sockaddr *) &addr, sizeof(addr));
	if (ret == -1) {
		perror("connect()");
		exit(1);
	} else {
		return fd;
	}
}

static void *
workerMain(void *arg) {
	char buf[1024 * 8];
	for (int i = 0; i < iterations / concurrency; i++) {
		int fd = connectToUnixServer(filename);
		bool eof = false;
		write(fd, request.data(), request.size());
		shutdown(fd, SHUT_WR);
		while (!eof) {
			eof = read(fd, buf, sizeof(buf)) <= 0;
		}
		close(fd);
	}
	return NULL;
}

int
main(int argc, char *argv[]) {
	vector<string> args;
	int i;
	
	for (i = 1; i < argc; i++) {
		if (strcmp(argv[i], "-c") == 0) {
			concurrency = atoi(argv[i + 1]);
			i++;
		} else if (strcmp(argv[i], "-n") == 0) {
			iterations = atoi(argv[i + 1]);
			i++;
		} else {
			args.push_back(argv[i]);
		}
	}
	
	if (args.empty()) {
		filename = "server.sock";
	} else {
		filename = args[0];
	}
	
	signal(SIGPIPE, SIG_IGN);
	request.assign("\000\000\000@REQUEST_METHOD\000PING\000PATH_INFO\000/\000PASSENGER_CONNECT_PASSWORD\0001234\000", 68);
	
	struct timeval startTime, endTime;
	
	threads = (pthread_t *) malloc(sizeof(pthread_t) * concurrency);
	gettimeofday(&startTime, NULL);
	for (i = 0; i < concurrency; i++) {
		pthread_create(&threads[i], NULL, &workerMain, NULL);
	}
	for (i = 0; i < concurrency; i++) {
		pthread_join(threads[i], NULL);
	}
	gettimeofday(&endTime, NULL);
	
	unsigned long long diff =
		((unsigned long long) endTime.tv_sec * 1000000 + endTime.tv_usec) -
		((unsigned long long) startTime.tv_sec * 1000000 + startTime.tv_usec);
	printf("%.1f req/sec\n", iterations / (diff / 1000000.0));
	
	return 0;
}
