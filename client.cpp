#include <sys/socket.h>
#include <sys/un.h>
#include <sys/types.h>
#include <sys/time.h>
#include <stdio.h>
#include <stdlib.h>
#include <math.h>
#include <unistd.h>
#include <string.h>
#include <errno.h>
#include <pthread.h>
#include <stdexcept>
#include <string>
#include <vector>

using namespace std;

#define ITERATIONS 100000

static int concurrency = 10;
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
		throw runtime_error("Cannot create a Unix socket file descriptor");
	}
	
	addr.sun_family = AF_UNIX;
	memcpy(addr.sun_path, filename.c_str(), filename.size());
	addr.sun_path[filename.size()] = '\0';
	
	bool retry = true;
	int counter = 0;
	while (retry) {
		try {
			ret = connect(fd, (const sockaddr *) &addr, sizeof(addr));
		} catch (...) {
			do {
				ret = close(fd);
			} while (ret == -1 && errno == EINTR);
			throw;
		}
		if (ret == -1) {
			perror("connect()");
			#if defined(sun) || defined(__sun)
				/* Solaris has this nice kernel bug where connecting to
				 * a newly created Unix socket which is obviously
				 * connectable can cause an ECONNREFUSED. So we retry
				 * in a loop.
				 */
				retry = errno == ECONNREFUSED;
			#else
				retry = false;
			#endif
			retry = false;
			retry = retry && counter < 9;
			
			if (retry) {
				usleep((useconds_t) (10000 * pow((double) 2, (double) counter)));
				counter++;
			} else {
				do {
					ret = close(fd);
				} while (ret == -1 && errno == EINTR);
				throw runtime_error("Cannot connect");
			}
		} else {
			return fd;
		}
	}
	abort();   // Never reached.
	return -1; // Shut up compiler warning.
}

static void *
workerMain(void *arg) {
	char buf[1024 * 8];
	for (int i = 0; i < (ITERATIONS) / concurrency; i++) {
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
		} else {
			args.push_back(argv[i]);
		}
	}
	
	if (args.empty()) {
		filename = "server.sock";
	} else {
		filename = args[0];
	}
	
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
	printf("%.1f req/sec\n", (ITERATIONS) / (diff / 1000000.0));
	
	return 0;
}
