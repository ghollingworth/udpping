#include <stdlib.h>
#include <string.h>
#include <stdio.h>
#include <unistd.h>
#include <sys/types.h>
#include <sys/socket.h>
#include <sys/queue.h>
#include <sys/time.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <netdb.h>
#include <pthread.h>

/*
** UDP ping service
** This server process is designed to respond to IPV6 UDP messages with a returned UDP message.
** 
** Message contains an integer value representing the number of seconds for the server to wait
** before responding to the ping.  It also contains a value passed from the client device which
** will be returned
*/

#define MAX_DELAY_SECONDS 1024

/* The Ping request packet expected from the client */
struct s_packet {
   int delay;
   int uuid[4];
};

/* The ping table is an array of linked lists containing the following */
struct s_ping_table_entry {
   struct sockaddr_in6 addr;
   int delay;
   int uuid[4];
   SLIST_ENTRY(s_ping_table_entry) list_pointers;
};
SLIST_HEAD(slisthead, s_ping_table_entry);

/* Information to pass to the thread receiving packets from the client */
struct s_threadinfo {
   int sock;
   struct slisthead ping_table[MAX_DELAY_SECONDS];
   int current_position;
};

static void * request_thread(void *threadArgs) {
   struct s_threadinfo * info = (struct s_threadinfo *) threadArgs;
   struct s_packet packet;

   while(1) {
      // Wait for packet from client
      struct sockaddr_in6 client_addr;
      int client_addr_len = sizeof(client_addr);

      int status = recvfrom(info->sock, &packet, sizeof(packet), 0, 
         (struct sockaddr *)&client_addr, &client_addr_len);

		if(status == sizeof(packet)) {
			//char s[256] = {0,};
			//inet_ntop(AF_INET6, &client_addr, s, sizeof(s)-1);

         // Reply to device immediately to complete the connection
         printf("Send immediate reply\n");
         status = sendto(info->sock, &packet, sizeof(packet), 0, (struct sockaddr *) &client_addr, client_addr_len);
         if(status < 0) {
            printf("Failed with error %d\n", status);
            exit(0);
         }

         if(packet.delay > 0)
         {
            struct s_ping_table_entry * new_entry = (struct s_ping_table_entry *) malloc(sizeof(struct s_ping_table_entry));
            if(new_entry != NULL) {
               new_entry->delay = packet.delay;
               memcpy(new_entry->uuid, packet.uuid, sizeof(new_entry->uuid));
               memcpy(&new_entry->addr, &client_addr, sizeof(client_addr));
               int pos = (info->current_position + packet.delay) % MAX_DELAY_SECONDS;
               SLIST_INSERT_HEAD(&info->ping_table[pos], new_entry, list_pointers);
               //printf("Received packet with delay %d, 0x%08x, from %s in slot %d\n", packet.delay, packet.uuid[0], s, pos);
            }
         }
      }
   } 
}

int main()
{
   int sock;
   int status;
   struct sockaddr_in6 sin6_src;
   struct sockaddr_in6 sin6_dest;
   int sin6len;

   sock = socket(PF_INET6, SOCK_DGRAM,0);

   int enable = 1;
   int enable_len = sizeof(enable);
   status = setsockopt(sock, IPPROTO_IPV6, IPV6_RECVERR, &enable, sizeof(enable));

   sin6len = sizeof(struct sockaddr_in6);

   memset(&sin6_dest, 0, sin6len);

   sin6_dest.sin6_port = htons(1111);
   sin6_dest.sin6_family = AF_INET6;
   sin6_dest.sin6_addr = in6addr_any;

   status = bind(sock, (struct sockaddr *)&sin6_dest, sin6len);
   if(-1 == status)
   {
     perror("bind");
     exit(1);
   }

   status = getsockname(sock, (struct sockaddr *)&sin6_dest, &sin6len);
   printf("Port = %d\n", ntohs(sin6_dest.sin6_port));

   pthread_t producer_id;
   struct s_threadinfo request_info = {
      sock,
      {
         SLIST_HEAD_INITIALIZER(head),
      },
      0
   };
   pthread_create(&producer_id, NULL, &request_thread, (void *) &request_info);

   uint8_t vec_buf[4096];
   uint8_t ancillary_buf[4096];
   struct iovec iov = { vec_buf, sizeof(vec_buf) };
   struct sockaddr_storage remote;

   struct msghdr msg = {
      .msg_name = (struct sockaddr *) &remote,
      .msg_namelen = sizeof(remote),
      .msg_iov = &iov,
      .msg_iovlen = 1,
      .msg_flags = 0,
      .msg_control = ancillary_buf,
      .msg_controllen = sizeof(ancillary_buf)
   };

   while(1) {
      struct timeval start, now;
      int elap_us;
      gettimeofday(&start,NULL);
      do {
         status = recvmsg(sock, &msg, MSG_DONTWAIT | MSG_ERRQUEUE);
         if(status > 0) {
            struct cmsghdr *cmsg;
            for(cmsg = CMSG_FIRSTHDR(&msg); cmsg != NULL; cmsg = CMSG_NXTHDR(&msg, cmsg)) {
               if(cmsg->cmsg_level == IPPROTO_IPV6 && cmsg->cmsg_type == IPV6_RECVERR)
               {
                  printf("Error received\n");
               }
            }
         }
         usleep(50000);
         gettimeofday(&now, NULL);
         elap_us = (now.tv_sec - start.tv_sec) * 1000000 + (now.tv_usec - start.tv_usec);
      } while(elap_us < 1000000);
      
      while(!SLIST_EMPTY(&request_info.ping_table[request_info.current_position])) {
         struct s_ping_table_entry * entry = (struct s_ping_table_entry *) SLIST_FIRST(&request_info.ping_table[request_info.current_position]);
         SLIST_REMOVE_HEAD(&request_info.ping_table[request_info.current_position], list_pointers);

         struct s_packet packet;
         packet.delay = entry->delay;
         memcpy(&packet.uuid, &entry->uuid, sizeof(entry->uuid));

         //printf("Sent packet with delay %d, 0x%08x, from slot %d\n", packet.delay, packet.uuid[0], request_info.current_position);
         // Reply to device
         status = sendto(sock, &packet, sizeof(packet), 0, (struct sockaddr *)&entry->addr, sizeof(entry->addr));
         if(status < 0) {
            printf("Failed with error %d\n", status);
            exit(0);
         }

         free(entry);
      }
      request_info.current_position = (request_info.current_position + 1) % MAX_DELAY_SECONDS;
	}

   shutdown(sock, 2);
   close(sock);
   return 0;
}
