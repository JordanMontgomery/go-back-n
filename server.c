/* server.c - go-back-n server implementation in C
 * by Elijah Jordan Montgomery <elijah.montgomery@uky.edu>
 * based on code by Kenneth Calvert
 *
 * This implements a go-back-n server that implements reliable data 
 * transfer over UDP using the go-back-n ARQ with variable chunk size
 *
 * for debug purposes, a loss rate can also be specified
 * compile with "gcc -o server server.c"
 * tested on UKY CS Multilab 
 */
#include <stdio.h>		/* for printf() and fprintf() */
#include <sys/socket.h>		/* for socket() and bind() */
#include <arpa/inet.h>		/* for sockaddr_in and inet_ntoa() */
#include <stdlib.h>		/* for atoi() and exit() */
#include <string.h>		/* for memset() */
#include <unistd.h>		/* for close() */
#include <errno.h>
#include <memory.h>
#include <signal.h>
#include "gbnpacket.c"		/* defines go-back-n packet structure */

#define ECHOMAX 255		/* Longest string to echo */

void DieWithError (char *errorMessage);	/* External error handling function */
void CatchAlarm (int ignored);

int
main (int argc, char *argv[])
{
  char buffer[8193];		/* buffer with room for null byte */
  buffer[8192] = '\0';		/* null terminate the buffer */
  int sock;			/* Socket */
  struct sockaddr_in gbnServAddr;	/* Local address */
  struct sockaddr_in gbnClntAddr;	/* Client address */
  unsigned int cliAddrLen;	/* Length of incoming message */
  unsigned short gbnServPort;	/* Server port */
  int recvMsgSize;		/* Size of received message */
  int packet_rcvd = -1;		/* highest packet successfully received */
  struct sigaction myAction;
  double lossRate;		/* loss rate as a decimal, 50% = input .50 */


  bzero (buffer, 8192);		/* zero out the buffer */

  if (argc < 3 || argc > 4)		/* Test for correct number of parameters */
    {
      fprintf (stderr, "Usage:  %s <UDP SERVER PORT> <CHUNK SIZE> [<LOSS RATE>]\n", argv[0]);
      exit (1);
    }

  gbnServPort = atoi (argv[1]);	/* First arg:  local port */
  int chunkSize = atoi(argv[2]);	/* num bytes per chunk */
  if(argc == 4)
	lossRate = atof(argv[3]);	/* loss rate as percentage i.e. 50% = .50 */
  else
	lossRate = 0.0;			/*default to 0% intentional loss rate */

  srand48(123456789);/* seed the pseudorandom number generator */
  /* Create socket for sending/receiving datagrams */
  if ((sock = socket (PF_INET, SOCK_DGRAM, IPPROTO_UDP)) < 0)
    DieWithError ("socket() failed");

  /* Construct local address structure */
  memset (&gbnServAddr, 0, sizeof (gbnServAddr));	/* Zero out structure */
  gbnServAddr.sin_family = AF_INET;	/* Internet address family */
  gbnServAddr.sin_addr.s_addr = htonl (INADDR_ANY);	/* Any incoming interface */
  gbnServAddr.sin_port = htons (gbnServPort);	/* Local port */

  /* Bind to the local address */
  if (bind (sock, (struct sockaddr *) &gbnServAddr, sizeof (gbnServAddr)) <
      0)
    DieWithError ("bind() failed");
  myAction.sa_handler = CatchAlarm;
  if (sigfillset (&myAction.sa_mask) < 0)/*setup everything for the timer - only used for teardown */
    DieWithError ("sigfillset failed");
  myAction.sa_flags = 0;
  if (sigaction (SIGALRM, &myAction, 0) < 0)
    DieWithError ("sigaction failed for SIGALRM");
  for (;;)			/* Run forever */
    {
      /* set the size of the in-out parameter */
      cliAddrLen = sizeof (gbnClntAddr);
      struct gbnpacket currPacket; /* current packet that we're working with */
      memset(&currPacket, 0, sizeof(currPacket));
      /* Block until receive message from a client */
      recvMsgSize = recvfrom (sock, &currPacket, sizeof (currPacket), 0, /* receive GBN packet */
			      (struct sockaddr *) &gbnClntAddr, &cliAddrLen);
      currPacket.type = ntohl (currPacket.type);
      currPacket.length = ntohl (currPacket.length); /* convert from network to host byte order */
      currPacket.seq_no = ntohl (currPacket.seq_no);
      if (currPacket.type == 4) /* tear-down message */
	{
	  printf ("%s\n", buffer);
	  struct gbnpacket ackmsg;
	  ackmsg.type = htonl(8);
	  ackmsg.seq_no = htonl(0);/*convert to network endianness */
	  ackmsg.length = htonl(0);
	  if (sendto
	      (sock, &ackmsg, sizeof (ackmsg), 0,
	       (struct sockaddr *) &gbnClntAddr,
	       cliAddrLen) != sizeof (ackmsg))
	    {
	      DieWithError ("Error sending tear-down ack"); /* not a big deal-data already rcvd */
	    }
	  alarm (7);
	  while (1)
	    {
	      while ((recvfrom (sock, &currPacket, sizeof (int)*3+chunkSize, 0,
				(struct sockaddr *) &gbnClntAddr,
				&cliAddrLen))<0)
		{
		  if (errno == EINTR)	/* Alarm went off  */
		    {
		      /* never reached */
		      exit(0);
		    }
		  else
		    ;
		}
	      if (ntohl(currPacket.type) == 4) /* respond to more teardown messages */
		{
		  ackmsg.type = htonl(8);
		  ackmsg.seq_no = htonl(0);/* convert to network endianness */
		  ackmsg.length = htonl(0);
		  if (sendto
		      (sock, &ackmsg, sizeof (ackmsg), 0, /* send teardown ack */
		       (struct sockaddr *) &gbnClntAddr,
		       cliAddrLen) != sizeof (ackmsg))
		    {
		      DieWithError ("Error sending tear-down ack");
		    }


		}


	    }
	  DieWithError ("recvfrom() failed");
	}
      else
	{
	  if(lossRate > drand48())
		continue; /* drop packet - for testing/debug purposes */
	  printf ("---- RECEIVE PACKET %d length %d\n", currPacket.seq_no, currPacket.length);

	  /* Send ack and store in buffer */
	  if (currPacket.seq_no == packet_rcvd + 1)
	    {
	      packet_rcvd++;
	      int buff_offset = chunkSize * currPacket.seq_no;
	      memcpy (&buffer[buff_offset], currPacket.data, /* copy packet data to buffer */
		      currPacket.length);
	    }
	  printf ("---- SEND ACK %d\n", packet_rcvd);
	  struct gbnpacket currAck; /* ack packet */
	  currAck.type = htonl (2); /*convert to network byte order */
	  currAck.seq_no = htonl (packet_rcvd);
	  currAck.length = htonl(0);
	  if (sendto (sock, &currAck, sizeof (currAck), 0, /* send ack */
		      (struct sockaddr *) &gbnClntAddr,
		      cliAddrLen) != sizeof (currAck))
	    DieWithError
	      ("sendto() sent a different number of bytes than expected");
	}
    }
  /* NOT REACHED */
}

void
DieWithError (char *errorMessage)
{
  perror (errorMessage);
  exit (1);
}

void
CatchAlarm (int ignored) /* just exit when encounter timeout */
{
  exit(0);
}
