// -*- c-basic-offset: 4; related-file-name: "ToNetmapDevice.hh" -*-
/*
 * ToNetmapDevice.{cc,hh} -- element reads packets live from network via
 * Netamap
 *
 * Copyright (c) 2014-2015 University of Liège
 * Copyright (c) 2014 Cyril Soldani
 * Copyright (c) 2015 Tom Barbette
 *
 * Permission is hereby granted, free of charge, to any person obtaining a
 * copy of this software and associated documentation files (the "Software"),
 * to deal in the Software without restriction, subject to the conditions
 * listed in the Click LICENSE file. These conditions include: you must
 * preserve this copyright notice, and you cannot mention the copyright
 * holders in advertising related to the Software without their permission.
 * The Software is provided WITHOUT ANY WARRANTY, EXPRESS OR IMPLIED. This
 * notice is a summary of the Click LICENSE file; the license in that file is
 * legally binding.
 */



#include <click/config.h>
#include <click/args.hh>
#include <click/error.hh>
#include <click/standard/scheduleinfo.hh>
#include <click/packet_anno.hh>
#include <click/master.hh>
#include "tonetmapdevice.hh"

CLICK_DECLS

ToNetmapDevice::ToNetmapDevice() : _burst(32),_block(1),_internal_queue(512), _dropped(0), _count(0), backoff(0), last_queue(0), timer()
{

}


int
ToNetmapDevice::configure(Vector<String> &conf, ErrorHandler *errh)
{
	String ifname;
	int burst = -1;
	int queue = -1;

	if (Args(conf, this, errh)
			.read_mp("DEVNAME", ifname)
			.read_p("QUEUE", queue)
			.read("IQUEUE",_internal_queue)
			.read("BLOCKING",_block)
			.read("BURST", burst)
			.complete() < 0)
		return -1;

	if (_internal_queue < _burst * 2) {
		return errh->error("IQUEUE (%d) must be at least twice the size of BURST (%d)!",_internal_queue, _burst);
	}

	if (burst > 0)
		_burst = burst;

	_device = NetmapDevice::open(ifname);
	if (!_device) {
		return errh->error("Could not initialize %s",ifname.c_str());
	}

	if (queue < 0) {
		_queue_begin = 0;
		_queue_end = _device->parent_nmd->nifp->ni_tx_rings - 1;
	} else {
		_queue_begin = queue;
		_queue_end = queue;
	}

	if (_burst > _device->some_nmd->some_ring->num_slots / 2) {
		errh->warning("BURST value larger than half the ring size (%d) is not recommended. Please set BURST to %d or less",_burst, _device->some_nmd->some_ring->num_slots,_device->some_nmd->some_ring->num_slots/2);
	}

	return 0;
}

int ToNetmapDevice::initialize(ErrorHandler *errh)
{
	int thread_id = router()->home_thread_id(this);
	int nqueues = _queue_end - _queue_begin + 1;

	_task = new Task(this);
	ScheduleInfo::initialize_task(this, _task, false, errh);
	_task->move_thread(thread_id);

	/*Do not use select mechanism in push mode, we'll use the rings to absorb
		transient traffic, the iqueue if it's not enough, and block or drop if
		even the iqueue is full*/
	if (input_is_pull(0)) {
		for (int i = _queue_begin; i <= _queue_end; i++) {
			master()->thread(thread_id)->select_set().add_select(_device->nmds[i]->fd,this,SELECT_WRITE);
		}

		_queue = 0;
		signal = (Notifier::upstream_empty_signal(this, 0, _task));
		timer = new Timer(_task);
		timer->initialize(this);
		backoff = 1;
	} else {
		_pushtimer = new Timer(this);
		_pushtimer->initialize(this,false);
		_iodone.resize(nqueues);
		for (int i = 0; i < nqueues; i++) {
			_iodone[i] = false;
		}
	}
	_state.resize(click_max_cpu_ids());

	return 0;
}

void
ToNetmapDevice::selected(int fd, int)
{
	_task->reschedule();
	master()->thread(click_current_cpu_id())->select_set().remove_select(fd,this,SELECT_WRITE);
}

/**
 * Will do a full ring synchronization
 */
inline void ToNetmapDevice::do_txsync(int fd) {
	ioctl(fd,NIOCTXSYNC,0);
}

