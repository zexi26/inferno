/*
  * generic shared memory routines for devshm
  */
#define		_XOPEN_SOURCE

#include	"dat.h"
#include	"fns.h"
#include	"error.h"
#include 	"ip.h"
#include   "shm.h"

#include	<sys/types.h>
#include	<sys/shm.h>
#include	<sys/ipc.h>

/* 
  * Shared memory circular buffer code
  */

static void
yield(int x)
{
	struct timespec yield_time;

	if(x==0) {
		osyield();
	} else {
		yield_time.tv_sec = 0;
		yield_time.tv_nsec = x; 
		nanosleep(&yield_time); 
	}
}

/* TODO: Fix - figure out what we really need to do here */
#define mb() __asm__ __volatile__ ("": : :"memory")

/*
 * (expr) may be as much as (limit) "below" zero (in an unsigned sense).
 * We add (limit) before taking the modulus so that we're not dealing with
 * "negative" numbers.
 */
#define CIRCULAR(expr, limit) (((expr) + (limit)) % (limit))


static inline int 
check_write_buffer(const Channel *h, __u32 bufsize)
{
	/* Buffer is "full" if the write index is one behind the read index. */
	return (h->write != CIRCULAR((h->read - 1), bufsize));
}

static inline int 
check_read_buffer(const Channel *h, __u32 bufsize)
{
	/* Buffer is empty if the read and write indices are the same. */
	return (h->read != h->write);
}

/* We can't fill last byte: would look like empty buffer. */
static char *
get_write_chunk(const Channel *h, char *buf, __u32 bufsize, __u32 *len)
{
        /* We can't fill last byte: would look like empty buffer. */
	__u32 write_avail = CIRCULAR(((h->read - 1) - h->write), bufsize);
	*len = ((h->write + write_avail) <= bufsize) ?
		write_avail : (bufsize - h->write);
	return buf + h->write; 
}

static const char *
get_read_chunk(const Channel *h, const char *buf, __u32 bufsize, __u32 *len)
{
	__u32 read_avail = CIRCULAR((h->write - h->read), bufsize);
       	*len = ((h->read + read_avail) <= bufsize) ?
		read_avail : (bufsize - h->read);
	return buf + h->read; 
}

static void 
update_write_chunk(Channel *h, __u32 bufsize, __u32 len)
{
	/* fprint(2, "> %x\n",len); DEBUG */
	h->write = CIRCULAR((h->write + len), bufsize);
	mb();
}

static void 
update_read_chunk(Channel *h, __u32 bufsize, __u32 len)
{
	/* fprint(2, "< %x\n",len); DEBUG */
	h->read = CIRCULAR((h->read + len), bufsize);
	mb();
}

#undef CIRCULAR

int 
shmwrite(struct Conv *conv, void *src, unsigned long len)
{
	int ret = 0;
	struct chan_pipe *p = (struct chan_pipe *)conv->chan;
	Channel *c = &p->out;
	int bufoff = 0;
	__u32 bufsize = p->buflen;

	if(conv->mode != SM_CLIENT) {
		c = &p->in;
		bufoff = p->buflen;
	}

	while (!check_write_buffer(c, bufsize)) {
		yield(conv->datapoll);
	}

        while (len > 0) {
		__u32 thislen;
		char *dst = get_write_chunk(c, p->buffers+bufoff, bufsize, &thislen); 

		if (thislen == 0) {
			yield(conv->datapoll);
			continue;
		}
		
		if (thislen > len)
			thislen = len;
		memcpy(dst, src, thislen);
		update_write_chunk(c, bufsize, thislen);
		src += thislen;
		len -= thislen;
		ret += thislen;
	}

	/* Must have written data before updating head. */
	return ret;
}



int 
shmread(struct Conv *conv, void *dst,  unsigned long len)
{
	int ret = 0;
	struct chan_pipe *p = (struct chan_pipe *)conv->chan;
	Channel *c = &p->out;
	int bufoff = 0;
	__u32 bufsize = p->buflen;

	if(conv->mode == SM_CLIENT) {
		c = &p->in;
		bufoff = p->buflen;
	}

	while (!check_read_buffer(c, bufsize)) {
		if((p->magic == 0xDEADDEAD)||(p->state == Hungup)) {
			conv->state = Hungup;
			return 0;
		}
		yield(conv->datapoll);
	}

	while (len > 0) {
		__u32 thislen;
		const char *src;
		src = get_read_chunk(c, p->buffers+bufoff, bufsize, &thislen); 
		if (thislen == 0) {
			if((p->magic == 0xDEADDEAD)||(p->state == Hungup)) {
				conv->state = Hungup;
				return 0;
			}
			yield(conv->datapoll);
			continue;
		}
		if (thislen > len) 
			thislen = len;
		memcpy(dst, src, thislen);
		update_read_chunk(c, bufsize, thislen);
		
		dst += thislen;
		len -= thislen;
		ret += thislen;
		break; /* obc */
	}

	/* Must have read data before updating head. */
	return ret;
}

int
shmdebug(struct Conv *c, void *buf, unsigned long len)
{
	int ret;
	struct chan_pipe *chan = (struct chan_pipe *) c->chan;
	ret = sprint(buf, "Magic: %ux\nBuflen: %ux\nOut\n Out.Magic: %ux\n Out.write: %ux\n Out.read: %ux\n Out.over: %ux\nIn\n In.Magic: %ux\n In.write: %ux\n In.read: %ux\n In.over: %ux\nIn.buf:%ux\nOut.buf:%ux\nBuf:%ux\n",
		chan->magic, chan->buflen, chan->out.magic, chan->out.write, chan->out.read, 
		chan->out.overflow, chan->in.magic, chan->in.write, chan->in.read,
		chan->in.overflow, chan->buffers+chan->buflen, chan->buffers+0, chan->buffers);
	return ret;
}

int
shmlisten(struct Conv *c)
{
	struct chan_pipe *chan = (struct chan_pipe *)c->chan;

	while((chan->state != Connecting)&&(chan->magic != 0xDEADDEAD))
		yield(0);

	if(chan->magic == 0xDEADDEAD) {
		c->state=chan->state=Hungup;
		return -1;
	}
	chan->state = Connected;
	mb();
	return 0;
}