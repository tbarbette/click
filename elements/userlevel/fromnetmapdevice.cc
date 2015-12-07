// -*- c-basic-offset: 4; related-file-name: "fromnetmapdevice.hh" -*-
/*
 * fromnetmapdevice.{cc,hh} -- element reads packets live from network via
 * Netmap.
 *
 * Copyright (c) 2014-2015 University of Liège
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
#include <click/master.hh>
#include <click/error.hh>
#include <click/standard/scheduleinfo.hh>
#include <click/packet.hh>
#include <click/packet_anno.hh>

#include "fromnetmapdevice.hh"

CLICK_DECLS

FromNetmapDevice::FromNetmapDevice() : _device(NULL), _promisc(1),_blockant(false), _count(0), _burst(32), _task(0)
{
	NetmapDevice::global_alloc += 2048;
}

void *
FromNetmapDevice::cast(const char *n)
{
	if (strcmp(n, "FromNetmapDevice") == 0)
		return (Element *)this;
	return NULL;
}

int
FromNetmapDevice::configure(Vector<String> &conf, ErrorHandler *errh)
{
	String flow,ifname;
	int queue = -1;
	int nr_queue = 1;

	if (Args(conf, this, errh)
			.read_mp("DEVNAME", ifname)
			.read_p("QUEUE", queue)
			.read_p("NR_QUEUE", nr_queue)
			.read("PROMISC", _promisc)
			.read("BURST", _burst)
			.complete() < 0)
		return -1;
	const char* device = ifname.c_str();

	_device = NetmapDevice::open(ifname);
	if (!_device) {
		return errh->error("Could not initialize %s",ifname.c_str());
	}

	if (queue < 0)
		queue = 0;

	_queue_begin = queue;

	if (nr_queue <= 0)
		_queue_end = _device->parent_nmd->nifp->ni_rx_rings - 1;
	else
		_queue_end = _queue_begin + nr_queue - 1;

	if (_queue_end >= _device->parent_nmd->nifp->ni_rx_rings)
		return errh->error("Last queue %d is out of bound ! Please fix QUEUE and NR_QUEUE parameters !", _queue_end);

	return 0;
}


int
FromNetmapDevice::initialize(ErrorHandler *errh)
{
	int thread_id = router()->home_thread_id(this);

    _task = new Task(this);
    ScheduleInfo::initialize_task(this, _task, false, errh);
    _task->move_thread(thread_id);

    for (int i = _queue_begin; i <= _queue_end; i++) {
		master()->thread(thread_id)->select_set().add_select(_device->nmds[i]->fd,this,SELECT_READ);
	}

	return 0;
}

inline bool
FromNetmapDevice::receive_packets(bool fromtask) {
	unsigned nr_pending = 0;

	int sent = 0;

	for (int i = _queue_begin; i <= _queue_end; i++) {
		struct nm_desc* nmd = _device->nmds[i];

		struct netmap_ring *rxring = NETMAP_RXRING(nmd->nifp, i);

		u_int cur, n;

		cur = rxring->cur;

		n = nm_ring_space(rxring);
		if (_burst && n > _burst) {
			nr_pending += n - _burst;
			n = _burst;
		}

		if (n == 0) {
			continue;
		}

		Timestamp ts = Timestamp::make_usec(nmd->hdr.ts.tv_sec, nmd->hdr.ts.tv_usec);

		sent+=n;

		while (n > 0) {
			struct netmap_slot* slot = &rxring->slot[cur];

			unsigned char* data = (unsigned char*)NETMAP_BUF(rxring, slot->buf_idx);
			WritablePacket *p;
			uint32_t new_buf_idx;

			if ((new_buf_idx = NetmapBufQ::local_pool()->extract()) > 0) {
				__builtin_prefetch(data);
				p = Packet::make( data, slot->len, NetmapBufQ::buffer_destructor,NetmapBufQ::local_pool());
				if (!p) goto error;
				slot->buf_idx = new_buf_idx;
			} else {
				p = Packet::make(data, slot->len);
				if (!p) goto error;
			}
			p->set_packet_type_anno(Packet::HOST);

			p->set_timestamp_anno(ts);

			output(0).push(p);

			slot->flags |= NS_BUF_CHANGED;

			cur = nm_ring_next(rxring, cur);
			n--;
		}

		rxring->head = rxring->cur = cur;
	}

	if (nr_pending > _burst) {
		if (fromtask) {
			_task->fast_reschedule();
		} else {
			_task->reschedule();
		}
	}

	_count += sent;
	return sent;
	error: //No more buffer

	click_chatter("No more buffers !");
	router()->master()->kill_router(router());
	return 0;
}

void
FromNetmapDevice::selected(int fd, int)
{
	receive_packets(false);
}

void
FromNetmapDevice::cleanup(CleanupStage)
{
	if (_task)
		delete _task;

	if (_device)
		_device->destroy();
}

bool
FromNetmapDevice::run_task(Task* t)
{
	return receive_packets(true);
}


String FromNetmapDevice::count_handler(Element *e, void *)
{
    FromNetmapDevice *fnd = static_cast<FromNetmapDevice *>(e);
    return String(fnd->_count);
}

int FromNetmapDevice::reset_count_handler(const String &, Element *e, void *,
                                ErrorHandler *)
{
    FromNetmapDevice *fnd = static_cast<FromNetmapDevice *>(e);
    fnd->_count = 0;
    return 0;
}

void FromNetmapDevice::add_handlers()
{
	add_read_handler("count", count_handler, 0);
	add_write_handler("reset_counts", reset_count_handler, 0, Handler::BUTTON);
}



CLICK_ENDDECLS
ELEMENT_REQUIRES(userlevel netmap)
EXPORT_ELEMENT(FromNetmapDevice)
ELEMENT_MT_SAFE(FromNetmapDevice)
