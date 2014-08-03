#ifndef _STREAM_IO_H
#define _STREAM_IO_H

#include <ev.h>


#define REQ_READ 1
#define REQ_WRITE 2

struct stream_request;
typedef void (*request_cb_t)(struct stream_request *);

struct stream_result {
	unsigned int len;
#define STAT_NONE 1
#define STAT_CONN_CLOSED 2
#define STAT_ERROR -1
	int stats;
	int errcode;
};

struct stream_buffer {
	void *buf;
	unsigned int len;
};

struct stream_request {
	int request;
	struct stream *stream;

	int waterlevel;
	struct stream_buffer buffer;
	struct stream_result result;

	request_cb_t callback;

	struct event_list node;
	struct stream_buffer private;
};

struct stream_loop;

struct stream {
	int fd;
	struct stream_loop *loop;
#define IO_WAIT 0
#define IO_FIN 1
#define IO_MAX 2

	int events;
	int errcode;
	struct event_list read_queue[IO_MAX];
	struct event_list write_queue[IO_MAX];
	struct event_list node;
};

struct stream_locker {
	int holder_id;
	int lock;
};

struct stream_loop {
	int epoll_fd;
	int bell;
	int breaking;
	int connections;
	void (*bell_handle)(struct stream_loop *, struct stream *);
	struct epoll_event *epoll_events;
	struct event_list event_queue;
	struct event_rb_root *worker_tree;
	struct event_rb_root *timer;
    struct event_rb_root *load_tree;
};

struct stream_timer {
	int timer;
	struct event_rb_node rb_node;
};

struct stream_worker {
	int worker_pid;
	int worker_id;
	int load;
	int bell;
	int epoll_fd;
	struct epoll_event *epoll_events;
	struct event_list event_queue;
	struct stream_locker data_lock;
	struct stream_loop *loop;
	struct event_rb_root *timer;
	struct event_rb_node rb_worker_node;
    struct event_rb_node rb_load_node;
};

#endif
