cmake_minimum_required (VERSION 2.6)
project (ka9q)

# For now, until the include files are converted to use
# cmake generated host configurations
set(CMAKE_C_FLAGS "-DHOST_BSD -Wall")

INCLUDE(CheckIncludeFiles)
CHECK_INCLUDE_FILES(net/if_tap.h HAVE_NET_IF_TAP_H)
CHECK_INCLUDE_FILES(net/if_tun.h HAVE_NET_IF_TUN_H)

configure_file(${CMAKE_CURRENT_LIST_DIR}/config.h.in
  ${CMAKE_CURRENT_BINARY_DIR}/config.h)
add_definitions(-DHAVE_CONFIG_H)

find_package(Curses REQUIRED)
include_directories(${CURSES_INCLUDE_DIR})

find_package(Threads REQUIRED)

add_library(clients telnet.c ftpcli.c finger.c smtpcli.c hop.c tip.c nntpcli.c bootp.c popcli.c lterm.c)
add_library(servers ttylink.c ftpserv.c smisc.c smtpserv.c fingerd.c mailbox.c rewrite.c bmutil.c forward.c tipmail.c bootpd.c bootpdip.c bootpcmd.c popserv.c tnserv.c)
add_library(internet tcpcmd.c tcpsock.c tcpuser.c tcptimer.c tcpout.c tcpin.c tcpsubr.c tcphdr.c udpcmd.c udpsock.c udp.c udphdr.c domain.c domhdr.c ripcmd.c rip.c ipcmd.c ipsock.c ip.c iproute.c iphdr.c icmpcmd.c ping.c icmp.c icmpmsg.c icmphdr.c arpcmd.c arp.c arphdr.c netuser.c sim.c)
# add_library(ipsec ipsec.c esp.c deskey.c des3port.c desport.c desspa.c ah.c)
add_library(ax25 ax25cmd.c axsock.c ax25user.c ax25.c axheard.c lapbtime.c lapb.c kiss.c ax25subr.c ax25hdr.c ax25mail.c axip.c)
add_library(netrom nrcmd.c nrsock.c nr4user.c nr4timer.c nr4.c nr4subr.c nr4hdr.c nr3.c nrs.c nrhdr.c nr4mail.c)
add_library(ppp asy.c asy_unix.c ppp.c pppcmd.c pppfsm.c ppplcp.c ppppap.c pppipcp.c pppdump.c slhc.c slhcdump.c slip.c sppp.c)
add_library(netinet ftpsubr.c sockcmd.c sockuser.c locsock.c socket.c sockutil.c iface.c timer.c ttydriv.c cmdparse.c mbuf.c misc.c pathname.c files.c kernel.c ksubr_unix.c wildmat.c devparam.c stdio.c ahdlc.c crc.c md5c.c errno.c errlst.c getopt.c)
add_library(dump trace.c enetdump.c arcdump.c kissdump.c ax25dump.c arpdump.c nrdump.c ipdump.c icmpdump.c udpdump.c tcpdump.c ripdump.c)
add_library(unix ksubr_unix.c timer_unix.c display_crs.c unix.c dirutil_unix.c enet.c)

if (HAVE_NET_IF_TAP_H)
  add_library(tap tapdrvr.c)
endif()

if (HAVE_NET_IF_TUN_H)
  add_library(tun tundrvr.c)
endif()

add_executable(net main.c config.c version.c session.c)
target_link_libraries(net clients servers internet ax25 netrom ppp netinet dump unix)
target_link_libraries(net ${CURSES_LIBRARIES} ${CMAKE_THREAD_LIBS_INIT})
