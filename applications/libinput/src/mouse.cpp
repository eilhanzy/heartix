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

#include <ghost.h>
#include <libps2driver/ps2driver.hpp>

#include "libinput/mouse/mouse.hpp"

g_mouse_info g_mouse::readMouse(const g_ps2_event_stream& stream)
{
	g_ps2_mouse_packet packet;
	static g_mouse_info last{};

	auto status = ps2DriverReadMouse(stream.mouseTx, &packet, G_MESSAGE_RECEIVE_MODE_BLOCKING);
	if(status != G_MESSAGE_RECEIVE_STATUS_SUCCESSFUL)
	{
		last.x = 0;
		last.y = 0;
		last.scroll = 0;
		return last;
	}

	last.x = packet.x;
	last.y = packet.y;
	last.scroll = packet.scroll;
	last.button1 = (packet.flags & (1 << 0));
	last.button2 = (packet.flags & (1 << 1));
	last.button3 = (packet.flags & (1 << 2));
	return last;
}
