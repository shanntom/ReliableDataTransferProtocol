/*Shannon Tom, Michelle Tran
  CS 118 Winter 2017
  Project 2 
  Server Code
*/

#include <stdio.h> 
#include <string.h>
#include <stdlib.h>
#include <arpa/inet.h>
#include <sys/socket.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <sys/select.h>
#include <time.h>
#include <fcntl.h>
#include <unistd.h>

#include "packet.h"

#define BILLION 1E9  //one billion for sec -> ns conversion

#define PACKET_SIZE 1024
#define DATA_SIZE 1010
#define WIND_SIZE 5120 //bytes
#define MAX_SEQ_NUM 30720
#define TIMEOUT 500  // mili seconds

#define TYPE_DATA 1
#define TYPE_SYN 2
#define TYPE_ACK 3
#define TYPE_FIN 4
#define TYPE_SYNACK 5

struct window {
	struct packet pkt;
	struct timespec start_time; 	
	int isAcked;

};


void error(char *msg) {
    perror(msg);
    exit(1);
}


void dataPacket(struct packet* p, int startReadByte, int sequence,  int len, int fd) {
	p->seq = sequence;
	p->length = len;
	p->type = TYPE_DATA;
	memset((char*) p->data ,0,DATA_SIZE);
	int n = read(fd, p->data, len);
	if (n < 0)
		error("Could not read from file\n");
}

void controlPacket(struct packet* p, int type, int sequence){
	p->type = type;
	p->length = 0;
	p->seq = sequence;
}


//////////////////////////////////////////////////////////////////////////////////

