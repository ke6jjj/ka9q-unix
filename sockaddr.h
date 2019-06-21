#ifndef _SOCKADDR_H
#define _SOCKADDR_H

/* Berkeley format socket address structures. These things were rather
 * poorly thought out, but compatibility is important (or so they say).
 * Note that all the sockaddr variants must be of the same size, 16 bytes
 * to be specific. Although attempts have been made to account for alignment
 * requirements (notably in sockaddr_ax), porters should check each
 * structure.
 */

/* Generic socket address structure */
struct ksockaddr {
	short sa_family;
	char sa_data[14];
};

/* This is a structure for "historical" reasons (whatever they are) */
struct kin_addr {
	uint32 s_addr;
};

/* Socket address, DARPA Internet style */
struct ksockaddr_in {
	short sin_family;
	unsigned short sin_port;
	struct kin_addr sin_addr;
	char sin_zero[8];
};

#define	SOCKSIZE	(sizeof(struct ksockaddr))
#define MAXSOCKSIZE	SOCKSIZE /* All sockets are of the same size for now */

#endif /* _SOCKADDR_H */
