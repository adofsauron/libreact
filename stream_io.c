#include <stream_io.h>

#ifndef CONFIG_EVENT_WIN32_BUILD
 #include <sys/types.h>
 #include <ctype.h>

 #include <unistd.h>
 #include <sys/socket.h>
 #include <sys/epoll.h>
 #include <sys/ioctl.h>
 #include <fcntl.h>
 #include <pthread.h>

 #include <sys/eventfd.h>
#else
 #include <winsock2.h>
#endif


#include <stdio.h>
#include <stdlib.h>
#include <memory.h>
#include <string.h>
#include <errno.h>


static inline void __stream_init_io_queue(struct stream *s)
{
	int i;
	for (i = IO_WAIT;i < IO_MAX;++i)
		INIT_LIST_HEAD(s->read_queue + i);
	for (i = IO_WAIT;i < IO_MAX;++i)
		INIT_LIST_HEAD(s->write_queue + i);
}

static inline int __stream_loop_init_bell(struct stream_loop *loop)
{
	loop->bell = eventfd(0, EFD_NONBLOCK);
	if (loop->bell < 0)
		goto ouch;
	do {
		struct epoll_event epev;
		int ret = 0;
		epev.events = EPOLLIN;
		epev.data.ptr = loop;
		ret = epoll_ctl(loop->epoll_fd, EPOLL_CTL_ADD, loop->bell, &epev);
		if (ret < 0)
			goto ouch;
	} while (0);
	return 0;
ouch:
	return -errno;
}

int stream_loop_init(struct stream_loop *loop, int connections)
{
	void *epev;
	loop->epoll_fd = epoll_create(connections);
	if (loop < 0)
		goto oops;

	if (__stream_loop_init_bell(loop) < 0) {
		close(loop->epoll_fd);
		goto oops;
	}
	epev = malloc(connections * sizeof(struct epoll_event));
	if (!epev) {
		close(loop->epoll_fd);
		close(loop->bell);
		goto oops;
	}
	loop->epoll_events = epev;
	loop->connections = connections;
	loop->breaking = 0;

	INIT_LIST_HEAD(&loop->event_queue);
	return 0;
oops:
	return -errno;
}

int stream_activate(struct stream_loop *loop, struct stream *s)
{
	struct epoll_event epev;
	int ret = 0;
	epev.events = EPOLLIN|EPOLLOUT|EPOLLHUP|EPOLLET;
	epev.data.ptr = s;
	ret = epoll_ctl(loop->epoll_fd, EPOLL_CTL_ADD, s->fd, &epev);
	if (ret < 0) {
		s->errcode = errno;
		return -errno;
	}
	printf("Done.\n");
	s->loop = loop;
	return 0;
}

int stream_init(struct stream *s, int fd)
{
	s->fd = fd;
	s->events = 0;
	__stream_init_io_queue(s);
	INIT_LIST_HEAD(&s->node);

	return 0;
}

static int stream_process_events(struct stream_loop *loop)
{
	int i, ret;
	struct stream *s;
	struct epoll_event *epev;
	ret = epoll_wait(loop->epoll_fd, loop->epoll_events, loop->connections, -1);
	if (ret < 0)
		return -errno;
	epev = loop->epoll_events;
	printf("Signal events: %d\n", ret);
	for (i = 0;i < ret;i++) {
		int added = 0;
		if (epev[i].data.ptr == (void *)loop)
			continue;
		if (epev[i].events & EPOLLIN) {
			s = epev[i].data.ptr;
			event_list_add(&s->node, &loop->event_queue);
			added = 1;
			s->events = EV_READ;
		}
		if (epev[i].events & EPOLLOUT) {
			s = epev[i].data.ptr;
			if (!added) {
				event_list_add(&s->node, &loop->event_queue);
				added = 1;
			}
			s->events |= EV_WRITE;
		}
	}
	return ret;
}

int stream_feed(struct stream *s, int events)
{
	s->events |= events;
	if (event_list_empty(&s->node))
		event_list_add(&s->node, &s->loop->event_queue);
}

static int __move_to_queue(struct stream_request *req, struct event_list *dest)
{
	event_list_del(&req->node);
	event_list_add(&req->node, dest);
}

static inline int __do_write(struct stream_loop *loop, struct stream *s, struct stream_request *req)
{
	int nbyte;
	char *buf = req->private.buf;
	while (req->private.len &&
			(nbyte = write(s->fd, buf, req->private.len)) > 0) {
		if (nbyte < 0)
			goto out;

		buf += nbyte;
		req->private.len -= nbyte;
		req->private.buf = buf;
	}
	req->result.len = req->buffer.len - req->private.len;
	req->result.stats = STAT_NONE;
	req->result.errcode = 0;
	s->errcode = 0;
done:
	return 0;
out:
	if (errno && errno != EAGAIN && errno != EINTR) {
		req->result.stats = STAT_ERROR;
		req->result.errcode = errno;
		s->errcode = errno;
	}
	return -errno;
}

static inline int __do_read(struct stream_loop *loop, struct stream *s, struct stream_request *req)
{
	int nbyte;
	char *buf = req->buffer.buf;
	nbyte = read(s->fd, buf, req->buffer.len);
	if (nbyte < 0)
		goto out;
	req->result.len = nbyte;
	if (nbyte) {
		req->result.stats = STAT_NONE;
		req->result.errcode = 0;
	} else {
		req->result.stats = STAT_CONN_CLOSED;
		req->result.errcode = 0;
	}
	s->errcode = 0;
done:
	return 0;
out:
	if (errno && errno != EAGAIN && errno != EINTR) {
		req->result.stats = STAT_ERROR;
		req->result.errcode = errno;
		s->errcode = errno;
	}
	return -errno;
}

