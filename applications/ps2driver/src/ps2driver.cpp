/* * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * *
 *                                                                           *
 *  Ghost, a micro-kernel based operating system for the x86 architecture    *
 *  Copyright (C) 2015, Max Schluessel <lokoxe@gmail.com>                     *
 *                                                                           *
 *  This program is free software: you can redistribute it and/or modify     *
 *  it under the terms of the GNU General Public License as published by     *
 *  the Free Software Foundation, either version 3 of the License, or        *
 *  (at your option) any later version.                                      *
 *                                                                           *
 *  This program is distributed in the hope that it will be useful,          *
 *  but WITHOUT ANY WARRANTY; without even the implied warranty of           *
 *  MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the            *
 *  GNU General Public License for more details.                             *
 *                                                                           *
 *  You should have received a copy of the GNU General Public License        *
 *  along with this program.  If not, see <http://www.gnu.org/licenses/>.    *
 *                                                                           *
 * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * */

#include "ps2driver.hpp"

#include <libps2/ps2.hpp>

#include <ghost.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>

struct ps2_subscriber
{
	g_tid tid;
	g_message_transaction keyboardTx;
	g_message_transaction mouseTx;
	g_tid keyboardPartner;
	g_tid mousePartner;
	ps2_subscriber* next;
};

static ps2_subscriber* subscribers = nullptr;
static g_user_mutex subscriberLock = g_mutex_initialize();

static volatile uint64_t mouseDropped = 0;
static volatile uint64_t keyDropped = 0;

static void ps2AddOrUpdateSubscriber(g_tid tid, g_message_transaction keyboardTx, g_message_transaction mouseTx,
                                    g_tid keyboardPartner, g_tid mousePartner);
static void ps2BroadcastMouse(const g_ps2_mouse_packet& packet);
static void ps2BroadcastKey(uint8_t scancode);

int main()
{
	if(!g_task_register_name(G_PS2_DRIVER_NAME))
	{
		klog("ps2driver: could not register with task name '%s'", (char*) G_PS2_DRIVER_NAME);
		return -1;
	}

	ps2DriverInitialize();
	ps2DriverReceiveMessages();
	return 0;
}

void ps2DriverInitialize()
{
	ps2Initialize(ps2MouseCallback, ps2KeyboardCallback);
}

void ps2MouseCallback(int16_t x, int16_t y, uint8_t flags, int8_t scroll)
{
	g_ps2_mouse_packet packet;
	packet.x = x;
	packet.y = y;
	packet.flags = flags;
	packet.scroll = scroll;

	ps2BroadcastMouse(packet);
}

void ps2KeyboardCallback(uint8_t c)
{
	if(c == 0x3f) // F5
	{
		g_dump();
	}

	ps2BroadcastKey(c);
}

void ps2DriverReceiveMessages()
{
	size_t buflen = sizeof(g_message_header) + sizeof(g_ps2_initialize_request);
	uint8_t buf[buflen];

	for(;;)
	{
		auto status = g_receive_message(buf, buflen);
		if(status != G_MESSAGE_RECEIVE_STATUS_SUCCESSFUL)
		{
			klog("ps2driver: error receiving message, retrying");
			continue;
		}

		g_message_header* header = (g_message_header*) buf;
		g_ps2_request_header* request = (g_ps2_request_header*) G_MESSAGE_CONTENT(buf);

		if(request->command == G_PS2_COMMAND_INITIALIZE)
		{
			ps2HandleCommandInitialize((g_ps2_initialize_request*) request, header->sender, header->transaction);
		}
		else
		{
			klog("ps2driver: received unknown command %i from task %i", request->command, header->sender);
		}
	}
}

void ps2HandleCommandInitialize(g_ps2_initialize_request* request, g_tid requestingTaskId,
                                g_message_transaction requestTransaction)
{
	g_ps2_initialize_response response;
	response.status = G_PS2_INITIALIZE_FAILED;

	ps2AddOrUpdateSubscriber(requestingTaskId, request->keyboardTx, request->mouseTx, request->keyboardPartnerTask,
	                         request->mousePartnerTask);
	response.status = G_PS2_INITIALIZE_SUCCESS;

	g_send_message_t(requestingTaskId, &response, sizeof(g_ps2_initialize_response), requestTransaction);
}

static void ps2AddOrUpdateSubscriber(g_tid tid, g_message_transaction keyboardTx, g_message_transaction mouseTx,
                                    g_tid keyboardPartner, g_tid mousePartner)
{
	if(tid == G_TID_NONE)
		return;

	g_mutex_acquire(subscriberLock);
	for(ps2_subscriber* sub = subscribers; sub; sub = sub->next)
	{
		if(sub->tid == tid)
		{
			sub->keyboardTx = keyboardTx;
			sub->mouseTx = mouseTx;
			sub->keyboardPartner = keyboardPartner;
			sub->mousePartner = mousePartner;
			g_mutex_release(subscriberLock);
			return;
		}
	}

	ps2_subscriber* node = (ps2_subscriber*) malloc(sizeof(ps2_subscriber));
	if(!node)
	{
		g_mutex_release(subscriberLock);
		return;
	}

	node->tid = tid;
	node->keyboardTx = keyboardTx;
	node->mouseTx = mouseTx;
	node->keyboardPartner = keyboardPartner;
	node->mousePartner = mousePartner;
	node->next = subscribers;
	subscribers = node;
	g_mutex_release(subscriberLock);
}

static void ps2BroadcastMouse(const g_ps2_mouse_packet& packet)
{
	g_mutex_acquire(subscriberLock);
	for(ps2_subscriber* sub = subscribers; sub; sub = sub->next)
	{
		if(sub->mouseTx == G_MESSAGE_TRANSACTION_NONE)
			continue;

		auto status = g_send_message_tm(sub->tid, (void*) &packet, sizeof(packet), sub->mouseTx,
		                               G_MESSAGE_SEND_MODE_NON_BLOCKING);
		if(status == G_MESSAGE_SEND_STATUS_SUCCESSFUL)
		{
			if(sub->mousePartner != G_TID_NONE)
				g_yield_t(sub->mousePartner);
		}
		else if(status == G_MESSAGE_SEND_STATUS_FULL)
		{
			++mouseDropped;
		}
	}
	g_mutex_release(subscriberLock);
}

static void ps2BroadcastKey(uint8_t scancode)
{
	g_ps2_key_event event;
	event.scancode = scancode;

	g_mutex_acquire(subscriberLock);
	for(ps2_subscriber* sub = subscribers; sub; sub = sub->next)
	{
		if(sub->keyboardTx == G_MESSAGE_TRANSACTION_NONE)
			continue;

		auto status = g_send_message_tm(sub->tid, &event, sizeof(event), sub->keyboardTx,
		                               G_MESSAGE_SEND_MODE_NON_BLOCKING);
		if(status == G_MESSAGE_SEND_STATUS_SUCCESSFUL)
		{
			if(sub->keyboardPartner != G_TID_NONE)
				g_yield_t(sub->keyboardPartner);
		}
		else if(status == G_MESSAGE_SEND_STATUS_FULL)
		{
			++keyDropped;
		}
	}
	g_mutex_release(subscriberLock);
}
