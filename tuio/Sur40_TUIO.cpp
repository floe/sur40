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

#include "Sur40_TUIO.h"

Sur40_TUIO *sur40_tuio = NULL;
usb_dev_handle* sur40 = NULL;
bool verbose = false;

static void terminate (int param)
{
        if(sur40_tuio!=NULL) sur40_tuio->stop();
}

Sur40_TUIO::Sur40_TUIO(TuioServer *server)
	: running(true)
	, width	(1920)
	, height(1080)
{
	tuioServer = server;
	tuioServer->setSourceName("sur40");
	tuioServer->setVerbose(verbose);
}

void Sur40_TUIO::stop() {
	running = false;
}

void Sur40_TUIO::run() {

        surface_blob blob[256];
        int result, count = 0;

        while (running) {

                result = surface_get_blobs( sur40, blob );
		count = tuioServer->getTuioObjectCount() + tuioServer->getTuioCursorCount();// + tuioServer->getTuioBlobCount();

                if ((result <= 0) && (count<=0)) continue;

		TuioTime frameTime = TuioTime::getSystemTime();
		tuioServer->initFrame(frameTime);

                //printf("%d blobs\n",result);
                for (int i = 0; i < result; i++) {

			switch(blob[i].type) {
				case 0x01: {	// blob
					/*TuioBlob *tblb = tuioServer->getTuioBlob(blob[i].blob_id);
					if (tblb==NULL) {
						tblb = tuioServer->addTuioBlob(blob[i].pos_x/width,blob[i].pos_y/height,blob[i].angle,blob[i].bb_size_x/width,blob[i].bb_size_y/height,blob[i].area/(width*height));
						tblb->setSessionID(blob[i].blob_id);
					} else tuioServer->updateTuioBlob(tblb,blob[i].pos_x/width,blob[i].pos_y/height,blob[i].angle,blob[i].bb_size_x/width,blob[i].bb_size_y/height,blob[i].area/(width*height));*/
					break;
				}
				case 0x02: {	// touch
					TuioCursor *tcur = tuioServer->getTuioCursor(blob[i].blob_id);
					if (tcur==NULL) {
						tcur = tuioServer->addTuioCursor(blob[i].pos_x/width,blob[i].pos_y/height);
						tcur->setSessionID(blob[i].blob_id);
					} else tuioServer->updateTuioCursor(tcur,blob[i].pos_x/width,blob[i].pos_y/height);
					break;
				}
				case 0x04: {	// tag
					TuioObject *tobj = tuioServer->getTuioObject(blob[i].blob_id);
					if (tobj==NULL) {
						tobj = tuioServer->addTuioObject(blob[i].tag_id,blob[i].pos_x/width,blob[i].pos_y/height,blob[i].angle);
						tobj->setSessionID(blob[i].blob_id);
					} else tuioServer->updateTuioObject(tobj,blob[i].pos_x/width,blob[i].pos_y/height,blob[i].angle);
					break;
				}
			}

                        //printf("    x: %d y: %d size: %d\n",blob[i].type,blob[i].pos_x,blob[i].pos_y);
		}

		//tuioServer->stopUntouchedMovingBlobs();
		tuioServer->stopUntouchedMovingCursors();
		tuioServer->stopUntouchedMovingObjects();
		//tuioServer->removeUntouchedStoppedBlobs();
		tuioServer->removeUntouchedStoppedCursors();
		tuioServer->removeUntouchedStoppedObjects();
	        tuioServer->commitFrame();
        }
}

int main(int argc, char* argv[])
{

#ifndef WIN32
        signal(SIGINT,terminate);
        signal(SIGHUP,terminate);
        signal(SIGQUIT,terminate);
        signal(SIGTERM,terminate);
#endif

        sur40 = usb_get_device_handle( ID_MICROSOFT, ID_SURFACE );
	if (sur40 == NULL) {
		std::cout << "no SUR40 found" << std::endl;
		return 0;
	}
        surface_init(sur40);

	surface_set_vsvideo(sur40);
	surface_set_irlevel(sur40);

	TuioServer *server;
	if( argc == 3 ) {
		server = new TuioServer(argv[1],atoi(argv[2]));
	} else server = new TuioServer(); // default is UDP port 3333 on localhost

	// add an additional TUIO/TCP sender
	OscSender *tcp_sender;
	if( argc == 2 ) {
		tcp_sender = new TcpSender(atoi(argv[1]));
	} else if ( argc == 3 ) {
		tcp_sender = new TcpSender(argv[1],atoi(argv[2]));
	} else tcp_sender = new TcpSender(3333);
	server->addOscSender(tcp_sender);

	// add an additional TUIO/FLC sender
	OscSender *flash_sender = new FlashSender();
	server->addOscSender(flash_sender);

	// add an additional WebSocket sender
	WebSockSender *websock_sender = new WebSockSender();
	server->addOscSender(websock_sender);

	sur40_tuio = new Sur40_TUIO(server);
	sur40_tuio->run();

	delete sur40_tuio;
	delete server;
	usb_close(sur40);
	return 0;
}
