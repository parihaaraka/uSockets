/*
 * Adapted for libev by AI, based on libuv backend by Alex Hultman.
 * Original Intellectual property of third-party.

 * Licensed under the Apache License, Version 2.0 (the "License");
 * you may not use this file except in compliance with the License.
 * You may obtain a copy of the License at

 *     http://www.apache.org/licenses/LICENSE-2.0

 * Unless required by applicable law or agreed to in writing, software
 * distributed under the License is distributed on an "AS IS" BASIS,
 * WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 * See the License for the specific language governing permissions and
 * limitations under the License.
 */

#include "libusockets.h" // This should declare the us_ functions
#include "internal/internal.h" // uSockets internal functions
#include <stdlib.h>
#include <stdio.h> // For potential debugging

#ifdef LIBUS_USE_LIBEV // Guard for this backend

// Callback for I/O events (sockets)
static void poll_cb_ev(struct ev_loop *loop, ev_io *w, int revents) {
    struct us_poll_t *p = (struct us_poll_t *)w->data; // data should point to us_poll_t
    // libev doesn't give a separate status. An error usually means revents & EV_ERROR.
    // Or, the read/write on the fd will fail.
    // us_internal_dispatch_ready_poll expects an error flag.
    int error = (revents & EV_ERROR) ? 1 : 0;
    us_internal_dispatch_ready_poll(p, error, revents & (EV_READ | EV_WRITE));
}

// Callback for prepare watcher (before I/O polling)
static void prepare_cb_ev(struct ev_loop *loop_ptr, ev_prepare *w, int revents) {
    struct us_loop_t *loop = (struct us_loop_t *)w->data; // data should point to us_loop_t
    us_internal_loop_pre(loop);
}

// Callback for check watcher (after I/O polling)
static void check_cb_ev(struct ev_loop *loop_ptr, ev_check *w, int revents) {
    struct us_loop_t *loop = (struct us_loop_t *)w->data; // data should point to us_loop_t
    us_internal_loop_post(loop);
}

// Callback for timers
static void timer_cb_ev(struct ev_loop *loop, ev_timer *w, int revents) {
    struct us_internal_callback_t *cb = (struct us_internal_callback_t *)w->data;
    cb->cb(cb);
}

// Callback for async watchers
static void async_cb_ev(struct ev_loop *loop_ptr, ev_async *w, int revents) {
    struct us_internal_callback_t *cb = (struct us_internal_callback_t *)w->data;
    // internal asyncs give their loop, not themselves, to the callback
    cb->cb((struct us_internal_callback_t *) cb->loop);
}

// Poll functions
void us_poll_init(struct us_poll_t *p, LIBUS_SOCKET_DESCRIPTOR fd, int poll_type) {
    p->poll_type = poll_type;
    // ev_io_init prepares the watcher but doesn't start it.
    // The fd and events are set in ev_io_set before starting.
    ev_io_init(p->io, poll_cb_ev, fd, 0); // Initially no events
    p->io->data = p; // Link back to the us_poll_t structure
}

// Freeing a poll structure. With libev, watchers are stopped synchronously.
void us_poll_free(struct us_poll_t *p, struct us_loop_t *loop) {
    // Ensure the watcher is stopped if it's active.
    // If us_poll_stop was called, it's already stopped.
    if (ev_is_active(p->io)) {
        ev_io_stop(loop->ev_loop, p->io);
    }
    free(p->io);
    free(p);
}

void us_poll_start(struct us_poll_t *p, struct us_loop_t *loop, int events) {
    // Update uSockets internal poll_type tracking
    p->poll_type = us_internal_poll_type(p) |
                   ((events & LIBUS_SOCKET_READABLE) ? POLL_TYPE_POLLING_IN : 0) |
                   ((events & LIBUS_SOCKET_WRITABLE) ? POLL_TYPE_POLLING_OUT : 0);

    // Set the file descriptor and events for the libev watcher
    ev_io_set(p->io, us_poll_fd(p), events);
    ev_io_start(loop->ev_loop, p->io);
}

void us_poll_change(struct us_poll_t *p, struct us_loop_t *loop, int events) {
    if (us_poll_events(p) != events) {
        // Update uSockets internal poll_type tracking
        p->poll_type = us_internal_poll_type(p) |
                       ((events & LIBUS_SOCKET_READABLE) ? POLL_TYPE_POLLING_IN : 0) |
                       ((events & LIBUS_SOCKET_WRITABLE) ? POLL_TYPE_POLLING_OUT : 0);

        // Stop if active, change events, then restart.
        // libev recommends stop -> set -> start for changing fd or events on active watcher.
        if (ev_is_active(p->io)) {
            ev_io_stop(loop->ev_loop, p->io);
        }
        ev_io_set(p->io, us_poll_fd(p), events);
        ev_io_start(loop->ev_loop, p->io);
    }
}

