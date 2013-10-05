
/*
 * MARSSx86 : A Full System Computer-Architecture Simulator
 *
 * Permission is hereby granted, free of charge, to any person obtaining a copy
 * of this software and associated documentation files (the "Software"), to deal
 * in the Software without restriction, including without limitation the rights
 * to use, copy, modify, merge, publish, distribute, sublicense, and/or sell
 * copies of the Software, and to permit persons to whom the Software is
 * furnished to do so, subject to the following conditions:
 *
 * The above copyright notice and this permission notice shall be included in
 * all copies or substantial portions of the Software.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
 * IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
 * FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL
 * THE AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
 * LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
 * OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN
 * THE SOFTWARE.
 *
 * Copyright 2009 Avadh Patel <apatel@cs.binghamton.edu>
 * Copyright 2009 Furat Afram <fafram@cs.binghamton.edu>
 *
 */

#ifdef MEM_TEST
#include <test.h>
#else
#include <ptlsim.h>
#define PTLSIM_PUBLIC_ONLY
#include <ptlhwdef.h>
#endif

#include <memoryControllerSimple.h>
#include <memoryHierarchy.h>

#include <machine.h>

using namespace Memory;

MemoryControllerSimple::MemoryControllerSimple(W8 coreid, const char *name,
		MemoryHierarchy *memoryHierarchy) :
	Controller(coreid, name, memoryHierarchy)
    , new_stats(name, &memoryHierarchy->get_machine())
{
    memoryHierarchy_->add_cache_mem_controller(this, true);

    if(!memoryHierarchy_->get_machine().get_option(name, "latency", latency_)) {
        latency_ = 50;
    }

    /* Convert latency from ns to cycles */
    latency_ = ns_to_simcycles(latency_);

    SET_SIGNAL_CB(name, "_Access_Completed", accessCompleted_,
            &MemoryControllerSimple::access_completed_cb);

    SET_SIGNAL_CB(name, "_Wait_Interconnect", waitInterconnect_,
            &MemoryControllerSimple::wait_interconnect_cb);

	bankBits_ = log2(MEM_BANKS);

	foreach(i, MEM_BANKS) {
		banksUsed_[i] = 0;
	}
}

/*
 * @brief: get bank id from input address using
 *         cache line interleaving address mapping
 *         using lower bits for bank id
 *
 * @param: addr - input address of the memory request
 *
 * @return: bank id of input address
 *
 */
int MemoryControllerSimple::get_bank_id(W64 addr)
{
    return lowbits(addr >> 6, bankBits_);
}

void MemoryControllerSimple::register_interconnect(Interconnect *interconnect,
        int type)
{
    switch(type) {
        case INTERCONN_TYPE_UPPER:
            cacheInterconnect_ = interconnect;
            break;
        default:
            assert(0);
    }
}

bool MemoryControllerSimple::handle_interconnect_cb(void *arg)
{
	Message *message = (Message*)arg;

	memdebug("Received message in Memory controller: ", *message, endl);

	if(message->hasData && message->request->get_type() !=
			MEMORY_OP_UPDATE)
		return true;

    if (message->request->get_type() == MEMORY_OP_EVICT) {
        /* We ignore all the evict messages */
        return true;
    }

	/*
	 * if this request is a memory update request then
	 * first check the pending queue and see if we have a
	 * memory update request to same line and if we can merge
	 * those requests then merge them into one request
	 */
	if(message->request->get_type() == MEMORY_OP_UPDATE) {
		MemoryQueueEntry *entry;
		foreach_list_mutable_backwards(pendingRequests_.list(),
				entry, entry_t, nextentry_t) {
			if(entry->request->get_physical_address() ==
					message->request->get_physical_address()) {
				/*
				 * found an request for same line, now if this
				 * request is memory update then merge else
				 * don't merge to maintain the serialization
				 * order
				 */
				if(!entry->inUse && entry->request->get_type() ==
						MEMORY_OP_UPDATE) {
					/*
					 * We can merge the request, so in simulation
					 * we dont have data, so don't do anything
					 */
					return true;
				}
				/*
				 * we can't merge the request, so do normal
				 * simuation by adding the entry to pending request
				 * queue.
				 */
				break;
			}
		}
	}

	MemoryQueueEntry *queueEntry = pendingRequests_.alloc();

	/* if queue is full return false to indicate failure */
	if(queueEntry == NULL) {
		memdebug("Memory queue is full\n");
		return false;
	}

	if(pendingRequests_.isFull()) {
		memoryHierarchy_->set_controller_full(this, true);
	}

	queueEntry->request = message->request;
	queueEntry->source = (Controller*)message->origin;

	queueEntry->request->incRefCounter();
	ADD_HISTORY_ADD(queueEntry->request);

	int bank_no = get_bank_id(message->request->
			get_physical_address());

    assert(queueEntry->inUse == false);

	if(banksUsed_[bank_no] == 0) {
		banksUsed_[bank_no] = 1;
		queueEntry->inUse = true;
		marss_add_event(&accessCompleted_, latency_,
				queueEntry);
	}

	return true;
}

void MemoryControllerSimple::print(ostream& os) const
{
	os << "---Memory-Controller: ", get_name(), endl;
	if(pendingRequests_.count() > 0)
		os << "Queue : ", pendingRequests_, endl;
    os << "banksUsed_: ", banksUsed_, endl;
	os << "---End Memory-Controller: ", get_name(), endl;
}

