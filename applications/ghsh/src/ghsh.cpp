/* * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * *
 *                                                                           *
 *  Ghost, a micro-kernel based operating system for the x86 architecture    *
 *  Copyright (C) 2015, Max Schluessel <lokoxe@gmail.com>                    *
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
#include <ghost/tasks.h>
#include <libterminal/terminal.hpp>
#include <unistd.h>

#include <sstream>
#include <string>

#include "screen/fb_screen.hpp"
#include "screen/headless_screen.hpp"
#include "stream_status.hpp"

static std::string defaultKeyboardLayout = "de-DE";

static g_pid shellProcess = G_PID_NONE;
static g_fd shellStdin = G_FD_NONE;
static g_fd shellStdout = G_FD_NONE;
static g_fd shellStderr = G_FD_NONE;
static g_pid controlProcess = G_PID_NONE;

static screen_t* screen = nullptr;
static g_user_mutex screenLock = g_mutex_initialize();
static g_terminal_mode inputMode = G_TERMINAL_MODE_DEFAULT;
static bool inputEcho = true;
static g_user_mutex exitFlag = g_mutex_initialize();

struct output_routine_startinfo_t
{
	bool isStderr;
};

static void ghshShellJoiner();
static void ghshTerminationRoutine();
static void ghshWriteToShell(const std::string& line);
static void ghshWriteToShell(const char* line);
static void ghshWriteTermkeyToShell(int termkey);
static bool ghshStartShell();
static void ghshInputRoutine();
static void ghshProcessSequenceVt100(stream_control_status_t* status);
static void ghshProcessSequenceGhostterm(stream_control_status_t* status);
static screen_color_t terminalColorFromVt100(int color);
static void ghshProcessOutput(stream_control_status_t* status, bool isStderr, char c);
static void ghshOutputRoutine(output_routine_startinfo_t* data);

static void ghshShellJoiner()
{
	g_join(shellProcess);
	g_exit(0);
}

static void ghshTerminationRoutine()
{
	g_mutex_acquire(exitFlag);
	g_mutex_acquire(exitFlag);
	if(shellProcess != G_PID_NONE)
		g_kill(shellProcess);
	g_exit(0);
}

static void ghshWriteToShell(const std::string& line)
{
	const char* lineContent = line.c_str();
	int lineLength = static_cast<int>(line.size());

	int done = 0;
	int len = 0;
	while(done < lineLength)
	{
		len = write(shellStdin, &lineContent[done], lineLength - done);
		if(len <= 0)
			break;
		done += len;
	}
}

static void ghshWriteToShell(const char* line)
{
	ghshWriteToShell(std::string(line));
}

static void ghshWriteTermkeyToShell(int termkey)
{
	char buf[3];
	buf[0] = G_TERMKEY_SUB;
	buf[1] = termkey & 0xFF;
	buf[2] = (termkey >> 8) & 0xFF;
	write(shellStdin, &buf, 3);
}

static bool ghshStartShell()
{
	g_fd shellStdinW;
	g_fd shellStdinR;
	if(g_pipe(&shellStdinW, &shellStdinR) != G_FS_PIPE_SUCCESSFUL)
	{
		klog("ghsh: failed to setup stdin pipe for shell");
		return false;
	}

	g_fd shellStdoutW;
	g_fd shellStdoutR;
	if(g_pipe(&shellStdoutW, &shellStdoutR) != G_FS_PIPE_SUCCESSFUL)
	{
		klog("ghsh: failed to setup stdout pipe for shell");
		return false;
	}

	g_fd shellStderrW;
	g_fd shellStderrR;
	if(g_pipe(&shellStderrW, &shellStderrR) != G_FS_PIPE_SUCCESSFUL)
	{
		klog("ghsh: failed to setup stderr pipe for shell");
		return false;
	}

	g_fd stdioIn[3];
	stdioIn[0] = shellStdinR;
	stdioIn[1] = shellStdoutW;
	stdioIn[2] = shellStderrW;

	auto shellStatus = g_spawn_poi("/applications/gsh.bin", "", "/", G_SECURITY_LEVEL_DRIVER, &shellProcess,
	                               nullptr, stdioIn);
	if(shellStatus != G_SPAWN_STATUS_SUCCESSFUL)
	{
		klog("ghsh: failed to spawn shell process");
		return false;
	}

	g_create_task((void*) ghshShellJoiner);
	g_create_task((void*) ghshTerminationRoutine);

	shellStdin = shellStdinW;
	shellStdout = shellStdoutR;
	shellStderr = shellStderrR;
	return true;
}

static void ghshInputRoutine()
{
	std::string buffer = "";
	while(true)
	{
		g_key_info input = screen->readInput();

		if(inputMode == G_TERMINAL_MODE_DEFAULT)
		{
			if(input.key == "KEY_ENTER" && input.pressed)
			{
				if(inputEcho)
				{
					g_mutex_acquire(screenLock);
					screen->write('\n');
					g_mutex_release(screenLock);
				}

				buffer += '\n';
				ghshWriteToShell(buffer);

				buffer = "";
			}
			else if((input.ctrl && input.key == "KEY_C") || (input.key == "KEY_ESC"))
			{
				if(controlProcess && controlProcess != shellProcess)
				{
					g_kill(controlProcess);
				}
			}
			else if(input.key == "KEY_BACKSPACE" && input.pressed)
			{
				buffer = buffer.size() > 0 ? buffer.substr(0, buffer.size() - 1) : buffer;
				screen->backspace();
			}
			else
			{
				char chr = g_keyboard::charForKey(input);
				if(chr != -1)
				{
					buffer += chr;
					if(inputEcho)
					{
						g_mutex_acquire(screenLock);
						screen->write(chr);
						g_mutex_release(screenLock);
					}
				}
			}
		}
		else
		{
			if(input.key == "KEY_ENTER" && input.pressed)
			{
				ghshWriteTermkeyToShell(G_TERMKEY_ENTER);
			}
			else if(input.key == "KEY_BACKSPACE" && input.pressed)
			{
				ghshWriteTermkeyToShell(G_TERMKEY_BACKSPACE);
			}
			else if(input.key == "KEY_ARROW_LEFT" && input.pressed)
			{
				ghshWriteTermkeyToShell(G_TERMKEY_LEFT);
			}
			else if(input.key == "KEY_ARROW_RIGHT" && input.pressed)
			{
				ghshWriteTermkeyToShell(G_TERMKEY_RIGHT);
			}
			else if(input.key == "KEY_ARROW_UP" && input.pressed)
			{
				ghshWriteTermkeyToShell(G_TERMKEY_UP);
			}
			else if(input.key == "KEY_ARROW_DOWN" && input.pressed)
			{
				ghshWriteTermkeyToShell(G_TERMKEY_DOWN);
			}
			else if(input.key == "KEY_TAB" && input.pressed && input.shift)
			{
				ghshWriteToShell("\t");
			}
			else if(input.key == "KEY_TAB" && input.pressed)
			{
				ghshWriteTermkeyToShell(G_TERMKEY_STAB);
			}
			else
			{
				char chr = g_keyboard::charForKey(input);
				if(chr != -1)
				{
					write(shellStdin, &chr, 1);

					if(inputEcho)
					{
						g_mutex_acquire(screenLock);
						screen->write(chr);
						g_mutex_release(screenLock);
					}
				}
			}
		}
		screen->flush();
	}
}

static void ghshProcessSequenceVt100(stream_control_status_t* status)
{
	switch(status->controlCharacter)
	{
		case 'A':
			screen->setCursor(screen->getCursorX(), screen->getCursorY() - status->parameters[0]);
			screen->flush();
			break;
		case 'B':
			screen->setCursor(screen->getCursorX(), screen->getCursorY() + status->parameters[0]);
			screen->flush();
			break;
		case 'C':
			screen->setCursor(screen->getCursorX() + status->parameters[0], screen->getCursorY());
			screen->flush();
			break;
		case 'D':
			screen->setCursor(screen->getCursorX() - status->parameters[0], screen->getCursorY());
			screen->flush();
			break;
		case 'm':
			for(int i = 0; i < status->parameterCount; i++)
			{
				int param = status->parameters[i];
				if(param == 0)
				{
					screen->setColorBackground(SC_BLACK);
					screen->setColorForeground(SC_WHITE);
				}
				else if(param >= 30 && param < 40)
				{
					screen->setColorForeground(terminalColorFromVt100(param - 30));
				}
				else if(param >= 40 && param < 50)
				{
					screen->setColorBackground(terminalColorFromVt100(param - 40));
				}
			}
			break;
		case 'J':
			if(status->parameterCount == 1)
			{
				if(status->parameters[0] == 2)
					screen->clean();
			}
			break;
		case 'f':
			screen->setCursor(status->parameters[1], status->parameters[0]);
			break;
		case 'n':
			if(status->parameters[0] == 6)
			{
				std::stringstream response;
				response << (char) G_TERMKEY_ESC << "[" << screen->getCursorY() << ";" << screen->getCursorX() << "R";
				auto responseStr = response.str();
				write(shellStdin, responseStr.c_str(), responseStr.size());
			}
			break;
		case 'r':
			if(status->parameterCount == 0)
			{
				screen->setScrollAreaScreen();
			}
			else
			{
				screen->setScrollArea(status->parameters[0], status->parameters[1]);
			}
			break;
		case 'S':
			screen->scroll(status->parameters[0]);
			break;
		case 'T':
			screen->scroll(-status->parameters[0]);
			break;
	}
}

static void ghshProcessSequenceGhostterm(stream_control_status_t* status)
{
	switch(status->controlCharacter)
	{
		case 'm':
			inputMode = (g_terminal_mode) status->parameters[0];
			break;
		case 'e':
			inputEcho = (status->parameters[0] == 1);
			break;
		case 'i':
		{
			std::stringstream response;
			response << (char) G_TERMKEY_ESC << "{" << screen->getColumns() << ";" << screen->getRows() << "i";
			ghshWriteToShell(response.str());
			break;
		}
		case 'p':
			screen->write(status->parameters[0]);
			break;
		case 'x':
			screen->remove();
			break;
		case 'c':
			controlProcess = status->parameters[0];
			break;
		case 'C':
			if(status->parameters[0] == 0)
			{
				screen->setCursorVisible(status->parameters[1]);
			}
			break;
	}
}

static screen_color_t terminalColorFromVt100(int color)
{
	switch(color)
	{
		case VT100_COLOR_BLACK:
			return SC_BLACK;
		case VT100_COLOR_BLUE:
			return SC_BLUE;
		case VT100_COLOR_CYAN:
			return SC_CYAN;
		case VT100_COLOR_GREEN:
			return SC_GREEN;
		case VT100_COLOR_MAGENTA:
			return SC_MAGENTA;
		case VT100_COLOR_RED:
			return SC_RED;
		case VT100_COLOR_WHITE:
			return SC_WHITE;
		case VT100_COLOR_YELLOW:
			return SC_YELLOW;
		case VT100_COLOR_GRAY:
			return SC_LGRAY;
	}
	return SC_WHITE;
}

static void ghshProcessOutput(stream_control_status_t* status, bool isStderr, char c)
{
	if(status->status == TERMINAL_STREAM_STATUS_TEXT)
	{
		if(c == '\r')
		{
			return;
		}
		else if(c == '\t')
		{
			screen->write(' ');
			screen->write(' ');
			screen->write(' ');
			screen->write(' ');
		}
		else if(c == 27)
		{
			status->status = TERMINAL_STREAM_STATUS_LAST_WAS_ESC;
		}
		else
		{
			int fg = screen->getColorForeground();
			if(isStderr)
				screen->setColorForeground(SC_RED);
			screen->write(c);
			if(isStderr)
				screen->setColorForeground(fg);
		}
	}
	else if(status->status == TERMINAL_STREAM_STATUS_LAST_WAS_ESC)
	{
		if(c == '[')
		{
			status->status = TERMINAL_STREAM_STATUS_WITHIN_VT100;
		}
		else if(c == '{')
		{
			status->status = TERMINAL_STREAM_STATUS_WITHIN_GHOSTTERM;
		}
		else
		{
			status->status = TERMINAL_STREAM_STATUS_TEXT;
		}
	}
	else if(status->status == TERMINAL_STREAM_STATUS_WITHIN_VT100 ||
	        status->status == TERMINAL_STREAM_STATUS_WITHIN_GHOSTTERM)
	{
		if(c >= '0' && c <= '9')
		{
			if(status->parameterCount == 0)
				status->parameterCount = 1;

			if(status->parameterCount <= TERMINAL_STREAM_CONTROL_MAX_PARAMETERS)
			{
				status->parameters[status->parameterCount - 1] =
				    status->parameters[status->parameterCount - 1] * 10;
				status->parameters[status->parameterCount - 1] += c - '0';
			}
		}
		else if(c == ';')
		{
			status->parameterCount++;
		}
		else
		{
			status->controlCharacter = c;
			if(status->status == TERMINAL_STREAM_STATUS_WITHIN_VT100)
				ghshProcessSequenceVt100(status);
			else if(status->status == TERMINAL_STREAM_STATUS_WITHIN_GHOSTTERM)
				ghshProcessSequenceGhostterm(status);

			status->parameterCount = 0;
			for(int i = 0; i < TERMINAL_STREAM_CONTROL_MAX_PARAMETERS; i++)
				status->parameters[i] = 0;
			status->status = TERMINAL_STREAM_STATUS_TEXT;
		}
	}
}

static void ghshOutputRoutine(output_routine_startinfo_t* data)
{
	size_t bufSize = 1024;
	uint8_t* buf = new uint8_t[bufSize];

	stream_control_status_t status;

	while(true)
	{
		g_fs_read_status stat;
		int r = g_read_s(data->isStderr ? shellStderr : shellStdout, buf, bufSize, &stat);

		if(stat == G_FS_READ_SUCCESSFUL)
		{
			g_mutex_acquire(screenLock);
			for(int i = 0; i < r; i++)
			{
				ghshProcessOutput(&status, data->isStderr, buf[i]);
			}
			screen->flush();
			g_mutex_release(screenLock);
		}
		else
		{
			break;
		}
	}

	delete[] buf;
	delete data;
}

int main(int argc, char* argv[])
{
	screen = new fb_screen_t();
	if(!screen->initialize(exitFlag))
	{
		klog("ghsh: failed to initialize framebuffer screen, falling back");
		screen = new headless_screen_t();
		if(!screen->initialize(exitFlag))
		{
			klog("ghsh: failed to initialize headless screen");
			return -1;
		}
	}
	screen->clean();

	if(!g_keyboard::loadLayout(defaultKeyboardLayout))
	{
		if(!g_keyboard::loadLayout("en-US"))
		{
			klog("ghsh: failed to load keyboard layout");
			return -1;
		}
	}

	if(!ghshStartShell())
		return -1;

	output_routine_startinfo_t* outInfo = new output_routine_startinfo_t();
	outInfo->isStderr = false;
	g_create_task_d((void*) &ghshOutputRoutine, outInfo);

	output_routine_startinfo_t* errInfo = new output_routine_startinfo_t();
	errInfo->isStderr = true;
	g_create_task_d((void*) &ghshOutputRoutine, errInfo);

	ghshInputRoutine();
	return 0;
}