void us_poll_stop(struct us_poll_t *p, struct us_loop_t *loop) {
    // Clear polling flags in uSockets internal state
    p->poll_type &= ~(POLL_TYPE_POLLING_IN | POLL_TYPE_POLLING_OUT);
    p->io->data = 0;
    if (ev_is_active(p->io)) {
        ev_io_stop(loop->ev_loop, p->io);
    }
}

int us_poll_events(struct us_poll_t *p) {
    return ((p->poll_type & POLL_TYPE_POLLING_IN) ? LIBUS_SOCKET_READABLE : 0) |
           ((p->poll_type & POLL_TYPE_POLLING_OUT) ? LIBUS_SOCKET_WRITABLE : 0);
}

// These are uSockets internal poll type helpers, should remain similar
unsigned int us_internal_accept_poll_event(struct us_poll_t *p) {
    // This function seems to be specific to how uSockets handles accept events.
    // In libuv, it returned 0. For libev, it likely remains 0 unless there's
    // a specific libev nuance for accept behavior that uSockets leverages.
    // Assuming it's about event flags for now.
    return 0;
}

int us_internal_poll_type(struct us_poll_t *p) {
    return p->poll_type & 3; // Keep existing logic
}

void us_internal_poll_set_type(struct us_poll_t *p, int poll_type) {
    p->poll_type = poll_type | (p->poll_type & 12); // Keep existing logic
}

LIBUS_SOCKET_DESCRIPTOR us_poll_fd(struct us_poll_t *p) {
    // In libev, the fd is stored in the ev_io watcher
    return p->io->fd;
}

struct us_loop_t *us_create_loop(void *hint, void (*wakeup_cb)(struct us_loop_t *loop),
                                 void (*pre_cb)(struct us_loop_t *loop),
                                 void (*post_cb)(struct us_loop_t *loop), unsigned int ext_size) {
    struct us_loop_t *loop = (struct us_loop_t *)malloc(sizeof(struct us_loop_t) + ext_size);
    if (!loop) return NULL;

    loop->is_default = (hint != 0);
    loop->ev_loop = hint ? (struct ev_loop *)hint : ev_loop_new(EVFLAG_AUTO); // or other flags like EVFLAG_NOSIGFD etc.
    if (!loop->ev_loop) {
        free(loop);
        return NULL;
    }

    // Initialize and start prepare watcher
    ev_prepare_init(&loop->ev_pre, prepare_cb_ev);
    loop->ev_pre.data = loop;
    ev_prepare_start(loop->ev_loop, &loop->ev_pre);
    ev_unref(loop->ev_loop); // Don't let prepare watcher keep loop alive alone

    // Initialize and start check watcher
    ev_check_init(&loop->ev_check, check_cb_ev);
    loop->ev_check.data = loop;
    ev_check_start(loop->ev_loop, &loop->ev_check);
    ev_unref(loop->ev_loop); // Don't let check watcher keep loop alive alone

    us_internal_loop_data_init(loop, wakeup_cb, pre_cb, post_cb);

    // If we do not own this loop (e.g. using default loop), integrate.
    if (hint) {
        us_loop_integrate(loop); // This function needs to be backend-agnostic or adapted
    }

    return loop;
}

void us_loop_free(struct us_loop_t *loop) {
    // Stop and ref prepare/check watchers so loop knows they are gone
    ev_ref(loop->ev_loop); // Balance the unref from start
    ev_prepare_stop(loop->ev_loop, &loop->ev_pre);

    ev_ref(loop->ev_loop); // Balance the unref from start
    ev_check_stop(loop->ev_loop, &loop->ev_check);

    us_internal_loop_data_free(loop); // Free uSockets internal data

    // Run the loop once more to process any pending events from stopping watchers
    // (though libev stop is usually synchronous for watcher state)
    if (!loop->is_default) {
        ev_run(loop->ev_loop, EVRUN_NOWAIT); // Process any pending internal events
        ev_loop_destroy(loop->ev_loop);
    }
    // If it's the default loop, we don't destroy it.

    free(loop);
}

void us_loop_run(struct us_loop_t *loop) {
    us_loop_integrate(loop); // Ensure integrated if not done already
    ev_run(loop->ev_loop, 0); // 0 means run until ev_break or no active watchers
    us_internal_loop_post(loop); //free closed sockets
}

struct us_poll_t *us_create_poll(struct us_loop_t *loop, int fallthrough, unsigned int ext_size) {
    struct us_poll_t *p = (struct us_poll_t *)malloc(sizeof(struct us_poll_t) + ext_size);
    p->io = malloc(sizeof(ev_io));
    p->io->data = p;
    return p;
}

