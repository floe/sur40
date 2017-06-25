/*
 SUR40 TUIO Server
 Copyright (c) 2017 Martin Kaltenbrunner <martin@tuio.org>
 
 This program is free software; you can redistribute it and/or modify
 it under the terms of the GNU General Public License as published by
 the Free Software Foundation; either version 2 of the License, or
 (at your option) any later version.
 
 This program is distributed in the hope that it will be useful,
 but WITHOUT ANY WARRANTY; without even the implied warranty of
 MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 GNU General Public License for more details.
 
 You should have received a copy of the GNU General Public License
 along with this program; if not, write to the Free Software
 Foundation, Inc., 59 Temple Place, Suite 330, Boston, MA  02111-1307  USA
 */

#ifndef INCLUDED_SUR40TUIO_H
#define INCLUDED_SUR40TUIO_H

#include "surface.h"
#include "TuioServer.h"
#include "TuioCursor.h"
#include "TuioObject.h"
#include "TuioBlob.h"
#include "osc/OscTypes.h"
#include <list>
#include <math.h>
#include <signal.h>

#include "FlashSender.h"
#include "TcpSender.h"
#include "WebSockSender.h"

using namespace TUIO;

class Sur40_TUIO {

public:
	Sur40_TUIO(TuioServer *server);
	Sur40_TUIO() {};

	void run();
	void stop();
	TuioServer *tuioServer;

private:

	bool running;
	float width, height;
	TuioTime frameTime;

};

#endif /* INCLUDED_SUR40TUIO_H */
