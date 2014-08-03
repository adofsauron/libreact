#ifndef _EVENT_H
#define _EVENT_H

#define _LARGEFILE_SOURCE
#define _LARGEFILE64_SOURCE


#include <errno.h>
#include <inttypes.h>
#include <stdbool.h>
#include <ev_rbtree.h>
typedef int evutil_socket_t;
#define EXPORT_SYMBOL(s) 
#define EV_READ 0x1
#define EV_WRITE 0x2
#define EV_EOF 0x4
#define EV_ERR 0x8
#define EV_CLOSE 16

#ifndef offsetof
#define offsetof(type, member) ((size_t) &((type *)0)->member)
#endif
#ifndef container_of
#define container_of(ptr, type, member) \
		((type *)( \
		(char *)(ptr) - \
		(unsigned long)(&((type *)0)->member)))
#endif

#ifdef CONFIG_EVENT_WIN32_BUILD
#define inline __inline
#endif


struct event_list {
	struct event_list *prev, *next;
};

/**
 * INIT_LIST_HEAD - Initialize a list head.
 * @list: The list head to be reset.
 */
static inline void INIT_LIST_HEAD(struct event_list *list)
{
	list->next = list;
	list->prev = list;
}

/*
 * Insert a new entry between two known consecutive entries.
 *
 * This is only for internal list manipulation where we know
 * the prev/next entries already!
 */
static inline void __event_list_add(struct event_list *newp,
                              struct event_list *prev,
                              struct event_list *next)
{
	next->prev = newp;
	newp->next = next;
	newp->prev = prev;
	prev->next = newp;
}

/**
 * event_list_add - add a newp entry
 * @newp: newp entry to be added
 * @head: list head to add it after
 *
 * Insert a newp entry after the specified head.
 */
static inline void event_list_add(struct event_list *newp, struct event_list *head)
{
	__event_list_add(newp, head, head->next);
}

/**
 * event_list_add_tail - add a newp entry
 * @newp: newp entry to be added
 * @head: list head to add it before
 *
 * Insert a newp entry before the specified head.
 */
static inline void event_list_add_tail(struct event_list *newp, struct event_list *head)
{
	__event_list_add(newp, head->prev, head);
}

/*
 * Delete a list entry by making the prev/next entries
 * point to each other.
 *
 * This is only for internal list manipulation where we know
 * the prev/next entries already!
 */
static inline void __event_list_del(struct event_list * prev, struct event_list * next)
{
	next->prev = prev;
	prev->next = next;
}

/**
 * event_list_del - deletes entry from list.
 * @entry: the element to delete from the list.
 * Note: evlist_empty() on entry does not return true after this, the entry is
 * in an undefined state.
 */
static inline void event_list_del(struct event_list *entry)
{
	__event_list_del(entry->prev, entry->next);
	entry->next = entry;
	entry->prev = entry;
}

/**
 * event_list_empty - tests whether a list is empty
 * @head: the list to test.
 */
static inline int event_list_empty(struct event_list *head)
{
	return head->next == head;
}

/**
 * event_list_first_entry - get the first element from a list
 * @ptr:	the list head to take the element from.
 * @type:	the type of the struct this is embedded in.
 * @member:	the name of the evlist_struct within the struct.
 *
 * Note, that list is expected to be not empty.
 */
#define event_list_first_entry(ptr, type, member) \
	container_of((ptr)->next, type, member)

/**
 * event_list_for_each_safe - iterate over a list safe against removal of list entry
 * @pos:	the &struct evlist_head to use as a loop cursor.
 * @n:		another &struct evlist_head to use as temporary storage
 * @head:	the head for your list.
 */
#define event_list_for_each_safe(pos, n, head) \
	for (pos = (head)->next, n = pos->next; pos != (head); \
	pos = n, n = pos->next)

#if 0

struct event;
struct event_modreg {
	int em_type;
	void *em_data;
};

struct event_module {
	int em_type;
	char *em_name;
	
	int(*em_init)(void);
	int(*em_exit)(void);
	void *em_arg;
	struct event_modreg *em_modreg;
	
	struct event_list em_node;
};

#endif

struct evmap_item {
	/* The file-descriptor which stands for this event object. */
	evutil_socket_t ei_fd;
	int ei_delete;

	unsigned long ei_watches;
	unsigned long ei_events;
	
#define EV_MAX 4
	struct event_list ei_queue[EV_MAX];

	/* For signal appearing. */
	struct event_list ei_next;

	/* Red-Black tree node */
	struct event_rb_node ei_node;
};

struct event_base;
struct eventop {
	char *name;
	
	int(*init)(void);
	int(*exit)(void);
	
	int(*add)(struct evmap_item *);
	int(*mod)(struct evmap_item *);
	int(*del)(struct evmap_item *);
	int (*process)(struct event_base *);
};

#define EVENTOP_FILL(name, init, exit, add, mod, del, process) \
	{name, init, exit, add, mod, del, process}


struct eventio {
	char *name;

	int(*recv)(evutil_socket_t fd, char *buf, size_t len, int flags);
	int(*send)(evutil_socket_t fd, char *buf, size_t len, int flags);
	int(*read)(int fd, char *buf, size_t len);
	int(*write)(int fd, char *buf, size_t len);
	int(*buf_size)(evutil_socket_t fd, int rw);
	int(*pending_size)(evutil_socket_t fd, int rw);
};