struct us_poll_t *us_poll_resize(struct us_poll_t *p, struct us_loop_t *loop, unsigned int ext_size) {
    struct us_poll_t *new_p = realloc(p, sizeof(struct us_poll_t) + ext_size);
    new_p->io->data = new_p;
    return new_p;
}

/* Timer functions */
struct us_timer_t *us_create_timer(struct us_loop_t *loop, int fallthrough, unsigned int ext_size) {
    struct us_internal_callback_t *cb = malloc(sizeof(struct us_internal_callback_t) + sizeof(ev_timer) + ext_size);

    cb->loop = loop;
    cb->cb_expects_the_loop = 0;
    cb->leave_poll_ready = 0;

    ev_timer *ev_t = (ev_timer *)(cb + 1);
    // Initialize timer but don't start. Set repeat to 0 (non-repeating by default).
    ev_timer_init(ev_t, timer_cb_ev, 0.0, 0.0);
    ev_t->data = cb; // Link back to base struct

    if (fallthrough) {
        ev_unref(loop->ev_loop);
    }

    return (struct us_timer_t *)cb;
}

void *us_timer_ext(struct us_timer_t *timer) {
    // Ext data is after us_internal_callback_t and ev_timer
    return ((char *)timer) + sizeof(struct us_internal_callback_t) + sizeof(ev_timer);
}

void us_timer_close(struct us_timer_t *t) {
    struct us_internal_callback_t *cb = (struct us_internal_callback_t *)t;
    struct us_loop_t *loop = cb->loop; // Get loop from callback struct
    ev_timer *ev_t = (ev_timer *)(cb + 1);

    ev_ref(loop->ev_loop);

    if (ev_is_active(ev_t)) {
        ev_timer_stop(loop->ev_loop, ev_t);
    }

    // ev_timer is part of the cb allocation, so free(cb) handles it.
    free(cb);
}

void us_timer_set(struct us_timer_t *t, void (*callback)(struct us_timer_t *t), int ms, int repeat_ms) {
    struct us_internal_callback_t *internal_cb = (struct us_internal_callback_t *)t;
    struct us_loop_t *loop = internal_cb->loop;
    ev_timer *ev_t = (ev_timer *)(internal_cb + 1);

    internal_cb->cb = (void (*)(struct us_internal_callback_t *))callback;

    if (ms == 0) { // 0 ms means stop the timer
        if (ev_is_active(ev_t)) {
            ev_timer_stop(loop->ev_loop, ev_t);
        }
    } else {
        // libev times are in seconds (double)
        double after = ms / 1000.0;
        double repeat = repeat_ms / 1000.0;
        ev_timer_set(ev_t, after, repeat);
        ev_timer_start(loop->ev_loop, ev_t); // Start or restart the timer
    }
}

struct us_loop_t *us_timer_loop(struct us_timer_t *t) {
    struct us_internal_callback_t *internal_cb = (struct us_internal_callback_t *)t;
    return internal_cb->loop;
}

// Async functions (internal to uSockets)
// us_internal_async is a typedef for us_internal_callback_t
struct us_internal_async *us_internal_create_async(struct us_loop_t *loop, int fallthrough, unsigned int ext_size) {
    struct us_internal_callback_t *cb = malloc(sizeof(struct us_internal_callback_t) + sizeof(ev_async) + ext_size);
    cb->loop = loop;
    return (struct us_internal_async *) cb;
}

void us_internal_async_close(struct us_internal_async *a) {
    struct us_internal_callback_t *cb = (struct us_internal_callback_t *)a;
    struct us_loop_t *loop = cb->loop;
    ev_async *ev_a = (ev_async *)(cb + 1);

    if (ev_is_active(ev_a)) { // ev_is_active for ev_async checks if it's started
        ev_ref(loop->ev_loop);
        ev_async_stop(loop->ev_loop, ev_a);
    }
    free(cb);
}

void us_internal_async_set(struct us_internal_async *a, void (*callback)(struct us_internal_async *)) {
    struct us_internal_callback_t *internal_cb = (struct us_internal_callback_t *)a;
    internal_cb->cb = (void (*)(struct us_internal_callback_t *))callback;

    ev_async *async = (ev_async *) (internal_cb + 1);
    ev_async_init(async, async_cb_ev);
    ev_async_start(internal_cb->loop->ev_loop, async);
    ev_unref(internal_cb->loop->ev_loop);
    async->data = internal_cb;
}

void us_internal_async_wakeup(struct us_internal_async *a) {
    struct us_internal_callback_t *internal_cb = (struct us_internal_callback_t *)a;
    // struct us_loop_t *loop = internal_cb->loop; // Loop is needed for ev_async_send
    ev_async *ev_a = (ev_async *)(internal_cb + 1);
    ev_async_send(internal_cb->loop->ev_loop, ev_a);
}

#endif // LIBUS_USE_LIBEV