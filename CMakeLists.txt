cmake_minimum_required (VERSION 2.6)
project (ka9q)

# For now, until the include files are converted to use
# cmake generated host configurations
set(CMAKE_C_FLAGS "-DHOST_BSD -Wall")

INCLUDE(CheckIncludeFiles)
include(CheckFunctionExists)

set(X_CMAKE_SYSTEM_NAME ${CMAKE_SYSTEM_NAME})

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

CHECK_FUNCTION_EXISTS (srandomdev HAVE_SRANDOMDEV)
CHECK_FUNCTION_EXISTS (funopen HAVE_FUNOPEN)

configure_file(${CMAKE_CURRENT_LIST_DIR}/cmake_config.h.in
  ${CMAKE_CURRENT_BINARY_DIR}/cmake_config.h)
add_definitions(-DUSE_CMAKE_CONFIG_H)
include_directories(${CMAKE_CURRENT_BINARY_DIR})

find_package(Curses REQUIRED)
include_directories(${CURSES_INCLUDE_DIR})

include_directories(${CMAKE_SOURCE_DIR})

find_package(Threads REQUIRED)

add_library(lib_std lib/std/stdio.c lib/std/errno.c lib/std/errlst.c)
add_library(lib_smtp lib/smtp/rewrite.c)
if (NOT HAVE_FUNOPEN)
add_library(lib_std_format lib/std/format.c)
endif()

add_library(clients telnet.c cmd/ftpcli/ftpcli.c cmd/finger/finger.c
  cmd/smtpcli/smtpcli.c cmd/inet/hop.c cmd/tip/tip.c cmd/nntpcli/nntpcli.c
  service/bootp/bootp.c cmd/popcli/popcli.c cmd/lterm/lterm.c
  cmd/sockcmd/sockcmd.c)

add_library(servers service/ttylink/ttylink.c service/ftp/ftpserv.c
  service/smisc/smisc.c service/smtp/smtpserv.c service/fingerd/fingerd.c
  mailbox.c bmutil.c forward.c tipmail.c service/bootpd/bootpd.c
  service/bootpd/bootpdip.c cmd/bootpcmd/bootpcmd.c service/pop/popserv.c
  service/telnetd/tnserv.c)

add_library(internet cmd/inet/tcpcmd.c net/inet/tcpsock.c net/inet/tcpuser.c
  net/inet/tcptimer.c net/inet/tcpout.c net/inet/tcpin.c net/inet/tcpsubr.c
  net/inet/tcphdr.c cmd/inet/udpcmd.c net/inet/udpsock.c net/inet/udp.c
  net/inet/udphdr.c net/dns/domain.c net/dns/domhdr.c cmd/rip/ripcmd.c
  service/rip/rip.c cmd/inet/ipcmd.c net/inet/ipsock.c net/inet/ip.c
  net/inet/iproute.c net/inet/iphdr.c cmd/inet/icmpcmd.c net/inet/ping.c
  net/inet/icmp.c net/inet/icmpmsg.c net/inet/icmphdr.c lib/inet/netuser.c
  net/inet/sim.c)

# add_library(ipsec ipsec.c esp.c deskey.c des3port.c desport.c desspa.c ah.c)
add_library(ax25 cmd/ax25/ax25cmd.c net/ax25/axsock.c net/ax25/ax25user.c
  net/ax25/ax25.c net/ax25/axheard.c net/ax25/lapbtime.c net/ax25/lapb.c
  net/ax25/kiss.c net/ax25/ax25subr.c net/ax25/ax25hdr.c net/ax25/ax25mail.c
  net/ax25/axip.c)

add_library(netrom cmd/netrom/nrcmd.c net/netrom/nrsock.c
  net/netrom/nr4user.c net/netrom/nr4timer.c net/netrom/nr4.c
  net/netrom/nr4subr.c net/netrom/nr4hdr.c net/netrom/nr3.c
  net/netrom/nrs.c net/netrom/nrhdr.c net/netrom/nr4mail.c)

# Asynchronous PPP support
add_library(ppp net/ppp/ppp.c cmd/ppp/pppcmd.c net/ppp/pppfsm.c
  net/ppp/ppplcp.c net/ppp/ppppap.c net/ppp/pppipcp.c cmd/pppdump/pppdump.c)

# SLHC - TCP/IP header compression (used in PPP, SPPP)
add_library(slhc net/slhc/slhc.c cmd/slhcdump/slhcdump.c)

# SLIP support
add_library(slip net/slip/slip.c)

# Synchronous serial PPP over HDLC
add_library(sppp net/sppp/sppp.c net/sppp/ahdlc.c)

# ARP
add_library(arp net/arp/arp.c cmd/arp/arpcmd.c net/arp/arpdump.c net/arp/arphdr.c)

# Ethernet networking
add_library(enet net/enet/enet.c net/enet/enetdump.c)

add_library(netinet lib/ftp/ftpsubr.c files.c)
add_library(lib_util lib/util/cmdparse.c lib/util/crc.c lib/util/getopt.c
  lib/util/md5c.c lib/util/misc.c lib/util/pathname.c lib/util/wildmat.c)

add_library(dump net/ax25/kissdump.c net/ax25/ax25dump.c net/netrom/nrdump.c
  cmd/inet/ipdump.c cmd/inet/icmpdump.c cmd/inet/udpdump.c cmd/inet/tcpdump.c
  cmd/rip/ripdump.c)

add_library(unix unix/ksubr_unix.c unix/timer_unix.c unix/display_crs.c
  unix/unix.c unix/dirutil_unix.c unix/ksubr_unix.c unix/unix_socket.c
  unix/asy_unix.c)

add_library(core core/asy.c core/devparam.c core/kernel.c core/locsock.c
  core/session.c core/socket.c core/sockuser.c core/sockutil.c core/timer.c
  core/trace.c core/ttydriv.c)
add_library(net_core net/core/iface.c net/core/mbuf.c cmd/net/iface.c)

if (HAVE_NET_IF_TAP_H)
  add_library(tap net/tap/tapdrvr.c)
endif()

if (HAVE_NET_IF_TUN_H)
  add_library(tun net/tun/tundrvr.c)
endif()

add_executable(ka9q_net main.c config.c version.c)
target_link_libraries(ka9q_net clients servers internet ax25 netrom ppp)
target_link_libraries(ka9q_net netinet dump unix)
target_link_libraries(ka9q_net ppp sppp enet arp slip slhc lib_std lib_smtp)
target_link_libraries(ka9q_net core net_core lib_util)
target_link_libraries(ka9q_net ${CURSES_LIBRARIES} ${CMAKE_THREAD_LIBS_INIT})
if (HAVE_NET_IF_TAP_H)
  target_link_libraries(ka9q_net tap)
endif()
if (HAVE_NET_IF_TUN_H)
  target_link_libraries(ka9q_net tun)
endif()

if (NOT HAVE_FUNOPEN)
  target_link_libraries(ka9q_net lib_std_format)
endif()
