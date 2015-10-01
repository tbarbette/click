// -*- c-basic-offset: 4 -*-
#ifndef CLICK_TONETMAPDEVICE_HH
#define CLICK_TONETMAPDEVICE_HH
#include <click/config.h>
#include <click/element.hh>
#include <click/task.hh>
#include <click/timer.hh>
#include <click/notifier.hh>
#include <click/etheraddress.hh>
#include <click/vector.hh>
#include <click/sync.hh>
#include "netmapinfo.hh"


CLICK_DECLS

/*
 * =title ToDPDKDevice
 *
 * =c
 *
 * ToNetmapDevice(DEVNAME [, QUEUE, [, I<keywords> PROMISC, BURST])
 *
 * =s comm
 *
 * Send packets through a Netmap device, optionnaly specifying a queue number.
 * This element is NOT thread safe. This element supports both push and pull.
 * In push mode, it will batch packets up to BURST then sends them. If the
 * output rings are full, it will either block until there is some space (if
 *  BLOCKANT is set) or it will internally queue the packets and finally drops
 *  them if there is still not enough space.
 *
 * =item DEVNAME
 *
 * String Device name
 *
 * =item QUEUE
 *
 * Integer.  Index of a queue. If set to a negative value or if not defined, all
 * 	queues will be used.
 *
 * =item IQUEUE (push mode only)
 *
 * Unsigned integer Number of packets that we can bufferize if all output rings are full while in push mode
 *
 * =item BLOCKING (push mode only)
 *
 * Boolean. If true and packets are pushed and the IQUEUE is full, the element will loop until there is space in the output ring, or we'll drop. Default true.
 *
 * =item BURST
 *
 * Number of packets to batch before sending them out.
 *
 * =h n_sent read-only
 *
 * Returns the number of packets sent by the device.
 *
 * =h n_dropped read-only
 *
 * Returns the number of packets dropped by the device.
 *
 * =h reset_counts write-only
 *
 * Resets n_send and n_dropped counts to zero.
 *
 */


class ToNetmapDevice: public Element {

public:

	ToNetmapDevice() CLICK_COLD;

	void selected(int, int);

	const char *class_name() const		{ return "ToNetmapDevice"; }
	const char *port_count() const		{ return "1/0-2"; }
	const char *processing() const		{ return "a/h"; }
	const char *flags() const			{ return "S2"; }
	int	configure_phase() const			{ return CONFIGURE_PHASE_PRIVILEGED; }

	void do_txsync(int fd);
	void allow_txsync();
	void try_txsync(int queue, int fd);

	void cleanup(CleanupStage);
	int configure(Vector<String>&, ErrorHandler*) CLICK_COLD;
	int initialize(ErrorHandler*) CLICK_COLD;

	void add_handlers() CLICK_COLD;

	void push(int, Packet*);
	void run_timer(Timer *timer);

	bool run_task(Task *);
	static String toqueue_read_rate_handler(Element *e, void *thunk);

private :
	unsigned int _burst;
	bool _block;
	Spinlock _lock;
	unsigned long last_count;
	unsigned long last_rate;
	Timer* _pushtimer;
	Vector<bool> _iodone;
	bool _debug;
	enum { h_signal };

	class State {
	public:
		State() : q(NULL), q_size(0) {};
		Packet* q; //Pending packets to send
		unsigned int q_size;
	};
	Vector<State> _state;

	unsigned int _internal_queue;

	unsigned int send_packets(Packet* &packet, bool push);

	NetmapDevice* _device;

	Task* _task;

	int _queue_begin;
	int _queue_end;

	int _dropped;
	int _count;

	unsigned int backoff;
	unsigned int last_queue; //Last queue used to send packets
	NotifierSignal signal;
	Timer* timer; //Timer for repeateadly empty pull()

	Packet* _queue;

	static String count_handler(Element *e, void *);
	static String dropped_handler(Element *e, void *);

	static int reset_count_handler(const String &, Element *e, void *,
	                                ErrorHandler *);
};

CLICK_ENDDECLS
#endif
