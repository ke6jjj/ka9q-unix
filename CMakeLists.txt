cmake_minimum_required (VERSION 2.6)
project (ka9q)

# For now, until the include files are converted to use
# cmake generated host configurations
set(CMAKE_C_FLAGS "-DHOST_BSD -Wall")

INCLUDE(CheckIncludeFiles)

# FreeBSD-10 uses u_char in if_tap.h / if_tun.h; so include if
# if it exists.
CHECK_INCLUDE_FILES(sys/types.h HAVE_SYS_TYPES_H)
if (HAVE_SYS_TYPES_H)
  CHECK_INCLUDE_FILES("sys/types.h;net/if_tap.h" HAVE_NET_IF_TAP_H)
  CHECK_INCLUDE_FILES("sys/types.h;net/if_tun.h" HAVE_NET_IF_TUN_H)
else()
  CHECK_INCLUDE_FILES(net/if_tap.h HAVE_NET_IF_TAP_H)
  CHECK_INCLUDE_FILES(net/if_tun.h HAVE_NET_IF_TUN_H)
endif()

configure_file(${CMAKE_CURRENT_LIST_DIR}/cmake_config.h.in
  ${CMAKE_CURRENT_BINARY_DIR}/cmake_config.h)
add_definitions(-DUSE_CMAKE_CONFIG_H)
include_directories(${CMAKE_CURRENT_BINARY_DIR})

find_package(Curses REQUIRED)
include_directories(${CURSES_INCLUDE_DIR})

find_package(Threads REQUIRED)

add_library(clients telnet.c ftpcli.c finger.c smtpcli.c hop.c tip.c nntpcli.c bootp.c popcli.c lterm.c)
add_library(servers ttylink.c ftpserv.c smisc.c smtpserv.c fingerd.c mailbox.c rewrite.c bmutil.c forward.c tipmail.c bootpd.c bootpdip.c bootpcmd.c popserv.c tnserv.c)
add_library(internet tcpcmd.c tcpsock.c tcpuser.c tcptimer.c tcpout.c tcpin.c tcpsubr.c tcphdr.c udpcmd.c udpsock.c udp.c udphdr.c domain.c domhdr.c ripcmd.c rip.c ipcmd.c ipsock.c ip.c iproute.c iphdr.c icmpcmd.c ping.c icmp.c icmpmsg.c icmphdr.c netuser.c sim.c)
# add_library(ipsec ipsec.c esp.c deskey.c des3port.c desport.c desspa.c ah.c)
add_library(ax25 net/ax25/ax25cmd.c net/ax25/axsock.c net/ax25/ax25user.c net/ax25/ax25.c net/ax25/axheard.c net/ax25/lapbtime.c net/ax25/lapb.c net/ax25/kiss.c net/ax25/ax25subr.c net/ax25/ax25hdr.c net/ax25/ax25mail.c net/ax25/axip.c)

add_library(netrom net/netrom/nrcmd.c net/netrom/nrsock.c net/netrom/nr4user.c net/netrom/nr4timer.c net/netrom/nr4.c net/netrom/nr4subr.c net/netrom/nr4hdr.c net/netrom/nr3.c net/netrom/nrs.c net/netrom/nrhdr.c net/netrom/nr4mail.c)

# Asynchronous serial (over device, TCP/IP)
add_library(asy asy.c asy_unix.c)

# Asynchronous PPP support
add_library(ppp net/ppp/ppp.c net/ppp/pppcmd.c net/ppp/pppfsm.c net/ppp/ppplcp.c net/ppp/ppppap.c net/ppp/pppipcp.c net/ppp/pppdump.c)

# SLHC - TCP/IP header compression (used in PPP, SPPP)
add_library(slhc net/slhc/slhc.c net/slhc/slhcdump.c)

# SLIP support
add_library(slip net/slip/slip.c)

# Synchronous serial PPP over HDLC
add_library(sppp net/sppp/sppp.c)

# ARP
add_library(arp net/arp/arp.c net/arp/arpcmd.c net/arp/arpdump.c net/arp/arphdr.c)

# Ethernet networking
add_library(enet net/enet/enet.c net/enet/enetdump.c)

add_library(netinet ftpsubr.c sockcmd.c sockuser.c locsock.c socket.c sockutil.c iface.c timer.c ttydriv.c cmdparse.c mbuf.c misc.c pathname.c files.c kernel.c wildmat.c devparam.c stdio.c ahdlc.c crc.c md5c.c errno.c errlst.c getopt.c)
add_library(dump trace.c net/ax25/kissdump.c net/ax25/ax25dump.c net/netrom/nrdump.c ipdump.c icmpdump.c udpdump.c tcpdump.c ripdump.c)
add_library(unix unix/ksubr_unix.c unix/timer_unix.c unix/display_crs.c unix/unix.c unix/dirutil_unix.c unix/ksubr_unix.c unix_socket.c)

if (HAVE_NET_IF_TAP_H)
  add_library(tap tapdrvr.c)
endif()

if (HAVE_NET_IF_TUN_H)
  add_library(tun tundrvr.c)
endif()

add_executable(net main.c config.c version.c session.c)
target_link_libraries(net clients servers internet ax25 netrom ppp netinet dump unix asy)
target_link_libraries(net ppp sppp enet arp slip slhc)
target_link_libraries(net ${CURSES_LIBRARIES} ${CMAKE_THREAD_LIBS_INIT})
if (HAVE_NET_IF_TAP_H)
  target_link_libraries(net tap)
endif()
if (HAVE_NET_IF_TUN_H)
  target_link_libraries(net tun)
endif()
