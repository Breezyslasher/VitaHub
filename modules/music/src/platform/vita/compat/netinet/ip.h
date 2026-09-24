/**
 * Minimal <netinet/ip.h> for PS Vita (vitasdk newlib has none).
 * Classic BSD IPv4 header definitions, as used by usrsctp's userspace stack.
 */

#ifndef VITA_COMPAT_NETINET_IP_H
#define VITA_COMPAT_NETINET_IP_H

#include <sys/types.h>
#include <netinet/in.h>
#include <netinet/in_systm.h>

#define IPVERSION 4

/*
 * Structure of an internet header, naked of options.
 */
struct ip {
#if defined(__BYTE_ORDER__) && (__BYTE_ORDER__ == __ORDER_BIG_ENDIAN__)
	u_int8_t ip_v : 4,  /* version */
	         ip_hl : 4; /* header length */
#else
	u_int8_t ip_hl : 4, /* header length */
	         ip_v : 4;  /* version */
#endif
	u_int8_t ip_tos;      /* type of service */
	u_int16_t ip_len;     /* total length */
	u_int16_t ip_id;      /* identification */
	u_int16_t ip_off;     /* fragment offset field */
#define IP_RF 0x8000      /* reserved fragment flag */
#define IP_DF 0x4000      /* dont fragment flag */
#define IP_MF 0x2000      /* more fragments flag */
#define IP_OFFMASK 0x1fff /* mask for fragmenting bits */
	u_int8_t ip_ttl;      /* time to live */
	u_int8_t ip_p;        /* protocol */
	u_int16_t ip_sum;     /* checksum */
	struct in_addr ip_src, ip_dst; /* source and dest address */
};

#define IP_MAXPACKET 65535 /* maximum packet size */

/*
 * Definitions for DiffServ Codepoints as per RFC2474 and ECN as per RFC3168
 */
#define IPTOS_DSCP_CS0 0x00
#define IPTOS_DSCP_CS1 0x20
#define IPTOS_DSCP_AF11 0x28
#define IPTOS_DSCP_AF12 0x30
#define IPTOS_DSCP_AF13 0x38
#define IPTOS_DSCP_CS2 0x40
#define IPTOS_DSCP_AF21 0x48
#define IPTOS_DSCP_AF22 0x50
#define IPTOS_DSCP_AF23 0x58
#define IPTOS_DSCP_CS3 0x60
#define IPTOS_DSCP_AF31 0x68
#define IPTOS_DSCP_AF32 0x70
#define IPTOS_DSCP_AF33 0x78
#define IPTOS_DSCP_CS4 0x80
#define IPTOS_DSCP_AF41 0x88
#define IPTOS_DSCP_AF42 0x90
#define IPTOS_DSCP_AF43 0x98
#define IPTOS_DSCP_CS5 0xa0
#define IPTOS_DSCP_EF 0xb8
#define IPTOS_DSCP_CS6 0xc0
#define IPTOS_DSCP_CS7 0xe0

#define IPTOS_ECN_NOTECT 0x00 /* not-ECT */
#define IPTOS_ECN_ECT1 0x01   /* ECN-capable transport (1) */
#define IPTOS_ECN_ECT0 0x02   /* ECN-capable transport (0) */
#define IPTOS_ECN_CE 0x03     /* congestion experienced */
#define IPTOS_ECN_MASK 0x03   /* ECN field mask */

#define IPTOS_PREC_NETCONTROL 0xe0
#define IPTOS_PREC_INTERNETCONTROL 0xc0
#define IPTOS_PREC_CRITIC_ECP 0xa0
#define IPTOS_PREC_FLASHOVERRIDE 0x80
#define IPTOS_PREC_FLASH 0x60
#define IPTOS_PREC_IMMEDIATE 0x40
#define IPTOS_PREC_PRIORITY 0x20
#define IPTOS_PREC_ROUTINE 0x00

/*
 * Definitions for options.
 */
#define IPOPT_EOL 0     /* end of option list */
#define IPOPT_NOP 1     /* no operation */
#define IPOPT_RR 7      /* record packet route */
#define IPOPT_TS 68     /* timestamp */
#define IPOPT_SECURITY 130
#define IPOPT_LSRR 131  /* loose source route */
#define IPOPT_SATID 136
#define IPOPT_SSRR 137  /* strict source route */

#define MAXTTL 255      /* maximum time to live */
#define IPDEFTTL 64     /* default ttl, from RFC 1340 */
#define IPFRAGTTL 60    /* time to live for frags */
#define IPTTLDEC 1      /* subtracted when forwarding */

#endif /* VITA_COMPAT_NETINET_IP_H */