int main(int argc, char* argv[]){

 	 //printf("Size of packet %ld\n", sizeof(struct packet));

 	 //Setting initial timeout value
 	struct timespec start, stop; //tv_nsec and tv_usec are longs, even if the structs are different
	struct timeval tv;
	fd_set readfds;
	tv.tv_sec = 0;
	tv.tv_usec = 500000; //5E5 mircoseconds in 500 ms

   // seed rand()
   srand(time(NULL)); 

	int udpSocket, nBytes;
	struct sockaddr_in serverAddr, clientAddr;
  	socklen_t addr_size, client_addr_size;
  	int i, portno;

  	if (argc < 2) {
  		fprintf(stderr,"ERROR, no port provided\n");  //argument number is port
        exit(1);
     }

  	//create UPD socket
  	udpSocket = socket(AF_INET, SOCK_DGRAM, 0);
  	if (udpSocket < 0) 
  		error("ERROR opening UDP socket");

  	//configure address struct
  	memset ((char*) &serverAddr, 0, sizeof(serverAddr));
  	portno = atoi(argv[1]);
  	serverAddr.sin_family = AF_INET;
  	serverAddr.sin_port = htons(portno);
  	serverAddr.sin_addr.s_addr = htonl(INADDR_ANY);

  	//bind socket to a port
  	if (bind(udpSocket, (struct sockaddr *) &serverAddr, sizeof(serverAddr)) <0)
  		error("ERROR on binding");

  	int recvBytes, sentBytes, n;
  	//char buffer[PACKET_SIZE];
	//memset((char*)&buffer,0,PACKET_SIZE);

	while(1){
	  	printf("Waiting for requests...\n");
		socklen_t clientsize = sizeof(clientAddr);

		// Receive SYN 
		struct packet p;
		n = recvfrom(udpSocket, &p, PACKET_SIZE, 0, (struct sockaddr*)&clientAddr, &clientsize);
      	if(n < 0){ 
	       close(udpSocket); 
	       error ("Could not read from socket"); 
      	}
      	if(p.type != TYPE_SYN){
	        printf("Receiving packet %i\n", p.seq);
	        continue;
     	}
        else
        	printf("Receiving packet %i\n", p.seq);

		//Send SYNACK
		struct packet synAckPacket;
		int initSeq = rand() % MAX_SEQ_NUM;
		controlPacket(&synAckPacket, TYPE_SYNACK, initSeq);
		sentBytes = sendto(udpSocket, &synAckPacket , PACKET_SIZE, 0, (struct sockaddr*)&clientAddr, clientsize);
		printf("Sending packet %i %i SYN\n", synAckPacket.seq, WIND_SIZE);
		if(sentBytes < 0){
			close(udpSocket);
			error("Error writing to socket\n");
		}

	    //Receive ACK with filename -> retransmit SYNACK if timeout
	    while(1){
	         FD_ZERO(&readfds);
	         FD_SET(udpSocket, &readfds);
	         tv.tv_usec = 500000; // 500ms is 5E5 us
	         n = select(udpSocket+1, &readfds, NULL, NULL, &tv);
	         if (n == -1){
	            close(udpSocket);
	            error("Error with select\n");
	         }
	         if (n == 0){ //timeout -> retransmit
	            sendto(udpSocket, &synAckPacket , PACKET_SIZE, 0, (struct sockaddr*)&clientAddr, clientsize);
	            printf("Sending packet %i %i SYN Retransmission\n", synAckPacket.seq, WIND_SIZE);
	         }
	         else{
	            n = recvfrom(udpSocket, &p, PACKET_SIZE, 0, (struct sockaddr*)&clientAddr, &clientsize);
	            if(n < 0){ 
	             close(udpSocket); 
	             error ("Could not read from socket"); 
	            }
	            if (p.type == TYPE_ACK){
	               printf("Receiving packet %i\n", p.seq);
	               break;
	            }
	            else //Packet not of type ACK. Ignore.
	               printf("Receiving packet %i\n", p.seq);
	        }
	    }

        //Packet p currently has filename in data portion


		//Open file and find file size
		int fd = open(p.data, O_RDONLY);
		if (fd < 0) {
			printf("Unable to open file '%s'\n", p.data);
			continue;
		 }
		struct stat st;
		fstat(fd,&st);
		off_t fileSize = st.st_size;
		//printf("The filesize is: %i\n", (int) fileSize );

		//Numbers relating to file transfer
		off_t currByte = 0;			//offset of file
		int currSeq = initSeq;			//sequence number of current packet
		off_t totalPackets = fileSize / DATA_SIZE;
		if (fileSize % DATA_SIZE != 0)
			totalPackets++;

		// Initialize window
		int maxPacketsInWindow = WIND_SIZE / PACKET_SIZE;
		struct window wind[maxPacketsInWindow];
		int numPackets = 0;   		// number of packets in the window, index of next open window
		int curPacket = 1;
		off_t ackedPackets = 0;	 	// number of packets ACKed by client


		//send data packets
		while (ackedPackets < totalPackets) {

			// loop: send packets if there is empty space in the window
			while (numPackets < maxPacketsInWindow && currByte < fileSize) {
				//calcuate length of data in packet
				int len = DATA_SIZE;
				if (currByte + DATA_SIZE > fileSize)
					len = fileSize - currByte;

				//create data packet, add to window
				dataPacket(&p, currByte, currSeq, len, fd);
				currByte += len; 
				currSeq += len; 
				if (currSeq > MAX_SEQ_NUM)
					currSeq -= MAX_SEQ_NUM;
				p.nextSeq = currSeq; //sets current sequence number to the next sequence number for the next packet
				wind[numPackets].pkt = p;
				wind[numPackets].isAcked = 0;

				//send packet to client
				sentBytes = sendto(udpSocket, &p , PACKET_SIZE, 0, (struct sockaddr*)&clientAddr, clientsize);
				printf("Sending packet %i %i\n",p.seq, WIND_SIZE);
				if(sentBytes < 0){
					close(fd);
					close(udpSocket);
				    error("Error writing to socket\n");
				}

				//Set the beginning time of when packet is sent. 
				n = clock_gettime(CLOCK_MONOTONIC_RAW, &start);
				if(n != 0){
					close(fd);
					close(udpSocket);
				    error("Error getting start time.\n");
				}

				wind[numPackets].start_time = start;

				curPacket++;
				numPackets++;
			}

			//Check current time to check against wind[0].pkttv to see if there is a need to update tv. 
			n = clock_gettime(CLOCK_MONOTONIC_RAW, &stop);
			if(n != 0){
				close(fd);
				close(udpSocket);
			    error("Error getting stop time.\n");
			}

			//If already over the timeout interval, then set tv_usec to 0. Select will return immediately as timed out. 
			//Else: set tv_usec to the difference of all three values to get remaining time out countdown.
			long sdiff = stop.tv_sec - wind[0].start_time.tv_sec;
			long nsdiff = stop.tv_nsec - wind[0].start_time.tv_nsec;
			long totDiffNS = sdiff * BILLION + nsdiff;
			
			if(totDiffNS > 500000000){   // 500ms = 5E8 ns 
				tv.tv_usec = 0;
			}
			else{
				tv.tv_usec = 500000 - totDiffNS/1000; 
			}
			
	
			//select() for timer
			FD_ZERO(&readfds);
			FD_SET(udpSocket, &readfds);

			n = select(udpSocket+1, &readfds, NULL, NULL, &tv);
			if(n == -1){  // error on select 
				close(fd);
				close(udpSocket);
				error("Error with select\n");
			}
			if (n == 0) {  //timeout occured
				//resend packet to client
				sentBytes = sendto(udpSocket, &wind[0].pkt, PACKET_SIZE, 0, (struct sockaddr*)&clientAddr, clientsize);
				printf("Sending packet %i %i Retransmission\n",wind[0].pkt.seq, WIND_SIZE);
				if(sentBytes < 0){
					close(fd);
					close(udpSocket);
				    error("Error writing to socket\n");
				}

				n = clock_gettime(CLOCK_MONOTONIC_RAW, &wind[0].start_time);
				if(n != 0){
					close(fd);	close(udpSocket);
				    error("Error getting start time.\n");
				}
			}
			//Information on sockect-> read ACKs
			else {
					struct packet ackPacket;
					recvBytes = recvfrom(udpSocket, &ackPacket, PACKET_SIZE, 0, (struct sockaddr*)&clientAddr, &clientsize);
					if (recvBytes < 0) {
						close(fd);
						close(udpSocket);
					    error("Error reading from socket\n");
					}
					printf("Receiving packet %i\n", ackPacket.seq);

					//Update window: check to see which packet was ACKed
					int i = 0;
					while (i < numPackets) {
						if (wind[i].pkt.seq == ackPacket.seq){
							wind[i].isAcked = 1;
							ackedPackets++;
							break;
						}
						i++;
					}
					if (i == numPackets) //Sequence not in window - Ignored
						printf("Receiving packet %i\n", ackPacket.seq);

					//Update window: remove packets from window if consecutively ACKed
					while (wind[0].isAcked == 1 && numPackets > 0) { //first packet in window has been ACKed
						int a = 0;
						for (a; a < numPackets-1; a++){
							wind[a] = wind[a+1];
						}
						numPackets--;
					}

			}
		}


		//send FIN: 
		struct packet finPacket;
		controlPacket(&finPacket, TYPE_FIN, currSeq);
		sentBytes = sendto(udpSocket, &finPacket , PACKET_SIZE, 0, (struct sockaddr*)&clientAddr, clientsize);
      	printf("Sending packet %i %i FIN\n", finPacket.seq, WIND_SIZE);
      	if(sentBytes < 0){
        	close(fd);  close(udpSocket);
        	error("Error writing to socket\n");
    }

    //Receive ACK : retransmit FIN if timeout
    while(1) {
         FD_ZERO(&readfds);
         FD_SET(udpSocket, &readfds);
         tv.tv_usec = 500000; // 500ms is 5E5 us
         n = select(udpSocket+1, &readfds, NULL, NULL, &tv);
         if (n == -1){
            close(udpSocket);
            error("Error with select\n");
         }
         if (n == 0) {// timeout -> retransmit
            sendto(udpSocket, &finPacket , PACKET_SIZE, 0, (struct sockaddr*)&clientAddr, clientsize);
            printf("Sending packet %i %i FIN Retransmission\n", finPacket.seq, WIND_SIZE);
         }
         else{  // ACK
            n = recvfrom(udpSocket, &p, PACKET_SIZE, 0, (struct sockaddr*)&clientAddr, &clientsize);
            if(n < 0){ 
             close(udpSocket); 
             error ("Could not read from socket"); 
            }
            if (p.type == TYPE_FIN){
               printf("Receiving packet %i\n", p.seq);
               break;
            }
            else //Not of type FIN ignored. 
               printf("Receiving packet %i\n", p.seq);
         }
    }



		printf("Transfer of file has been completed\n");
		close(fd);
 	}


  	
	close(udpSocket);
	return 0;

}
