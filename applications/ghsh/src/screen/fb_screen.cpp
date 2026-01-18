/* * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * *
 *                                                                           *
 *  Ghost, a micro-kernel based operating system for the x86 architecture    *
 *  Copyright (C) 2025, Max Schluessel <lokoxe@gmail.com>                    *
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

#include "fb_screen.hpp"

#include "../bitmap_font.hpp"

#include <ghost.h>
#include <ghost/messages.h>
#include <libps2driver/ps2driver.hpp>
#include <string.h>

namespace
{
static g_ps2_event_stream inputStream;
}

bool fb_screen_t::waitForVideoDevice(g_tid& outDriver, g_device_id& outDevice)
{
	auto tx = G_MESSAGE_TOPIC_TRANSACTION_START;
	uint8_t buf[1024];

	while(true)
	{
		auto status = g_receive_topic_message(G_DEVICE_EVENT_TOPIC, buf, sizeof(buf), tx);
		if(status != G_MESSAGE_RECEIVE_STATUS_SUCCESSFUL)
			continue;

		auto header = reinterpret_cast<g_message_header*>(buf);
		tx = header->transaction;
		auto content = reinterpret_cast<g_device_event_header*>(G_MESSAGE_CONTENT(header));

		if(content->event == G_DEVICE_EVENT_DEVICE_REGISTERED)
		{
			auto ev = reinterpret_cast<g_device_event_device_registered*>(content);
			if(ev->type == G_DEVICE_TYPE_VIDEO)
			{
				outDriver = ev->driver;
				outDevice = ev->id;
				return true;
			}
		}
	}
}

bool fb_screen_t::initializeVideo()
{
	if(!waitForVideoDevice(driverTid, deviceId))
		return false;

	if(!videoDriverSetMode(driverTid, deviceId, 1024, 768, 32, mode))
		return false;

	columns = mode.resX / bitmapFontCharWidth;
	rows = mode.resY / bitmapFontCharHeight;
	if(columns <= 0 || rows <= 0)
		return false;

	return true;
}

void fb_screen_t::clearPixels()
{
	if(!mode.lfb)
		return;

	uint8_t* fb = reinterpret_cast<uint8_t*>(mode.lfb);
	memset(fb, 0, mode.bpsl * mode.resY);
	if(mode.explicit_update)
		videoDriverUpdate(driverTid, deviceId, 0, 0, mode.resX, mode.resY);
}

void fb_screen_t::scrollUp()
{
	if(!mode.lfb)
		return;

	uint8_t* fb = reinterpret_cast<uint8_t*>(mode.lfb);
	uint32_t rowBytes = mode.bpsl;
	uint32_t scrollBytes = rowBytes * bitmapFontCharHeight;
	uint32_t copyBytes = rowBytes * (mode.resY - bitmapFontCharHeight);

	memmove(fb, fb + scrollBytes, copyBytes);
	memset(fb + copyBytes, 0, scrollBytes);
	if(mode.explicit_update)
		videoDriverUpdate(driverTid, deviceId, 0, 0, mode.resX, mode.resY);
}

uint32_t fb_screen_t::mapColor(screen_color_t color) const
{
	static const uint32_t kColors[16] = {
		0x00000000, 0x000000AA, 0x0000AA00, 0x0000AAAA,
		0x00AA0000, 0x00AA00AA, 0x00AA5500, 0x00AAAAAA,
		0x00555555, 0x005555FF, 0x0055FF55, 0x0055FFFF,
		0x00FF5555, 0x00FF55FF, 0x00FFFF55, 0x00FFFFFF
	};

	if(color >= 0 && color < 16)
		return kColors[color];
	return 0x00FFFFFF;
}

void fb_screen_t::drawChar(int x, int y, char c)
{
	int onScreenX = x * bitmapFontCharWidth;
	int onScreenY = y * bitmapFontCharHeight;
	if(onScreenX > mode.resX - bitmapFontCharWidth ||
	   onScreenY > mode.resY - bitmapFontCharHeight)
	{
		return;
	}

	uint8_t* fontChar = bitmapFontGetChar(c);
	uint8_t* fb = reinterpret_cast<uint8_t*>(mode.lfb);
	uint32_t pitch = mode.bpsl;
	uint32_t fg = fgColor;
	uint32_t bg = bgColor;

	if(!fontChar)
	{
		for(int cy = 0; cy < bitmapFontCharHeight; cy++)
		{
			uint8_t* row = fb + (onScreenY + cy) * pitch + (onScreenX * (mode.bpp / 8));
			for(int cx = 0; cx < bitmapFontCharWidth; cx++)
			{
				if(mode.bpp == 32)
				{
					uint32_t* pixel = reinterpret_cast<uint32_t*>(row + cx * 4);
					*pixel = fg;
				}
				else if(mode.bpp == 24)
				{
					uint8_t* pixel = row + cx * 3;
					pixel[0] = fg & 0xFF;
					pixel[1] = (fg >> 8) & 0xFF;
					pixel[2] = (fg >> 16) & 0xFF;
				}
			}
		}
		return;
	}

	for(int cy = 0; cy < bitmapFontCharHeight; cy++)
	{
		uint8_t* row = fb + (onScreenY + cy) * pitch + (onScreenX * (mode.bpp / 8));
		for(int cx = 0; cx < bitmapFontCharWidth; cx++)
		{
			uint32_t color = fontChar[cy * bitmapFontCharWidth + cx] ? fg : bg;
			if(mode.bpp == 32)
			{
				uint32_t* pixel = reinterpret_cast<uint32_t*>(row + cx * 4);
				*pixel = color;
			}
			else if(mode.bpp == 24)
			{
				uint8_t* pixel = row + cx * 3;
				pixel[0] = color & 0xFF;
				pixel[1] = (color >> 8) & 0xFF;
				pixel[2] = (color >> 16) & 0xFF;
			}
		}
	}
}

bool fb_screen_t::initialize(g_user_mutex exitFlag)
{
	lock = g_mutex_initialize();
	this->exitFlag = exitFlag;

	if(!initializeVideo())
		return false;

	clearPixels();

	if(!ps2DriverInitialize(&inputStream, G_TID_NONE, G_TID_NONE, G_PS2_SUBSCRIBE_KEYBOARD))
		return false;

	fgColor = mapColor(SC_WHITE);
	bgColor = mapColor(SC_BLACK);
	return true;
}

g_key_info fb_screen_t::readInput()
{
	return g_keyboard::readKey(inputStream);
}

void fb_screen_t::clean()
{
	g_mutex_acquire(lock);
	clearPixels();
	cursorX = 0;
	cursorY = 0;
	g_mutex_release(lock);
}

void fb_screen_t::backspace()
{
	g_mutex_acquire(lock);
	if(cursorX > 0)
	{
		--cursorX;
		drawChar(cursorX, cursorY, ' ');
	}
	g_mutex_release(lock);
}

void fb_screen_t::write(char c)
{
	g_mutex_acquire(lock);
	if(c == '\n')
	{
		cursorX = 0;
		++cursorY;
		if(cursorY >= rows)
		{
			scrollUp();
			cursorY = rows - 1;
		}
	}
	else
	{
		drawChar(cursorX, cursorY, c);
		++cursorX;
		if(cursorX >= columns)
		{
			cursorX = 0;
			++cursorY;
			if(cursorY >= rows)
			{
				scrollUp();
				cursorY = rows - 1;
			}
		}
	}
	g_mutex_release(lock);
}

void fb_screen_t::flush()
{
	if(mode.explicit_update)
		videoDriverUpdate(driverTid, deviceId, 0, 0, mode.resX, mode.resY);
}

void fb_screen_t::remove()
{
	g_mutex_acquire(lock);
	drawChar(cursorX, cursorY, ' ');
	g_mutex_release(lock);
}

void fb_screen_t::setCursor(int x, int y)
{
	g_mutex_acquire(lock);
	if(columns <= 0 || rows <= 0)
	{
		cursorX = 0;
		cursorY = 0;
		g_mutex_release(lock);
		return;
	}
	if(x < 0)
		x = 0;
	if(y < 0)
		y = 0;
	if(x >= columns)
	{
		y += x / columns;
		x = x % columns;
	}
	if(y >= rows)
	{
		int scrolls = y - (rows - 1);
		for(int i = 0; i < scrolls; ++i)
			scrollUp();
		y = rows - 1;
	}
	cursorX = x;
	cursorY = y;
	g_mutex_release(lock);
}

int fb_screen_t::getCursorX()
{
	return cursorX;
}

int fb_screen_t::getCursorY()
{
	return cursorY;
}

void fb_screen_t::setCursorVisible(bool visible)
{
	(void) visible;
}

void fb_screen_t::setScrollAreaScreen()
{
}

void fb_screen_t::setScrollArea(int start, int end)
{
	(void) start;
	(void) end;
}

void fb_screen_t::scroll(int value)
{
	(void) value;
}

int fb_screen_t::getColumns()
{
	return columns;
}

int fb_screen_t::getRows()
{
	return rows;
}

void fb_screen_t::setColorForeground(int c)
{
	fgColor = mapColor((screen_color_t) c);
}

void fb_screen_t::setColorBackground(int c)
{
	bgColor = mapColor((screen_color_t) c);
}
