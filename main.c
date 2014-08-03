#include <stream_io.h>

#ifndef CONFIG_EVENT_WIN32_BUILD
 #include <unistd.h>
 #include <sys/types.h>
 #include <sys/socket.h>
 #include <sys/epoll.h>
 #include <netdb.h>
 #include <ctype.h>
 #include <netinet/in.h>
 #include <arpa/inet.h>
 #include <sys/ioctl.h>
 #include <stdarg.h>
 #include <fcntl.h>
 #include <pthread.h>
 #include <sched.h>
 #include <signal.h>
 #include <sys/syscall.h>
 #include <sys/eventfd.h>
#else
 #include <winsock2.h>
#endif


static struct stream_loop g_loop;
static struct stream g_stream;
static struct stream_request g_read, g_write;

static char send_content[] = "GET / HTTP/1.1\r\nHost: www.baidu.com\r\n\r\n";
static char recv_content[4096] = {0};

static void sender(struct stream_request *req)
{
	printf("Send len: %d\nbuf len: %d\ncontent:\n%s\n", req->result.len, req->buffer.len, req->buffer.buf);
	stream_io_submit(&g_read);
}

static void receiver(struct stream_request *req)
{
	if (req->result.len == 0)
		return;
    *((char *)req->buffer.buf + req->result.len) = 0;
	printf("%s", req->buffer.buf);
	stream_io_submit(&g_read);
}

int main(int argc, char **argv)
{
	int ret;
	int flags;
	int sockfd;
	struct sockaddr_in sa;
	unsigned long sndbuf;
	int ulong_size;
	sa.sin_family = PF_INET;
	sa.sin_port = htons(80);
	sa.sin_addr.s_addr = inet_addr("180.76.3.151");

	sockfd = socket(PF_INET, SOCK_STREAM, IPPROTO_TCP);
	if (sockfd < 0)
		return -errno;
	stream_loop_init(&g_loop, 2);
	printf("Fuck it on!\n");
	ret = connect(sockfd, (struct sockaddr *)&sa, sizeof(sa));
	if (ret < 0)
		return -errno;

	printf("Connected!\n");
	flags = fcntl(sockfd, F_GETFL, 0);
	fcntl(sockfd, F_SETFL, flags | O_NONBLOCK);
	sndbuf = 128;
	setsockopt(sockfd, SOL_SOCKET, SO_SNDBUF, &sndbuf, sizeof(sndbuf));
	getsockopt(sockfd, SOL_SOCKET, SO_SNDBUF, &sndbuf, &ulong_size);
	printf("sndbuf: %u\n", sndbuf);
	stream_init(&g_stream, sockfd);
	stream_activate(&g_loop, &g_stream);

	stream_init_request(&g_read);
	stream_init_request(&g_write);

	g_read.stream = &g_stream;
	g_write.stream = &g_stream;
	g_read.request = REQ_READ;
	g_write.request = REQ_WRITE;
	g_read.buffer.buf = recv_content;
	g_read.buffer.len = sizeof(recv_content);
	g_write.buffer.buf = send_content;
	g_write.buffer.len = sizeof(send_content) - 1;
	g_read.callback = &receiver;
	g_write.callback = &sender;

	stream_io_submit(&g_write);
	while(!stream_loop_start(&g_loop, 0));
}
