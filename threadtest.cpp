#include <stdio.h>
#include <unistd.h>
#include <fcntl.h>
#include <stdlib.h>
#include <pthread.h>
#include <string.h>
#include <queue>

using namespace std;

#define TEST_THREAD
#define NTHREADS 4
#define ITERATIONS 500000

typedef void (*Callback)(void *sender, void *data);

static pthread_t threads[NTHREADS];


void
makeNonBlock(int fd) {
	int flags = fcntl(fd, F_GETFL);
	fcntl(fd, F_SETFL, flags | O_NONBLOCK);
}

static void
readInput() {
	char buf[1024 * 4];
	read(0, buf, sizeof(buf));
}

static void
writeOutput(int size) {
	char buf[size];
	memset(buf, 0, size);
	write(2, buf, size);
}

static void
doSmallWork() {
	int i, j = 0;
	for (i = 0; i < 250; i++) {
		j = j * 2;
	}
}

static void
doLargeWork() {
	int i, j = 0;
	for (i = 0; i < 3000; i++) {
		j = j * 2;
	}
}

struct ScopedLock {
	pthread_mutex_t *mutex;
	bool locked;
	
	ScopedLock(pthread_mutex_t *m) {
		mutex = m;
		pthread_mutex_lock(mutex);
		locked = true;
	}
	
	~ScopedLock() {
		if (locked) {
			pthread_mutex_unlock(mutex);
		}
	}
	
	void unlock() {
		pthread_mutex_unlock(mutex);
		locked = false;
	}
};

struct Object {
	Callback cb;
	void *data;
	
	Object(Callback cb, void *data) {
		this->cb = cb;
		this->data = data;
	}
	
	~Object() {
		cb(this, data);
	}
};


#ifdef TEST_THREAD


struct Pool {
	pthread_mutex_t mutex;
	pthread_cond_t cond;
	int available;
	
	Pool() {
		pthread_mutex_init(&mutex, NULL);
		pthread_cond_init(&cond, NULL);
		available = 7;
	}
	
	Object *checkout() {
		ScopedLock lock(&mutex);
		while (available == 0) {
			pthread_cond_wait(&cond, &mutex);
		}
		available--;
		doSmallWork();
		sendSignals();
		return new Object(checkout, this);
	}
	
	void sendSignals() {
		if (available > 0) {
			pthread_cond_signal(&cond);
		}
	}
	
	static void checkout(void *sender, void *data) {
		Pool *self = (Pool *) data;
		ScopedLock lock(&self->mutex);
		self->available++;
		self->sendSignals();
	}
};

static Pool pool;

static void *
workerMain(void *arg) {
	unsigned int i;
	for (i = 0; i < (ITERATIONS) / (NTHREADS); i++) {
		readInput(); // Simulate accept()
		
		readInput();
		Object *o = pool.checkout();
		readInput();
		readInput();
		doLargeWork();
		writeOutput(1024 * 8);
		writeOutput(1024 * 8);
		delete o;
	}
	return NULL;
}

int
main() {
	int i;
	
	freopen("/dev/zero", "rb", stdin);
	freopen("/dev/null", "a", stderr);
	
	for (i = 0; i < NTHREADS; i++) {
		pthread_create(&threads[i], NULL, &workerMain, NULL);
	}
	for (i = 0; i < NTHREADS; i++) {
		pthread_join(threads[i], NULL);
	}
	return 0;
}


#else


#include <ev++.h>


struct Waiter {
	Callback cb;
	void *data;
	
	Waiter(Callback cb, void *data) {
		this->cb = cb;
		this->data = data;
	}
};

struct Pool {
	struct ev_loop *loop;
	queue<Waiter> waiters;
	pthread_mutex_t mutex;
	int available;
	
	Pool(struct ev_loop *loop) {
		this->loop = loop;
		pthread_mutex_init(&mutex, NULL);
		available = 7;
	}
	
	void checkout(Callback cb, void *data) {
		ScopedLock lock(&mutex);
		checkoutWithoutLocking(lock, cb, data);
	}
	
