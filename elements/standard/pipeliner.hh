// -*- c-basic-offset: 4 -*-
#ifndef CLICK_PIPELINER_HH
#define CLICK_PIPELINER_HH

#include <click/element.hh>
#include <click/task.hh>
#include <click/ring.hh>
#include <click/multithread.hh>

CLICK_DECLS

/*
=c

Pipeliner

Fast version of ThreadSafeQueue->Unqueue, allowing to offload processing
of packets pushed to this element to another one, without the inherent 
scheduling cost of normal queues. Multiple thread can push packets to
this queue, and the home thread of this element will push packet out.
*/



class Pipeliner: public Element {

public:

    Pipeliner();
    ~Pipeliner();

    const char *class_name() const      { return "Pipeliner"; }
    const char *port_count() const      { return "1-/1"; }
    const char *processing() const      { return PUSH; }

    int configure(Vector<String>&, ErrorHandler*) CLICK_COLD;

    int initialize(ErrorHandler*) CLICK_COLD;

    void cleanup(CleanupStage);

    void push(int,Packet*);

    bool run_task(Task *);

    unsigned long int n_dropped() {
        unsigned long int total = 0;
        for (unsigned i = 0; i < stats.size(); i++)
            total += stats.get_value_for_thread(i).dropped;
        return total;
    }

    unsigned long int n_count() {
        unsigned long int total = 0;
        for (unsigned i = 0; i < stats.size(); i++)
            total += stats.get_value_for_thread(i).count;
        return total;
    }

    static String dropped_handler(Element *e, void *)
    {
        Pipeliner *p = static_cast<Pipeliner *>(e);
        return String(p->n_dropped());
    }

    static String count_handler(Element *e, void *)
    {
        Pipeliner *p = static_cast<Pipeliner *>(e);
        return String(p->n_count());
    }

    void add_handlers() CLICK_COLD;

    int _ring_size;
    bool _block;

    typedef DynamicRing<Packet*> PacketRing;

    per_thread<PacketRing> storage;
    struct stats {
        stats() : dropped(0), count(0) {

        }
        unsigned long int dropped;
        unsigned long int count;
    };
    per_thread<struct stats> stats;
    volatile int sleepiness;
    int _sleep_threshold;

  protected:
    Task _task;
    unsigned int last_start;
};

CLICK_ENDDECLS
#endif
