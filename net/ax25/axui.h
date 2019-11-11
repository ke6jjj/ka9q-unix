#ifndef	_KA9Q_AX25UI_H
#define	_KA9Q_AX25UI_H

#include "global.h"
#include "net/core/mbuf.h"
#include "net/core/iface.h"
#include "core/timer.h"

#include "net/ax25/ax25.h"

/*
 * For now this is just a placeholder until a full
 * ax25 ui socket table and mux/demux is written.
 */
struct ax25ui_cb {
  int unused;
};

#endif /* _KA9Q_AX25UI_H */
