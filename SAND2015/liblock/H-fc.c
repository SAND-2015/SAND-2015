/* ########################################################################## */
/* (C) UPMC, 2010-2011                                                        */
/*     Authors:                                                               */
/*       Jean-Pierre Lozi <jean-pierre.lozi@lip6.fr>                          */
/*       Gaël Thomas <gael.thomas@lip6.fr>                                    */
/*       Florian David <florian.david@lip6.fr>                                */
/*       Julia Lawall <julia.lawall@lip6.fr>                                  */
/*       Gilles Muller <gilles.muller@lip6.fr>                                */
/* -------------------------------------------------------------------------- */
/* ########################################################################## */
#include <sched.h>
#include <stdio.h>
#include <stdlib.h>
#include <sys/mman.h>
#include <errno.h>
#include <string.h>
#include <stdint.h>
#include "liblock.h"
#include "liblock-fatal.h"

#define MAX_LOCKS 65536

#define CLEANUP_FREQUENCY     100
#define CLEANUP_OLD_THRESHOLD 10

#define NODE_NUM			  8

/*
 * For the Hflat combining algorithm, see
 * [1] Danny Hendler, Itai Incze, Nir Shavit, Moran Tzafrir:
 *     Hflat combining and the synchronization-parallelism tradeoff.
 *     SPAA 2010: 355-364
 */
struct request {
	struct request* volatile next;
	unsigned int volatile active;
	void* (* volatile pending)(void*);
	unsigned int volatile age;
	void* volatile val;
	unsigned int thread_id;
};

struct fc_liblock_impl {
	unsigned int volatile lock;
	unsigned int volatile count;
	struct request* volatile head;
	char pad[pad_to_cache_line(2 * sizeof(unsigned int) + sizeof(void*))];
};

struct liblock_impl {
	struct fc_liblock_impl fc_locks[NODE_NUM];
	unsigned int lock_id;
	unsigned int volatile lock;
	liblock_lock_t* liblock_lock;
	char pad[pad_to_cache_line(
			NODE_NUM * sizeof(struct fc_liblock_impl) + 2 * sizeof(unsigned int) + sizeof(void*))];
};

static unsigned int cur_lock_number = 0;

static __thread struct request __attribute__((aligned (CACHE_LINE_SIZE))) *_local_requests =
		0;

static struct liblock_impl* do_liblock_init_lock(Hflat)(liblock_lock_t* lock,
		struct core* core, pthread_mutexattr_t* attr) {
	int i = 0;
	struct liblock_impl* impl = liblock_allocate(sizeof(struct liblock_impl));

	impl->lock = 0;

	for (i = 0; i < NODE_NUM; i++) {
		impl->fc_locks[i].lock = 0;
		impl->fc_locks[i].count = 0;
		impl->fc_locks[i].head = 0;
	}

	impl->lock_id = __sync_fetch_and_add(&cur_lock_number, 1);

	if (cur_lock_number > MAX_LOCKS)
		fatal("too many Hflat lock");

	return impl;
}

static int do_liblock_destroy_lock(Hflat)(liblock_lock_t* lock) {
	return 0;
}

static void enqueue_request(struct fc_liblock_impl* impl, unsigned int lock_id) {
	struct request* request = &_local_requests[lock_id];
	struct request* supposed;
	request->active = 1;

	do {
		supposed = impl->head;
		request->next = supposed;
	} while (__sync_val_compare_and_swap(&impl->head, supposed, request)
			!= supposed);
}

static void build_request() {
	_local_requests = mmap(0, sizeof(struct request) * MAX_LOCKS,
	PROT_READ | PROT_WRITE, MAP_ANONYMOUS | MAP_PRIVATE, 0, 0);
}

static void* do_liblock_execute_operation(Hflat)(liblock_lock_t* lock,
		void* (*pending)(void*), void* val) {
	int node_id = self.running_core->node->node_id;

	struct liblock_impl* himpl = lock->impl;
	struct fc_liblock_impl* impl = &himpl->fc_locks[node_id];
	struct request* request;

	if (!_local_requests)
		build_request();

	//printf("lock %d\n", impl->lock_id);
	request = &_local_requests[himpl->lock_id];
	request->val = val;
	request->pending = pending;
	request->thread_id = self.id;

	while (1) {
		if (!request->active)
			enqueue_request(impl, himpl->lock_id);

		while (impl->lock && request->pending && request->active)
			PAUSE();

		if (!request->pending)
			return request->val;
		else if (!__sync_val_compare_and_swap(&himpl->lock, 0, 1))
			break;
	}

	if (!request->active)
		enqueue_request(impl, himpl->lock_id);

	unsigned int count = ++impl->count;
	struct request* cur;

	for (cur = impl->head; cur; cur = cur->next) {
		if (cur->pending) {
			cur->val = cur->pending(cur->val);
			cur->pending = 0;
			cur->age = count;
		}
	}

	if (!(count % CLEANUP_FREQUENCY)) {
		struct request* prev = impl->head;
		if (!prev)
			fatal("zarbi");
		while ((cur = prev->next)) {
			if ((cur->age + CLEANUP_OLD_THRESHOLD) < count) {
				prev->next = cur->next;
				cur->active = 0;
			} else
				prev = cur;
		}
	}

	himpl->lock = 0;

	return request->val;
}

static void do_liblock_init_library(Hflat)() {
}

static void do_liblock_kill_library(Hflat)() {
}

static void do_liblock_run(Hflat)(void (*callback)()) {
	if (__sync_val_compare_and_swap(&liblock_start_server_threads_by_hand, 1, 0)
			!= 1)
		fatal("servers are not managed by hand");
	if (callback)
		callback();
}

static int do_liblock_cond_init(Hflat)(liblock_cond_t* cond) {
	fatal("implement me");
}

static int do_liblock_cond_wait(Hflat)(liblock_cond_t* cond,
		liblock_lock_t* lock) {
	fatal("implement me");
}

static int do_liblock_cond_timedwait(Hflat)(liblock_cond_t* cond,
		liblock_lock_t* lock, const struct timespec* ts) {
	fatal("implement me");
}

static int do_liblock_cond_signal(Hflat)(liblock_cond_t* cond) {
	fatal("implement me");
}

static int do_liblock_cond_broadcast(Hflat)(liblock_cond_t* cond) {
	fatal("implement me");
}

static int do_liblock_cond_destroy(Hflat)(liblock_cond_t* cond) {
	fatal("implement me");
}

static void do_liblock_on_thread_exit(Hflat)(struct thread_descriptor* desc) {
}

static void do_liblock_on_thread_start(Hflat)(struct thread_descriptor* desc) {
}

static void do_liblock_unlock_in_cs(Hflat)(liblock_lock_t* lock) {
	fatal("not implemented");
}

static void do_liblock_relock_in_cs(Hflat)(liblock_lock_t* lock) {
	fatal("not implemented");
}

static void do_liblock_declare_server(Hflat)(struct core* core) {
}

liblock_declare(Hflat);
