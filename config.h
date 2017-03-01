#ifndef	_CONFIG_H
#define	_CONFIG_H

/* Software options */
#define	MAILBOX		1	/* Include SM0RGV mailbox server */
#define	NNTP		1	/* Netnews client */
#define	SERVERS		1	/* Include TCP servers */
#define	SMTP		1	/* Include SMTP server */
#define	TRACE		1	/* Include packet tracing code */
#define	RIP		1	/* Include RIP routing */
#define	HOPCHECK	1	/* IP path tracing command */
#undef	DIALER			/* SLIP redial code */
#define	NRS		1	/* NET/ROM async interface */
#define	NETROM		1	/* NET/ROM network support */
#define	LZW		1	/* LZW-compressed sockets */
#define	SLIP		1	/* Serial line IP on built-in ports */
#define	PPP		1	/* Point-to-Point Protocol code */
#define	VJCOMPRESS	1	/* Van Jacobson TCP compression for SLIP */
#undef	TRACEBACK		/* Stack traceback code */
#define	LOCSOCK		1	/* Local loopback sockets */
#define	SCROLLBACK	1000	/* Default lines in session scrollback file */

#undef	IPSEC			/* IP network layer security functions */

/* Software tuning parameters */
#define	MTHRESH		8192	/* Default memory threshold */
#define	NSESSIONS	20	/* Number of interactive clients */
#define DEFNSOCK	100	/* Default number of sockets */
#define	DEFNFILES	128	/* Default number of kopen files */

/* Hardware driver options */
#undef	SOUND			/* Soundblaster 16 */
#undef	ARCNET			/* ARCnet via PACKET driver */
#define	KISS		1	/* KISS TNC code */
#undef	HS			/* High speed (56kbps) modem driver */
#undef	HAPN			/* Hamilton Area Packet Network driver code */
#undef	EAGLE			/* Eagle card driver */
#undef	PI			/* PI card driver */
#undef	PACKET			/* FTP Software's Packet Driver interface */
#undef	PC100			/* PAC-COM PC-100 driver code */
#undef	APPLETALK		/* Appletalk interface (Macintosh) */
#undef	DRSI			/* DRSI PCPA slow-speed driver */
#undef	SCC			/* PE1CHL generic scc driver */
#define	ASY		1	/* Asynch driver code */
#undef	SLFP			/* SLFP packet driver class supported */
#undef	KSP			/* Kitchen Sink Protocol */

#if defined(NRS) && !defined(NETROM)
#define	NETROM		1	/* NRS implies NETROM */
#endif

#if (defined(HS)||defined(NETROM)||defined(KISS)||defined(HAPN)||defined(EAGLE)||defined(PC100)||defined(PI)||defined(SCC))
#define	AX25		1	/* AX.25 subnet code */
#endif

#if (defined(ARCNET) || defined(SLFP)) && !defined(PACKET)
#define	PACKET		1	/* FTP Software's Packet Driver interface */
#endif

#if (defined(PC_EC) || defined(PACKET))
#define	ETHER	1		/* Generic Ethernet code */
#endif

#if defined(CMDA_DM) && !defined(VJCOMPRESS)
#define VJCOMPRESS	1	/* Van Jacobson TCP compression for SLIP */
#endif

#endif	/* _CONFIG_H */
