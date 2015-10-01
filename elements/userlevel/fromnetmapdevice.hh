// -*- c-basic-offset: 4 -*-
#ifndef CLICK_FROMNETMAPDEVICE_HH
#define CLICK_FROMNETMAPDEVICE_HH
#include <click/config.h>
#include <click/task.hh>
#include <click/etheraddress.hh>
#include <click/netmapdevice.hh>
#include <click/vector.hh>

CLICK_DECLS

/*
 *
 * =c
 *
 * FromNetmapDevice(DEVNAME [, QUEUE, NR_QUEUE, [, I<keywords> PROMISC, BURST])
 *
 * =s netdevices
 *
 * =item DEVNAME
 *
 * String.  Device number
 *
 * =item QUEUE
 *
 * Integer.  Index of the first hardware queue to use. Default to the first
 * available queue (generally, 0).
 *
 * =item NR_QUEUE
 *
 * Integer. Number of queue to use starting from QUEUE. If negative or zero,
 * all available queue starting from QUEUE will be used.
 *
 * =item PROMISC
 *
 * Boolean.  FromNetmapDevice puts the device in promiscuous mode if PROMISC is
 * true. The default is false.
 *
 * =item BURST
 *
 * Unsigned integer. Maximal number of packets that will be processed before
 *  rescheduling Click default is 32.
 *
 */

class FromNetmapDevice: public Element {

public:

    FromNetmapDevice() CLICK_COLD;

    void selected(int, int);

    const char *class_name() const		{ return "FromNetmapDevice"; }
    const char *port_count() const		{ return PORTS_0_1; }
    const char *processing() const		{ return PUSH; }

    int configure_phase() const			{ return CONFIGURE_PHASE_PRIVILEGED - 5; }
    void* cast(const char*);


    int configure(Vector<String>&, ErrorHandler*) CLICK_COLD;
    int initialize(ErrorHandler*) CLICK_COLD;
    void cleanup(CleanupStage) CLICK_COLD;

    inline bool receive_packets(bool fromtask);

    bool run_task(Task *);

    int _count;

  protected:

    NetmapDevice* _device;
    bool _promisc;
    bool _blockant;
    unsigned int _burst;
    int _queue_begin;
    int _queue_end;
    Task* _task;

    static String count_handler(Element *e, void *);
    static int reset_count_handler(const String &, Element *e, void *,
                                    ErrorHandler *);

    void add_handlers();
};

CLICK_ENDDECLS
#endif