bool MemoryControllerSimple::access_completed_cb(void *arg)
{
    MemoryQueueEntry *queueEntry = (MemoryQueueEntry*)arg;

    bool kernel = queueEntry->request->is_kernel();

    int bank_no = get_bank_id(queueEntry->request->
            get_physical_address());
    banksUsed_[bank_no] = 0;

    N_STAT_UPDATE(new_stats.bank_access, [bank_no]++, kernel);
    switch(queueEntry->request->get_type()) {
        case MEMORY_OP_READ:
            N_STAT_UPDATE(new_stats.bank_read, [bank_no]++, kernel);
            break;
        case MEMORY_OP_WRITE:
            N_STAT_UPDATE(new_stats.bank_write, [bank_no]++, kernel);
            break;
        case MEMORY_OP_UPDATE:
            N_STAT_UPDATE(new_stats.bank_update, [bank_no]++, kernel);
            break;
        default:
            assert(0);
    }

    /*
     * Now check if we still have pending requests
     * for the same bank
     */
    MemoryQueueEntry* entry;
    foreach_list_mutable(pendingRequests_.list(), entry, entry_t,
            prev_t) {
        int bank_no_2 = get_bank_id(entry->request->
                get_physical_address());
        if(bank_no == bank_no_2 && entry->inUse == false) {
            entry->inUse = true;
            marss_add_event(&accessCompleted_,
                    latency_, entry);
            banksUsed_[bank_no] = 1;
            break;
        }
    }

    if(!queueEntry->annuled) {

        /* Send response back to cache */
        memdebug("Memory access done for Request: ", *queueEntry->request,
                endl);

        wait_interconnect_cb(queueEntry);
    } else {
        queueEntry->request->decRefCounter();
        ADD_HISTORY_REM(queueEntry->request);
        pendingRequests_.free(queueEntry);
    }

    return true;
}

bool MemoryControllerSimple::wait_interconnect_cb(void *arg)
{
	MemoryQueueEntry *queueEntry = (MemoryQueueEntry*)arg;

	bool success = false;

	/* Don't send response if its a memory update request */
	if(queueEntry->request->get_type() == MEMORY_OP_UPDATE) {
		queueEntry->request->decRefCounter();
		ADD_HISTORY_REM(queueEntry->request);
		pendingRequests_.free(queueEntry);
		return true;
	}

	/* First send response of the current request */
	Message& message = *memoryHierarchy_->get_message();
	message.sender = this;
	message.dest = queueEntry->source;
	message.request = queueEntry->request;
	message.hasData = true;

	memdebug("Memory sending message: ", message);
	success = cacheInterconnect_->get_controller_request_signal()->
		emit(&message);
	/* Free the message */
	memoryHierarchy_->free_message(&message);

	if(!success) {
		/* Failed to response to cache, retry after 1 cycle */
		marss_add_event(&waitInterconnect_, 1, queueEntry);
	} else {
		queueEntry->request->decRefCounter();
		ADD_HISTORY_REM(queueEntry->request);
        pendingRequests_.free(queueEntry);

		if(!pendingRequests_.isFull()) {
			memoryHierarchy_->set_controller_full(this, false);
		}
	}
	return true;
}

void MemoryControllerSimple::annul_request(MemoryRequest *request)
{
    MemoryQueueEntry *queueEntry;
    foreach_list_mutable(pendingRequests_.list(), queueEntry,
            entry, nextentry) {
        if(queueEntry->request->is_same(request)) {
            queueEntry->annuled = true;
            if(!queueEntry->inUse) {
                queueEntry->request->decRefCounter();
                ADD_HISTORY_REM(queueEntry->request);
                pendingRequests_.free(queueEntry);
            }
        }
    }
}

int MemoryControllerSimple::get_no_pending_request(W8 coreid)
{
	int count = 0;
	MemoryQueueEntry *queueEntry;
	foreach_list_mutable(pendingRequests_.list(), queueEntry,
			entry, nextentry) {
		if(queueEntry->request->get_coreid() == coreid)
			count++;
	}
	return count;
}

/**
 * @brief Dump Memory Controller in YAML Format
 *
 * @param out YAML Object
 */
void MemoryControllerSimple::dump_configuration(YAML::Emitter &out) const
{
	out << YAML::Key << get_name() << YAML::Value << YAML::BeginMap;

	YAML_KEY_VAL(out, "type", "dram_cont");
	YAML_KEY_VAL(out, "RAM_size", ram_size); /* ram_size is from QEMU */
	YAML_KEY_VAL(out, "number_of_banks", MEM_BANKS);
	YAML_KEY_VAL(out, "latency", latency_);
	YAML_KEY_VAL(out, "latency_ns", simcycles_to_ns(latency_));
	YAML_KEY_VAL(out, "pending_queue_size", pendingRequests_.size());

	out << YAML::EndMap;
}

/* Memory Controller Builder */
struct MemoryControllerSimpleBuilder : public ControllerBuilder
{
    MemoryControllerSimpleBuilder(const char* name) :
        ControllerBuilder(name)
    {}

    Controller* get_new_controller(W8 coreid, W8 type,
            MemoryHierarchy& mem, const char *name) {
        return new MemoryControllerSimple(coreid, name, &mem);
    }
};

MemoryControllerSimpleBuilder memControllerBuilder("simple_dram_module");
