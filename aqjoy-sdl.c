/* $Copyright: $
 * Copyright (c) 2022-2025 by Steve Baker (steve.baker.llc@gmail.com)
 *   and Thomas Moore (darktjm@gmail.com)
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 59 Temple Place, Suite 330, Boston, MA  02111-1307  USA
 */

#include "Aq.h"
#include <math.h>

/*
 * SDL2 is used instead of Linux event devices for portability and
 * lazy configuratbility.  Rather than provide my own configuration
 * technique for joysticks, it is done as follows:
 * 1) Only the first controller and second controller it finds are used.
 *    Don't like it?  Use an external remapper to change what is seen.
 * 2) The mapping is hard-coded to SDL2 virtual buttons:
 *     Stick #1:
 *       dir = leftx/lefty
 *       buttons: 1=x 2=y 3=rightshoulder
 *                4=a 5=b 6=righttrigger
 *     Stick #2:
 *       dir = rightx/righty
 *       buttons: 1=dpleft 2=dpup 3=leftshoulder
 *                4=dpdown 5=dpright 6=lefttrigger
 *    If there is a second controller, the sticks are reversed.
 *    Don't like it?  Use the SDL_GAMECONTROLLERCONFIG[_FILE] environment
 *    variable to change it.  I print the current config for all
 *    controllers to ilog.  Adjust as needed.  Or use a separate
 *    remapper, like my joy-remap library or xboxdrv or MoltanGamePad.
 * 3) Hot plugging is not supported.  Sorry about that if you have a flaky
 *    connection (e.g. some BT devices disconnect occasionally, and Linux
 *    is poorly equipped to handle that).  It only checks for/opens the
 *    devices once, on startup.
 * Of course I could've had just as much configurability by making you use
 * a remapper with Linux event devices, but at least this is more portable.
 */

static int rdbut(SDL_GameController *pad, SDL_GameControllerButton b);
static uint8_t chkax(int x, int y);

