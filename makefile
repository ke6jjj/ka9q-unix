#
#	Makefile for KA9Q TCP/IP package for PC clones with DJGPP
#
# parameters for typical IBM-PC installation
#
CC= gcc
RM= del
LIB= ar
CFLAGS= -I. -O -g
LFLAGS=


# List of libraries

LIBS = clients.a servers.a internet.a \
	ppp.a netrom.a ax25.a dump.a net.a pc.a

# Library object file lists
CLIENTS= telnet.o ftpcli.o finger.o smtpcli.o hop.o tip.o \
	dialer.o nntpcli.o bootp.o popcli.o lterm.o

SERVERS= ttylink.o ftpserv.o smisc.o smtpserv.o \
        fingerd.o mailbox.o rewrite.o bmutil.o forward.o tipmail.o \
	bootpd.o bootpdip.o bootpcmd.o popserv.o tnserv.o

INTERNET= tcpcmd.o tcpsock.o tcpuser.o \
	tcptimer.o tcpout.o tcpin.o tcpsubr.o tcphdr.o \
	udpcmd.o udpsock.o udp.o udphdr.o \
	domain.o domhdr.o \
	ripcmd.o rip.o \
	ipcmd.o ipsock.o ip.o iproute.o iphdr.o \
	icmpcmd.o ping.o icmp.o icmpmsg.o icmphdr.o \
	arpcmd.o arp.o arphdr.o \
	netuser.o sim.o

IPSEC=	ipsec.o esp.o deskey.o des3port.o desport.o desspa.o ah.o

AX25=	ax25cmd.o axsock.o ax25user.o ax25.o \
	axheard.o lapbtime.o \
	lapb.o kiss.o ax25subr.o ax25hdr.o ax25mail.o

NETROM=	nrcmd.o nrsock.o nr4user.o nr4timer.o nr4.o nr4subr.o \
	nr4hdr.o nr3.o nrs.o nrhdr.o nr4mail.o

PPP=	asy.o ppp.o pppcmd.o pppfsm.o ppplcp.o \
	ppppap.o pppipcp.o pppdump.o \
	slhc.o slhcdump.o slip.o sppp.o

NET=	view.o ftpsubr.o sockcmd.o sockuser.o locsock.o socket.o \
	sockutil.o iface.o timer.o ttydriv.o cmdparse.o \
	mbuf.o misc.o pathname.o audit.o files.o \
	kernel.o ksubr.o alloc.o getopt.o wildmat.o \
	devparam.o stdio.o format.o ahdlc.o crc.o md5c.o

DUMP= 	trace.o enetdump.o arcdump.o \
	kissdump.o ax25dump.o arpdump.o nrdump.o \
	ipdump.o icmpdump.o udpdump.o tcpdump.o ripdump.o

PC=	random.o display.o pc.o dirutil.o pktdrvr.o sim.o enet.o \
	n8250.o arcnet.o kbraw.o sb.o dma.o gormcb.o gopint.o

DSP=	fsk.o mdb.o qpsk.o fft.o r4bf.o fano.o tab.o

all:	net.exe

debug:	net
	gdb net

disk:	net.exe
	pklite net.exe
	copy net.exe a:

makelist.exe: makelist.o getopt.o
	$(CC) $(MODEL) $**

net.exe: net
	coff2exe net

net: main.o config.o version.o session.o $(LIBS)
	$(CC) $(LFLAGS) -o net main.o config.o version.o session.o $(LIBS) -lpc

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

srcrcs.zip:
	-pkzip -urp srcrcs.zip makefile turboc.cfg dodeps.sh makefile.%v *.c%v *.h%v *.s%v 

src.zip:
	-pkzip -u src.zip makefile turboc.cfg dodeps.sh *.c *.h *.s

clean:	nul
	$(RM) *.a
	$(RM) *.o
	$(RM) *.exe
	$(RM) *.sym
