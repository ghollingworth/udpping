#include <stdio.h>
#include <unistd.h>
#include <time.h>
#include <fcntl.h>
#include <sys/types.h>
#include <sys/socket.h>
#include <sys/uio.h>
#include <netinet/in.h>
#include <netinet/icmp6.h>
#include <arpa/inet.h>
#include <netdb.h>
#include <linux/errqueue.h>
#include <stdlib.h>
#include <string.h>
#include <errno.h>

#define REPEAT_TIMES 2
   
int ping_test(int delay, int repeat_times, int s_udp, struct sockaddr_in6 * dest, int addrlen)
{
   struct s_packet {
      int delay;
      int uuid[4];
   } rx_packet = { 
         .delay = delay,
         .uuid = 0x12345678 
      };

   int status;
   printf("Test %d: ", delay);
   fflush(stdout);

   // Repeat on failure in case a packet got lost
   for(int t = 0; t < repeat_times; t++) {
      // Send ping request packet to server
      status = sendto(s_udp, &rx_packet, sizeof(rx_packet), 0, (struct sockaddr *) dest, addrlen);
      if(status < 0) {
               printf("Failed with error %d\n", status);
               exit(0);
      }

      int start_time = time(NULL);
      // The server always immediately responds with a single packet, it then responds again after
      // delay seconds
      int initial_packet = 1;

      while(time(NULL) < (start_time + delay + 3)) {
         struct sockaddr_in6 src;
         int srclen;

         // Response should happen delay seconds later
         status = recvfrom(s_udp, &rx_packet, sizeof(rx_packet), 0, 
               (struct sockaddr *) &src, &srclen);
         
         if(status > 0) {
            printf("P%d", rx_packet.delay); fflush(stdout);
            if(initial_packet && rx_packet.delay == delay) {
               initial_packet = 0;
            } else {
               char s[256] = {0,};
               printf("O\n");
               inet_ntop(AF_INET6, &src.sin6_addr, s, sizeof(s)-1);
               return 1;
            }
         }

         #if 0
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

         status = recvmsg(s_udp, &msg, MSG_DONTWAIT | MSG_ERRQUEUE);
         if(status > 0) {
            struct cmsghdr *cmsg;

            for(cmsg = CMSG_FIRSTHDR(&msg); cmsg != NULL; cmsg = CMSG_NXTHDR(&msg, cmsg)) {
               if(cmsg->cmsg_level == IPPROTO_IPV6 && cmsg->cmsg_type == IPV6_RECVERR)
               {
                  printf("E\n");
                  return 0;
               }
            }
         }
         #endif

         usleep(100000);
      }

      // If failed, then retry just in case packet was lost
      printf("X");
      fflush(stdout);

   }
   printf("\n");

   return 0;
}

int main(int argc, char *argv[])
{
   int s_udp;
   int status;
   struct sockaddr_in6 sin6_src;
   struct sockaddr_in6 sin6_dest;
   int sin6len;

   s_udp = socket(AF_INET6, SOCK_DGRAM, 0);

   if(fcntl(s_udp, F_SETFL, fcntl(s_udp, F_GETFL) | O_NONBLOCK) < 0) {
      printf("Failed to 1set non-blocking on socket\n");
      exit(-1);
   }

   int enable = 1;
   int enable_len = sizeof(enable);
   status = setsockopt(s_udp, IPPROTO_IPV6, IPV6_RECVERR, &enable, sizeof(enable));

   sin6len = sizeof(struct sockaddr_in6);

   memset(&sin6_src, 0, sin6len);

   /* just use the first address returned in the structure */

   sin6_src.sin6_port = htons(0);
   sin6_src.sin6_family = AF_INET6;
   sin6_src.sin6_addr = in6addr_any;

   status = bind(s_udp, (struct sockaddr *)&sin6_src, sin6len);
   if(-1 == status)
     perror("bind"), exit(1);

   sin6_src.sin6_port = htons(0);
   sin6_src.sin6_family = AF_INET6;
   sin6_src.sin6_addr = in6addr_any;

   status = getsockname(s_udp, (struct sockaddr *)&sin6_src, &sin6len);

   printf("Port = %d\n", ntohs(sin6_src.sin6_port));

   sin6_dest.sin6_port = htons(1111);
   sin6_dest.sin6_family = AF_INET6;
   status = inet_pton(AF_INET6, "2a00:1098:8:185::1", &sin6_dest.sin6_addr );
   if(status != 1) {
      printf("Failed to convert destination IPV6 addr\n");
      exit(0);
   }

   if(argc > 1) {
      int delay = atoi(argv[1]);
      printf("Trying ping of delay %d\n", delay);
      ping_test(delay, REPEAT_TIMES, s_udp, &sin6_dest, sin6len);
      exit(0);
   }

   int max_delay = 0;
   // double the delay each time to find the fail point start with 1<<6 = 32 seconds
   int delay_log2 = 1;
   do {
      if(ping_test(1 << delay_log2, REPEAT_TIMES, s_udp, &sin6_dest, sin6len) == 1) {
         max_delay = 1 << delay_log2;
         delay_log2 ++;
      }
      else
         break;
   }
   while(1);

   // Now start at 1<<(delay_log2-1) and set the next lower bit, if it passes keep the bit
   delay_log2--;
   int delay = 1 << delay_log2;
   delay_log2--;
   while(delay_log2 >= 0) {
      if(ping_test(delay | 1 << delay_log2, REPEAT_TIMES, s_udp, &sin6_dest, sin6len) == 1) {
         if(delay | 1 << delay_log2 > max_delay)
            max_delay = delay | 1 << delay_log2;
         delay |= 1 << delay_log2;
      }
      delay_log2--;
   }

   printf("Maximum server delay is %d seconds\n", max_delay);
 
   shutdown(s_udp, 2);
   close(s_udp);
   return 0;
}