uint8_t aqjoy_read(uint8_t port)
{
    if(port < 14 || port > 15)
	return 0;
    static int init = 0;
    static SDL_GameController *pad1 = NULL, *pad2 = NULL;
    if(!init) {
	SDL_Init(SDL_INIT_GAMECONTROLLER);
	int nj = SDL_NumJoysticks();
	for(int i = 0; i < nj; i++)
	    if(SDL_IsGameController(i)) {
		char *map = SDL_GameControllerMappingForDeviceIndex(i);
		if(!map) {
		    char buf[33];
		    SDL_JoystickGUID guid = SDL_JoystickGetDeviceGUID(i);
		    SDL_GUIDToString(guid, buf, sizeof(buf));
		    map = strdup(buf);
		}
		if(_ilog)
		    fprintf(_ilog, "Found controller %s %s\n",
			    SDL_JoystickPathForIndex(i), map);
		free(map);
		SDL_GameController *pad = SDL_GameControllerOpen(i);
		if(!i) {
		    perror(SDL_GetError());
		    continue;
		}
		if(!pad1)
		    pad1 = pad;
		else {
		    pad2 = pad;
		    if(SDL_GameControllerGetPlayerIndex(pad1) >
		       SDL_GameControllerGetPlayerIndex(pad2)) {
			pad2 = pad1;
			pad1 = pad;
		    }
		    break;
		}
	    }
	init = 1;
    }
    if(!pad1)
	return 0xff;
    SDL_GameControllerUpdate();
    uint8_t ret1 = 0xff, ret2 = 0xff;
    const uint8_t butmask[] = {
	0xbf, 0x7b, 0x5f, 0xdf, 0x7d, 0x7e
    };
    const SDL_GameControllerButton butmap[] = {
	SDL_CONTROLLER_BUTTON_X, SDL_CONTROLLER_BUTTON_Y, SDL_CONTROLLER_BUTTON_RIGHTSHOULDER,
	SDL_CONTROLLER_BUTTON_A, SDL_CONTROLLER_BUTTON_B, SDL_CONTROLLER_BUTTON_BACK,
	SDL_CONTROLLER_BUTTON_DPAD_LEFT, SDL_CONTROLLER_BUTTON_DPAD_UP, SDL_CONTROLLER_BUTTON_LEFTSHOULDER,
	SDL_CONTROLLER_BUTTON_DPAD_DOWN, SDL_CONTROLLER_BUTTON_DPAD_RIGHT, SDL_CONTROLLER_BUTTON_START
    };
    for(int i = 0; i < 6; i++) {
	ret1 &= ~((rdbut(pad1, butmap[i]) | rdbut(pad2, butmap[i + 6])) - 1) | butmask[i];
	ret2 &= ~((rdbut(pad1, butmap[i + 6]) | rdbut(pad2, butmap[i])) - 1) | butmask[i];
    }
    int x1, y1, x2 = 0, y2 = 0;
    x1 = SDL_GameControllerGetAxis(pad1, SDL_CONTROLLER_AXIS_LEFTX);
    y1 = SDL_GameControllerGetAxis(pad1, SDL_CONTROLLER_AXIS_LEFTY);
    if(pad2) {
	x2 = SDL_GameControllerGetAxis(pad2, SDL_CONTROLLER_AXIS_LEFTX);
	y2 = SDL_GameControllerGetAxis(pad2, SDL_CONTROLLER_AXIS_LEFTY);
	if(!x1 && !y1) {
	    x1 = SDL_GameControllerGetAxis(pad2, SDL_CONTROLLER_AXIS_RIGHTX);
	    y1 = SDL_GameControllerGetAxis(pad2, SDL_CONTROLLER_AXIS_RIGHTY);
	}
    }
    if(!x2 && !y2) {
	x2 = SDL_GameControllerGetAxis(pad1, SDL_CONTROLLER_AXIS_RIGHTX);
	y2 = SDL_GameControllerGetAxis(pad1, SDL_CONTROLLER_AXIS_RIGHTY);
    }
    ret1 &= chkax(x1, y1);
    ret2 &= chkax(x2, y2);
    const char *err = SDL_GetError(); // maybe disconnect?
    if(err) {
	SDL_GameControllerClose(pad1);
	if(pad2)
	    SDL_GameControllerClose(pad2);
	init = 0;
    }
    return port == 14 ? ret1 : ret2;
}

static int rdbut(SDL_GameController *pad, SDL_GameControllerButton b)
{
    if(!pad)
	return 0;
    if(b == SDL_CONTROLLER_BUTTON_BACK)
	return SDL_GameControllerGetAxis(pad, SDL_CONTROLLER_AXIS_TRIGGERRIGHT) >= 16384;
    else if(b == SDL_CONTROLLER_BUTTON_START)
	return SDL_GameControllerGetAxis(pad, SDL_CONTROLLER_AXIS_TRIGGERLEFT) >= 16384;
    else
	return SDL_GameControllerGetButton(pad, b);
}

static uint8_t chkax(int x, int y)
{
    const uint8_t axmap[] = { // E ESE ... ENE
	0xfd, 0xed, 0xec, 0xfc, 0xfe, 0xee, 0xe6, 0xf6,
	0xf7, 0xe7, 0xe3, 0xf3, 0xf8, 0xeb, 0xe9, 0xf9
    };
    if(x > -8192 && x < 8192 && y > -8192 && y < 8192)
	return 0xff;
    /* neg == left/up, so y -> -y */
    double th = x ? atan((double)-y/x) / M_PI * 16 : -y < 0 ? -8.0 : 8.0;
    if(x < 0)
	th = 16.0 + th; // -8 .. 24
//    double th = acos((double)x/sqrt(x*x+y*y)) / M_PI * 16; // 0 .. 16
//    if(-y < 0)
//	th = 32.0 - th; // 0 .. 32
    int ith = (int)th;
    ith = ((ith + 1) / 2) & 15; // 0 .. 15
//  if(_ilog)  fprintf(_ilog, "stick x=%d, y=%d angle=%d\n", x, y, ith);
    return axmap[ith];
}
