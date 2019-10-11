#
#	Makefile for KA9Q TCP/IP package for UNIX
#
# parameters for typical UNIX installation
#
CC?= cc
RM?= rm -f
LIB?= ar
CFLAGS= -g -DHOST_BSD -Werror -Wno-int-to-void-pointer-cast -O3
# This was enabled by default, maintain backwards compatibility for now
CFLAGS+= -DHAVE_NET_IF_TAP_H
CFLAGS+= -DHAVE_NET_IF_TUN_H
LFLAGS= -lcurses


# List of libraries

LIBS = clients.a servers.a internet.a \
	ppp.a netrom.a ax25.a dump.a net.a unix.a

# Library object file lists
# UNIX: port dialer, needs to be less bound to the 8250 UART (dialer.o)
# UNIX: more/view needs to be less conio.h bound NET(view.o)
# UNIX: mbuf audit is very DOS/malloc specific NET(audit.o)
# UNIX: alloc routines are not UNIX compatible NET(alloc.o)
# UNIX: NET(format.o). Not for HAVE_FUNOPEN.
CLIENTS= telnet.o cmd/ftpcli/ftpcli.o cmd/finger/finger.o cmd/smtpcli/smtpcli.o cmd/inet/hop.o tip.o \
	cmd/nntpcli/nntpcli.o service/bootp/bootp.o cmd/popcli/popcli.o lterm.o

SERVERS= ttylink.o service/ftp/ftpserv.o service/smisc/smisc.o service/smtp/smtpserv.o \
	service/fingerd/fingerd.o mailbox.o rewrite.o bmutil.o forward.o tipmail.o \
	service/bootpd/bootpd.o service/bootpd/bootpdip.o cmd/bootpdcmd/bootpcmd.o service/pop/popserv.o tnserv.o

INTERNET= cmd/inet/tcpcmd.o net/inet/tcpsock.o net/inet/tcpuser.o \
	net/inet/tcptimer.o net/inet/tcpout.o net/inet/tcpin.o net/inet/tcpsubr.o net/inet/tcphdr.o \
	cmd/inet/udpcmd.o net/inet/udpsock.o net/inet/udp.o net/inet/udphdr.o \
	net/dns/domain.o net/dns/domhdr.o \
	cmd/rip/ripcmd.o service/rip/rip.o \
	cmd/inet/ipcmd.o net/inet/ipsock.o net/inet/ip.o net/inet/iproute.o net/inet/iphdr.o \
	cmd/inet/icmpcmd.o net/inet/ping.o net/inet/icmp.o net/inet/icmpmsg.o net/inet/icmphdr.o \
	net/arp/arpcmd.o net/arp/arp.o net/arp/arphdr.o \
	lib/inet/netuser.o net/inet/sim.o

IPSEC=	ipsec.o esp.o deskey.o des3port.o desport.o desspa.o ah.o

AX25=	net/ax25/ax25cmd.o net/ax25/axsock.o net/ax25/ax25user.o net/ax25/ax25.o \
	net/ax25/axheard.o net/ax25/lapbtime.o net/ax25/lapb.o \
	net/ax25/kiss.o net/ax25/ax25subr.o net/ax25/ax25hdr.o \
	net/ax25/ax25mail.o net/ax25/axip.o

NETROM=	net/netrom/nrcmd.o net/netrom/nrsock.o net/netrom/nr4user.o \
	net/netrom/nr4timer.o net/netrom/nr4.o net/netrom/nr4subr.o \
	net/netrom/nr4hdr.o net/netrom/nr3.o net/netrom/nrs.o \
	net/netrom/nrhdr.o net/netrom/nr4mail.o

PPP=	asy.o asy_unix.o net/ppp/ppp.o net/ppp/pppcmd.o net/ppp/pppfsm.o \
	net/ppp/ppplcp.o net/ppp/ppppap.o net/ppp/pppipcp.o cmd/pppdump/pppdump.o \
	net/slhc/slhc.o cmd/slhcdump/slhcdump.o net/slip/slip.o net/sppp/sppp.o

NET=	lib/ftp/ftpsubr.o sockcmd.o sockuser.o locsock.o socket.o \
	sockutil.o iface.o timer.o ttydriv.o cmdparse.o \
	mbuf.o lib/util/misc.o lib/util/pathname.o files.o \
	kernel.o lib/util/wildmat.o \
	devparam.o stdio.o net/sppp/ahdlc.o lib/util/crc.o lib/util/md5c.o errno.o \
	errlst.o lib/util/getopt.o

DUMP= 	trace.o net/enet/enetdump.o \
	net/ax25/kissdump.o net/ax25/ax25dump.o net/arp/arpdump.o \
	net/netrom/nrdump.o cmd/inet/ipdump.o cmd/inet/icmpdump.o cmd/inet/udpdump.o cmd/inet/tcpdump.o cmd/rip/ripdump.o

UNIX=	unix/ksubr_unix.o unix/timer_unix.o unix/display_crs.o unix/unix.o unix/dirutil_unix.o \
	unix/ksubr_unix.o net/enet/enet.o unix_socket.o

UNIX+=	net/tap/tapdrvr.o net/tun/tundrvr.o

DSP=	fsk.o mdb.o qpsk.o fft.o r4bf.o fano.o tab.o

all:	ka9q_net

debug:	ka9q_net
	gdb ka9q_net

ka9q_net: main.o config.o version.o session.o $(LIBS)
	$(CC) $(LFLAGS) -o $@ main.o config.o version.o session.o $(LIBS)

mkpass.exe: mkpass.o md5c.o
	$(CC) $(MODEL) -emkpass $**

# build DES SP table
desspa.c: gensp
	gensp c > desspa.c
gensp: gensp.c
	gcc -o gensp gensp.c
	coff2exe gensp

# FFT subroutines
fft.o: fft.c fft.h
	$(CC) -O4 -ffast-math -DPENTIUM -c fft.c

# Radix 4 FFT in Pentium-optimized assembler
# Is MUCH faster than the C version on the Pentium; is slightly faster
# even on the 486DX4-100
r4bf.o: r4bf.s
	cpp r4bf.s | as -o r4bf.o -

# Library dependencies
ax25.a: $(AX25)
	$(LIB) rs $@ $?
clients.a: $(CLIENTS)
	$(LIB) rs $@ $?
dsp.a: $(DSP)
	$(LIB) rs $@ $?
dump.a: $(DUMP)
	$(LIB) rs $@ $?
internet.a: $(INTERNET)
	$(LIB) rs $@ $?
ipsec.a: $(IPSEC)
	$(LIB) rs $@ $?
net.a: $(NET)
	$(LIB) rs $@ $?
netrom.a: $(NETROM)
	$(LIB) rs $@ $?
pc.a: $(PC)
	$(LIB) rs $@ $?
ppp.a: $(PPP)
	$(LIB) rs $@ $?
servers.a: $(SERVERS)
	$(LIB) rs $@ $?
unix.a: $(UNIX)
	$(LIB) rs $@ $?

srcrcs.zip:
	-pkzip -urp srcrcs.zip makefile turboc.cfg dodeps.sh makefile.%v *.c%v *.h%v *.s%v 

src.zip:
	-pkzip -u src.zip makefile turboc.cfg dodeps.sh *.c *.h *.s

clean:
	$(RM) *.a
	$(RM) *.o
	$(RM) *.exe
	$(RM) *.sym