	void checkoutWithoutLocking(ScopedLock &lock, Callback cb, void *data) {
		if (available == 0) {
			waiters.push(Waiter(cb, data));
		} else {
			available--;
			doSmallWork();
			lock.unlock();
			cb(new Object(checkout, this), data);
		}
	}
	
	static void checkout(void *sender, void *data) {
		Pool *self = (Pool *) data;
		ScopedLock lock(&self->mutex);
		self->available++;
		if (!self->waiters.empty()) {
			Waiter waiter = self->waiters.front();
			self->waiters.pop();
			self->checkoutWithoutLocking(lock, waiter.cb, waiter.data);
		}
	}
};

static Pool *pool;
static struct ev_loop *loops[NTHREADS];
static int exitPipe[2];

struct Worker {
	struct ev_loop *loop;
	ev_idle idle;
	int iteration;
	pthread_mutex_t mutex;
	vector<Object *> checkedOutObjects;
	ev_async async;
};

static void
onAsync(struct ev_loop *loop, ev_async *w, int revents) {
	Worker *worker = (Worker *) w->data;
	ScopedLock lock(&worker->mutex);
	vector<Object *> objects = worker->checkedOutObjects;
	worker->checkedOutObjects.clear();
	lock.unlock();
	
	vector<Object *>::iterator it;
	vector<Object *>::iterator end = objects.end();
	
	for (it = objects.begin(); it != end; it++) {
		Object *o = *it;
		readInput();
		readInput();
		doLargeWork();
		writeOutput(1024 * 8);
		writeOutput(1024 * 8);
		delete o;
		worker->iteration++;
	}
	
	ev_idle_start(worker->loop, &worker->idle);
}

static void
checkedOut(void *object, void *data) {
	Object *o = (Object *) object;
	Worker *worker = (Worker *) data;
	
	ScopedLock lock(&worker->mutex);
	worker->checkedOutObjects.push_back(o);
	lock.unlock();
	
	if (pool->loop == worker->loop) {
		onAsync(worker->loop, &worker->async, 0);
	} else {
		ev_async_send(worker->loop, &worker->async);
	}
}

static void
onIdle(struct ev_loop *loop, ev_idle *w, int revents) {
	Worker *worker = (Worker *) w->data;
	if (worker->iteration == (ITERATIONS) / (NTHREADS)) {
		ev_unloop(worker->loop, EVUNLOOP_ONE);
		return;
	}
	
	readInput();
	ev_idle_stop(worker->loop, &worker->idle);
	pool->checkout(checkedOut, worker);
}

static void *
workerMain(void *arg) {
	Worker worker;
	worker.loop = (struct ev_loop *) arg;
	ev_idle_init(&worker.idle, onIdle);
	worker.idle.data = &worker;
	worker.iteration = 0;
	pthread_mutex_init(&worker.mutex, NULL);
	ev_async_init(&worker.async, onAsync);
	worker.async.data = &worker;
	
	ev_idle_start(worker.loop, &worker.idle);
	ev_async_start(worker.loop, &worker.async);
	ev_loop(worker.loop, 0);
	
	char c;
	read(exitPipe[0], &c, 1);
	return NULL;
}

int
main() {
	int i;
	
	freopen("/dev/zero", "rb", stdin);
	freopen("/dev/null", "a", stderr);
	makeNonBlock(0);
	makeNonBlock(2);
	
	pipe(exitPipe);
	for (i = 0; i < NTHREADS; i++) {
		loops[i] = ev_loop_new(EVFLAG_AUTO);
	}
	pool = new Pool(loops[0]);
	for (i = 0; i < NTHREADS; i++) {
		pthread_create(&threads[i], NULL, &workerMain, loops[i]);
	}
	char exitData[NTHREADS];
	write(exitPipe[1], exitData, NTHREADS);
	for (i = 0; i < NTHREADS; i++) {
		pthread_join(threads[i], NULL);
	}
	return 0;
}


#endif