#define EVENTIO_FILL(n, r, s, re, wr, b, p) \
{ .name = n, .recv = r, .send = s, .read = re, .write = wr, .buf_size = b, .pending_size = p }

#if 1

extern struct eventio ev_unix_io;
#define event_io ev_unix_io

#endif


struct event_base {
	/** Function pointers and other data to describe this event_base's
	 * backend. */
	struct eventop *backendop;
	/** Pointer to backend-specific data. */
	void *backend_private;

	/** Number of total events added to this event_base */
	int nr_event;
	/** Number of total events active in this event_base */
	int nr_active;
	
	/** Set if we should terminate the loop immediately */
	int event_break;

	/** Set if we're running the event_base_loop function, to prevent
	 * reentrant invocation. */
	int running_loop;

	/** All events that have been enabled (added) in this event_base */
	struct event_list active_queue;

#if defined(_EVENT_HAVE_CLOCK_GETTIME) && defined(CLOCK_MONOTONIC)
	/** Difference between internal time (maybe from clock_gettime) and
	 * gettimeofday. */
	struct timeval tv_clock_diff;
	/** Second in which we last updated tv_clock_diff, in monotonic time. */
	time_t last_updated_clock_diff;
#endif
};

struct event_base;
struct event {
	/* Queue node area for the event list and active queue. */
	struct event_list ev_next;
	
	/* The file-descriptor which stands for this event object. */
	evutil_socket_t ev_fd;

	/* This event_base is the belonger of thie event object. */
	struct event_base *ev_base;
	
	/* The setting flags, watcher and events. */
	unsigned long ev_watch;
	unsigned long ev_flags;
	
	struct evmap_item *ev_mapitem;


	/* allows us to adopt for different types of events */
	void (*ev_callback)(evutil_socket_t, unsigned long, void *arg);
	void *ev_arg;
};


struct event_buffer {
	/* The pointer of buffer that user posted. */
	char *eb_buf;

	/* The length of the request. */
	int eb_len;
};

struct event_aiocb {
	struct event_list aio_next;

	/* The event structure which handles events. */
	struct event *aio_ev;
	
	/* User buffer for data IO. */
	struct event_buffer aio_buf;

	/* Private buffer pointer. */
	struct event_buffer __aio_buf_ptr;

	/* Which AIO request is this ? */
	int aio_req;

	/* AIO callback for this AIO operation. */
	void (*aio_cb)(struct event_aiocb *, unsigned long, void *);

	/* Arguments being transferred to the callback. */
	void *aio_arg;
};

struct event_stream;
struct event_io_req {
	/* IO request chain. */
	struct event_list irq_next;

	/* IO request file-descriptor. */
	int fd;

	/* Type for this request. */
	union {
		int type;
		#define EIO_READ 0
		#define EIO_WRITE 1
		int rw;
	};

	/* User buffer for data IO. */
	struct event_buffer irq_buf;

	/* Private buffer pointer. */
	struct event_buffer __irq_buf_ptr;

	/* After the request is finished, we
	   store the result here. */
	struct event_buffer irq_buf_ret;

	int irq_errcode;

	int irq_pid;

	/* Callback for this IO operation. */
	void (*irq_cb)(struct event_stream *, int io, 
		struct event_io_req *, int len);

	/* Callback for EIO_ASYNC request. */
	void (*irq_eio_cb)(struct event_io_req *);

	/* Callback arguments. */
	void *irq_cb_arg;
};

struct event_stream {
	int s_lock;

	/* Reading queue of this stream. */
	struct event_list s_readq;

	/* Writing queue of this stream. */
	struct event_list s_writeq;

	/* Write callback queue of this stream. */
	struct event_list s_write_completed_q;
	
	/* Event which handles the read events. */
	struct event *s_rev;

	/* Event which handles the write events. */
	struct event *s_wev;

	/* Whether the write event is being added. */
	int s_wev_insert;
	
	/* When the stream is closing, s_closing=1. */
	int s_closing;
	
	/* After finishing the cleaning, make a
	 notification by callback routine. */
	void (*s_close_cb)(struct event_stream *);
};
 

#ifdef CONFIG_EVENT_WIN32_BUILD
extern const struct eventop win32_select;
#else
extern const struct eventop epoll;
#endif

int
event_init(struct eventop *evop);
int
event_add(struct event *ev);
int
event_del(struct event *ev);
void
event_feed(struct event *ev, int io);
void
event_feed_fd(int fd, int io);
void
event_feed_mapitem(struct evmap_item *item, int io);
void
event_set(struct event *ev, evutil_socket_t fd, unsigned long events,
	  void (*callback)(evutil_socket_t, unsigned long, void *), void *arg);
int
event_aio_request(evutil_socket_t fd,
	struct event_aiocb *aiocb, 
	int io);
int
event_stream_read_start(
	struct event_stream *stream, 
	struct event_io_req *req);
int event_stream_read_stop(
	struct event_stream *stream);
int event_stream_write(
	struct event_stream *stream, 
	struct event_io_req *req);
int event_stream_init(struct event_stream *stream, evutil_socket_t fd);
#endif
