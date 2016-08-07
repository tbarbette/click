// -*- c-basic-offset: 4 -*-
#ifndef CLICK_RING_HH
#define CLICK_RING_HH

CLICK_DECLS

/**
 * Ring of object with size set at initialization time
 * This ring single-producer, single-consumer, but they may be two different threads.
 */
template <typename T> class DynamicRing {

protected:
    uint32_t _size;

    inline uint32_t next_i(uint32_t i) {
        if (i == _size -1)
            return 0;
        else
            return i + 1;
    }

    inline void inc_i(uint32_t &i) {
        if (i == _size -1 )
            i = 0;
        else
            i++;
    }

    T* ring;

public:
    uint32_t head;
    uint32_t tail;

    DynamicRing() : _size(0),ring(0) {
        head = 0;
        tail = 0;
    }

    ~DynamicRing() {
        if (_size)
            delete[] ring;
    }


    inline T extract() {
        if (!is_empty()) {
            T &v = ring[tail];
            inc_i(tail);
            return v;
        } else
            return 0;
    }

    inline bool insert(T v) {
        if (!is_full()) {
            ring[head] = v;
            inc_i(head);
            return true;
        } else
            return false;
    }

    inline unsigned int count() {
        int count = (int)head - (int)tail;
        if (count < 0)
            count += _size;
        return count;
    }

    inline bool is_empty() {
        return head == tail;
    }

    inline bool is_full() {
        return next_i(head) == tail;
    }

    void initialize(int size) {
        _size = size;
        ring = new T[size];
    }
};

CLICK_ENDDECLS
#endif
