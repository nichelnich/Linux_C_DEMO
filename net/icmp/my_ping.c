/***********************************************************
 ***********************************************************/
#include <stdio.h>
#include <signal.h>
#include <arpa/inet.h>
#include <sys/types.h>
#include <sys/socket.h>
#include <sys/time.h>
#include <unistd.h>
#include <netinet/in.h>
#include <netinet/ip.h>
#include <netinet/ip_icmp.h>
#include <netdb.h>
#include <setjmp.h>
#include <errno.h>
#include <time.h>
#include <string.h>


#define MAX_WAIT_TIME   3
#define PACKET_DATALEN  56
#define PACKET_SIZE     4096
#define RECV_TIMEOUT 3



struct ping_result {
	int ttl;
	double rtt;
};

/*两个timespec结构相减*/
void tv_sub(struct timespec *out, struct timespec *in)
{       if( (out->tv_nsec-=in->tv_nsec)<0)
        {       --out->tv_sec;
                out->tv_nsec+=1000000000;
        }
        out->tv_sec-=in->tv_sec;
}


/*校验和算法*/
unsigned short cal_chksum(unsigned short *addr,int len)
{       int nleft=len;
        int sum=0;
        unsigned short *w=addr;
        unsigned short answer=0;
         
/*把ICMP报头二进制数据以2字节为单位累加起来*/
        while(nleft>1)
        {       sum+=*w++;
                nleft-=2;
        }
        /*若ICMP报头为奇数个字节，会剩下最后一字节。把最后一个字节视为一个2字节数据的高字节，这个2字节数据的低字节为0，继续累加*/
        if( nleft==1)
        {       *(unsigned char *)(&answer)=*(unsigned char *)w;
                sum+=answer;
        }
        sum=(sum>>16)+(sum&0xffff);
        sum+=(sum>>16);
        answer=~sum;
        return answer;
}
/*设置ICMP报头*/
int pack(int pack_no, char *sendpacket)
{       int packsize;
        struct icmp *icmp;
        struct timeval *tval;
        icmp=(struct icmp*)sendpacket;
        icmp->icmp_type=ICMP_ECHO;
        icmp->icmp_code=0;
        icmp->icmp_cksum=0;
        icmp->icmp_seq=pack_no;
        icmp->icmp_id=getpid();
        packsize=8 + PACKET_DATALEN;
        tval= (struct timeval *)icmp->icmp_data;
        gettimeofday(tval,NULL);    /*记录发送时间*/
        icmp->icmp_cksum=cal_chksum( (unsigned short *)icmp,packsize); /*校验算法*/
        return packsize;
}
/*剥去ICMP报头*/
int unpack(char *buf,int len, struct ping_result *result)
{       int iphdrlen;
        struct ip *ip;
        struct icmp *icmp;
   
        ip=(struct ip *)buf;
        iphdrlen=ip->ip_hl<<2;    /*求ip报头长度,即ip报头的长度标志乘4*/
        icmp=(struct icmp *)(buf+iphdrlen);  /*越过ip报头,指向ICMP报头*/
        len-=iphdrlen;            /*ICMP报头及ICMP数据报的总长度*/
        if( len<8)                /*小于ICMP报头长度则不合理*/
        {       printf("ICMP packets\'s length is less than 8\n");
                return -1;
        }
        /*确保所接收的是我所发的的ICMP的回应*/
        if( (icmp->icmp_type==ICMP_ECHOREPLY) && (icmp->icmp_id==getpid()) )
        {
			result->ttl = ip->ip_ttl;
 			return 0;
        }
        else    
			return -1;
}
/* ret: 0 lost
 *      1 recv echo
 */
