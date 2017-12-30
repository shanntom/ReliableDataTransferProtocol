/*Shannon Tom, Michelle Tran
  CS 118 Winter 2017
  Project 2 
  Client Code
*/

#include <unistd.h>
#include <stdio.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <string.h>
#include <stdlib.h>
#include <arpa/inet.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <sys/select.h>
#include <fcntl.h>
#include <time.h>

#include "packet.h"

#define BUFSIZE 1024
#define DATA_SIZE 1010
#define MAX_SEQ_NUM 30720

#define TYPE_DATA 1
#define TYPE_SYN 2
#define TYPE_ACK 3
#define TYPE_FIN 4
#define TYPE_SYNACK 5




void error(char *msg) {
    perror(msg);
    exit(1);
}

//create packet with no data in it: for ACK and SYN/ FIN packets
void createControlPacket(struct packet* p, int type, int sequence) {
  p->seq = sequence;
  p->type = type;
  p->length = 0;
}

void createPacket (struct packet* p, short type, int sequence, int len, char* d) {
  p->seq = sequence;
  p->nextSeq = sequence + DATA_SIZE;
  if (p->nextSeq > MAX_SEQ_NUM)
    p->nextSeq -= MAX_SEQ_NUM;
  p->length = len;
  p->type = type;
  strcpy(p->data, d);
}