/**
 * Set down all IODONE flag for each queue, allowing subsequent try_txsync to
 * perform a ring synchronization
 */
inline void ToNetmapDevice::allow_txsync() {
	for (int i = _queue_begin; i <= _queue_end; i++)
		_iodone[i] = false;
}

/**
 * Will do a full ring synchronization if the IODONE flag is down, then raise
 * the flag.
 */
inline void ToNetmapDevice::try_txsync(int queue, int fd) {
	if (!_iodone[queue]) {
		do_txsync(fd);
		_iodone[queue] = true;
	}
}

void
ToNetmapDevice::push(int, Packet* p) {
	State &s = _state[click_current_cpu_id()];

	if (s.q == NULL) {
		//If the internal queue is empty, add this packet in the queue
		s.q = p;
		p->set_prev(p);
		p->set_next(NULL);
		s.q_size = 1;
	} else {
		if (s.q_size < _internal_queue) { //Append packet at the end while there is space
			s.q->prev()->set_next(p);
			s.q->set_prev(p);
			s.q_size++;
		} else {
			if (!_block) {
				_dropped++;
				p->kill();
			}
		}
	}

	if (s.q_size >= _burst) {
		do_send:
		Packet* last = s.q->prev();

		s.q->prev()->set_next(NULL);

		_lock.acquire();
		unsigned int sent = send_packets(s.q,true);
		_lock.release();

		if (sent > 0 && s.q)
			s.q->set_prev(last);

		s.q_size -= sent;

		if (s.q && backoff < 128) {
			backoff++;

			if (!_pushtimer->scheduled()) {
				_pushtimer->schedule_after(Timestamp::make_usec(1));
			}
		} else {
			//If we backed off a lot, we may try to do a sync before waiting for the timer to trigger
			//or if we could send everything we remove the backoff and allow sync too
			backoff = 0;
			allow_txsync();
		}

		if (s.q && s.q_size >= _internal_queue && _block) {
			allow_txsync();
			goto do_send;
		}
	}
}

/*Timer for push mode. It will raise the IODONE flag when running to allow a new
 * full synchronization of the ring to be done.*/
void
ToNetmapDevice::run_timer(Timer *) {
	allow_txsync();
}

int complaint = 0;

/**
 * Send a linked list of packet, return the number of packet sent and the head
 * 	points toward the packets following the last sent packet (could be null)
 *
 * @return The number of packets sent
 */
inline unsigned int ToNetmapDevice::send_packets(Packet* &head, bool push) {
	struct nm_desc* nmd;
	struct netmap_ring *txring;
	struct netmap_slot *slot;

	WritablePacket* s_head = head->uniqueify();
	WritablePacket* next = s_head;
	WritablePacket* p = next;

	bool dosync = false;
	unsigned int sent = 0;
	int nqueues = _queue_end - _queue_begin + 1;

	//For all queue, but start at the last one we used
	for (int iloop = 0; iloop < nqueues; iloop++) {
		int in = (last_queue + iloop) % nqueues;
		int i =  _queue_begin + in;

		nmd = _device->nmds[i];
		txring = NETMAP_TXRING(nmd->nifp, i);

		if (nm_ring_empty(txring)) {
			if (push)
				try_txsync(i,nmd->fd);
			continue;
		}

		u_int cur = txring->cur;
		while ((cur != txring->tail) && next) {
			p = next;

			next = static_cast<WritablePacket*>(p->next());

			slot = &txring->slot[cur];
			slot->len = p->length();
			if (likely(NetmapBufQ::is_netmap_packet(p))) {
				NetmapBufQ::local_pool()->insert(slot->buf_idx);
				slot->buf_idx = NETMAP_BUF_IDX(txring,p->buffer());
				slot->flags |= NS_BUF_CHANGED;
				p->set_buffer_destructor(Packet::empty_destructor);
			} else {
				unsigned char* dstdata = (unsigned char*)NETMAP_BUF(txring, slot->buf_idx);
				void* srcdata = (void*)(p->data());
				memcpy(dstdata,srcdata,p->length());
			}
			p->kill();
			sent++;
			cur = nm_ring_next(txring,cur);
		}
		txring->head = txring->cur = cur;

		if (unlikely(dosync))
			do_txsync(nmd->fd);

		if (next == NULL) { //All is sent
			_count += sent;
			head = NULL;
			return sent;
		}
	}

	if (next == s_head) { //Nothing could be sent...
		head = s_head;
		return 0;
	} else {
		_count += sent;
		head = next;
		return sent;
	}
}

