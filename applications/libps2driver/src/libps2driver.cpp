/* * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * *
 *                                                                           *
 *  Ghost, a micro-kernel based operating system for the x86 architecture    *
 *  Copyright (C) 2015, Max Schlüssel <lokoxe@gmail.com>                     *
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

#include "libps2driver/ps2driver.hpp"
#include <ghost.h>

bool ps2DriverInitialize(g_ps2_event_stream* outStream, g_tid keyboardPartnerTask, g_tid mousePartnerTask)
{
	if(!outStream)
		return false;

	g_tid driverTid = g_task_await_by_name(G_PS2_DRIVER_NAME);
	g_message_transaction transaction = g_get_message_tx_id();

	outStream->keyboardTx = g_get_message_tx_id();
	outStream->mouseTx = g_get_message_tx_id();

	g_ps2_initialize_request request{};
	request.header.command = G_PS2_COMMAND_INITIALIZE;
	request.keyboardPartnerTask = keyboardPartnerTask;
	request.mousePartnerTask = mousePartnerTask;
	request.keyboardTx = outStream->keyboardTx;
	request.mouseTx = outStream->mouseTx;
	g_send_message_t(driverTid, &request, sizeof(request), transaction);

	size_t buflen = sizeof(g_message_header) + sizeof(g_ps2_initialize_response);
	uint8_t buf[buflen];
	auto status = g_receive_message_t(buf, buflen, transaction);
	auto response = (g_ps2_initialize_response*) G_MESSAGE_CONTENT(buf);

	if(status == G_MESSAGE_RECEIVE_STATUS_SUCCESSFUL)
	{
		if(response->status == G_PS2_INITIALIZE_SUCCESS)
			return true;
	}

	return false;
}

g_message_receive_status ps2DriverReadKeyboard(g_message_transaction tx, g_ps2_key_event* outEvent,
                                               g_message_receive_mode mode)
{
	if(!outEvent)
		return G_MESSAGE_RECEIVE_STATUS_FAILED;

	size_t buflen = sizeof(g_message_header) + sizeof(g_ps2_key_event);
	uint8_t buf[buflen];
	auto status = g_receive_message_tm(buf, buflen, tx, mode);
	if(status != G_MESSAGE_RECEIVE_STATUS_SUCCESSFUL)
		return status;

	*outEvent = *(g_ps2_key_event*) G_MESSAGE_CONTENT(buf);
	return status;
}

g_message_receive_status ps2DriverReadMouse(g_message_transaction tx, g_ps2_mouse_packet* outPacket,
                                            g_message_receive_mode mode)
{
	if(!outPacket)
		return G_MESSAGE_RECEIVE_STATUS_FAILED;

	size_t buflen = sizeof(g_message_header) + sizeof(g_ps2_mouse_packet);
	uint8_t buf[buflen];
	auto status = g_receive_message_tm(buf, buflen, tx, mode);
	if(status != G_MESSAGE_RECEIVE_STATUS_SUCCESSFUL)
		return status;

	*outPacket = *(g_ps2_mouse_packet*) G_MESSAGE_CONTENT(buf);
	return status;
}