int main(int argc, char** argv){
  int sockfd, portno, n;
  char filename[BUFSIZE];
  struct sockaddr_in serverAddr;
  socklen_t addr_size;
  fd_set rfds;
  struct timeval tv;
  tv.tv_sec = 0;
  tv.tv_usec = 500000; // 500ms is 5E5 us

  strcpy(filename,argv[3]);
  portno = atoi(argv[2]);

  // seed rand()
  srand(time(NULL)); 

  /*Create UDP socket*/
  sockfd = socket(AF_INET, SOCK_DGRAM, 0);

  /*Configure settings in address struct*/
  serverAddr.sin_family = AF_INET;
  serverAddr.sin_port = htons(portno);
  serverAddr.sin_addr.s_addr = inet_addr(argv[1]);
  memset(serverAddr.sin_zero, '\0', sizeof(serverAddr.sin_zero));  

  /*Initialize size variable to be used later on*/
  int size = sizeof(serverAddr);



  // HANDSHAKE

  // send SYN
  int initSeq = rand() % MAX_SEQ_NUM;
  struct packet synPacket;
  createControlPacket(&synPacket, TYPE_SYN, initSeq);
  n = sendto(sockfd, &synPacket , sizeof(struct packet) ,0,(struct sockaddr*)&serverAddr,size);
  printf("Sending packet SYN\n");
  if(n < 0){ 
    close(sockfd); 
    error ("Could not write to socket"); 
  }

  // Receive SYNACK, retransmit SYN if timeout
  struct packet p;
  int initDataSeq;
  while(1) {
    FD_ZERO(&rfds);
    FD_SET(sockfd, &rfds);
    tv.tv_usec = 500000; // 500ms is 5E5 us
    n = select(sockfd + 1, &rfds, NULL, NULL, &tv); 
    if (n == -1) {
      close(sockfd);
      error("Select"); 
    }
    if (n == 0) {  //timeout -> retransmit syn
      sendto(sockfd, &synPacket , sizeof(struct packet) ,0,(struct sockaddr*)&serverAddr,size);
      printf("Sending packet SYN Retransmission\n");
    }
    else { //a packet is ready to be received
      n = recvfrom(sockfd, &p, BUFSIZE, 0, (struct sockaddr *)&serverAddr, &size);
      if(n < 0){ 
       close(sockfd); 
       error ("Could not read from socket"); 
      }
      if (p.type == TYPE_SYNACK) {
      	initDataSeq = p.seq; 
        printf("Receiving packet %i\n", p.seq);
        break;
      }
      else{  // if not type SYNACK, will wait for SYNACK PACKET by enterming loop again
        printf("Receiving packet not of type SYNACK %i Ignored\n", p.seq);
      }

    }
  }


  // Send ACK : payload contains filename
  struct packet namePacket;
  createPacket(&namePacket, TYPE_ACK, p.seq, strlen(filename), filename);
  n = sendto(sockfd, &namePacket , sizeof(struct packet) ,0,(struct sockaddr*)&serverAddr,size);
  if(n < 0){ 
    close(sockfd); 
    error ("Could not write to socket"); 
  }
  printf("Sending packet %i\n", namePacket.seq);


  
  //open file where data will be written to
  int fd = open("received.data", O_WRONLY | O_CREAT | O_TRUNC, 0777);
  if (fd < 0)
  error("Could not create file 'received.data'");  


  int first = 1; //Indicates first ordered packet
  int connected = 0; //Indicates that the first data packet has been received 
  int numOutofOrder = 0; //Indexing for the out of order packet buffer.
  int numACKed = 0; //Indexing for ack buffer. 
  int duplicate = 0; //Indicates whether the packet has already been received or not. 
  struct packet lastOrderedp;
  struct packet ackPacket;
  struct packet pktBuffer[BUFSIZE];
  struct packet ackBuffer[BUFSIZE];
  
  while(1){

  	//Set timer for ACK and if a data packet has been received, then set connected to 1. 
  	if(connected == 0){
  		// Receive 1st data packet, retransmit namePacket (ACK) if timeout
	    FD_ZERO(&rfds);
	    FD_SET(sockfd, &rfds);
	    tv.tv_usec = 500000; // 500ms is 5E5 us
	    n = select(sockfd + 1, &rfds, NULL, NULL, &tv); 
	    if (n == -1) {
	      	close(sockfd);
	    	error("Error with select\n"); 
	    }
	    if (n == 0) {  //timeout -> retransmit namePacket
		    sendto(sockfd, &namePacket , sizeof(struct packet) ,0,(struct sockaddr*)&serverAddr,size);
		    printf("Sending packet %i Retransmission\n", namePacket.seq);
	    }
	    else {  //receive 1st data packet
			n = recvfrom(sockfd, &p, BUFSIZE, 0, (struct sockaddr *)&serverAddr, &size);
			if(n < 0){ 
				close(sockfd); 
				error ("Could not read from socket"); 
			}
			if (p.type == TYPE_DATA){
				connected = 1; 
				printf("Receiving packet %i\n", p.seq);
			}
			else  // if not type DATA, will wait for DATA PACKET by entering loop again
				printf("Receiving packet %i\n", p.seq);
	    }
  	}
  	else{
  		//If connect == 1 for the packets after 1st data packet. 
	    //Receive data packet from server
	    n = recvfrom(sockfd, &p, BUFSIZE, 0, (struct sockaddr *)&serverAddr, &size);
	    if(n < 0){ 
	      close(sockfd); close (fd);
	      error ("Could not read from socket"); 
	    }

	    if (p.type == TYPE_FIN) 
	      break;
	    if (p.type != TYPE_DATA){
	      printf("Receiving packet %i\n", p.seq);
	      continue;
	    }

	    printf("Receiving packet %i\n",p.seq);
  	}

  	//Need this to check immediately after if we've connected to the server to handle 1st received packet. 
  	if(connected == 1){
    //If received packet is not the first but is in order with the expected next packet, then write to file. 
    //Else: write to buffer, where numOutofOrder indexes how many packets are out of order.
        duplicate = 0;
        if(numACKed > 0){
            for(int i = 0; i < numACKed; i++){
                if(p.seq == ackBuffer[i].seq){
                    duplicate = 1;
                }
            }
        }

      if(duplicate == 0){
	    //lastOrderedp keeps track of which was the last packet to be written to file. If the incoming file matches 
	    //the expected next file of lastOrderedp, then write to file and check buffer against current p. 
	    if((first == 1 && p.seq == initDataSeq) || (first == 0 && lastOrderedp.nextSeq == p.seq)){
			//printf("Current pkt = %d, next pkt = %d\n",p.seq,p.nextSeq);
			n = write(fd, p.data, p.length);
			if (n < 0){
				close(sockfd); close(fd);
				error("Could not write to 'received.data'");
			}

			lastOrderedp = p;

			//Mark that it's not the first packet anymore. 
			if(p.seq == initDataSeq)
				first = 0;

			//TODO: check against buffer, update lastOrderedp
			if(numOutofOrder != 0){
			  int i = 0;
				while(i < numOutofOrder){
					if(lastOrderedp.nextSeq == pktBuffer[i].seq){
						n = write(fd, pktBuffer[i].data, pktBuffer[i].length);
							if (n < 0){
							close(sockfd); close(fd);
							error("Could not write to 'received.data'");
							}

						lastOrderedp = pktBuffer[i];

						for(int q = i+1; q < numOutofOrder; q++){
							pktBuffer[i] = pktBuffer[q];
							i++;
						}

						i = 0;
						numOutofOrder--;
						//printf("Updated pktBuffer. numOutofOrder = %d\n",numOutofOrder);
					}
					else
					  	i++;
				}
			}
		}
	    else{ //if packet out of order, buffer it. 
			pktBuffer[numOutofOrder] = p;
			numOutofOrder++;
			//printf("Buffering pkt %d, numOutofOrder = %d\n",p.seq,numOutofOrder);
	    }
      }
	}

    createControlPacket(&ackPacket, TYPE_ACK, p.seq);
    n = sendto(sockfd, &ackPacket , sizeof(struct packet) ,0,(struct sockaddr*)&serverAddr,size);
    if(n < 0){ 
    	close(sockfd); close (fd);
    	error ("Could not write to socket\n"); 
    }
    printf("Sending packet %i\n", ackPacket.seq);
    ackBuffer[numACKed] = ackPacket;
    numACKed++;
    if(numACKed == BUFSIZE){
      int q = BUFSIZE/2;
      for(int i = 0; i < BUFSIZE/2; i++){
        ackBuffer[i] = ackBuffer[q];
        q++;
      }
      numACKed = BUFSIZE/2;
    }

  }

  // TEARDOWN
  // packet p is the recieved FIN packet
  // Enter Timed wait = 2 * RTO = 1s
  tv.tv_sec = 1;
  tv.tv_usec = 0; // 500ms is 5E5 us
  struct packet finAck;
  createControlPacket(&finAck, TYPE_FIN, p.seq);
  n = sendto(sockfd, &finAck, sizeof(struct packet), 0, (struct sockaddr*)&serverAddr, size);
  printf("Sending packet %i FIN\n", finAck.seq);  
  if(n < 0){ 
    close(sockfd); close (fd);
    error ("Could not write to socket\n"); 
  }

  while(1) {
      FD_ZERO(&rfds);
      FD_SET(sockfd, &rfds);
      n = select(sockfd + 1, &rfds, NULL, NULL, &tv); 
      if (n == -1) {
        close(sockfd);
        error("Error with select\n"); 
      }
      if (n == 0) {  // end of timed wait -> close connection
        break; 
      }
      else { //FIN packet was recieved again -> resend FINACK
        n = recvfrom(sockfd, &p, BUFSIZE, 0, (struct sockaddr *)&serverAddr, &size);
        if(n < 0){ 
         close(sockfd); 
         error ("Could not read from socket\n"); 
        }
        printf("Receiving packet %i\n", p.seq);

        sendto(sockfd, &finAck , sizeof(struct packet) ,0,(struct sockaddr*)&serverAddr,size);
        printf("Sending packet %i FIN Retransmission\n", finAck.seq);


      }
  }


  
  close (fd);
  close (sockfd);
  return 0;
}