/**
 * Task used for pull mode only.
 */
bool
ToNetmapDevice::run_task(Task* task)
{
	unsigned int total_sent = 0;
	State &s = _state[click_current_cpu_id()];

	Packet* batch = s.q;
	unsigned batch_size = s.q_size;
	s.q = NULL;
	s.q_size = 0;
	do {
		/* Difference from normal ToDevice is that we build a batch up to _burst
		 * size and then process it. This allows to synchronize less often,
		 * which is costly with netmap. */
		if (!batch || batch_size < _burst) { //Create a batch up to _burst size, or less if no packets are available
			Packet* last;
			if (!batch) {
				//Nothing in the internal queue
				if ((batch = input(0).pull()) == NULL) {
					//Nothing to pull
					break;
				}
				last = batch;
				batch_size = 1;
			} else {
				last = batch->prev();
			}

			Packet* p;
			//Pull packets until we reach burst or there is nothing to pull
			while (batch_size < _burst && (p = input(0).pull()) != NULL) {
				last->set_next(p);
				last = p;
				batch_size++;
			}
			batch->set_prev(last); //Prev of the head is the tail of the batch
		}

		if (batch) {
			Packet* last = batch->prev();
			last->set_next(NULL); //Just to be sure

			unsigned int sent = send_packets(batch,false);

			total_sent += sent;
			batch_size -= sent;

			if (sent > 0) { //At least one packet sent
				if (batch) //Reestablish the tail if we could not send everything
					batch->set_prev(last);
			} else
				break;
		} else //No packet to send
			break;
	} while (1);

	if (batch != NULL) {/*Output ring is full, we rely on the select mechanism
		to know when we'll have space to send packets*/
		backoff = 1;
		s.q = batch;
		s.q_size = batch_size;
		//Register fd to wait for space
		for (int i = _queue_begin; i <= _queue_end; i++) {
			master()->thread(click_current_cpu_id())->select_set().add_select(_device->nmds[i]->fd,this,SELECT_WRITE);
		}
	} else if (signal.active()) { //TODO is this really needed?
		//We sent everything we could, but check signal to see if packet arrived after last read
		task->fast_reschedule();
	} else { //Empty and no signal
		if (backoff < 256)
			backoff*=2;
		timer->schedule_after(Timestamp::make_usec(backoff));
	}
	return total_sent;
}


void
ToNetmapDevice::cleanup(CleanupStage)
{
	if (_task)
		delete _task;

	for (int i = 0; i < _state.size(); i++) {
		while (_state[i].q) {
			Packet* next = _state[i].q->next();
			_state[i].q->kill();
			_state[i].q = next;
		}
	}

	if (timer)
		delete timer;

	if (!input_is_pull(0) && _pushtimer)
		delete _pushtimer;

	if (_device) _device->destroy();
}

String ToNetmapDevice::count_handler(Element *e, void *)
{
    ToNetmapDevice *tnd = static_cast<ToNetmapDevice *>(e);
    return String(tnd->_count);
}

String ToNetmapDevice::dropped_handler(Element *e, void *)
{
	ToNetmapDevice *tnd = static_cast<ToNetmapDevice *>(e);
    return String(tnd->_dropped);
}


int ToNetmapDevice::reset_count_handler(const String &, Element *e, void *,
                                ErrorHandler *)
{
	ToNetmapDevice *tnd = static_cast<ToNetmapDevice *>(e);
    tnd->_count = 0;
    tnd->_dropped = 0;
    return 0;
}

void
ToNetmapDevice::add_handlers()
{
	add_read_handler("n_sent", count_handler, 0);
	add_read_handler("n_dropped", dropped_handler, 0);
	add_write_handler("reset_counts", reset_count_handler, 0, Handler::BUTTON);
}

CLICK_ENDDECLS
ELEMENT_REQUIRES(userlevel netmap)
EXPORT_ELEMENT(ToNetmapDevice)
