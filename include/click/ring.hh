// -*- c-basic-offset: 4 -*-
#ifndef CLICK_RING_HH
#define CLICK_RING_HH

#include <click/atomic.hh>
#include <click/sync.hh>

CLICK_DECLS

template <typename T, size_t RING_SIZE> class Ring {

protected:

	inline bool has_space() {
		return head - tail < RING_SIZE;
	}

	inline bool is_empty() {
		return head == tail;
	}

public:
	int id;
	Ring() {
		head = 0;
		tail = 0;
	}

	inline T extract() {
		if (!is_empty()) {
			T &v = ring[tail % RING_SIZE];
			tail++;
			return v;
		} else
			return 0;
	}

	inline bool insert(T batch) {
		if (has_space()) {
			ring[head % RING_SIZE] = batch;
			head++;
			return true;
		} else
			return false;
	}

	inline unsigned int count() {
		return head - tail;
	}

	T ring[RING_SIZE];
	uint32_t head;
	uint32_t tail;
};

template <typename T, size_t RING_SIZE> class MTRing : public Ring<T, RING_SIZE> {
	Spinlock _lock;


public:
	inline void acquire() {
		_lock.acquire();
	}

	inline void release() {
		_lock.release();
	}

	inline void release_tail() {
		release();
	}

	inline void release_head() {
		release();
	}

	inline void acquire_tail() {
		acquire();
	}

	inline void acquire_head() {
		acquire();
	}

public:

	MTRing() : _lock() {

	}

	inline bool insert(T batch) {
		acquire_head();
		if (Ring<T,RING_SIZE>::has_space()) {
			Ring<T,RING_SIZE>::ring[Ring<T,RING_SIZE>::head % RING_SIZE] = batch;
			Ring<T,RING_SIZE>::head++;
			release_head();
			return true;
		} else {
			release_head();
			return false;
		}
	}

	inline T extract() {
		acquire_tail();
		if (!Ring<T,RING_SIZE>::is_empty()) {
			T &v = Ring<T,RING_SIZE>::ring[Ring<T,RING_SIZE>::tail % RING_SIZE];
			Ring<T,RING_SIZE>::tail++;
			release_tail();
			return v;
		} else {
			release_tail();
			return 0;
		}
	}
};

CLICK_ENDDECLS
#endif
