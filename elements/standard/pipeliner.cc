// -*- c-basic-offset: 4; related-file-name: "pipeliner.hh" -*-
/*
 * pipeliner.{cc,hh} --
 */

#include <click/config.h>
#include "pipeliner.hh"
#include <click/standard/scheduleinfo.hh>
#include <click/ring.hh>
#include <click/args.hh>

CLICK_DECLS


#define PS_MIN_THRESHOLD 2048

Pipeliner::Pipeliner()
    :   _ring_size(-1),_block(false),sleepiness(0),_task(this),_sleep_threshold(0) {

}

Pipeliner::~Pipeliner()
{

}

void Pipeliner::cleanup(CleanupStage) {
    for (unsigned i = 0; i < storage.size(); i++) {
        Packet* p;
        while ((p = storage.get_value_for_thread(i).extract()) != 0) {
                p->kill();
        }
    }
}


int
Pipeliner::configure(Vector<String> & conf, ErrorHandler * errh)
{
    if (Args(conf, this, errh)
    .read_p("CAPACITY", _ring_size)
    .read_p("BLOCKING", _block)
    .complete() < 0)
        return -1;

    if (_ring_size == -1) {
        _ring_size = 1024;
    }

    //Amount of empty run of task after which it unschedule
    _sleep_threshold = _ring_size / 4;

    return 0;
}


int
Pipeliner::initialize(ErrorHandler *errh)
{
    for (unsigned i = 0; i < storage.size(); i++) {
        storage.get_value_for_thread(i).initialize(_ring_size);
    }

    ScheduleInfo::initialize_task(this, &_task, false, errh);

    return 0;
}

void Pipeliner::push(int,Packet* p) {
    retry:
    if (storage->insert(p)) {
        stats->count++;
    } else {
        if (_block) {
            if (sleepiness >= _sleep_threshold)
                _task.reschedule();
            goto retry;
        }
        p->kill();
        stats->dropped++;
        if (stats->dropped < 10 || stats->dropped % 100 == 1)
            click_chatter("%s : Dropped %d packets : have %d packets in ring", name().c_str(), stats->dropped, storage->count());
    }
    if (sleepiness >= _sleep_threshold)
        _task.reschedule();
}

bool
Pipeliner::run_task(Task* t)
{
    bool r = false;
    for (unsigned i = 0; i < storage.size(); i++) {
        PacketRing& s = storage.get_value_for_thread(i);
        while (!s.is_empty()) {
            Packet* p = s.extract();
            output(0).push(p);
            r = true;
        }
    }
    if (!r) {
        sleepiness++;
        if (sleepiness < _sleep_threshold) {
            t->fast_reschedule();
        }
    } else {
        sleepiness = 0;
        t->fast_reschedule();
    }
    return r;

}

void Pipeliner::add_handlers()
{
    add_read_handler("dropped", dropped_handler, 0);
    add_read_handler("count", count_handler, 0);
}

CLICK_ENDDECLS

ELEMENT_REQUIRES(userlevel)
EXPORT_ELEMENT(Pipeliner)
ELEMENT_MT_SAFE(Pipeliner)