static int __process_read_request(struct stream_loop *loop, struct stream *s)
{
	int np = 0;
	struct stream_request *req;
	while (!event_list_empty(&s->read_queue[IO_WAIT])) {
		int ret;
		req = event_list_first_entry(&s->read_queue[IO_WAIT], struct stream_request, node);
		ret = __do_read(loop, s, req);
		if (ret && ret != -EAGAIN && ret != -EINTR)
			goto out;
		if (!ret) {
			__move_to_queue(req, &s->read_queue[IO_FIN]);
		} else
			break;
		np++;
	}
	return np;
out:
	__move_to_queue(req, &s->read_queue[IO_FIN]);
	return -errno;
}

static int __process_write_request(struct stream_loop *loop, struct stream *s)
{
	int np = 0;
	struct stream_request *req;
	while (!event_list_empty(&s->write_queue[IO_WAIT])) {
		int ret;
		req = event_list_first_entry(&s->write_queue[IO_WAIT], struct stream_request, node);
		ret = __do_write(loop, s, req);
		if (ret && ret != -EAGAIN && ret != -EINTR)
				goto out;
		if (!ret)
			__move_to_queue(req, &s->write_queue[IO_FIN]);
		else
			break;
		np++;
	}
	return np;
out:
	__move_to_queue(req, &s->write_queue[IO_FIN]);
	return -errno;
}

static int __process_read_callback(struct stream_loop *loop, struct stream *s)
{
	struct stream_request *req;
	while (!event_list_empty(&s->read_queue[IO_FIN])) {
		req = event_list_first_entry(&s->read_queue[IO_FIN], struct stream_request, node);
		event_list_del(&req->node);
		req->callback(req);
		if (s->errcode && s->errcode != EAGAIN && s->errcode != EINTR)
			break;
	}
}

static int __process_write_callback(struct stream_loop *loop, struct stream *s)
{
	struct stream_request *req;
	while (!event_list_empty(&s->write_queue[IO_FIN])) {
		req = event_list_first_entry(&s->write_queue[IO_FIN], struct stream_request, node);
		event_list_del(&req->node);
		req->callback(req);
		if (s->errcode && s->errcode != EAGAIN && s->errcode != EINTR)
			break;
	}
}

void stream_io_submit(struct stream_request *req)
{
	struct stream *s = req->stream;
	struct stream_loop *loop = s->loop;
	struct event_list *list;
	int events;
	req->private.buf = req->buffer.buf;
	req->private.len = req->buffer.len;

	if (req->request == REQ_READ) {
		list = &s->read_queue[IO_WAIT];
		events = EV_READ;
	} else if (req->request == REQ_WRITE) {
		list = &s->write_queue[IO_WAIT];
		events = EV_WRITE;
	}

	if (event_list_empty(&req->node))
		event_list_add(&req->node, list);

	stream_feed(s, events);
	return;
}

int stream_init_request(struct stream_request *req)
{
	INIT_LIST_HEAD(&req->node);
}

static inline int __stream_worker_init_bell(struct stream_worker *worker)
{
	worker->bell = eventfd(0, EFD_NONBLOCK);
	if (worker->bell < 0)
		goto ouch;
	do {
		struct epoll_event epev;
		int ret = 0;
		epev.events = EPOLLIN;
		epev.data.ptr = worker;
		ret = epoll_ctl(worker->epoll_fd, EPOLL_CTL_ADD, worker->bell, &epev);
		if (ret < 0)
			goto ouch;
	} while (0);
	return 0;
ouch:
	return -errno;
}

static struct stream_worker *stream_worker_create(struct stream_loop *loop, int id)
{
    int err;
	struct stream_worker *worker;
	worker = (struct stream_worker *)malloc(sizeof(struct stream_worker));
	if (!worker) {
		printf("Allocate memory for worker failed!\n");
        err = -ENOMEM;
		goto ouch;
	}
	worker->worker_pid = worker->worker_id = id;
	worker->load = 0;
	if ((err = __stream_worker_init_bell(worker)) < 0)
        goto ouch;
	worker->epoll_fd = epoll_create(1);
    if (worker->epoll_fd < 0)
        goto ouch;
    worker->loop = loop;
ouch:
    return NULL;
}

static void __stream_get_worker(struct stream_loop *loop, int id)
{
	
}

int stream_loop_start(struct stream_loop *loop, int once)
{
	int ret;
	int breaking = once;
	struct stream_worker *worker;
	int id = getpid();
	do {
		struct stream *s;
		if (!event_list_empty(&loop->event_queue))
			goto try_proceed;

		ret = stream_process_events(loop);
		if (ret < 0)
			break;
try_proceed:
		while (!event_list_empty(&loop->event_queue)) {
			s = event_list_first_entry(&loop->event_queue, struct stream, node);
			event_list_del(&s->node);
			if (s->events & EV_READ)
				__process_read_request(loop, s);
			if (s->events & EV_WRITE)
				__process_write_request(loop, s);

			s->events = 0;
			__process_read_callback(loop, s);
			__process_write_callback(loop, s);
		}
	} while (!breaking && !loop->breaking);
	loop->breaking = 0;

	return ret;
}


