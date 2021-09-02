#include <unistd.h>
#include <stdio.h>
#include <stdlib.h>
#include <sys/socket.h>
#include <sys/types.h>
#include <linux/if_ether.h>
#include <linux/in.h>

#define BUFFER_MAX 2048

typedef int int32;
typedef unsigned int u_int32;
typedef unsigned char u_char;
typedef unsigned short u_short;

//参考内核 struct ethhdr 定义 (include/uapi/linux/if_ether.h）
typedef struct eth_hdr {
	 char h_dest[6];	
	 char h_source[6];
	 short h_proto;
}__attribute__((packed));

//参考内核 struct iphdr 定义（include/uapi/linux/ip.h）
typedef struct iphdr{
	#ifdef __LITTLE_ENDIAN_BIFIELD
		u_char ip_len:4, ip_ver:4;
	#else
		u_char ip_ver:4, ip_len:4;
	#endif
	u_char  tos;
	u_short total_len;
	u_short id;
	u_short flags_off;
	u_char  ttl;
	u_char  protocol;
	u_short check;
	u_int32 saddr;
	u_int32 daddr;
}__attribute__((packed));

int main(int argc, char *argv[])
{
	int  sock;
	char buffer[BUFFER_MAX];
	int len;

	struct eth_hdr *mac_hdr; 
	struct iphdr *ip_hdr;
	char* p;

	if( (sock = socket(PF_PACKET, SOCK_RAW, htons(ETH_P_ALL))) < 0 ){
		printf("Create socket error.\n");
		exit(0);
	}

	while(1){
		len = recvfrom(sock, buffer, BUFFER_MAX, 0, NULL, NULL);
		if (len < 46) {
			printf("Catch packet length error.\n" );
			close(sock);
			exit(0);
		}
		
		printf("截获内容长度 %d\n", len);

		mac_hdr = buffer;	
		ip_hdr = buffer + sizeof(struct ethhdr);

		printf("源 MAC:%X:%X:%X:%X:%X:%X",
				(u_char)mac_hdr->h_source[0],
				(u_char)mac_hdr->h_source[1],
				(u_char)mac_hdr->h_source[2],
				(u_char)mac_hdr->h_source[3],
				(u_char)mac_hdr->h_source[4],
				(u_char)mac_hdr->h_source[5]
			);

		printf(" ==> 目的 MAC:%X:%X:%X:%X:%X\n",
				(u_char)mac_hdr->h_dest[0],
				(u_char)mac_hdr->h_dest[1],
				(u_char)mac_hdr->h_dest[2],
				(u_char)mac_hdr->h_dest[3],
				(u_char)mac_hdr->h_dest[4],
				(u_char)mac_hdr->h_dest[5]
			);

		p = (char*)&ip_hdr->saddr;
		printf("源 IP: %d.%d.%d.%d",
				(u_char)p[0], 
				(u_char)p[1], 
				(u_char)p[2],
				(u_char)p[3]
			);

		p = (char*)&ip_hdr->daddr;
		printf(" ==> 目的 IP: %d.%d.%d.%d\n",
				(u_char)p[0], 
				(u_char)p[1], 
				(u_char)p[2],
				(u_char)p[3]
			);

	printf("协议类型：");
		switch(ip_hdr->protocol) {
			 case IPPROTO_ICMP: 
				  printf("ICMP"); 
				  break;
			case IPPROTO_IGMP: 
				printf("IGMP"); 
				break;
			case IPPROTO_IPIP: 
				printf("IPIP"); 
				break;
			case IPPROTO_TCP:  
				printf("TCP"); 
				break;
			case IPPROTO_UDP:  
				printf("UDP"); 
				break;
			case IPPROTO_RAW: 
				printf("RAW"); 
				break;
			default: 
				printf("Unknown type"); 
				break;
		}

		printf("\n\n");
	}

	close(sock);
	return 0;
}