int do_icmp(struct sockaddr_in *dest_addr, struct ping_result *result, int seq)
{
	int packetsize;
	char sendpacket[PACKET_SIZE];
	char recvpacket[PACKET_SIZE];
	int sockfd;
	int ttl_val=64;
	int size=50*1024;
	struct timeval tv_out; 
	struct timespec time_start, time_end;

	int recvlen;
	socklen_t fromlen;
	struct sockaddr_in from;
	int ret;

	sockfd = socket(AF_INET, SOCK_RAW, IPPROTO_ICMP); 
	if(sockfd<0) 
	{ 
		printf("\nSocket file descriptor not received!!\n"); 
		return 0; 
	} 
	// set socket options at ip to TTL and value to 64, 
	// change to what you want by setting ttl_val 
	if (setsockopt(sockfd, SOL_IP, IP_TTL, 
			&ttl_val, sizeof(ttl_val)) != 0) 
	{ 
		printf("\nSetting socket options to TTL failed!\n"); 
		close(sockfd);
		return 0; 
	} 
	/*扩大套接字接收缓冲区到50K这样做主要为了减小接收缓冲区溢出的
      的可能性,若无意中ping一个广播地址或多播地址,将会引来大量应答*/
    if (0 != setsockopt(sockfd,SOL_SOCKET,SO_RCVBUF,&size,sizeof(size) ))
    {
		printf("\nSetting socket options to SO_RCVBUF failed!\n"); 
		close(sockfd);
		return 0; 
	}

	// setting timeout of recv setting
	tv_out.tv_sec = RECV_TIMEOUT; 
	tv_out.tv_usec = 0; 
	if (0 != setsockopt(sockfd, SOL_SOCKET, SO_RCVTIMEO, (const char*)&tv_out, sizeof tv_out))
	{
		printf("\nSetting socket options to SO_RCVTIMEO failed!\n"); 
		close(sockfd);
		return 0; 
	}
	
	packetsize=pack(seq, sendpacket); /*设置ICMP报头*/

	clock_gettime(CLOCK_MONOTONIC, &time_start); 
    if( sendto(sockfd,sendpacket,packetsize,0,
               (struct sockaddr *)dest_addr,sizeof(*dest_addr) )<0  )
    {       
    	perror("sendto error");
		close(sockfd);
		return 0; 
    }
	printf("DEBUG:%d\n", __LINE__);
	/* recv */
	if( (recvlen=recvfrom(sockfd,recvpacket,sizeof(recvpacket),0,
                                (struct sockaddr *)&from,&fromlen)) <0)
    {       
    	if(errno==EINTR);  //TODO signal ?
        perror("recvfrom error");
        close(sockfd);
		return 0;
    }
    clock_gettime(CLOCK_MONOTONIC, &time_end); 
	printf("DEBUG:%d\n", __LINE__);						
    close(sockfd);
	ret =  unpack(recvpacket, recvlen, result);
	if(ret < 0) {
		return 0;
	}
	//time
	tv_sub(&time_end, &time_start);
	result->rtt = time_end.tv_sec * 1000 + ((double)(time_end.tv_nsec / 1000))/1000;
	
	return 1;
}

int main(int argc,char *argv[])
{
	in_addr_t  inaddr;
	struct hostent *host = NULL;
	struct sockaddr_in dest_addr;
	struct ping_result result = {0};
	int ret;

	bzero(&dest_addr,sizeof(dest_addr));
    dest_addr.sin_family=AF_INET;
	printf("DEBUG:%d\n", __LINE__);
	/*判断是主机名还是ip地址*/
    if( (inaddr=inet_addr(argv[1])) == INADDR_NONE)
    {       
    	if((host=gethostbyname(argv[1]) )==NULL) /*是主机名*/
        {       
             perror("gethostbyname error");
             printf("====result: no resolve\n");
			 return 0;
        }
		
        memcpy( (char *)&dest_addr.sin_addr,host->h_addr,host->h_length);
   }
   else    /*是ip地址*/
        memcpy( (char *)&dest_addr.sin_addr,(char *)&inaddr, sizeof(in_addr_t));
	printf("DEBUG:%d\n", __LINE__);
   ret = do_icmp(&dest_addr, &result, 1);
   if(ret ) {
		printf("====result: get respone; time:%.3f ms ; ttl:%d\n", result.rtt, result.ttl);
   } else {
		printf("====result: lost\n");
   }

   return 0;
}

