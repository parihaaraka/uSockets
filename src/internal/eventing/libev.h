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

#ifndef LIBEV_H
#define LIBEV_H

#include "internal/loop_data.h" // Assuming this is uSockets internal

#include <ev.h>

// Define LIBUS_SOCKET_READABLE and LIBUS_SOCKET_WRITABLE based on libev events
#define LIBUS_SOCKET_READABLE EV_READ
#define LIBUS_SOCKET_WRITABLE EV_WRITE

struct us_loop_t {
    alignas(LIBUS_EXT_ALIGNMENT) struct us_internal_loop_data_t data;

    struct ev_loop *ev_loop;
    int is_default; // True if ev_loop was provided (e.g., ev_default_loop())

    ev_prepare ev_pre; //libuv uses pointers here
    ev_check ev_check;
};

// For libev, ev_io is typically embedded.
struct us_poll_t {
    ev_io *io;
    // LIBUS_SOCKET_DESCRIPTOR fd is part of ev_io (io_watcher.fd)
    unsigned char poll_type; // uSockets specific poll type tracking
    // No need for a separate pointer to the watcher like uv_p
};

// Timers and Asyncs in uSockets often use a common callback structure.
// We'll embed the ev_timer or ev_async after us_internal_callback_t.
// us_timer_t and us_internal_async * will be cast to us_internal_callback_t *.

#endif // LIBEV_H